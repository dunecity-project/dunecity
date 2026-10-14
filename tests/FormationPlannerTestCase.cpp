/*
 *  FormationPlannerTestCase.cpp
 *
 *  The pure formation planner (include/FormationPlanner.h, docs/formations.md): slot
 *  layout, determinism independent of member order, fallback for unusable slots,
 *  uniqueness, map-edge clipping, the group speed and the one-speed march rule, harvesters
 *  left out, MCVs in the protected centre and the sonic-lane rule.
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

/// Every formation member has a valid slot of its own (infantry share up to the capacity);
/// harvesters have none.
void requireUnique(const Plan& plan, const std::vector<Member>& members) {
    std::map<std::pair<int, int>, int> vehicles, infantry;
    for (const auto& member : members) {
        const Coord tile = plan.slotFor(member.objectID);
        if (!joinsFormations(member.itemID)) {
            REQUIRE(tile.isInvalid());
            continue;
        }
        REQUIRE(tile.isValid());
        (member.infantry ? infantry : vehicles)[{tile.x, tile.y}]++;
    }
    for (const auto& [tile, count] : vehicles) {
        REQUIRE(count == 1);
        REQUIRE(infantry.count(tile) == 0);
    }
    for (const auto& [tile, count] : infantry) REQUIRE(count <= kInfantryPerTile);
}

size_t formationMembers(const std::vector<Member>& members) {
    return static_cast<size_t>(std::count_if(members.begin(), members.end(),
        [](const Member& member) { return joinsFormations(member.itemID); }));
}

} // namespace

TEST_CASE("Formation roles put armour (siege tanks too) in front of ranged units and infantry", "[formation]") {
    REQUIRE(roleOf(Unit_Tank) == Role::Armour);
    REQUIRE(roleOf(Unit_Quad) == Role::Armour);
    REQUIRE(roleOf(Unit_Trike) == Role::Armour);
    REQUIRE(roleOf(Unit_Devastator) == Role::Armour);
    REQUIRE(roleOf(Unit_SonicTank) == Role::Armour);
    REQUIRE(roleOf(Unit_SiegeTank) == Role::Armour);
    REQUIRE(roleOf(Unit_EliteSiegeTank) == Role::Armour);
    REQUIRE(roleOf(Unit_ChemicalSiegeTank) == Role::Armour);
    REQUIRE(roleOf(Unit_Launcher) == Role::Ranged);
    REQUIRE(roleOf(Unit_Deviator) == Role::Ranged);
    REQUIRE(roleOf(Unit_Soldier) == Role::Infantry);
    REQUIRE(roleOf(Unit_Trooper) == Role::Infantry);
    REQUIRE(roleOf(Unit_MCV) == Role::Support);
    // Harvesters never join; MCVs are the protected centre.
    REQUIRE_FALSE(joinsFormations(Unit_Harvester));
    REQUIRE_FALSE(joinsFormations(Unit_RebelHarvester));
    REQUIRE(joinsFormations(Unit_MCV));
    REQUIRE(joinsFormations(Unit_Tank));
    REQUIRE(isProtected(Unit_MCV));
    REQUIRE_FALSE(isProtected(Unit_Tank));
    // Only a sonic unit of the same type may stand in a sonic unit's lane (the damage code
    // spares sonic tanks from sonic tanks and sonic trikes from sonic trikes, nothing else).
    REQUIRE(mayStandInLane(Unit_SonicTank, Unit_SonicTank));
    REQUIRE(mayStandInLane(Unit_SonicTrike, Unit_SonicTrike));
    REQUIRE_FALSE(mayStandInLane(Unit_SonicTank, Unit_SonicTrike));
    REQUIRE_FALSE(mayStandInLane(Unit_SonicTrike, Unit_SonicTank));
    REQUIRE_FALSE(mayStandInLane(Unit_SonicTank, Unit_Tank));
    REQUIRE_FALSE(mayStandInLane(Unit_Tank, Unit_Tank));
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
    REQUIRE(reference.slots.size() == members.size() - 1);    // not the harvester
    REQUIRE(reference.slotFor(41).isInvalid());
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

TEST_CASE("The group speed is the slowest member's, infantry included", "[formation][speed]") {
    Terrain terrain;
    // The soldier (0.56) is the slowest member; the devastator (0.72) the slowest vehicle.
    const auto result = plan(mixedArmy(), Coord(40, 20), kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
    REQUIRE(result.groupSpeed == 0.56_fix);
    REQUIRE(groupPace(mixedArmy()) == 0.56_fix);
    const std::vector<Member> vehicles = {makeMember(1, Unit_Tank, 1, 1, 1.28_fix), makeMember(2, Unit_MCV, 2, 1, 0.64_fix)};
    REQUIRE(groupPace(vehicles) == 0.64_fix);
    REQUIRE(groupPace({}) == 0);
    // A harvester left out of the plan does not set the pace either.
    const std::vector<Member> withHarvester = {makeMember(1, Unit_Tank, 1, 1, 1.28_fix), makeMember(2, Unit_Tank, 2, 1, 1.28_fix),
                                               makeMember(3, Unit_Harvester, 3, 1, 0.50_fix)};
    REQUIRE(plan(withHarvester, Coord(40, 20), kMapSize, kMapSize, terrain.query(), kInfantryPerTile).groupSpeed == 1.28_fix);
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

TEST_CASE("A scattered group forms a box with armour (siege tanks too) in front of ranged units", "[formation]") {
    Terrain terrain;
    const auto members = mixedArmy();
    auto progress = [](const Coord& tile, const Coord& forward) { return tile.x * forward.x + tile.y * forward.y; };
    for (const auto& [target, forward] : {std::pair<Coord, Coord>{Coord(50, 20), Coord(1, 0)},
                                          std::pair<Coord, Coord>{Coord(5, 60), Coord(0, 1)},
                                          std::pair<Coord, Coord>{Coord(45, 60), Coord(1, 1)}}) {
        const auto result = plan(members, target, kMapSize, kMapSize, terrain.query(), kInfantryPerTile);
        int armourBack = INT32_MAX, rangedFront = INT32_MIN;
        for (const auto& member : members) {
            if (!joinsFormations(member.itemID)) continue;
            const int p = progress(result.slotFor(member.objectID), forward);
            if (member.role == Role::Armour) armourBack = std::min(armourBack, p);
            if (member.role == Role::Ranged) rangedFront = std::max(rangedFront, p);
        }
        INFO("target " << target.x << "," << target.y);
        REQUIRE(armourBack > rangedFront);
        // The siege tank stands with the armour, ahead of every launcher.
        REQUIRE(progress(result.slotFor(24), forward) > rangedFront);
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

TEST_CASE("The one-speed march: pace, recover a lost place, wait for a member far behind", "[formation][speed]") {
    REQUIRE(kRecoverLagTiles == 1);
    REQUIRE(kHoldLeadTiles == 2);
    // In formation every member is the same distance from its slot: everyone keeps the pace.
    REQUIRE(marchState(0, 0) == March::Pace);
    REQUIRE(marchState(kRecoverLagTiles, 0) == March::Pace);
    REQUIRE(marchState(0, kHoldLeadTiles) == March::Pace);
    // A member that fell behind runs at full speed to recover its place.
    REQUIRE(marchState(kRecoverLagTiles + 1, 0) == March::Recover);
    // Members ahead of one that fell further behind wait for it - also how a scattered group
    // forms up before it marches: the leaders wait.
    REQUIRE(marchState(0, kHoldLeadTiles + 1) == March::Hold);
    REQUIRE(marchState(kRecoverLagTiles + 1, kHoldLeadTiles + 1) == March::Hold);
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

// ---- Formation shapes and the sonic-lane rule (docs/formations.md) --------------------------

namespace {

/// Large enough that no shape in these tests is clipped by the map edge.
constexpr int kShapeMap = 128;

int progressOf(const Coord& tile, const Coord& forward) { return tile.x * forward.x + tile.y * forward.y; }
int acrossOf(const Coord& tile, const Coord& forward) { return tile.y * forward.x - tile.x * forward.y; }

/// The sonic invariant: for every sonic member, every member ahead of it within its lane is a
/// sonic unit of the same type (mayStandInLane); nothing else ever stands in front of one.
/// Returns how many same-type sonic units stood in a sonic lane.
int requireSonicLanesClear(const Plan& result, const std::vector<Member>& members) {
    int sonicBehindSonic = 0;
    for (const auto& sonic : members) {
        if (!isSonic(sonic.itemID)) continue;
        for (const auto& other : members) {
            if (other.objectID == sonic.objectID || !joinsFormations(other.itemID)) continue;
            INFO("sonic " << sonic.objectID << " at " << result.slotFor(sonic.objectID).x << "," << result.slotFor(sonic.objectID).y
                 << " other " << other.objectID << " (item " << other.itemID << ") at " << result.slotFor(other.objectID).x
                 << "," << result.slotFor(other.objectID).y << " facing " << result.forward.x << "," << result.forward.y);
            if (!inSonicLane(result.slotFor(sonic.objectID), result.slotFor(other.objectID), result.forward)) continue;
            REQUIRE(isSonic(other.itemID));
            REQUIRE(mayStandInLane(sonic.itemID, other.itemID));
            ++sonicBehindSonic;
        }
    }
    return sonicBehindSonic;
}

/// A deterministic pseudo-random sequence (LCG) for the property sweeps.
struct Sequence {
    Uint32 state;
    Uint32 next(Uint32 bound) { state = state * 1664525u + 1013904223u; return (state >> 8) % bound; }
};

/// Twelve units for the shape tests, scattered west of x = 12, moving east.
std::vector<Member> combinedArmy() {
    return {
        makeMember(1, Unit_Tank, 4, 10, 0.80_fix),      makeMember(2, Unit_Tank, 6, 20, 0.80_fix),
        makeMember(3, Unit_Devastator, 3, 30, 0.32_fix), makeMember(4, Unit_Launcher, 5, 12, 1.04_fix),
        makeMember(5, Unit_Launcher, 2, 25, 1.04_fix),  makeMember(6, Unit_SiegeTank, 1, 18, 0.64_fix),
        makeMember(7, Unit_Quad, 9, 15, 1.28_fix),      makeMember(8, Unit_Trike, 8, 28, 1.60_fix),
        makeMember(9, Unit_RaiderTrike, 7, 5, 1.92_fix), makeMember(10, Unit_Quad, 10, 22, 1.28_fix),
        makeMember(11, Unit_Trooper, 7, 22, 0.48_fix),  makeMember(12, Unit_Soldier, 3, 14, 0.24_fix),
    };
}

} // namespace

TEST_CASE("Shapes and unit classes", "[formation][shapes]") {
    REQUIRE(shapeFromIndex(0) == Shape::Box);
    REQUIRE(shapeFromIndex(1) == Shape::Combined);
    REQUIRE(shapeFromIndex(2) == Shape::Line);
    REQUIRE(shapeFromIndex(3) == Shape::Column);
    REQUIRE(shapeFromIndex(4) == Shape::Box);
    REQUIRE(shapeFromIndex(-1) == Shape::Box);
    REQUIRE(std::string(shapeName(Shape::Combined)) == "Combined arms");
    REQUIRE(classOf(Unit_SonicTank) == UnitClass::Sonic);
    REQUIRE(classOf(Unit_SonicTrike) == UnitClass::Sonic);
    REQUIRE(classOf(Unit_Tank) == UnitClass::Heavy);
    REQUIRE(classOf(Unit_Devastator) == UnitClass::Heavy);
    REQUIRE(classOf(Unit_FlameTank) == UnitClass::Heavy);
    REQUIRE(classOf(Unit_Quad) == UnitClass::Light);
    REQUIRE(classOf(Unit_Trike) == UnitClass::Light);
    REQUIRE(classOf(Unit_RaiderTrike) == UnitClass::Light);
    REQUIRE(classOf(Unit_Launcher) == UnitClass::Ranged);
    REQUIRE(classOf(Unit_SiegeTank) == UnitClass::Heavy);
    REQUIRE(classOf(Unit_EliteSiegeTank) == UnitClass::Heavy);
    REQUIRE(classOf(Unit_Deviator) == UnitClass::Ranged);
    REQUIRE(classOf(Unit_RocketTrike) == UnitClass::Ranged);
    REQUIRE(classOf(Unit_Trooper) == UnitClass::Infantry);
    REQUIRE(classOf(Unit_MCV) == UnitClass::Support);
    REQUIRE(scoutCount(2, 1) == 1);
    REQUIRE(scoutCount(7, 5) == 1);
    REQUIRE(scoutCount(8, 5) == 2);
    REQUIRE(scoutCount(16, 5) == 3);
    REQUIRE(scoutCount(30, 2) == 2);
    REQUIRE(scoutCount(30, 0) == 0);
    REQUIRE(scoutDistance(5) == 4);
    REQUIRE(scoutDistance(10) == 5);
    REQUIRE(scoutDistance(25) == 6);
    REQUIRE(columnWidth(3) == 1);
    REQUIRE(columnWidth(12) == 2);
    REQUIRE(columnWidth(20) == 3);
    // Facing codes round-trip all eight steps; 0 is no facing.
    std::set<int> codes;
    for (const Coord& step : {Coord(0, -1), Coord(1, -1), Coord(1, 0), Coord(1, 1),
                              Coord(0, 1), Coord(-1, 1), Coord(-1, 0), Coord(-1, -1)}) {
        REQUIRE(facingCode(step) >= 1);
        REQUIRE(facingCode(step) <= 8);
        REQUIRE(facingFromCode(facingCode(step)) == step);
        codes.insert(facingCode(step));
    }
    REQUIRE(codes.size() == 8);
    REQUIRE(facingCode(Coord(0, 0)) == 0);
    REQUIRE(facingCode(Coord(2, 0)) == 0);
    REQUIRE(facingFromCode(0) == Coord(0, 0));
    REQUIRE(facingFromCode(9) == Coord(0, 0));
}

TEST_CASE("The sonic lane is everything ahead within one formation column", "[formation][sonic]") {
    const Coord east(1, 0), southEast(1, 1);
    const Coord s(10, 10);
    REQUIRE(inSonicLane(s, Coord(11, 10), east));
    REQUIRE(inSonicLane(s, Coord(40, 10), east));     // any distance ahead
    REQUIRE(inSonicLane(s, Coord(12, 11), east));     // the adjacent column
    REQUIRE(inSonicLane(s, Coord(12, 9), east));
    REQUIRE_FALSE(inSonicLane(s, Coord(12, 12), east)); // two columns over
    REQUIRE_FALSE(inSonicLane(s, Coord(10, 11), east)); // level with it
    REQUIRE_FALSE(inSonicLane(s, Coord(9, 10), east));  // behind it
    REQUIRE(inSonicLane(s, Coord(13, 13), southEast));
    REQUIRE(inSonicLane(s, Coord(12, 10), southEast));  // the next diagonal column
    REQUIRE(inSonicLane(s, Coord(10, 12), southEast));
    REQUIRE_FALSE(inSonicLane(s, Coord(13, 10), southEast));
    REQUIRE_FALSE(inSonicLane(s, Coord(11, 9), southEast)); // level (progress 0)
    REQUIRE_FALSE(inSonicLane(s, Coord(9, 9), southEast));
}

TEST_CASE("Combined arms: tanks in front, ranged behind, light vehicles on the flanks, scouts ahead", "[formation][shapes]") {
    Terrain terrain;
    const auto members = combinedArmy();
    for (const auto& [target, forward] : {std::pair<Coord, Coord>{Coord(50, 20), Coord(1, 0)},
                                          std::pair<Coord, Coord>{Coord(5, 60), Coord(0, 1)},
                                          std::pair<Coord, Coord>{Coord(45, 60), Coord(1, 1)}}) {
        INFO("target " << target.x << "," << target.y);
        const auto result = plan(members, target, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined);
        REQUIRE(result.forward == forward);
        requireUnique(result, members);
        // Twelve members: two scouts, the fastest light vehicles (raider trike 1.92, trike 1.60).
        REQUIRE(result.scouts == std::vector<Uint32>{8, 9});
        int heavyBack = INT32_MAX, heavyFront = INT32_MIN, rangedFront = INT32_MIN, rangedBack = INT32_MAX;
        int infantryFront = INT32_MIN, bodyLeft = INT32_MAX, bodyRight = INT32_MIN;
        for (const auto& member : members) {
            const Coord slot = result.slotFor(member.objectID);
            const int p = progressOf(slot, forward);
            const int a = acrossOf(slot, forward);
            switch (classOf(member.itemID)) {
                case UnitClass::Heavy:
                    heavyBack = std::min(heavyBack, p); heavyFront = std::max(heavyFront, p);
                    bodyLeft = std::min(bodyLeft, a); bodyRight = std::max(bodyRight, a); break;
                case UnitClass::Ranged:
                    rangedFront = std::max(rangedFront, p); rangedBack = std::min(rangedBack, p);
                    bodyLeft = std::min(bodyLeft, a); bodyRight = std::max(bodyRight, a); break;
                case UnitClass::Infantry: infantryFront = std::max(infantryFront, p); break;
                default: break;
            }
        }
        REQUIRE(heavyBack > rangedFront);           // tanks and the siege tank in front of the launchers
        REQUIRE(heavyBack == heavyFront);           // all four in the front row
        REQUIRE(progressOf(result.slotFor(6), forward) == heavyFront);     // the siege tank
        REQUIRE(rangedBack > infantryFront);        // infantry at the rear
        for (const auto& member : members) {
            if (classOf(member.itemID) != UnitClass::Light) continue;
            const Coord slot = result.slotFor(member.objectID);
            const bool scout = std::count(result.scouts.begin(), result.scouts.end(), member.objectID) > 0;
            if (scout) {
                // Ahead of the front row by the scout distance (diagonal steps count twice).
                const int ahead = progressOf(slot, forward) - heavyFront;
                const int step = forward.x * forward.x + forward.y * forward.y;
                REQUIRE(ahead == scoutDistance(12) * step);
            } else {
                // Flankers: beside the tanks and launchers, never ahead of the front row.
                const int a = acrossOf(slot, forward);
                REQUIRE((a < bodyLeft || a > bodyRight));
                REQUIRE(progressOf(slot, forward) <= heavyFront);
                REQUIRE(progressOf(slot, forward) >= rangedBack);
            }
        }
    }
}

TEST_CASE("Combined arms sends scouts only when light vehicles exist", "[formation][shapes]") {
    Terrain terrain;
    const std::vector<Member> heavy = {
        makeMember(1, Unit_Tank, 4, 10), makeMember(2, Unit_Tank, 6, 20), makeMember(3, Unit_Launcher, 3, 30),
        makeMember(4, Unit_SiegeTank, 2, 2), makeMember(5, Unit_Soldier, 9, 9),
    };
    REQUIRE(plan(heavy, Coord(40, 20), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined).scouts.empty());
    auto withQuad = heavy;
    withQuad.push_back(makeMember(6, Unit_Quad, 5, 5, 1.28_fix));
    const auto result = plan(withQuad, Coord(40, 20), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined);
    REQUIRE(result.scouts == std::vector<Uint32>{6});
    // The scout is the most advanced member.
    for (const auto& member : heavy)
        REQUIRE(progressOf(result.slotFor(6), result.forward) > progressOf(result.slotFor(member.objectID), result.forward));
    // Thirty units with plenty of light vehicles send three.
    std::vector<Member> large;
    for (Uint32 i = 0; i < 30; ++i)
        large.push_back(makeMember(100 + i, i % 3 == 0 ? Unit_Trike : Unit_Tank, 2 + (i % 6) * 2, 2 + (i / 6) * 3,
                                   i % 3 == 0 ? 1.60_fix : 0.80_fix));
    REQUIRE(plan(large, Coord(50, 40), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined).scouts.size() == 3);
}

TEST_CASE("Line is wide and shallow, Column narrow and deep", "[formation][shapes]") {
    Terrain terrain;
    const auto members = combinedArmy();
    for (const Coord& target : {Coord(50, 20), Coord(30, 60)}) {
        const auto line = plan(members, target, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Line);
        const auto column = plan(members, target, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Column);
        requireUnique(line, members);
        requireUnique(column, members);
        auto extent = [&](const Plan& result, bool along) {
            int lo = INT32_MAX, hi = INT32_MIN;
            for (const auto& slot : result.slots) {
                const int v = along ? progressOf(slot.tile, result.forward) : acrossOf(slot.tile, result.forward);
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
            // A diagonal facing steps two units of progress (or across) per rank (or column).
            const int step = result.forward.x * result.forward.x + result.forward.y * result.forward.y;
            return (hi - lo) / step + 1;
        };
        // Eleven cells (the two infantry share one) abreast in one rank.
        REQUIRE(extent(line, true) == 1);
        REQUIRE(extent(line, false) == 11);
        // Two abreast, six deep.
        REQUIRE(extent(column, false) == 2);
        REQUIRE(extent(column, true) == 6);
        // Armour leads both: the tanks are in the first rank.
        int tankProgress = INT32_MIN;
        for (const auto& member : members)
            if (member.itemID == Unit_Tank)
                tankProgress = std::max(tankProgress, progressOf(column.slotFor(member.objectID), column.forward));
        for (const auto& slot : column.slots) REQUIRE(progressOf(slot.tile, column.forward) <= tankProgress);
    }
    // Wider than kLineMaxWidth: a second rank behind the first.
    std::vector<Member> many;
    for (Uint32 i = 0; i < 20; ++i) many.push_back(makeMember(200 + i, Unit_Tank, 2 + (i % 5), 2 + (i / 5) * 4));
    const auto wide = plan(many, Coord(50, 30), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Line);
    std::set<int> ranks;
    for (const auto& slot : wide.slots) ranks.insert(progressOf(slot.tile, wide.forward));
    REQUIRE(ranks.size() == 2);
}

TEST_CASE("Sonic units take the front row, as one block, with nothing ahead of their lanes", "[formation][sonic]") {
    Terrain terrain;
    auto members = combinedArmy();
    members.push_back(makeMember(13, Unit_SonicTank, 2, 16, 0.80_fix));
    members.push_back(makeMember(14, Unit_SonicTank, 5, 26, 0.80_fix));
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        INFO("shape " << shapeName(shape));
        const auto result = plan(members, Coord(50, 20), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
        requireUnique(result, members);
        requireSonicLanesClear(result, members);
        // Both sonic tanks stand in the most advanced rank apart from scouts, next to each other.
        int front = INT32_MIN;
        for (const auto& member : members)
            if (!std::count(result.scouts.begin(), result.scouts.end(), member.objectID))
                front = std::max(front, progressOf(result.slotFor(member.objectID), result.forward));
        REQUIRE(progressOf(result.slotFor(13), result.forward) == front);
        REQUIRE(progressOf(result.slotFor(14), result.forward) == front);
        REQUIRE(std::abs(acrossOf(result.slotFor(13), result.forward) - acrossOf(result.slotFor(14), result.forward)) == 1);
    }
    // More sonic units than the front is wide: the front row widens to take them all.
    std::vector<Member> sonics;
    for (Uint32 i = 0; i < 5; ++i) sonics.push_back(makeMember(300 + i, Unit_SonicTank, 3, 2 + i * 4));
    sonics.push_back(makeMember(310, Unit_Tank, 1, 10));
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        const auto result = plan(sonics, Coord(40, 20), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
        requireSonicLanesClear(result, sonics);
        std::set<int> ranks;
        for (const auto& member : sonics)
            if (isSonic(member.itemID)) ranks.insert(progressOf(result.slotFor(member.objectID), result.forward));
        REQUIRE(ranks.size() == 1);
    }
}

TEST_CASE("A tight group with a unit in front of a sonic tank forms the box instead", "[formation][sonic]") {
    Terrain terrain;
    // Moving east; the tank stands directly east of (ahead of) the sonic tank.
    const std::vector<Member> group = {
        makeMember(1, Unit_SonicTank, 10, 10), makeMember(2, Unit_Tank, 11, 10), makeMember(3, Unit_Launcher, 10, 11),
    };
    const auto result = plan(group, Coord(40, 10), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile);
    requireSonicLanesClear(result, group);
    // Without a sonic tank the same group keeps its shape.
    auto plain = group;
    plain[0] = makeMember(1, Unit_Tank, 10, 10);
    const auto kept = plan(plain, Coord(40, 10), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile);
    REQUIRE(kept.slotFor(2) - kept.slotFor(1) == Coord(1, 0));
}

TEST_CASE("A sonic tank may stand behind a sonic tank, not behind anything else", "[formation][sonic]") {
    Terrain terrain;
    // Moving east; the second sonic tank stands directly behind the first.
    const std::vector<Member> group = {
        makeMember(1, Unit_SonicTank, 10, 10), makeMember(2, Unit_SonicTank, 9, 10), makeMember(3, Unit_Launcher, 8, 12),
    };
    const auto kept = plan(group, Coord(40, 10), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile);
    REQUIRE(kept.slotFor(2) - kept.slotFor(1) == Coord(-1, 0));
    REQUIRE(requireSonicLanesClear(kept, group) == 1);
    // A sonic trike's wave hurts a sonic tank (and the other way round): that pair forms the box.
    auto mixed = group;
    mixed[1] = makeMember(2, Unit_SonicTrike, 9, 10);
    const auto boxed = plan(mixed, Coord(40, 10), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile);
    REQUIRE(requireSonicLanesClear(boxed, mixed) == 0);
    REQUIRE(boxed.slotFor(2) - boxed.slotFor(1) != Coord(-1, 0));
}

TEST_CASE("Harvesters never get a slot, in any shape or selection", "[formation][harvester]") {
    Terrain terrain;
    const Uint32 items[] = {Unit_Tank, Unit_SiegeTank, Unit_Launcher, Unit_Quad, Unit_Trooper, Unit_SonicTank,
                            Unit_MCV, Unit_Harvester, Unit_RebelHarvester};
    int harvesters = 0;
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        for (int size = 1; size <= 16; ++size) {
            Sequence random{static_cast<Uint32>(size * 977 + static_cast<int>(shape) * 31)};
            std::vector<Member> members;
            for (int i = 0; i < size; ++i) {
                // Every selection has a harvester; the rest is a random mix.
                const Uint32 item = i == 0 ? Unit_Harvester : items[random.next(sizeof(items) / sizeof(items[0]))];
                members.push_back(makeMember(500 + i, item, 4 + static_cast<int>(random.next(12)), 4 + static_cast<int>(random.next(12))));
            }
            const auto result = plan(members, Coord(60, 40), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
            INFO("shape " << shapeName(shape) << " size " << size);
            const size_t joining = formationMembers(members);
            REQUIRE(result.formation == (joining >= 2));
            REQUIRE(result.slots.size() == joining);
            for (const auto& member : members)
                if (!joinsFormations(member.itemID)) { REQUIRE(result.slotFor(member.objectID).isInvalid()); ++harvesters; }
            requireUnique(result, members);
            requireSonicLanesClear(result, members);
        }
    }
    REQUIRE(harvesters >= 4 * 16);
    // One tank selected with two harvesters is a plain move for the tank alone.
    const auto lone = plan({makeMember(1, Unit_Tank, 5, 5), makeMember(2, Unit_Harvester, 6, 5), makeMember(3, Unit_RebelHarvester, 7, 5)},
                           Coord(40, 40), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined);
    REQUIRE_FALSE(lone.formation);
    REQUIRE(lone.slots.size() == 1);
    REQUIRE(lone.slotFor(1) == Coord(40, 40));
}

TEST_CASE("An MCV takes the centre slot, on the target, with its escort around it", "[formation][mcv]") {
    Terrain terrain;
    const Uint32 escortItems[] = {Unit_Tank, Unit_SiegeTank, Unit_Launcher, Unit_Quad, Unit_Trike, Unit_Trooper, Unit_SonicTank};
    const Coord facings[] = {Coord(1, 0), Coord(1, 1), Coord(0, 1), Coord(-1, 1),
                             Coord(-1, 0), Coord(-1, -1), Coord(0, -1), Coord(1, -1)};
    int plans = 0;
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        for (int escorts = 2; escorts <= 20; ++escorts) {
            for (int f = 0; f < 8; ++f) {
                Sequence random{static_cast<Uint32>(escorts * 7907 + f * 613 + static_cast<int>(shape) * 17)};
                const Coord start(64, 64);
                const Coord target(start.x + facings[f].x * 25, start.y + facings[f].y * 25);
                std::vector<Member> members = {makeMember(1, Unit_MCV, start.x, start.y, 0.64_fix)};
                for (int i = 0; i < escorts; ++i)
                    members.push_back(makeMember(10 + i, escortItems[random.next(sizeof(escortItems) / sizeof(escortItems[0]))],
                                                 start.x - 5 + static_cast<int>(random.next(11)),
                                                 start.y - 5 + static_cast<int>(random.next(11))));
                const auto result = plan(members, target, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
                INFO("shape " << shapeName(shape) << " escorts " << escorts << " facing " << facings[f].x << "," << facings[f].y);
                REQUIRE(result.formation);
                requireUnique(result, members);
                requireSonicLanesClear(result, members);
                // The MCV stands on the target itself (its deploy site, for a colonist).
                const Coord mcv = result.slotFor(1);
                REQUIRE(mcv == target);
                int left = 0, right = 0, ahead = 0, behind = 0;
                std::set<int> ranks;
                std::set<int> rowMates;     // across positions of the other tiles in the MCV's row
                for (const auto& member : members) {
                    if (member.objectID == 1 || std::count(result.scouts.begin(), result.scouts.end(), member.objectID)) continue;
                    const Coord slot = result.slotFor(member.objectID);
                    const int across = acrossOf(slot, result.forward) - acrossOf(mcv, result.forward);
                    const int progress = progressOf(slot, result.forward) - progressOf(mcv, result.forward);
                    ahead += progress > 0 ? 1 : 0;
                    behind += progress < 0 ? 1 : 0;
                    ranks.insert(progress);
                    if (progress == 0) {
                        rowMates.insert(across);
                        left += across < 0 ? 1 : 0;
                        right += across > 0 ? 1 : 0;
                    }
                }
                // The middle row: escort ahead of the MCV once the escort stands in two ranks or
                // more, and behind it as well from three - never the MCV alone at the rear.
                if (ranks.size() >= 2) REQUIRE(ahead > 0);
                if (ranks.size() >= 3) REQUIRE(behind > 0);
                // In the middle of its row: row mates on both sides of it, except in the narrow column.
                if (shape != Shape::Column && rowMates.size() >= 2) {
                    REQUIRE(left > 0);
                    REQUIRE(right > 0);
                }
                ++plans;
            }
        }
    }
    REQUIRE(plans == 4 * 19 * 8);
    // The colonist and two escort tanks (QuantBot): a tank either side of the MCV.
    const std::vector<Member> convoy = {makeMember(1, Unit_MCV, 10, 10, 0.64_fix), makeMember(2, Unit_Tank, 12, 9, 0.80_fix),
                                        makeMember(3, Unit_Tank, 9, 12, 0.80_fix)};
    const auto escorted = plan(convoy, Coord(50, 12), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined);
    REQUIRE(escorted.slotFor(1) == Coord(50, 12));
    REQUIRE(std::set<int>{escorted.slotFor(2).y, escorted.slotFor(3).y} == std::set<int>{11, 13});
    REQUIRE(escorted.slotFor(2).x == 50);
    REQUIRE(escorted.slotFor(3).x == 50);
    REQUIRE(escorted.groupSpeed == 0.64_fix);
}

TEST_CASE("Form-up: the shape gathers at the group, level with its foremost member", "[formation][formup]") {
    Terrain terrain;
    auto members = combinedArmy();
    members.push_back(makeMember(13, Unit_SonicTank, 2, 16, 0.80_fix));
    members.push_back(makeMember(14, Unit_MCV, 6, 9, 0.64_fix));
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        INFO("shape " << shapeName(shape));
        const auto result = plan(members, Coord(60, 18), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
        std::vector<Coord> slots;
        for (const auto& member : members) slots.push_back(result.slotFor(member.objectID));
        const auto formUp = formUpTiles(members, slots, result.forward, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile);
        REQUIRE(formUp.size() == members.size());
        // On open ground the shape is translated as a whole: one displacement for everyone.
        const Coord shift = slots[0] - formUp[0];
        int foremostMember = INT32_MIN, foremostFormUp = INT32_MIN;
        for (size_t i = 0; i < members.size(); ++i) {
            REQUIRE(slots[i] - formUp[i] == shift);
            foremostMember = std::max(foremostMember, progressOf(members[i].tile, result.forward));
            foremostFormUp = std::max(foremostFormUp, progressOf(formUp[i], result.forward));
        }
        // At least level with the foremost member (the shape centred on the group may already
        // reach further): nobody walks back to form up, the leaders wait.
        REQUIRE(foremostFormUp >= foremostMember);
        REQUIRE(tileDistance(shift, Coord(0, 0)) >= kFormUpMinMarch);
        // The form-up keeps the sonic rule too.
        Plan gathered = result;
        for (size_t i = 0; i < members.size(); ++i) gathered.slots[i] = {members[i].objectID, formUp[i]};
        std::sort(gathered.slots.begin(), gathered.slots.end(), [](const Slot& a, const Slot& b) { return a.objectID < b.objectID; });
        requireSonicLanesClear(gathered, members);
        // Independent of the members' order.
        auto reversed = members;
        auto reversedSlots = slots;
        std::reverse(reversed.begin(), reversed.end());
        std::reverse(reversedSlots.begin(), reversedSlots.end());
        auto again = formUpTiles(reversed, reversedSlots, result.forward, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile);
        std::reverse(again.begin(), again.end());
        REQUIRE(again == formUp);
    }
    // A short march goes straight to the slots.
    const std::vector<Member> near = {makeMember(1, Unit_Tank, 10, 10), makeMember(2, Unit_Tank, 10, 12), makeMember(3, Unit_Tank, 9, 11)};
    const auto shortPlan = plan(near, Coord(16, 11), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined);
    std::vector<Coord> shortSlots;
    for (const auto& member : near) shortSlots.push_back(shortPlan.slotFor(member.objectID));
    REQUIRE(formUpTiles(near, shortSlots, shortPlan.forward, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile).empty());
    // Blocked ground: form-up tiles are usable and unique.
    Terrain rough;
    for (int y = 0; y < kShapeMap; ++y)
        for (int x = 4; x < 12; ++x)
            if ((x + y) % 3 == 0) rough.blocked.insert({x, y});
    const auto roughPlan = plan(members, Coord(60, 18), kShapeMap, kShapeMap, rough.query(), kInfantryPerTile, Shape::Combined);
    std::vector<Coord> roughSlots;
    for (const auto& member : members) roughSlots.push_back(roughPlan.slotFor(member.objectID));
    const auto roughFormUp = formUpTiles(members, roughSlots, roughPlan.forward, kShapeMap, kShapeMap, rough.query(), kInfantryPerTile);
    REQUIRE(roughFormUp.size() == members.size());
    Plan roughGathered = roughPlan;
    for (size_t i = 0; i < members.size(); ++i) {
        REQUIRE_FALSE(rough.blocked.count({roughFormUp[i].x, roughFormUp[i].y}));
        roughGathered.slots[i] = {members[i].objectID, roughFormUp[i]};
    }
    std::sort(roughGathered.slots.begin(), roughGathered.slots.end(), [](const Slot& a, const Slot& b) { return a.objectID < b.objectID; });
    requireUnique(roughGathered, members);
    requireSonicLanesClear(roughGathered, members);
}

TEST_CASE("Siege tanks stand with the front-line armour in every shape", "[formation][shapes]") {
    Terrain terrain;
    const std::vector<Member> members = {
        makeMember(1, Unit_Tank, 4, 10, 0.80_fix),      makeMember(2, Unit_Tank, 6, 20, 0.80_fix),
        makeMember(3, Unit_SiegeTank, 2, 14, 0.64_fix), makeMember(4, Unit_SiegeTank, 3, 24, 0.64_fix),
        makeMember(5, Unit_Launcher, 9, 12, 1.04_fix),  makeMember(6, Unit_Launcher, 8, 18, 1.04_fix),
        makeMember(7, Unit_Quad, 9, 15, 1.28_fix),      makeMember(8, Unit_Trooper, 7, 22, 0.48_fix),
    };
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        INFO("shape " << shapeName(shape));
        const auto result = plan(members, Coord(50, 18), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
        int launcherFront = INT32_MIN, front = INT32_MIN;
        for (const auto& member : members) {
            const int p = progressOf(result.slotFor(member.objectID), result.forward);
            if (member.itemID == Unit_Launcher) launcherFront = std::max(launcherFront, p);
            if (!std::count(result.scouts.begin(), result.scouts.end(), member.objectID)) front = std::max(front, p);
        }
        for (const Uint32 siege : {3u, 4u}) {
            const int p = progressOf(result.slotFor(siege), result.forward);
            // The Line is one rank; elsewhere the siege tanks stand ahead of every launcher.
            if (shape != Shape::Line) REQUIRE(p > launcherFront);
            // Combined arms puts every tank and siege tank in the front row itself.
            if (shape == Shape::Combined || shape == Shape::Line) REQUIRE(p == front);
        }
    }
}

TEST_CASE("Every shape is identical whatever order the members are listed in", "[formation][determinism][shapes]") {
    Terrain terrain;
    terrain.blocked = {{40, 20}, {41, 20}, {40, 21}, {44, 18}, {44, 22}};
    auto members = combinedArmy();
    members.push_back(makeMember(13, Unit_SonicTank, 2, 16, 0.80_fix));
    members.push_back(makeMember(14, Unit_MCV, 6, 9, 0.64_fix));
    members.push_back(makeMember(15, Unit_Harvester, 4, 19, 0.64_fix));
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        const auto reference = plan(members, Coord(40, 20), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
        auto shuffled = members;
        for (int round = 0; round < 24; ++round) {
            std::rotate(shuffled.begin(), shuffled.begin() + 1 + round % 5, shuffled.end());
            if (round % 2) std::reverse(shuffled.begin(), shuffled.end());
            const auto again = plan(shuffled, Coord(40, 20), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
            REQUIRE(slotsOf(again) == slotsOf(reference));
            REQUIRE(again.scouts == reference.scouts);
            REQUIRE(again.forward == reference.forward);
        }
    }
}

TEST_CASE("Sonic lanes stay clear for every shape, group size, facing and obstacle field", "[formation][sonic][property]") {
    constexpr int kSweepMap = 128;
    const Uint32 items[] = {Unit_SonicTank, Unit_SonicTrike, Unit_Tank, Unit_Devastator, Unit_Quad, Unit_Trike,
                            Unit_RaiderTrike, Unit_Launcher, Unit_SiegeTank, Unit_Deviator, Unit_Trooper,
                            Unit_Soldier, Unit_Harvester, Unit_MCV, Unit_SonicTank, Unit_SonicTrike};
    const Coord facings[] = {Coord(1, 0), Coord(1, 1), Coord(0, 1), Coord(-1, 1),
                             Coord(-1, 0), Coord(-1, -1), Coord(0, -1), Coord(1, -1)};
    int plans = 0, relocated = 0, sonicBehindSonic = 0;
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        for (int size = 2; size <= 30; ++size) {
            for (int f = 0; f < 8; ++f) {
                for (int obstacles = 0; obstacles < 2; ++obstacles) {
                    Sequence random{static_cast<Uint32>(size * 7919 + f * 104729 + obstacles * 31 + static_cast<int>(shape) * 13)};
                    const Coord start(64, 64);
                    const Coord target(start.x + facings[f].x * 25, start.y + facings[f].y * 25);
                    std::vector<Member> members;
                    bool sonic = false;
                    for (int i = 0; i < size; ++i) {
                        // At least one sonic unit; otherwise a random mix.
                        const Uint32 item = i == 0 ? Unit_SonicTank : items[random.next(sizeof(items) / sizeof(items[0]))];
                        sonic = sonic || isSonic(item);
                        members.push_back(makeMember(1000 + random.next(9000) * 32 + i, item,
                                                     start.x - 8 + static_cast<int>(random.next(17)),
                                                     start.y - 8 + static_cast<int>(random.next(17)),
                                                     FixPoint(1 + random.next(3))));
                    }
                    REQUIRE(sonic);
                    Terrain terrain;
                    if (obstacles) {
                        // About a fifth of the ground around the target is blocked.
                        for (int y = target.y - 10; y <= target.y + 10; ++y)
                            for (int x = target.x - 10; x <= target.x + 10; ++x)
                                if (random.next(5) == 0) terrain.blocked.insert({x, y});
                    }
                    const auto result = plan(members, target, kSweepMap, kSweepMap, terrain.query(), kInfantryPerTile, shape);
                    INFO("shape " << shapeName(shape) << " size " << size << " facing " << facings[f].x << "," << facings[f].y
                         << " obstacles " << obstacles);
                    REQUIRE(result.formation == (formationMembers(members) >= 2));
                    requireUnique(result, members);
                    sonicBehindSonic += requireSonicLanesClear(result, members);
                    // On open ground an MCV stands on the target.
                    if (!obstacles && result.formation) {
                        bool mcv = false, onTarget = false;
                        for (const auto& member : members)
                            if (member.itemID == Unit_MCV) { mcv = true; onTarget = onTarget || result.slotFor(member.objectID) == target; }
                        REQUIRE(onTarget == mcv);
                    }
                    for (const auto& slot : result.slots) {
                        REQUIRE(slot.tile.x >= 0);
                        REQUIRE(slot.tile.y >= 0);
                        REQUIRE(slot.tile.x < kSweepMap);
                        REQUIRE(slot.tile.y < kSweepMap);
                        REQUIRE_FALSE(terrain.blocked.count({slot.tile.x, slot.tile.y}));
                    }
                    if (obstacles) {
                        const auto open = plan(members, target, kSweepMap, kSweepMap, Terrain().query(), kInfantryPerTile, shape);
                        relocated += slotsOf(open) != slotsOf(result) ? 1 : 0;
                    }
                    ++plans;
                }
            }
        }
    }
    REQUIRE(plans == 4 * 29 * 8 * 2);
    // The obstacle fields really forced fallbacks.
    REQUIRE(relocated > 4 * 29 * 8 / 2);
    // Relocations around obstacles do put sonic units behind sonic units of their own type.
    REQUIRE(sonicBehindSonic > 0);
}

// ---- Form-up in place: the formation buttons and hotkeys (docs/formations.md) ---------------

TEST_CASE("Form-up in place: the shape a move would take, laid out where the group stands", "[formation][formup][inplace]") {
    Terrain terrain;
    auto members = combinedArmy();
    members.push_back(makeMember(13, Unit_SonicTank, 2, 16, 0.80_fix));
    members.push_back(makeMember(14, Unit_SonicTank, 6, 17, 0.80_fix));
    const Coord centroid = centroidOf(members);
    REQUIRE(centroid == Coord(5, 18));
    const Coord facings[] = {Coord(1, 0), Coord(1, 1), Coord(0, 1), Coord(-1, 1),
                             Coord(-1, 0), Coord(-1, -1), Coord(0, -1), Coord(1, -1)};
    // Far enough on the big map that nothing is clipped, here and at the move's target.
    std::vector<Member> placed = members;
    for (auto& member : placed) member.tile += Coord(50, 50);
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column}) {
        for (const Coord& forward : facings) {
            INFO("shape " << shapeName(shape) << " facing " << forward.x << "," << forward.y);
            const auto here = planInPlace(placed, forward, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
            REQUIRE(here.formation);
            REQUIRE(here.forward == forward);
            REQUIRE(here.slots.size() == placed.size());
            requireUnique(here, placed);
            requireSonicLanesClear(here, placed);
            // The same slots as a move 30 tiles that way, moved back onto the group: the group
            // is scattered, so the move lays the shape out too.
            const Coord shift(forward.x * 30, forward.y * 30);
            const auto moved = plan(placed, centroidOf(placed) + shift, kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape);
            REQUIRE(moved.forward == forward);
            for (const auto& member : placed)
                REQUIRE(here.slotFor(member.objectID) == moved.slotFor(member.objectID) - shift);
            REQUIRE(here.scouts == moved.scouts);
        }
    }
}

TEST_CASE("Form-up in place: a tight group forms the box instead of keeping its shape", "[formation][formup][inplace]") {
    Terrain terrain;
    const std::vector<Member> diagonal = {makeMember(1, Unit_Tank, 10, 10), makeMember(2, Unit_Tank, 11, 11),
                                          makeMember(3, Unit_Tank, 12, 12), makeMember(4, Unit_Tank, 13, 13)};
    // A move keeps the diagonal...
    const auto moved = plan(diagonal, Coord(11, 40), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Box);
    REQUIRE(moved.slotFor(4) - moved.slotFor(1) == Coord(3, 3));
    // ...the Box button forms the box: three abreast, one behind, facing north.
    const auto here = planInPlace(diagonal, Coord(0, -1), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Box);
    std::map<int, int> perRow;
    for (const auto& slot : here.slots) perRow[slot.tile.y]++;
    std::multiset<int> rowSizes;
    for (const auto& [row, count] : perRow) rowSizes.insert(count);
    REQUIRE(rowSizes == std::multiset<int>{1, 3});
    // Around the centroid (11, 11).
    for (const auto& slot : here.slots) REQUIRE(tileDistance(slot.tile, Coord(11, 11)) <= 2);
}

TEST_CASE("Form-up in place: the anchor, the MCV, harvesters, small groups and the facing fallback", "[formation][formup][inplace]") {
    Terrain terrain;
    std::vector<Member> convoy = {makeMember(1, Unit_MCV, 20, 20, 0.64_fix), makeMember(2, Unit_Tank, 18, 22),
                                  makeMember(3, Unit_Tank, 24, 21), makeMember(4, Unit_Harvester, 30, 30),
                                  makeMember(5, Unit_Launcher, 20, 25)};
    // The harvester is not part of the group: the centroid is the other four's, (20, 22).
    REQUIRE(centroidOf(convoy) == Coord(20, 22));
    const auto open = planInPlace(convoy, Coord(1, 0), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined);
    REQUIRE(open.formation);
    REQUIRE(open.slotFor(4).isInvalid());
    REQUIRE(open.slotFor(1) == Coord(20, 22));     // the MCV stands on the anchor
    REQUIRE(open.groupSpeed == 0.64_fix);
    // The centroid is a rock spire: the anchor is the nearest tile the MCV may stand on.
    terrain.blocked = {{20, 22}};
    const auto blocked = planInPlace(convoy, Coord(1, 0), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Combined);
    const Coord anchor = blocked.slotFor(1);
    REQUIRE(anchor != Coord(20, 22));
    REQUIRE(tileDistance(anchor, Coord(20, 22)) == 1);
    requireUnique(blocked, convoy);
    for (const auto& slot : blocked.slots) REQUIRE_FALSE(terrain.blocked.count({slot.tile.x, slot.tile.y}));
    // No facing (or not a unit step) faces north.
    terrain.blocked.clear();
    REQUIRE(planInPlace(convoy, Coord(0, 0), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Line).forward == Coord(0, -1));
    REQUIRE(planInPlace(convoy, Coord(3, 0), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Line).forward == Coord(0, -1));
    // One tank and a harvester: nothing to form, everyone keeps its tile.
    const auto lone = planInPlace({makeMember(1, Unit_Tank, 5, 5), makeMember(2, Unit_Harvester, 6, 5)}, Coord(1, 0),
                                  kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, Shape::Box);
    REQUIRE_FALSE(lone.formation);
    REQUIRE(lone.slots.size() == 1);
    REQUIRE(lone.slotFor(1) == Coord(5, 5));
    // Independent of the members' order.
    auto shuffled = convoy;
    std::reverse(shuffled.begin(), shuffled.end());
    for (const Shape shape : {Shape::Box, Shape::Combined, Shape::Line, Shape::Column})
        REQUIRE(slotsOf(planInPlace(shuffled, Coord(-1, 1), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape))
                == slotsOf(planInPlace(convoy, Coord(-1, 1), kShapeMap, kShapeMap, terrain.query(), kInfantryPerTile, shape)));
}
