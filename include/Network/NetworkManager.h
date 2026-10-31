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

#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <Campaign/CoopCampaignSession.h>
#include <Campaign/CoopCampaignTransition.h>
#include <Network/SnapshotTransfer.h>
#include <Network/NetworkTransportTypes.h>
#include <Network/ChangeEventList.h>
#include <Network/CommandList.h>
#include <Network/NetworkPacketTypes.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/GamePayloadRouter.h>
#include <Network/DirectRoomTransport.h>
#include <Network/RoomRelayClient.h>
#include <Network/RoomSessionTransport.h>
#include <Network/WebRtcTransport.h>

#ifndef __EMSCRIPTEN__
#include <Network/LANGameFinderAndAnnouncer.h>
#include <Network/MetaServerClient.h>
#include <Network/UPnPManager.h>
#include <enet/enet.h>
#endif

#include <misc/string_util.h>
#include <misc/SDL2pp.h>

#include <string>
#include <list>
#include <map>
#include <vector>
#include <functional>
#include <stdarg.h>

// rejectIncompatibleNetworkProtocol() and rejectIncompatibleGameVersion() moved to
// Network/NetworkPacketPolicy.h so both transports can use them; they are still reachable
// through this header.

#ifdef __EMSCRIPTEN__
/**
    ENetPeer stand-in for the browser transport. Owns nothing itself: NetworkManager
    allocates one per connected remote peer and stores its PeerData* in `data`,
    exactly like ENet does for native connections. `webRtcPeerId` is the opaque
    signaling/transport handle — browser peer identity never fakes IP/port data.
*/
struct NetPeer {
    void* data = nullptr;
    Uint32 roundTripTime = 0;
    uint32_t webRtcPeerId = 0;
};
#else
using NetPeer = ENetPeer;
#endif

#define AWAITING_CONNECTION_TIMEOUT     5000

class GameInitSettings;

class NetworkManager {
public:
    /// Which transport carries this session.
    enum class Transport {
        EnetMesh,   ///< legacy UDP mesh with LAN discovery, the metaserver and UPnP
        RoomRelay,  ///< legacy crossplay through the room relay over one outbound WebSocket
        /**
            Crossplay played directly between the players over WebRTC data channels.

            HTTPS is used to find the room and introduce the players to each other and for
            nothing else: no gameplay byte passes through a server. This is what Play Online
            starts now; RoomRelay remains for older tests and releases.
        */
        DirectP2P
    };

    /// Legacy ENet mesh session.
    NetworkManager(int port, const std::string& metaserver);

    /**
        Room session, relayed or direct.

        Deliberately creates no ENet host and starts no LAN discovery, metaserver thread or UPnP
        mapping. None of those work in a browser, and a room session has no use for any of them
        anywhere: a relay session opens one outbound WebSocket, and a direct session opens no
        server socket at all - the data channels are negotiated outbound.
    */
    explicit NetworkManager(Transport transport);

    NetworkManager(const NetworkManager& o) = delete;
    ~NetworkManager();

    bool isServer() const { return bIsServer; };
    bool isLANServer() const { return bLANServer; };

    void setPublicRelayRoom(bool value) { publicRelayRoom = value; }
    bool isPublicRelayRoom() const { return publicRelayRoom; }
    /// True for both room transports: this session has a room, a grant and logical peer ids.
    bool isRoomSession() const {
        return transport == Transport::RoomRelay || transport == Transport::DirectP2P;
    }
    /// Only the legacy relay needs a wall-clock batch cadence. Direct transports send each step.
    static constexpr bool usesBatchedCommands(Transport kind) { return kind == Transport::RoomRelay; }
    bool usesBatchedCommands() const { return usesBatchedCommands(transport); }
    /// True only when gameplay travels straight between the players.
    bool isDirectSession() const { return transport == Transport::DirectP2P; }

    /**
        Relay v1 carries bundled, matching content only: mod transfer packets are refused by the
        relay, so the lobby must not wait for mod acknowledgements on that transport.
    */
    bool supportsModTransfer() const { return transport == Transport::EnetMesh; }

    /**
        Whether every player is directly connected to every other player.

        True on the transports where the question does not arise. On a direct session it is the
        start barrier: the host having a channel to each guest does not mean the guests can reach
        each other, and starting across a missing guest-to-guest link loses that pair's commands
        silently.
    */
    bool isMeshReady() const;

    /// A player-facing sentence naming what the mesh is waiting for, or empty when it is ready.
    std::string getMeshBlockedReason() const;

    /**
        Starts the relay session with a grant that HTTPS admission already produced.
        \param  config  session parameters
        \param  error   set to a player-facing reason on failure
        \return true if the session is connecting
    */
    bool startRelaySession(const RoomRelayClient::Config& config, std::string& error);

    /**
        Starts a direct session with a grant that HTTPS admission already produced.

        The grant is redeemed at the signaling service, which introduces the players to each
        other. After that the service carries nothing, and the match survives losing it.
    */
    bool startDirectSession(const DirectRoomTransport::Config& config, std::string& error);

    /// The room session, or nullptr on the ENet transport.
    RoomSessionTransport* getRelayClient() { return pRelayClient.get(); }
    const RoomSessionTransport* getRelayClient() const { return pRelayClient.get(); }

    /// The direct session, or nullptr when this session is not a direct one.
    DirectRoomTransport* getDirectTransport() {
        return transport == Transport::DirectP2P
                   ? static_cast<DirectRoomTransport*>(pRelayClient.get()) : nullptr;
    }
    const DirectRoomTransport* getDirectTransport() const {
        return transport == Transport::DirectP2P
                   ? static_cast<const DirectRoomTransport*>(pRelayClient.get()) : nullptr;
    }

    /// The room code a player can pass to a friend, or empty when there is no relay session.
    std::string getRoomCode() const {
        return pRelayClient ? pRelayClient->roomCode() : std::string();
    }

    /// The outcome of comparing every relay peer's content with ours.
    enum class ContentCheck {
        Match,          ///< everybody reported, and everybody agrees
        AwaitingPeer,   ///< somebody has not reported yet; try again shortly
        Mismatch        ///< somebody reported content this transport cannot reconcile
    };

    /**
        Checks every relay peer's reported content against ours, for the host to call before it
        starts a match.

        The per-message check happens when a peer's hashes arrive, but the lobby lets the host
        pick a different mod afterwards, so the comparison has to be made again against what is
        actually about to be played. A peer that has not reported yet is reported separately from
        one that disagrees: waiting is recoverable, disagreeing is not.

        \param  quantBotHash    our current QuantBot config hash
        \param  objectDataHash  our current object data hash
        \param  gameVersion     our build version
        \param  reason          set to a player-facing explanation unless everybody matches
    */
    ContentCheck checkRelayContent(const std::string& quantBotHash,
                                   const std::string& objectDataHash,
                                   const std::string& gameVersion,
                                   std::string& reason) const;

    /**
        Sends a bounded diagnostic to the room. Carried in the relay envelope, never as a game
        packet, so the ENet wire format and NETWORK_PROTOCOL_VERSION are untouched.
        \return false if there is no relay session or the payload was refused
    */
    bool sendRelayDiagnostic(RoomRelay::DiagnosticKind kind, const std::uint8_t* payload,
                             std::size_t length);

    /**
        Sets the function called when a diagnostic arrives from another peer.
        \param  callback    function(senderName, kind, payload, length)
    */
    void setOnReceiveRelayDiagnostic(
        std::function<void (const std::string&, std::uint8_t, const std::uint8_t*, std::size_t)> callback) {
        pOnReceiveRelayDiagnostic = std::move(callback);
    }

    void startServer(bool bLANServer, const std::string& serverName, const std::string& playerName, GameInitSettings* pGameInitSettings, int numPlayers, int maxPlayers);
    void updateServer(int numPlayers);
    void stopAnnouncing();  // Stops lobby announcement but keeps bIsServer = true
    void stopServer();

    void connect(const std::string& hostname, int port, const std::string& playerName);
#ifndef __EMSCRIPTEN__
    void connect(ENetAddress address, const std::string& playerName);
#else
    /**
        Browser transport: enter the global matchmaking lobby. The lobby pairs
        the next two finders and assigns the roles (host/joiner); pairing is
        reported through the onMatched callback. No IP/port or room code
        involved.
    */
    void connectWebRtc(const std::string& playerName);

    /// Leave the matchmaking queue; only meaningful before pairing.
    void cancelMatchmaking();
#endif

    void disconnect();

    void update();

#ifdef __EMSCRIPTEN__
    /// Coarse transport state for UI (connecting/connected/error indicators).
    WebRtcTransport::State getWebRtcState() const { return pWebRtcTransport ? pWebRtcTransport->getState() : WebRtcTransport::State::Idle; }

    /**
        Sets the function that should be called when the matchmaking lobby pairs us.
        \param  pOnMatched  function to call; true when we take the host (offer) role
    */
    inline void setOnMatched(std::function<void (bool bHost)> pOnMatched) {
        this->pOnMatched = pOnMatched;
    }
#endif

    void sendChatMessage(const std::string& message);

    void sendChangeEventList(const ChangeEventList& changeEventList);

    void sendConfigHash(const std::string& quantBotHash, const std::string& objectDataHash, const std::string& gameVersion);

    bool sendStartGame(unsigned int timeLeft);
    void sendCoopMission(const GameInitSettings& settings);
    std::unique_ptr<GameInitSettings> takeCoopMission();
    void configureCoopCampaign(const coop::Context& context, Uint32 epoch);
    void clearCoopCampaign();
    Uint32 getCoopCampaignEpoch() const { return coopCampaignEpoch; }
    void sendCoopReady(const std::string& sessionId, int stage, Uint8 outcome);
    void sendCoopAdvance(const std::string& sessionId, int stage, Uint8 action,
                         const std::string& settingsBlob = {});
    std::vector<coop::ControlMessage> takeCoopReady();
    std::vector<coop::ControlMessage> takeCoopAdvance();
    bool hasPendingCoopSetup() const {
        return !coopSetupOutgoing.empty() || coopSetupIncoming.transaction != 0;
    }
    /// Called after the guest lobby has installed its callbacks, including after browser yields.
    void markCoopSetupLobbyReady();

    /**
        Called on every peer (host and clients) when the match starts. Besides the simulation
        seed this freezes the session phase: lobby-only packets, including renames and mod
        transfers, stop being accepted from this point on.
        \param  seed    the shared simulation seed
    */
    bool beginLateJoin(const std::string& requestId, const std::string& name, const GameInitSettings& snapshot, bool spectator = false);
    bool canCancelLateJoin() const { return bIsServer && lateJoinPaused() && joinStage!=JoinStage::Starting && joinStage!=JoinStage::Ready; }
    void cancelLateJoin() { if(canCancelLateJoin()) abortLateJoin("The host cancelled the join request."); }
    bool lateJoinPaused() const { return joinStage != JoinStage::Idle; }
    bool lateJoinReady() const { return joinStage == JoinStage::Ready; }
    bool lateJoinLoading() const { return joinLoading; }
    const std::string& lateJoinStatus() const { return joinStatus; }
    const std::string& lateJoinPlayerName() const { return joinName; }
    std::string recentJoinNotice() const {
        return SDL_GetTicks() - joinNoticeTime < 12000 ? joinNotice : std::string();
    }
    // Percent describes the current stage, not an estimated elapsed duration.
    int lateJoinPercent() const;
    std::string lateJoinProgressText() const;
    bool observerCatchingUp() const { return observerCatchup; }
    void observerAdvanced(Uint32 cycle);
    const std::string& joinFailure() const { return joinFailureReason; }
    void failObserver(const std::string& reason);
    bool requestObserverResync(Uint32 cycle);
    bool observerResyncWaiting() const { return observerResyncPending; }
    std::unique_ptr<GameInitSettings> takeLateJoin();
    void expectLateJoin() { joinExpected=true; }
    bool isSpectator(const std::string& name) const {
        if(spectators.count(name)) return true;
        if(const auto* direct=getDirectTransport()) for(const auto& p : direct->peers())
            if(p.name==name) return p.spectator;
        return false;
    }
    bool isSpectating() const { return (getDirectTransport() && getDirectTransport()->isSpectating()) || isSpectator(playerName); }
    std::vector<Uint32> observersNeedingSnapshot() const;
    bool beginObserverSnapshot(Uint32 peer, const GameInitSettings& snapshot, const std::string& runtime, Uint32 cycle);
    void publishObserverCycle(Uint32 cycle, const std::string& bytes);
    bool hasObserverStreams() const { return !observerTransfers.empty(); }
    bool takeObserverCycle(Uint32 cycle, std::string& bytes);
    std::string takeObserverRuntime() { auto bytes=std::move(observerRuntime); observerRuntime.clear(); return bytes; }
    void observerLoaded(Uint32 cycle);
    Uint32 observerFrontier() const { return observerNextCycle; }
    // The end of a match's spectator streams. The host calls endObserverStreams() once its game
    // has run its last cycle; every spectator then receives all the cycles it does not have yet
    // (past the replay window) and an end marker, which it acknowledges. A spectator that is behind
    // plays them after the host has left, so it sees the end of the game the players saw.
    void endObserverStreams(Uint32 endCycle);
    /// True once every spectator with a running stream has acknowledged the end marker (or the
    /// session has ended): the host can leave without taking the rest of the game with it.
    bool observerStreamsDelivered() const;
    /// The cycle at which the host's game ended (the end marker), 0 while it is still running.
    Uint32 observerFinalCycle() const { return observerEndCycle; }
    void beginSimulation(Uint32 seed);
    void sendCommandList(const CommandList& commandList);

    void sendSelectedList(const std::set<Uint32>& selectedList, int groupListIndex = -1);

    std::list<std::string> getConnectedPeers() const {
        std::list<std::string> peerNameList;

        if(pRelayClient) {
            for(const RoomSessionTransport::Peer& peer : pRelayClient->peers()) {
                if(!peer.spectator) peerNameList.push_back(peer.name);
            }
            return peerNameList;
        }

        for(const NetPeer* pPeer : peerList) {
            PeerData* peerData = static_cast<PeerData*>(pPeer->data);
            if(peerData != nullptr) {
                peerNameList.push_back(peerData->name);
            }
        }

        return peerNameList;
    }

    /// Largest ENet peer RTT; for relay sessions this is only the local heartbeat RTT.
    /// Relay heartbeat timing does not measure or bound the complete peer delivery path.
    int getMaxPeerRoundTripTime();

    /// Local relay heartbeat RTT in milliseconds; zero before the first sample or without a relay.
    Uint32 getRelayServerRoundTripTimeMs() const;
    bool isRelayHttpPollingSession() const;

#ifndef __EMSCRIPTEN__
    LANGameFinderAndAnnouncer* getLANGameFinderAndAnnouncer() {
        return pLANGameFinderAndAnnouncer.get();
    };

    MetaServerClient* getMetaServerClient() {
        return pMetaServerClient.get();
    };
#endif

    /**
        Sets the function that should be called when a chat message is received
        \param  pOnReceiveChatMessage   function to call on new chat message
    */
    inline void setOnReceiveChatMessage(std::function<void (const std::string&, const std::string&)> pOnReceiveChatMessage) {
        this->pOnReceiveChatMessage = pOnReceiveChatMessage;
    }

    /**
        Sets the function that should be called when game infos are received after connecting to the server.
        \param  pOnReceiveGameInfo  function to call on receive
    */
    inline void setOnReceiveGameInfo(std::function<void (const GameInitSettings&, const ChangeEventList&)> pOnReceiveGameInfo) {
        this->pOnReceiveGameInfo = pOnReceiveGameInfo;
    }


    /**
        Sets the function that should be called when a change event is received. The first
        argument is the bound peer name of the connection it arrived on, so the lobby can
        authorize the request against the seat that connection holds.
        \param  pOnReceiveChangeEventList   function(senderName, changeEventList) to call
    */
    inline void setOnReceiveChangeEventList(std::function<void (const std::string&, const ChangeEventList&)> pOnReceiveChangeEventList) {
        this->pOnReceiveChangeEventList = pOnReceiveChangeEventList;
    }


    /**
        Sets the function that should be called when a peer disconnects.
        \param  pOnPeerDisconnected function to call on disconnect
    */
    inline void setOnPeerDisconnected(std::function<void (const std::string&, bool, int)> pOnPeerDisconnected) {
        this->pOnPeerDisconnected = pOnPeerDisconnected;
    }

    /**
        Sets the function that can be used to retreive all house/player changes to get the current state
        \param  pGetGameInitSettingsCallback    function to call
    */
    inline void setGetChangeEventListForNewPlayerCallback(std::function<ChangeEventList (const std::string&)> pGetChangeEventListForNewPlayerCallback) {
        this->pGetChangeEventListForNewPlayerCallback = pGetChangeEventListForNewPlayerCallback;
    }

    /**
        Sets the function that should be called when the game is about to start and the time (in ms) left is received
        \param  pOnStartGame    function to call on receive
    */
    inline void setOnStartGame(std::function<void (unsigned int)> pOnStartGame) {
        this->pOnStartGame = pOnStartGame;
    }

    /**
        Sets the function that should be called when a command list is received.
        \param  pOnReceiveCommandList   function to call on receive
    */
    inline void setOnReceiveCommandList(std::function<void (const std::string&, const CommandList&)> pOnReceiveCommandList) {
        this->pOnReceiveCommandList = pOnReceiveCommandList;
    }

    /**
        Sets the function that should be called when a selection list is received.
        \param  pOnReceiveSelectionList function to call on receive
    */
    inline void setOnReceiveSelectionList(std::function<void (const std::string&, const std::set<Uint32>&, int)> pOnReceiveSelectionList) {
        this->pOnReceiveSelectionList = pOnReceiveSelectionList;
    }

    /**
        Sets the function that should be called when a config mismatch is detected.
        \param  pOnConfigMismatch   function to call with error message
    */
    inline void setOnConfigMismatch(std::function<void (const std::string&)> pOnConfigMismatch) {
        this->pOnConfigMismatch = pOnConfigMismatch;
    }

    /**
        Sets the function that should be called when client performance stats are received (host only).
        \param  pOnReceiveClientStats   function to call on receive
    */
    inline void setOnReceiveClientStats(std::function<void (Uint32, Uint32, float, float, Uint32, Uint32)> pOnReceiveClientStats) {
        this->pOnReceiveClientStats = pOnReceiveClientStats;
    }

    /**
        Sets the function that should be called when a path budget change order is received (client only).
        \param  pOnReceiveSetPathBudget function to call on receive
    */
    inline void setOnReceiveSetPathBudget(std::function<void (size_t, Uint32)> pOnReceiveSetPathBudget) {
        this->pOnReceiveSetPathBudget = pOnReceiveSetPathBudget;
    }

    /**
        Sets the function called when the host's authoritative match control state arrives
        (client and spectator only).
        \param  pOnReceiveMatchControl  function to call with (revision, speed, pauseCycle, resumedPauseCycle)
    */
    inline void setOnReceiveMatchControl(std::function<void (Uint32, Uint32, Uint32, Uint32)> pOnReceiveMatchControl) {
        this->pOnReceiveMatchControl = pOnReceiveMatchControl;
    }

    /**
        Sets the function called when a peer asks to leave a pause (host only).

        The name passed to the callback is bound to the connection the request arrived on, so
        the game can check it against the active human players.
        \param  pOnReceiveMatchResumeRequest function to call with (peer name, pauseCycle)
    */
    inline void setOnReceiveMatchResumeRequest(std::function<void (const std::string&, Uint32)> pOnReceiveMatchResumeRequest) {
        this->pOnReceiveMatchResumeRequest = pOnReceiveMatchResumeRequest;
    }

    /**
        Sends client performance stats to host (client → host).
        \param  avgFps          Average FPS (legacy metric)
        \param  simMsAvg        Average simulation time per tick in ms (primary metric)
        \param  queueDepth      Pathfinding queue depth
        \param  currentBudget   Current path budget (for validation)
        \param  gameCycle       Current game cycle
    */
    void sendClientStats(float avgFps, float simMsAvg, Uint32 queueDepth, Uint32 currentBudget, Uint32 gameCycle);

    /**
        Broadcasts a path budget change to all clients (host → all clients).
        \param  newBudget       New path budget
        \param  applyCycle      Game cycle to apply the change
    */
    void broadcastPathBudget(size_t newBudget, Uint32 applyCycle);

    /**
        Broadcasts the authoritative match control state to every connected peer, spectators
        included (host → all).

        Only the host decides the shared settings. Resume travels this way rather than as a
        synchronized command because a paused simulation no longer produces command cycles.
        \param  revision            monotonic state revision, never zero
        \param  speed               wall-clock milliseconds per tick, GAMESPEED_MIN..GAMESPEED_MAX
        \param  pauseCycle          cycle the current pause started at, 0 when never paused
        \param  resumedPauseCycle   pause cycle that has been lifted, never above pauseCycle
    */
    void sendMatchControl(Uint32 revision, Uint32 speed, Uint32 pauseCycle, Uint32 resumedPauseCycle);

    /**
        Asks the host to lift a pause (client → host).

        The host decides: it checks that the sender is an active human player of this match and
        answers everybody with sendMatchControl().
        \param  pauseCycle  the pause this client believes the match is in, never 0
    */
    void requestMatchResume(Uint32 pauseCycle);

    // === Mod Transfer Methods ===

    /**
        Send mod info to a specific peer (host only).
        Called when a new client connects to inform them of the active mod.
        \param  peer            The peer to send to
        \param  modName         Name of the active mod
        \param  modChecksum     Combined mod checksum
    */
    void sendModInfoToPeer(NetPeer* peer, const std::string& modName, const std::string& modChecksum);

    /**
        Send mod info to all connected clients (host only).
        \param  modName         Name of the active mod
        \param  modChecksum     Combined mod checksum
    */
    void sendModInfo(const std::string& modName, const std::string& modChecksum);

    /**
        Request mod files from host (client only).
        \param  modName         Name of the mod to download
    */
    void requestModDownload(const std::string& modName);

    /**
        Send mod sync acknowledgment to host (client only).
        \param  success         Whether mod sync was successful
        \param  modChecksum     The client's mod checksum after sync
    */
    void sendModAck(bool success, const std::string& modChecksum);

    /**
        Sets the function that should be called when mod info is received.
        \param  pOnReceiveModInfo   function(modName, modChecksum) to call
    */
    inline void setOnReceiveModInfo(std::function<void (const std::string&, const std::string&)> pOnReceiveModInfo) {
        this->pOnReceiveModInfo = pOnReceiveModInfo;
    }

    /**
        Sets the function that should be called when mod download progress updates.
        \param  pOnModDownloadProgress  function(bytesReceived, totalBytes) to call
    */
    inline void setOnModDownloadProgress(std::function<void (size_t, size_t)> pOnModDownloadProgress) {
        this->pOnModDownloadProgress = pOnModDownloadProgress;
    }

    /**
        Sets the function that should be called when mod download completes.
        \param  pOnModDownloadComplete  function(success, errorMsg) to call
    */
    inline void setOnModDownloadComplete(std::function<void (bool, const std::string&)> pOnModDownloadComplete) {
        this->pOnModDownloadComplete = pOnModDownloadComplete;
    }

    /**
        Sets the function that should be called when host receives mod ACK from client.
        \param  pOnReceiveModAck  function(playerName, success, modChecksum) to call
    */
    inline void setOnReceiveModAck(std::function<void (const std::string&, bool, const std::string&)> pOnReceiveModAck) {
        this->pOnReceiveModAck = pOnReceiveModAck;
    }

private:
    enum class JoinStage { Idle, Preparing, Admission, Connecting, Sending, Receiving, Starting, Ready };
    JoinStage joinStage = JoinStage::Idle;
    Uint32 joinTotal = 0, joinTransaction = 0, joinDeadline = 0, joinOffset = 0, joinNextOffset = 0, resumeSeed = 0;
    bool joinExpected = false, joinLoading = false, joinAsSpectator = false;
    std::set<std::string> spectators, joinSpectators;
    std::string joinRequestId, joinName, joinBytes, joinStatus;
    std::string joinNotice;
    Uint32 joinNoticeTime = 0;
    std::vector<Uint32> joinOriginalPeers;
    std::map<Uint32,Uint32> joinAcks;
    std::unique_ptr<GameInitSettings> joinSnapshot;
    std::function<void (Uint32, Uint32, Uint32, Uint32, const std::string&)> pOnJoinSync;
    void receiveJoinSync(Uint32 peer, Uint32 operation, Uint32 transaction, Uint32 offset, const std::string& data);
    void updateLateJoin();
    bool sendObserverPacket(Uint32 peer, Uint32 operation, Uint32 epoch, Uint32 offset, const std::string& data);
    void receiveObserverPacket(Uint32 peer, Uint32 operation, Uint32 epoch, Uint32 offset, const std::string& data);
    void updateObservers();
    void forwardObserverChat(const std::string& sender, const std::string& message);
    struct ObserverTransfer {
        SnapshotTransfer::Sender checkpoint;
        Uint32 epoch=0, offset=0, sent=0, nextCycle=0, ackCycle=0, deadline=0;
        bool began=false, ready=false, endSent=false, endAcked=false;
        Uint32 progressAt=0;
    };
    std::map<Uint32,ObserverTransfer> observerTransfers;
    std::deque<std::pair<Uint32,std::string>> observerHistory, observerIncoming;
    std::size_t observerHistoryBytes=0;
    std::string observerBytes, observerRuntime;
    Uint32 observerDecodedSize=0;
    Uint32 observerEpoch=0, observerTotal=0, observerNextCycle=0, observerSendCursor=0;
    Uint32 observerStartCycle=0, observerHostCycle=0, observerAppliedCycle=0, observerEndCycle=0;
    bool observerCatchup=false, observerResyncPending=false;
    Uint32 observerResyncDeadline=0, observerSnapshotSerial=0;
    unsigned observerResyncAttempts=0;
    std::map<Uint32,unsigned> observerRestarts;
    std::string joinFailureReason;
    void abortLateJoin(const std::string& reason);
    bool sendJoinSync(Uint32 operation, Uint32 offset, const std::string& data, Uint32 recipient = 0);
    bool publicRelayRoom = false;
    static void debugNetwork(PRINTF_FORMAT_STRING const char* fmt, ...) PRINTF_VARARG_FUNC(1);

    /// Bundles the game's callbacks for the shared payload handling.
    NetworkSessionCallbacks sessionCallbacks() const;

    /// Drives the relay session and drains its event queue. Called from update().
    void updateRelaySession();

    /**
        Applies one game payload received over the relay.

        Takes the sender's id rather than a reference to its record: handling a payload runs the
        game's own callbacks, and anything that reaches the session again can add or remove a
        peer, which moves the vector a reference would point into.
    */
    void handleRelayGamePayload(std::uint32_t peerId, const std::uint8_t* payload,
                                std::size_t length);

    /**
        Hands one relay message to the transport.
        \param  packetStream    the packet to send; its ENetPacket is consumed
        \param  channel         0 or 1
        \param  recipient       0 for every other peer in the room, or a relay peer id
    */
    bool sendPacketOverRelay(NetworkPacketOStream& packetStream, int channel,
                             std::uint32_t recipient);

    /// The relay peer id of the designated host, or 0 if this process is the host.
    std::uint32_t relayHostPeerId() const;
    mutable std::uint32_t knownRelayHostPeerId = 0;   ///< the host's id, still valid while it departs

    void sendPacketToHost(NetworkPacketOStream& packetStream, int channel = 0);

    void sendPacketToPeer(NetPeer* peer, NetworkPacketOStream& packetStream, int channel = 0);

    /**
        Package and send mod files to a peer in chunks.
        \param  peer        The peer to send to
        \param  modName     Name of the mod to send
    */
    void sendModFilesToPeer(NetPeer* peer, const std::string& modName);

    void sendPacketToAllConnectedPeers(NetworkPacketOStream& packetStream, int channel = 0);

    void handlePacket(NetPeer* peer, NetworkPacketIStream& packetStream);

#ifdef __EMSCRIPTEN__
    /// Look up (or create) the NetPeer for a transport peer handle.
    NetPeer* findPeerByWebRtcId(uint32_t webRtcPeerId, bool bCreate);
    /// Disconnect one remote peer (browser: tears down the single connection).
    void disconnectPeer(NetPeer* peer, int cause);
    /// Drop all peer bookkeeping after the transport went away.
    void clearAllPeers();
    void drainWebRtcDisconnects();
    void releaseWebRtcPeer(NetPeer* peer, int cause, bool notify);
    std::map<uint32_t, int> pendingWebRtcDisconnects;
#else
    void disconnectPeer(ENetPeer* peer, enet_uint32 cause) {
        enet_peer_disconnect_later(peer, cause);
    }
#endif

    /**
        Hands a mesh packet to the payload handling that both transports share.
        \return true if this packet id belongs to the shared set
    */
    bool routeSharedPayload(NetPeer* peer, Uint32 packetType, NetworkPacketIStream& packetStream);

    class PeerData;

    /**
        Applies the central admission policy to one inbound packet.
        \param  peer        the connection the packet arrived on
        \param  packetType  the packet id that was just read
        \return true if the packet may be interpreted
    */
    bool admitPacket(NetPeer* peer, Uint32 packetType);

    /**
        Records a rejected or malformed packet for this peer, throttles the log line and
        disconnects peers that keep sending garbage.
        \param  peer    the offending connection
        \param  reason  short description for the log
    */
    void noteRejectedPacket(NetPeer* peer, const char* reason);

    /**
        Requests a disconnect once and marks the connection, so further packets from it are
        dropped without being parsed, counted or logged again.
        \param  peer    the connection to drop
        \param  reason  short description for the single log line
    */
    void beginPeerDisconnect(NetPeer* peer, const char* reason);

    /**
        Accounts the raw size of an inbound packet against this peer's byte budget.
        \param  peer        the connection the packet arrived on
        \param  byteCount   size of the packet as delivered by ENet
        \return true if the packet may be parsed
    */
    bool acceptIncomingBytes(NetPeer* peer, std::size_t byteCount);

    class PeerData {
    public:
        enum class PeerState {
            WaitingForConnect,
            WaitingForName,
            ReadyForOtherPeersToConnect,
            WaitingForOtherPeersToConnect,
            Connected
        };


        PeerData(NetPeer* pPeer, PeerState peerState)
         : pPeer(pPeer), peerState(peerState), timeout(0)  {
        }


        NetPeer*                pPeer;

        PeerState               peerState;
        Uint32                  timeout;

        std::string             name;
        bool                    bNameAssigned = false;  ///< identity is bound once and never re-bound
        Uint32                  clientId = 0;           ///< stable per-connection id (not an address hash)
        std::string             gameVersion;
        std::string             quantBotConfigHash;
        std::string             objectDataHash;
        std::string             modRevisionHash;
        std::list<NetPeer*>      notYetConnectedPeers;

        // Abuse accounting: a legitimate peer never trips these.
        NetworkPacketPolicy::RefusalCounter refusals;
        NetworkPacketPolicy::RateWindow     packetWindow;
        NetworkPacketPolicy::RateWindow     byteWindow;
        Uint32                  lastRejectLogTime = 0;
    };

    /**
        Allocates peer state and gives the connection a stable local id.
        \param  peer        the connection the state belongs to
        \param  peerState   the initial handshake state
        \return the new peer state (ownership stays with peer->data)
    */
    PeerData* createPeerData(NetPeer* peer, PeerData::PeerState peerState);

    // The path budget, start-game countdown and chat length bounds moved to
    // src/Network/GamePayloadRouter.cpp with the payload handling they belong to. Two copies of
    // one limit is how limits drift apart.

    /// Mod checksums are 16 hex characters; this leaves room without allowing junk.
    static constexpr std::size_t MAX_MOD_CHECKSUM_LENGTH = 128;
    /// Longest status message accepted with a mod transfer result.
    static constexpr std::size_t MAX_MOD_MESSAGE_LENGTH = 256;
    /// The ENet host is created with 32 peer slots; the mesh can never legitimately exceed it.
    static constexpr std::size_t MAX_MESH_PEERS = 32;
    /// Traffic budgets and abuse thresholds live in NetworkPacketPolicy so they stay
    /// transport independent and the tests use the same values the production path does.
    static constexpr Uint32     MAX_REJECTED_PACKETS_PER_PEER = NetworkPacketPolicy::kMaxRefusalsPerBurst;
    static constexpr Uint32     REJECT_DECAY_MS = NetworkPacketPolicy::kRefusalDecayMs;
    static constexpr Uint32     MAX_PACKETS_PER_PEER_PER_SECOND = NetworkPacketPolicy::kMaxPacketsPerWindow;
    static constexpr Uint32     BYTE_WINDOW_MS = NetworkPacketPolicy::kTrafficWindowMs;
    static constexpr Uint64     MAX_PEER_BYTES_PER_SECOND = NetworkPacketPolicy::kMaxPeerBytesPerWindow;
    static constexpr Uint64     MAX_MOD_TRANSFER_BYTES_PER_SECOND = NetworkPacketPolicy::kMaxModTransferBytesPerWindow;
    /// Rejection log lines per peer are throttled to one per this many milliseconds.
    static constexpr Uint32     REJECT_LOG_INTERVAL_MS = 5000;
    /// Largest single packet ENet will reassemble. Applied before the reassembly buffer is
    /// allocated, so an announced fragment total beyond it costs nothing. The largest
    /// legitimate packet is a multiplayer map inside SENDGAMEINFO; a mod chunk is 64 KiB.
    static constexpr std::size_t MAX_ENET_PACKET_SIZE = 4u * 1024 * 1024;
    /// Aggregate incoming data ENet may hold for one peer while commands are reassembled.
    static constexpr std::size_t MAX_ENET_WAITING_DATA = 16u * 1024 * 1024;

    std::unique_ptr<GameInitSettings> pendingCoopMission;
    std::optional<coop::Context> coopCampaignContext;
    Uint32 coopCampaignEpoch = 0;
    std::vector<coop::ControlMessage> pendingCoopReady, pendingCoopAdvance;
    struct CoopSetupPayload { std::string bytes, header; };
    struct CoopSetupOutgoing {
        std::shared_ptr<const CoopSetupPayload> payload;
        Uint32 transaction = 0, nextOffset = 0, expectedAck = 0, deadline = 0;
        bool readyToSend = false;
    };
    struct CoopSetupIncoming {
        std::string bytes;
        Uint32 transaction = 0, total = 0, decodedSize = 0, deadline = 0, hostPeer = 0;
        bool awaitingLobby = false;
    } coopSetupIncoming;
    std::map<Uint32, CoopSetupOutgoing> coopSetupOutgoing;
    Uint32 nextCoopSetupTransaction = 0;
    bool queueCoopSetup(const std::uint8_t* payload, std::size_t length, int channel, Uint32 recipient);
    bool sendCoopSetupRecord(Uint32 recipient, Uint8 operation, Uint32 transaction,
                             Uint32 offset, const std::string& data);
    void receiveCoopSetup(Uint32 peer, bool peerIsHost, InputStream& stream);
    void updateCoopSetup();
    void failCoopSetup(const char* reason);
    Transport transport = Transport::EnetMesh;
    std::unique_ptr<RoomSessionTransport> pRelayClient;
    Uint32 nextClientId = 1;        ///< source of the stable per-connection client ids
    Uint32 lastUnidentifiedLogTime = 0;  ///< throttles logging for connections without peer state
    Uint32 simulationSeed = 0;
#ifndef __EMSCRIPTEN__
    ENetHost* host = nullptr;
#endif
    bool bIsServer = false;
    bool bLANServer = false;
    bool bGameInProgress = false;  // Set true when game starts - disables lobby-only features
    GameInitSettings* pGameInitSettings = nullptr;
    int numPlayers = 0;
    int maxPlayers = 0;

    std::string playerName;

    NetPeer*   connectPeer = nullptr;

    std::list<NetPeer*> peerList;

    std::list<NetPeer*> awaitingConnectionList;

#ifdef __EMSCRIPTEN__
    std::unique_ptr<WebRtcTransport> pWebRtcTransport;
    uint32_t connectPeerWebRtcId = 0;   // transport handle of the host we joined
    bool bWebRtcHost = false;           // role assigned by the lobby (Matched event)
    std::function<void (bool)> pOnMatched;
#endif

    std::function<void (const std::string&, const std::string&)>            pOnReceiveChatMessage;
    std::function<void (const GameInitSettings&, const ChangeEventList&)>   pOnReceiveGameInfo;
    std::function<void (const std::string&, const ChangeEventList&)>        pOnReceiveChangeEventList;
    std::function<void (const std::string&, bool, int)>                     pOnPeerDisconnected;
    std::function<ChangeEventList (const std::string&)>                     pGetChangeEventListForNewPlayerCallback;
    std::function<void (unsigned int)>                                      pOnStartGame;
    std::function<void (unsigned int)>                                      pOnStartGameBridge;
    std::function<void (const std::string&, const CommandList&)>            pOnReceiveCommandList;
    std::function<void (const std::string&, const std::set<Uint32>&, int)>  pOnReceiveSelectionList;
    std::function<void (const std::string&)>                                 pOnConfigMismatch;
    std::function<void (Uint32, Uint32, float, float, Uint32, Uint32)>     pOnReceiveClientStats;      // Host: (clientId, gameCycle, avgFps, simMsAvg, queueDepth, currentBudget)
    std::function<void (size_t, Uint32)>                                     pOnReceiveSetPathBudget;    // Client: (newBudget, applyCycle)
    std::function<void (Uint32, Uint32, Uint32, Uint32)>                    pOnReceiveMatchControl;     // Client: (revision, speed, pauseCycle, resumedPauseCycle)
    std::function<void (const std::string&, Uint32)>                        pOnReceiveMatchResumeRequest; // Host: (bound peer name, pauseCycle)
    std::function<void (const std::string&, const std::string&)>            pOnReceiveModInfo;          // Client: (modName, modChecksum)
    std::function<void (size_t, size_t)>                                     pOnModDownloadProgress;     // Client: (bytesReceived, totalBytes)
    std::function<void (bool, const std::string&)>                          pOnModDownloadComplete;     // Client: (success, errorMsg)
    std::function<void (const std::string&, bool, const std::string&)>      pOnReceiveModAck;           // Host: (playerName, success, modChecksum)
    std::function<void (const std::string&, std::uint8_t, const std::uint8_t*, std::size_t)> pOnReceiveRelayDiagnostic;
    /// Bridges the shared payload handling back into pendingCoopMission; set by both constructors.
    std::function<void (const GameInitSettings&)>                          pOnReceiveCoopMissionBridge;
    std::function<void (const coop::ControlMessage&)> pOnReceiveCoopControlBridge;

    /// Installs the callbacks the shared payload handling needs from the session itself.
    void installSessionBridges();

    // Mod transfer state (for chunked transfer)
    struct ModTransferState {
        std::string modName;
        std::string modData;
        size_t totalSize = 0;
        size_t receivedSize = 0;
        bool inProgress = false;
        bool requested = false;         ///< true once this client asked the host for a mod
        std::string requestedModName;   ///< the mod this client asked for; chunks must match it
    };
    ModTransferState modTransferState;

    /// Clears a mod download, optionally reporting the failure to the lobby.
    void abortModTransfer(const char* reason);

    // Keep-alive: send a reliable ping every 10 seconds (ENet reliable packet
    // natively, control DataChannel in the browser build) to keep the
    // connection warm.
    Uint32                                      lastKeepAliveTime = 0;
    static constexpr int                        KEEPALIVE_INTERVAL_MS = 10000;   // 10 seconds

#ifndef __EMSCRIPTEN__
    std::unique_ptr<LANGameFinderAndAnnouncer>  pLANGameFinderAndAnnouncer = nullptr;
    std::unique_ptr<MetaServerClient>           pMetaServerClient = nullptr;
    std::unique_ptr<UPnPManager>                pUPnPManager = nullptr;
    bool                                        upnpPortMapped = false;
    uint16_t                                    upnpMappedPort = 0;
    Uint32                                      upnpLeaseStartTime = 0;
    static constexpr int                        UPNP_LEASE_DURATION = 3600;      // 1 hour lease
    static constexpr int                        UPNP_RENEWAL_MARGIN = 300;       // Renew 5 min before expiry

    // NAT Hole Punch: Non-blocking state machine for host-side punching
    struct PendingPunch {
        std::string clientId;
        std::string clientIP;
        uint16_t clientPort = 0;
        Uint32 punchAtTime = 0;       // SDL_GetTicks() when to start punching
        int packetsRemaining = 0;     // Packets left to send
        Uint32 lastPacketTime = 0;    // For pacing packets
    };
    std::vector<PendingPunch>                   pendingPunches;
    Uint32                                      lastPunchPollTime = 0;
    static constexpr int                        PUNCH_POLL_INTERVAL_MS = 1000;   // Poll every 1s
    static constexpr int                        PUNCH_DELAY_MS = 2000;           // Delay before punching
    static constexpr int                        PUNCH_PACKET_COUNT = 10;         // Packets per punch
    static constexpr int                        PUNCH_PACKET_INTERVAL_MS = 50;   // Interval between packets

public:
    /**
     * Get UPnP status information
     */
    bool isUPnPAvailable() const { return pUPnPManager && pUPnPManager->isAvailable(); }
    bool isUPnPPortMapped() const { return upnpPortMapped; }
    std::string getUPnPStatus() const { return pUPnPManager ? pUPnPManager->getStatusString() : "Not initialized"; }
    std::string getExternalIPAddress() const { return pUPnPManager ? pUPnPManager->getExternalIPAddress() : ""; }
    
    /**
     * NAT Hole Punch: Send UDP punch packets to an address to create NAT mappings.
     * @param targetIP   Target IP address
     * @param targetPort Target port
     * @param count      Number of packets to send (default: 5)
     * @param intervalMs Interval between packets in ms (default: 50)
     */
    void sendHolePunchPackets(const std::string& targetIP, uint16_t targetPort, int count = 5, int intervalMs = 50);
    
    /**
     * Perform STUN query to discover external IP:port.
     * SAFETY: Only call when no ENet peers exist (peerList empty).
     * @return External port if successful, 0 on failure
     */
    uint16_t performStunQuery();
    
    /**
     * Perform STUN query and return both external IP and port.
     * SAFETY: Only call when no ENet peers exist (peerList empty).
     * @param outIP  Will be set to the external IP if successful
     * @param outPort Will be set to the external port if successful
     * @return true if successful, false on failure
     */
    bool performStunQueryFull(std::string& outIP, uint16_t& outPort);
    
    /**
     * Get the ENet host (for STUN queries)
     */
    ENetHost* getHost() const { return host; }
#endif // __EMSCRIPTEN__
};

#endif // NETWORKMANAGER_H
