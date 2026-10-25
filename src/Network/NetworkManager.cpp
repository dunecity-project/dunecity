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

#include <Network/NetworkManager.h>

#include <config.h>

#include <Network/ContentCompatibility.h>
#include <Network/ENetHelper.h>
#include <Network/StunClient.h>

#include <GameInitSettings.h>
#include <Network/GameInitSettingsPolicy.h>

#include <misc/exceptions.h>
#include <misc/FileSystem.h>
#include <misc/fnkdat.h>

#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <Network/OnlineModPolicy.h>
#include <mod/ModTransferValidation.h>

#include <globals.h>
#include <players/QuantBotConfig.h>

#include <stdio.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <set>

namespace {

/// Peer endpoint label for log lines. Browser peers have no address; the transport id is
/// the closest thing to an identity a log reader can act on.
#ifdef __EMSCRIPTEN__
std::string peerAddressLabel(const NetPeer* peer) {
    return "webrtc:" + std::to_string(peer->webRtcPeerId);
}
#else
std::string peerAddressLabel(const ENetPeer* peer) {
    return Address2String(peer->address);
}
#endif

/**
    Presents one peer of either transport to the shared payload handling.

    The adapter holds pointers to the fields that actually live in the transport's own peer
    record and small hooks for the four things that differ (refusing a payload, binding a name,
    answering a config hash, ending the session). That is enough for GamePayloadRouter to be
    written once, and it keeps the relay path from ever seeing an ENetPeer.
*/
class PayloadPeerAdapter final : public GamePayloadPeer {
public:
    struct Fields {
        Uint32       clientId           = 0;
        std::string* name               = nullptr;
        bool*        nameAssigned       = nullptr;
        bool         isHost             = false;
        std::string* gameVersion        = nullptr;
        std::string* quantBotConfigHash = nullptr;
        std::string* objectDataHash     = nullptr;
        std::string* modRevisionHash = nullptr;
    };

    PayloadPeerAdapter(const Fields& fields,
                       std::function<void (const char*)> refuseFn,
                       std::function<bool (const std::string&)> bindNameFn,
                       std::function<void ()> replyConfigHashFn,
                       std::function<void (int)> disconnectFn)
     : fields_(fields), refuse_(std::move(refuseFn)), bindName_(std::move(bindNameFn)),
       replyConfigHash_(std::move(replyConfigHashFn)), disconnect_(std::move(disconnectFn)) {
    }

    Uint32 clientId() const override { return fields_.clientId; }

    const std::string& name() const override {
        static const std::string empty;
        return fields_.name != nullptr ? *fields_.name : empty;
    }

    bool nameAssigned() const override {
        return fields_.nameAssigned != nullptr && *fields_.nameAssigned;
    }

    bool bindName(const std::string& newName) override {
        return bindName_ ? bindName_(newName) : false;
    }

    bool isHostPeer() const override { return fields_.isHost; }

    void refuse(const char* reason) override {
        if(refuse_) {
            refuse_(reason);
        }
    }

    void replyConfigHash() override {
        if(replyConfigHash_) {
            replyConfigHash_();
        }
    }

    std::string& gameVersion() override { return field(fields_.gameVersion); }
    std::string& quantBotConfigHash() override { return field(fields_.quantBotConfigHash); }
    std::string& objectDataHash() override { return field(fields_.objectDataHash); }
    std::string& modRevisionHash() override { return field(fields_.modRevisionHash); }

    void disconnectWithCause(int cause) override {
        if(disconnect_) {
            disconnect_(cause);
        }
    }

private:
    std::string& field(std::string* pointer) {
        return pointer != nullptr ? *pointer : discard_;
    }

    Fields      fields_;
    std::string discard_;
    std::function<void (const char*)>        refuse_;
    std::function<bool (const std::string&)> bindName_;
    std::function<void ()>                   replyConfigHash_;
    std::function<void (int)>                disconnect_;
};

/// A relay peer's name was bound by the relay when it admitted the connection, and there is no
/// rename message on that transport. Nothing ever writes this; it exists so the adapter can
/// point at a "yes" for peers whose identity is not the client's to change.
bool relayPeerNamesAreBound = true;

} // namespace

void NetworkManager::installSessionBridges() {
    pOnJoinSync=[this](Uint32 peer,Uint32 operation,Uint32 transaction,Uint32 offset,const std::string& data) {
        receiveJoinSync(peer,operation,transaction,offset,data);
    };
    pOnStartGameBridge = [this](unsigned int timeLeft) {
        // The packet router has already verified that STARTGAME came from the host.
        // Freeze now, before the countdown allows a membership change to alter this match.
        if(auto* direct = getDirectTransport()) {
            if(!direct->acceptStartCallback()) return;
        }
        if(joinStage==JoinStage::Starting || joinStage==JoinStage::Receiving) {
            if(joinSnapshot) { joinStage=JoinStage::Ready; joinStatus="Resuming with "+joinName+"..."; }
            else abortLateJoin("The game snapshot was not ready.");
            return;
        }
        if(pOnStartGame) pOnStartGame(timeLeft);
    };
    pOnReceiveCoopMissionBridge = [this](const GameInitSettings& settings) {
        pendingCoopMission = std::make_unique<GameInitSettings>(settings);
    };
}

#ifdef __EMSCRIPTEN__

NetworkManager::NetworkManager(int port, const std::string& metaserver) {
    // Browser build: no ENet, no LAN discovery, no metaserver, no UPnP. All
    // traffic flows through the WebRTC transport created here. Room sessions
    // (relay/direct) still use the NetworkManager(Transport) constructor below.
    (void) port;
    (void) metaserver;

    installSessionBridges();

    pWebRtcTransport = std::make_unique<WebRtcTransport>();
}

NetworkManager::~NetworkManager() {
    clearAllPeers();
    pWebRtcTransport.reset();

    if(pRelayClient) {
        pRelayClient->stop(1);
        pRelayClient.reset();
    }
}

#else // native desktop

NetworkManager::NetworkManager(int port, const std::string& metaserver) {
    installSessionBridges();

    if(enet_initialize() != 0) {
        THROW(std::runtime_error, "NetworkManager: An error occurred while initializing ENet.");
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    host = enet_host_create(&address, 32, 2, 0, 0);
    if(host == nullptr) {
        enet_deinitialize();
        THROW(std::runtime_error, "NetworkManager: An error occurred while trying to create a server host.");
    }

    if(enet_host_compress_with_range_coder(host) < 0) {
        enet_deinitialize();
        THROW(std::runtime_error, "NetworkManager: Cannot activate range coder.");
    }

    // Bound what ENet itself will allocate for an inbound peer before we ever see a packet.
    // maximumPacketSize is checked against the announced fragment total *before* the
    // reassembly buffer is allocated (src/enet/protocol.c), and maximumWaitingData bounds the
    // data one peer may have queued. Both defaults are 32 MiB, far above anything this
    // protocol sends: the largest legitimate packet is a map inside SENDGAMEINFO and the
    // largest legitimate burst is a 10 MiB mod transfer in 64 KiB chunks.
    host->maximumPacketSize = MAX_ENET_PACKET_SIZE;
    host->maximumWaitingData = MAX_ENET_WAITING_DATA;

    try {
        pLANGameFinderAndAnnouncer = std::make_unique<LANGameFinderAndAnnouncer>();
        pMetaServerClient = std::make_unique<MetaServerClient>(metaserver);
        pUPnPManager = std::make_unique<UPnPManager>();
        // UPnP discovery is deferred to startServer() to avoid startup delay
        // when just joining games or playing offline
    } catch (...) {
        enet_deinitialize();
        throw;
    }
}


#endif // __EMSCRIPTEN__

NetworkManager::NetworkManager(Transport transportMode)
 : transport(transportMode) {
    installSessionBridges();

    if(transportMode == Transport::EnetMesh) {
        THROW(std::runtime_error,
              "NetworkManager: the mesh transport needs a port and a metaserver.");
    }

    // enet_initialize() only installs the allocation callbacks; it opens no socket. The packet
    // streams are reused verbatim on the relay so the serialized game payloads stay identical,
    // which is why this is called even though no ENet host is ever created here.
    if(enet_initialize() != 0) {
        THROW(std::runtime_error, "NetworkManager: An error occurred while initializing ENet.");
    }

    // No LAN announcer, no metaserver thread, no UPnP: a browser cannot run any of them, and a
    // relay session does not need them on any platform.
}

#ifndef __EMSCRIPTEN__
NetworkManager::~NetworkManager() {
    if(pRelayClient) {
        pRelayClient->stop(1);
        pRelayClient.reset();
    }

    // Remove UPnP port mapping if active
    // Note: We attempt removal even if previous attempts failed, and log but don't block on failure
    if (upnpMappedPort != 0 && pUPnPManager) {
        if (pUPnPManager->removePortMapping(upnpMappedPort, "UDP")) {
            SDL_Log("NetworkManager: UPnP port mapping removed on shutdown");
        } else {
            SDL_Log("NetworkManager: Warning - failed to remove UPnP port mapping on shutdown");
        }
        upnpPortMapped = false;
        upnpMappedPort = 0;
    }

    pUPnPManager.reset();
    pMetaServerClient.reset();
    pLANGameFinderAndAnnouncer.reset();
    if(host != nullptr) {
        enet_host_destroy(host);
        host = nullptr;
    }
    enet_deinitialize();
}

#endif // __EMSCRIPTEN__

void NetworkManager::startServer(bool bLANServer, const std::string& serverName, const std::string& playerName, GameInitSettings* pGameInitSettings, int numPlayers, int maxPlayers) {
    // Reset game-in-progress flag for new game. The same NetworkManager instance is reused
    // when a player returns from a match and hosts another one, so the previous match's phase
    // and seed must not leak into this session.
    bGameInProgress = false;
    simulationSeed = 0;

    if(isRoomSession()) {
        // The room already exists and the relay already knows who the host is. There is nothing
        // to announce, no port to forward and no address to discover.
        bIsServer = true;
        this->bLANServer = false;
        this->numPlayers = numPlayers;
        this->maxPlayers = maxPlayers;
        pendingCoopMission.reset();
        this->playerName = playerName;
        this->pGameInitSettings = pGameInitSettings;

        // Any peer that is already in the room needs the lobby state now.
        if(pRelayClient && pGameInitSettings != nullptr) {
            for(const RoomSessionTransport::Peer& peer : pRelayClient->peers()) {
                NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                packetStream.writeUint32(NETWORKPACKET_SENDGAMEINFO);
                pGameInitSettings->save(packetStream);
                ChangeEventList changeEventList = pGetChangeEventListForNewPlayerCallback
                    ? pGetChangeEventListForNewPlayerCallback(peer.name) : ChangeEventList();
                changeEventList.save(packetStream);
                sendPacketOverRelay(packetStream, 0, peer.id);
            }
        }
        return;
    }

#ifdef __EMSCRIPTEN__
    // Browser host: the matchmaking lobby already paired the transport (see
    // connectWebRtc); hosting just means taking the server role for the
    // post-connect handshake.
    (void) bLANServer;
    (void) serverName;
#else
    if(bLANServer == true) {
        if(pLANGameFinderAndAnnouncer != nullptr) {
            pLANGameFinderAndAnnouncer->startAnnounce(serverName, host->address.port, pGameInitSettings->getFilename(), numPlayers, maxPlayers);
        }
    } else {
        // Internet game - try UPnP port mapping
        if (pUPnPManager && !upnpPortMapped) {
            // Discover UPnP devices if not already done (deferred from constructor)
            // Only attempt discovery once - don't retry if it failed before
            if (!pUPnPManager->wasDiscoveryAttempted()) {
                SDL_Log("NetworkManager: Discovering UPnP devices...");
                if (pUPnPManager->discover(2000)) {
                    SDL_Log("NetworkManager: UPnP available - automatic port forwarding enabled");
                } else {
                    SDL_Log("NetworkManager: UPnP not available - manual port forwarding may be required");
                }
            }
            
            // Try to add port mapping if UPnP is available
            // Use 1 hour lease (3600s) instead of permanent - will be renewed if game runs longer
            if (pUPnPManager->isAvailable()) {
                if (pUPnPManager->addPortMapping(host->address.port, host->address.port, "UDP", "Dune City", UPNP_LEASE_DURATION)) {
                    upnpPortMapped = true;
                    upnpMappedPort = host->address.port;
                    upnpLeaseStartTime = SDL_GetTicks();
                    SDL_Log("NetworkManager: UPnP port %d mapped successfully (%d min lease, auto-renews)", 
                            host->address.port, UPNP_LEASE_DURATION / 60);
                } else {
                    SDL_Log("NetworkManager: UPnP port mapping failed - manual port forwarding may be required");
                }
            }
        }
        
        if(pMetaServerClient != nullptr) {
            // Get active mod info
            ModInfo activeModInfo = ModManager::instance().getModInfo(ModManager::instance().getActiveModName());
            
            // NAT traversal: Perform STUN query to discover external IP:port
            // SAFETY: STUN only runs here because peerList is empty (pre-connection)
            uint16_t stunPort = 0;
            if (host != nullptr && host->socket != ENET_SOCKET_NULL && peerList.empty()) {
                SDL_Log("NetworkManager: Performing STUN query for NAT traversal...");
                StunClient::StunResult stunResult = StunClient::performStunQuery(host->socket);
                if (stunResult.success) {
                    stunPort = stunResult.externalPort;
                    SDL_Log("NetworkManager: STUN discovered external address %s:%d", 
                            stunResult.externalIP.c_str(), stunResult.externalPort);
                } else {
                    SDL_Log("NetworkManager: STUN query failed: %s (will announce without STUN port)", 
                            stunResult.errorMessage.c_str());
                }
            }
            
            pMetaServerClient->startAnnounce(serverName, host->address.port, pGameInitSettings->getFilename(), numPlayers, maxPlayers,
                                             activeModInfo.name, activeModInfo.version, stunPort);
        }
    }
#endif // __EMSCRIPTEN__

    bIsServer = true;
    this->bLANServer = bLANServer;
    this->numPlayers = numPlayers;
    this->maxPlayers = maxPlayers;
    pendingCoopMission.reset();
    this->playerName = playerName;
    this->pGameInitSettings = pGameInitSettings;
}

void NetworkManager::updateServer(int numPlayers) {
    if(isRoomSession()) {
        this->numPlayers = numPlayers;
        return;
    }

#ifndef __EMSCRIPTEN__
    if(bLANServer == true) {
        if(pLANGameFinderAndAnnouncer != nullptr) {
            pLANGameFinderAndAnnouncer->updateAnnounce(numPlayers);
        }
    } else {
        if(pMetaServerClient != nullptr) {
            pMetaServerClient->updateAnnounce(numPlayers);
        }
    }
#endif

    this->numPlayers = numPlayers;
}

void NetworkManager::stopAnnouncing() {
    if(isRoomSession()) {
        // Nothing is announced, but the phase flip still matters: it is what stops lobby-only
        // packets being accepted from this point on.
        bGameInProgress = true;
        return;
    }

    // Stop announcing the game in the lobby/server list
    // This is called when the game starts, but the server should remain active
#ifndef __EMSCRIPTEN__
    if(bLANServer == true) {
        if(pLANGameFinderAndAnnouncer != nullptr) {
            pLANGameFinderAndAnnouncer->stopAnnounce();
        }
    } else {
        if(pMetaServerClient != nullptr) {
            pMetaServerClient->stopAnnounce();
        }
    }
#endif
    // NOTE: bIsServer remains TRUE so the host can continue managing the game

    // Mark game as in progress - this disables lobby-only features like NAT hole punch polling
    // (which uses blocking HTTP calls that would cause major stutter during gameplay)
    bGameInProgress = true;
    SDL_Log("NetworkManager: Game in progress - lobby features disabled");
}

void NetworkManager::stopServer() {
    stopAnnouncing();

    if(isRoomSession()) {
        bIsServer = false;
        bLANServer = false;
        pGameInitSettings = nullptr;
        return;
    }

#ifndef __EMSCRIPTEN__
    // Remove UPnP port mapping if active
    if (upnpPortMapped && pUPnPManager && upnpMappedPort != 0) {
        if (pUPnPManager->removePortMapping(upnpMappedPort, "UDP")) {
            upnpPortMapped = false;
            upnpMappedPort = 0;
            upnpLeaseStartTime = 0;
            SDL_Log("NetworkManager: UPnP port mapping removed");
        } else {
            // Keep upnpPortMapped true so destructor can retry
            SDL_Log("NetworkManager: Warning - failed to remove UPnP port mapping, will retry on exit");
        }
    }
#endif // __EMSCRIPTEN__

    // Fully stop the server (called when leaving a game or menu)
    bIsServer = false;
    bLANServer = false;
    // NOTE: Do NOT reset bGameInProgress here - it should remain true while game is active
    // It will be reset when NetworkManager is destroyed or when a new server is started
    pGameInitSettings = nullptr;
}

#ifndef __EMSCRIPTEN__

void NetworkManager::sendHolePunchPackets(const std::string& targetIP, uint16_t targetPort, int count, int intervalMs) {
    if (host == nullptr || host->socket == ENET_SOCKET_NULL) {
        SDL_Log("NetworkManager::sendHolePunchPackets - No socket available");
        return;
    }
    
    // Resolve target address
    ENetAddress targetAddress;
    if (enet_address_set_host(&targetAddress, targetIP.c_str()) < 0) {
        SDL_Log("NetworkManager::sendHolePunchPackets - Failed to resolve %s", targetIP.c_str());
        return;
    }
    targetAddress.port = targetPort;
    
    // Send punch packets - "DLHP" (Dune Legacy Hole Punch) signature
    const uint8_t punchData[] = {'D', 'L', 'H', 'P'};
    
    SDL_Log("NetworkManager: Sending %d hole punch packets to %s:%d", count, targetIP.c_str(), targetPort);
    
    for (int i = 0; i < count; i++) {
        ENetBuffer sendBuffer;
        sendBuffer.data = const_cast<uint8_t*>(punchData);
        sendBuffer.dataLength = sizeof(punchData);
        
        int sent = enet_socket_send(host->socket, &targetAddress, &sendBuffer, 1);
        if (sent < 0) {
            SDL_Log("NetworkManager::sendHolePunchPackets - Send failed on packet %d", i + 1);
        }
        
        if (i < count - 1 && intervalMs > 0) {
            SDL_Delay(intervalMs);
        }
    }
    
    SDL_Log("NetworkManager: Hole punch packets sent");
}

uint16_t NetworkManager::performStunQuery() {
    if (host == nullptr || host->socket == ENET_SOCKET_NULL) {
        SDL_Log("NetworkManager::performStunQuery - No socket available");
        return 0;
    }
    
    if (!peerList.empty()) {
        SDL_Log("NetworkManager::performStunQuery - Cannot run with active peers");
        return 0;
    }
    
    StunClient::StunResult result = StunClient::performStunQuery(host->socket);
    if (result.success) {
        SDL_Log("NetworkManager::performStunQuery - External: %s:%d", 
                result.externalIP.c_str(), result.externalPort);
        return result.externalPort;
    } else {
        SDL_Log("NetworkManager::performStunQuery - Failed: %s", result.errorMessage.c_str());
        return 0;
    }
}

bool NetworkManager::performStunQueryFull(std::string& outIP, uint16_t& outPort) {
    if (host == nullptr || host->socket == ENET_SOCKET_NULL) {
        SDL_Log("NetworkManager::performStunQueryFull - No socket available");
        return false;
    }
    
    if (!peerList.empty()) {
        SDL_Log("NetworkManager::performStunQueryFull - Cannot run with active peers");
        return false;
    }
    
    StunClient::StunResult result = StunClient::performStunQuery(host->socket);
    if (result.success) {
        outIP = result.externalIP;
        outPort = result.externalPort;
        SDL_Log("NetworkManager::performStunQueryFull - External: %s:%d", 
                outIP.c_str(), outPort);
        return true;
    } else {
        SDL_Log("NetworkManager::performStunQueryFull - Failed: %s", result.errorMessage.c_str());
        return false;
    }
}

void NetworkManager::connect(const std::string& hostname, int port, const std::string& playerName) {
    ENetAddress address;

    if(enet_address_set_host(&address, hostname.c_str()) < 0) {
        THROW(std::runtime_error, "NetworkManager: Resolving hostname '" + hostname + "' failed!");
    }
    address.port = port;

    connect(address, playerName);
}

void NetworkManager::connect(ENetAddress address, const std::string& playerName) {
    if(isRoomSession() || host == nullptr) {
        // A relay session reaches other players through the room, never through an address of
        // somebody else's choosing. There is no code path from here to a UDP connect.
        THROW(std::runtime_error,
              "NetworkManager: this session does not connect to addresses.");
    }

    debugNetwork("Connecting to %s:%d\n", Address2String(address).c_str(), address.port);

    // A new client session starts in the lobby again. The same NetworkManager instance is
    // reused when a player returns from a match and joins another game, so the phase and the
    // simulation seed of the previous match must not leak into this one.
    bGameInProgress = false;
    simulationSeed = 0;
    modTransferState = ModTransferState();

    connectPeer = enet_host_connect(host, &address, 2, 0);
    if(connectPeer == nullptr) {
        THROW(std::runtime_error, "NetworkManager: No available peers for initiating a connection.");
    }

    pendingCoopMission.reset();
    this->playerName = playerName;

    connectPeer->data = createPeerData(connectPeer, PeerData::PeerState::WaitingForConnect);
    awaitingConnectionList.push_back(connectPeer);
}

#else // __EMSCRIPTEN__

void NetworkManager::connectWebRtc(const std::string& playerName) {
    if(pWebRtcTransport->findMatch() == false) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Could not enter the matchmaking lobby");
        return;
    }

    clearAllPeers();
    bIsServer = false;
    bGameInProgress = false;
    simulationSeed = 0;
    pGameInitSettings = nullptr;
    pendingCoopMission.reset();
    modTransferState = ModTransferState();
    this->playerName = playerName;

    // The lobby reports the pairing as a Matched event (host/joiner role).
    // The NetPeer for the opponent is created when the transport reports the
    // Connect event (both DataChannels open).
}

void NetworkManager::cancelMatchmaking() {
    pWebRtcTransport->cancelMatchmaking();
    // Cancel also works after pairing, while WebRTC is still connecting.
    disconnect();
}

#endif // __EMSCRIPTEN__

void NetworkManager::disconnect() {
    if(isRoomSession()) {
        if(pRelayClient) {
            pRelayClient->stop(1 /* the player left */);
        }
        return;
    }

#ifdef __EMSCRIPTEN__
    clearAllPeers();
    pWebRtcTransport->disconnect();
#else
    for(ENetPeer* pAwaitingConnectionPeer : awaitingConnectionList) {
        enet_peer_disconnect_later(pAwaitingConnectionPeer, NETWORKDISCONNECT_QUIT);
    }
    for(ENetPeer* pCurrentPeer : peerList) {
        enet_peer_disconnect_later(pCurrentPeer, NETWORKDISCONNECT_QUIT);
    }
#endif
}

void NetworkManager::update()
{
    if(isRoomSession()) {
        updateRelaySession();
        updateLateJoin();
        updateObservers();
        return;
    }

#ifndef __EMSCRIPTEN__
    if(pLANGameFinderAndAnnouncer != nullptr) {
        pLANGameFinderAndAnnouncer->update();
    }

    if(pMetaServerClient != nullptr) {
        pMetaServerClient->update();
    }
    
    // Renew UPnP lease before it expires (5 minutes before expiry)
    if (upnpPortMapped && pUPnPManager && upnpLeaseStartTime != 0) {
        Uint32 elapsed = (SDL_GetTicks() - upnpLeaseStartTime) / 1000;  // seconds
        if (elapsed >= (UPNP_LEASE_DURATION - UPNP_RENEWAL_MARGIN)) {
            SDL_Log("NetworkManager: Renewing UPnP port mapping lease...");
            if (pUPnPManager->addPortMapping(upnpMappedPort, upnpMappedPort, "UDP", "Dune City", UPNP_LEASE_DURATION)) {
                upnpLeaseStartTime = SDL_GetTicks();
                SDL_Log("NetworkManager: UPnP lease renewed successfully");
            } else {
                SDL_Log("NetworkManager: Warning - UPnP lease renewal failed");
            }
        }
    }
    
    // NAT Hole Punch: Non-blocking state machine for host-side punching
    // Only when hosting an internet game (not LAN) and NOT in an active game
    // CRITICAL: This uses blocking HTTP calls - MUST NOT run during gameplay!
    if (bIsServer && !bLANServer && !bGameInProgress && pMetaServerClient != nullptr) {
        Uint32 now = SDL_GetTicks();
        
        // Step 1: Poll for new punch requests (every 1 second)
        if (now - lastPunchPollTime >= PUNCH_POLL_INTERVAL_MS) {
            lastPunchPollTime = now;
            
            std::vector<std::tuple<std::string, std::string, uint16_t>> punchRequests;
            if (pMetaServerClient->pollPunchRequests(punchRequests) && !punchRequests.empty()) {
                for (const auto& request : punchRequests) {
                    std::string clientId = std::get<0>(request);
                    std::string clientIP = std::get<1>(request);
                    uint16_t clientPort = std::get<2>(request);
                    
                    SDL_Log("NAT Hole Punch: Received punch request from %s:%d (id: %s)",
                            clientIP.c_str(), clientPort, clientId.c_str());
                    
                    // Signal ready to punch (non-blocking - just HTTP GET)
                    if (pMetaServerClient->signalPunchReady(clientId)) {
                        // Schedule punch for PUNCH_DELAY_MS from now (no blocking!)
                        PendingPunch pending;
                        pending.clientId = clientId;
                        pending.clientIP = clientIP;
                        pending.clientPort = clientPort;
                        pending.punchAtTime = now + PUNCH_DELAY_MS;
                        pending.packetsRemaining = PUNCH_PACKET_COUNT;
                        pending.lastPacketTime = 0;
                        pendingPunches.push_back(pending);
                        
                        SDL_Log("NAT Hole Punch: Scheduled punch to %s:%d in %dms",
                                clientIP.c_str(), clientPort, PUNCH_DELAY_MS);
                    }
                }
            }
        }
        
        // Step 2: Process pending punches (send 1 packet per interval, no blocking)
        for (auto it = pendingPunches.begin(); it != pendingPunches.end(); ) {
            PendingPunch& pending = *it;
            
            // Check if it's time to start/continue punching
            if (now >= pending.punchAtTime && pending.packetsRemaining > 0) {
                // Check if enough time passed since last packet
                if (now - pending.lastPacketTime >= PUNCH_PACKET_INTERVAL_MS) {
                    // Send one punch packet
                    if (host != nullptr && host->socket != ENET_SOCKET_NULL) {
                        ENetAddress targetAddress;
                        if (enet_address_set_host(&targetAddress, pending.clientIP.c_str()) == 0) {
                            targetAddress.port = pending.clientPort;
                            
                            const uint8_t punchData[] = {'D', 'L', 'H', 'P'};
                            ENetBuffer sendBuffer;
                            sendBuffer.data = const_cast<uint8_t*>(punchData);
                            sendBuffer.dataLength = sizeof(punchData);
                            
                            enet_socket_send(host->socket, &targetAddress, &sendBuffer, 1);
                        }
                    }
                    
                    pending.packetsRemaining--;
                    pending.lastPacketTime = now;
                    
                    if (pending.packetsRemaining == 0) {
                        SDL_Log("NAT Hole Punch: Completed punch to %s:%d",
                                pending.clientIP.c_str(), pending.clientPort);
                    }
                }
            }
            
            // Remove completed punches
            if (pending.packetsRemaining <= 0) {
                it = pendingPunches.erase(it);
            } else {
                ++it;
            }
        }
    }
#endif // __EMSCRIPTEN__

    // NAT keep-alive: Send periodic reliable ping to prevent NAT mapping timeout
    // Many routers drop UDP NAT mappings after 30-60 seconds of "inactivity"
    // (unreliable packets don't count as activity since they have no ACKs)
    if (!peerList.empty() || connectPeer != nullptr) {
        Uint32 now = SDL_GetTicks();
        if (now - lastKeepAliveTime >= KEEPALIVE_INTERVAL_MS) {
            lastKeepAliveTime = now;
            
            NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
            packetStream.writeUint32(NETWORKPACKET_KEEPALIVE);
            packetStream.writeUint32(now);  // Timestamp for debugging
            
            if (bIsServer) {
                sendPacketToAllConnectedPeers(packetStream);
            } else if (connectPeer != nullptr) {
                sendPacketToHost(packetStream);
            }
        }
    }

    if(bIsServer) {
        // Check for timeout of one client
        if(awaitingConnectionList.empty() == false) {
            NetPeer* pCurrentPeer = awaitingConnectionList.front();
            PeerData* peerData = static_cast<PeerData*>(pCurrentPeer->data);

            if(peerData->peerState == PeerData::PeerState::ReadyForOtherPeersToConnect) {
                if(numPlayers >= maxPlayers) {
                    disconnectPeer(pCurrentPeer, NETWORKDISCONNECT_GAME_FULL);
                } else {
                    // only one peer should be in state 'PeerState::WaitingForOtherPeersToConnect'
                    peerData->peerState = PeerData::PeerState::WaitingForOtherPeersToConnect;
                    peerData->timeout = SDL_GetTicks() + AWAITING_CONNECTION_TIMEOUT;
                    peerData->notYetConnectedPeers = peerList;

                    if(peerData->notYetConnectedPeers.empty()) {
                        // first client on this server
                        // => change immediately to connected

                        // get change event list first
                        ChangeEventList changeEventList = pGetChangeEventListForNewPlayerCallback(peerData->name);

                        debugNetwork("Moving '%s' from awaiting connection list to peer list\n", peerData->name.c_str());
                        peerList.push_back(pCurrentPeer);
                        peerData->peerState = PeerData::PeerState::Connected;
                        peerData->timeout = 0;
                        awaitingConnectionList.remove(pCurrentPeer);

                        // send peer game settings
                        NetworkPacketOStream packetOStream2(NETWORK_PACKET_FLAG_RELIABLE);
                        packetOStream2.writeUint32(NETWORKPACKET_SENDGAMEINFO);
                        pGameInitSettings->save(packetOStream2);

                        changeEventList.save(packetOStream2);

                        sendPacketToPeer(pCurrentPeer, packetOStream2);

                        // Send mod info to newly connected peer for mod sync
                        if(ModManager::instance().isInitialized()) {
                            std::string modName = ModManager::instance().getActiveModName();
                            std::string modChecksum = ModManager::instance().getEffectiveChecksums().combined;
                            SDL_Log("NetworkManager: Sending mod info to new peer - mod='%s', checksum=%s",
                                    modName.c_str(), modChecksum.c_str());
                            sendModInfoToPeer(pCurrentPeer, modName, modChecksum);
                        }
                    } else {
#ifdef __EMSCRIPTEN__
                        // The browser build is exactly two players; the first
                        // client always takes the branch above.
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Unexpected additional peer in browser session");
#else
                        // instruct all connected peers to connect

                        NetworkPacketOStream packetOStream(NETWORK_PACKET_FLAG_RELIABLE);
                        packetOStream.writeUint32(NETWORKPACKET_CONNECT);
                        packetOStream.writeUint32(SDL_SwapBE32(pCurrentPeer->address.host));
                        packetOStream.writeUint16(pCurrentPeer->address.port);
                        packetOStream.writeString(peerData->name);

                        sendPacketToAllConnectedPeers(packetOStream);
#endif
                    }
                }
            }

            if(peerData->timeout > 0 && SDL_GetTicks() > peerData->timeout) {
                // timeout
                switch(peerData->peerState) {
                    case PeerData::PeerState::WaitingForName: {
#ifdef __EMSCRIPTEN__
                        disconnectPeer(pCurrentPeer, NETWORKDISCONNECT_TIMEOUT);
#endif
                        // Native teardown remains owned by ENet.
                    } break;

                    case PeerData::PeerState::WaitingForOtherPeersToConnect: {
#ifdef __EMSCRIPTEN__
                        // the client awaiting connection timed out
                        disconnectPeer(pCurrentPeer, NETWORKDISCONNECT_TIMEOUT);
#else
                        // the client awaiting connection has timed out => send everyone a disconnect message
                        NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                        packetStream.writeUint32(NETWORKPACKET_DISCONNECT);
                        packetStream.writeUint32(SDL_SwapBE32(pCurrentPeer->address.host));
                        packetStream.writeUint16(pCurrentPeer->address.port);

                        sendPacketToAllConnectedPeers(packetStream);

                        enet_peer_disconnect(pCurrentPeer, NETWORKDISCONNECT_TIMEOUT);

                        awaitingConnectionList.pop_front();
#endif
                    } break;

                    case PeerData::PeerState::Connected:
                    default: {
                        // should never happen
                    } break;
                }
            }
        }
    }

#ifdef __EMSCRIPTEN__

    // Browser: refresh RTT estimates from the transport
    if(pWebRtcTransport->getState() == WebRtcTransport::State::Connected) {
        for(NetPeer* pCurrentPeer : peerList) {
            pCurrentPeer->roundTripTime = pWebRtcTransport->getRoundTripTimeMs(pCurrentPeer->webRtcPeerId);
        }
        if(connectPeer != nullptr) {
            connectPeer->roundTripTime = pWebRtcTransport->getRoundTripTimeMs(connectPeer->webRtcPeerId);
        }
    }

    WebRtcTransport::Event event;
    while(pWebRtcTransport->pollEvent(event)) {
        switch(event.type) {
            case WebRtcTransport::EventType::Connect: {
                // A duplicate SDK notification must not overwrite owned PeerData.
                if(findPeerByWebRtcId(event.peerHandle, false)) break;
                NetPeer* peer = findPeerByWebRtcId(event.peerHandle, true);

                // The lobby assigned the role when pairing. bIsServer is only
                // set once the host reaches the game lobby (startServer),
                // which can race with the data channels opening, so decide by
                // the matched role here.
                if(bIsServer || bWebRtcHost) {
                    // Host: the joining player's data channels are now open
                    debugNetwork("NetworkManager: peer %u connected.\n", (unsigned int) event.peerHandle);

                    PeerData* newPeerData = new PeerData(peer, PeerData::PeerState::WaitingForName);
                    newPeerData->timeout = SDL_GetTicks() + AWAITING_CONNECTION_TIMEOUT;
                    peer->data = newPeerData;

                    awaitingConnectionList.push_back(peer);

                    // Send name
                    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                    packetStream.writeUint32(NETWORKPACKET_SENDNAME);
                    packetStream.writeString(playerName);

                    sendPacketToPeer(peer, packetStream);
                } else {
                    // Client: connected to the host
                    connectPeer = peer;
                    connectPeerWebRtcId = event.peerHandle;

                    PeerData* peerData = new PeerData(peer, PeerData::PeerState::WaitingForConnect);
                    peer->data = peerData;

                    // Track the host peer for admission like the ENet client path,
                    // so GAMEINFO acceptance promotes it into peerList (Established).
                    awaitingConnectionList.push_back(peer);

                    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                    packetStream.writeUint32(NETWORKPACKET_SENDNAME);
                    packetStream.writeString(playerName);

                    sendPacketToHost(packetStream);

                    peerData->peerState = PeerData::PeerState::WaitingForOtherPeersToConnect;
                    peerData->timeout = 0;
                }
            } break;

            case WebRtcTransport::EventType::Message: {
                if(event.channel != 0 && event.channel != 1) {
                    break;
                }

                NetPeer* peer = findPeerByWebRtcId(event.peerHandle, false);
                if(peer == nullptr) {
                    break;
                }

                if(acceptIncomingBytes(peer, event.data.size())) {
                    NetworkPacketIStream packetStream(event.data.data(), event.data.size());
                    handlePacket(peer, packetStream);
                }
            } break;

            case WebRtcTransport::EventType::Disconnect: {
                NetPeer* peer = findPeerByWebRtcId(event.peerHandle, false);
                if(peer == nullptr) {
                    break;
                }

                releaseWebRtcPeer(peer, event.cause, true);
            } break;

            case WebRtcTransport::EventType::Matched: {
                // The lobby paired us; cause is the assigned role.
                bWebRtcHost = (event.cause == static_cast<int>(WebRtcTransport::MatchRole::Host));
                debugNetwork("NetworkManager: matched as %s.\n", bWebRtcHost ? "host" : "joiner");

                if(pOnMatched) {
                    pOnMatched(bWebRtcHost);
                }
            } break;

            case WebRtcTransport::EventType::State:
            default: {
                // Transport-level state is polled by the UI (getWebRtcState()).
            } break;
        }
    }

    drainWebRtcDisconnects();

#else // native desktop

    ENetEvent event;
    while(enet_host_service(host, &event, 0) > 0) {

        ENetPeer* peer = event.peer;

        switch(event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                if(bIsServer) {
                    // Server
                    debugNetwork("NetworkManager: %s:%u connected.\n", Address2String(peer->address).c_str(), peer->address.port);

                    // Admission, before any state is allocated for this connection.
                    if(bGameInProgress) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "NetworkManager: refusing connection from %s:%u - game already in progress",
                                    Address2String(peer->address).c_str(), peer->address.port);
                        enet_peer_disconnect(peer, NETWORKDISCONNECT_GAME_FULL);
                        break;
                    }

                    const std::size_t knownPeers = peerList.size() + awaitingConnectionList.size();
                    if(knownPeers >= MAX_MESH_PEERS
                       || (maxPlayers > 0 && knownPeers >= static_cast<std::size_t>(maxPlayers))) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "NetworkManager: refusing connection from %s:%u - lobby is full (%zu peers)",
                                    Address2String(peer->address).c_str(), peer->address.port, knownPeers);
                        enet_peer_disconnect(peer, NETWORKDISCONNECT_GAME_FULL);
                        break;
                    }

                    PeerData* newPeerData = createPeerData(peer, PeerData::PeerState::WaitingForName);
                    newPeerData->timeout = SDL_GetTicks() + AWAITING_CONNECTION_TIMEOUT;
                    peer->data = newPeerData;

                    debugNetwork("Adding '%s' to awaiting connection list\n", newPeerData->name.c_str());
                    awaitingConnectionList.push_back(peer);

                    // Send name
                    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                    packetStream.writeUint32(NETWORKPACKET_SENDNAME);
                    packetStream.writeString(playerName);

                    sendPacketToPeer(peer, packetStream);
                } else if(connectPeer != nullptr) {
                    // Client
                    PeerData* peerData = static_cast<PeerData*>(peer->data);

                    if(bGameInProgress && peer != connectPeer && peerData == nullptr) {
                        // No new mesh members once the match is running.
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "NetworkManager: refusing mesh connection from %s:%u during a match",
                                    Address2String(peer->address).c_str(), peer->address.port);
                        enet_peer_disconnect(peer, NETWORKDISCONNECT_GAME_FULL);
                        break;
                    }

                    if(peer == connectPeer) {
                        NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                        packetStream.writeUint32(NETWORKPACKET_SENDNAME);
                        packetStream.writeString(playerName);

                        sendPacketToHost(packetStream);

                        peerData->peerState = PeerData::PeerState::WaitingForOtherPeersToConnect;
                        peerData->timeout = 0;
                    } else {
                        debugNetwork("NetworkManager: %s:%u connected.\n", Address2String(peer->address).c_str(), peer->address.port);

                        PeerData* pConnectPeerData = static_cast<PeerData*>(connectPeer->data);

                        if(pConnectPeerData->peerState == PeerData::PeerState::WaitingForOtherPeersToConnect) {
                            if(peerData == nullptr) {
                                if(peerList.size() + awaitingConnectionList.size() >= MAX_MESH_PEERS) {
                                    enet_peer_disconnect(peer, NETWORKDISCONNECT_GAME_FULL);
                                    break;
                                }

                                peerData = createPeerData(peer, PeerData::PeerState::Connected);
                                peer->data = peerData;

                                debugNetwork("Adding '%s' to awaiting connection list\n", peerData->name.c_str());
                                awaitingConnectionList.push_back(peer);
                            }
                        } else if(peerData == nullptr) {
                            // We are fully connected and did not open this connection: nobody
                            // instructed us to expect it, so it is not part of the mesh.
                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                        "NetworkManager: refusing unsolicited connection from %s:%u",
                                        Address2String(peer->address).c_str(), peer->address.port);
                            enet_peer_disconnect(peer, NETWORKDISCONNECT_TIMEOUT);
                        } else {
                            NetworkPacketOStream packetStream1(NETWORK_PACKET_FLAG_RELIABLE);
                            packetStream1.writeUint32(NETWORKPACKET_PEER_CONNECTED);
                            packetStream1.writeUint32(SDL_SwapBE32(peer->address.host));
                            packetStream1.writeUint16(peer->address.port);

                            sendPacketToHost(packetStream1);

                            NetworkPacketOStream packetStream2(NETWORK_PACKET_FLAG_RELIABLE);
                            packetStream2.writeUint32(NETWORKPACKET_SENDNAME);
                            packetStream2.writeString(playerName);

                            sendPacketToPeer(peer, packetStream2);
                        }
                    }
                } else {
                    enet_peer_disconnect(peer, NETWORKDISCONNECT_TIMEOUT);
                }
            } break;

            case ENET_EVENT_TYPE_RECEIVE: {
                //debugNetwork("NetworkManager: A packet of length %u was received from %s:%u on channel %u on this server.\n",
                //                (unsigned int) event.packet->dataLength, Address2String(peer->address).c_str(), peer->address.port, event.channelID);

                const std::size_t receivedBytes = (event.packet != nullptr) ? event.packet->dataLength : 0;

                // The stream takes ownership of the packet, so build it first: the packet is
                // released even when the byte budget refuses to parse it.
                NetworkPacketIStream packetStream(event.packet);

                if(acceptIncomingBytes(peer, receivedBytes)) {
                    handlePacket(peer, packetStream);
                }
            } break;

            case ENET_EVENT_TYPE_DISCONNECT: {
                PeerData* peerData = static_cast<PeerData*>(peer->data);

                int disconnectCause = event.data;

                debugNetwork("NetworkManager: %s:%u (%s) disconnected (%d).\n", Address2String(peer->address).c_str(), peer->address.port, (peerData != nullptr) ? peerData->name.c_str() : "unknown", disconnectCause);

                if(peerData != nullptr) {
                    if(std::find(awaitingConnectionList.begin(), awaitingConnectionList.end(), peer) != awaitingConnectionList.end()) {
                        // Only the host announces that a peer is gone. Every client is
                        // connected to every other client, so each of them sees its own ENet
                        // disconnect event; a client-sent DISCONNECT is both redundant and
                        // refused by the receiving peers' host-only rule for this packet.
                        if(bIsServer && peerData->peerState == PeerData::PeerState::WaitingForOtherPeersToConnect) {
                            NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                            packetStream.writeUint32(NETWORKPACKET_DISCONNECT);
                            packetStream.writeUint32(SDL_SwapBE32(peer->address.host));
                            packetStream.writeUint16(peer->address.port);

                            sendPacketToAllConnectedPeers(packetStream);
                        }

                        debugNetwork("Removing '%s' from awaiting connection list\n", peerData->name.c_str());
                        awaitingConnectionList.remove(peer);
                    }


                    if(std::find(peerList.begin(), peerList.end(), peer) != peerList.end()) {
                        debugNetwork("Removing '%s' from peer list\n", peerData->name.c_str());
                        peerList.remove(peer);

                        if(bIsServer) {
                            NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                            packetStream.writeUint32(NETWORKPACKET_DISCONNECT);
                            packetStream.writeUint32(SDL_SwapBE32(peer->address.host));
                            packetStream.writeUint16(peer->address.port);

                            sendPacketToAllConnectedPeers(packetStream);
                        }

                        if(pOnPeerDisconnected) {
                            pOnPeerDisconnected(peerData->name, (peer == connectPeer), disconnectCause);
                        }
                    } else {
                        if(peer == connectPeer) {
                            // host disconnected while establishing connection
                            if(pOnPeerDisconnected) {
                                pOnPeerDisconnected(peerData->name, true, disconnectCause);
                            }
                        }
                    }
                }

                // delete peer data
                delete peerData;
                peer->data = nullptr;

                if(peer == connectPeer) {
                    connectPeer = nullptr;
                }

            } break;

            default: {

            } break;
        }
    }
#endif // __EMSCRIPTEN__
}

NetworkSessionCallbacks NetworkManager::sessionCallbacks() const {
    NetworkSessionCallbacks callbacks;
    callbacks.onReceiveChatMessage     = &pOnReceiveChatMessage;
    callbacks.onReceiveGameInfo        = &pOnReceiveGameInfo;
    callbacks.onReceiveChangeEventList = &pOnReceiveChangeEventList;
    callbacks.onStartGame              = &pOnStartGameBridge;
    callbacks.onJoinSync               = &pOnJoinSync;
    callbacks.onReceiveCommandList     = &pOnReceiveCommandList;
    callbacks.onReceiveSelectionList   = &pOnReceiveSelectionList;
    callbacks.onReceiveClientStats     = &pOnReceiveClientStats;
    callbacks.onReceiveSetPathBudget   = &pOnReceiveSetPathBudget;
    callbacks.onReceiveMatchControl    = &pOnReceiveMatchControl;
    callbacks.onReceiveMatchResumeRequest = &pOnReceiveMatchResumeRequest;
    callbacks.onReceiveCoopMission     = &pOnReceiveCoopMissionBridge;
    callbacks.onConfigMismatch         = &pOnConfigMismatch;
    return callbacks;
}

NetworkManager::ContentCheck NetworkManager::checkRelayContent(
        const std::string& quantBotHash, const std::string& objectDataHash,
        const std::string& gameVersion, std::string& reason) const {
    reason.clear();

    ContentCompatibility::Fingerprint local;
    local.gameVersion = gameVersion;
    local.quantBotHash = quantBotHash;
    local.objectDataHash = objectDataHash;
    try { local.modRevisionHash = OnlineModPolicy::fingerprint(); }
    catch(const std::exception&) { }
    ContentCheck worst = ContentCheck::Match;
    const auto check = [&](const auto& peer) {
        ContentCompatibility::Fingerprint reported;
        reported.gameVersion = peer.gameVersion;
        reported.quantBotHash = peer.quantBotConfigHash;
        reported.objectDataHash = peer.objectDataHash;
        reported.modRevisionHash = peer.modRevisionHash;
        std::string why;
        switch(ContentCompatibility::compare(local, reported, peer.name, why)) {
            case ContentCompatibility::Verdict::Mismatch: worst = ContentCheck::Mismatch; reason = why; break;
            case ContentCompatibility::Verdict::AwaitingPeer:
                if(worst == ContentCheck::Match) { worst = ContentCheck::AwaitingPeer; reason = why; } break;
            case ContentCompatibility::Verdict::Match: break;
        }
    };
    if(pRelayClient) {
        for(const auto& peer : pRelayClient->peers()) check(peer);
    } else {
        for(const auto* peer : peerList) if(peer->data) check(*static_cast<const PeerData*>(peer->data));
    }

    return worst;
}

bool NetworkManager::startDirectSession(const DirectRoomTransport::Config& config,
                                       std::string& error) {
    if(transport != Transport::DirectP2P) {
        error = "This game session is not a direct one.";
        return false;
    }

    bGameInProgress = false;
    simulationSeed  = 0;
    pendingCoopMission.reset();
    playerName = config.displayName;

    auto direct = std::make_unique<DirectRoomTransport>();
    if(!direct->start(config, error)) {
        return false;
    }
    pRelayClient = std::move(direct);
    return true;
}

bool NetworkManager::isMeshReady() const {
    const DirectRoomTransport* direct = getDirectTransport();
    // On every other transport the question does not arise: the ENet mesh connects every pair
    // itself, and the relay forwards between all of them.
    return direct == nullptr ? true : direct->meshReady();
}

std::string NetworkManager::getMeshBlockedReason() const {
    const DirectRoomTransport* direct = getDirectTransport();
    return direct == nullptr ? std::string() : direct->meshBlockedReason();
}

bool NetworkManager::startRelaySession(const RoomRelayClient::Config& config, std::string& error) {
    if(transport != Transport::RoomRelay) {
        error = "This game session is not using the game service.";
        return false;
    }

    bGameInProgress = false;
    simulationSeed = 0;
    pendingCoopMission.reset();
    playerName = config.displayName;

    auto relay = std::make_unique<RoomRelayClient>();
    if(!relay->start(config, error)) {
        return false;
    }
    pRelayClient = std::move(relay);
    return true;
}

std::uint32_t NetworkManager::relayHostPeerId() const {
    if(!pRelayClient) {
        return 0;
    }
    for(const RoomSessionTransport::Peer& peer : pRelayClient->peers()) {
        if(peer.isHost()) {
            knownRelayHostPeerId = peer.id;
            return peer.id;
        }
    }
    // A host that has just left is still found (findPeer) until its departure is handled, so
    // what it sent before leaving is still accepted as the host's.
    if(const auto* departed = pRelayClient->findPeer(knownRelayHostPeerId); departed && departed->isHost()) {
        return knownRelayHostPeerId;
    }
    return 0;
}

bool NetworkManager::sendPacketOverRelay(NetworkPacketOStream& packetStream, int channel,
                                         std::uint32_t recipient) {
#ifdef __EMSCRIPTEN__
    // Browser: the buffer already holds the serialized bytes; hand them over directly.
    return pRelayClient && pRelayClient->sendGamePayload(
        packetStream.getData(), packetStream.getDataLength(), channel, recipient);
#else
    ENetPacket* packet = packetStream.getPacket();
    if(packet == nullptr) return false;
    const bool accepted = pRelayClient && pRelayClient->sendGamePayload(
        packet->data, packet->dataLength, channel, recipient);
    enet_packet_destroy(packet);
    return accepted;
#endif
}

bool NetworkManager::sendRelayDiagnostic(RoomRelay::DiagnosticKind kind,
                                         const std::uint8_t* payload, std::size_t length) {
    if(!pRelayClient) {
        return false;
    }
    return pRelayClient->sendDiagnostic(kind, payload, length);
}

void NetworkManager::updateRelaySession() {
    if(!pRelayClient) {
        return;
    }

    pRelayClient->update();

    RoomSessionTransport::Event event;
    bool sessionEnded = false;
    while(!sessionEnded && pRelayClient->pollEvent(event)) {
        switch(event.type) {
            case RoomSessionTransport::Event::Type::PeerJoined: {
                if(bGameInProgress && event.spectator) {
                    joinNotice=event.name+" joined as a spectator";
                    joinNoticeTime=SDL_GetTicks();
                }
                debugNetwork("Relay peer '%s' joined (%s, %s)\n", event.name.c_str(),
                             event.role == RoomRelay::Role::Host ? "host" : "client",
                             event.runtime.c_str());
                if(bIsServer && pGameInitSettings != nullptr && !bGameInProgress && !lateJoinPaused()) {
                    // A late join receives the authoritative checkpoint. Sending the original
                    // lobby here would reopen its player-assignment callback during a match.
                    // The lobby state is what a joining player needs first, exactly as on the
                    // mesh transport - only addressed to a relay peer id instead of an address.
                    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
                    packetStream.writeUint32(NETWORKPACKET_SENDGAMEINFO);
                    pGameInitSettings->save(packetStream);
                    ChangeEventList changeEventList = pGetChangeEventListForNewPlayerCallback
                        ? pGetChangeEventListForNewPlayerCallback(event.name) : ChangeEventList();
                    changeEventList.save(packetStream);
                    sendPacketOverRelay(packetStream, 0, event.peerId);
                }
            } break;

            case RoomSessionTransport::Event::Type::PeerLeft: {
                if(event.spectator || isSpectator(event.name) || observerTransfers.count(event.peerId)) {
                    observerTransfers.erase(event.peerId);
                    spectators.erase(event.name); break;
                }
                if(event.name==joinName && event.role!=RoomRelay::Role::Host) {
                    if(lateJoinPaused()) abortLateJoin("The new player disconnected.");
                    break;
                }
                debugNetwork("Relay peer '%s' left (reason %u)\n", event.name.c_str(),
                             static_cast<unsigned>(event.reason));
                if(pOnPeerDisconnected) {
                    const bool wasHost = (event.role == RoomRelay::Role::Host);
                    const int cause = (event.reason == 2) ? NETWORKDISCONNECT_TIMEOUT
                                                          : NETWORKDISCONNECT_QUIT;
                    pOnPeerDisconnected(event.name, wasHost, cause);
                }
            } break;

            case RoomSessionTransport::Event::Type::MatchStart:
                if(pOnStartGameBridge) pOnStartGameBridge(event.code);
                break;
            case RoomSessionTransport::Event::Type::GamePayload: {
                handleRelayGamePayload(event.peerId, event.payload.data(), event.payload.size());
            } break;

            case RoomSessionTransport::Event::Type::Diagnostic: {
                const RoomSessionTransport::Peer* peer = pRelayClient->findPeer(event.peerId);
                if(peer == nullptr && !event.message.empty()) {
                    // A transport-level note rather than a peer's diagnostic: the direct
                    // transport reports losing the signaling service this way, which is worth
                    // saying out loud precisely because the match keeps running.
                    SDL_Log("NetworkManager: %s", event.message.c_str());
                    break;
                }
                if(peer != nullptr && pOnReceiveRelayDiagnostic) {
                    // By value: the callback runs game code, and the peer list is not the
                    // callback's to keep alive.
                    const std::string senderName = peer->name;
                    pOnReceiveRelayDiagnostic(senderName, event.diagnosticKind,
                                              event.payload.data(), event.payload.size());
                }
            } break;

            case RoomSessionTransport::Event::Type::PhaseChanged: {
                debugNetwork("Relay room phase is now %s\n",
                             event.phase == RoomRelay::Phase::Match ? "match" : "lobby");
            } break;

            case RoomSessionTransport::Event::Type::Refused: {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "NetworkManager: the game service refused a message: %s",
                            event.message.c_str());
            } break;

            case RoomSessionTransport::Event::Type::Closed: {
                SDL_Log("NetworkManager: relay session ended: %s", event.message.c_str());
                // Nothing after this belongs to a live session. The terminal event is always
                // last, but stopping here is what makes that a rule rather than an ordering
                // accident - and the disconnect callback below can reenter the menu loop.
                sessionEnded = true;
                if(pOnPeerDisconnected) {
                    // Look like a lost host connection so every existing menu and the running
                    // game react the way they already do when a session ends.
                    int cause = NETWORKDISCONNECT_QUIT;
                    switch(event.code) {
                        case RoomRelay::Close::Timeout:         cause = NETWORKDISCONNECT_TIMEOUT; break;
                        case RoomRelay::Close::RoomFull:        cause = NETWORKDISCONNECT_GAME_FULL; break;
                        case RoomRelay::Close::VersionMismatch: cause = NETWORKDISCONNECT_PROTOCOL_MISMATCH; break;
                        default: break;
                    }
                    pOnPeerDisconnected(std::string(), true, cause);
                }
            } break;
        }
    }

    // The relay knows the role authoritatively; mirror it so isServer() is right everywhere.
    if(pRelayClient->isJoined()) {
        bIsServer = pRelayClient->isHost();
    }
}

void NetworkManager::handleRelayGamePayload(std::uint32_t peerId,
                                            const std::uint8_t* payload, std::size_t length) {
    if(payload == nullptr || length < 4 || !pRelayClient) {
        return;
    }

    const RoomSessionTransport::Peer* sender = pRelayClient->findPeer(peerId);
    if(sender == nullptr) {
        return;     // announced to us once, gone by the time the game loop got here
    }

    // Copies, not references into the peer list. Handling a payload runs the game's own
    // callbacks, and anything that reaches the session again can add or remove a peer - which
    // moves the vector any reference would be pointing into. The peer is looked up again by id
    // afterwards to store what was learned.
    std::string peerName               = sender->name;
    std::string peerGameVersion        = sender->gameVersion;
    std::string peerQuantBotConfigHash = sender->quantBotConfigHash;
    std::string peerObjectDataHash     = sender->objectDataHash;
    std::string peerModRevisionHash = sender->modRevisionHash;
    const bool  peerIsHost             = sender->isHost();
    const std::size_t peerCount        = pRelayClient->peers().size();
    sender = nullptr;

    // The payload is the same serialized packet the mesh transport carries, so it is read with
    // the same hardened reader rather than a second, parallel parser.
#ifdef __EMSCRIPTEN__
    // Browser: no ENet packet wrapper is needed; the view reads the payload in place.
    NetworkPacketIStream packetStream(payload, length);
#else
    ENetPacket* packet = enet_packet_create(payload, length, 0);
    if(packet == nullptr) {
        return;
    }
    ENetPacketIStream packetStream(packet);
#endif

    try {
        const Uint32 packetType = packetStream.readUint32();
        if(isSpectator(peerName) && packetType!=NETWORKPACKET_JOIN_ACK && packetType!=NETWORKPACKET_CHATMESSAGE) return;
        if(lateJoinPaused() && (packetType==NETWORKPACKET_COMMANDLIST || packetType==NETWORKPACKET_SELECTIONLIST
            || packetType==NETWORKPACKET_CLIENTSTATS || packetType==NETWORKPACKET_SETPATHBUDGET)) return;

        // The central admission policy applies unchanged. On the relay a peer is always fully
        // established (the relay would not route for anybody else) and "the host connection"
        // means "the peer the relay designated as host".
        NetworkPacketPolicy::PacketContext context;
        context.packetType       = packetType;
        context.localRole        = bIsServer ? NetworkPacketPolicy::LocalRole::Host
                                             : NetworkPacketPolicy::LocalRole::Client;
        context.phase            = bGameInProgress ? NetworkPacketPolicy::SessionPhase::InGame
                                                   : NetworkPacketPolicy::SessionPhase::Lobby;
        context.admission        = NetworkPacketPolicy::PeerAdmission::Established;
        context.isHostConnection = peerIsHost;

        const NetworkPacketPolicy::PacketVerdict verdict =
            NetworkPacketPolicy::classifyPacket(context);
        if(verdict != NetworkPacketPolicy::PacketVerdict::Accept) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: refused relay packet %u from '%s': %s",
                        static_cast<unsigned>(packetType), peerName.c_str(),
                        NetworkPacketPolicy::describeVerdict(verdict));
            return;
        }

        // Address-bearing and mod-transfer packets have no relay code path at all. The relay
        // refuses to carry them; this is the second, independent refusal.
        if(!GamePayloadRouter::handles(packetType)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: packet %u is not carried over the game service",
                        static_cast<unsigned>(packetType));
            return;
        }

        PayloadPeerAdapter::Fields fields;
        fields.clientId           = peerId;
        fields.name               = &peerName;
        fields.isHost             = peerIsHost;
        fields.gameVersion        = &peerGameVersion;
        fields.quantBotConfigHash = &peerQuantBotConfigHash;
        fields.objectDataHash     = &peerObjectDataHash;
        fields.modRevisionHash = &peerModRevisionHash;

        fields.nameAssigned = &relayPeerNamesAreBound;

        PayloadPeerAdapter adapter(
            fields,
            [this, peerId, &peerName](const char* reason) {
                // Found again by id rather than captured by reference: the accounting lives on
                // the peer, and the peer may have moved or gone while this payload was handled.
                RoomSessionTransport::Peer* offender =
                    pRelayClient ? pRelayClient->findPeer(peerId) : nullptr;
                if(offender == nullptr) {
                    return;
                }

                const Uint32 now = SDL_GetTicks();
                if(offender->lastRefuseTime != 0
                   && (now - offender->lastRefuseTime) > REJECT_DECAY_MS) {
                    offender->refusedMessages = 0;
                }
                offender->lastRefuseTime = now;
                offender->refusedMessages++;
                if(offender->refusedMessages <= 3
                   || (now - offender->lastRefuseLog) >= REJECT_LOG_INTERVAL_MS) {
                    offender->lastRefuseLog = now;
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "NetworkManager: refused relay payload from '%s': %s (%u so far)",
                                peerName.c_str(), reason,
                                static_cast<unsigned>(offender->refusedMessages));
                }
                if(offender->refusedMessages >= MAX_REJECTED_PACKETS_PER_PEER && pRelayClient) {
                    if(auto* direct=getDirectTransport(); direct && direct->isSpectatorPeer(peerId))
                        direct->disconnectSpectator(peerId,"The spectator sent invalid messages.");
                    else pRelayClient->stop(3 /* ended because of an error */);
                }
            },
            [](const std::string&) { return false; },   // names are not rebindable on the relay
            [this]() {
                sendConfigHash(getQuantBotConfig().getConfigHash(), getObjectDataHash(),
                               VERSIONSTRING);
            },
            [this](int cause) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "NetworkManager: ending the relay session (cause %d)", cause);
                if(pOnPeerDisconnected) {
                    pOnPeerDisconnected(std::string(), true, cause);
                }
                if(pRelayClient) {
                    pRelayClient->stop(3);
                }
            });

        GamePayloadContext payloadContext;
        payloadContext.isHost         = bIsServer;
        payloadContext.inGame         = bGameInProgress;
        payloadContext.simulationSeed = simulationSeed;
        // Verified Workshop revisions are retained for both online and LAN games.
        payloadContext.allowMapWrite  = true;
        // On the relay a client sends its hashes once when it enters the lobby; answering the
        // host's would race the host's own move to the match phase and be refused.
        payloadContext.replyToConfigHash = false;
        // Nothing on this transport can transfer content, so a difference is final and the
        // lobby has to be told rather than only the log.
        payloadContext.contentMustMatch = true;
        payloadContext.coopPartnerIsSolePeer = (peerCount == 1) && peerIsHost;

        auto callbacks=sessionCallbacks();
        std::function<void(const std::string&,const std::string&)> chat=[this](const auto& name,const auto& message) {
            if(pOnReceiveChatMessage) pOnReceiveChatMessage(name,message);
            forwardObserverChat(name,message);
        };
        callbacks.onReceiveChatMessage=&chat;
        GamePayloadRouter::handle(packetType, packetStream, adapter, payloadContext, callbacks);
    } catch(InputStream::eof&) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NetworkManager: truncated relay payload from '%s'", peerName.c_str());
    } catch(std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NetworkManager: unusable relay payload from '%s': %s",
                    peerName.c_str(), e.what());
    }

    // Store what the payload taught us about the peer, if it is still in the room.
    if(pRelayClient) {
        if(RoomSessionTransport::Peer* current = pRelayClient->findPeer(peerId)) {
            current->gameVersion        = peerGameVersion;
            current->quantBotConfigHash = peerQuantBotConfigHash;
            current->objectDataHash     = peerObjectDataHash;
            current->modRevisionHash = peerModRevisionHash;
        }
    }
}

NetworkManager::PeerData* NetworkManager::createPeerData(NetPeer* peer, PeerData::PeerState peerState) {
    PeerData* peerData = new PeerData(peer, peerState);
    peerData->clientId = nextClientId++;
    if(nextClientId == 0) {
        nextClientId = 1;   // never hand out 0, it doubles as "no client"
    }
    return peerData;
}

void NetworkManager::beginPeerDisconnect(NetPeer* peer, const char* reason) {
    if(peer == nullptr) {
        return;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);

    if(peerData == nullptr) {
        // Nothing to mark; throttle the log so a connection without peer state cannot spin it.
        const Uint32 now = SDL_GetTicks();
        if(lastUnidentifiedLogTime == 0 || (now - lastUnidentifiedLogTime) >= REJECT_LOG_INTERVAL_MS) {
            lastUnidentifiedLogTime = now;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: dropping traffic from unidentified peer %s (%s)",
                        peerAddressLabel(peer).c_str(), reason);
        }
        disconnectPeer(peer, NETWORKDISCONNECT_TIMEOUT);
        return;
    }

    if(!peerData->refusals.beginDisconnect()) {
        // The drop was already requested and logged once; say nothing further.
        return;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "NetworkManager: disconnecting '%s' (%s): %s",
                 peerData->name.c_str(), peerAddressLabel(peer).c_str(), reason);
    disconnectPeer(peer, NETWORKDISCONNECT_TIMEOUT);
}

void NetworkManager::noteRejectedPacket(NetPeer* peer, const char* reason) {
    if(peer == nullptr) {
        return;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);

    if(peerData == nullptr) {
        beginPeerDisconnect(peer, reason);
        return;
    }

    if(peerData->refusals.isDisconnecting()) {
        return;
    }

    const Uint32 now = SDL_GetTicks();

    // Refusals that are far apart are not an attack: a few packets can legitimately race a
    // phase transition or a peer leaving.
    const bool bTooMany = peerData->refusals.noteRefusal(now, MAX_REJECTED_PACKETS_PER_PEER,
                                                         REJECT_DECAY_MS);

    if(peerData->refusals.refusals <= 3
       || (now - peerData->lastRejectLogTime) >= REJECT_LOG_INTERVAL_MS) {
        peerData->lastRejectLogTime = now;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NetworkManager: rejected packet from '%s' (%s): %s (%u refused so far)",
                    peerData->name.c_str(), peerAddressLabel(peer).c_str(),
                    reason, peerData->refusals.refusals);
    }

    if(bTooMany) {
        // One shot: this marks the peer, so nothing from it is parsed, counted or logged again.
        beginPeerDisconnect(peer, "too many refused packets");
    }
}

bool NetworkManager::acceptIncomingBytes(NetPeer* peer, std::size_t byteCount) {
    if(peer == nullptr) {
        return false;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);
    if(peerData == nullptr) {
        // No admitted state: the packet is refused by admitPacket() anyway, and that path
        // drops the connection.
        return true;
    }

    if(peerData->refusals.isDisconnecting()) {
        return false;
    }

    // Only a mod transfer this client asked for may use the large budget, and only on the
    // connection to the host. Everything else lives far below the ordinary budget.
    const bool expectingModTransfer = (!bIsServer) && (connectPeer != nullptr) && (peer == connectPeer)
        && (modTransferState.requested || modTransferState.inProgress);
    const Uint64 budget = expectingModTransfer ? MAX_MOD_TRANSFER_BYTES_PER_SECOND
                                               : MAX_PEER_BYTES_PER_SECOND;

    if(!peerData->byteWindow.accept(SDL_GetTicks(), static_cast<Uint64>(byteCount),
                                    budget, BYTE_WINDOW_MS)) {
        beginPeerDisconnect(peer, "incoming byte budget exceeded");
        return false;
    }

    return true;
}

bool NetworkManager::admitPacket(NetPeer* peer, Uint32 packetType) {
    if(peer == nullptr) {
        return false;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);

    // Already dropped: stop parsing anything else this peer has queued.
    if(peerData != nullptr && peerData->refusals.isDisconnecting()) {
        return false;
    }

    // Cheap flood guard: even well-formed packets are refused above a rate no legitimate
    // peer reaches (a full mod transfer is ~160 packets, in-game traffic a few dozen/s).
    if(peerData != nullptr) {
        if(!peerData->packetWindow.accept(SDL_GetTicks(), 1, MAX_PACKETS_PER_PEER_PER_SECOND,
                                          BYTE_WINDOW_MS)) {
            beginPeerDisconnect(peer, "packet rate limit exceeded");
            return false;
        }
    }

    NetworkPacketPolicy::PacketContext context;
    context.packetType = packetType;
    context.localRole = bIsServer ? NetworkPacketPolicy::LocalRole::Host
                                  : NetworkPacketPolicy::LocalRole::Client;
    context.phase = bGameInProgress ? NetworkPacketPolicy::SessionPhase::InGame
                                    : NetworkPacketPolicy::SessionPhase::Lobby;
    context.isHostConnection = (!bIsServer) && (connectPeer != nullptr) && (peer == connectPeer);

    if(peerData == nullptr) {
        context.admission = NetworkPacketPolicy::PeerAdmission::Unidentified;
    } else if(std::find(peerList.begin(), peerList.end(), peer) != peerList.end()) {
        context.admission = NetworkPacketPolicy::PeerAdmission::Established;
    } else {
        context.admission = NetworkPacketPolicy::PeerAdmission::Handshaking;
    }

    const NetworkPacketPolicy::PacketVerdict verdict = NetworkPacketPolicy::classifyPacket(context);
    if(verdict == NetworkPacketPolicy::PacketVerdict::Accept) {
        return true;
    }

    if(NetworkPacketPolicy::isExpectedOrderingRefusal(verdict)) {
        // Peers change phase at slightly different times - clients start their countdown half
        // a round trip before the host, and campaign co-op moves between missions - so packets
        // that are valid but stale are dropped quietly rather than held against the sender.
        debugNetwork("NetworkManager: dropping out-of-phase packet %u from %s\n",
                     packetType, peerAddressLabel(peer).c_str());
        return false;
    }

    noteRejectedPacket(peer, NetworkPacketPolicy::describeVerdict(verdict));
    return false;
}

void NetworkManager::abortModTransfer(const char* reason) {
    const bool wasInProgress = modTransferState.inProgress;

    modTransferState.inProgress = false;
    modTransferState.modData.clear();
    modTransferState.modName.clear();
    modTransferState.totalSize = 0;
    modTransferState.receivedSize = 0;

    if(wasInProgress && pOnModDownloadComplete) {
        pOnModDownloadComplete(false, reason);
    }
}

void NetworkManager::handlePacket(NetPeer* peer, NetworkPacketIStream& packetStream)
{
    try {
        Uint32 packetType = packetStream.readUint32();

        // Central admission: role, handshake state and session phase are checked before any
        // payload of this packet is interpreted.
        if(!admitPacket(peer, packetType)) {
            return;
        }

        switch(packetType) {
            case NETWORKPACKET_CONNECT: {
                // Only reachable on a client, on the connection to the designated host
                // (enforced by admitPacket); the mesh address it names still has to be sane.
                const Uint32 rawHost = packetStream.readUint32();
                const Uint16 rawPort = packetStream.readUint16();
                const std::string peerName = packetStream.readString();

                // rawHost is the dotted-quad as an integer (the sender wrote
                // SDL_SwapBE32(address.host)), so the first octet is its most significant byte.
                if(!NetworkPacketPolicy::isPlausibleMeshTarget(rawHost, rawPort)
                   || !NetworkPacketPolicy::isAcceptablePlayerName(peerName)) {
                    noteRejectedPacket(peer, "implausible mesh connect target");
                    break;
                }

#ifndef __EMSCRIPTEN__

                if(awaitingConnectionList.size() + peerList.size() >= MAX_MESH_PEERS) {
                    noteRejectedPacket(peer, "mesh peer limit reached");
                    break;
                }

                ENetAddress address;
                address.host = SDL_SwapBE32(rawHost);
                address.port = rawPort;

                debugNetwork("Connecting to %s:%d\n", Address2String(address).c_str(), address.port);

                ENetPeer *newPeer = enet_host_connect(host, &address, 2, 0);
                if(newPeer == nullptr) {
                    debugNetwork("NetworkManager: No available peers for initiating a connection.");
                } else {
                    PeerData* peerData = createPeerData(newPeer, PeerData::PeerState::WaitingForOtherPeersToConnect);
                    peerData->name = peerName;
                    peerData->bNameAssigned = true;

                    newPeer->data = peerData;
                    debugNetwork("Adding '%s' to awaiting connection list\n", peerData->name.c_str());
                    awaitingConnectionList.push_back(newPeer);
                }
#else
                // Mesh connect-by-address has no browser equivalent: the v1
                // browser session is exactly two players connected by the
                // signaling room, so there is never a third peer to dial.
#endif
            } break;

            case NETWORKPACKET_DISCONNECT: {
#ifdef __EMSCRIPTEN__
                // Address-keyed disconnects only matter for 3+ player meshes;
                // in the two-player browser model the transport-level
                // Disconnect event already tears the peer down.
#else
                ENetAddress address;

                address.host = SDL_SwapBE32(packetStream.readUint32());
                address.port = packetStream.readUint16();

                for(ENetPeer* pCurrentPeer : peerList) {
                    if((pCurrentPeer->address.host == address.host) && (pCurrentPeer->address.port == address.port)) {
                        enet_peer_disconnect_later(pCurrentPeer, NETWORKDISCONNECT_QUIT);
                        break;
                    }
                }

                for(ENetPeer* pAwaitingConnectionPeer : awaitingConnectionList) {
                    if((pAwaitingConnectionPeer->address.host == address.host) && (pAwaitingConnectionPeer->address.port == address.port)) {
                        enet_peer_disconnect_later(pAwaitingConnectionPeer, NETWORKDISCONNECT_QUIT);
                        break;
                    }
                }
#endif // __EMSCRIPTEN__

            } break;

            case NETWORKPACKET_PEER_CONNECTED: {
#ifdef __EMSCRIPTEN__
                // Same rationale as NETWORKPACKET_CONNECT/PEER_CONNECTED mesh
                // bookkeeping: two-player sessions never exchange these.
#else
                ENetAddress address;

                address.host = SDL_SwapBE32(packetStream.readUint32());
                address.port = packetStream.readUint16();

                if(isServer()) {

                    if(awaitingConnectionList.empty() == false) {
                        ENetPeer* pCurrentPeer = awaitingConnectionList.front();
                        PeerData* peerData = static_cast<PeerData*>(pCurrentPeer->data);
                        if(!peerData) {
                            break;
                        }

                        if((pCurrentPeer->address.host == address.host) && (pCurrentPeer->address.port == address.port)) {

                            peerData->notYetConnectedPeers.remove(peer);

                            if(peerData->notYetConnectedPeers.empty()) {
                                // send connected to all peers (excluding the new one)
                                NetworkPacketOStream packetOStream(NETWORK_PACKET_FLAG_RELIABLE);
                                packetOStream.writeUint32(NETWORKPACKET_PEER_CONNECTED);
                                packetOStream.writeUint32(SDL_SwapBE32(pCurrentPeer->address.host));
                                packetOStream.writeUint16(pCurrentPeer->address.port);

                                sendPacketToAllConnectedPeers(packetOStream);

                                // get change event list first
                                ChangeEventList changeEventList = pGetChangeEventListForNewPlayerCallback(peerData->name);

                                // move peer to peer list
                                debugNetwork("Moving '%s' from awaiting connection list to peer list\n", peerData->name.c_str());
                                peerList.push_back(pCurrentPeer);
                                peerData->peerState = PeerData::PeerState::Connected;
                                peerData->timeout = 0;
                                awaitingConnectionList.remove(pCurrentPeer);

                                // send peer game settings
                                NetworkPacketOStream packetOStream2(NETWORK_PACKET_FLAG_RELIABLE);
                                packetOStream2.writeUint32(NETWORKPACKET_SENDGAMEINFO);
                                pGameInitSettings->save(packetOStream2);

                                changeEventList.save(packetOStream2);

                                sendPacketToPeer(pCurrentPeer, packetOStream2);
                                
                                // Send mod info to newly connected peer for mod sync
                                if(ModManager::instance().isInitialized()) {
                                    std::string modName = ModManager::instance().getActiveModName();
                                    std::string modChecksum = ModManager::instance().getEffectiveChecksums().combined;
                                    SDL_Log("NetworkManager: Sending mod info to new peer - mod='%s', checksum=%s", 
                                            modName.c_str(), modChecksum.c_str());
                                    sendModInfoToPeer(pCurrentPeer, modName, modChecksum);
                                }
                            }
                        }
                    }
                } else {
                    for(auto iter = awaitingConnectionList.begin(); iter != awaitingConnectionList.end(); ++iter) {
                        ENetPeer* pCurrentPeer = *iter;

                        if((pCurrentPeer->address.host == address.host) && (pCurrentPeer->address.port == address.port)) {
                            PeerData* peerData = static_cast<PeerData*>(pCurrentPeer->data);
                            if(!peerData) {
                                continue;
                            }
                            debugNetwork("Moving '%s' from awaiting connection list to peer list\n", peerData->name.c_str());
                            peerList.push_back(pCurrentPeer);
                            peerData->peerState = PeerData::PeerState::Connected;
                            peerData->timeout = 0;
                            awaitingConnectionList.erase(iter);
                            break;
                        }
                    }
                }
#endif // __EMSCRIPTEN__

            } break;

            case NETWORKPACKET_SENDGAMEINFO: {
                if(!connectPeer || !connectPeer->data) break;
                routeSharedPayload(peer, packetType, packetStream);
            } break;

            case NETWORKPACKET_MOD_INFO: {
                // Client only, from the host connection, lobby only (enforced by admitPacket).
                std::string modName = packetStream.readString();
                std::string modChecksum = packetStream.readString();

                if(!ModTransferValidation::isValidModName(modName)
                   || modChecksum.size() > MAX_MOD_CHECKSUM_LENGTH) {
                    noteRejectedPacket(peer, "invalid mod info");
                    break;
                }

                SDL_Log("NetworkManager: Received mod info from host - mod: '%s', checksum: %s",
                        modName.c_str(), modChecksum.c_str());

                if(pOnReceiveModInfo) {
                    pOnReceiveModInfo(modName, modChecksum);
                }
            } break;

            case NETWORKPACKET_MOD_REQUEST: {
                // Host only, from an established client, lobby only (enforced by admitPacket).
                std::string requestedModName = packetStream.readString();

                if(!ModTransferValidation::isValidModName(requestedModName)) {
                    noteRejectedPacket(peer, "invalid mod name in mod request");
                    break;
                }

                SDL_Log("NetworkManager: Client requested mod download for '%s'", requestedModName.c_str());

                // Package and send the mod files to the requesting peer
                sendModFilesToPeer(peer, requestedModName);
            } break;

            case NETWORKPACKET_MOD_CHUNK: {
                // Client only, from the host connection, lobby only (enforced by admitPacket).
                std::string modName = packetStream.readString();
                Uint32 totalSize = packetStream.readUint32();
                Uint32 chunkOffset = packetStream.readUint32();
                std::string chunkData = packetStream.readString();

                // Content is only accepted for a transfer this client actually asked for.
                if(!modTransferState.requested || modName != modTransferState.requestedModName) {
                    noteRejectedPacket(peer, "mod chunk for a transfer that was not requested");
                    abortModTransfer("Unexpected mod transfer");
                    break;
                }

                // Security: Validate totalSize against maximum allowed
                if(totalSize > MAX_MOD_TRANSFER_SIZE) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: Mod transfer size %u exceeds limit %d - aborting",
                        totalSize, MAX_MOD_TRANSFER_SIZE);
                    abortModTransfer("Mod exceeds size limit");
                    break;
                }

                if(chunkData.size() > static_cast<std::size_t>(MOD_CHUNK_SIZE)) {
                    noteRejectedPacket(peer, "mod chunk exceeds the chunk size limit");
                    abortModTransfer("Invalid chunk size");
                    break;
                }

                // Initialize transfer state if this is the first chunk
                if(!modTransferState.inProgress || modTransferState.modName != modName) {
                    modTransferState.modName = modName;
                    modTransferState.modData.clear();
                    modTransferState.modData.reserve(totalSize);
                    modTransferState.totalSize = totalSize;
                    modTransferState.receivedSize = 0;
                    modTransferState.inProgress = true;
                } else if(totalSize != modTransferState.totalSize) {
                    noteRejectedPacket(peer, "mod transfer size changed mid-transfer");
                    abortModTransfer("Invalid chunk size");
                    break;
                }

                // Security: Validate chunk offset matches expected position (enforce in-order)
                if(chunkOffset != modTransferState.receivedSize) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: Chunk offset mismatch - expected %zu, got %u. Aborting transfer.",
                        modTransferState.receivedSize, chunkOffset);
                    abortModTransfer("Out-of-order mod chunk");
                    break;
                }

                // Security: Check that adding this chunk won't exceed totalSize
                // (subtraction form: receivedSize is never greater than totalSize)
                if(chunkData.size() > totalSize - modTransferState.receivedSize) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: Chunk would exceed total size - aborting");
                    abortModTransfer("Invalid chunk size");
                    break;
                }

                // Append chunk data
                modTransferState.modData.append(chunkData);
                modTransferState.receivedSize += chunkData.size();

                SDL_Log("NetworkManager: Received mod chunk %zu/%zu bytes", 
                        modTransferState.receivedSize, modTransferState.totalSize);

                if(pOnModDownloadProgress) {
                    pOnModDownloadProgress(modTransferState.receivedSize, modTransferState.totalSize);
                }
            } break;

            case NETWORKPACKET_MOD_COMPLETE: {
                // Client only, from the host connection, lobby only (enforced by admitPacket).
                bool success = packetStream.readBool();
                std::string message = packetStream.readString();
                if(message.size() > MAX_MOD_MESSAGE_LENGTH) {
                    message.resize(MAX_MOD_MESSAGE_LENGTH);
                }

                SDL_Log("NetworkManager: Mod transfer complete - success: %s, message: %s",
                        success ? "yes" : "no", message.c_str());

                if(!modTransferState.requested) {
                    noteRejectedPacket(peer, "mod completion for a transfer that was not requested");
                    abortModTransfer("Unexpected mod transfer");
                    break;
                }

                // A "successful" transfer that did not deliver every announced byte must not be
                // handed on as if it were a complete payload.
                const bool payloadComplete = modTransferState.inProgress
                    && modTransferState.totalSize > 0
                    && modTransferState.receivedSize == modTransferState.totalSize;

                if(success && !payloadComplete) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "NetworkManager: mod transfer reported success with %zu of %zu bytes - rejecting",
                                modTransferState.receivedSize, modTransferState.totalSize);
                    abortModTransfer("Incomplete mod transfer");
                    break;
                }

                modTransferState.inProgress = false;

                if(pOnModDownloadComplete) {
                    if(success) {
                        // Pass the received mod data for saving
                        pOnModDownloadComplete(true, modTransferState.modData);
                    } else {
                        pOnModDownloadComplete(false, message);
                    }
                }

                // Clear transfer state
                modTransferState.modData.clear();
                modTransferState.modName.clear();
                modTransferState.totalSize = 0;
                modTransferState.receivedSize = 0;
                modTransferState.requested = false;
                modTransferState.requestedModName.clear();
            } break;

            case NETWORKPACKET_MOD_ACK: {
                // Host only, from an established client, lobby only (enforced by admitPacket).
                bool success = packetStream.readBool();
                std::string modChecksum = packetStream.readString();

                if(modChecksum.size() > MAX_MOD_CHECKSUM_LENGTH) {
                    noteRejectedPacket(peer, "oversized mod checksum in mod ack");
                    break;
                }

                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(peerData == nullptr) {
                    break;
                }
                const std::string ackPlayerName = peerData->name;

                SDL_Log("NetworkManager: Received mod ACK from '%s' - success: %s, checksum: %s",
                        ackPlayerName.c_str(), success ? "yes" : "no", modChecksum.c_str());

                if(pOnReceiveModAck) {
                    pOnReceiveModAck(ackPlayerName, success, modChecksum);
                }
            } break;
            
            default: {
                // Everything that is not part of the mesh handshake or a mod transfer is
                // handled by the code the relay transport shares.
                if(!routeSharedPayload(peer, packetType, packetStream)) {
                    // Unreachable: admitPacket() already refuses unknown packet types.
                    noteRejectedPacket(peer, "unknown packet type");
                }
            } break;
        }

    } catch (InputStream::eof&) {
        noteRejectedPacket(peer, "packet truncated");
        return;
    } catch (std::exception& e) {
        noteRejectedPacket(peer, e.what());
    }
}

bool NetworkManager::routeSharedPayload(NetPeer* peer, Uint32 packetType,
                                        NetworkPacketIStream& packetStream) {
    if(!GamePayloadRouter::handles(packetType)) {
        return false;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);
    if(peerData == nullptr) {
        return true;
    }

    PayloadPeerAdapter::Fields fields;
    fields.clientId           = peerData->clientId;
    fields.name               = &peerData->name;
    fields.nameAssigned       = &peerData->bNameAssigned;
    fields.isHost             = (!bIsServer) && (connectPeer != nullptr) && (peer == connectPeer);
    fields.gameVersion        = &peerData->gameVersion;
    fields.quantBotConfigHash = &peerData->quantBotConfigHash;
    fields.objectDataHash     = &peerData->objectDataHash;
    fields.modRevisionHash = &peerData->modRevisionHash;

    PayloadPeerAdapter adapter(
        fields,
        [this, peer](const char* reason) { noteRejectedPacket(peer, reason); },
        [this, peer](const std::string& newName) {
            PeerData* data = static_cast<PeerData*>(peer->data);
            if(data == nullptr) {
                return false;
            }

            // Bind names uniquely on every receiver, including clients in the mesh join window.
            bool nameTaken = (playerName == newName);
            for(const auto& peers : {peerList, awaitingConnectionList}) {
                for(NetPeer* otherPeer : peers) {
                    if(otherPeer == peer) continue;
                    auto* other = static_cast<PeerData*>(otherPeer->data);
                    if(other && other->bNameAssigned && other->name == newName) nameTaken = true;
                }
            }
            if(nameTaken) {
                disconnectPeer(peer, NETWORKDISCONNECT_PLAYER_EXISTS);
                return false;
            }

            data->name = newName;
            data->bNameAssigned = true;
            if(data->peerState == PeerData::PeerState::WaitingForName) {
                data->peerState = PeerData::PeerState::ReadyForOtherPeersToConnect;
            }
            return true;
        },
        [this]() {
            sendConfigHash(getQuantBotConfig().getConfigHash(), getObjectDataHash(),
                           VERSIONSTRING);
        },
        [peer, this](int cause) {
            disconnectPeer(peer, static_cast<enet_uint32>(cause));
        });

    GamePayloadContext context;
    context.isHost         = bIsServer;
    context.inGame         = bGameInProgress;
    context.simulationSeed = simulationSeed;
    context.allowMapWrite  = true;
    // Co-op has exactly one remote partner; on the mesh that is "the peer list holds only this
    // peer", which is also the connection to the host.
    context.coopPartnerIsSolePeer =
        (!bIsServer) && (peerList.size() == 1) && (peerList.front() == peer);

    auto callbacks = sessionCallbacks();
    callbacks.onGameInfoAccepted = [this]() {
        auto* data = static_cast<PeerData*>(connectPeer->data);
        peerList = awaitingConnectionList;
        data->peerState = PeerData::PeerState::Connected;
        data->timeout = 0;
        awaitingConnectionList.clear();
    };
    GamePayloadRouter::handle(packetType, packetStream, adapter, context, callbacks);
    return true;
}


void NetworkManager::sendPacketToHost(NetworkPacketOStream& packetStream, int channel) {
    if(isRoomSession()) {
        sendPacketOverRelay(packetStream, channel, relayHostPeerId());
        return;
    }


    if(connectPeer == nullptr) {
        // This can happen if host disconnected but game hasn't processed the quit yet
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: sendPacketToHost() failed - no host connection");
        return;
    }

#ifdef __EMSCRIPTEN__
    if(pWebRtcTransport->sendToPeer(connectPeerWebRtcId, channel, packetStream.getData(), packetStream.getDataLength()) == false) {
        SDL_Log("NetworkManager: Cannot send packet!");
    }
#else
    ENetPacket* enetPacket = packetStream.getPacket();

    if(enet_peer_send(connectPeer, channel, enetPacket) < 0) {
        SDL_Log("NetworkManager: Cannot send packet!");
    }
#endif
}

void NetworkManager::sendPacketToPeer(NetPeer* peer, NetworkPacketOStream& packetStream, int channel) {
    if(peer == nullptr) {
        return;
    }

#ifdef __EMSCRIPTEN__
    if(pWebRtcTransport->sendToPeer(peer->webRtcPeerId, channel, packetStream.getData(), packetStream.getDataLength()) == false) {
        SDL_Log("NetworkManager: Cannot send packet!");
    }
#else
    ENetPacket* enetPacket = packetStream.getPacket();

    if(enet_peer_send(peer, channel, enetPacket) < 0) {
        SDL_Log("NetworkManager: Cannot send packet!");
    }

    if(enetPacket->referenceCount == 0) {
        enet_packet_destroy(enetPacket);
    }
#endif
}


void NetworkManager::sendPacketToAllConnectedPeers(NetworkPacketOStream& packetStream, int channel) {
    if(isRoomSession()) {
        // Recipient 0 means "every other peer in this room"; the relay fans it out, and a peer
        // in another room can never be reached from here.
        sendPacketOverRelay(packetStream, channel, 0);
        return;
    }


#ifdef __EMSCRIPTEN__
    for(NetPeer* pCurrentPeer : peerList) {
        if(pWebRtcTransport->sendToPeer(pCurrentPeer->webRtcPeerId, channel, packetStream.getData(), packetStream.getDataLength()) == false) {
            SDL_Log("NetworkManager: Cannot send packet!");
            continue;
        }
    }
#else
    ENetPacket* enetPacket = packetStream.getPacket();

    for(ENetPeer* pCurrentPeer : peerList) {
        if(enet_peer_send(pCurrentPeer, channel, enetPacket) < 0) {
            SDL_Log("NetworkManager: Cannot send packet!");
            continue;
        }
    }

    if(enetPacket->referenceCount == 0) {
        enet_packet_destroy(enetPacket);
    }
#endif
}


void NetworkManager::sendChatMessage(const std::string& message)
{
    forwardObserverChat(playerName,message);
    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_CHATMESSAGE);
    packetStream.writeString(message);

    sendPacketToAllConnectedPeers(packetStream);
}

void NetworkManager::sendChangeEventList(const ChangeEventList& changeEventList)
{
    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_CHANGEEVENTLIST);
    changeEventList.save(packetStream);

    if(bIsServer) {
        sendPacketToAllConnectedPeers(packetStream);
    } else {
        sendPacketToHost(packetStream);
    }
}

void NetworkManager::sendConfigHash(const std::string& quantBotHash, const std::string& objectDataHash, const std::string& gameVersion) {
    std::string revision;
    try { revision = OnlineModPolicy::fingerprint(); }
    catch(const std::exception&) { /* Empty hash fails content verification. */ }
    SDL_Log("========== SENDING CONFIG HASHES ==========");
    SDL_Log("Role: %s", bIsServer ? "SERVER" : "CLIENT");
    SDL_Log("Protocol Version: %d", NETWORK_PROTOCOL_VERSION);
    SDL_Log("Version: %s", gameVersion.c_str());
    SDL_Log("QuantBot: %s", quantBotHash.c_str());
    SDL_Log("ObjectData: %s", objectDataHash.c_str());
    
    if(isRoomSession()) {
        NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
        packetStream.writeUint32(NETWORKPACKET_CONFIG_HASH);
        packetStream.writeUint32(NETWORK_PROTOCOL_VERSION);
        packetStream.writeString(gameVersion);
        packetStream.writeString(quantBotHash);
        packetStream.writeString(objectDataHash);
        packetStream.writeString(revision);
        if(bIsServer) {
            sendPacketOverRelay(packetStream, 0, 0);
        } else {
            sendPacketOverRelay(packetStream, 0, relayHostPeerId());
        }
        SDL_Log("Config sent successfully");
        return;
    }

    if(bIsServer) {
        // Server sends to all clients
        SDL_Log("Sending to %d client(s)", (int)peerList.size());
        for(NetPeer* pCurrentPeer : peerList) {
            NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
            packetStream.writeUint32(NETWORKPACKET_CONFIG_HASH);
            packetStream.writeUint32(NETWORK_PROTOCOL_VERSION);
            packetStream.writeString(gameVersion);
            packetStream.writeString(quantBotHash);
            packetStream.writeString(objectDataHash);
        packetStream.writeString(revision);
            sendPacketToPeer(pCurrentPeer, packetStream);
        }
    } else {
        // Client sends to server
        SDL_Log("Sending to server");
        NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
        packetStream.writeUint32(NETWORKPACKET_CONFIG_HASH);
        packetStream.writeUint32(NETWORK_PROTOCOL_VERSION);
        packetStream.writeString(gameVersion);
        packetStream.writeString(quantBotHash);
        packetStream.writeString(objectDataHash);
        packetStream.writeString(revision);
        sendPacketToHost(packetStream);
    }
    
    SDL_Log("Config sent successfully");
    SDL_Log("==========================================");
}

void NetworkManager::sendCoopMission(const GameInitSettings& settings) {
    if(!bIsServer) return;
    NetworkPacketOStream packet(NETWORK_PACKET_FLAG_RELIABLE);
    packet.writeUint32(NETWORKPACKET_COOP_MISSION);
    settings.save(packet);
    sendPacketToAllConnectedPeers(packet);
}

std::unique_ptr<GameInitSettings> NetworkManager::takeCoopMission() {
    return std::move(pendingCoopMission);
}

void NetworkManager::beginSimulation(Uint32 seed) {
    if(bIsServer) { observerTransfers.clear(); observerHistory.clear(); observerHistoryBytes=0; observerEndCycle=0; }
    if(joinLoading) {
        seed=resumeSeed; joinLoading=false;
        if(!joinName.empty() && !isSpectating()) {
            joinNotice=joinName+" joined to play";
            joinStatus=joinNotice;
            joinNoticeTime=SDL_GetTicks();
        }
        joinName.clear();
    }
    simulationSeed = seed;
    bGameInProgress = true;

    if(pRelayClient) {
        if(bIsServer) {
            // The host owns the room phase; it is what stops the relay carrying lobby traffic.
            pRelayClient->setRoomPhase(RoomRelay::Phase::Match);
        } else {
            // A client's own simulation may start a moment before the host's phase change
            // arrives. Assuming the match locally means its first command is never refused by
            // its own outgoing check - a dropped command is a desynchronised match.
            pRelayClient->assumeMatchPhase();
        }
    }
}

bool NetworkManager::sendStartGame(unsigned int timeLeft) {
    if(isRoomSession()) {
        if(!pRelayClient || !pRelayClient->isHost()) return false;
        // Use the worst connected-peer RTT to give every player time to receive the start.
        const unsigned int halfRoundTrip =
            pRelayClient ? (pRelayClient->roundTripTimeMs() / 2) : 0u;
        const unsigned int peerTimeLeft =
            (halfRoundTrip >= timeLeft) ? 0u : (timeLeft - halfRoundTrip);

        NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
        packetStream.writeUint32(NETWORKPACKET_STARTGAME);
        packetStream.writeUint32(peerTimeLeft);
        if(auto* direct = getDirectTransport()) {
#ifdef __EMSCRIPTEN__
            return direct->sendMatchStart(packetStream.getData(), packetStream.getDataLength(), timeLeft);
#else
            ENetPacket* packet = packetStream.getPacket();
            if(!packet) return false;
            const bool started = direct->sendMatchStart(packet->data, packet->dataLength, timeLeft);
            enet_packet_destroy(packet);
            return started;
#endif
        }
        if(!sendPacketOverRelay(packetStream, 0, 0)) return false;

        // Switch the room to the match phase immediately afterwards. The relay routes in order,
        // and no client can send a command before its own countdown has run, so the phase change
        // is always in place before the first command arrives - a command refused for being in
        // the wrong phase would be a lost lockstep message.
        if(pRelayClient) {
            pRelayClient->setRoomPhase(RoomRelay::Phase::Match);
        }
        return true;
    }

    for(NetPeer* pCurrentPeer : peerList) {
        NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
        packetStream.writeUint32(NETWORKPACKET_STARTGAME);

        // Clients start half a round trip earlier, but a large RTT must not wrap the
        // subtraction into a countdown of billions of milliseconds.
        const unsigned int halfRoundTrip = pCurrentPeer->roundTripTime / 2;
        const unsigned int peerTimeLeft = (halfRoundTrip >= timeLeft) ? 0u : (timeLeft - halfRoundTrip);
        packetStream.writeUint32(peerTimeLeft);

        sendPacketToPeer(pCurrentPeer, packetStream);
    }
    return true;
}

void NetworkManager::sendCommandList(const CommandList& commandList) {
    if(isSpectating()) return;
    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_UNSEQUENCED);
    packetStream.writeUint32(NETWORKPACKET_COMMANDLIST);
    packetStream.writeUint32(simulationSeed);
    commandList.save(packetStream);

    sendPacketToAllConnectedPeers(packetStream, 1);
}

void NetworkManager::sendSelectedList(const std::set<Uint32>& selectedList, int groupListIndex) {
    if(isSpectating()) return;
    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_SELECTIONLIST);
    packetStream.writeUint32(simulationSeed);
    packetStream.writeSint32(groupListIndex);
    packetStream.writeUint32Set(selectedList);

    sendPacketToAllConnectedPeers(packetStream, 0);
}

int NetworkManager::getMaxPeerRoundTripTime() {
    if(isRoomSession()) {
        // This measures the local relay hop, not the peer path or polling delivery delay.
        return pRelayClient ? static_cast<int>(pRelayClient->roundTripTimeMs()) : 0;
    }

    int maxPeerRTT = 0;

    for(NetPeer* pCurrentPeer : peerList) {
        maxPeerRTT = std::max(maxPeerRTT, (int) (pCurrentPeer->roundTripTime));
    }

    return maxPeerRTT;
}

Uint32 NetworkManager::getRelayServerRoundTripTimeMs() const {
    return pRelayClient ? pRelayClient->roundTripTimeMs() : 0u;
}

bool NetworkManager::isRelayHttpPollingSession() const {
    // A direct session never polls for gameplay, so the HTTP pacing budget that exists for the
    // polling relay must not be applied to it.
    return pRelayClient && !pRelayClient->isDirectSession()
        && pRelayClient->transportKind() == RelayTransportKind::HttpPolling;
}

void NetworkManager::debugNetwork(const char* fmt, ...) {
    if(settings.network.debugNetwork) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
}

void NetworkManager::sendClientStats(float avgFps, float simMsAvg, Uint32 queueDepth, Uint32 currentBudget, Uint32 gameCycle) {
    if(isSpectating()) return;
    // Client → Host: Send performance stats (including simulation timing for post-vsync throttling)
    if(bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host trying to send client stats (should only be called by clients)");
        return;
    }

    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_CLIENTSTATS);
    packetStream.writeUint32(simulationSeed);
    packetStream.writeUint32(gameCycle);
    packetStream.writeFloat(avgFps);
    packetStream.writeFloat(simMsAvg);  // POST-VSYNC: Add simulation timing
    packetStream.writeUint32(queueDepth);
    packetStream.writeUint32(currentBudget);

    sendPacketToHost(packetStream);
}

void NetworkManager::broadcastPathBudget(size_t newBudget, Uint32 applyCycle) {
    // Host → All Clients: Broadcast budget change
    if(!bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client trying to broadcast path budget (only host can broadcast)");
        return;
    }

    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_SETPATHBUDGET);
    packetStream.writeUint32(simulationSeed);
    packetStream.writeUint32(static_cast<Uint32>(newBudget));
    packetStream.writeUint32(applyCycle);

    sendPacketToAllConnectedPeers(packetStream);
}

void NetworkManager::sendMatchControl(Uint32 revision, Uint32 speed, Uint32 pauseCycle, Uint32 resumedPauseCycle) {
    // Host → everyone in the session, spectators included: they follow the host's pacing and
    // pause state, they just never originate it.
    if(!bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client trying to send match control (only the host decides the shared settings)");
        return;
    }
    // The same bounds every receiver applies. Sending a state that would be refused would only
    // cost every peer a refusal against its abuse budget.
    if(revision == 0 || speed < static_cast<Uint32>(GAMESPEED_MIN) || speed > static_cast<Uint32>(GAMESPEED_MAX)
       || resumedPauseCycle > pauseCycle) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Refusing to broadcast an out-of-range match control state");
        return;
    }

    const auto send = [&](Uint32 recipient, Uint32 seed) {
        NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
        packetStream.writeUint32(NETWORKPACKET_MATCH_CONTROL);
        packetStream.writeUint32(seed);
        packetStream.writeUint32(revision);
        packetStream.writeUint32(speed);
        packetStream.writeUint32(pauseCycle);
        packetStream.writeUint32(resumedPauseCycle);
        if(recipient) sendPacketOverRelay(packetStream, 0, recipient);
        else sendPacketToAllConnectedPeers(packetStream);
    };
    send(0, simulationSeed);
    // Direct player broadcasts intentionally exclude spectators. Deliver control
    // state to their established snapshot streams separately; a connecting or
    // slow spectator must never hold up the active players. Their network seed is
    // the snapshot epoch established by takeLateJoin(), not the players' seed.
    if(getDirectTransport()) {
        for(const auto& entry : observerTransfers) if(entry.second.ready) send(entry.first, entry.second.epoch);
    }
}

void NetworkManager::requestMatchResume(Uint32 pauseCycle) {
    // A paused simulation produces no command cycles, so this cannot travel in the command
    // stream: it goes straight to the host, which answers with sendMatchControl().
    if(isSpectating()) return;
    if(bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host trying to request a resume from itself");
        return;
    }
    if(pauseCycle == 0) {
        return;
    }

    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MATCH_RESUME_REQUEST);
    packetStream.writeUint32(simulationSeed);
    packetStream.writeUint32(pauseCycle);

    sendPacketToHost(packetStream);
}

void NetworkManager::sendModInfoToPeer(NetPeer* peer, const std::string& modName, const std::string& modChecksum) {
    if(isRoomSession()) {
        // Relay v1 carries bundled, matching content only. Custom content transfer is refused
        // by the relay itself; there is no client-side path for it either.
        return;
    }


    // Host → Single Client: Send active mod info
    if(!bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client trying to send mod info (only host can send)");
        return;
    }

    SDL_Log("NetworkManager: Sending mod info to peer - mod: '%s', checksum: %s", 
            modName.c_str(), modChecksum.c_str());

    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MOD_INFO);
    packetStream.writeString(modName);
    packetStream.writeString(modChecksum);

    sendPacketToPeer(peer, packetStream);
}

void NetworkManager::sendModInfo(const std::string& modName, const std::string& modChecksum) {
    if(isRoomSession()) {
        return;     // see sendModInfoToPeer()
    }

    // Host → All Clients: Send active mod info for verification
    if(!bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client trying to send mod info (only host can send)");
        return;
    }

    SDL_Log("NetworkManager: Broadcasting mod info to all clients - mod: '%s', checksum: %s", 
            modName.c_str(), modChecksum.c_str());

    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MOD_INFO);
    packetStream.writeString(modName);
    packetStream.writeString(modChecksum);

    sendPacketToAllConnectedPeers(packetStream);
}

void NetworkManager::requestModDownload(const std::string& modName) {
    if(isRoomSession()) {
        return;     // see sendModInfoToPeer()
    }

    // Client → Host: Request mod files because of checksum mismatch
    if(bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host trying to request mod download (only clients can request)");
        return;
    }

    if(!ModTransferValidation::isValidModName(modName)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NetworkManager: refusing to request mod with an unusable name");
        return;
    }

    SDL_Log("NetworkManager: Requesting mod '%s' from host", modName.c_str());

    // Remember what we asked for: mod chunks that do not belong to this request are refused.
    modTransferState.requested = true;
    modTransferState.requestedModName = modName;
    modTransferState.inProgress = false;
    modTransferState.modData.clear();
    modTransferState.modName.clear();
    modTransferState.totalSize = 0;
    modTransferState.receivedSize = 0;

    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MOD_REQUEST);
    packetStream.writeString(modName);

    sendPacketToHost(packetStream);
}

void NetworkManager::sendModFilesToPeer(NetPeer* peer, const std::string& modName) {
    // Host: Package and send mod files to requesting client

    if(!ModManager::instance().isValidModName(modName)
       || modName != ModManager::instance().getActiveModName()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NetworkManager: Rejected request for non-active or invalid mod '%s'",
                    modName.c_str());
        NetworkPacketOStream completePacket(NETWORK_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("Invalid mod request");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    // Get mod path from ModManager
    std::string modPath = ModManager::instance().getModPath(modName);
    std::string modIniPath = modPath + "/mod.ini";
    
    SDL_Log("NetworkManager::sendModFilesToPeer - modName: '%s'", modName.c_str());
    SDL_Log("NetworkManager::sendModFilesToPeer - modPath: '%s'", modPath.c_str());
    SDL_Log("NetworkManager::sendModFilesToPeer - modIniPath: '%s'", modIniPath.c_str());
    SDL_Log("NetworkManager::sendModFilesToPeer - modPath.empty(): %d", modPath.empty() ? 1 : 0);
    SDL_Log("NetworkManager::sendModFilesToPeer - existsFile(modIniPath): %d", existsFile(modIniPath) ? 1 : 0);
    
    // Check if mod exists by looking for mod.ini (modPath is a directory, not a file)
    if(modPath.empty() || !existsFile(modIniPath)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Mod '%s' not found at path: %s (mod.ini missing)", 
                    modName.c_str(), modPath.c_str());
        
        // Send failure notification
        NetworkPacketOStream completePacket(NETWORK_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("Mod not found on server");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    SDL_Log("NetworkManager: Packaging mod '%s' from path: %s", modName.c_str(), modPath.c_str());

    // Package mod files into a simple format:
    // [num_files:uint32][file1_name:string][file1_data:string][file2_name:string][file2_data:string]...
    std::string packagedData;
    
    // Write number of files placeholder (we'll update this)
    uint32_t numFiles = 0;
    
    std::vector<std::pair<std::string, std::string>> fileData;  // name -> content pairs

    try {
        const std::filesystem::path root = std::filesystem::weakly_canonical(modPath);
        std::vector<std::filesystem::path> paths;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if(entry.is_symlink()) {
                throw std::runtime_error("mod contains a symbolic link");
            }
            if(entry.is_regular_file()) {
                paths.push_back(entry.path());
            }
        }
        std::sort(paths.begin(), paths.end(), [&](const auto& lhs, const auto& rhs) {
            return std::filesystem::relative(lhs, root).generic_string()
                < std::filesystem::relative(rhs, root).generic_string();
        });

        std::size_t contentBytes = 0;
        std::set<std::string> portablePathKeys;
        for(const auto& filePath : paths) {
            const std::string filename = std::filesystem::relative(filePath, root).generic_string();
            if(filename == ".dunecity-managed") {
                continue;
            }
            std::filesystem::path normalizedPath;
            if(!ModTransferValidation::normalizeRelativeFilePath(filename, normalizedPath)
               || normalizedPath.generic_string() != filename) {
                throw std::runtime_error("mod contains a non-portable path");
            }
            if(!portablePathKeys.insert(
                   ModTransferValidation::portablePathKey(normalizedPath)).second) {
                throw std::runtime_error("mod contains duplicate or case-colliding paths");
            }
            if(fileData.size() >= 4096) {
                throw std::runtime_error("mod exceeds transfer file-count limit");
            }
            const auto fileSize = std::filesystem::file_size(filePath);
            if(fileSize > static_cast<std::uintmax_t>(MAX_MOD_TRANSFER_SIZE)
               || contentBytes + static_cast<std::size_t>(fileSize) > MAX_MOD_TRANSFER_SIZE) {
                throw std::runtime_error("This mod is larger than the 10 MiB LAN transfer limit. Connect both players to the community server, share the mod, and download it before joining.");
            }
            std::ifstream file(filePath, std::ios::binary);
            if(!file) {
                throw std::runtime_error("could not read " + filename);
            }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            contentBytes += content.size();
            fileData.push_back({filename, std::move(content)});
        }
    } catch(const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Could not package mod '%s': %s",
                    modName.c_str(), e.what());
        NetworkPacketOStream completePacket(NETWORK_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString(e.what());
        sendPacketToPeer(peer, completePacket);
        return;
    }

    if(fileData.size() > UINT32_MAX) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Mod '%s' has too many files", modName.c_str());
        return;
    }
    numFiles = static_cast<uint32_t>(fileData.size());

    if(numFiles == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: No files found in mod '%s'", modName.c_str());
        
        NetworkPacketOStream completePacket(NETWORK_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("No mod files found");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    // Build the package
    // Format: numFiles (4 bytes) + [nameLen (4 bytes) + name + dataLen (4 bytes) + data] * numFiles
    packagedData.reserve(1024 * 1024);  // Reserve 1MB initially
    
    // Write number of files
    packagedData.append(reinterpret_cast<const char*>(&numFiles), sizeof(numFiles));
    
    for(const auto& [name, content] : fileData) {
        uint32_t nameLen = static_cast<uint32_t>(name.size());
        uint32_t dataLen = static_cast<uint32_t>(content.size());
        
        packagedData.append(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        packagedData.append(name);
        packagedData.append(reinterpret_cast<const char*>(&dataLen), sizeof(dataLen));
        packagedData.append(content);
    }

    // Check size limit
    if(packagedData.size() > MAX_MOD_TRANSFER_SIZE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Mod '%s' exceeds size limit (%zu > %d)", 
                    modName.c_str(), packagedData.size(), MAX_MOD_TRANSFER_SIZE);
        
        NetworkPacketOStream completePacket(NETWORK_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("Mod exceeds size limit");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    SDL_Log("NetworkManager: Sending mod '%s' (%zu bytes total, %u files) in chunks", 
            modName.c_str(), packagedData.size(), numFiles);

    // Send in chunks
    size_t totalSize = packagedData.size();
    size_t offset = 0;
    
    while(offset < totalSize) {
        size_t chunkSize = std::min(static_cast<size_t>(MOD_CHUNK_SIZE), totalSize - offset);
        std::string chunk = packagedData.substr(offset, chunkSize);
        
        NetworkPacketOStream chunkPacket(NETWORK_PACKET_FLAG_RELIABLE);
        chunkPacket.writeUint32(NETWORKPACKET_MOD_CHUNK);
        chunkPacket.writeString(modName);
        chunkPacket.writeUint32(static_cast<Uint32>(totalSize));
        chunkPacket.writeUint32(static_cast<Uint32>(offset));
        chunkPacket.writeString(chunk);
        
        sendPacketToPeer(peer, chunkPacket);
        
        offset += chunkSize;
    }

    // Send completion notification
    NetworkPacketOStream completePacket(NETWORK_PACKET_FLAG_RELIABLE);
    completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
    completePacket.writeBool(true);
    completePacket.writeString("Transfer complete");
    sendPacketToPeer(peer, completePacket);
    
    SDL_Log("NetworkManager: Mod transfer complete for '%s'", modName.c_str());
}

void NetworkManager::sendModAck(bool success, const std::string& modChecksum) {
    if(isRoomSession()) {
        return;     // see sendModInfoToPeer()
    }

    // Client → Host: Acknowledge mod sync complete
    if(bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host trying to send mod ACK (only clients can send)");
        return;
    }

    SDL_Log("NetworkManager: Sending mod ACK to host - success: %s, checksum: %s", 
            success ? "yes" : "no", modChecksum.c_str());

    NetworkPacketOStream packetStream(NETWORK_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MOD_ACK);
    packetStream.writeBool(success);
    packetStream.writeString(modChecksum);

    sendPacketToHost(packetStream);
}
