#include <catch2/catch_test_macros.hpp>

#include <players/ArmyPosturePolicy.h>
#include <players/CombatPowerPolicy.h>
#include <players/FrontBatteryPolicy.h>
#include <players/QuantBotConfig.h>
#include <players/SpecialUnitPolicy.h>

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

using ArmyPosturePolicy::Posture;

namespace {

ArmyPosturePolicy::Thresholds defaults() {
    ArmyPosturePolicy::Thresholds t;
    t.windowCycles = 5625;             // 90 s at 16 ms per cycle
    t.sampleStrideCycles = t.windowCycles / 8;
    t.lossShareBps = 2000;
    t.tradeShareBps = 6500;
    t.lossFloor = 1500;
    t.localWithdrawBps = 13000;
    t.localSevereBps = 20000;
    t.localPersistCycles = 562;
    t.resumeAssembledBps = 8000;
    t.resumeAdvantageBps = 12000;
    t.stabiliseCycles = 1562;
    t.minWithdrawCycles = 937;
    t.maxWithdrawCycles = 4687;
    t.maxRecoverCycles = 11250;
    t.outnumberedBps = 15000;
    t.dispatchBypassBps = 8000;
    return t;
}

/// A situation that is losing badly on every axis, so each test can switch one
/// condition off and show that the trigger really needs all of them.
ArmyPosturePolicy::Situation collapsing() {
    ArmyPosturePolicy::Situation s;
    s.cycle = 100000;
    s.windowCovered = true;
    s.windowStartDeployable = 40000;
    s.windowLoss = 20000;   // 50% of the deployed force
    s.windowKill = 2000;    // 10% of our own losses: a bad trade
    s.readinessWorsening = true;
    s.adverseMainFront = true;
    s.assembledPower = 0;
    s.designatedPower = 10000;
    s.frontFriendly = 10000;
    s.frontHostile = 40000;
    s.frontObserved = true;
    return s;
}

/// Minimal in-memory stream pair with the method names the engine streams use,
/// so the ledger's own save/load can be exercised without the game.
struct Out {
    std::vector<uint8_t> bytes;
    void push(uint64_t value, int width) {
        for (int i = 0; i < width; ++i) bytes.push_back(uint8_t((value >> (8 * i)) & 0xFF));
    }
    void writeUint32(uint32_t v) { push(v, 4); }
    void writeSint64(int64_t v) { push(uint64_t(v), 8); }
};
struct In {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    uint64_t pull(int width) {
        uint64_t value = 0;
        for (int i = 0; i < width; ++i) {
            if (at >= bytes.size()) throw std::runtime_error("eof");
            value |= uint64_t(bytes[at++]) << (8 * i);
        }
        return value;
    }
    uint32_t readUint32() { return uint32_t(pull(4)); }
    int64_t readSint64() { return int64_t(pull(8)); }
};

} // namespace

TEST_CASE("Army posture: a collapsing main force withdraws", "[ai][posture]") {
    const auto t = defaults();
    const auto decision = ArmyPosturePolicy::evaluate(Posture::Offensive, 0, collapsing(), t);
    REQUIRE(decision.changed);
    REQUIRE(decision.posture == Posture::Withdrawing);
    REQUIRE(std::string(decision.reason) == "sustained_main_force_attrition");
    REQUIRE_FALSE(decision.emergency);
}

TEST_CASE("Army posture: every attrition condition is required", "[ai][posture]") {
    const auto t = defaults();
    SECTION("an immature window never triggers") {
        auto s = collapsing(); s.windowCovered = false;
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Offensive, 0, s, t).changed);
    }
    SECTION("a minor raid below the material floor never triggers") {
        auto s = collapsing();
        s.windowStartDeployable = 2000; s.windowLoss = 1400; s.windowKill = 0;
        REQUIRE(s.windowLoss * 10000 >= int64_t(t.lossShareBps) * s.windowStartDeployable);
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Offensive, 0, s, t).changed);
    }
    SECTION("a small share of a large force never triggers") {
        auto s = collapsing(); s.windowLoss = 4000; // 10% of 40000
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Offensive, 0, s, t).changed);
    }
    SECTION("a good trade never triggers") {
        auto s = collapsing(); s.windowKill = 19000; // 95% of our own losses
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Offensive, 0, s, t).changed);
    }
    SECTION("losses alone, with no corroboration, never trigger") {
        auto s = collapsing();
        s.readinessWorsening = false; s.adverseMainFront = false;
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Offensive, 0, s, t).changed);
    }
    SECTION("either corroborating signal is enough once the rest holds") {
        auto a = collapsing(); a.adverseMainFront = false;
        auto b = collapsing(); b.readinessWorsening = false;
        REQUIRE(ArmyPosturePolicy::evaluate(Posture::Offensive, 0, a, t).changed);
        REQUIRE(ArmyPosturePolicy::evaluate(Posture::Offensive, 0, b, t).changed);
    }
}

TEST_CASE("Army posture: an emergency is never gated by dwell or window maturity",
          "[ai][posture]") {
    const auto t = defaults();
    ArmyPosturePolicy::Situation s;
    s.cycle = 10;                 // One cycle of posture age: no dwell served.
    s.windowCovered = false;      // No attrition evidence at all.
    s.designatedPower = 10000;
    SECTION("a severe local defeat withdraws at once") {
        s.severeLocalDefeat = true;
        const auto decision = ArmyPosturePolicy::evaluate(Posture::Offensive, 9, s, t);
        REQUIRE(decision.changed);
        REQUIRE(decision.emergency);
        REQUIRE(decision.posture == Posture::Withdrawing);
    }
    SECTION("an attack on a core asset withdraws at once") {
        s.coreUnderAttack = true;
        const auto decision = ArmyPosturePolicy::evaluate(Posture::Offensive, 9, s, t);
        REQUIRE(decision.changed);
        REQUIRE(decision.emergency);
    }
    SECTION("an emergency during recovery is reported, not suppressed") {
        s.coreUnderAttack = true;
        const auto decision = ArmyPosturePolicy::evaluate(Posture::Recovering, 9, s, t);
        REQUIRE(decision.emergency);
        REQUIRE(decision.posture == Posture::Recovering);
    }
}

TEST_CASE("Army posture: withdrawal ends on assembly or on its bounded fallback",
          "[ai][posture]") {
    const auto t = defaults();
    ArmyPosturePolicy::Situation s;
    // Well past the fallback window, so the unsigned age arithmetic below is
    // measuring a real elapsed time rather than clamping at zero.
    s.cycle = 100000;
    s.designatedPower = 10000;
    SECTION("assembled at the rally, once the dwell has been served") {
        s.assembledPower = 8000; // Exactly the 80% share.
        const auto decision = ArmyPosturePolicy::evaluate(Posture::Withdrawing,
            s.cycle - t.minWithdrawCycles, s, t);
        REQUIRE(decision.posture == Posture::Recovering);
        REQUIRE(std::string(decision.reason) == "withdrawal_assembled");
    }
    SECTION("a trapped remnant does not hold the house in place") {
        s.assembledPower = 0;
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Withdrawing,
            s.cycle - t.minWithdrawCycles, s, t).changed);
        const auto timeout = ArmyPosturePolicy::evaluate(Posture::Withdrawing,
            s.cycle - t.maxWithdrawCycles, s, t);
        REQUIRE(timeout.posture == Posture::Recovering);
        REQUIRE(std::string(timeout.reason) == "withdrawal_timeout");
    }
}

TEST_CASE("Army posture: recovery resumes only when stable, assembled and favourable",
          "[ai][posture]") {
    const auto t = defaults();
    ArmyPosturePolicy::Situation s;
    s.cycle = 100000;
    s.designatedPower = 10000;
    s.assembledPower = 9000;
    s.frontObserved = true;
    s.frontFriendly = 15000;
    s.frontHostile = 10000; // 1.5x advantage, above the 1.2x requirement
    // Genuinely quiet: no material loss inside the stabilisation window.
    s.quietCycles = t.stabiliseCycles * 2;
    SECTION("stable, assembled and ahead resumes") {
        const auto decision = ArmyPosturePolicy::evaluate(Posture::Recovering,
            s.cycle - t.stabiliseCycles, s, t);
        REQUIRE(decision.posture == Posture::Offensive);
        REQUIRE(std::string(decision.reason) == "recovered_with_front_advantage");
    }
    SECTION("not yet stabilised holds") {
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Recovering, s.cycle - 10, s, t).changed);
    }
    SECTION("not assembled holds") {
        auto partial = s; partial.assembledPower = 1000;
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Recovering,
            s.cycle - t.stabiliseCycles, partial, t).changed);
    }
    SECTION("an unknown front needs scouting, but does not freeze the house") {
        auto blind = s; blind.frontObserved = false;
        REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Recovering,
            s.cycle - t.stabiliseCycles, blind, t).changed);
        const auto fallback = ArmyPosturePolicy::evaluate(Posture::Recovering,
            s.cycle - t.maxRecoverCycles, blind, t);
        REQUIRE(fallback.posture == Posture::Offensive);
        REQUIRE(std::string(fallback.reason) == "recovery_timeout_assembled");
    }
    SECTION("an unassembled remnant still resumes on the outer bound") {
        auto stuck = s; stuck.frontObserved = false; stuck.assembledPower = 0;
        const auto fallback = ArmyPosturePolicy::evaluate(Posture::Recovering,
            s.cycle - 2 * t.maxRecoverCycles, stuck, t);
        REQUIRE(fallback.posture == Posture::Offensive);
        REQUIRE(std::string(fallback.reason) == "recovery_timeout_bounded");
    }
}

TEST_CASE("Army posture: local verdict needs persistence unless it is severe",
          "[ai][posture]") {
    const auto t = defaults();
    SECTION("a 1.4x disadvantage waits for persistence") {
        REQUIRE_FALSE(ArmyPosturePolicy::localVerdict(10000, 14000, 0, t).withdraw);
        REQUIRE(ArmyPosturePolicy::localVerdict(10000, 14000, t.localPersistCycles, t).withdraw);
        REQUIRE_FALSE(ArmyPosturePolicy::localVerdict(10000, 14000, t.localPersistCycles, t).severe);
    }
    SECTION("a 2x disadvantage withdraws immediately") {
        const auto verdict = ArmyPosturePolicy::localVerdict(10000, 20000, 0, t);
        REQUIRE(verdict.withdraw);
        REQUIRE(verdict.severe);
    }
    SECTION("no observed enemy is not a disadvantage") {
        REQUIRE_FALSE(ArmyPosturePolicy::localVerdict(0, 0, 100000, t).withdraw);
    }
}

TEST_CASE("Army posture: outnumbered dispatch gate and the 80 percent bypass",
          "[ai][posture][dispatch]") {
    const auto t = defaults();
    const int limit = 80000;
    REQUIRE(ArmyPosturePolicy::dispatchBypassValue(limit, t) == 64000);
    SECTION("a weak enemy never defers") {
        const auto gate = ArmyPosturePolicy::dispatchGate(10000, 5000, limit, 1000, t);
        REQUIRE_FALSE(gate.outnumbered);
        REQUIRE_FALSE(gate.defer);
    }
    SECTION("exactly 1.5x is substantially outnumbered") {
        REQUIRE(ArmyPosturePolicy::dispatchGate(10000, 15000, limit, 1000, t).outnumbered);
        REQUIRE_FALSE(ArmyPosturePolicy::dispatchGate(10000, 14999, limit, 1000, t).outnumbered);
    }
    SECTION("the boundary is at exactly 80 percent of the configured limit") {
        // 79.9 percent defers, 80 percent and 80.1 percent dispatch.
        REQUIRE(ArmyPosturePolicy::dispatchGate(10000, 60000, limit, 63920, t).defer);
        REQUIRE_FALSE(ArmyPosturePolicy::dispatchGate(10000, 60000, limit, 64000, t).defer);
        REQUIRE_FALSE(ArmyPosturePolicy::dispatchGate(10000, 60000, limit, 64080, t).defer);
        const auto bypass = ArmyPosturePolicy::dispatchGate(10000, 60000, limit, 64000, t);
        REQUIRE(bypass.outnumbered);
        REQUIRE(bypass.bypass);
        REQUIRE(std::string(bypass.reason) == "value_threshold_reached");
    }
    SECTION("a vastly stronger enemy defers below the threshold") {
        const auto gate = ArmyPosturePolicy::dispatchGate(5000, 200000, limit, 5000, t);
        REQUIRE(gate.outnumbered);
        REQUIRE(gate.defer);
        REQUIRE(std::string(gate.reason) == "outnumbered_below_value_threshold");
    }
    SECTION("below the limit but with good local odds still dispatches") {
        const auto gate = ArmyPosturePolicy::dispatchGate(30000, 20000, limit, 20000, t);
        REQUIRE_FALSE(gate.outnumbered);
        REQUIRE_FALSE(gate.defer);
        REQUIRE(std::string(gate.reason) == "not_outnumbered");
    }
    SECTION("no observed enemy is not an advantage claim either way") {
        const auto gate = ArmyPosturePolicy::dispatchGate(0, 0, limit, 0, t);
        REQUIRE_FALSE(gate.outnumbered);
        REQUIRE_FALSE(gate.defer);
    }
    SECTION("an empty army facing a real enemy is outnumbered") {
        REQUIRE(ArmyPosturePolicy::dispatchGate(0, 100, limit, 0, t).outnumbered);
    }
    SECTION("a nonpositive configured limit refuses the bypass explicitly") {
        for (const int degenerate : {0, -1, -80000}) {
            const auto gate = ArmyPosturePolicy::dispatchGate(5000, 200000, degenerate, 999999, t);
            REQUIRE(gate.outnumbered);
            REQUIRE_FALSE(gate.bypass);
            REQUIRE(gate.defer);
            REQUIRE(std::string(gate.reason) == "outnumbered_no_configured_limit");
            REQUIRE(ArmyPosturePolicy::dispatchBypassValue(degenerate, t) == -1);
            // And it is not a permanent block: parity still dispatches.
            REQUIRE_FALSE(ArmyPosturePolicy::dispatchGate(5000, 5000, degenerate, 0, t).defer);
        }
    }
}

TEST_CASE("Army posture: attrition ledger windows, strides and bounds", "[ai][posture]") {
    const auto t = defaults();
    ArmyPosturePolicy::AttritionLedger ledger;
    REQUIRE(ledger.windowStart(0, t.windowCycles) == nullptr);
    // One sample per stride, and no second sample inside the same stride.
    REQUIRE(ledger.sample(0, 40000, t.sampleStrideCycles));
    REQUIRE_FALSE(ledger.sample(1, 40000, t.sampleStrideCycles));
    // A young house has no covered window, so nothing can trigger yet.
    REQUIRE(ledger.windowStart(1000, t.windowCycles) == nullptr);
    for (uint32_t cycle = t.sampleStrideCycles; cycle <= t.windowCycles * 2;
         cycle += t.sampleStrideCycles) {
        ledger.lostCost += 1000;
        ledger.killCost += 100;
        ledger.sample(cycle, 40000, t.sampleStrideCycles);
    }
    const auto* start = ledger.windowStart(t.windowCycles * 2, t.windowCycles);
    REQUIRE(start != nullptr);
    REQUIRE(t.windowCycles * 2 - start->cycle >= t.windowCycles);
    // The ring must actually have wrapped, or the round-trip below would only
    // be testing the easy unwrapped case.
    REQUIRE(ledger.count == ArmyPosturePolicy::kAttritionSamples);
    REQUIRE(ledger.next != 0);

    SECTION("a wrapped ring serialises canonically and re-serialises identically") {
        Out out; ledger.save(out);
        In in{out.bytes};
        ArmyPosturePolicy::AttritionLedger restored;
        restored.load(in, [] { throw std::runtime_error("unexpected invalid ledger"); });
        REQUIRE(restored.lostCost == ledger.lostCost);
        REQUIRE(restored.killCost == ledger.killCost);
        REQUIRE(restored.count == ledger.count);
        // Canonical physical layout: oldest at slot zero, cursor derived.
        REQUIRE(restored.next == restored.count % ArmyPosturePolicy::kAttritionSamples);
        for (uint32_t i = 0; i < ledger.count; ++i) {
            REQUIRE(restored.at(i).cycle == ledger.at(i).cycle);
            REQUIRE(restored.at(i).lostCost == ledger.at(i).lostCost);
            REQUIRE(restored.at(i).killCost == ledger.at(i).killCost);
            REQUIRE(restored.at(i).deployable == ledger.at(i).deployable);
        }
        Out again; restored.save(again);
        REQUIRE(again.bytes == out.bytes);
        // And a second generation is still identical, which is what an observer
        // checkpoint taken from a restored peer actually does.
        In second{again.bytes};
        ArmyPosturePolicy::AttritionLedger twice;
        twice.load(second, [] { throw std::runtime_error("unexpected invalid ledger"); });
        Out third; twice.save(third);
        REQUIRE(third.bytes == out.bytes);
        // The window answer has to survive the trip too, not just the bytes.
        const auto* before = ledger.windowStart(t.windowCycles * 2, t.windowCycles);
        const auto* after = restored.windowStart(t.windowCycles * 2, t.windowCycles);
        REQUIRE(after != nullptr);
        REQUIRE(after->cycle == before->cycle);
        REQUIRE(after->lostCost == before->lostCost);
        REQUIRE(after->deployable == before->deployable);
    }
    SECTION("an impossible sample count is rejected") {
        Out out;
        out.writeSint64(0); out.writeSint64(0);
        out.writeUint32(ArmyPosturePolicy::kAttritionSampleLimit + 1);
        In in{out.bytes};
        ArmyPosturePolicy::AttritionLedger restored;
        bool rejected = false;
        restored.load(in, [&] { rejected = true; });
        REQUIRE(rejected);
        REQUIRE(restored.count == 0);
    }
}

TEST_CASE("Army posture: posture ids are validated on load", "[ai][posture]") {
    REQUIRE(ArmyPosturePolicy::validPosture(0));
    REQUIRE(ArmyPosturePolicy::validPosture(2));
    REQUIRE_FALSE(ArmyPosturePolicy::validPosture(3));
    REQUIRE_FALSE(ArmyPosturePolicy::validPosture(255));
}

TEST_CASE("Combat power: hit-point weighting, floors and class separation",
          "[ai][combatpower]") {
    // Full health is the full price; a wreck keeps the documented floor.
    REQUIRE(CombatPowerPolicy::unitPower(600, 600, 600) == 600 * 1000);
    REQUIRE(CombatPowerPolicy::unitPower(60, 100, 100) == 60 * 1000);
    REQUIRE(CombatPowerPolicy::unitPower(600, 300, 600) == 300 * 1000);
    REQUIRE(CombatPowerPolicy::unitPower(600, 1, 600) == 600 * 1000 * 2500 / 10000);
    // A priceless summoned unit still occupies the field, for TACTICAL power.
    REQUIRE(CombatPowerPolicy::unitPower(0, 100, 100) == 100 * 1000);

    // Credit value uses the ACTUAL price with no floor, because the configured
    // militaryValueLimit it is compared against is a sum of actual prices. A
    // sixty-credit Soldier that counted a hundred would make the eighty percent
    // share be measured in a different currency than the limit.
    REQUIRE(CombatPowerPolicy::unitValue(60, 100, 100) == 60);
    REQUIRE(CombatPowerPolicy::unitValue(60, 50, 100) == 30);
    REQUIRE(CombatPowerPolicy::unitValue(600, 300, 600) == 300);
    REQUIRE(CombatPowerPolicy::unitValue(0, 100, 100) == 0);
    // The cheapest real units in the game must all report their own price.
    for (const int price : {10, 60, 70, 100, 150}) {
        REQUIRE(CombatPowerPolicy::unitValue(price, 100, 100) == price);
    }

    // Air capability follows the engine's own canAttack overrides.
    REQUIRE(CombatPowerPolicy::airCapableItem(Unit_Launcher));
    REQUIRE(CombatPowerPolicy::airCapableItem(Unit_Trooper));
    REQUIRE(CombatPowerPolicy::airCapableItem(Unit_Deviator));
    REQUIRE(CombatPowerPolicy::airCapableItem(Structure_RocketTurret));
    REQUIRE_FALSE(CombatPowerPolicy::airCapableItem(Unit_Tank));
    REQUIRE_FALSE(CombatPowerPolicy::airCapableItem(Unit_SonicTank));
    REQUIRE_FALSE(CombatPowerPolicy::airCapableItem(Unit_SiegeTank));
    REQUIRE_FALSE(CombatPowerPolicy::airCapableItem(Unit_Soldier));
    REQUIRE_FALSE(CombatPowerPolicy::airCapableItem(Unit_Ornithopter));
    REQUIRE_FALSE(CombatPowerPolicy::airCapableItem(Structure_GunTurret));

    CombatPowerPolicy::Force friendly, hostile;
    friendly.addGround(1000, false);
    hostile.addGround(1000, false);
    hostile.addAir(5000, false);
    // The ground dispatch veto compares ground against ground only.
    REQUIRE(CombatPowerPolicy::hostileGroundOnly(hostile) == 1000);
    // The recovery triggers count the aircraft, because an Ornithopter destroys
    // the ground army it strafes whether or not that army can shoot back.
    REQUIRE(CombatPowerPolicy::hostileThreatToGround(hostile) == 6000);
    REQUIRE_FALSE(CombatPowerPolicy::canAnswerAir(friendly));
    friendly.addGround(500, true);
    REQUIRE(CombatPowerPolicy::canAnswerAir(friendly));
    REQUIRE(CombatPowerPolicy::hostileThreatToGround(hostile) == 6000);
    // Static defence holds ground; it never joins the offensive figure.
    friendly.addDefence(9999, true);
    REQUIRE(friendly.offensive() == 1500);
    REQUIRE(friendly.holding() == 1500 + 9999);
}

TEST_CASE("Army posture: a resume needs genuine quiet, not merely an old state",
          "[ai][posture]") {
    const auto t = defaults();
    ArmyPosturePolicy::Situation s;
    s.cycle = 500000;
    s.designatedPower = 10000;
    s.assembledPower = 10000;
    s.frontObserved = true;
    s.frontFriendly = 20000;
    s.frontHostile = 1000;
    s.quietCycles = t.stabiliseCycles * 4;
    // Stable, assembled, ahead and quiet: resumes.
    REQUIRE(ArmyPosturePolicy::evaluate(Posture::Recovering,
        s.cycle - t.maxRecoverCycles * 3, s, t).posture == Posture::Offensive);
    SECTION("a fresh material loss blocks every resume path") {
        auto bleeding = s;
        bleeding.recentSeriousLoss = true;
        bleeding.quietCycles = 10;
        // Not the normal path, not the assembled timeout, not the outer bound.
        for (const uint32_t age : {t.stabiliseCycles, t.maxRecoverCycles,
                                   2 * t.maxRecoverCycles, 10 * t.maxRecoverCycles}) {
            const auto decision = ArmyPosturePolicy::evaluate(Posture::Recovering,
                bleeding.cycle - age, bleeding, t);
            REQUIRE_FALSE(decision.changed);
            REQUIRE(decision.posture == Posture::Recovering);
        }
    }
    SECTION("an old posture that is still not quiet does not count as stable") {
        auto noisy = s;
        noisy.quietCycles = t.stabiliseCycles / 2;
        const auto decision = ArmyPosturePolicy::evaluate(Posture::Recovering,
            noisy.cycle - t.stabiliseCycles * 2, noisy, t);
        // The ordinary path is refused; only the bounded fallback may fire.
        REQUIRE(std::string(decision.reason) != "recovered_with_front_advantage");
    }
}

TEST_CASE("Army posture: the withdrawal dwell is actually applied", "[ai][posture]") {
    const auto t = defaults();
    REQUIRE(t.minWithdrawCycles > 0);
    ArmyPosturePolicy::Situation s;
    s.cycle = 500000;
    s.designatedPower = 10000;
    s.assembledPower = 10000;   // Already gathered.
    // Inside the dwell the posture holds, so a wave that was near home when it
    // was recalled cannot flip straight back out on the next evaluation.
    REQUIRE_FALSE(ArmyPosturePolicy::evaluate(Posture::Withdrawing,
        s.cycle - (t.minWithdrawCycles - 1), s, t).changed);
    REQUIRE(ArmyPosturePolicy::evaluate(Posture::Withdrawing,
        s.cycle - t.minWithdrawCycles, s, t).posture == Posture::Recovering);
}

TEST_CASE("Front battery: demand-scaled goal, economy gate and geometry",
          "[ai][battery]") {
    SECTION("no battery before the economy that pays for it") {
        REQUIRE_FALSE(FrontBatteryPolicy::economyReady(1, 1, 1));
        REQUIRE_FALSE(FrontBatteryPolicy::economyReady(2, 0, 1));
        REQUIRE_FALSE(FrontBatteryPolicy::economyReady(2, 1, 0));
        REQUIRE(FrontBatteryPolicy::economyReady(2, 1, 1));
        REQUIRE(FrontBatteryPolicy::batteryGoal(30, false, 0) == 0);
        REQUIRE(FrontBatteryPolicy::batteryGoal(30, true, 0, false) == 0);
    }
    SECTION("a small base lands in the opening band") {
        // coverageTurretCap of a twelve-building base is around nine.
        const int goal = FrontBatteryPolicy::batteryGoal(9, true, 0);
        REQUIRE(goal >= FrontBatteryPolicy::kOpeningBandMin);
        REQUIRE(goal <= FrontBatteryPolicy::kOpeningBandMax);
    }
    SECTION("a tiny demand still gets a usable opening battery") {
        REQUIRE(FrontBatteryPolicy::batteryGoal(2, true, 0) == FrontBatteryPolicy::kOpeningBandMin);
    }
    SECTION("a mature base scales past twenty-four") {
        REQUIRE(FrontBatteryPolicy::batteryGoal(27, true, 0) == 27);
        REQUIRE(FrontBatteryPolicy::batteryGoal(27, true, 10000) > 27);
        REQUIRE(FrontBatteryPolicy::batteryGoal(27, true, 10000) <= 27 + 27 / 2 + 1);
    }
    SECTION("front share leaves cover for the flank and the rear") {
        REQUIRE(FrontBatteryPolicy::frontAllowance(24) == 16);
        REQUIRE(FrontBatteryPolicy::frontAllowance(24) < 24);
        REQUIRE(FrontBatteryPolicy::onFrontSide(5, 0, 1, 0));
        REQUIRE_FALSE(FrontBatteryPolicy::onFrontSide(-5, 0, 1, 0));
        REQUIRE_FALSE(FrontBatteryPolicy::onFrontSide(0, 5, 1, 0));
    }
    SECTION("spacing, density, mutual support and corridors") {
        REQUIRE(FrontBatteryPolicy::spacedEnough(-1));  // Nothing nearby yet.
        REQUIRE_FALSE(FrontBatteryPolicy::spacedEnough(1));
        REQUIRE(FrontBatteryPolicy::spacedEnough(2));
        REQUIRE(FrontBatteryPolicy::clusterRadius(7) == 3);
        REQUIRE(FrontBatteryPolicy::clusterRadius(1) == 2);
        REQUIRE(FrontBatteryPolicy::withinClusterLimit(5));
        REQUIRE_FALSE(FrontBatteryPolicy::withinClusterLimit(FrontBatteryPolicy::kMaxCluster));
        REQUIRE(FrontBatteryPolicy::mutuallySupported(0, false));
        REQUIRE(FrontBatteryPolicy::mutuallySupported(1, false));
        REQUIRE_FALSE(FrontBatteryPolicy::mutuallySupported(2, false));
        REQUIRE(FrontBatteryPolicy::mutuallySupported(9, true));
        REQUIRE_FALSE(FrontBatteryPolicy::keepsCorridor(2));
        REQUIRE(FrontBatteryPolicy::keepsCorridor(3));
        REQUIRE(FrontBatteryPolicy::mayOrderThisPass(0));
        REQUIRE_FALSE(FrontBatteryPolicy::mayOrderThisPass(1));
    }
}

TEST_CASE("Army recovery config: every new knob is hashed, without aliasing",
          "[ai][posture][config]") {
    const QuantBotConfig baseline;
    const std::string base = baseline.getConfigHash();

    // Every recovery field has to change the hash, or two peers could run
    // different AI tuning and the lobby would not notice.
    auto differs = [&](void (*mutate)(QuantBotConfig&)) {
        QuantBotConfig changed;
        mutate(changed);
        return changed.getConfigHash() != base;
    };
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.enabled = !c.recovery.enabled; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.attritionWindowMs += 1000; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.lossShareBps += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.tradeShareBps += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.lossFloorCredits += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.localWithdrawBps += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.localSevereBps += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.localPersistMs += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.resumeAssembledBps += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.resumeAdvantageBps += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.stabiliseMs += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.minWithdrawMs += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.maxWithdrawMs += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.maxRecoverMs += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.outnumberedBps += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.dispatchBypassBps += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.recovery.recallOrdersPerPass += 1; }));
    REQUIRE(differs([](QuantBotConfig& c){
        c.recovery.frontBatteriesEnabled = !c.recovery.frontBatteriesEnabled; }));

    SECTION("adjacent integer fields cannot alias each other") {
        // Bare concatenation would make these two configurations identical:
        // "1" + "234" and "12" + "34" are the same bytes. Named, delimited
        // fields keep them apart.
        QuantBotConfig a, b;
        a.recovery.lossShareBps = 1;     a.recovery.tradeShareBps = 234;
        b.recovery.lossShareBps = 12;    b.recovery.tradeShareBps = 34;
        REQUIRE(a.getConfigHash() != b.getConfigHash());
        QuantBotConfig c, d;
        c.recovery.stabiliseMs = 100;    c.recovery.minWithdrawMs = 0;
        d.recovery.stabiliseMs = 10;     d.recovery.minWithdrawMs = 00;
        // 10/0 and 100/0 differ by a digit boundary only.
        REQUIRE(c.getConfigHash() != d.getConfigHash());
        QuantBotConfig e, f;
        e.recovery.recallOrdersPerPass = 1; e.recovery.outnumberedBps = 25000;
        f.recovery.recallOrdersPerPass = 12; f.recovery.outnumberedBps = 5000;
        REQUIRE(e.getConfigHash() != f.getConfigHash());
    }
    SECTION("the per-difficulty attack tuning gap is closed") {
        REQUIRE(differs([](QuantBotConfig& c){ c.brutal.attackThresholdPercent += 0.05f; }));
        REQUIRE(differs([](QuantBotConfig& c){ c.hard.attackForceMilitaryValueRatio += 0.05f; }));
        REQUIRE(differs([](QuantBotConfig& c){ c.medium.refineryMinimum += 1; }));
    }
}

TEST_CASE("Special units: the within-group choice is measured, not array order",
          "[ai][special]") {
    auto candidate = [](uint32_t item, int price) {
        SpecialUnitPolicy::Candidate c;
        c.item = item; c.price = price; c.available = true; c.affordable = true;
        return c;
    };
    SECTION("no affordable special") {
        SpecialUnitPolicy::Candidates specials{};
        REQUIRE(SpecialUnitPolicy::select(specials) == -1);
        REQUIRE(std::string(SpecialUnitPolicy::selectionReason(specials, -1))
            == "no_affordable_special");
    }
    SECTION("with no evidence the least-owned type is tried") {
        // This is the condition the reviewed match logged: identical deficits,
        // both available and affordable. The old pooled comparison always kept
        // the Devastator because it came first in the array.
        SpecialUnitPolicy::Candidates specials{
            candidate(28, 800), candidate(35, 600), candidate(41, 600)};
        specials[0].committedValue = 4000;   // Plenty of Devastators already.
        specials[1].committedValue = 0;
        specials[2].committedValue = 1200;
        const int chosen = SpecialUnitPolicy::select(specials);
        REQUIRE(chosen == 1);
        REQUIRE(std::string(SpecialUnitPolicy::selectionReason(specials, chosen))
            == "exploration_least_owned");
    }
    SECTION("exploration rotates across the group rather than fixing on one type") {
        SpecialUnitPolicy::Candidates specials{
            candidate(28, 800), candidate(35, 600), candidate(41, 600)};
        std::vector<int> picks;
        for (int round = 0; round < 3; ++round) {
            const int chosen = SpecialUnitPolicy::select(specials);
            REQUIRE(chosen >= 0);
            picks.push_back(chosen);
            specials[size_t(chosen)].committedValue += specials[size_t(chosen)].price;
        }
        REQUIRE(picks[0] != picks[1]);
        REQUIRE(picks[1] != picks[2]);
    }
    SECTION("a measurably better type wins once there is evidence") {
        SpecialUnitPolicy::Candidates specials{
            candidate(28, 800), candidate(35, 600), candidate(41, 600)};
        // The Devastator has traded badly and the Sonic Tank has traded well,
        // with enough evidence on both sides to overcome the prior.
        specials[0].rewardMilli = 2'000'000'000;  specials[0].lossMilli = 40'000'000'000;
        specials[1].rewardMilli = 60'000'000'000; specials[1].lossMilli = 4'000'000'000;
        specials[2].available = false;
        const int chosen = SpecialUnitPolicy::select(specials);
        REQUIRE(chosen == 1);
        REQUIRE(std::string(SpecialUnitPolicy::selectionReason(specials, chosen))
            == "measured_return_per_loss");
        REQUIRE(SpecialUnitPolicy::rawScore(specials[1]) > SpecialUnitPolicy::rawScore(specials[0]));
        // And the reverse evidence picks the other way, so nothing is globally
        // preferred: the measurement decides.
        std::swap(specials[0].rewardMilli, specials[1].rewardMilli);
        std::swap(specials[0].lossMilli, specials[1].lossMilli);
        REQUIRE(SpecialUnitPolicy::select(specials) == 0);
    }
    SECTION("an unavailable or unaffordable type is never chosen") {
        SpecialUnitPolicy::Candidates specials{
            candidate(28, 800), candidate(35, 600), candidate(41, 600)};
        specials[1].rewardMilli = 60'000'000'000; specials[1].lossMilli = 1'000'000;
        specials[1].affordable = false;          // Best, but cannot be paid for.
        specials[2].available = false;
        REQUIRE(SpecialUnitPolicy::select(specials) == 0);
    }
}
