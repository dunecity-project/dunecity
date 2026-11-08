// VanillaArmyCounters (players/QuantBotArmyCounterPolicy.h): the typed policy that re-weights
// QuantBot's production mix toward the units answering the strongest reachable hostile team.
// The live survey (QuantBot::vanillaArmyCounterMix) only gathers these inputs from the game.
#include <catch2/catch_test_macros.hpp>

#include <players/QuantBotArmyCounterPolicy.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <utility>

using namespace QuantBotArmyCounterPolicy;

namespace {

// Stock rows of config/ObjectData.ini.default: price, hit points, weapon damage, reload.
// The policy never reads these; the game passes the ObjectData row of each production house.
const UnitType kTank{Unit_Tank, 300, 200, 25, 240, false};
const UnitType kSiege{Unit_SiegeTank, 600, 300, 30, 270, false};
const UnitType kLauncher{Unit_Launcher, 450, 100, 75, 750, false};
const UnitType kDevastator{Unit_Devastator, 800, 400, 40, 300, false};
const UnitType kSonic{Unit_SonicTank, 600, 110, 60, 240, false};
const UnitType kDeviator{Unit_Deviator, 750, 120, 0, 540, false};
const UnitType kOrnithopter{Unit_Ornithopter, 600, 25, 45, 150, true};
const UnitType kTrike{Unit_Trike, 150, 100, 5, 150, false};
const UnitType kRaider{Unit_RaiderTrike, 150, 80, 5, 150, false};
const UnitType kQuad{Unit_Quad, 200, 130, 7, 150, false};
const UnitType kTrooper{Unit_Trooper, 100, 45, 5, 150, false};

// Mix slots (QuantBot::kMixItems order).
constexpr size_t kTankSlot = 0, kLauncherSlot = 2, kDeviatorSlot = 5, kOrnithopterSlot = 6, kQuadSlot = 9;

constexpr auto kCity = DamageMode::CityBalance;
constexpr auto kOriginal = DamageMode::Original;

// A native mix with every slot in use; it sums to 10000.
const Mix kNative{2000, 1000, 1000, 500, 500, 500, 1000, 1000, 1000, 1500};

Candidates everySlot() {
    const UnitType types[] = {kTank, kSiege, kLauncher, kDevastator, kSonic, kDeviator, kOrnithopter,
                              kTrike, kRaider, kQuad};
    Candidates candidates{};
    for (size_t i = 0; i < candidates.size(); ++i) candidates[i] = {types[i], true};
    return candidates;
}

Candidates without(size_t slot) {
    auto candidates = everySlot();
    candidates[slot].available = false;
    return candidates;
}

Composition<32> army(std::initializer_list<std::pair<UnitType, int64_t>> units) {
    Composition<32> force;
    for (const auto& [type, value] : units) force.add(type, value);
    return force;
}

int total(const Mix& mix) { return std::accumulate(mix.begin(), mix.end(), 0); }

int64_t total(const Weights& weights) { return std::accumulate(weights.begin(), weights.end(), int64_t(0)); }

size_t largest(const Weights& weights) {
    size_t best = 0;
    for (size_t i = 1; i < weights.size(); ++i) if (weights[i] > weights[best]) best = i;
    return best;
}

bool withinBounds(const Mix& mix) {
    for (const int bps : mix) if (bps < 0 || bps > 10000) return false;
    return total(mix) == 10000;
}

} // namespace

TEST_CASE("Army counters read weapons through the engine's own target and damage rules",
          "[quantbot][production][army-counters]") {
    // Ordinary ground impacts: OrdinaryDamagePolicy's centre damage under each rule set.
    REQUIRE(volleyDamage(kTank, kTank, kCity) == 12);
    REQUIRE(volleyDamage(kTank, kTank, kOriginal) == 25);
    REQUIRE(volleyDamage(kLauncher, kTank, kCity) == 2 * 37);
    REQUIRE(volleyDamage(kLauncher, kTank, kOriginal) == 2 * 75);
    REQUIRE(volleyDamage(kTrike, kTank, kCity) == 2 * 5);
    REQUIRE(volleyDamage(kOrnithopter, kTank, kCity) == 22);
    REQUIRE(volleyDamage(kOrnithopter, kTank, kOriginal) == 38);
    // A Trooper's ranged rocket keeps three quarters of its light payload.
    REQUIRE(volleyDamage(kTrooper, kTank, kCity) == 4);
    REQUIRE(volleyDamage(kSonic, kTank, kCity) == 60);

    // Only the engine's anti-air types reach the air layer, with the whole payload.
    REQUIRE(volleyDamage(kLauncher, kOrnithopter, kCity) == 2 * 75);
    REQUIRE(volleyDamage(kTrooper, kOrnithopter, kCity) == 5);
    REQUIRE(volleyDamage(kTank, kOrnithopter, kCity) == 0);
    REQUIRE(volleyDamage(kOrnithopter, kOrnithopter, kCity) == 0);
    REQUIRE(volleyDamage(kQuad, kOrnithopter, kOriginal) == 0);

    // Sonic Tanks cannot hurt each other; gas converts and removes no hit points.
    REQUIRE(volleyDamage(kSonic, kSonic, kCity) == 0);
    REQUIRE(volleyDamage(kDeviator, kTank, kCity) == 0);
    REQUIRE(volleyDamage(kDeviator, kOrnithopter, kCity) == 0);
    REQUIRE(armyType(Unit_Deviator));
    REQUIRE_FALSE(destroys(Unit_Deviator));

    // Reload cycles to destroy a fresh unit; 0 means never.
    REQUIRE(defeatCycles(kTank, kTank, kCity) == 17 * 240);
    REQUIRE(defeatCycles(kTank, kTank, kOriginal) == 8 * 240);
    REQUIRE(defeatCycles(kLauncher, kOrnithopter, kCity) == 750);
    REQUIRE(defeatCycles(kTank, kOrnithopter, kCity) == 0);
    REQUIRE(defeatCycles(kDeviator, kTank, kCity) == 0);
}

TEST_CASE("Army counters count only armed units as army", "[quantbot][production][army-counters]") {
    for (const int item : {Unit_Carryall, Unit_Harvester, Unit_MCV, Unit_Saboteur, Unit_Sandworm, Unit_Frigate})
        REQUIRE_FALSE(armyType(item));
    for (const int item : {Unit_Tank, Unit_Ornithopter, Unit_Soldier, Unit_Trooper, Unit_Quad})
        REQUIRE(armyType(item));

    Teams<4> teams;
    REQUIRE_FALSE(teams.add(3, Unit_Carryall, 5000));
    REQUIRE_FALSE(teams.add(3, Unit_Harvester, 5000));
    REQUIRE_FALSE(teams.add(3, Unit_MCV, 5000));
    REQUIRE(teams.strongest() == -1);
    REQUIRE(teams.add(3, Unit_Tank, 300));
    REQUIRE(teams.strongest() == 3);

    Composition<4> force;
    REQUIRE_FALSE(force.add({Unit_MCV, 900, 600, 0, 0, false}, 900));
    REQUIRE_FALSE(force.add({Unit_Carryall, 800, 100, 0, 0, true}, 800));
    REQUIRE(force.total == 0);
    REQUIRE(force.size == 0u);
}

TEST_CASE("Army counters pick the strongest team and break ties to the lower team id",
          "[quantbot][production][army-counters]") {
    Teams<13> teams;
    REQUIRE(teams.add(5, Unit_Tank, 900));
    REQUIRE(teams.add(2, Unit_Launcher, 450));
    REQUIRE(teams.add(2, Unit_Tank, 300));
    REQUIRE(teams.strongest() == 5);

    // Equal value: the lower team id, whatever order the units were counted in.
    REQUIRE(teams.add(2, Unit_Quad, 150));
    REQUIRE(teams.strongest() == 2);
    Teams<13> reversed;
    REQUIRE(reversed.add(2, Unit_Quad, 900));
    REQUIRE(reversed.add(5, Unit_Tank, 900));
    REQUIRE(reversed.strongest() == 2);

    // Units without value add nothing.
    REQUIRE_FALSE(teams.add(7, Unit_Tank, 0));
    REQUIRE_FALSE(teams.add(7, Unit_Tank, -300));
    REQUIRE(teams.size == 2u);
    REQUIRE(Teams<13>{}.strongest() == -1);
}

TEST_CASE("Army counters answer armed air with anti-air", "[quantbot][production][army-counters]") {
    const auto force = army({{kOrnithopter, 3000}});
    const auto target = counterTarget(force, everySlot(), kCity);
    // The Launcher is the only buildable unit whose weapon removes an aircraft's hit points.
    REQUIRE(target.value[kLauncherSlot] == 3000);
    REQUIRE(total(target.value) == 3000);
    REQUIRE(target.unanswered == 0);

    // The Deviator is not re-weighted and keeps its native 500; the rest goes to Launchers.
    const auto full = blend(kNative, everySlot(), target, 10000);
    REQUIRE(full[kLauncherSlot] == 9500);
    REQUIRE(full[kDeviatorSlot] == 500);
    REQUIRE(full[kTankSlot] == 0);
    REQUIRE(full[kOrnithopterSlot] == 0);
    REQUIRE(withinBounds(full));

    const auto half = blend(kNative, everySlot(), target, 5000);
    REQUIRE(half == Mix{1000, 500, 5250, 250, 250, 500, 500, 500, 500, 750});
}

TEST_CASE("Army counters weigh armour against the whole team's anti-air",
          "[quantbot][production][army-counters]") {
    for (const auto mode : {kCity, kOriginal}) {
        // Devastators cannot shoot aircraft: the Ornithopter answers them best.
        const auto armour = counterTarget(army({{kDevastator, 4000}}), everySlot(), mode);
        REQUIRE(largest(armour.value) == kOrnithopterSlot);
        REQUIRE(armour.value[kDeviatorSlot] == 0);
        REQUIRE(armour.unanswered == 0);
        const auto mix = blend(kNative, everySlot(), armour, 10000);
        REQUIRE(mix[kOrnithopterSlot] > kNative[kOrnithopterSlot]);
        REQUIRE(mix[kTankSlot] < kNative[kTankSlot]);
        REQUIRE(withinBounds(mix));

        // A Launcher escort shoots aircraft, so the same Devastators no longer favour them.
        const auto escorted = counterTarget(army({{kDevastator, 4000}, {kLauncher, 2000}}), everySlot(), mode);
        REQUIRE(escorted.value[kOrnithopterSlot] * total(armour.value)
                < armour.value[kOrnithopterSlot] * total(escorted.value));
    }
}

TEST_CASE("Army counters never plan around a unit no factory can build",
          "[quantbot][production][army-counters]") {
    // Without a High-Tech Factory the Ornithopter gets no counter value and keeps its
    // native share exactly; the buildable slots are re-weighted among themselves.
    const auto noAir = without(kOrnithopterSlot);
    const auto armour = counterTarget(army({{kDevastator, 4000}}), noAir, kCity);
    REQUIRE(armour.value[kOrnithopterSlot] == 0);
    const auto mix = blend(kNative, noAir, armour, 10000);
    REQUIRE(mix[kOrnithopterSlot] == kNative[kOrnithopterSlot]);
    REQUIRE(mix[kDeviatorSlot] == kNative[kDeviatorSlot]);
    REQUIRE(withinBounds(mix));

    // Aircraft with no buildable anti-air are unanswered: the native mix stands.
    const auto noLaunchers = without(kLauncherSlot);
    const auto air = counterTarget(army({{kOrnithopter, 3000}}), noLaunchers, kCity);
    REQUIRE(total(air.value) == 0);
    REQUIRE(air.unanswered == 3000);
    REQUIRE(blend(kNative, noLaunchers, air, 10000) == kNative);

    // A mixed army: its aircraft keep native proportions, its armour is answered.
    const auto mixed = counterTarget(army({{kTank, 3000}, {kOrnithopter, 1200}}), noLaunchers, kCity);
    REQUIRE(mixed.unanswered == 1200);
    REQUIRE(total(mixed.value) > 0);
    const auto partial = blend(kNative, noLaunchers, mixed, 10000);
    REQUIRE(partial[kLauncherSlot] == kNative[kLauncherSlot]);
    REQUIRE(withinBounds(partial));

    // No factory at all: nothing is answerable.
    Candidates none{};
    const auto idle = counterTarget(army({{kTank, 3000}}), none, kCity);
    REQUIRE(total(idle.value) == 0);
    REQUIRE(idle.unanswered == 3000);
    REQUIRE(blend(kNative, none, idle, 10000) == kNative);

    // No reachable hostile army: no target, the native mix.
    const auto empty = counterTarget(Composition<32>{}, everySlot(), kCity);
    REQUIRE(total(empty.value) == 0);
    REQUIRE(empty.unanswered == 0);
    REQUIRE(blend(kNative, everySlot(), empty, 10000) == kNative);
}

TEST_CASE("Army counters blend 0, 50 and 100 percent of the way", "[quantbot][production][army-counters]") {
    // Tank and Quad are buildable; the Launcher's 2000 belongs to an unavailable slot.
    const Mix native{4000, 0, 2000, 0, 0, 0, 0, 0, 0, 4000};
    Candidates candidates{};
    candidates[kTankSlot] = {kTank, true};
    candidates[kQuadSlot] = {kQuad, true};
    Target target;
    target.value[kQuadSlot] = 3000;

    REQUIRE(blend(native, candidates, target, 0) == native);
    REQUIRE(blend(native, candidates, target, 5000) == Mix{2000, 0, 2000, 0, 0, 0, 0, 0, 0, 6000});
    REQUIRE(blend(native, candidates, target, 10000) == Mix{0, 0, 2000, 0, 0, 0, 0, 0, 0, 8000});
    // Out-of-range strengths clamp.
    REQUIRE(blend(native, candidates, target, -1) == native);
    REQUIRE(blend(native, candidates, target, 20000) == blend(native, candidates, target, 10000));

    // Unanswered value follows the native proportions of the buildable slots.
    target.unanswered = 3000;
    REQUIRE(blend(native, candidates, target, 10000) == Mix{2000, 0, 2000, 0, 0, 0, 0, 0, 0, 6000});
}

TEST_CASE("Army counters stay bounded", "[quantbot][production][army-counters]") {
    constexpr int64_t largestValue = std::numeric_limits<int64_t>::max();
    constexpr int largestInt = std::numeric_limits<int>::max();

    REQUIRE(normalize(Weights{}) == Mix{});
    REQUIRE(normalize(Weights{-5, 0, 0, 0, 0, 0, 0, 0, 0, 5}) == Mix{0, 0, 0, 0, 0, 0, 0, 0, 0, 10000});
    REQUIRE(normalize(Weights{largestValue, largestValue}) == Mix{5000, 5000});
    REQUIRE(normalize(Weights{1, 1, 1}) == Mix{3334, 3333, 3333});
    REQUIRE(scaled(largestValue, 10000) == largestValue);
    REQUIRE(scaled(largestValue, 0) == 0);
    REQUIRE(scaled(12345, 5000) == 6172);
    REQUIRE(shareBps(largestValue, largestValue) == 10000);
    REQUIRE(shareBps(1, 3) == 3333);
    REQUIRE(shareBps(0, 5) == 0);
    REQUIRE(addSaturated(largestValue, 1) == largestValue);

    const UnitType slow{Unit_Tank, largestInt, largestInt, 1, largestInt, false};
    REQUIRE(defeatCycles(slow, slow, kOriginal) == kMaxCycles);

    // Types beyond the table are counted and left to the native mix.
    Composition<2> small;
    REQUIRE(small.add(kTank, 300));
    REQUIRE(small.add(kQuad, 200));
    REQUIRE(small.add(kTrike, 150));
    REQUIRE(small.add(kTank, 300));
    REQUIRE(small.size == 2u);
    REQUIRE(small.overflow == 150);
    REQUIRE(small.total == 950);
    REQUIRE(counterTarget(small, everySlot(), kCity).unanswered == 150);

    // Saturating values and extreme unit data never overflow the result.
    auto candidates = everySlot();
    candidates[kQuadSlot].type = {Unit_Quad, largestInt, largestInt, largestInt, largestInt, false};
    candidates[kTankSlot].type = {Unit_Tank, 1, 1, largestInt, 1, false};
    const auto huge = army({{kSiege, largestValue}, {kOrnithopter, largestValue}, {slow, largestValue}});
    REQUIRE(huge.total == largestValue);
    const auto target = counterTarget(huge, candidates, kCity);
    for (const auto value : target.value) REQUIRE(value >= 0);
    REQUIRE(target.unanswered >= 0);
    REQUIRE(withinBounds(blend(kNative, candidates, target, 10000)));
    REQUIRE(withinBounds(blend(kNative, candidates, target, 5000)));
    const Mix extreme{largestInt, 0, largestInt, 0, 0, 0, 0, 0, 0, largestInt};
    REQUIRE(withinBounds(blend(extreme, candidates, target, 5000)));
}
