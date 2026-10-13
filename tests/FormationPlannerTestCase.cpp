/*
 *  FormationPlannerTestCase.cpp
 *
 *  The pure formation planner (include/FormationPlanner.h, docs/formations.md): slot
 *  layout, determinism independent of member order, fallback for unusable slots,
 *  uniqueness, map-edge clipping, the group speed and the catch-up rule.
 */

#include <catch2/catch_all.hpp>
#include <FormationPlanner.h>
#include <data.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

using namespace FormationPlanner;

namespace {

constexpr int kMapSize = 64;
constexpr int kInfantryPerTile = 5;

Member makeMember(Uint32 id, Uint32 item, int x, int y, FixPoint speed = 1) {
    Member member;
    member.objectID = id;
    member.itemID = item;
    member.tile = Coord(x, y);
    member.role = roleOf(item);
    member.speed = speed;
    member.infantry = isInfantryUnit(static_cast<int>(item));
    return member;
}

/// Open sand everywhere, except the listed blocked tiles.
struct Terrain {
    std::set<std::pair<int, int>> blocked;
    CanStand query() const {
        return [this](const Member&, const Coord& tile) { return !blocked.count({tile.x, tile.y}); };
    }
};

std::map<Uint32, Coord> slotsOf(const Plan& plan) {
    std::map<Uint32, Coord> slots;
    for (const auto& slot : plan.slots) slots[slot.objectID] = slot.tile;
    return slots;
}

/// Twelve mixed units spread over more than the coherent radius.
std::vector<Member> mixedArmy() {
    return {
        makeMember(11, Unit_Tank, 4, 10, 1.28_fix),       makeMember(12, Unit_Tank, 6, 20, 1.28_fix),
        makeMember(13, Unit_Tank, 3, 30, 1.28_fix),       makeMember(14, Unit_Quad, 9, 15, 2.56_fix),
        makeMember(21, Unit_Launcher, 5, 12, 1.28_fix),   makeMember(22, Unit_Launcher, 2, 25, 1.28_fix),
        makeMember(23, Unit_Launcher, 8, 28, 1.28_fix),   makeMember(24, Unit_SiegeTank, 1, 18, 1.0_fix),
        makeMember(31, Unit_Trooper, 7, 22, 1.12_fix),    makeMember(32, Unit_Soldier, 3, 14, 0.56_fix),
        makeMember(41, Unit_Harvester, 2, 21, 1.28_fix),  makeMember(42, Unit_Devastator, 6, 26, 0.72_fix),
    };
}

void requireUnique(const Plan& plan, const std::vector<Member>& members) {
    std::map<std::pair<int, int>, int> vehicles, infantry;
    for (const auto& member : members) {
        const Coord tile = plan.slotFor(member.objectID);
        REQUIRE(tile.isValid());
        (member.infantry ? infantry : vehicles)[{tile.x, tile.y}]++;
    }
    for (const auto& [tile, count] : vehicles) {
        REQUIRE(count == 1);
        REQUIRE(infantry.count(tile) == 0);
    }
    for (const auto& [tile, count] : infantry) REQUIRE(count <= kInfantryPerTile);
}

} // namespace

TEST_CASE("Formation roles put armour in front of ranged units, infantry and support", "[formation]") {
    REQUIRE(roleOf(Unit_Tank) == Role::Armour);
    REQUIRE(roleOf(Unit_Quad) == Role::Armour);
    REQUIRE(roleOf(Unit_Trike) == Role::Armour);
    REQUIRE(roleOf(Unit_Devastator) == Role::Armour);
    REQUIRE(roleOf(Unit_SonicTank) == Role::Armour);
    REQUIRE(roleOf(Unit_Launcher) == Role::Ranged);
    REQUIRE(roleOf(Unit_SiegeTank) == Role::Ranged);
    REQUIRE(roleOf(Unit_Deviator) == Role::Ranged);
    REQUIRE(roleOf(Unit_Soldier) == Role::Infantry);
    REQUIRE(roleOf(Unit_Trooper) == Role::Infantry);
    REQUIRE(roleOf(Unit_Harvester) == Role::Support);
    REQUIRE(roleOf(Unit_MCV) == Role::Support);
}

TEST_CASE("Box width and travel direction", "[formation]") {
    REQUIRE(boxWidth(1) == 2);
    REQUIRE(boxWidth(2) == 2);
    REQUIRE(boxWidth(6) == 3);
    REQUIRE(boxWidth(12) == 5);     // ceil(sqrt(18)) = 5
    REQUIRE(boxWidth(24) == 6);     // ceil(sqrt(36)) = 6
    REQUIRE(facing(Coord(5, 5), Coord(5, 5)) == Coord(0, 0));
    REQUIRE(facing(Coord(0, 0), Coord(20, 1)) == Coord(1, 0));
    REQUIRE(facing(Coord(0, 0), Coord(-3, -20)) == Coord(0, -1));
    REQUIRE(facing(Coord(0, 0), Coord(10, 9)) == Coord(1, 1));
    REQUIRE(facing(Coord(0, 0), Coord(-10, 8)) == Coord(-1, 1));
}

TEST_CASE("Fewer than two members is a plain move to the target", "[formation]") {
    Terrain terrain;
    REQUIRE(plan({}, Coord(5, 5), kMapSize, kMapSize, terrain.query(), kInfantryPerTile).slots.empty());
    const auto single = plan({makeMember(7, Unit_Tank, 1, 1, 1.28_fix)}, Coord(30, 30), kMapSize, kMapSize,
                             terrain.query(), kInfantryPerTile);
    REQUIRE_FALSE(single.formation);
    REQUIRE(single.slots.size() == 1);
    REQUIRE(single.slotFor(7) == Coord(30, 30));
    REQUIRE(single.groupSpeed == 1.28_fix);
}

TEST_CASE("The plan is identical whatever order the members are listed in", "[formation][determinism]") {
    Terrain terrain;
    terrain.blocked = {{40, 20}, {41, 20}, {40, 21}};    // force some fallbacks too
    auto members = mixedArmy();
    const auto reference = plan(members, Coord(40, 20), kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
    REQUIRE(reference.formation);
    REQUIRE(reference.slots.size() == members.size());
    for (int round = 0; round < 24; ++round) {
        // A deterministic sequence of distinct permutations.
        std::rotate(members.begin(), members.begin() + 1 + round % 5, members.end());
        if (round % 2) std::reverse(members.begin(), members.end());
        const auto again = plan(members, Coord(40, 20), kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
        REQUIRE(slotsOf(again) == slotsOf(reference));
        REQUIRE(again.groupSpeed == reference.groupSpeed);
    }
    // Slots are reported sorted by object id.
    REQUIRE(std::is_sorted(reference.slots.begin(), reference.slots.end(),
        [](const Slot& a, const Slot& b) { return a.objectID < b.objectID; }));
}

TEST_CASE("The group speed is the slowest vehicle's; infantry set it only on their own", "[formation]") {
    Terrain terrain;
    // The soldier (0.56) and trooper are slower, but the devastator is the slowest vehicle.
    const auto result = plan(mixedArmy(), Coord(40, 20), kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
    REQUIRE(result.groupSpeed == 0.72_fix);
    REQUIRE(groupPace(mixedArmy()) == 0.72_fix);
    const std::vector<Member> squad = {makeMember(1, Unit_Trooper, 1, 1, 1.12_fix), makeMember(2, Unit_Soldier, 2, 1, 0.56_fix)};
    REQUIRE(groupPace(squad) == 0.56_fix);
    REQUIRE(groupPace({}) == 0);
}

TEST_CASE("A coherent group keeps its shape, compressed to at most two tiles of spacing", "[formation]") {
    Terrain terrain;
    SECTION("Tight block: offsets preserved exactly") {
        const std::vector<Member> block = {
            makeMember(1, Unit_Tank, 10, 10), makeMember(2, Unit_Tank, 11, 10),
            makeMember(3, Unit_Launcher, 10, 11), makeMember(4, Unit_Launcher, 11, 11),
        };
        const auto result = plan(block, Coord(40, 30), kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
        const Coord base = result.slotFor(1);
        REQUIRE(result.slotFor(2) - base == Coord(1, 0));
        REQUIRE(result.slotFor(3) - base == Coord(0, 1));
        REQUIRE(result.slotFor(4) - base == Coord(1, 1));
        // Centred on the target.
        REQUIRE(tileDistance(base, Coord(40, 30)) <= 1);
    }
    SECTION("Loose group: order kept, gaps closed to two tiles") {
        const std::vector<Member> loose = {
            makeMember(1, Unit_Tank, 10, 10), makeMember(2, Unit_Tank, 14, 10),
            makeMember(3, Unit_Tank, 19, 10), makeMember(4, Unit_Tank, 14, 15),
        };
        const auto result = plan(loose, Coord(40, 30), kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
        const Coord a = result.slotFor(1), b = result.slotFor(2), c = result.slotFor(3), d = result.slotFor(4);
        REQUIRE(b.x - a.x == 2);
        REQUIRE(c.x - b.x == 2);
        REQUIRE(d.x == b.x);
        REQUIRE(d.y - b.y == 2);
        REQUIRE(a.y == b.y);
        REQUIRE(c.y == b.y);
    }
}

TEST_CASE("A scattered group forms a box with armour in front and support at the rear", "[formation]") {
    Terrain terrain;
    const auto members = mixedArmy();
    auto progress = [](const Coord& tile, const Coord& forward) { return tile.x * forward.x + tile.y * forward.y; };
    for (const auto& [target, forward] : {std::pair<Coord, Coord>{Coord(50, 20), Coord(1, 0)},
                                          std::pair<Coord, Coord>{Coord(5, 60), Coord(0, 1)},
                                          std::pair<Coord, Coord>{Coord(45, 60), Coord(1, 1)}}) {
        const auto result = plan(members, target, kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
        int armourBack = INT32_MAX, rangedFront = INT32_MIN, rangedBack = INT32_MAX, supportFront = INT32_MIN;
        for (const auto& member : members) {
            const int p = progress(result.slotFor(member.objectID), forward);
            if (member.role == Role::Armour) armourBack = std::min(armourBack, p);
            if (member.role == Role::Ranged) { rangedFront = std::max(rangedFront, p); rangedBack = std::min(rangedBack, p); }
            if (member.role == Role::Support) supportFront = std::max(supportFront, p);
        }
        INFO("target " << target.x << "," << target.y);
        REQUIRE(armourBack > rangedFront);
        REQUIRE(rangedBack > supportFront);
        // Infantry never stand ahead of the ranged row.
        for (const auto& member : members)
            if (member.role == Role::Infantry)
                REQUIRE(progress(result.slotFor(member.objectID), forward) <= rangedFront);
        // Compact: everything within the box around the target.
        for (const auto& slot : result.slots) REQUIRE(tileDistance(slot.tile, target) <= boxWidth(12));
        requireUnique(result, members);
    }
}

TEST_CASE("Unusable slots fall back to the nearest free usable tile", "[formation]") {
    const std::vector<Member> members = {
        makeMember(1, Unit_Tank, 2, 2), makeMember(2, Unit_Tank, 2, 12), makeMember(3, Unit_Tank, 2, 22),
    };
    Terrain open;
    const auto freePlan = plan(members, Coord(40, 12), kMapSize, kMapSize, open.query(), kInfantryPerTile);
    // Block exactly the tiles the open plan chose, plus a structure footprint around them.
    Terrain blocked;
    for (const auto& slot : freePlan.slots)
        for (int dy = -1; dy <= 1; ++dy) blocked.blocked.insert({slot.tile.x, slot.tile.y + dy});
    const auto result = plan(members, Coord(40, 12), kMapSize, kMapSize, blocked.query(), kInfantryPerTile);
    for (const auto& slot : result.slots) {
        REQUIRE_FALSE(blocked.blocked.count({slot.tile.x, slot.tile.y}));
        // Nearest: one step beside the blocked column at most two tiles away.
        REQUIRE(tileDistance(slot.tile, freePlan.slotFor(slot.objectID)) <= 2);
    }
    requireUnique(result, members);

    // The search is per member: a query that refuses tanks off one tile still accepts others.
    const CanStand noTankOnTarget = [](const Member& member, const Coord& tile) {
        return !(member.itemID == Unit_Tank && tile == Coord(30, 30));
    };
    REQUIRE(nearestStandable(members[0], Coord(30, 30), kMapSize, kMapSize, noTankOnTarget) != Coord(30, 30));
    REQUIRE(tileDistance(nearestStandable(members[0], Coord(30, 30), kMapSize, kMapSize, noTankOnTarget),
                         Coord(30, 30)) == 1);
    Member soldier = makeMember(9, Unit_Soldier, 1, 1);
    REQUIRE(nearestStandable(soldier, Coord(30, 30), kMapSize, kMapSize, noTankOnTarget) == Coord(30, 30));
    // Nothing usable in reach: invalid, and the planner keeps the member where it is.
    const CanStand nowhere = [](const Member&, const Coord&) { return false; };
    REQUIRE(nearestStandable(soldier, Coord(30, 30), kMapSize, kMapSize, nowhere).isInvalid());
    const auto stuck = plan(members, Coord(40, 12), kMapSize, kMapSize, nowhere, kInfantryPerTile);
    for (const auto& member : members) REQUIRE(stuck.slotFor(member.objectID) == member.tile);
}

TEST_CASE("Vehicles get a tile each; infantry share up to the tile capacity", "[formation]") {
    Terrain terrain;
    std::vector<Member> members;
    for (Uint32 i = 0; i < 9; ++i) members.push_back(makeMember(100 + i, Unit_Soldier, 2 + (i % 3) * 5, 2 + (i / 3) * 5, 0.56_fix));
    for (Uint32 i = 0; i < 6; ++i) members.push_back(makeMember(200 + i, Unit_Tank, 30 + i * 3, 40, 1.28_fix));
    const auto result = plan(members, Coord(32, 20), kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
    requireUnique(result, members);
    std::set<std::pair<int, int>> infantryTiles;
    for (const auto& member : members)
        if (member.infantry) infantryTiles.insert({result.slotFor(member.objectID).x, result.slotFor(member.objectID).y});
    // Nine soldiers fit in two shared tiles.
    REQUIRE(infantryTiles.size() == 2);
    // With a capacity of one, every soldier gets its own tile.
    const auto single = plan(members, Coord(32, 20), kMapSize, kMapSize, terrain.query(), 1);
    std::set<std::pair<int, int>> tiles;
    for (const auto& slot : single.slots) tiles.insert({slot.tile.x, slot.tile.y});
    REQUIRE(tiles.size() == members.size());
}

TEST_CASE("Slots at the map edge are clipped into the map", "[formation]") {
    Terrain terrain;
    const auto members = mixedArmy();
    for (const Coord& corner : {Coord(0, 0), Coord(kMapSize - 1, kMapSize - 1), Coord(0, kMapSize - 1)}) {
        const auto result = plan(members, corner, kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
        for (const auto& slot : result.slots) {
            REQUIRE(slot.tile.x >= 0);
            REQUIRE(slot.tile.y >= 0);
            REQUIRE(slot.tile.x < kMapSize);
            REQUIRE(slot.tile.y < kMapSize);
            REQUIRE(tileDistance(slot.tile, corner) <= 2 * boxWidth(12));
        }
        requireUnique(result, members);
    }
}

TEST_CASE("Catch-up: lagging members run at full speed", "[formation][speed]") {
    REQUIRE_FALSE(runsAtFullSpeed(0, 0));
    REQUIRE_FALSE(runsAtFullSpeed(kCatchUpLagTiles, kCatchUpCentroidTiles));
    REQUIRE(runsAtFullSpeed(kCatchUpLagTiles + 1, 0));
    REQUIRE(runsAtFullSpeed(0, kCatchUpCentroidTiles + 1));
    REQUIRE(kCatchUpLagTiles == 5);
    REQUIRE(kCatchUpCentroidTiles == 10);
}

TEST_CASE("Group ids carry the issuing player and a non-zero token", "[formation]") {
    REQUIRE(isValidToken(makeToken(0, 0)));
    REQUIRE(isValidToken(makeToken(0xFFFFFFFFu, 14)));
    REQUIRE_FALSE(isValidToken(0));
    REQUIRE_FALSE(isValidToken(0x1000000u));
    REQUIRE(makeToken(100, 0) != makeToken(100, 1));
    REQUIRE(makeToken(100, 0) != makeToken(101, 0));
    REQUIRE(makeGroupID(3, makeToken(100, 0)) != makeGroupID(4, makeToken(100, 0)));
    REQUIRE((makeGroupID(3, 0x123456u) >> 24) == 3u);
    REQUIRE((makeGroupID(3, 0x123456u) & kTokenMask) == 0x123456u);
}
