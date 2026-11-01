/*
    The direct session, driven with a scripted signaling service and fake data channels.

    These are the rules that are hard to see by reading the code and expensive to discover in a
    real match: that a match survives the service disappearing, that a peer the service says is
    gone stays connected if its channel is open, that a description with the wrong fingerprint is
    refused, that the start barrier waits for guest-to-guest links, that round trip is measured on
    the channel rather than on an HTTP request, and that a peer sending something unreadable ends
    that connection rather than being skipped past.

    The real DirectRoomTransport, the real framing and the real protocol parser are used; only the
    socket and the RTC stack are replaced.
*/

#include <catch2/catch_test_macros.hpp>
#include <SDL_log.h>

#include <Network/DirectRoomTransport.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/P2PWireFraming.h>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct CapturedConnectionLogs {
    std::string text;
    SDL_LogOutputFunction previous;
    void* previousData = nullptr;
    SDL_LogPriority priority = SDL_LogGetPriority(SDL_LOG_CATEGORY_APPLICATION);
    CapturedConnectionLogs() {
        SDL_LogGetOutputFunction(&previous, &previousData);
        SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN);
        SDL_LogSetOutputFunction([](void* data, int, SDL_LogPriority, const char* message) {
            static_cast<CapturedConnectionLogs*>(data)->text += std::string(message) + "\n";
        }, this);
    }
    ~CapturedConnectionLogs() {
        SDL_LogSetOutputFunction(previous, previousData);
        SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, priority);
    }
};

/// 32 colon-separated octets, the only shape a sha-256 fingerprint has.
const std::string kFingerprintA =
    "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:"
    "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99";
const std::string kFingerprintB =
    "11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:"
    "11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00";

std::string sdpWith(const std::string& fingerprint) {
    return "v=0\r\n"
           "o=- 1 2 IN IP4 127.0.0.1\r\n"
           "s=-\r\n"
           "t=0 0\r\n"
           "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
           "a=mid:0\r\n"
           "a=setup:active\r\n"
           "a=fingerprint:sha-256 " + fingerprint + "\r\n";
}

std::string hexOf(const std::string& text) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for(const unsigned char c : text) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 15]);
    }
    return out;
}

/// A signaling service that answers from a script and remembers every request it was sent.
class ScriptedService final : public BoundedHttpClient {
public:
    struct Exchange {
        std::string url;
        std::string body;
        std::string sessionToken;
    };

    std::vector<Exchange> requests;
    std::deque<Result>    answers;
    /// When set, every further request simply never answers, as if the service had gone away.
    bool silent = false;
    std::string phaseAnswer;

    void begin(const Request& request) override {
        requests.push_back(Exchange{request.url, request.body, request.sessionToken});
        pendingUrl_ = request.url;
        pending_    = true;
    }

    void update() override { }

    /**
        Answers from the script.

        A test scripts the answers it cares about; everything else gets a benign one. That
        matters more than it sounds: the transport has one request in flight at a time, so a
        scripted service that simply stopped answering would block every later request and a test
        about posting an offer would quietly become a test about a stalled poll. A test that
        wants silence asks for it with `silent`.

        The session request is the exception: before a session exists there is nothing benign to
        say, so it waits for its script.
    */
    bool poll(Result& out) override {
        if(!pending_ || silent) {
            return false;
        }
        if(pendingUrl_.find("/v1/p2p/session") != std::string::npos) {
            if(answers.empty()) {
                return false;
            }
            out = answers.front();
            answers.pop_front();
        } else if(pendingUrl_.find("/v1/p2p/poll") != std::string::npos) {
            if(!answers.empty()) {
                out = answers.front();
                answers.pop_front();
            } else {
                // Nothing new in the room. Deliberately carries no peer records: the transport
                // only ever adds members and acts on `gone`, so an answer that says nothing
                // changes nothing.
                out = ok("status=ok\ncursor=0\n");
            }
        } else if(pendingUrl_.find("/v1/p2p/phase") != std::string::npos) {
            auto body=requests.back().body;
            auto pos=body.find("&roster=");
            auto roster=pos==std::string::npos ? "" : body.substr(pos+8);
            for(auto p=roster.find("%2C");p!=std::string::npos;p=roster.find("%2C")) roster.replace(p,3,",");
            out=ok(phaseAnswer.empty() ? "status=ok\nphase=match\nstartId="+std::string(32,'a')+"\nroster="+roster+"\n" : phaseAnswer);
        } else {
            out = ok("status=ok\n");
        }
        pending_ = false;
        return true;
    }

    bool busy() const override { return pending_; }

    void cancel() override { pending_ = false; }

    /// The last request sent to a path, or nullptr.
    const Exchange* lastRequestTo(const std::string& suffix) const {
        for(std::size_t i = requests.size(); i > 0; --i) {
            const Exchange& exchange = requests[i - 1];
            if(exchange.url.size() >= suffix.size()
               && exchange.url.compare(exchange.url.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return &exchange;
            }
        }
        return nullptr;
    }

    static Result ok(const std::string& body) {
        Result result;
        result.httpStatus = 200;
        result.body       = body;
        return result;
    }

private:
    std::string pendingUrl_;
    bool        pending_ = false;
};

/// A data channel that does what the test tells it to.
class FakeChannel final : public DirectPeerConnection {
public:
    std::vector<std::string>  sent;
    std::deque<std::string>   inbox;
    std::deque<LocalSignal>   localSignals;
    std::vector<std::pair<P2PSignal::SignalKind, std::string>> appliedDescriptions;
    std::vector<std::string>  appliedCandidates;
    State                     current = State::Connecting;
    bool                      initiator = false;
    /// Set by a test to stand in for a channel that is full or has gone away.
    bool                      refuseSends = false;
    std::string               error;

    bool setRemoteDescription(P2PSignal::SignalKind kind, const std::string& sdp) override {
        appliedDescriptions.emplace_back(kind, sdp);
        return true;
    }
    bool addRemoteCandidate(const std::string& candidate) override {
        appliedCandidates.push_back(candidate);
        return true;
    }
    bool sendValue(const std::string& value) override {
        if(current != State::Connected || refuseSends) {
            return false;
        }
        sent.push_back(value);
        return true;
    }
    std::size_t bufferedAmount() const override { return 0; }
    void update() override { }
    void close() override { current = State::Closed; }
    bool pollLocalSignal(LocalSignal& out) override {
        if(localSignals.empty()) {
            return false;
        }
        out = localSignals.front();
        localSignals.pop_front();
        return true;
    }
    bool pollValue(std::string& out) override {
        if(inbox.empty()) {
            return false;
        }
        out = inbox.front();
        inbox.pop_front();
        return true;
    }
    State state() const override { return current; }
    const std::string& lastError() const override { return error; }

    /// Queues a value as if the peer had sent it.
    void deliver(const std::string& value) { inbox.push_back(value); }

};

/**
    What the transport actually owns.

    The fake itself is kept alive by the test, because a test needs to look at what a channel was
    asked to do *after* the transport has closed and released it - which is exactly the case when
    a peer is dropped for presenting the wrong fingerprint.
*/
class ChannelHandle final : public DirectPeerConnection {
public:
    explicit ChannelHandle(std::shared_ptr<FakeChannel> channel) : channel_(std::move(channel)) { }

    bool setRemoteDescription(P2PSignal::SignalKind kind, const std::string& sdp) override {
        return channel_->setRemoteDescription(kind, sdp);
    }
    bool addRemoteCandidate(const std::string& candidate) override {
        return channel_->addRemoteCandidate(candidate);
    }
    bool sendValue(const std::string& value) override { return channel_->sendValue(value); }
    std::size_t bufferedAmount() const override { return channel_->bufferedAmount(); }
    void update() override { channel_->update(); }
    void close() override { channel_->close(); }
    bool pollLocalSignal(LocalSignal& out) override { return channel_->pollLocalSignal(out); }
    bool pollValue(std::string& out) override { return channel_->pollValue(out); }
    State state() const override { return channel_->state(); }
    const std::string& lastError() const override { return channel_->lastError(); }

private:
    std::shared_ptr<FakeChannel> channel_;
};

/// Everything a test needs to drive one transport.
struct Harness {
    ScriptedService*                  service = nullptr;
    std::vector<std::shared_ptr<FakeChannel>> channels;
    std::uint32_t                     clock = 1000;
    bool advanceClockOnRead = false;
    std::unique_ptr<DirectRoomTransport> transport;

    Harness() {
        DirectRoomTransport::Dependencies dependencies;
        dependencies.leaveSender=[](BoundedHttpClient::Request) {};
        dependencies.httpFactory = [this]() {
            auto owned = std::make_unique<ScriptedService>();
            service = owned.get();
            return std::unique_ptr<BoundedHttpClient>(std::move(owned));
        };
        dependencies.peerFactory = [this](const DirectPeerConnectionOptions& options,
                                          std::string&) {
            auto fake = std::make_shared<FakeChannel>();
            fake->initiator = options.initiator;
            channels.push_back(fake);
            return std::unique_ptr<DirectPeerConnection>(new ChannelHandle(fake));
        };
        dependencies.clock = [this]() { return advanceClockOnRead ? clock++ : clock; };
        transport = std::make_unique<DirectRoomTransport>(std::move(dependencies));
    }

    DirectRoomTransport::Config config() const {
        DirectRoomTransport::Config config;
        config.signalingBaseUrl = "https://dunelegacy.com/p2p";
        config.grant            = std::string(64, 'a');
        config.roomCode         = "H4PQ-7T2M-9XKB";
        config.displayName      = "Stefan";
        config.appVersion       = "1.0.662";
        config.contentHash      = std::string(64, 'b');
        config.runtime          = "native";
        config.gameProtocolVersion = 5;
        return config;
    }

    /**
        Starts the session and answers the session request as the host, peer id 1.

        Afterwards a poll is in flight, so a test queues the answer it wants that poll to get and
        drives one more update.
    */
    void join() {
        std::string error;
        REQUIRE(transport->start(config(), error));
        service->answers.push_back(ScriptedService::ok(
            "status=ok\nprotocol=1\npeer=1\nsession=" + std::string(64, 'c')
            + "\nrole=host\nmaxPeers=4\nphase=lobby\nice=stun:stun.l.google.com:19302\n"));
        pump();
    }

    void pump(int times = 1, std::uint32_t stepMs = 0) {
        for(int i = 0; i < times; ++i) {
            clock += stepMs;
            transport->update();
        }
    }

    /**
        Queues one answer and drives the transport until it has been taken and acted on.

        The transport decides for itself when to poll again, so a test that assumed a fixed number
        of updates would be asserting the poll schedule by accident. This waits for the answer to
        be consumed instead, then gives the connections a turn.
    */
    void deliverPoll(const std::string& body) {
        service->answers.push_back(ScriptedService::ok(body));
        // Small steps, and as many as the transport's own poll interval needs - which is 250 ms
        // while a peer is still negotiating and 30 s once the match is running. Waiting this way
        // rather than for a fixed number of updates keeps the test about what the transport does
        // with an answer instead of about when it decided to ask.
        for(int i = 0; i < 200 && !service->answers.empty(); ++i) {
            pump(1, 250);
        }
        REQUIRE(service->answers.empty());
        pump(1, 50);
    }

    /// The same, for an answer that is not a 200.
    void deliverResult(const BoundedHttpClient::Result& result) {
        service->answers.push_back(result);
        for(int i = 0; i < 200 && !service->answers.empty(); ++i) {
            pump(1, 250);
        }
        pump(1, 50);
    }

    /// Drains events, returning the ones of one type.
    std::vector<RoomSessionTransport::Event> drain() {
        std::vector<RoomSessionTransport::Event> events;
        RoomSessionTransport::Event event;
        while(transport->pollEvent(event)) {
            events.push_back(event);
        }
        return events;
    }
};

std::string peerRecord(std::uint32_t id, const char* role, const char* name, const char* runtime) {
    return "peer=" + std::to_string(id) + "|" + role + "|" + hexOf(name) + "|" + hexOf(runtime) + "\n";
}

/// An attestation is for a pair: "this peer's certificate, on its connection to us".
std::string fingerprintRecord(std::uint32_t from, const std::string& fingerprint,
                              std::uint32_t to = 1) {
    return "fp=" + std::to_string(from) + "|" + std::to_string(to) + "|sha-256|" + fingerprint
         + "\n";
}

} // namespace

// The transport is built against these; a test binary has no libcurl session and no RTC stack.
bool isDirectPeerConnectionAvailable() {
    return true;
}

std::unique_ptr<DirectPeerConnection> createDirectPeerConnection(
    const DirectPeerConnectionOptions&, std::string& error) {
    error = "no backend in tests";
    return nullptr;
}

void sendBestEffortHttpRequest(BoundedHttpClient::Request) {}

std::unique_ptr<BoundedHttpClient> createBoundedHttpClient() {
    return nullptr;
}

TEST_CASE("A direct session refuses anything that is not a direct service", "[direct]") {
    Harness harness;
    std::string error;

    auto refuses = [&error](const std::string& url) {
        Harness fresh;
        DirectRoomTransport::Config config = fresh.config();
        config.signalingBaseUrl = url;
        error.clear();
        const bool started = fresh.transport->start(config, error);
        REQUIRE_FALSE(started);
        REQUIRE_FALSE(error.empty());
    };

    refuses("wss://relay.example.net/v1/socket");
    refuses("ws://relay.example.net/v1/socket");
    refuses("https://dunelegacy.com/relay");       // the legacy endpoint, by name
    refuses("http://dunelegacy.com/p2p");          // plaintext, not loopback
    refuses("https://dunelegacy.com/anything");    // not a direct service path
    refuses("https://dunelegacy.com/p2p?x=1");     // query strings are not endpoints

    SECTION("a loopback development service is allowed when the player chose one") {
        DirectRoomTransport::Config config = harness.config();
        config.signalingBaseUrl       = "http://127.0.0.1:8788";
        config.allowLoopbackPlaintext = true;
        REQUIRE(harness.transport->start(config, error));
    }
}

TEST_CASE("The grant and the session token never appear in a url", "[direct][security]") {
    Harness harness;
    harness.join();

    const ScriptedService::Exchange* session = harness.service->lastRequestTo("/v1/p2p/session");
    REQUIRE(session != nullptr);
    REQUIRE(session->url.find(std::string(64, 'a')) == std::string::npos);
    REQUIRE(session->body.find("grant=" + std::string(64, 'a')) != std::string::npos);

    const ScriptedService::Exchange* poll = harness.service->lastRequestTo("/v1/p2p/poll");
    REQUIRE(poll != nullptr);
    REQUIRE(poll->url.find(std::string(64, 'c')) == std::string::npos);
    REQUIRE(poll->sessionToken == std::string(64, 'c'));
    REQUIRE(harness.transport->isJoined());
    REQUIRE(harness.transport->isHost());
}

TEST_CASE("Peers are connected directly and announced once the channel opens", "[direct]") {
    Harness harness;
    harness.join();

    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    REQUIRE(harness.channels.size() == 1);

    // Nothing is announced before the channel is actually open: a peer the game can see but not
    // reach is not a player in the match.
    for(const auto& event : harness.drain()) {
        REQUIRE(event.type != RoomSessionTransport::Event::Type::PeerJoined);
    }

    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);
    bool announced = false;
    for(const auto& event : harness.drain()) {
        if(event.type == RoomSessionTransport::Event::Type::PeerJoined) {
            announced = true;
            REQUIRE(event.peerId == 2u);
            REQUIRE(event.name == "Ada");
            REQUIRE(event.runtime == "browser");
        }
    }
    REQUIRE(announced);

    SECTION("a game payload goes out as an envelope and comes back as bytes") {
        const std::vector<std::uint8_t> packet{0x10, 0x00, 0xff, 0x01};
        REQUIRE(harness.transport->sendGamePayload(packet.data(), packet.size(), 0, 0));
        REQUIRE_FALSE(harness.channels[0]->sent.empty());

        P2PWire::Envelope decoded;
        const char* reason = nullptr;
        REQUIRE(P2PWire::decodeEnvelope(harness.channels[0]->sent.back(), decoded, reason));
        REQUIRE(decoded.kind == P2PWire::EnvelopeKind::Game);
        REQUIRE(decoded.payload == packet);

        harness.channels[0]->deliver(
            P2PWire::encodeGameEnvelope(packet.data(), packet.size(), 1, 0));
        harness.pump(1, 100);
        bool received = false;
        for(const auto& event : harness.drain()) {
            if(event.type == RoomSessionTransport::Event::Type::GamePayload) {
                received = true;
                REQUIRE(event.peerId == 2u);
                REQUIRE(event.channel == 1);
                REQUIRE(event.payload == packet);
            }
        }
        REQUIRE(received);
    }

    SECTION("a payload addressed to somebody else is never forwarded") {
        const std::vector<std::uint8_t> packet{0x10, 0x00};
        harness.channels[0]->deliver(
            P2PWire::encodeGameEnvelope(packet.data(), packet.size(), 0, 99));
        harness.pump(1, 100);
        bool left = false;
        for(const auto& event : harness.drain()) {
            if(event.type == RoomSessionTransport::Event::Type::PeerLeft) {
                left = true;
            }
            REQUIRE(event.type != RoomSessionTransport::Event::Type::GamePayload);
        }
        REQUIRE(left);
    }

    SECTION("an unreadable message ends that connection rather than being skipped") {
        harness.channels[0]->deliver("\"g:0:0:zzzz\"");
        harness.pump(1, 100);
        bool left = false;
        for(const auto& event : harness.drain()) {
            if(event.type == RoomSessionTransport::Event::Type::PeerLeft) {
                left = true;
                REQUIRE_FALSE(event.message.empty());
            }
        }
        REQUIRE(left);
        REQUIRE(harness.transport->connectedPeerCount() == 0);
    }

    SECTION("round trip is measured on the channel, not on the signaling request") {
        harness.pump(1, 3000);      // past the probe interval
        bool sawPing = false;
        std::uint32_t token = 0;
        for(const std::string& value : harness.channels[0]->sent) {
            P2PWire::Envelope envelope;
            const char* reason = nullptr;
            if(P2PWire::decodeEnvelope(value, envelope, reason)
               && envelope.kind == P2PWire::EnvelopeKind::Ping) {
                sawPing = true;
                token   = envelope.token;
            }
        }
        REQUIRE(sawPing);

        harness.channels[0]->deliver(P2PWire::encodeProbeEnvelope(true, token));
        harness.pump(1, 40);
        REQUIRE(harness.transport->peerRoundTripTimeMs(2) == 40u);
        REQUIRE(harness.transport->roundTripTimeMs() == 40u);
    }

    SECTION("a ping is answered on the same channel") {
        harness.channels[0]->sent.clear();
        harness.channels[0]->deliver(P2PWire::encodeProbeEnvelope(false, 1234));
        harness.pump(1, 10);
        bool answered = false;
        for(const std::string& value : harness.channels[0]->sent) {
            P2PWire::Envelope envelope;
            const char* reason = nullptr;
            if(P2PWire::decodeEnvelope(value, envelope, reason)
               && envelope.kind == P2PWire::EnvelopeKind::Pong) {
                answered = true;
                REQUIRE(envelope.token == 1234u);
            }
        }
        REQUIRE(answered);
    }
}

TEST_CASE("Before any measurement the round trip is a cautious assumption, never zero",
          "[direct]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 10);
    REQUIRE(harness.transport->roundTripTimeMs() > 0u);
}

TEST_CASE("A description is only applied when it matches the admitted fingerprint",
          "[direct][security]") {
    Harness harness;
    harness.join();

    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA)
        + "sig=2|1|answer|" + hexOf(sdpWith(kFingerprintB)) + "\ncursor=1\n");

    // The channel was never given the description, and the peer is gone.
    REQUIRE(harness.channels.size() == 1);
    REQUIRE(harness.channels[0]->appliedDescriptions.empty());
    REQUIRE(harness.transport->peers().empty());

    SECTION("the matching one is applied") {
        Harness good;
        good.join();
        good.deliverPoll(
            "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
            + fingerprintRecord(2, kFingerprintA)
            + "sig=2|1|answer|" + hexOf(sdpWith(kFingerprintA)) + "\ncursor=1\n");
        REQUIRE(good.channels.size() == 1);
        REQUIRE(good.channels[0]->appliedDescriptions.size() == 1);
        REQUIRE(good.channels[0]->appliedDescriptions[0].first == P2PSignal::SignalKind::Answer);
    }
}

TEST_CASE("A description that arrives before its attestation is held, not applied", "[direct]") {
    Harness harness;
    harness.join();

    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + "sig=2|1|answer|" + hexOf(sdpWith(kFingerprintA)) + "\ncursor=1\n");
    REQUIRE(harness.channels.size() == 1);
    REQUIRE(harness.channels[0]->appliedDescriptions.empty());

    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=1\n");
    REQUIRE(harness.channels[0]->appliedDescriptions.size() == 1);
}

TEST_CASE("Local offers and candidates are published, relayed ones are not",
          "[direct][security]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    REQUIRE(harness.channels.size() == 1);

    DirectPeerConnection::LocalSignal offer;
    offer.kind    = P2PSignal::SignalKind::Offer;
    offer.payload = sdpWith(kFingerprintB);
    harness.channels[0]->localSignals.push_back(offer);

    DirectPeerConnection::LocalSignal relayed;
    relayed.kind    = P2PSignal::SignalKind::Candidate;
    relayed.payload = "0|candidate:1 1 udp 4188 1.2.3.4 5000 typ relay raddr 0.0.0.0 rport 0";
    harness.channels[0]->localSignals.push_back(relayed);

    DirectPeerConnection::LocalSignal host;
    host.kind    = P2PSignal::SignalKind::Candidate;
    host.payload = "0|candidate:2 1 udp 2122260223 192.168.1.2 50000 typ host";
    harness.channels[0]->localSignals.push_back(host);

    // Each signal is its own request; the scripted service acknowledges those automatically.
    harness.pump(16, 400);

    std::string posted;
    for(const auto& request : harness.service->requests) {
        if(request.url.find("/v1/p2p/signal") != std::string::npos) {
            posted += request.body + "\n";
        }
    }
    REQUIRE(posted.find("kind=offer") != std::string::npos);
    REQUIRE(posted.find("kind=candidate") != std::string::npos);
    REQUIRE(posted.find(hexOf("typ relay")) == std::string::npos);
}

TEST_CASE("A host alone in the room is not waiting for anybody", "[direct][mesh]") {
    Harness harness;
    harness.join();
    // Nobody else has joined, so there are no pairs to be ready: a host playing computer
    // opponents must not be blocked by a barrier about connections that do not exist.
    REQUIRE(harness.transport->meshReady());
    REQUIRE(harness.transport->meshBlockedReason().empty());
}

TEST_CASE("A backend that produces unusable connection details ends that connection",
          "[direct][lifetime]") {
    // Closing a link from inside the loop that is reading from it is exactly the shape that
    // leaves a freed pointer behind, so these run the real loop with a backend that makes it
    // happen on the first item and on the last.
    SECTION("an oversized local description") {
        Harness harness;
        harness.join();
        harness.deliverPoll(
            "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
            + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
        REQUIRE(harness.channels.size() == 1);

        DirectPeerConnection::LocalSignal huge;
        huge.kind    = P2PSignal::SignalKind::Offer;
        huge.payload = std::string(P2PSignal::Limits::kMaxSdpBytes + 1, 'x');
        harness.channels[0]->localSignals.push_back(huge);
        // A second signal behind it: the loop must not come back for it through a freed link.
        DirectPeerConnection::LocalSignal following;
        following.kind    = P2PSignal::SignalKind::Candidate;
        following.payload = "0|candidate:2 1 udp 2122260223 192.168.1.2 50000 typ host";
        harness.channels[0]->localSignals.push_back(following);

        harness.pump(2, 200);
        REQUIRE(harness.transport->peers().empty());
        REQUIRE(harness.transport->connectedPeerCount() == 0);
    }

    SECTION("a local candidate this protocol cannot carry") {
        Harness harness;
        harness.join();
        harness.deliverPoll(
            "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
            + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
        DirectPeerConnection::LocalSignal malformed;
        malformed.kind    = P2PSignal::SignalKind::Candidate;
        malformed.payload = "no-media-id-and-no-delimiter";
        harness.channels[0]->localSignals.push_back(malformed);

        harness.pump(2, 200);
        REQUIRE(harness.transport->peers().empty());
    }

    SECTION("more local signals than the queue holds") {
        Harness harness;
        harness.join();
        harness.deliverPoll(
            "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
            + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
        for(int i = 0; i < 400; ++i) {
            DirectPeerConnection::LocalSignal candidate;
            candidate.kind    = P2PSignal::SignalKind::Candidate;
            candidate.payload = "0|candidate:" + std::to_string(i)
                              + " 1 udp 2122260223 192.168.1.2 50000 typ host";
            harness.channels[0]->localSignals.push_back(candidate);
        }

        // The queue fills, the connection is ended, and the loop stops reading from a link that
        // no longer exists rather than walking off it.
        harness.pump(2, 200);
        REQUIRE(harness.transport->peers().empty());
        REQUIRE(harness.transport->connectedPeerCount() == 0);
    }
}

TEST_CASE("The match does not start until every pair is connected", "[direct][mesh]") {
    Harness harness;
    harness.join();

    // Three players: us (1) and two guests. The host reaches both, but the guests have to reach
    // each other too or their commands never arrive.
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + peerRecord(3, "client", "Ada2", "native")
        + fingerprintRecord(2, kFingerprintA) + fingerprintRecord(3, kFingerprintB)
        + "cursor=0\n");
    REQUIRE(harness.channels.size() == 2);

    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.channels[1]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);
    REQUIRE(harness.transport->connectedPeerCount() == 2);
    REQUIRE_FALSE(harness.transport->meshReady());          // nobody has reported yet

    // Peer 2 reports the same room and both links; peer 3 is still only connected to the host.
    harness.channels[0]->deliver(P2PWire::encodeReadinessEnvelope("1,2,3", {1, 3}));
    harness.channels[1]->deliver(P2PWire::encodeReadinessEnvelope("1,2,3", {1}));
    harness.pump(1, 100);
    REQUIRE_FALSE(harness.transport->meshReady());
    REQUIRE_FALSE(harness.transport->meshBlockedReason().empty());

    // The delayed guest-to-guest link finally completes.
    harness.channels[1]->deliver(P2PWire::encodeReadinessEnvelope("1,2,3", {1, 2}));
    harness.pump(1, 100);
    REQUIRE(harness.transport->meshReady());

    SECTION("a peer that sees a different room is not ready either") {
        harness.channels[1]->deliver(P2PWire::encodeReadinessEnvelope("1,3", {1}));
        harness.pump(1, 100);
        REQUIRE_FALSE(harness.transport->meshReady());
    }

    SECTION("we tell the others what we can reach") {
        bool sawReadiness = false;
        for(const std::string& value : harness.channels[0]->sent) {
            P2PWire::Envelope envelope;
            const char* reason = nullptr;
            if(P2PWire::decodeEnvelope(value, envelope, reason)
               && envelope.kind == P2PWire::EnvelopeKind::Readiness) {
                sawReadiness = true;
                REQUIRE(envelope.rosterKey == "1,2,3");
            }
        }
        REQUIRE(sawReadiness);
    }
}

TEST_CASE("A started match outlives the signaling service", "[direct][resilience]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);
    // The match has started: the roster is frozen, and from here the players carry it themselves.
    REQUIRE(harness.transport->setRoomPhase(RoomRelay::Phase::Match));
    harness.drain();

    SECTION("a healthy settled poll has time to answer after its scheduled interval") {
        harness.pump(2, 1); // Send and acknowledge the pending match-phase request.
        harness.drain();
        harness.service->silent = true;
        harness.pump(1, 30000); // The next settled-match poll is now due.
        harness.pump(1, 100);   // A normal round trip must not report an outage.
        REQUIRE_FALSE(harness.transport->signalingLost());
        for(const auto& event : harness.drain())
            REQUIRE(event.type != RoomSessionTransport::Event::Type::Diagnostic);

        harness.service->silent = false;
        harness.pump(1, 200);
        REQUIRE_FALSE(harness.transport->signalingLost());
        for(const auto& event : harness.drain())
            REQUIRE(event.type != RoomSessionTransport::Event::Type::Diagnostic);

        // Do not hide a real outage: the existing grace still expires, while
        // gameplay continues directly between the connected players.
        harness.service->silent = true;
        harness.pump(40, 5000);
        REQUIRE(harness.transport->signalingLost());
        REQUIRE(harness.transport->isJoined());
        REQUIRE(harness.transport->connectedPeerCount() == 1);
    }

    SECTION("the service simply stops answering") {
        harness.service->silent = true;
        // Far beyond any session expiry the service might apply.
        harness.pump(40, 5000);
        REQUIRE(harness.transport->isJoined());
        REQUIRE(harness.transport->connectedPeerCount() == 1);
        REQUIRE(harness.transport->signalingLost());

        const std::vector<std::uint8_t> packet{0x10, 0x01};
        REQUIRE(harness.transport->sendGamePayload(packet.data(), packet.size(), 0, 0));
    }

    SECTION("the service says our membership is gone") {
        BoundedHttpClient::Result unauthorized;
        unauthorized.httpStatus = 401;
        unauthorized.body       = "status=error\ncode=forbidden\n";
        harness.deliverResult(unauthorized);
        REQUIRE(harness.transport->isJoined());
        REQUIRE(harness.transport->connectedPeerCount() == 1);
    }

    SECTION("the service says a connected player left") {
        harness.deliverPoll(
            "status=ok\nphase=match\ngone=2\ncursor=0\n");
        // The channel is open, so the channel is what counts.
        REQUIRE(harness.transport->connectedPeerCount() == 1);
        for(const auto& event : harness.drain()) {
            REQUIRE(event.type != RoomSessionTransport::Event::Type::PeerLeft);
        }
    }

    SECTION("a player joining after the start is refused, not admitted") {
        harness.deliverPoll(
            "status=ok\nphase=match\n" + peerRecord(2, "client", "Ada", "browser")
            + peerRecord(3, "client", "Late", "native")
            + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
        REQUIRE(harness.transport->peers().size() == 1);
        bool refused = false;
        for(const auto& event : harness.drain()) {
            if(event.type == RoomSessionTransport::Event::Type::Refused && event.peerId == 3u) {
                refused = true;
            }
        }
        REQUIRE(refused);
    }
}

TEST_CASE("Before the match starts, losing the admission authority is fatal",
          "[direct][resilience][security]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);
    harness.drain();
    REQUIRE(harness.transport->connectedPeerCount() == 1);

    SECTION("a revoked session ends the lobby even with a channel open") {
        BoundedHttpClient::Result unauthorized;
        unauthorized.httpStatus = 403;
        unauthorized.body       = "status=error\ncode=forbidden\n";
        harness.deliverResult(unauthorized);
        REQUIRE(harness.transport->status() == RoomSessionTransport::Status::Closed);
        REQUIRE_FALSE(harness.transport->statusMessage().empty());
    }

    SECTION("an explicit closure ends the lobby") {
        harness.deliverPoll("status=ok\nphase=lobby\nclosed=1000\ncursor=0\n");
        REQUIRE(harness.transport->status() == RoomSessionTransport::Status::Closed);
    }

    SECTION("the service going silent ends the lobby") {
        harness.service->silent = true;
        harness.pump(40, 5000);
        REQUIRE(harness.transport->status() == RoomSessionTransport::Status::Closed);
    }
}

TEST_CASE("A broadcast reaches every player or none of them", "[direct][lockstep]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + peerRecord(3, "client", "Bo", "native")
        + fingerprintRecord(2, kFingerprintA) + fingerprintRecord(3, kFingerprintB)
        + "cursor=0\n");
    REQUIRE(harness.channels.size() == 2);
    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.channels[1]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);
    harness.drain();

    // One peer's channel will not take any more. Reporting success because the other one did is
    // exactly how two players end up with different command histories.
    harness.channels[1]->refuseSends = true;
    const std::vector<std::uint8_t> packet{0x10, 0x00, 0x01};
    REQUIRE_FALSE(harness.transport->sendGamePayload(packet.data(), packet.size(), 0, 0));

    bool left = false;
    for(const auto& event : harness.drain()) {
        if(event.type == RoomSessionTransport::Event::Type::PeerLeft && event.peerId == 3u) {
            left = true;
        }
    }
    REQUIRE(left);
    REQUIRE(harness.transport->connectedPeerCount() == 1);
}

TEST_CASE("Losing the host ends the session for a client", "[direct][lockstep]") {
    Harness harness;
    std::string error;
    REQUIRE(harness.transport->start(harness.config(), error));
    // This time we are a client, and peer 2 is the host.
    harness.service->answers.push_back(ScriptedService::ok(
        "status=ok\nprotocol=1\npeer=1\nsession=" + std::string(64, 'c')
        + "\nrole=client\nmaxPeers=4\nphase=lobby\n"));
    harness.pump();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "host", "Ada", "browser")
        + peerRecord(3, "client", "Bo", "native")
        + fingerprintRecord(2, kFingerprintA) + fingerprintRecord(3, kFingerprintB)
        + "cursor=0\n");
    REQUIRE(harness.channels.size() == 2);
    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.channels[1]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);
    harness.drain();

    // The host's channel dies. The other client is still reachable, but there is no room left:
    // nothing else can start the match, and lockstep has no way to appoint a new host.
    harness.channels[0]->current = DirectPeerConnection::State::Failed;
    harness.pump(1, 100);
    REQUIRE(harness.transport->status() == RoomSessionTransport::Status::Closed);
    REQUIRE(harness.transport->closeCode() == RoomRelay::Close::HostLeft);
}

TEST_CASE("Losing any player after the start ends the match", "[direct][lockstep]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + peerRecord(3, "client", "Bo", "native")
        + fingerprintRecord(2, kFingerprintA) + fingerprintRecord(3, kFingerprintB)
        + "cursor=0\n");
    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.channels[1]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);
    REQUIRE(harness.transport->setRoomPhase(RoomRelay::Phase::Match));
    harness.drain();

    harness.channels[1]->current = DirectPeerConnection::State::Failed;
    harness.pump(1, 100);
    REQUIRE(harness.transport->status() == RoomSessionTransport::Status::Closed);
    REQUIRE(harness.transport->statusMessage().find("cannot continue") != std::string::npos);
}

TEST_CASE("The host tells the service the match started, even while busy", "[direct][security]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);

    // A poll is in flight, which is the normal state of affairs. The phase change must not be
    // lost because of it: it is what stops a grant issued a moment ago being redeemed into a
    // match that has already started.
    REQUIRE(harness.transport->setRoomPhase(RoomRelay::Phase::Match));
    for(int i = 0; i < 4; ++i) {
        harness.service->answers.push_back(ScriptedService::ok("status=ok\ncursor=0\n"));
    }
    harness.pump(12, 400);

    const ScriptedService::Exchange* phase = harness.service->lastRequestTo("/v1/p2p/phase");
    REQUIRE(phase != nullptr);
    REQUIRE(phase->body == "phase=match&roster=1%2C2");
    REQUIRE(phase->sessionToken == std::string(64, 'c'));
}

TEST_CASE("A flood from an admitted player is cut off", "[direct][adversarial]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    harness.channels[0]->current = DirectPeerConnection::State::Connected;
    harness.pump(1, 100);
    harness.drain();

    // Every message is well formed, so nothing else would ever complain about them: this is the
    // ingress budget's job, not the parser's.
    const std::vector<std::uint8_t> packet(1024, 0x5a);
    const std::string envelope =
        P2PWire::encodeGameEnvelope(packet.data(), packet.size(), 0, 0);
    for(int i = 0; i < 6000; ++i) {
        harness.channels[0]->deliver(envelope);
    }

    // Driven the way the game loop drives it - a few milliseconds per frame, draining events
    // each time - so that what fires is the per-second budget rather than the local event queue
    // filling up, which is a different defence with a different outcome.
    bool cut = false;
    std::size_t delivered = 0;
    for(int frame = 0; frame < 40 && !cut; ++frame) {
        harness.pump(1, 5);
        for(const auto& event : harness.drain()) {
            if(event.type == RoomSessionTransport::Event::Type::GamePayload) {
                ++delivered;
            }
            if(event.type == RoomSessionTransport::Event::Type::PeerLeft) {
                cut = true;
            }
        }
    }
    REQUIRE(cut);
    REQUIRE(harness.transport->connectedPeerCount() == 0);
    // It was cut off at its budget, not after everything it sent had been handed to the game.
    REQUIRE(delivered <= NetworkPacketPolicy::kMaxPacketsPerWindow);
    REQUIRE(delivered > 0);
}

TEST_CASE("A player cannot be renamed or re-roled once admitted", "[direct][security]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    REQUIRE(harness.transport->peers().size() == 1);

    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "host", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    REQUIRE(harness.transport->peers().empty());
}

TEST_CASE("A peer that never connects is a player-facing failure", "[direct]") {
    Harness harness;
    harness.join();
    harness.deliverPoll(
        "status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    REQUIRE(harness.channels.size() == 1);

    harness.channels[0]->current = DirectPeerConnection::State::Failed;
    harness.pump(1, 100);

    bool explained = false;
    for(const auto& event : harness.drain()) {
        if(!event.message.empty()
           && event.message.find("Could not open a direct connection") != std::string::npos) {
            explained = true;
            // No mention of falling back to anything, because there is no fallback.
            REQUIRE(event.message.find("relay") == std::string::npos);
        }
    }
    REQUIRE(explained);
}

TEST_CASE("Direct setup failures identify the completed phase without guessing NAT", "[direct][regression]") {
    CapturedConnectionLogs logs;
    Harness h;
    h.join();
    h.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
        + fingerprintRecord(2, kFingerprintA) + "cursor=0\n");
    auto channel = h.channels[0];
    bool described = true, timeout = false;
    std::string expected;
    SECTION("no remote description is a signaling failure") {
        described = false;
        channel->error = "ice negotiation failed";
        expected = "Connection setup did not finish exchanging details";
    }
    SECTION("native ICE failure explains direct-only networking") {
        channel->error = "ice negotiation failed";
        expected = "no relay fallback";
    }
    SECTION("browser ICE failure explains direct-only networking") {
        channel->error = "WebRTC failed; ICE failed";
        expected = "no relay fallback";
    }
    SECTION("a non-ICE backend failure does not blame a router") {
        channel->error = "data channel error";
        expected = "direct data channel did not open";
    }
    SECTION("a deadline alone is not proof of blocked NAT") {
        timeout = true;
        expected = "direct data channel did not open";
    }
    if(described) {
        channel->localSignals.push_back({P2PSignal::SignalKind::Offer, sdpWith(kFingerprintB)});
        channel->localSignals.push_back({P2PSignal::SignalKind::Candidate,
            "0|candidate:2 1 udp 2122260223 192.0.2.1 50000 typ host"});
        h.pump(16, 400);
        h.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2, "client", "Ada", "browser")
            + fingerprintRecord(2, kFingerprintA)
            + "sig=2|1|answer|" + hexOf(sdpWith(kFingerprintA)
                + "a=candidate:3 1 udp 2122260223 192.0.2.2 51000 typ srflx\r\n") + "\ncursor=1\n");
    }
    if(!timeout) channel->current = DirectPeerConnection::State::Failed;
    h.pump(1, timeout ? 46000 : 100);
    bool explained = false;
    for(const auto& event : h.drain()) {
        if(event.message.find(expected) != std::string::npos) {
            explained = true;
            if(expected != "no relay fallback") {
                REQUIRE(event.message.find("NAT") == std::string::npos);
                REQUIRE(event.message.find("firewall") == std::string::npos);
            }
        }
    }
    REQUIRE(explained);
    REQUIRE(logs.text.find("phase=" + std::string(!described ? "signaling"
        : expected == "no relay fallback" ? "ice" : "data_channel")) != std::string::npos);
    if(described) {
        REQUIRE(logs.text.find("local_description_published=1 remote_description_applied=1") != std::string::npos);
        REQUIRE(logs.text.find("local_host=1 local_srflx=0 local_prflx=0 remote_host=0 remote_srflx=1") != std::string::npos);
    }
    REQUIRE(logs.text.find("192.0.2.") == std::string::npos);
    REQUIRE(logs.text.find(kFingerprintA) == std::string::npos);
}

TEST_CASE("A refused session is reported in the player's words", "[direct]") {
    Harness harness;
    std::string error;
    REQUIRE(harness.transport->start(harness.config(), error));
    harness.service->answers.push_back(ScriptedService::ok(
        "status=error\ncode=room_full\nmessage=That game is already full.\n"));
    harness.pump(2, 100);

    REQUIRE(harness.transport->status() == RoomSessionTransport::Status::Closed);
    REQUIRE(harness.transport->statusMessage() == "That game is already full.");
}

TEST_CASE("An open lobby channel does not override an authoritative departure", "[direct][security]") {
    Harness h; h.join();
    h.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2,"client","Ada","browser")
        + fingerprintRecord(2,kFingerprintA) + "cursor=0\n");
    h.channels[0]->current = DirectPeerConnection::State::Connected;
    h.pump(1,100);
    h.deliverPoll("status=ok\nphase=lobby\ngone=2\ncursor=0\n");
    REQUIRE(h.channels[0]->current == DirectPeerConnection::State::Closed);
    REQUIRE(h.transport->connectedPeerCount() == 0);
}

TEST_CASE("Old signaling pages cannot replay an answer or roll the cursor back", "[direct][security]") {
    Harness h; h.join();
    const auto page = "status=ok\nphase=lobby\n" + peerRecord(2,"client","Ada","browser")
        + fingerprintRecord(2,kFingerprintA);
    h.deliverPoll(page + "sig=2|1|answer|" + hexOf(sdpWith(kFingerprintA)) + "\ncursor=1\n");
    REQUIRE(h.channels[0]->appliedDescriptions.size() == 1);
    h.deliverPoll(page + "sig=2|1|answer|" + hexOf(sdpWith(kFingerprintB)) + "\ncursor=1\n");
    h.deliverPoll(page + "cursor=0\n");
    REQUIRE(h.channels[0]->appliedDescriptions.size() == 1);
    h.pump(5,400);
    REQUIRE(h.service->lastRequestTo("/v1/p2p/poll")->body == "cursor=1");
}

TEST_CASE("The game-start send fails the whole match when the last channel refuses", "[direct][lockstep]") {
    Harness h; h.join();
    h.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2,"client","Ada","browser")
        +peerRecord(3,"client","Bo","native")+fingerprintRecord(2,kFingerprintA)
        +fingerprintRecord(3,kFingerprintB)+"cursor=0\n");
    for(auto& c:h.channels)c->current=DirectPeerConnection::State::Connected;
    h.pump(1,100);
    h.channels[0]->inbox.push_back(P2PWire::encodeReadinessEnvelope("1,2,3",{1,3}));
    h.channels[1]->inbox.push_back(P2PWire::encodeReadinessEnvelope("1,2,3",{1,2}));
    h.pump();
    REQUIRE(h.transport->meshReady());
    h.channels[1]->refuseSends=true;
    const std::uint8_t start[]={0,0,0,5,0,0,11,184};
    REQUIRE(h.transport->sendMatchStart(start,sizeof(start)));
    h.pump(8,10);
    REQUIRE(h.transport->status()==RoomSessionTransport::Status::Closed);
    REQUIRE(h.channels[0]->current==DirectPeerConnection::State::Closed);
    REQUIRE(h.channels[1]->current==DirectPeerConnection::State::Closed);
}

TEST_CASE("A connection created during a frame does not time out from unsigned underflow", "[direct][regression]") {
    Harness h; h.join();
    h.advanceClockOnRead = true;
    h.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2,"client","Ada","browser")
        +fingerprintRecord(2,kFingerprintA)+"cursor=0\n");
    REQUIRE(h.channels.size()==1);
    REQUIRE(h.channels[0]->current==DirectPeerConnection::State::Connecting);
    REQUIRE(h.transport->isJoined());
    h.pump(1,60000);
    REQUIRE(h.channels[0]->current==DirectPeerConnection::State::Closed);
}

namespace {
void readyTwoGuests(Harness& h) {
    h.join();
    h.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2,"client","Ada","browser")
        +peerRecord(3,"client","Bo","native")+fingerprintRecord(2,kFingerprintA)
        +fingerprintRecord(3,kFingerprintB)+"cursor=0\n");
    for(auto& c:h.channels) c->current=DirectPeerConnection::State::Connected;
    h.pump();
    h.channels[0]->deliver(P2PWire::encodeReadinessEnvelope("1,2,3",{1,3}));
    h.channels[1]->deliver(P2PWire::encodeReadinessEnvelope("1,2,3",{1,2}));
    h.pump(); REQUIRE(h.transport->meshReady());
}
unsigned startEvents(Harness& h) {
    RoomSessionTransport::Event e; unsigned count=0;
    while(h.transport->pollEvent(e)) if(e.type==RoomSessionTransport::Event::Type::MatchStart) ++count;
    return count;
}
}
TEST_CASE("A host countdown waits for the service and every frozen peer", "[direct][start]") {
    Harness h;readyTwoGuests(h);
    const std::uint8_t packet[]={8,0,0,0,184,11,0,0};
    REQUIRE(h.transport->sendMatchStart(packet,sizeof(packet)));
    REQUIRE(startEvents(h)==0);
    REQUIRE_FALSE(h.transport->acceptStartCallback());
    h.pump(8,10);
    REQUIRE(startEvents(h)==0);
    const auto ack=P2PWire::encodeStartEnvelope('a',std::string(32,'a'),"1,2,3");
    h.channels[0]->deliver(ack);h.pump();REQUIRE(startEvents(h)==0);
    h.channels[0]->deliver(ack);h.pump();REQUIRE(startEvents(h)==0);
    h.channels[1]->deliver(ack);h.pump();REQUIRE(startEvents(h)==1);
    REQUIRE(h.transport->acceptStartCallback());REQUIRE_FALSE(h.transport->acceptStartCallback());
    h.channels[1]->deliver(ack);h.pump();REQUIRE(startEvents(h)==0);
}
TEST_CASE("An authoritative roster mismatch never sends prepare or starts a countdown", "[direct][start]") {
    Harness h;readyTwoGuests(h);
    h.service->phaseAnswer="status=ok\nphase=match\nstartId="+std::string(32,'a')+"\nroster=1,2,3,4\n";
    const std::uint8_t packet[]={8,0,0,0,184,11,0,0};
    REQUIRE(h.transport->sendMatchStart(packet,sizeof(packet)));h.pump(8,10);
    REQUIRE(h.transport->status()==RoomSessionTransport::Status::Closed);
    REQUIRE(startEvents(h)==0);
    for(const auto& c:h.channels)for(const auto& value:c->sent) {
        P2PWire::Envelope e;const char* why=nullptr;REQUIRE(P2PWire::decodeEnvelope(value,e,why));
        REQUIRE(e.kind!=P2PWire::EnvelopeKind::Start);
    }
}
TEST_CASE("A missing start acknowledgement ends the room without starting", "[direct][start]") {
    Harness h;readyTwoGuests(h);const std::uint8_t packet[]={8,0,0,0,184,11,0,0};
    REQUIRE(h.transport->sendMatchStart(packet,sizeof(packet)));h.pump(8,10);
    h.pump(1,31000);
    REQUIRE(h.transport->status()==RoomSessionTransport::Status::Closed);REQUIRE(startEvents(h)==0);
}
TEST_CASE("A failed final commit closes all peers and never starts the host", "[direct][start]") {
    Harness h;readyTwoGuests(h);const std::uint8_t packet[]={8,0,0,0,184,11,0,0};
    REQUIRE(h.transport->sendMatchStart(packet,sizeof(packet)));h.pump(8,10);
    const auto ack=P2PWire::encodeStartEnvelope('a',std::string(32,'a'),"1,2,3");
    for(auto& c:h.channels)c->deliver(ack);
    h.channels[1]->refuseSends=true;h.pump();
    REQUIRE(h.transport->status()==RoomSessionTransport::Status::Closed);
    REQUIRE(startEvents(h)==0);
    for(const auto& c:h.channels) REQUIRE(c->current==DirectPeerConnection::State::Closed);
}
TEST_CASE("A guest accepts exactly one committed start after matching preparation", "[direct][start]") {
    Harness h;std::string error;REQUIRE(h.transport->start(h.config(),error));
    h.service->answers.push_back(ScriptedService::ok("status=ok\nprotocol=1\npeer=1\nsession="+std::string(64,'c')
        +"\nrole=client\nmaxPeers=4\nphase=lobby\n"));h.pump();
    h.deliverPoll("status=ok\nphase=lobby\n"+peerRecord(2,"host","Ada","browser")+fingerprintRecord(2,kFingerprintA)+"cursor=0\n");
    auto c=h.channels[0];c->current=DirectPeerConnection::State::Connected;h.pump();
    c->deliver(P2PWire::encodeReadinessEnvelope("1,2",{1}));h.pump();
    REQUIRE_FALSE(h.transport->acceptStartCallback());
    const auto id=std::string(32,'a');
    c->deliver(P2PWire::encodeStartEnvelope('p',id,"1,2"));h.pump();
    REQUIRE_FALSE(h.transport->acceptStartCallback());
    const std::vector<std::uint8_t> packet={8,0,0,0,184,11,0,0};
    c->deliver(P2PWire::encodeStartEnvelope('c',id,"1,2",packet));h.pump();
    REQUIRE(h.transport->acceptStartCallback());REQUIRE_FALSE(h.transport->acceptStartCallback());
    RoomSessionTransport::Event e;unsigned delivered=0;
    while(h.transport->pollEvent(e))if(e.type==RoomSessionTransport::Event::Type::GamePayload)++delivered;
    REQUIRE(delivered==1);
    c->deliver(P2PWire::encodeStartEnvelope('c',id,"1,2",packet));h.pump();
    while(h.transport->pollEvent(e)) REQUIRE(e.type!=RoomSessionTransport::Event::Type::GamePayload);
    REQUIRE_FALSE(h.transport->acceptStartCallback());
}

TEST_CASE("Changed readiness refreshes our report without echoing identical reports", "[direct][start]") {
    Harness h;
    h.join();
    h.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2,"client","Partner","native") + "cursor=0\n");
    auto channel = h.channels[0];
    channel->current = DirectPeerConnection::State::Connected;
    h.pump();
    const auto report = P2PWire::encodeReadinessEnvelope("1,2", {2});
    const auto countReports = [&]() { return std::count(channel->sent.begin(), channel->sent.end(), report); };
    const auto initial = countReports();
    REQUIRE(initial > 0);
    // Our first report can reach a peer before its authenticated role update.
    // Its first controller report must elicit a fresh copy of ours.
    channel->deliver(P2PWire::encodeReadinessEnvelope("1,2", {1}));
    h.pump();
    REQUIRE(countReports() == initial + 1);
    channel->deliver(P2PWire::encodeReadinessEnvelope("1,2", {1}));
    h.pump();
    REQUIRE(countReports() == initial + 1);
}

TEST_CASE("A guest waits for cross-channel readiness before acknowledging a start", "[direct][start]") {
    Harness h;
    std::string error;
    REQUIRE(h.transport->start(h.config(), error));
    h.service->answers.push_back(ScriptedService::ok(
        "status=ok\nprotocol=1\npeer=1\nsession=" + std::string(64, 'c')
        + "\nrole=client\nmaxPeers=4\nphase=lobby\n"));
    h.pump();
    h.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2,"host","Host","browser")
        + peerRecord(3,"client","Partner","native") + "cursor=0\n");
    REQUIRE(h.channels.size() == 2);
    for(auto& channel : h.channels) channel->current = DirectPeerConnection::State::Connected;
    h.pump();
    auto host = h.channels[0];
    auto partner = h.channels[1];
    const auto id = std::string(32, 'a');
    const auto prepare = P2PWire::encodeStartEnvelope('p', id, "1,2,3");
    const auto ack = P2PWire::encodeStartEnvelope('a', id, "1,2,3");
    host->deliver(P2PWire::encodeReadinessEnvelope("1,2,3", {1,3}));
    // Different peer channels have no shared delivery ordering: the host's
    // prepare can arrive before Partner's readiness, even within one pump.
    host->deliver(prepare);
    h.pump();
    REQUIRE(h.transport->isJoined());
    REQUIRE(std::count(host->sent.begin(), host->sent.end(), ack) == 0);
    REQUIRE_FALSE(h.transport->acceptStartCallback());

    SECTION("late readiness acknowledges once, then allows the committed start") {
        partner->deliver(P2PWire::encodeReadinessEnvelope("1,2,3", {1,2}));
        h.pump();
        REQUIRE(h.transport->isJoined());
        REQUIRE(std::count(host->sent.begin(), host->sent.end(), ack) == 1);
        host->deliver(prepare);
        h.pump();
        REQUIRE(std::count(host->sent.begin(), host->sent.end(), ack) == 1);
        host->deliver(P2PWire::encodeStartEnvelope('c', id, "1,2,3", {8,0,0,0,184,11,0,0}));
        h.pump();
        REQUIRE(h.transport->acceptStartCallback());
        REQUIRE_FALSE(h.transport->acceptStartCallback());
    }
    SECTION("replayed preparation cannot extend the readiness deadline") {
        h.pump(1, 20000);
        host->deliver(prepare);
        h.pump();
        h.pump(1, 11000);
        REQUIRE(h.transport->status() == RoomSessionTransport::Status::Closed);
        REQUIRE(std::count(host->sent.begin(), host->sent.end(), ack) == 0);
    }
    SECTION("commit before readiness never starts the guest") {
        host->deliver(P2PWire::encodeStartEnvelope('c', id, "1,2,3", {8,0,0,0,184,11,0,0}));
        h.pump();
        REQUIRE(h.transport->status() == RoomSessionTransport::Status::Closed);
        REQUIRE_FALSE(h.transport->acceptStartCallback());
    }
    SECTION("a conflicting roster is still rejected") {
        host->deliver(P2PWire::encodeStartEnvelope('p', id, "1,2"));
        h.pump();
        REQUIRE(h.transport->status() == RoomSessionTransport::Status::Closed);
    }
}

TEST_CASE("Spectators can leave a frozen match without closing player connections", "[direct][spectator]") {
    Harness harness;
    harness.join();
    harness.deliverPoll("status=ok\nphase=lobby\n" + peerRecord(2,"client","Observer","browser")
        + fingerprintRecord(2,kFingerprintA) + "cursor=0\n");
    harness.channels[0]->current=DirectPeerConnection::State::Connected;
    harness.pump(1,100);
    harness.transport->setSpectators({"Observer"});
    REQUIRE(harness.transport->setRoomPhase(RoomRelay::Phase::Match));
    harness.drain();
    harness.channels[0]->current=DirectPeerConnection::State::Failed;
    harness.pump(1,100);
    REQUIRE(harness.transport->isJoined());
    REQUIRE(harness.transport->peers().empty());
}

TEST_CASE("An observer cannot delay player readiness, broadcasts or match start", "[direct][spectator]") {
    Harness h; h.join();
    h.deliverPoll("status=ok\nphase=lobby\n"+peerRecord(2,"client","Player","native")+"cursor=0\n");
    h.channels[0]->current=DirectPeerConnection::State::Connected;
    h.channels[0]->deliver(P2PWire::encodeReadinessEnvelope("1,2",{1}));
    h.pump(); REQUIRE(h.transport->meshReady());
    h.transport->assumeMatchPhase(); h.drain();
    auto observer=peerRecord(3,"client","Watcher","browser");
    observer.pop_back(); observer+="|1\n";
    h.deliverPoll("status=ok\nphase=match\n"+peerRecord(2,"client","Player","native")+observer+"cursor=0\n");
    REQUIRE(h.channels.size()==2);
    // Observer is still connecting and has never reported readiness.
    REQUIRE(h.transport->meshReady());
    const std::vector<std::uint8_t> payload{2,0,0,0};
    REQUIRE(h.transport->sendGamePayload(payload.data(),payload.size(),0,0));
    REQUIRE(h.transport->isJoined());
    h.channels[1]->current=DirectPeerConnection::State::Connected; h.pump();
    h.channels[1]->sent.clear();
    REQUIRE(h.transport->sendGamePayload(payload.data(),payload.size(),0,0));
    REQUIRE(h.channels[1]->sent.empty());
    h.channels[1]->refuseSends=true;
    REQUIRE_FALSE(h.transport->sendGamePayload(payload.data(),payload.size(),0,3));
    REQUIRE(h.transport->isJoined()); REQUIRE(h.transport->meshReady());
    REQUIRE(h.channels[0]->current==DirectPeerConnection::State::Connected);
    bool spectatorLeft=false;
    for(const auto& event : h.drain()) if(event.type==RoomSessionTransport::Event::Type::PeerLeft) {
        REQUIRE(event.peerId==3); REQUIRE(event.spectator); spectatorLeft=true;
    }
    REQUIRE(spectatorLeft);
}
