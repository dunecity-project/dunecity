#include <Network/NetworkManager.h>
#include <Network/GameInitSettingsPolicy.h>
#include <GameInitSettings.h>
#include <mod/ModManager.h>
#include <misc/IMemoryStream.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace {
constexpr Uint8 offer = 1, chunk = 2, acknowledgment = 3;
constexpr Uint32 prepareAck = std::numeric_limits<Uint32>::max();
constexpr Uint32 chunkBytes = 48u * 1024;
constexpr Uint32 transferTimeout = 120000;

// Only the existing saved-game lobby packet may use this larger, streamed path.
// Ordinary games/maps and every relayed transport retain their current bounds.
bool isFamilyCheckpointSetup(const std::uint8_t* bytes, std::size_t length) {
    if(length < 4 || length > SnapshotTransfer::maxBytes) return false;
    IMemoryStream stream(reinterpret_cast<const char*>(bytes), static_cast<int>(length));
    if(stream.readUint32() != NETWORKPACKET_SENDGAMEINFO) return false;
    const GameInitSettings init(stream);
    if(init.getGameType() != GameType::LoadMultiplayer) return false;
    std::string rejection;
    if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(init, rejection)) return false;
    const ChangeEventList changes(stream);
    if(stream.getRemainingLength() != 0) return false;
    IMemoryStream savedStream(init.getFiledata().data(), static_cast<int>(init.getFiledata().size()));
    GameInitSettings::HouseInfoList houses;
    const auto saved = GameInitSettings::readSaveSetup(savedStream, houses);
    if(saved.getGameType() != GameType::CustomMultiplayer && saved.getGameType() != GameType::CustomGame) return false;
    const auto context = coop::readContext(saved.getFiledata());
    return context && ModManager::instance().isTornieFamilyContent(context->modName);
}
}

bool NetworkManager::sendCoopSetupRecord(Uint32 recipient, Uint8 operation, Uint32 transaction,
                                        Uint32 offset, const std::string& data) {
    if(!isDirectSession()) return false;
    NetworkPacketOStream packet(NETWORK_PACKET_FLAG_RELIABLE);
    packet.writeUint32(NETWORKPACKET_COOP_SETUP_TRANSFER);
    packet.writeUint8(operation);
    packet.writeUint32(transaction);
    packet.writeUint32(offset);
    packet.writeString(data);
    return sendPacketOverRelay(packet, 0, recipient);
}

bool NetworkManager::queueCoopSetup(const std::uint8_t* bytes, std::size_t length,
                                   int channel, Uint32 recipient) {
    if(!isDirectSession() || !bIsServer || bGameInProgress || channel != 0 || !pRelayClient) return false;
    try {
        if(!isFamilyCheckpointSetup(bytes, length)) return false;
        SnapshotTransfer::Sender snapshot;
        if(!snapshot.prepare(std::string(reinterpret_cast<const char*>(bytes), length))) return false;
        auto payload = std::make_shared<CoopSetupPayload>();
        payload->header = snapshot.header();
        payload->bytes = std::move(snapshot.bytes);
        ++nextCoopSetupTransaction;
        if(nextCoopSetupTransaction == 0) ++nextCoopSetupTransaction;
        bool sent = false;
        for(const auto& peer : pRelayClient->peers()) {
            if(peer.spectator || (recipient != 0 && peer.id != recipient)) continue;
            CoopSetupOutgoing outgoing;
            outgoing.payload = payload;
            outgoing.transaction = nextCoopSetupTransaction;
            outgoing.expectedAck = prepareAck;
            outgoing.deadline = SDL_GetTicks() + transferTimeout;
            coopSetupOutgoing[peer.id] = std::move(outgoing);
            if(!sendCoopSetupRecord(peer.id, offer, nextCoopSetupTransaction,
                                   static_cast<Uint32>(payload->bytes.size()), payload->header)) {
                failCoopSetup("The co-op checkpoint could not be sent to every controller.");
                return false;
            }
            sent = true;
        }
        return sent;
    } catch(const std::exception& error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Co-op checkpoint setup failed: %s", error.what());
        failCoopSetup("The co-op checkpoint could not be prepared.");
        return false;
    }
}

void NetworkManager::receiveCoopSetup(Uint32 peer, bool peerIsHost, InputStream& stream) {
    if(!isDirectSession() || bGameInProgress) return;
    const auto operation = stream.readUint8();
    const auto transaction = stream.readUint32();
    const auto offset = stream.readUint32();
    const auto data = stream.readString();
    if(transaction == 0 || data.size() > chunkBytes || stream.getRemainingLength() != 0) {
        failCoopSetup("A controller sent an invalid co-op checkpoint record.");
        return;
    }
    if(bIsServer) {
        if(peerIsHost || operation != acknowledgment || !data.empty()) {
            failCoopSetup("Only checkpoint acknowledgments may be sent to the host.");
            return;
        }
        const auto found = coopSetupOutgoing.find(peer);
        if(found == coopSetupOutgoing.end() || found->second.transaction != transaction) return;
        auto& outgoing = found->second;
        if(offset != outgoing.expectedAck) {
            failCoopSetup("A co-op checkpoint acknowledgment had the wrong offset.");
            return;
        }
        if(offset == outgoing.payload->bytes.size()) {
            const auto* connected = pRelayClient ? pRelayClient->findPeer(peer) : nullptr;
            if(!connected || !pGetChangeEventListForNewPlayerCallback) {
                failCoopSetup("The host lobby closed before the checkpoint was ready.");
                return;
            }
            const std::string name = connected->name;
            coopSetupOutgoing.erase(found);
            // Another controller may have joined/left while this guest had no lobby
            // callbacks. Refresh the entire authoritative roster after its ready ACK.
            const auto changes = pGetChangeEventListForNewPlayerCallback(name);
            NetworkPacketOStream packet(NETWORK_PACKET_FLAG_RELIABLE);
            packet.writeUint32(NETWORKPACKET_CHANGEEVENTLIST);
            changes.save(packet);
            if(!sendPacketOverRelay(packet, 0, peer))
                failCoopSetup("The current co-op lobby roster could not be sent.");
        } else {
            outgoing.nextOffset = offset == prepareAck ? 0 : offset;
            outgoing.readyToSend = true;
            outgoing.deadline = SDL_GetTicks() + transferTimeout;
        }
        return;
    }
    if(!peerIsHost || operation == acknowledgment) {
        failCoopSetup("Only the authenticated host may supply a co-op checkpoint.");
        return;
    }
    if(operation == offer) {
        Uint32 decodedSize = 0;
        if(!SnapshotTransfer::readHeader(data, offset, decodedSize)) {
            failCoopSetup("The co-op checkpoint header is invalid.");
            return;
        }
        coopSetupIncoming = {};
        coopSetupIncoming.transaction = transaction;
        coopSetupIncoming.total = offset;
        coopSetupIncoming.decodedSize = decodedSize;
        coopSetupIncoming.hostPeer = peer;
        coopSetupIncoming.deadline = SDL_GetTicks() + transferTimeout;
        coopSetupIncoming.bytes.reserve(offset);
        if(!sendCoopSetupRecord(peer, acknowledgment, transaction, prepareAck, {}))
            failCoopSetup("The co-op checkpoint host could not be reached.");
        return;
    }
    if(operation != chunk || coopSetupIncoming.awaitingLobby || coopSetupIncoming.transaction != transaction
       || offset != coopSetupIncoming.bytes.size() || data.empty()
       || data.size() > coopSetupIncoming.total - coopSetupIncoming.bytes.size()) {
        failCoopSetup("The co-op checkpoint stream is incomplete or out of order.");
        return;
    }
    coopSetupIncoming.bytes += data;
    coopSetupIncoming.deadline = SDL_GetTicks() + transferTimeout;
    const auto received = static_cast<Uint32>(coopSetupIncoming.bytes.size());
    if(received == coopSetupIncoming.total) {
        std::string decoded;
        if(!SnapshotTransfer::decode(coopSetupIncoming.bytes, coopSetupIncoming.decodedSize, decoded)
           || !isFamilyCheckpointSetup(reinterpret_cast<const std::uint8_t*>(decoded.data()), decoded.size())) {
            failCoopSetup("The co-op checkpoint did not contain a valid family campaign setup.");
            return;
        }
        // Keep both peers fenced until the newly constructed lobby can consume authoritative
        // edits. Its browser constructor may yield several frames before installing callbacks.
        coopSetupIncoming.awaitingLobby = true;
        std::string().swap(coopSetupIncoming.bytes);
        handleRelayGamePayload(peer, reinterpret_cast<const std::uint8_t*>(decoded.data()), decoded.size());
    } else if(!sendCoopSetupRecord(peer, acknowledgment, transaction, received, {}))
        failCoopSetup("The co-op checkpoint host could not be reached.");
}

void NetworkManager::markCoopSetupLobbyReady() {
    if(!isDirectSession() || bIsServer || !coopSetupIncoming.awaitingLobby) return;
    if(!sendCoopSetupRecord(coopSetupIncoming.hostPeer, acknowledgment,
                           coopSetupIncoming.transaction, coopSetupIncoming.total, {})) {
        failCoopSetup("The co-op checkpoint host could not be reached.");
        return;
    }
    coopSetupIncoming = {};
}

void NetworkManager::updateCoopSetup() {
    if(!isDirectSession() || !pRelayClient || !pRelayClient->isJoined()) return;
    const auto now = SDL_GetTicks();
    if(coopSetupIncoming.transaction != 0 && SDL_TICKS_PASSED(now, coopSetupIncoming.deadline)) {
        failCoopSetup("The co-op checkpoint transfer timed out.");
        return;
    }
    for(auto& entry : coopSetupOutgoing) {
        auto& outgoing = entry.second;
        if(!pRelayClient->findPeer(entry.first) || SDL_TICKS_PASSED(now, outgoing.deadline)) {
            failCoopSetup("A controller left before receiving the co-op checkpoint.");
            return;
        }
        if(!outgoing.readyToSend) continue;
        const auto count = std::min<std::size_t>(chunkBytes, outgoing.payload->bytes.size() - outgoing.nextOffset);
        outgoing.expectedAck = outgoing.nextOffset + static_cast<Uint32>(count);
        outgoing.readyToSend = false;
        if(!sendCoopSetupRecord(entry.first, chunk, outgoing.transaction, outgoing.nextOffset,
                               outgoing.payload->bytes.substr(outgoing.nextOffset, count))) {
            failCoopSetup("The co-op checkpoint could not be sent to every controller.");
            return;
        }
    }
}

void NetworkManager::failCoopSetup(const char* reason) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", reason);
    coopSetupOutgoing.clear();
    coopSetupIncoming = {};
    if(pRelayClient && isDirectSession()) pRelayClient->stop(3);
}
