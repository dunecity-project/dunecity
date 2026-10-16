#include <catch2/catch_test_macros.hpp>

#include <players/ForceConcentrationPolicy.h>

#include <algorithm>
#include <vector>

// QuantBot force concentration (docs/formations.md, "Concentrating force"): the pure decisions
// behind the test-only candidates - focus fire, converging on a fight, pooling and recalling
// defenders, staging and wave size - measured in duels with tests/ai/formation-duel.py.

using namespace ForceConcentrationPolicy;

TEST_CASE("Force: focus fire prefers fighters, then the fewest hit points, then the lowest id", "[ai][force]") {
    const TargetKey tank{ArmedUnit, 300, 7}, trike{ArmedUnit, 90, 9}, turret{ArmedBuilding, 50, 3},
        harvester{Unarmed, 20, 2}, refinery{Building, 10, 1}, wall{Wall, 1, 0};
    REQUIRE(betterTarget(trike, tank));
    REQUIRE(betterTarget(tank, turret));
    REQUIRE(betterTarget(turret, harvester));
    REQUIRE(betterTarget(harvester, refinery));
    REQUIRE(betterTarget(refinery, wall));
    // Equal class and hit points: the lower id, so every peer and every list order agree.
    REQUIRE(betterTarget(TargetKey{ArmedUnit, 90, 4}, trike));
    REQUIRE_FALSE(betterTarget(trike, trike));
    // A strict weak order: the best of any permutation is the same unit.
    std::vector<TargetKey> keys{tank, trike, turret, harvester, refinery, wall, TargetKey{ArmedUnit, 90, 4}};
    std::sort(keys.begin(), keys.end(), [](const TargetKey& a, const TargetKey& b) { return a.id < b.id; });
    TargetKey first{};
    bool set = false;
    do {
        TargetKey best = keys.front();
        for (const auto& key : keys) if (betterTarget(key, best)) best = key;
        if (!set) { first = best; set = true; }
        REQUIRE(best.id == first.id);
    } while (std::next_permutation(keys.begin(), keys.end(), [](const TargetKey& a, const TargetKey& b) { return a.id < b.id; }));
    REQUIRE(first.id == 4);
}

TEST_CASE("Force: a unit keeps its target unless the new one is clearly better", "[ai][force]") {
    const TargetKey current{ArmedUnit, 200, 5};
    REQUIRE_FALSE(shouldSwitch(true, current, current));
    // Out of reach: always take the best in reach.
    REQUIRE(shouldSwitch(false, current, TargetKey{Building, 900, 1}));
    // A better class always, a worse class never.
    REQUIRE(shouldSwitch(true, TargetKey{Building, 10, 5}, TargetKey{ArmedUnit, 900, 6}));
    REQUIRE_FALSE(shouldSwitch(true, current, TargetKey{Unarmed, 1, 6}));
    // Same class: only at 75% of the hit points or fewer (no turret swinging).
    REQUIRE(shouldSwitch(true, current, TargetKey{ArmedUnit, 150, 6}));
    REQUIRE_FALSE(shouldSwitch(true, current, TargetKey{ArmedUnit, 151, 6}));
}

TEST_CASE("Force: attack value ranks damage per hit point exactly, and overkill is skipped", "[ai][force]") {
    const ValueKey launcher{ArmedUnit, 1000, 100, 8}, tank{ArmedUnit, 1500, 300, 3}, turret{ArmedBuilding, 5000, 10, 1};
    REQUIRE(betterValue(launcher, tank));         // 10 per hit point against 5
    REQUIRE(betterValue(tank, turret));           // a fighter before a turret
    // Equal damage per hit point: fewer hit points, then the lower id.
    REQUIRE(betterValue(ValueKey{ArmedUnit, 500, 50, 9}, launcher));
    REQUIRE(betterValue(ValueKey{ArmedUnit, 1000, 100, 2}, launcher));
    REQUIRE(overkilled(100, 100));
    REQUIRE_FALSE(overkilled(99, 100));
    REQUIRE(overkilled(1, 0));
    // Switching: kept unless out of reach, overkilled by the others, or a third better.
    REQUIRE(shouldSwitchValue(false, false, tank, tank) == false);
    REQUIRE(shouldSwitchValue(false, false, tank, launcher));
    REQUIRE(shouldSwitchValue(true, true, launcher, tank));
    REQUIRE(shouldSwitchValue(true, false, tank, launcher));                                // 2x
    REQUIRE_FALSE(shouldSwitchValue(true, false, launcher, ValueKey{ArmedUnit, 1300, 100, 9}));  // 1.3x
    REQUIRE(shouldSwitchValue(true, false, launcher, ValueKey{ArmedUnit, 1334, 100, 9}));        // 4/3
    REQUIRE_FALSE(shouldSwitchValue(true, false, ValueKey{Unarmed, 0, 10, 1}, ValueKey{Unarmed, 0, 5, 2}));
}

TEST_CASE("Force: a member joins a nearby fight unless its own target is about as close", "[ai][force]") {
    REQUIRE(converge(10, -1));
    REQUIRE(converge(kConvergeTiles, -1));
    REQUIRE_FALSE(converge(kConvergeTiles + 1, -1));
    REQUIRE_FALSE(converge(-1, -1));
    REQUIRE(converge(10, 10 + kConvergeSlackTiles + 1));
    REQUIRE_FALSE(converge(10, 10 + kConvergeSlackTiles));
    REQUIRE_FALSE(converge(10, 3));
}

TEST_CASE("Force: defenders are pooled nearest first by strength, the raiders only when needed", "[ai][force]") {
    REQUIRE(poolRequired(1000) == 2000);
    REQUIRE(poolRequired(-5) == 0);
    const std::vector<Responder> troops{{5, 600, 30}, {2, 500, 10}, {9, 800, 60}, {4, 400, 10}, {7, 0, 5}};
    // Nearest first, ties by id; the zero-value troop is skipped; stops at the requirement.
    REQUIRE(pool(900, 0, troops, 40, false) == std::vector<uint32_t>{2, 4});
    REQUIRE(pool(1000, 0, troops, 40, false) == std::vector<uint32_t>{2, 4, 5});
    // Already committed value counts.
    REQUIRE(pool(1000, 600, troops, 40, false) == std::vector<uint32_t>{2});
    // The band: without the recall the troops beyond it stay away, with it they come home.
    REQUIRE(pool(5000, 0, troops, 40, false) == std::vector<uint32_t>{2, 4, 5});
    REQUIRE(pool(5000, 0, troops, 40, true) == std::vector<uint32_t>{2, 4, 5, 9});
    // A raid the troops nearby can answer never recalls anyone.
    REQUIRE(pool(900, 0, troops, 40, true) == std::vector<uint32_t>{2, 4});
    // Any input order gives the same answer.
    auto reversed = troops;
    std::reverse(reversed.begin(), reversed.end());
    REQUIRE(pool(5000, 0, reversed, 40, true) == pool(5000, 0, troops, 40, true));
}

TEST_CASE("Force: a staged wave attacks with local superiority; wave readiness scales", "[ai][force]") {
    REQUIRE(superior(1500, 1000));
    REQUIRE_FALSE(superior(1499, 1000));
    REQUIRE(superior(1, 0));
    REQUIRE(scaledReadiness(10000, 150) == 15000);
    REQUIRE(scaledReadiness(10000, 60) == 6000);
    REQUIRE(scaledReadiness(-3, 150) == 0);
}
