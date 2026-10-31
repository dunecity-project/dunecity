/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Network/GamePayloadRouter.h>

#include <Network/ContentCompatibility.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/NetworkPacketTypes.h>

#include <GameInitSettings.h>
#include <Campaign/CoopCampaignSession.h>
#include <Campaign/CoopCampaignTransition.h>
#include <Network/GameInitSettingsPolicy.h>
#include <mod/Workshop.h>
#include <Network/OnlineModPolicy.h>
#include <mod/ModManager.h>

#include <Definitions.h>
#include <config.h>
#include <globals.h>
#include <misc/FileSystem.h>
#include <misc/fnkdat.h>
#include <players/QuantBotConfig.h>

#include <cstdio>
#include <filesystem>
#include <limits>
#include <algorithm>

namespace {

/// Coarse bound on a path budget order; Game::handleSetPathBudget applies the exact range.
constexpr Uint32 kMaxPathBudgetOrder = 1000000;
/// Longest start-game countdown accepted from the host (the lobby uses 3 s).
constexpr Uint32 kMaxStartGameCountdownMs = 30000;
/// Longest chat message accepted from a peer.
constexpr std::size_t kMaxChatMessageLength = 512;

/**
    True when the whole payload has been consumed.

    Match control packets are fixed-size records, so anything left over is not a packet this
    build produced. Streams that cannot say how much is left (getRemainingLength() returns
    size_t's maximum) are accepted: there is nothing to compare against there.
*/
bool payloadFullyConsumed(const InputStream& stream) {
    const std::size_t remaining = stream.getRemainingLength();
    return remaining == std::numeric_limits<std::size_t>::max() || remaining == 0;
}

// A refused or corrupt map must not reach the accepted callback or overwrite local history.
bool storeReceivedMap(const GameInitSettings& init, GamePayloadPeer& peer) {
    if(init.getGameType() != GameType::CustomMultiplayer) return true;
    try {
        std::string name;
        if(!NetworkPacketPolicy::sanitizeReceivedMapFilename(init.getFilename(), name)
           || init.getFiledata().size() > NetworkPacketPolicy::kMaxReceivedMapSize)
            throw std::runtime_error("Invalid shared map name or size.");
        if(init.getMapRevisionHash().empty() || init.getMapRevisionManifest().empty())
            throw std::runtime_error("The host did not identify the shared map revision.");
        const auto manifest = Workshop::parseManifest(init.getMapRevisionManifest());
        if(manifest.kind != "map" || manifest.modHash != init.getModRevisionHash())
            throw std::runtime_error("The shared map requires a different mod revision.");
        const auto map = Workshop::receiveMap(name, init.getFiledata(), init.getMapRevisionHash(),
            init.getMapRevisionManifest(), init.getMapRevisionVersion());
        Workshop::installMap(map);
        return true;
    } catch(const std::exception& error) {
        peer.refuse(error.what());
        return false;
    }
}

void handleConfigHash(InputStream& stream, GamePayloadPeer& peer,
                      const GamePayloadContext& context,
                      const NetworkSessionCallbacks& callbacks) {
    const Uint32 peerProtocolVersion = stream.readUint32();
    const std::string gameVersion    = stream.readString();
    const std::string quantBotHash   = stream.readString();
    const std::string objectDataHash = stream.readString();
    const std::string modRevisionHash = stream.readString();

    peer.gameVersion()        = gameVersion;
    peer.quantBotConfigHash() = quantBotHash;
    peer.objectDataHash()     = objectDataHash;
    peer.modRevisionHash() = modRevisionHash;

    const std::string localVersion        = VERSIONSTRING;
    const std::string localQuantBotHash   = getQuantBotConfig().getConfigHash();
    const std::string localObjectDataHash = getObjectDataHash();

    SDL_Log("Config from '%s': protocol %u, version %s", peer.name().c_str(),
            static_cast<unsigned>(peerProtocolVersion), gameVersion.c_str());

    const auto disconnect = [&peer](int cause) { peer.disconnectWithCause(cause); };

    if(rejectIncompatibleNetworkProtocol(peerProtocolVersion, disconnect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Rejecting incompatible protocol from '%s' (peer=%u, local=%u)",
                     peer.name().c_str(), static_cast<unsigned>(peerProtocolVersion),
                     NETWORK_PROTOCOL_VERSION);
        return;
    }

    // Content transfer cannot replace executable simulation code, so a different build is a
    // hard refusal rather than something a mod download could repair.
    if(rejectIncompatibleGameVersion(gameVersion, localVersion, disconnect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Rejecting game version mismatch: peer=%s local=%s",
                     gameVersion.c_str(), localVersion.c_str());
        return;
    }

    ContentCompatibility::Fingerprint local;
    local.gameVersion    = localVersion;
    local.quantBotHash   = localQuantBotHash;
    local.objectDataHash = localObjectDataHash;
    try { local.modRevisionHash = OnlineModPolicy::fingerprint(); }
    catch(const std::exception&) { /* Incomplete fingerprint fails closed below. */ }

    ContentCompatibility::Fingerprint reported;
    reported.gameVersion    = gameVersion;
    reported.quantBotHash   = quantBotHash;
    reported.objectDataHash = objectDataHash;
    reported.modRevisionHash = modRevisionHash;

    // The same rule the host applies again just before it starts. An absent hash is a mismatch
    // here too: a peer that could not fingerprint its own content has not shown that it matches
    // ours, and treating "I do not know" as "we agree" is how an unverified install ends up in a
    // lockstep match.
    std::string reason;
    const ContentCompatibility::Verdict verdict =
        ContentCompatibility::compare(local, reported, peer.name(), reason);

    if(verdict == ContentCompatibility::Verdict::Match) {
        SDL_Log("Config verification passed for '%s'", peer.name().c_str());
    } else {
        SDL_Log("Config check for '%s': %s", peer.name().c_str(), reason.c_str());

        // Only where nothing can repair the difference. On the mesh a mismatch is the expected
        // prelude to a mod transfer, and reporting it as an error would break a working flow.
        if(context.contentMustMatch && verdict == ContentCompatibility::Verdict::Mismatch
           && callbacks.onConfigMismatch && *callbacks.onConfigMismatch) {
            (*callbacks.onConfigMismatch)(reason);
        }
    }

    if(!context.isHost && context.replyToConfigHash) {
        // Always answer, even on a mismatch: the host validates independently.
        peer.replyConfigHash();
    }
}

bool isCampaignController(const coop::Context& context, const std::string& name) {
    for(std::size_t i = 0; i < context.slots.size(); ++i) {
        const auto& slot = context.slots[i];
        if(!context.isAlliedSlot(i) || slot.playerClass != HUMANPLAYERCLASS) continue;
        if(slot.controllers.empty()) {
            if(slot.playerName == name) return true;
        } else if(std::find(slot.controllers.begin(), slot.controllers.end(), name) != slot.controllers.end()) return true;
    }
    return false;
}

bool isSeparateCampaignContinuation(const coop::Context& current, const GameInitSettings& next, bool inGame) {
    if(next.getGameType() == GameType::Invalid) return true;
    if(next.getGameType() != GameType::CustomMultiplayer
       || !ModManager::instance().isTornieFamilyContent(next.getModName())
       || ModManager::instance().getContentBase(next.getModName()) != current.modName) return false;
    const auto context = coop::readContext(next.getFiledata());
    if(!context || context->sessionId != current.sessionId || context->modName != current.modName
       || context->campaign != current.campaign || context->seed != current.seed
       || context->mapLayout != current.mapLayout || (inGame && context->sourceFaction != current.sourceFaction)
       || context->roster != current.roster || context->alliedCapacity != current.alliedCapacity
       || context->archon != current.archon || context->alliedControl != current.alliedControl
       || context->alliedBuilding != current.alliedBuilding || context->easyMode != current.easyMode
       || context->chaosEligible != current.chaosEligible || context->extraEnemyForces != current.extraEnemyForces
       || context->extraHardEnemyForces != current.extraHardEnemyForces
       || context->randomVariants != current.randomVariants
       || (context->stage != current.stage && context->stage != current.stage + 1)
       || context->completedMask != (context->stage == current.stage ? current.completedMask
           : current.completedMask | (1u << (current.stage - 1)))) return false;
    // The host's final lobby snapshot applies agreed faction/seat choices. Unlike a mission
    // advance it must not change progression, but source faction and allied names may change.
    if(!inGame) return context->stage == current.stage && context->completedMask == current.completedMask;
    const auto oldAllies = current.alliedSlots(), newAllies = context->alliedSlots();
    for(std::size_t i = 0; i < oldAllies.size(); ++i) {
        const auto& a = current.slots[oldAllies[i]];
        const auto& b = context->slots[newAllies[i]];
        if(a.house != b.house || a.faction != b.faction || a.color != b.color
           || a.playerName != b.playerName || a.playerClass != b.playerClass
           || a.controllers != b.controllers) return false;
    }
    return true;
}

} // namespace

bool GamePayloadRouter::handle(Uint32 packetType, InputStream& stream, GamePayloadPeer& peer,
                               const GamePayloadContext& context,
                               const NetworkSessionCallbacks& callbacks) {
    switch(packetType) {
        case NETWORKPACKET_SENDGAMEINFO: {
            GameInitSettings gameInitSettings(stream);
            ChangeEventList changeEventList(stream);

            std::string rejectionReason;
            if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(gameInitSettings, rejectionReason)) {
                peer.refuse(rejectionReason.c_str());
                return true;
            }
            std::string mapFilename;
            if(gameInitSettings.getGameType() == GameType::CustomMultiplayer
               && !gameInitSettings.getFiledata().empty()
               && !NetworkPacketPolicy::sanitizeReceivedMapFilename(gameInitSettings.getFilename(), mapFilename)) {
                peer.refuse("unsafe received map filename");
                return true;
            }
            if(!context.allowMapWrite && gameInitSettings.getFiledata().size()
                      > NetworkPacketPolicy::kMaxReceivedMapSize) {
                // On the relay the map text is only ever used from memory, but an absurd size
                // is still a reason to refuse rather than to keep it around.
                peer.refuse("received map exceeds the size limit");
                return true;
            }

            if(context.allowMapWrite && !storeReceivedMap(gameInitSettings, peer)) return true;
            if(callbacks.onGameInfoAccepted) callbacks.onGameInfoAccepted();
            if(callbacks.onReceiveGameInfo && *callbacks.onReceiveGameInfo) {
                (*callbacks.onReceiveGameInfo)(gameInitSettings, changeEventList);
            }
        } return true;

        case NETWORKPACKET_SENDNAME: {
            const std::string newName = stream.readString();

            if(!NetworkPacketPolicy::isAcceptablePlayerName(newName)) {
                peer.refuse("unacceptable player name");
                return true;
            }
            // Identity is bound exactly once per connection. CommandManager resolves a command
            // list to a player by this name, so a later rename would let a peer take over
            // another player's commands.
            if(peer.nameAssigned()) {
                peer.refuse("peer tried to change its established name");
                return true;
            }
            peer.bindName(newName);
        } return true;

        case NETWORKPACKET_CHATMESSAGE: {
            const std::string message = stream.readString();
            if(message.size() > kMaxChatMessageLength) {
                peer.refuse("chat message exceeds the length limit");
                return true;
            }
            if(callbacks.onReceiveChatMessage && *callbacks.onReceiveChatMessage) {
                (*callbacks.onReceiveChatMessage)(peer.name(), message);
            }
        } return true;

        case NETWORKPACKET_CHANGEEVENTLIST: {
            ChangeEventList changeEventList(stream);

            if(context.isHost) {
                // A client only ever seats itself: every lobby slot claim it sends carries its
                // own name. Anything else is a peer trying to move another player around.
                bool foreignSlotClaim = false;
                for(const ChangeEventList::ChangeEvent& changeEvent
                        : changeEventList.changeEventList) {
                    if(changeEvent.eventType
                           == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer
                       && changeEvent.newStringValue != peer.name()) {
                        foreignSlotClaim = true;
                        break;
                    }
                }
                if(foreignSlotClaim) {
                    peer.refuse("lobby slot claim for another player");
                    return true;
                }
            }

            if(callbacks.onReceiveChangeEventList && *callbacks.onReceiveChangeEventList) {
                (*callbacks.onReceiveChangeEventList)(peer.name(), changeEventList);
            }
        } return true;

        case NETWORKPACKET_CONFIG_HASH: {
            handleConfigHash(stream, peer, context, callbacks);
        } return true;

        case NETWORKPACKET_JOIN_SYNC:
        case NETWORKPACKET_JOIN_ACK: {
            const Uint32 operation=stream.readUint32(), transaction=stream.readUint32(), offset=stream.readUint32();
            const auto data=stream.readString();
            // Prepare=1, chunk=2, abort=3. ACK offsets are cumulative; UINT_MAX acknowledges prepare.
            // Spectator streams: checkpoint 10/11, cycle 12, chat 15, progress 16, the rest of an
            // ended game 18 and its end marker 19 (acknowledged with 19).
            if(data.size()>48u*1024 || transaction==0 || (packetType==NETWORKPACKET_JOIN_SYNC && ((operation<1 || operation>3) && ((operation<10 || operation>12) && operation!=15 && operation!=16 && operation!=18 && operation!=19 && operation!=20)))
               || (packetType==NETWORKPACKET_JOIN_ACK && ((operation!=0 && operation!=10 && operation!=11 && operation!=13 && operation!=14 && operation!=17 && operation!=19) || !data.empty()))
               || ((operation==1 || operation==20) && (data.empty() || data.size()>64)) || ((operation==3 || operation==16 || operation==19) && !data.empty()) || (operation==20 && offset!=0)) {
                peer.refuse("invalid join synchronization packet"); return true;
            }
            if(callbacks.onJoinSync && *callbacks.onJoinSync)
                (*callbacks.onJoinSync)(peer.clientId(),operation,transaction,offset,data);
        } return true;
        case NETWORKPACKET_COOP_MISSION: {
            // The old same-house campaign still requires its sole remote partner. Only an
            // explicitly configured family [COOP] session may have several remote peers.
            if(context.isHost || !peer.isHostPeer()
               || (!context.coopPartnerIsSolePeer && !context.coopCampaign)) {
                peer.refuse("campaign continuation from an unexpected peer");
                return true;
            }

            GameInitSettings next(stream);
            std::string rejectionReason;
            if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(next, rejectionReason, true)) {
                peer.refuse(rejectionReason.c_str());
                return true;
            }
            const bool correctType = context.coopCampaign
                ? isSeparateCampaignContinuation(*context.coopCampaign, next, context.inGame)
                : (next.getGameType() == GameType::CampaignCoop || next.getGameType() == GameType::Invalid);
            if(!correctType) {
                peer.refuse("campaign continuation for the wrong game type or session");
                return true;
            }
            if(callbacks.onReceiveCoopMission && *callbacks.onReceiveCoopMission) {
                (*callbacks.onReceiveCoopMission)(next);
            }
        } return true;

        case NETWORKPACKET_COOP_CAMPAIGN_CONTROL: {
            if(!context.coopCampaign || !context.inGame) {
                peer.refuse("mission control outside a separate-house campaign");
                return true;
            }
            auto message = coop::readControl(stream);
            if(!coop::validControl(message) || !payloadFullyConsumed(stream)) {
                peer.refuse("invalid co-op mission control");
                return true;
            }
            // A late reliable packet from the previous mission is stale, not an order.
            if(message.epoch != context.coopEpoch || message.sessionId != context.coopCampaign->sessionId
               || message.stage != context.coopCampaign->stage) return true;
            const bool ready = message.operation == coop::ControlOperation::Ready;
            if((ready && (!context.isHost || peer.isHostPeer()))
               || (!ready && (context.isHost || !peer.isHostPeer()))
               || !isCampaignController(*context.coopCampaign, peer.name())) {
                peer.refuse("co-op mission control from an unexpected participant");
                return true;
            }
            message.playerName = peer.name();
            if(callbacks.onReceiveCoopControl && *callbacks.onReceiveCoopControl)
                (*callbacks.onReceiveCoopControl)(message);
        } return true;

        case NETWORKPACKET_STARTGAME: {
            const Uint32 timeLeft = stream.readUint32();
            if(timeLeft > kMaxStartGameCountdownMs) {
                peer.refuse("start-game countdown out of range");
                return true;
            }
            if(callbacks.onStartGame && *callbacks.onStartGame) {
                (*callbacks.onStartGame)(timeLeft);
            }
        } return true;

        case NETWORKPACKET_COMMANDLIST: {
            if(stream.readUint32() != context.simulationSeed) {
                return true;
            }
            CommandList commandList(stream);
            if(callbacks.onReceiveCommandList && *callbacks.onReceiveCommandList) {
                (*callbacks.onReceiveCommandList)(peer.name(), commandList);
            }
        } return true;

        case NETWORKPACKET_SELECTIONLIST: {
            if(stream.readUint32() != context.simulationSeed) {
                return true;
            }
            const int groupListIndex = stream.readSint32();
            const std::set<Uint32> selectedList = stream.readUint32Set();

            if(selectedList.size() > NetworkPacketPolicy::kMaxSelectionSize) {
                peer.refuse("selection list exceeds the size limit");
                return true;
            }
            // -1 means "current selection"; anything else indexes HumanPlayer::selectedLists.
            if(groupListIndex < -1 || groupListIndex >= NUMSELECTEDLISTS) {
                peer.refuse("selection group index out of range");
                return true;
            }
            if(callbacks.onReceiveSelectionList && *callbacks.onReceiveSelectionList) {
                (*callbacks.onReceiveSelectionList)(peer.name(), selectedList, groupListIndex);
            }
        } return true;

        case NETWORKPACKET_CLIENTSTATS: {
            if(stream.readUint32() != context.simulationSeed) {
                return true;
            }
            const Uint32 gameCycle     = stream.readUint32();
            const float  avgFps        = stream.readFloat();
            const float  simMsAvg      = stream.readFloat();
            const Uint32 queueDepth    = stream.readUint32();
            const Uint32 currentBudget = stream.readUint32();

            if(!NetworkPacketPolicy::isUsableStatValue(avgFps)
               || !NetworkPacketPolicy::isUsableStatValue(simMsAvg)) {
                peer.refuse("unusable client stats");
                return true;
            }

            // Identity is the connection, not an address hash: behind NAT or behind a relay a
            // host^port pair collides across players and is trivially spoofable.
            if(callbacks.onReceiveClientStats && *callbacks.onReceiveClientStats) {
                (*callbacks.onReceiveClientStats)(peer.clientId(), gameCycle, avgFps, simMsAvg,
                                                  queueDepth, currentBudget);
            }
        } return true;

        case NETWORKPACKET_SETPATHBUDGET: {
            if(stream.readUint32() != context.simulationSeed) {
                return true;
            }
            const Uint32 newBudget  = stream.readUint32();
            const Uint32 applyCycle = stream.readUint32();

            if(newBudget > kMaxPathBudgetOrder) {
                peer.refuse("path budget order out of range");
                return true;
            }
            if(callbacks.onReceiveSetPathBudget && *callbacks.onReceiveSetPathBudget) {
                (*callbacks.onReceiveSetPathBudget)(newBudget, applyCycle);
            }
        } return true;

        case NETWORKPACKET_MATCH_CONTROL: {
            // Read the whole record before anything is judged: a truncated payload has to leave
            // through the outer handler's bounded-reader path, not through a half-applied state.
            const Uint32 seed              = stream.readUint32();
            const Uint32 revision          = stream.readUint32();
            const Uint32 speed             = stream.readUint32();
            const Uint32 pauseCycle        = stream.readUint32();
            const Uint32 resumedPauseCycle = stream.readUint32();

            if(seed != context.simulationSeed) {
                // A packet from a previous match of this session. Silent, like the other
                // seed-tagged in-match packets: it is an ordering artefact, not abuse.
                return true;
            }
            if(!payloadFullyConsumed(stream)) {
                peer.refuse("match control packet has trailing data");
                return true;
            }
            // Revisions are monotonic and start at one, so zero can never be a real state; the
            // game does the actual "is this newer than what I have" comparison.
            if(revision == 0) {
                peer.refuse("match control revision out of range");
                return true;
            }
            // Speed is the wall-clock milliseconds per tick, not a simulation timestep.
            if(speed < static_cast<Uint32>(GAMESPEED_MIN) || speed > static_cast<Uint32>(GAMESPEED_MAX)) {
                peer.refuse("match control game speed out of range");
                return true;
            }
            // A pause can only be resumed once it exists. Equal means "the pause that started at
            // this cycle has been lifted"; both zero is the normal running state.
            if(resumedPauseCycle > pauseCycle) {
                peer.refuse("match control resumes a pause that has not started");
                return true;
            }
            if(callbacks.onReceiveMatchControl && *callbacks.onReceiveMatchControl) {
                (*callbacks.onReceiveMatchControl)(revision, speed, pauseCycle, resumedPauseCycle);
            }
        } return true;

        case NETWORKPACKET_MATCH_RESUME_REQUEST: {
            const Uint32 seed       = stream.readUint32();
            const Uint32 pauseCycle = stream.readUint32();

            if(seed != context.simulationSeed) {
                return true;
            }
            if(!payloadFullyConsumed(stream)) {
                peer.refuse("match resume request has trailing data");
                return true;
            }
            // There is nothing to resume from before the first pause exists.
            if(pauseCycle == 0) {
                peer.refuse("match resume request without a pause");
                return true;
            }
            // No upper bound here on purpose: whether this names the pause the match is actually
            // in is something only the game's current state can answer, and it does.
            if(callbacks.onReceiveMatchResumeRequest && *callbacks.onReceiveMatchResumeRequest) {
                // The connection's bound name, never an identity the sender put in the payload.
                (*callbacks.onReceiveMatchResumeRequest)(peer.name(), pauseCycle);
            }
        } return true;

        case NETWORKPACKET_KEEPALIVE: {
            // Receiving it is the whole point; nothing to do.
        } return true;

        default:
            return false;
    }
}
