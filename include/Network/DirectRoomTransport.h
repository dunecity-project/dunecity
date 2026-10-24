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

#ifndef DIRECTROOMTRANSPORT_H
#define DIRECTROOMTRANSPORT_H

/**
    A room whose gameplay never touches a server.

    HTTPS admission says who may be in the room. This transport then asks the signaling service
    for a session, learns who else is in the room, and opens one WebRTC data channel to each of
    them. From the moment the channels are open, the service carries nothing: it is polled for
    membership and phase, and it may go away entirely without ending the match.

    What it deliberately does not do:

      - no relay, no TURN and no forwarding. A message is sent once per peer over that peer's own
        channel, and a message addressed to someone else is dropped rather than passed on.
      - no fallback to HTTP or WebSocket gameplay. A peer that will not connect is a player-facing
        failure, because a silent fallback is how "direct play" quietly becomes relayed play.
      - no second game parser. A received payload goes to the same NetworkManager receive path,
        and from there to GamePayloadRouter, as every other transport.

    Identity: the signaling service tells each member which DTLS fingerprint it admitted for the
    others. A description whose fingerprint does not match is refused before it reaches the RTC
    stack, and the RTC stack then verifies the certificate actually presented against the
    fingerprint in the description it accepted. So the peer that ends up on the channel is the
    peer the HTTPS admission let into the room.
*/

#include <Network/DirectPeerConnection.h>
#include <Network/LateJoinPolicy.h>
#include <Network/BoundedHttpClient.h>
#include <Network/P2PSignalingProtocol.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/P2PWireFraming.h>
#include <Network/RoomSessionTransport.h>

#include <deque>
#include <bitset>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class DirectRoomTransport : public RoomSessionTransport {
public:
    struct Config {
        /// HTTPS base url of the signaling service, no trailing slash. Never a ws:// endpoint.
        std::string   signalingBaseUrl;
        std::string   grant;                    ///< single use, from HTTPS admission
        std::string   roomCode;
        std::string   displayName;
        std::string   appVersion;
        std::string   contentHash;
        std::string   runtime;                  ///< "native" or "browser"
        std::uint16_t gameProtocolVersion = 0;
        bool          allowLoopbackPlaintext = false;
        bool          allowLateJoin = false;
    };

    /// Injection points, so the tests can drive the real state machine without a socket or a NIC.
    struct Dependencies {
        std::function<std::unique_ptr<BoundedHttpClient>()> httpFactory;
        std::function<std::unique_ptr<DirectPeerConnection>(const DirectPeerConnectionOptions&,
                                                            std::string&)> peerFactory;
        /// Monotonic milliseconds. Defaults to SDL_GetTicks().
        std::function<std::uint32_t()> clock;
        std::function<void(BoundedHttpClient::Request)> leaveSender;
    };

    DirectRoomTransport();
    explicit DirectRoomTransport(Dependencies dependencies);
    DirectRoomTransport(const DirectRoomTransport&) = delete;
    DirectRoomTransport& operator=(const DirectRoomTransport&) = delete;
    ~DirectRoomTransport() override;

    /**
        Validates the endpoint and begins the session.
        \param  error  set to a player-facing reason if the session cannot be started
    */
    bool start(const Config& config, std::string& error);

    void update() override;
    void stop(std::uint8_t reason) override;
    bool pollEvent(Event& event) override;

    Status status() const override { return status_; }
    bool   isJoined() const override { return status_ == Status::Joined; }
    bool   isHost() const override { return localRole_ == RoomRelay::Role::Host; }

    const std::string& roomCode() const override { return roomCode_; }
    void updateInvitationCode(const std::string& code) override {
        if(RoomRelay::isAcceptableRoomCode(code)) roomCode_ = code;
    }

    std::uint32_t    localPeerId() const override { return localPeerId_; }
    std::uint8_t     maxPeers() const override { return maxPeers_; }
    RoomRelay::Phase phase() const override { return phase_; }

    const std::string& statusMessage() const override { return statusMessage_; }
    std::uint16_t      closeCode() const override { return closeCode_; }

    /**
        The worst direct round trip to any connected player.

        Measured with a probe on the data channel itself, never from the signaling request: how
        long an HTTPS POST takes says nothing about how long a command takes to reach a player
        when the command does not go through that server. Before the first probe answers this
        reports a cautious wide-area assumption rather than zero, because zero would size the
        lockstep input buffer for a LAN.
    */
    Uint32 roundTripTimeMs() const override;

    /// Round trip to the signaling service. Diagnostics only; never used to size a buffer.
    Uint32 signalingRoundTripTimeMs() const { return roundTripMs_; }

    RelayTransportKind transportKind() const override {
        return RelayTransportKind::DirectPeerToPeer;
    }

    std::size_t outgoingBacklogBytes() const override;

    const std::vector<Peer>& peers() const override { return peers_; }
    Peer*       findPeer(std::uint32_t peerId) override;
    const Peer* findPeer(std::uint32_t peerId) const override;

    bool sendGamePayload(const std::uint8_t* payload, std::size_t length, int channel,
                         std::uint32_t recipient) override;
    bool setRoomPhase(RoomRelay::Phase phase) override;
    /// Recheck the whole mesh and freeze its roster before accepting or sending STARTGAME.
    bool prepareMatchStart();
    /// Atomically commit the roster before fanout; any rejection closes every match link.
    bool sendMatchStart(const std::uint8_t* payload, std::size_t length, unsigned int localDelay = 3000);
    bool acceptStartCallback();
    /**
        The local simulation has started, whatever the service thinks.

        Freezes the roster for the same reason the host's own start does: from here on there is
        no state a new member could be given, so membership stops being the service's to change.
    */
    void assumeMatchPhase() override {
        phase_ = RoomRelay::Phase::Match;
        freezeRoster("this computer started the match");
    }
    bool sendDiagnostic(RoomRelay::DiagnosticKind kind, const std::uint8_t* payload,
                        std::size_t length) override;

    bool isDirectSession() const override { return true; }

    /// Number of peers whose data channel is open. The lobby shows progress with it.
    std::size_t connectedPeerCount() const;

    /**
        True when every admitted player is connected to every other admitted player.

        A host with a channel to each guest has not established that the guests can reach each
        other, and a match started in that state loses guest-to-guest commands silently. Each peer
        reports the roster it sees and the peers it has open channels to; this is true only when
        all of those agree with our own view. Any membership change resets it.
    */
    bool meshReady() const;

    /// A player-facing sentence naming what the mesh is still waiting for, or empty when ready.
    std::string meshBlockedReason() const;

    /// Round trip on the direct channel to one peer, or 0 before the first sample.
    Uint32 peerRoundTripTimeMs(std::uint32_t peerId) const;

    /// True once the signaling service has stopped answering. Not fatal to a running match.
    bool signalingLost() const { return signalingLost_; }

    using JoinRequest=LateJoinPolicy::Request;
    const std::vector<JoinRequest>& joinRequests() const { return joinRequests_; }
    bool manageJoin(const std::string& action, const std::string& request);
    bool joinDecisionPending() const { return !joinAction_.empty(); }
    bool joinDecisionSucceeded() const { return joinDecisionOK_; }
    bool requestToPlay(bool cancel = false);
    const std::string& playRequestState() const { return playRequestState_; }
    bool prepareSpectatorPromotion();
    bool allowsLateJoin() const { return config_.allowLateJoin; }
    // Only the authenticated host's synchronization packet may call this on a client.
    bool openJoinWindow(const std::string& name);
    void abortJoinWindow();
    void completeJoinWindow() { joinName_.clear(); }
    void setSpectators(const std::set<std::string>& names) { spectators_=names; }
    bool isSpectating() const { return localSpectator_; }
    bool isSpectatorPeer(std::uint32_t id) const {
        const auto* peer=findPeer(id); return peer && peer->spectator;
    }
    bool peerConnected(std::uint32_t id) const;
    void disconnectSpectator(std::uint32_t id, const std::string& reason) {
        if(isSpectatorPeer(id)) dropLink(id,reason);
    }

    std::size_t queuedEventCount() const { return events_.size(); }

private:
    /// What we are doing with the signaling service right now.
    enum class SignalingStage {
        Idle,
        OpeningSession,     ///< POST /v1/p2p/session with the single-use grant
        Running,            ///< polling, and posting our own offers and candidates
        Stopped
    };

    /// One other player, plus everything needed to reach them and nothing that could relay.
    struct Link {
        std::uint32_t                         peerId = 0;
        std::unique_ptr<DirectPeerConnection> connection;
        /// The fingerprint the signaling service attested for this peer, if it has yet.
        std::string  fingerprintAlgorithm;
        std::string  fingerprintValue;
        /// Descriptions held back because the attestation has not arrived yet.
        std::deque<P2PSignal::SignalRecord> deferred;
        bool         initiator      = false;
        bool         connected      = false;
        bool         announced      = false;    ///< a PeerJoined event has been emitted
        RoomRelay::Role role        = RoomRelay::Role::Unknown;   ///< remembered across removal
        std::uint32_t startedMs     = 0;
        std::uint32_t refusals      = 0;

        /**
            Ingress accounting, applied before anything expensive happens to a message.

            The same windows, and the same code, the ENet path has used for years
            (NetworkPacketPolicy): a peer that is admitted is not thereby allowed to spend this
            computer's time without limit. The backend has already bounded raw channel frames
            before parsing them; this is the budget for messages that became whole payloads.
        */
        NetworkPacketPolicy::RateWindow packetWindow;
        NetworkPacketPolicy::RateWindow byteWindow;

        // Direct-path liveness. Measured on the channel that actually carries the game, because
        // a signaling round trip says nothing about how long a command takes to reach a player.
        std::uint32_t lastPingMs    = 0;
        std::uint32_t pingSentMs    = 0;
        std::uint32_t pingToken     = 0;
        bool          pingOutstanding = false;
        std::uint32_t roundTripMs  = 0;
        bool          haveRoundTrip = false;

        // What this peer last said about the room it sees and the peers it can reach. The match
        // does not start until every peer agrees with us and with each other.
        std::string                rosterKey;
        std::vector<std::uint32_t> reportedConnections;
        bool                       reportedReadiness = false;
    };

    /// One thing waiting to be posted to the signaling service.
    struct OutgoingSignal {
        std::uint32_t         to   = 0;
        P2PSignal::SignalKind kind = P2PSignal::SignalKind::Offer;
        std::string           payload;
        /// Attempts already made. A handshake message is retried; it is not setup to lose.
        int                   attempts = 0;
    };

    void beginSession();
    void queueLeave();
    void handleStartEnvelope(Link& link, const P2PWire::Envelope& envelope);
    void acknowledgeStartIfReady();
    void completeStartIfReady();
    void beginStartPrepare(const BoundedHttpClient::Result& result);
    void freezeRoster(const char* why);
    bool isFrozenMember(std::uint32_t peerId) const;
    bool chargeIngress(Link& link, std::size_t bytes, std::uint32_t nowMs);
    void beginNextSignalingRequest(std::uint32_t nowMs);
    void handleSessionResult(const BoundedHttpClient::Result& result);
    void handlePollResult(const BoundedHttpClient::Result& result);
    void handlePostResult(const BoundedHttpClient::Result& result);
    void pumpSignaling(std::uint32_t nowMs);
    void pumpConnections(std::uint32_t nowMs);
    void applySignal(const P2PSignal::SignalRecord& record);
    void applyDescription(Link& link, const P2PSignal::SignalRecord& record);
    void openLink(std::uint32_t peerId, std::uint32_t nowMs);
    void dropLink(std::uint32_t peerId, const std::string& reason);
    void handleReceivedValue(Link& link, const std::string& value, std::uint32_t nowMs);
    /// Hands a connected link's still-queued values to handleReceivedValue before it is dropped.
    void deliverQueuedValues(std::uint32_t peerId, std::uint32_t nowMs);
    void sendReadiness();
    std::string localRosterKey() const;
    bool sendEnvelopeTo(Link& link, const std::string& envelope);
    void queueSignal(std::uint32_t to, P2PSignal::SignalKind kind, const std::string& payload);
    void pushEvent(Event&& event);
    void refuse(Link& link, const char* reason);
    void finish(std::uint16_t code, const std::string& message);
    Link* findLink(std::uint32_t peerId);
    Peer* mutablePeer(std::uint32_t peerId);
    std::uint32_t now() const;
    std::string endpoint(const char* path) const;
    BoundedHttpClient::Request signalingRequest() const;

    void pumpJoinRequests(std::uint32_t nowMs);
    void pumpPlayRequest(std::uint32_t nowMs);
    std::unique_ptr<BoundedHttpClient> playHttp_;
    std::string playRequestState_ = "none", playAction_;
    std::uint32_t nextPlayPoll_ = 0;
    bool promotionPrepared_ = false;
    std::unique_ptr<BoundedHttpClient> joinHttp_;
    std::vector<JoinRequest> joinRequests_;
    std::string joinAction_, joinRequestId_, joinName_;
    bool joinDecisionOK_ = false;
    std::uint32_t nextJoinPoll_ = 0;
    Dependencies dependencies_;
    Config       config_;
    std::unique_ptr<BoundedHttpClient> http_;

    Status           status_     = Status::Idle;
    SignalingStage   stage_      = SignalingStage::Idle;
    RoomRelay::Role  localRole_  = RoomRelay::Role::Unknown;
    bool localSpectator_ = false;
    RoomRelay::Phase phase_      = RoomRelay::Phase::Lobby;
    std::uint32_t    localPeerId_ = 0;
    std::uint8_t     maxPeers_   = 0;
    std::string      roomCode_;
    std::string      sessionToken_;
    std::string      statusMessage_;
    std::uint16_t    closeCode_  = 0;
    Uint32           roundTripMs_ = 0;
    bool             signalingLost_ = false;
    std::vector<std::string> iceServers_;

    std::vector<Peer>              peers_;
    /// Peers that have left but whose PeerLeft event has not been taken yet. What they sent before
    /// leaving is queued in front of that event and still has to find its sender (findPeer).
    std::vector<Peer>              departing_;
    std::vector<std::unique_ptr<Link>> links_;
    std::bitset<65536> retiredPeerIds_;
    std::deque<Event>              events_;
    std::deque<OutgoingSignal>     outgoing_;
    std::size_t                    eventBytes_ = 0;

    std::uint32_t  nextPingToken_   = 1;
    bool           readinessDirty_  = true;
    /**
        Set once the match has started, locally or by the host.

        Before it, this session still depends on the admission authority, so losing that
        authority is fatal. After it, the roster is frozen and the players carry the match
        themselves, so losing the service is only a diagnostic.
    */
    bool           matchStarted_    = false;
    enum class StartStage { Idle, ClosingRoster, AwaitingMesh, Preparing, Committed };
    StartStage startStage_ = StartStage::Idle;
    std::string startId_, startRoster_;
    std::vector<std::uint8_t> startPayload_;
    std::vector<std::uint32_t> startAcks_;
    std::uint32_t startDeadline_ = 0;
    unsigned int startDelay_ = 3000;
    unsigned int sessionRetries_ = 0;
    bool startCallbackAccepted_ = false;
    bool leaveQueued_ = false;
    std::vector<std::uint32_t> frozenRoster_;
    std::set<std::string> spectators_;
    /// The phase the host still owes the service, and when it stopped being worth retrying.
    bool           phaseUpdatePending_ = false;
    RoomRelay::Phase pendingPhase_   = RoomRelay::Phase::Lobby;
    OutgoingSignal inFlightSignal_;
    bool           haveInFlightSignal_ = false;
    std::uint64_t  cursor_          = 0;
    std::uint32_t  requestStartedMs_ = 0;
    std::uint32_t  lastPollMs_      = 0;
    std::uint32_t  lastAnswerMs_    = 0;
    std::uint32_t  sessionStartedMs_ = 0;
    /// What the request in flight was for, so its answer is parsed as the right shape.
    enum class InFlight { None, Session, Poll, Post, Phase };
    InFlight       inFlight_        = InFlight::None;

    static constexpr std::size_t   kMaxQueuedEvents      = 4096;
    static constexpr std::size_t   kMaxQueuedEventBytes  = 4 * 1024 * 1024;
    static constexpr std::size_t   kMaxQueuedSignals     = 256;
    static constexpr std::uint32_t kNegotiatingPollMs    = 250;
    static constexpr std::uint32_t kIdlePollMs           = 1000;
    static constexpr std::uint32_t kConnectedPollMs      = 3000;
    static constexpr std::uint32_t kMatchPollMs          = 30000;
    static constexpr std::uint32_t kRequestTimeoutMs     = 15000;
    static constexpr std::uint32_t kSignalingLostMs      = 30000;
    static constexpr std::uint32_t kConnectDeadlineMs    = 45000;
    static constexpr std::uint32_t kMaxRefusalsPerPeer   = 32;
    /// Attempts for one handshake message before setup is declared failed.
    static constexpr int          kMaxSignalAttempts    = 4;
    /**
        Completed payloads taken from one peer in one update().

        The ingress budget bounds how much a peer may send per second; this bounds how much of it
        this loop will look at in one frame, so a peer that queued a burst cannot turn a single
        update into an arbitrarily long one. The remainder is still there next frame, and the
        per-second budget is what decides whether the peer is sending too much at all.
    */
    static constexpr std::size_t  kMaxValuesPerUpdate   = 256;
    static constexpr std::uint32_t kPingIntervalMs       = 2000;
    /**
        What a peer's round trip is assumed to be before the first probe answers.

        Zero would tell CommandBufferPolicy that the link is free, which sizes the input buffer
        for a LAN and then stalls a wide-area match on the first jitter. This is deliberately a
        cautious wide-area guess, replaced by a measurement within a couple of seconds.
    */
    static constexpr Uint32       kAssumedRoundTripMs   = 200;
};

#endif // DIRECTROOMTRANSPORT_H
