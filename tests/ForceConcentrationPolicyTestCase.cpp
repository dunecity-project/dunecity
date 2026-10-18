#include <catch2/catch_test_macros.hpp>

#include <players/ForceConcentrationPolicy.h>

#include <algorithm>
#include <cstdlib>
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
    // Outnumbered (D): the observed enemy army is worth 125% of ours or more.
    REQUIRE(outnumbered(10000, 12500));
    REQUIRE_FALSE(outnumbered(10000, 12499));
    REQUIRE(outnumbered(0, 1));
    REQUIRE_FALSE(outnumbered(0, 0));
    REQUIRE_FALSE(outnumbered(10000, 0));
}

TEST_CASE("Force: a Construction Yard start holds its first wave for full readiness", "[ai][force]") {
    // Hard on a 128 map: house threshold 20,000, waves at 60% (12,000).
    REQUIRE(openingReadiness(60, false, 3800, 20000) == 100);     // MCV and a few units
    REQUIRE(openingReadiness(60, false, 0, 20000) == 100);
    REQUIRE(openingReadiness(60, false, 11999, 20000) == 100);
    REQUIRE(openingReadiness(60, false, 12000, 20000) == 60);     // a starting army that can strike
    REQUIRE(openingReadiness(60, false, 21000, 24000) == 60);     // Fortress on a huge map
    REQUIRE(openingReadiness(60, true, 0, 20000) == 60);          // from the first wave on
    REQUIRE(openingReadiness(100, false, 0, 20000) == 100);
    REQUIRE(openingReadiness(150, false, 0, 20000) == 150);
}

TEST_CASE("Force: the reserve bundles a quarter of the way forward and the gate waits for 70%", "[ai][force]") {
    REQUIRE(along(40, 120, kBundleForwardPercent) == 60);
    REQUIRE(along(120, 40, kBundleForwardPercent) == 100);
    REQUIRE(along(10, 10, kBundleForwardPercent) == 10);
    REQUIRE(bundleRadius(0) == 3);
    REQUIRE(bundleRadius(18) == 3);
    REQUIRE(bundleRadius(19) == 4);
    REQUIRE(bundleRadius(100) == 8);
    REQUIRE(bundled(7000, 10000));
    REQUIRE_FALSE(bundled(6999, 10000));
    REQUIRE(bundled(0, 0));
    // Bundle orders: farthest first, then the lower id; the same choice from any list order.
    REQUIRE(bundleOrderFirst(20, 9, 10, 1));
    REQUIRE_FALSE(bundleOrderFirst(10, 1, 20, 9));
    REQUIRE(bundleOrderFirst(10, 1, 10, 2));
    REQUIRE_FALSE(bundleOrderFirst(10, 2, 10, 1));
    REQUIRE_FALSE(bundleOrderFirst(10, 1, 10, 1));
    struct W { int distance; uint32_t id; };
    std::vector<W> a{{5, 4}, {30, 7}, {30, 2}, {12, 9}, {5, 1}};
    std::vector<W> b(a.rbegin(), a.rend());
    auto order = [](std::vector<W>& v) {
        std::sort(v.begin(), v.end(), [](const W& x, const W& y) { return bundleOrderFirst(x.distance, x.id, y.distance, y.id); });
        std::vector<uint32_t> ids;
        for (const auto& w : v) ids.push_back(w.id);
        return ids;
    };
    REQUIRE(order(a) == std::vector<uint32_t>{2, 7, 9, 1, 4});
    REQUIRE(order(b) == order(a));
}

TEST_CASE("Force: split release points are integer, symmetric and 90 degrees apart", "[ai][force]") {
    REQUIRE(isqrt(0) == 0);
    REQUIRE(isqrt(15) == 3);
    REQUIRE(isqrt(16) == 4);
    REQUIRE(isqrt(int64_t(1) << 40) == (int64_t(1) << 20));
    const Point objective{100, 100}, from{100, 40};     // the wave comes from straight above
    const Point head = releasePoint(objective, from, 14, 0);
    REQUIRE((head.x == 100 && head.y == 86));
    const Point a = releasePoint(objective, from, 14, 45), b = releasePoint(objective, from, 14, -45);
    // Mirror images across the approach line, each about 14 tiles from the objective.
    REQUIRE(a.y == b.y);
    REQUIRE(a.x - objective.x == objective.x - b.x);
    REQUIRE(std::abs(a.x - objective.x) == 9);
    REQUIRE(a.y == 91);
    // The two release directions are perpendicular: their dot product (from the objective) is ~0.
    const int dot = (a.x - objective.x) * (b.x - objective.x) + (a.y - objective.y) * (b.y - objective.y);
    REQUIRE(std::abs(dot) <= 1);
    const Point left = releasePoint(objective, from, 14, 90), right = releasePoint(objective, from, 14, -90);
    REQUIRE((left.y == 100 && right.y == 100 && left.x + right.x == 200 && std::abs(left.x - 100) == 14));
    REQUIRE((releasePoint(objective, objective, 14, 45).x == 100 && releasePoint(objective, objective, 14, 45).y == 100));
}

TEST_CASE("Force: split groups fill by lean and value in any input order", "[ai][force]") {
    const std::vector<SplitMember> wave{{5, 1000, -6}, {2, 1000, 4}, {9, 500, -2}, {7, 500, 3}, {3, 1500, -6}};
    const auto halves = splitGroups(wave, 5000);
    // Leaning most to group 0: ids 3 then 5 (both -6, by id); 2,500 of 4,500 passes half.
    REQUIRE(halves == std::vector<int>{0, 1, 1, 1, 0});
    // The 30% flank: id 3 alone (1,500 of 4,500) already holds it.
    const auto flank = splitGroups(wave, kFlankShareBps);
    REQUIRE(flank == std::vector<int>{1, 1, 1, 1, 0});
    auto reversed = wave;
    std::reverse(reversed.begin(), reversed.end());
    auto again = splitGroups(reversed, 5000);
    std::reverse(again.begin(), again.end());
    REQUIRE(again == halves);
    // Two members always make two groups; one stays in group 1.
    REQUIRE(splitGroups({{1, 100, 0}, {2, 100, 0}}, 9000) == std::vector<int>{0, 1});
    REQUIRE(splitGroups({{1, 100, 0}}, 5000) == std::vector<int>{1});
}

TEST_CASE("Force: a split attacks when both groups are there or one is fighting", "[ai][force]") {
    const int alive[2] = {8, 4};
    const int some[2] = {6, 2}, both[2] = {6, 3};
    REQUIRE_FALSE(splitReady(some, alive));
    REQUIRE(splitReady(both, alive));
    const int none[2] = {0, 0}, gone[2] = {0, 4};
    REQUIRE(splitReady(none, none));
    REQUIRE_FALSE(splitReady(none, gone));
    REQUIRE(splitFighting(2, 8));
    REQUIRE_FALSE(splitFighting(1, 8));
    REQUIRE_FALSE(splitFighting(0, 0));
}

TEST_CASE("Force: strength-aware waves and the home reserve follow the enemy estimate", "[ai][force]") {
    REQUIRE(strongEnough(10000, 10000, 10000));
    REQUIRE_FALSE(strongEnough(9999, 10000, 10000));
    REQUIRE(strongEnough(8000, 10000, 8000));
    REQUIRE(strongEnough(0, 0, 12000));
    REQUIRE(reserveShareBps(10000, 10000) == 0);
    REQUIRE(reserveShareBps(12000, 10000) == 0);
    REQUIRE(reserveShareBps(8000, 10000) == 2000);
    REQUIRE(reserveShareBps(1000, 10000) == kReserveMaxBps);
    REQUIRE(reserveShareBps(5000, 0) == 0);
}

TEST_CASE("Force: a sized defence takes the bundle first, then the nearest, and declines a lost cause", "[ai][force]") {
    REQUIRE(sizedRequired(1000) == 1500);
    REQUIRE(sizedRequired(-1) == 0);
    const std::vector<SizedResponder> troops{{5, 600, 30, false}, {2, 500, 10, false}, {9, 800, 60, true},
                                             {4, 400, 50, false}, {7, 0, 5, true}};
    // The bundled troop comes first even from beyond the band; the zero-value one is skipped.
    REQUIRE(sizedPool(1200, 0, troops, 40) == std::vector<uint32_t>{9, 2});
    // Beyond the band and not bundled: never.
    REQUIRE(sizedPool(5000, 0, troops, 40) == std::vector<uint32_t>{9, 2, 5});
    REQUIRE(sizedPool(1000, 900, troops, 40) == std::vector<uint32_t>{9});
    auto reversed = troops;
    std::reverse(reversed.begin(), reversed.end());
    REQUIRE(sizedPool(5000, 0, reversed, 40) == sizedPool(5000, 0, troops, 40));
    REQUIRE(answerable(1000, 400, 600));
    REQUIRE_FALSE(answerable(1000, 400, 599));
}

TEST_CASE("Force: perimeter turrets follow the enemy's lead and stand on the enemy-side band", "[ai][force]") {
    REQUIRE(perimeterTurretGoal(10000, 12499) == 0);       // not outnumbered
    REQUIRE(perimeterTurretGoal(10000, 12500) == 2);
    REQUIRE(perimeterTurretGoal(10000, 15999) == 5);
    REQUIRE(perimeterTurretGoal(1000, 90000) == kPerimeterTurretsMax);
    REQUIRE(perimeterTurretGoal(0, 0) == 0);
    REQUIRE(onPerimeterBand(10, 0, 10));
    REQUIRE(onPerimeterBand(6, kPerimeterHalfWidth, 10));
    REQUIRE(onPerimeterBand(12, -kPerimeterHalfWidth, 10));
    REQUIRE_FALSE(onPerimeterBand(5, 0, 10));               // inside the base
    REQUIRE_FALSE(onPerimeterBand(13, 0, 10));              // out in the field
    REQUIRE_FALSE(onPerimeterBand(10, kPerimeterHalfWidth + 1, 10));
}

TEST_CASE("Force: the late game finishes the weakest known team and waits for a bigger wave at a fortress", "[ai][force]") {
    TeamStrength strong{2, 40000, 10, 300}, weak{3, 20000, 30, 300}, weakest{4, 20000, 10, 300};
    REQUIRE(weakerTeam(weakest, strong));
    REQUIRE(weakerTeam(weakest, weak));                    // turrets count kKnownTurretValue each
    REQUIRE_FALSE(weakerTeam(strong, weakest));
    TeamStrength sameButSmaller{5, 20000, 10, 120};
    REQUIRE(weakerTeam(sameButSmaller, weakest));          // tie: fewer known structures
    TeamStrength twin{1, 20000, 10, 120};
    REQUIRE(weakerTeam(twin, sameButSmaller));             // tie: lower team id
    REQUIRE_FALSE(weakerTeam(sameButSmaller, twin));
    REQUIRE(fortifiedReady(7000, 100000, kFortifiedTurrets - 1) == 7000);
    REQUIRE(fortifiedReady(7000, 100000, kFortifiedTurrets) == 100000 * kFortifiedLimitPercent / 100);
    REQUIRE(fortifiedReady(90000, 100000, 40) == 90000);   // the normal readiness when it is more
}

TEST_CASE("Force: a sonic wave's path runs past the target and one tile either side of the line", "[ai][force]") {
    REQUIRE(inSonicPath(4, 0, 2, 0));                       // between shooter and target
    REQUIRE(inSonicPath(4, 0, 2, 1));                       // one tile beside the line
    REQUIRE_FALSE(inSonicPath(4, 0, 2, 2));
    REQUIRE(inSonicPath(4, 0, 7, 0));                       // past the target: the wave runs on
    REQUIRE(inSonicPath(4, 0, kSonicReachTiles, 0));
    REQUIRE_FALSE(inSonicPath(4, 0, kSonicReachTiles + 1, 0));
    REQUIRE_FALSE(inSonicPath(4, 0, -1, 0));                // behind the shooter
    REQUIRE_FALSE(inSonicPath(4, 0, 0, 1));                 // beside the shooter
    REQUIRE(inSonicPath(3, 3, 5, 5));                       // diagonal
    REQUIRE(inSonicPath(3, 3, 2, 3));                       // 0.7 tiles off the diagonal
    REQUIRE_FALSE(inSonicPath(3, 3, 1, 4));                 // 2.1 tiles off it
    REQUIRE_FALSE(inSonicPath(0, 0, 1, 0));                 // no direction
}
