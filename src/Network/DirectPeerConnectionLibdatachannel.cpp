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

/**
    The native half of a direct match: a real WebRTC data channel, from libdatachannel.

    It has to interoperate with a browser running P2PKit's RTCTransport, so everything visible on
    the wire is the browser's: a data-only SDP, candidates carrying their media id, the channel
    label "p2pkit", and the chunk packets defined in include/Network/P2PWireFraming.h.

    Thread safety is the delicate part. libdatachannel invokes callbacks on its own worker
    threads, and closing a peer connection does not guarantee that a callback already inside the
    library has returned. So no callback captures `this`: they capture a shared_ptr to a small
    state object that outlives the connection object if it has to, and every one of them takes
    that object's mutex and checks a closed flag before touching anything. The game thread only
    ever sees a snapshot taken in update().
*/

#include <Network/DirectPeerConnection.h>

#include <Network/P2PSignalingProtocol.h>
#include <Network/P2PWireFraming.h>

#if defined(DUNECITY_HAVE_LIBDATACHANNEL)

#include <rtc/rtc.hpp>

#include <chrono>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

const char* iceStateName(rtc::PeerConnection::IceState state) {
    switch(state) {
        case rtc::PeerConnection::IceState::New: return "new";
        case rtc::PeerConnection::IceState::Checking: return "checking";
        case rtc::PeerConnection::IceState::Connected: return "connected";
        case rtc::PeerConnection::IceState::Completed: return "completed";
        case rtc::PeerConnection::IceState::Failed: return "failed";
        case rtc::PeerConnection::IceState::Disconnected: return "disconnected";
        case rtc::PeerConnection::IceState::Closed: return "closed";
    }
    return "unknown";
}

const char* candidateTypeName(rtc::Candidate::Type type) {
    switch(type) {
        case rtc::Candidate::Type::Host: return "host";
        case rtc::Candidate::Type::ServerReflexive: return "srflx";
        case rtc::Candidate::Type::PeerReflexive: return "prflx";
        case rtc::Candidate::Type::Relayed: return "relay";
        default: return "none";
    }
}

/// Milliseconds from a monotonic clock, for chunk-group expiry only.
std::uint32_t monotonicMs() {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

/// Aggregate bytes of received values waiting for the game loop.
constexpr std::size_t kMaxQueuedValueBytes = 4 * 1024 * 1024;
/// Received values waiting for the game loop, as a count.
constexpr std::size_t kMaxQueuedValues     = 4096;
/// Local offers/answers/candidates waiting to be published.
constexpr std::size_t kMaxQueuedSignals    = 256;
/// Bytes we will let the channel buffer before refusing to hand it more.
constexpr std::size_t kMaxBufferedAmount   = 4 * 1024 * 1024;
/// Outstanding send jobs, matching the browser transport's bound.
constexpr std::size_t kMaxOutgoingJobs     = 128;
/// Candidates held while waiting for a remote description, matching the browser's bound.
constexpr std::size_t kMaxPendingCandidates = 128;
/**
    Raw ingress budget, applied before a message is parsed, copied or reassembled.

    A peer is admitted, not trusted: a flood of well-formed fragments would otherwise buy an
    unbounded amount of this computer's time before any higher-level budget noticed. These
    mirror the browser transport's raw guard, and are deliberately about *fragments*, because
    the game-level packet budget upstream cannot see them.
*/
constexpr std::uint32_t kMaxRawFramesPerSecond = 4096;
constexpr std::size_t   kMaxRawBytesPerSecond  = 16 * 1024 * 1024;

/**
    Everything a libdatachannel callback may touch.

    Owned by shared_ptr so that a callback that is already running when the connection object is
    destroyed still has something valid to write into. `closed` makes those writes harmless.
*/
struct ChannelState {
    std::mutex                            mutex;
    bool                                  closed = false;
    DirectPeerConnection::State           state  = DirectPeerConnection::State::New;
    std::string                           lastError;
    rtc::PeerConnection::IceState         iceState = rtc::PeerConnection::IceState::New;
    rtc::PeerConnection::GatheringState   gatheringState = rtc::PeerConnection::GatheringState::New;
    std::deque<DirectPeerConnection::LocalSignal> signals;
    std::deque<std::string>               values;
    std::size_t                           valueBytes = 0;
    std::uint32_t                         rawWindowStartedMs = 0;
    std::uint32_t                         rawFramesInWindow  = 0;
    std::size_t                           rawBytesInWindow   = 0;
    P2PWire::ChunkAssembler               assembler;
    std::shared_ptr<rtc::DataChannel>     channel;
    bool                                  channelAttached = false;
    std::string                           expectedLabel;

    /// Marks the connection unusable. The caller has the mutex.
    void failLocked(std::string reason) {
        if(state != DirectPeerConnection::State::Closed) {
            state = DirectPeerConnection::State::Failed;
        }
        if(lastError.empty()) {
            lastError = std::move(reason);
        }
        // Whatever is queued belongs to a stream we can no longer vouch for. Handing a prefix of
        // it to a lockstep simulation is worse than handing it nothing.
        values.clear();
        valueBytes = 0;
        signals.clear();
        assembler.reset();
    }
};

class LibDataChannelConnection final : public DirectPeerConnection {
public:
    explicit LibDataChannelConnection(const DirectPeerConnectionOptions& options)
        : shared_(std::make_shared<ChannelState>()), initiator_(options.initiator) {
        shared_->expectedLabel = options.label;

        rtc::Configuration configuration;
        // Bound SCTP reassembly inside the library, before a callback can allocate/copy a
        // whole hostile user message. Larger game payloads use P2PKit's 16 KiB fragments.
        configuration.maxMessageSize = P2PWire::Limits::kMaxChannelMessageBytes;
        for(const std::string& url : options.iceServers) {
            // Checked again here, and refused rather than filtered: the transport already screens
            // these, and quietly connecting with a different configuration than the one that was
            // handed over is the kind of success that shows up later as a player who cannot get
            // through a NAT.
            if(!P2PSignal::isAcceptableIceUrl(url)) {
                throw std::invalid_argument(
                    "the game service named a connection helper this game will not use");
            }
            configuration.iceServers.emplace_back(url);
        }
        // No TURN is ever configured, so there is no credential to leak and no relayed path to
        // fall back to: host and server-reflexive candidates only.
        configuration.disableAutoNegotiation = false;
        configuration.enableIceTcp           = false;

        connection_ = std::make_shared<rtc::PeerConnection>(configuration);

        std::weak_ptr<ChannelState> weak = shared_;

        connection_->onLocalDescription([weak](rtc::Description description) {
            const auto shared = weak.lock();
            if(!shared) {
                return;
            }
            const std::string type = description.typeString();
            LocalSignal signal;
            signal.kind    = type == "answer" ? P2PSignal::SignalKind::Answer
                                              : P2PSignal::SignalKind::Offer;
            signal.payload = std::string(description);
            std::lock_guard<std::mutex> guard(shared->mutex);
            if(shared->closed) {
                return;
            }
            if(shared->signals.size() >= kMaxQueuedSignals) {
                shared->failLocked("local signal queue overflowed");
                return;
            }
            shared->signals.push_back(std::move(signal));
        });

        connection_->onLocalCandidate([weak](rtc::Candidate candidate) {
            const auto shared = weak.lock();
            if(!shared) {
                return;
            }
            std::string line = candidate.candidate();
            // Normalised to the attribute value alone, which is what an RTCIceCandidateInit
            // carries, whichever form the library hands over.
            if(line.compare(0, 2, "a=") == 0) {
                line.erase(0, 2);
            }
            if(P2PSignal::mentionsRelayCandidate(line)) {
                return;     // never offered, because this build has no relay to offer
            }
            // "<mid>|<candidate>": the browser needs the media id to rebuild the candidate and
            // libdatachannel needs it to place the candidate on the right media description. A
            // data-only connection has one media section, so "0" stands in for a missing id -
            // both sides refuse an empty one.
            std::string mid = candidate.mid();
            if(mid.empty()) {
                mid = "0";
            }
            LocalSignal signal;
            signal.kind    = P2PSignal::SignalKind::Candidate;
            signal.payload = mid + "|" + line;
            std::lock_guard<std::mutex> guard(shared->mutex);
            if(shared->closed) {
                return;
            }
            if(shared->signals.size() >= kMaxQueuedSignals) {
                shared->failLocked("local signal queue overflowed");
                return;
            }
            shared->signals.push_back(std::move(signal));
        });

        connection_->onIceStateChange([weak](rtc::PeerConnection::IceState state) {
            const auto shared = weak.lock();
            if(!shared) return;
            std::lock_guard<std::mutex> guard(shared->mutex);
            if(shared->closed) return;
            shared->iceState = state;
            if(state == rtc::PeerConnection::IceState::Failed)
                shared->failLocked("ice negotiation failed");
        });
        connection_->onGatheringStateChange([weak](rtc::PeerConnection::GatheringState state) {
            const auto shared = weak.lock();
            if(!shared) return;
            std::lock_guard<std::mutex> guard(shared->mutex);
            if(!shared->closed) shared->gatheringState = state;
        });

        connection_->onStateChange([weak](rtc::PeerConnection::State state) {
            const auto shared = weak.lock();
            if(!shared) {
                return;
            }
            std::lock_guard<std::mutex> guard(shared->mutex);
            if(shared->closed) {
                return;     // a callback must never bring a closed connection back to life
            }
            switch(state) {
                case rtc::PeerConnection::State::Connecting:
                    if(shared->state == DirectPeerConnection::State::New) {
                        shared->state = DirectPeerConnection::State::Connecting;
                    }
                    break;
                case rtc::PeerConnection::State::Failed:
                    shared->failLocked("WebRTC connection failed");
                    break;
                case rtc::PeerConnection::State::Closed:
                case rtc::PeerConnection::State::Disconnected:
                    if(shared->state != DirectPeerConnection::State::Failed) {
                        shared->state = DirectPeerConnection::State::Closed;
                    }
                    break;
                default:
                    break;
            }
        });

        if(options.initiator) {
            rtc::DataChannelInit init;
            init.reliability.unordered = false;     // ordered, reliable: the browser's default
            attach(connection_->createDataChannel(options.label, init));
        } else {
            connection_->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> channel) {
                const auto shared = weak.lock();
                if(!shared) {
                    return;
                }
                attachShared(shared, std::move(channel));
            });
        }
    }

    ~LibDataChannelConnection() override { close(); }

    bool setRemoteDescription(P2PSignal::SignalKind kind, const std::string& sdp) override {
        if(kind == P2PSignal::SignalKind::Candidate || sdp.empty()
           || sdp.size() > P2PSignal::Limits::kMaxSignalBytes) {
            return false;
        }
        // One description, of the expected kind, exactly once. The browser transport refuses a
        // second one as well: there is no renegotiation in this protocol, so another description
        // is either a confused peer or somebody trying to move the connection somewhere else.
        const P2PSignal::SignalKind expected =
            initiator_ ? P2PSignal::SignalKind::Answer : P2PSignal::SignalKind::Offer;
        {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            if(remoteDescriptionSet_ || kind != expected) {
                shared_->lastError = "unexpected session description";
                return false;
            }
            remoteDescriptionSet_ = true;
        }
        try {
            // libdatachannel verifies the certificate the peer presents against the fingerprint
            // in this description. The transport has already checked that the fingerprint is the
            // one the signaling service bound to this pair, so accepting the description is what
            // ties the admitted player to the encrypted channel.
            //
            // The one thing removed first is a=sctp-init. Chromium offers the SCTP zero-RTT
            // extension; libdatachannel does not implement it but does reciprocate unknown
            // application attributes into its answer, so it would advertise an extension it will
            // not honour and the channel then fails on the first message. Dropping the line is
            // the difference between browser-native crossplay working and not working; nothing
            // else about the description changes, fingerprint and ICE credentials included.
            const std::string compatible = P2PSignal::stripUnsupportedSctpInit(sdp);
            connection_->setRemoteDescription(
                rtc::Description(compatible,
                                 kind == P2PSignal::SignalKind::Answer ? "answer" : "offer"));
            // Anything that arrived ahead of the description can be applied now.
            std::vector<std::string> pending;
            {
                std::lock_guard<std::mutex> guard(shared_->mutex);
                pending.swap(pendingCandidates_);
            }
            for(const std::string& candidate : pending) {
                applyCandidate(candidate);
            }
            return true;
        } catch(const std::exception& exception) {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            // It was never applied, so this side is still waiting for a description.
            remoteDescriptionSet_ = false;
            shared_->lastError    = exception.what();
            return false;
        }
    }

    bool addRemoteCandidate(const std::string& candidate) override {
        // One shared rule for what a candidate may be, so the browser and this backend accept
        // and refuse exactly the same things.
        if(!P2PSignal::isAcceptableCandidatePayload(candidate)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            if(!remoteDescriptionSet_) {
                // Trickle ICE does not wait for the description, and libdatachannel refuses a
                // candidate before there is one to attach it to. The browser transport holds
                // them until the description arrives; so does this. Throwing them away instead
                // loses most of a peer's candidates - especially on the side that waits a poll
                // for the service to attest the pair - and the connection then never completes.
                if(pendingCandidates_.size() >= kMaxPendingCandidates) {
                    shared_->lastError = "too many candidates arrived before the description";
                    return false;
                }
                pendingCandidates_.push_back(candidate);
                return true;
            }
        }
        return applyCandidate(candidate);
    }

    bool sendValue(const std::string& value) override {
        std::shared_ptr<rtc::DataChannel> channel;
        {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            if(shared_->closed || shared_->state != State::Connected) {
                return false;
            }
            channel = shared_->channel;
        }
        if(!channel || !channel->isOpen()) {
            return false;
        }

        const std::vector<std::string> packets =
            P2PWire::splitIntoChunkPackets(nextGroupId(), value);

        // Backpressure is applied *before* anything is handed over, and for the whole payload:
        // libdatachannel's send() returns false when it buffered the message rather than when it
        // rejected it (include/rtc/channel.hpp), so a false return is not a failure and the data
        // must never be sent again. Refusing up front is the only honest way to push back.
        std::size_t wanted = 0;
        for(const std::string& packet : packets) {
            wanted += packet.size();
        }
        if(channel->bufferedAmount() + wanted > kMaxBufferedAmount
           || packets.size() > kMaxOutgoingJobs) {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            shared_->lastError = "data channel is backlogged";
            return false;
        }

        try {
            for(const std::string& packet : packets) {
                // Accepted whether it went out now or was buffered; only an exception means the
                // channel refused it.
                channel->send(packet);
            }
            return true;
        } catch(const std::exception& exception) {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            shared_->failLocked(exception.what());
            return false;
        }
    }

    std::size_t bufferedAmount() const override {
        std::shared_ptr<rtc::DataChannel> channel;
        {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            channel = shared_->channel;
        }
        return channel ? static_cast<std::size_t>(channel->bufferedAmount()) : 0;
    }

    /// Copies the worker threads' view into a snapshot the game thread may read without a lock.
    void update() override {
        const State previous = stateSnapshot_;
        rtc::PeerConnection::IceState ice;
        {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            stateSnapshot_ = shared_->state;
            errorSnapshot_ = shared_->lastError;
            ice = shared_->iceState;
        }
        if(ice != iceSnapshot_) {
            iceSnapshot_ = ice;
            if(ice == rtc::PeerConnection::IceState::Connected || ice == rtc::PeerConnection::IceState::Completed)
                logIce("ice_connected");
        }
        if(stateSnapshot_ != previous) {
            if(stateSnapshot_ == State::Connected) logIce("channel_open");
            else if(stateSnapshot_ == State::Failed || stateSnapshot_ == State::Closed) logIce("failed");
        }
    }

    void close() override {
        std::shared_ptr<rtc::DataChannel>    channel;
        std::shared_ptr<rtc::PeerConnection> connection;
        {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            if(shared_->closed) return;
            if(shared_->state != State::Failed) {
                shared_->state = State::Closed;
            }
            shared_->closed = true;
            channel.swap(shared_->channel);
            shared_->values.clear();
            shared_->valueBytes = 0;
            shared_->assembler.reset();
        }
        if(stateSnapshot_ == State::New || stateSnapshot_ == State::Connecting) logIce("closed_before_channel");
        connection.swap(connection_);
        stateSnapshot_ = State::Closed;
        // Closing can block while the library drains its threads, and those threads take the
        // state mutex, so it happens with nothing held.
        try {
            if(channel) {
                channel->close();
            }
            if(connection) {
                connection->close();
            }
        } catch(const std::exception&) {
            /* closing twice is not an error worth reporting */
        }
    }

    bool pollLocalSignal(LocalSignal& out) override {
        std::lock_guard<std::mutex> guard(shared_->mutex);
        if(shared_->signals.empty()) {
            return false;
        }
        out = std::move(shared_->signals.front());
        shared_->signals.pop_front();
        return true;
    }

    bool pollValue(std::string& out) override {
        std::lock_guard<std::mutex> guard(shared_->mutex);
        if(shared_->values.empty()) {
            return false;
        }
        out = std::move(shared_->values.front());
        shared_->values.pop_front();
        shared_->valueBytes = shared_->valueBytes > out.size() ? shared_->valueBytes - out.size() : 0;
        return true;
    }

    State state() const override { return stateSnapshot_; }

    const std::string& lastError() const override { return errorSnapshot_; }

private:
    void logIce(const char* phase) {
        rtc::Candidate local, remote;
        if(connection_ && connection_->getSelectedCandidatePair(&local, &remote)) {
            selectedLocal_ = local.type();
            selectedRemote_ = remote.type();
        }
        rtc::PeerConnection::IceState ice;
        rtc::PeerConnection::GatheringState gathering;
        {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            ice = shared_->iceState;
            gathering = shared_->gatheringState;
        }
        // stderr is the game's native log and also works in the SDL-free interop tool.
        std::fprintf(stderr,
            "Direct ICE: phase=%s ice=%s gathering=%s selected_local=%s selected_remote=%s\n",
            phase, iceStateName(ice), gathering == rtc::PeerConnection::GatheringState::Complete ? "complete"
                : gathering == rtc::PeerConnection::GatheringState::InProgress ? "gathering" : "new",
            candidateTypeName(selectedLocal_), candidateTypeName(selectedRemote_));
    }

    /// Hands one already-validated candidate to the library.
    bool applyCandidate(const std::string& candidate) {
        const std::size_t split = candidate.find('|');
        const std::string mid   = candidate.substr(0, split);
        const std::string line  = candidate.substr(split + 1);
        try {
            connection_->addRemoteCandidate(rtc::Candidate(line, mid));
            return true;
        } catch(const std::exception& exception) {
            std::lock_guard<std::mutex> guard(shared_->mutex);
            shared_->lastError = exception.what();
            return false;
        }
    }

    void attach(std::shared_ptr<rtc::DataChannel> channel) {
        attachShared(shared_, std::move(channel));
    }

    /// Static so that a callback never needs the connection object to still exist.
    static void attachShared(const std::shared_ptr<ChannelState>& shared,
                             std::shared_ptr<rtc::DataChannel> channel) {
        const rtc::Reliability reliability = channel->reliability();
        bool refuse = false;
        {
            std::lock_guard<std::mutex> guard(shared->mutex);
            if(shared->closed) {
                refuse = true;
            } else if(shared->channelAttached || channel->label() != shared->expectedLabel
                      || reliability.unordered || reliability.maxRetransmits.has_value()
                      || reliability.maxPacketLifeTime.has_value()) {
                shared->failLocked("peer opened an unexpected or unreliable data channel");
                refuse = true;
            } else {
                shared->channel = channel;
                shared->channelAttached = true;
            }
        }
        if(refuse) {
            // Library callbacks may take the state mutex; never close while holding it.
            channel->close();
            return;
        }

        std::weak_ptr<ChannelState> weak = shared;
        channel->onOpen([weak]() {
            const auto state = weak.lock();
            if(!state) {
                return;
            }
            std::lock_guard<std::mutex> guard(state->mutex);
            if(!state->closed && state->state != DirectPeerConnection::State::Failed) {
                state->state = DirectPeerConnection::State::Connected;
            }
        });
        channel->onClosed([weak]() {
            const auto state = weak.lock();
            if(!state) {
                return;
            }
            std::lock_guard<std::mutex> guard(state->mutex);
            if(state->state != DirectPeerConnection::State::Failed) {
                state->state = DirectPeerConnection::State::Closed;
            }
        });
        channel->onError([weak](std::string error) {
            const auto state = weak.lock();
            if(!state) {
                return;
            }
            std::lock_guard<std::mutex> guard(state->mutex);
            state->failLocked(std::move(error));
        });
        channel->onMessage(
            [weak](rtc::binary data) {
                // A browser sends text; a binary message is decoded as UTF-8 exactly as
                // RTCTransport does for an ArrayBuffer, and then bounded like any other.
                ingest(weak, std::string(reinterpret_cast<const char*>(data.data()), data.size()));
            },
            [weak](rtc::string data) { ingest(weak, std::move(data)); });

        if(channel->isOpen()) {
            std::lock_guard<std::mutex> guard(shared->mutex);
            if(!shared->closed && shared->state != DirectPeerConnection::State::Failed) {
                shared->state = DirectPeerConnection::State::Connected;
            }
        }
    }

    static void ingest(const std::weak_ptr<ChannelState>& weak, std::string message) {
        const auto shared = weak.lock();
        if(!shared) {
            return;
        }
        std::lock_guard<std::mutex> guard(shared->mutex);
        if(shared->closed || shared->state == DirectPeerConnection::State::Failed) {
            return;
        }
        if(message.size() > P2PWire::Limits::kMaxChannelMessageBytes) {
            shared->failLocked("a player sent an oversized channel message");
            return;
        }
        // Charged before the message is parsed, reassembled or even copied.
        const std::uint32_t nowMs = monotonicMs();
        if(nowMs - shared->rawWindowStartedMs >= 1000) {
            shared->rawWindowStartedMs = nowMs;
            shared->rawFramesInWindow  = 0;
            shared->rawBytesInWindow   = 0;
        }
        ++shared->rawFramesInWindow;
        shared->rawBytesInWindow += message.size();
        if(shared->rawFramesInWindow > kMaxRawFramesPerSecond
           || shared->rawBytesInWindow > kMaxRawBytesPerSecond) {
            shared->failLocked("a player sent more data than this game will accept");
            return;
        }
        std::string value;
        const char* reason = nullptr;
        const P2PWire::ChunkAssembler::Outcome outcome =
            shared->assembler.ingest(message, monotonicMs(), value, reason);
        switch(outcome) {
            case P2PWire::ChunkAssembler::Outcome::Pending:
            case P2PWire::ChunkAssembler::Outcome::Duplicate:
                return;
            case P2PWire::ChunkAssembler::Outcome::Fatal:
                shared->failLocked(reason != nullptr ? reason : "unreadable channel message");
                return;
            case P2PWire::ChunkAssembler::Outcome::Complete:
                break;
        }
        if(shared->values.size() >= kMaxQueuedValues
           || shared->valueBytes + value.size() > kMaxQueuedValueBytes) {
            // Dropping the oldest and continuing would hand the simulation a stream with a hole
            // in it. The connection fails instead, and the player is told.
            shared->failLocked("this computer could not keep up with a player's messages");
            return;
        }
        shared->valueBytes += value.size();
        shared->values.push_back(std::move(value));
    }

    /**
        A fresh chunk group id.

        Random prefix plus a counter: unique for the life of the connection, and not guessable by
        somebody who can write to the channel and would like to collide with a group in flight.
    */
    std::string nextGroupId() {
        if(groupPrefix_.empty()) {
            std::random_device device;
            std::uniform_int_distribution<unsigned> distribution(0, 15);
            for(int i = 0; i < 8; ++i) {
                groupPrefix_.push_back(P2PWire::hexDigit(distribution(device)));
            }
        }
        std::string id = groupPrefix_;
        const std::uint64_t value = ++groupCounter_;
        for(int shift = 28; shift >= 0; shift -= 4) {
            id.push_back(P2PWire::hexDigit(static_cast<unsigned>((value >> shift) & 0xF)));
        }
        return id;
    }

    std::shared_ptr<ChannelState>        shared_;
    std::shared_ptr<rtc::PeerConnection> connection_;
    /// Game-thread copies, refreshed by update(), so no reference outlives a lock.
    State                                stateSnapshot_ = State::New;
    std::string                          errorSnapshot_;
    rtc::PeerConnection::IceState         iceSnapshot_ = rtc::PeerConnection::IceState::New;
    rtc::Candidate::Type selectedLocal_ = rtc::Candidate::Type::Unknown, selectedRemote_ = rtc::Candidate::Type::Unknown;
    bool                                 initiator_     = false;
    bool                                 remoteDescriptionSet_ = false;
    /// Candidates that arrived before the description they belong to.
    std::vector<std::string>             pendingCandidates_;
    std::uint64_t                        groupCounter_  = 0;
    std::string                          groupPrefix_;
};

} // namespace

bool isDirectPeerConnectionAvailable() {
    return true;
}

std::unique_ptr<DirectPeerConnection> createDirectPeerConnection(
    const DirectPeerConnectionOptions& options, std::string& error) {
    try {
        return std::make_unique<LibDataChannelConnection>(options);
    } catch(const std::exception& exception) {
        error = exception.what();
        return nullptr;
    }
}

#elif !defined(__EMSCRIPTEN__)

/**
    A build without libdatachannel.

    It reports honestly rather than pretending: the transport refuses to start and the player is
    told this build cannot open direct connections, instead of watching a match that looks like it
    is connecting and never does.
*/
bool isDirectPeerConnectionAvailable() {
    return false;
}

std::unique_ptr<DirectPeerConnection> createDirectPeerConnection(
    const DirectPeerConnectionOptions& options, std::string& error) {
    (void)options;
    error = "this build has no WebRTC backend";
    return nullptr;
}

#endif // DUNECITY_HAVE_LIBDATACHANNEL
