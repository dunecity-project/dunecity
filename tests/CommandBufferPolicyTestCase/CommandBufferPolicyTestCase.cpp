/*
 * Regression model for relay command allowance. Public 1.0.660 gameplay advanced at roughly
 * 37-42 cycles/s despite 60Hz rendering. This model isolates periodic delivery and the lockstep
 * watermark; it is not the production transport, and does not prove real-world latency bounds.
 */

#include <catch2/catch_all.hpp>

#include <CommandBufferPolicy.h>
#include <CommandEmissionSchedule.h>
#include <CommandValidation.h>
#include <Definitions.h>

#include <Network/RelayPollProtocol.h>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace {

/// Cycles a second a match is configured for, at a given millisecond-per-cycle game speed.
double configuredRate(int gameSpeedMs) {
    return 1000.0 / static_cast<double>(gameSpeedMs);
}

/**
    The delivery pipeline of a relay session, in the shape RelayHttpTransport gives it.

    A frame reaches the relay one uplink leg after it is queued. It then waits, because the
    relay can only hand it over inside an answer to a request of the receiver's, and the
    receiver has at most one request in flight - so its requests arrive one poll period apart.
    The answer takes a downlink leg to come back. The quantisation is the point: it is why the
    delay is not "half a round trip" and why a budget built out of one is too small.
*/
struct DeliveryPath {
    Uint32 uplinkMs    = 200;
    Uint32 downlinkMs  = 200;
    Uint32 pollPeriodMs = 500;

    Uint32 arrivalOf(Uint32 queuedAtMs) const {
        const Uint32 atRelay = queuedAtMs + uplinkMs;
        // The next request of the receiver's, rounded up to the poll grid.
        const Uint32 answered = ((atRelay + pollPeriodMs - 1u) / pollPeriodMs) * pollPeriodMs;
        return answered + downlinkMs;
    }

    /// Worst case one-way delay of a finished cycle, emission cadence included.
    Uint32 worstCaseOneWayMs() const {
        return CommandBufferPolicy::kEmissionIntervalMs + uplinkMs + pollPeriodMs + downlinkMs;
    }
};

/// One peer of a lockstep pair: its cycle, its buffer, and what it has heard about the other.
struct SimPeer {
    Uint32 cycle        = 0;
    Uint32 buffer       = 0;
    Uint32 debtMs       = 0;    ///< real time earned but not yet spent on a cycle
    Uint32 lastEmitMs   = 0;
    bool   everEmitted  = false;
    /// HumanPlayer::nextExpectedCommandsCycle for the other peer: the exclusive window end.
    Uint32 peerFrontier = 0;
    /// (arrival time, window end) pairs still on the wire towards this peer.
    std::vector<std::pair<Uint32, Uint32>> inbox;

    Uint32 waitFrames = 0;      ///< how often the wait rule blocked a cycle
};

/**
    Two peers, one millisecond per step.

    Per step, in the order the game does it: deliver whatever has arrived, emit if the cadence
    says so, then simulate as much as the earned real time and the wait rule allow. The wait
    rule is the production one - Game::handleNetworkUpdates() waits while
    `nextExpectedCommandsCycle <= gameCycleCount`, so a cycle may only be entered while the
    frontier is strictly above the current cycle.
*/
class LockstepPair {
public:
    LockstepPair(Uint32 bufferA, Uint32 bufferB, DeliveryPath path, int gameSpeedMs)
     : path_(path), gameSpeedMs_(static_cast<Uint32>(gameSpeedMs)) {
        peers_[0].buffer = bufferA;
        peers_[1].buffer = bufferB;
    }

    void run(Uint32 milliseconds) {
        for(Uint32 now = 0; now <= milliseconds; now++) {
            deliver(now);
            emit(now);
            advance();
        }
        ranMs_ = milliseconds;
    }

    double cyclesPerSecond(int index) const {
        return (ranMs_ == 0) ? 0.0
                             : (peers_[index].cycle * 1000.0 / static_cast<double>(ranMs_));
    }

    double slowestCyclesPerSecond() const {
        return std::min(cyclesPerSecond(0), cyclesPerSecond(1));
    }

    Uint32 cycleOf(int index) const { return peers_[index].cycle; }
    Uint32 waitFramesOf(int index) const { return peers_[index].waitFrames; }

private:
    void deliver(Uint32 now) {
        for(SimPeer& peer : peers_) {
            std::vector<std::pair<Uint32, Uint32>> stillFlying;
            for(const auto& message : peer.inbox) {
                if(message.first <= now) {
                    // A window end never moves backwards; the watermark is monotone.
                    peer.peerFrontier = std::max(peer.peerFrontier, message.second);
                } else {
                    stillFlying.push_back(message);
                }
            }
            peer.inbox.swap(stillFlying);
        }
    }

    void emit(Uint32 now) {
        for(int i = 0; i < 2; i++) {
            SimPeer& sender = peers_[i];
            if(sender.everEmitted
               && (now - sender.lastEmitMs) < CommandBufferPolicy::kEmissionIntervalMs) {
                continue;
            }
            sender.everEmitted = true;
            sender.lastEmitMs  = now;
            peers_[1 - i].inbox.emplace_back(path_.arrivalOf(now), sender.cycle + sender.buffer);
        }
    }

    void advance() {
        for(SimPeer& peer : peers_) {
            peer.debtMs = std::min(peer.debtMs + 1u, kCatchUpCycles * gameSpeedMs_);
            bool blocked = false;
            while(peer.debtMs >= gameSpeedMs_) {
                if(peer.peerFrontier <= peer.cycle) {
                    blocked = true;
                    break;
                }
                peer.cycle++;
                peer.debtMs -= gameSpeedMs_;
            }
            if(blocked) {
                peer.waitFrames++;
            }
        }
    }

    SimPeer peers_[2];
    DeliveryPath path_;
    Uint32 gameSpeedMs_;
    /// The frame loop lets only this much unspent real time accumulate before discarding the
    /// rest: max(getGameSpeed() * 3, 24) in Game::runMainLoop().
    static constexpr Uint32 kCatchUpCycles = 3;
    Uint32 ranMs_ = 0;
};

/// The sizing Game::initializeNetwork() used before this change, for a relay session.
Uint32 legacyRelayBufferCycles(Uint32 relayHeartbeatRoundTripMs) {
    const Uint32 rttBuffer = static_cast<Uint32>(MILLI2CYCLES(relayHeartbeatRoundTripMs)) + 5u;
    return std::max(rttBuffer, 10u);        // the "internet" minimum
}

/**
    The cycle window a peer running the *previous* build accepts, written out rather than called,
    to independently check that the unchanged receiver rule accepts our largest window.
*/
Uint32 legacyMaxAcceptableCommandCycle(Uint32 currentCycle, Uint32 networkCycleBuffer) {
    return currentCycle + networkCycleBuffer * 2u + CommandValidation::kCycleWindowSlack;
}

} // namespace


// -------------------------------------------------------------------------------------------
// The constants this policy is built out of belong to other files
// -------------------------------------------------------------------------------------------

TEST_CASE("CommandBufferPolicy: the budget's terms are the real protocol constants",
          "[network][relay][buffer]") {
    // Two of the four terms are our own protocol and are exact. Mirroring them is only safe
    // while they are checked, because a change to either silently changes the budget.
    REQUIRE(CommandBufferPolicy::kEmissionIntervalMs == CommandEmissionSchedule::kIntervalMs);
    REQUIRE(CommandBufferPolicy::kRelayHoldMs
            == static_cast<Uint32>(RoomPoll::Timing::kServerHoldMs));

    // The gateway holds an idle exchange for far less than a round trip, which is why a frame
    // waits for the receiver's *next* request rather than being handed to one already there.
    // If that ever stopped being true the budget could shrink; it is an assumption, so it is
    // written down as one.
    REQUIRE(CommandBufferPolicy::kRelayHoldMs < CommandBufferPolicy::kMinRelayBudgetMs);
}

TEST_CASE("CommandBufferPolicy: the largest buffer still emits a parseable packet",
          "[network][relay][buffer][security]") {
    // One emission carries the history window plus the whole buffer, one entry per cycle, and
    // a receiver refuses a packet with more entries than this outright - which ends a match
    // rather than slowing it down. CommandBufferPolicy.h static_asserts the same thing; this
    // repeats it as a runtime check so the margin is visible in the test output.
    const Uint32 entries =
        CommandBufferPolicy::emittedEntryCount(CommandBufferPolicy::kMaxRelayBufferCycles);
    REQUIRE(entries <= CommandValidation::kMaxCommandListEntries);
    REQUIRE(CommandValidation::isAcceptableCommandListEntryCount(entries));

    // And it has to be a ceiling on the cycle count, not only on the millisecond budget: at
    // GAMESPEED_MIN the same budget converts to eight times as many cycles as at the default.
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(0xFFFFFFFFu, GAMESPEED_MIN)
            == CommandBufferPolicy::kMaxRelayBufferCycles);
}

TEST_CASE("CommandBufferPolicy: a peer on the previous sizing still accepts our window",
          "[network][relay][buffer][compatibility]") {
    // The command wire and receiver-window rules stay unchanged. A hypothetical peer sized
    // by MILLI2CYCLES(rtt) + 5 would still accept this window. That
    // receiver accepts cycles up to `itsCycle + 2 * itsBuffer + kCycleWindowSlack` and drops a
    // batch whole when one entry is outside - so being refused by it stalls the match and then
    // ends it on the lockstep timeout. This is the bound kMaxRelayBufferCycles comes from.
    const Uint32 legacyBuffer = legacyRelayBufferCycles(0);          // its worst case: no sample
    REQUIRE(legacyBuffer == 10u);

    for(int gameSpeedMs = GAMESPEED_MIN; gameSpeedMs <= GAMESPEED_MAX; gameSpeedMs++) {
        const Uint32 ourBuffer =
            CommandBufferPolicy::relayCommandBufferCycles(0xFFFFFFFFu, gameSpeedMs);
        INFO("game speed " << gameSpeedMs << " ms, our buffer " << ourBuffer);

        // It can be at most one of its own buffers behind us - that buffer is what lets us run
        // at all - and our window reaches one short of our buffer past our own cycle.
        const Uint32 legacyCycle = 100000;
        const Uint32 ourCycle    = legacyCycle + legacyBuffer - 1;
        const Uint32 topOfWindow = ourCycle + ourBuffer - 1;

        // The independently written receiver rule checks the compatibility bound.
        REQUIRE(topOfWindow <= legacyMaxAcceptableCommandCycle(legacyCycle, legacyBuffer));
    }
}


// -------------------------------------------------------------------------------------------
// The budget itself
// -------------------------------------------------------------------------------------------

TEST_CASE("CommandBufferPolicy: the relay budget is the sum of its named terms",
          "[network][relay][buffer]") {
    // A round trip in the middle of the clamped range, so neither clamp is what is being read.
    const Uint32 rtt = 300;
    const Uint32 expected = CommandBufferPolicy::kEmissionIntervalMs    // peer's emission wait
                          + CommandBufferPolicy::kRelayHoldMs           // the gateway's hold on an empty exchange
                          + (rtt + rtt / 2)                             // our poll period + response leg
                          + (rtt + rtt / 2);                            // the peer's, assumed like ours
    REQUIRE(CommandBufferPolicy::relayDeliveryBudgetMs(rtt) == expected);

    // Never decreasing in the measurement: a worse hop may not buy a smaller window.
    Uint32 previous = 0;
    for(Uint32 sample = 0; sample <= 2000; sample += 25) {
        const Uint32 budget = CommandBufferPolicy::relayDeliveryBudgetMs(sample);
        REQUIRE(budget >= previous);
        previous = budget;
    }
}

TEST_CASE("CommandBufferPolicy: the budget is clamped at both ends",
          "[network][relay][buffer]") {
    SECTION("no sample yet still buys a usable window") {
        // A lobby answered quickly can reach the match before the first HeartbeatAck. The
        // floor is what makes that case a slightly conservative match instead of a ten-cycle
        // one, which is the sizing the stutter was measured on.
        REQUIRE(CommandBufferPolicy::relayDeliveryBudgetMs(0)
                == CommandBufferPolicy::kMinRelayBudgetMs);
        REQUIRE(CommandBufferPolicy::relayCommandBufferCycles(0, GAMESPEED_DEFAULT)
                > legacyRelayBufferCycles(0));
    }

    SECTION("an absurd sample cannot put seconds of lag on every click") {
        REQUIRE(CommandBufferPolicy::relayDeliveryBudgetMs(5000)
                == CommandBufferPolicy::kMaxRelayBudgetMs);
        // Including one that would wrap the arithmetic if the input were not clamped first.
        REQUIRE(CommandBufferPolicy::relayDeliveryBudgetMs(0xFFFFFFFFu)
                == CommandBufferPolicy::kMaxRelayBudgetMs);
    }

    SECTION("the cycle count is bounded whatever the game speed") {
        for(int speed = GAMESPEED_MIN; speed <= GAMESPEED_MAX; speed++) {
            const Uint32 cycles = CommandBufferPolicy::relayCommandBufferCycles(0xFFFFFFFFu, speed);
            REQUIRE(cycles <= CommandBufferPolicy::kMaxRelayBufferCycles);
            REQUIRE(cycles >= CommandBufferPolicy::kMinBufferCycles);
        }
        // A nonsensical speed is treated as the default rather than divided by.
        REQUIRE(CommandBufferPolicy::relayCommandBufferCycles(300, 0)
                == CommandBufferPolicy::relayCommandBufferCycles(300, GAMESPEED_DEFAULT));
    }

    SECTION("a speed outside the settable range is pulled back into it, not divided by") {
        // Zero and negative are the only values that cannot be a duration at all; everything
        // else is clamped, so a peer sending 1 is treated as the fastest real setting rather
        // than inflating the window. Nothing here may divide by zero.
        REQUIRE(CommandBufferPolicy::sanitizedGameSpeedMs(0) == GAMESPEED_DEFAULT);
        REQUIRE(CommandBufferPolicy::sanitizedGameSpeedMs(-1) == GAMESPEED_DEFAULT);
        REQUIRE(CommandBufferPolicy::sanitizedGameSpeedMs(GAMESPEED_MIN - 1)
                == static_cast<Uint32>(GAMESPEED_MIN));
        REQUIRE(CommandBufferPolicy::sanitizedGameSpeedMs(GAMESPEED_MIN)
                == static_cast<Uint32>(GAMESPEED_MIN));
        REQUIRE(CommandBufferPolicy::sanitizedGameSpeedMs(GAMESPEED_MIN + 1) == 3u);
        REQUIRE(CommandBufferPolicy::sanitizedGameSpeedMs(GAMESPEED_MAX + 1)
                == static_cast<Uint32>(GAMESPEED_MAX));
    }
}

TEST_CASE("CommandBufferPolicy: the fastest settings are latency-limited, not mis-sized",
          "[network][relay][buffer]") {
    // The receiver window caps the allowance in *cycles*, so the faster the match the less
    // wall-clock latency that same allowance covers. This is the documented capacity limit of
    // an HTTP-polled relay match at the two fastest settings: the pair runs below its
    // configured cycle rate, waiting on the lockstep rule, rather than running incorrectly.
    // Nothing about it is new - the cap already bound at 4ms - and the policy is unchanged.
    const Uint32 cover = CommandBufferPolicy::kMaxRelayBufferCycles * GAMESPEED_MIN;
    REQUIRE(cover < CommandBufferPolicy::kMinRelayBudgetMs);
    REQUIRE(CommandBufferPolicy::relayCommandBufferCycles(0, GAMESPEED_MIN)
            == CommandBufferPolicy::kMaxRelayBufferCycles);

    // At the default speed the floor budget still fits inside the cap, which is why the
    // limit is a property of the fast settings and not of the policy.
    REQUIRE(CommandBufferPolicy::relayCommandBufferCycles(0, GAMESPEED_DEFAULT)
            < CommandBufferPolicy::kMaxRelayBufferCycles);
}

TEST_CASE("CommandBufferPolicy: milliseconds become cycles of this match, rounded up",
          "[network][relay][buffer]") {
    // MILLI2CYCLES() divides by GAMESPEED_DEFAULT whatever the match is set to. A latency
    // budget converted that way is eight times too small at GAMESPEED_MIN, where the match runs
    // eight times as many cycles per second.
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(1600, 16) == 70); // cycle cap
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(1600, 32) == 50);
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(160, 4) == 40);

    // The two fastest settings convert the same wall-clock budget into proportionally more
    // cycles, up to the cap. 120ms is 40 cycles at 3ms and 60 at 2ms; the floor and the
    // rounding still apply at both.
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(120, 3) == 40);
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(120, 2) == 60);
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(121, 3) == 41);  // rounded up
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(8, 2) == CommandBufferPolicy::kMinBufferCycles);
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(141, 2)
            == CommandBufferPolicy::kMaxRelayBufferCycles);

    // Rounded up: a budget that is not a whole number of cycles must not be shortened.
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(17, 16) == 5);   // floor applies
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(161, 16) == 11);

    // Saturating: a near-UINT32_MAX budget must not wrap round to the floor.
    REQUIRE(CommandBufferPolicy::cyclesForMilliseconds(0xFFFFFFFFu, 16)
            == CommandBufferPolicy::kMaxRelayBufferCycles);
}

// -------------------------------------------------------------------------------------------
// What the sizing does to the simulation rate
// -------------------------------------------------------------------------------------------

TEST_CASE("Lockstep: the old sizing reproduces the reported 30-40 cycles a second",
          "[network][relay][buffer][lockstep]") {
    // The public profiles: a native peer on 34 cycles against a browser host on 28, over a
    // relay whose HeartbeatAck round trip measured 380-467 ms. Those buffers are what
    // MILLI2CYCLES(roundTripTimeMs()) + 5 produces from exactly those measurements, which is
    // the check that this really is the old sizing and not a number chosen to fail.
    REQUIRE(legacyRelayBufferCycles(467) == 34u);
    REQUIRE(legacyRelayBufferCycles(380) == 28u);

    LockstepPair pair(34u, 28u, DeliveryPath(), GAMESPEED_DEFAULT);
    pair.run(60000);

    const double configured = configuredRate(GAMESPEED_DEFAULT);
    REQUIRE(configured == Catch::Approx(62.5));

    // The reported symptom, reproduced: well under the configured rate, and the peers stall.
    CHECK(pair.slowestCyclesPerSecond() < 45.0);
    CHECK(pair.slowestCyclesPerSecond() > 25.0);
    CHECK(pair.waitFramesOf(0) > 0);
    CHECK(pair.waitFramesOf(1) > 0);
}

TEST_CASE("Lockstep: the budgeted sizing reaches the configured rate",
          "[network][relay][buffer][lockstep]") {
    const DeliveryPath path;
    const double configured = configuredRate(GAMESPEED_DEFAULT);

    // The heartbeat rides the same exchanges as everything else, so on this path it measures
    // about the two legs: 400 ms. The reported 380-467 ms is that, and 900 ms is the top of
    // the range the polling relay has been seen at.
    const Uint32 pathRoundTripMs = path.uplinkMs + path.downlinkMs;

    SECTION("a hop that was measured") {
        for(Uint32 relayRoundTripMs : {400u, 467u, 900u}) {
            const Uint32 buffer = CommandBufferPolicy::relayCommandBufferCycles(
                relayRoundTripMs, GAMESPEED_DEFAULT);
            INFO("relay hop " << relayRoundTripMs << " ms, buffer " << buffer << " cycles");

            LockstepPair pair(buffer, buffer, path, GAMESPEED_DEFAULT);
            pair.run(60000);
            CHECK(pair.slowestCyclesPerSecond() >= configured * 0.97);
        }

        // The margin has a reason: the budget covers the worst one-way delay of this path, not
        // its average. That is what turns "fast most of the time" into "does not stall on the
        // poll that lands badly".
        REQUIRE(CommandBufferPolicy::relayDeliveryBudgetMs(pathRoundTripMs)
                >= path.worstCaseOneWayMs());
    }

    SECTION("a hop that was under-measured degrades gently") {
        // The heartbeat is a small frame on a quiet session and can come back faster than a
        // command ever will. Half the real hop still runs the match at over nine tenths of its
        // rate, because the budget's other terms do not depend on the measurement.
        const Uint32 buffer =
            CommandBufferPolicy::relayCommandBufferCycles(pathRoundTripMs / 2, GAMESPEED_DEFAULT);
        LockstepPair pair(buffer, buffer, path, GAMESPEED_DEFAULT);
        pair.run(60000);
        CHECK(pair.slowestCyclesPerSecond() >= configured * 0.90);
    }

    SECTION("no sample at all is still far better than what it replaces") {
        // A lobby answered before the first HeartbeatAck. The old code fell through to the
        // ten-cycle internet minimum here, which is the worst sizing in the whole range.
        const Uint32 buffer =
            CommandBufferPolicy::relayCommandBufferCycles(0, GAMESPEED_DEFAULT);
        LockstepPair budgeted(buffer, buffer, path, GAMESPEED_DEFAULT);
        budgeted.run(60000);

        const Uint32 legacyBuffer = legacyRelayBufferCycles(0);
        REQUIRE(legacyBuffer == 10u);
        LockstepPair legacy(legacyBuffer, legacyBuffer, path, GAMESPEED_DEFAULT);
        legacy.run(60000);

        CHECK(budgeted.slowestCyclesPerSecond() >= configured * 0.75);
        CHECK(budgeted.slowestCyclesPerSecond() > legacy.slowestCyclesPerSecond() * 2.0);
    }
}

TEST_CASE("Lockstep: peers that size their buffers differently still run",
          "[network][relay][buffer][lockstep]") {
    // Buffers are local decisions made from local measurements, so a pair is normally
    // mismatched: the browser host and the native client never see the same round trip. The
    // window that matters is the sum of the two, so a well-sized peer carries a badly-sized
    // one rather than being dragged down to its rate.
    const DeliveryPath path;
    const Uint32 budgeted =
        CommandBufferPolicy::relayCommandBufferCycles(467, GAMESPEED_DEFAULT);

    LockstepPair mixed(budgeted, legacyRelayBufferCycles(380), path, GAMESPEED_DEFAULT);
    mixed.run(60000);

    LockstepPair legacy(legacyRelayBufferCycles(467), legacyRelayBufferCycles(380), path,
                        GAMESPEED_DEFAULT);
    legacy.run(60000);

    // Most of the loss is recovered - 0.91 of the configured rate against 0.63 - but not all
    // of it, because the sum of the two windows is what the rate is made of and one of them is
    // still the old number. Both peers updating is worth another tenth.
    CHECK(mixed.slowestCyclesPerSecond() > legacy.slowestCyclesPerSecond() * 1.3);
    CHECK(mixed.slowestCyclesPerSecond() >= configuredRate(GAMESPEED_DEFAULT) * 0.88);

    // Neither peer ever runs past what it has been told about, whatever the two buffers are:
    // the rate changes, the rule does not.
    CHECK(mixed.cycleOf(0) > 0);
    CHECK(mixed.cycleOf(1) > 0);
}

TEST_CASE("Lockstep: a slower relay degrades the rate instead of breaking the match",
          "[network][relay][buffer][lockstep]") {
    // Past the ceiling the window stops growing, so a very slow relay simply runs the match
    // below full speed. That is the accepted outcome: the model preserves the wait rule, while an uncapped allowance would add unbounded input lag.
    DeliveryPath slow;
    slow.uplinkMs     = 600u;
    slow.downlinkMs   = 600u;
    slow.pollPeriodMs = 1400u;

    const Uint32 buffer = CommandBufferPolicy::relayCommandBufferCycles(1200, GAMESPEED_DEFAULT);
    REQUIRE(buffer == CommandBufferPolicy::kMaxRelayBufferCycles);

    LockstepPair pair(buffer, buffer, slow, GAMESPEED_DEFAULT);
    pair.run(60000);

    CHECK(pair.slowestCyclesPerSecond() > 0.0);
    CHECK(pair.slowestCyclesPerSecond() < configuredRate(GAMESPEED_DEFAULT));
}

TEST_CASE("Lockstep: the ceiling is deliberately below the old sizing on a very slow hop",
          "[network][relay][buffer][compatibility]") {
    // Recorded because it is a knowing trade, not an oversight. The old formula was
    // `rtt + 80 ms` of window with no ceiling, so above a hop of about 1.04 s it asks for a
    // larger buffer than kMaxRelayBufferCycles allows, and on such a hop it would run faster.
    // The ceiling is kept anyway: it is what keeps our packets inside the window a peer on the
    // previous build will accept, and being refused by that peer stalls the match outright
    // rather than slowing it - see the compatibility test above. A hop that slow is close to
    // unplayable on input lag either way.
    const Uint32 slowHopMs = 1200;
    CHECK(CommandBufferPolicy::relayCommandBufferCycles(slowHopMs, GAMESPEED_DEFAULT)
          < legacyRelayBufferCycles(slowHopMs));

    // The crossover, so a change to either side shows up here rather than in the field.
    const Uint32 crossoverMs = 1040;
    CHECK(CommandBufferPolicy::relayCommandBufferCycles(crossoverMs - 16, GAMESPEED_DEFAULT)
          >= legacyRelayBufferCycles(crossoverMs - 16));
}


// -------------------------------------------------------------------------------------------
// The bounds a larger buffer must not loosen
// -------------------------------------------------------------------------------------------

TEST_CASE("Lockstep: every relay allowance fits the unchanged receiver window",
          "[network][relay][buffer][security]") {
    const Uint32 currentCycle = 100000;
    for(Uint32 receiverBuffer = CommandBufferPolicy::kMinBufferCycles;
        receiverBuffer <= CommandBufferPolicy::kMaxRelayBufferCycles; ++receiverBuffer) {
        const Uint32 peerCycle = currentCycle + receiverBuffer - 1;
        for(Uint32 senderBuffer = CommandBufferPolicy::kMinBufferCycles;
            senderBuffer <= CommandBufferPolicy::kMaxRelayBufferCycles; ++senderBuffer) {
            const Uint32 lastEntry = peerCycle + senderBuffer - 1;
            REQUIRE(CommandValidation::isAcceptableCommandCycle(lastEntry, currentCycle, receiverBuffer));
        }
        const Uint32 bound = 2u * receiverBuffer + CommandValidation::kCycleWindowSlack;
        REQUIRE(CommandValidation::maxAcceptableCommandCycle(currentCycle, receiverBuffer)
                == currentCycle + bound);
        REQUIRE_FALSE(CommandValidation::isAcceptableCommandCycle(currentCycle + bound + 1,
                                                                  currentCycle, receiverBuffer));
    }
}

TEST_CASE("Lockstep: the emission cadence is unchanged by the larger window",
          "[network][relay][buffer][emission]") {
    // The buffer decides how far ahead a window ends, not how often one is sent. Ten a second
    // is what keeps a polling session inside the relay's drain rate, and that is the property
    // CommandEmissionTestCase covers; this only asserts the two are independent.
    CommandEmissionSchedule schedule;
    const Uint32 buffer = CommandBufferPolicy::relayCommandBufferCycles(467, GAMESPEED_DEFAULT);

    REQUIRE(schedule.shouldEmit(1000, 0));
    schedule.noteEmission(1000, 0 + buffer);
    CHECK_FALSE(schedule.shouldEmit(1099, 1));
    CHECK(schedule.shouldEmit(1100, 1));

    // Successive windows stay contiguous: the next one starts no later than where the last
    // one ended, so the retention rule never has to skip a cycle to keep up with the buffer.
    const Uint32 nextCycle = 6;     // ~100 ms of simulation later
    CHECK(CommandEmissionSchedule::historyStartCycle(nextCycle) <= buffer);
}
