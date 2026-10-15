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

#ifndef FORMATIONPLANNER_H
#define FORMATIONPLANNER_H

#include <DataTypes.h>
#include <fixmath/FixPoint.h>

#include <functional>
#include <vector>

/**
    Formation movement planner (docs/formations.md).

    A pure, deterministic function from a group of ground units and a target tile to one
    destination tile per unit plus the group speed. It reads nothing but its arguments: the
    map is reached only through the CanStand query, so every peer that passes the same
    members, target and map answers computes the identical plan, whatever order the members
    were listed in. Integer arithmetic only.

    Units still path individually; the planner only chooses where each one goes and how fast
    the group travels (UnitBase formation membership applies the speed cap).
*/
namespace FormationPlanner {

/// Formation rank of a unit type. Lower ranks stand further forward.
enum class Role : Uint8 {
    Armour = 0,     ///< tanks, siege tanks, quads, trikes, devastators, sonic tanks: the front rows
    Ranged = 1,     ///< launchers, deviators, rocket trikes: behind the armour
    Infantry = 2,   ///< soldiers, troopers, saboteurs: flanks of the last row, then the back
    Support = 3     ///< MCVs: the protected centre (harvesters never join a formation)
};

/// The formation role of a unit type (the Box shape's rows).
Role roleOf(Uint32 itemID);

/// May a unit of this type be a formation member? Every ground unit except harvesters, which
/// keep their ordinary orders when they are selected or sent with a group.
bool joinsFormations(Uint32 itemID);

/// Is this unit protected by its formation: the centre slot (the target itself), with the
/// rest of the group laid out around it? MCVs.
bool isProtected(Uint32 itemID);

/**
    The shapes a group can be ordered into (docs/formations.md, "Formation research": the five
    kept of ten candidates). Every shape faces the direction from the group's centroid to the
    target and keeps the sonic-lane rule. The values are stable ids, stored in the settings
    ("Formation Shape") and as the QuantBot WaveFormation: Box and Column keep their released
    ids; 1, 2, 4, 5 and 8 belonged to shapes that were removed (Combined arms, Line, Wedge, Vee,
    Diamond) and read back as the Box (shapeFromId).
*/
enum class Shape : Uint8 {
    Box = 0,        ///< rows by role: armour, ranged, infantry; MCVs in the centre (the default)
    Column = 3,     ///< narrow and deep, for passes and bridges
    Echelon = 6,    ///< a slanted front: the left flank leads, each file further right trails back
    Staggered = 7,  ///< a loose box: one tile between neighbours, alternate ranks offset (less splash)
    Pincer = 9      ///< two wings with a gap between them, MCVs in the gap
};
constexpr int kShapeCount = 5;
/// The shapes in panel and Ctrl+Shift+F order.
constexpr Shape kShapes[kShapeCount] = {Shape::Box, Shape::Pincer, Shape::Column, Shape::Staggered, Shape::Echelon};
/// The shape with this stable id; a removed or unknown id is the Box.
Shape shapeFromId(int id);
/// The shape at this panel position (0 to kShapeCount - 1); anything else is the Box.
Shape shapeAt(int position);
/// The panel position of a shape.
int positionOf(Shape shape);
/// English name for the interface ("Box", "Pincer", "Column", "Staggered", "Echelon").
const char* shapeName(Shape shape);

/// Finer unit class used by the shapes other than the Box.
enum class UnitClass : Uint8 {
    Sonic = 0,      ///< sonic tank, sonic trike: their wave hits friends, so nothing stands ahead
    Heavy = 1,      ///< tanks, siege tanks, devastators, flame tanks and other armour: the front
    Light = 2,      ///< quads, trikes, raider trikes
    Ranged = 3,     ///< launchers, deviators, rocket trikes: behind the front
    Infantry = 4,
    Support = 5     ///< MCVs: the protected centre
};
UnitClass classOf(Uint32 itemID);
/// Does this unit fire a sonic wave that damages everything in its lane, friends included?
bool isSonic(Uint32 itemID);

/**
    May a unit of type 'otherItemID' stand in the lane of a sonic unit of type 'sonicItemID'?
    Only another sonic unit of the same type: SonicTank::handleDamage ignores damage whose
    shooter is a sonic tank and SonicTrike::handleDamage damage whose shooter is a sonic
    trike, but a sonic tank's wave hurts a sonic trike and the other way round, and both hurt
    every other unit (Bullet::update, Map::damage).
*/
bool mayStandInLane(Uint32 sonicItemID, Uint32 otherItemID);

/**
    The sonic-lane rule: is 'other' in the firing lane ahead of a sonic unit standing on
    'sonic', for a formation facing 'forward' (one of the eight unit steps)? The lane is
    every tile strictly ahead (positive progress along 'forward') whose lateral offset is at
    most one formation column: 1 tile for a straight facing, the diagonal column pitch
    (|dx - dy| or |dx + dy| <= 2) for a diagonal one.
*/
bool inSonicLane(const Coord& sonic, const Coord& other, const Coord& forward);

/// Echelon: at most this many cells abreast in each slanted rank.
constexpr int kEchelonMaxWidth = 12;
/// Column: cells abreast for a group with this many cells (1 to 3).
int columnWidth(int cellCount);

/// Facing codes, carried by formation commands and saved per member: 0 for none, 1-8 for
/// the eight unit steps clockwise from north (0,-1).
Uint8 facingCode(const Coord& forward);
/// The unit step of a facing code; (0,0) for 0 or an unknown code.
Coord facingFromCode(Uint8 code);

struct Member {
    Uint32 objectID = 0;
    Uint32 itemID = 0;
    Coord tile;                 ///< where the unit stands now
    Role role = Role::Armour;
    FixPoint speed = 0;         ///< reference speed (UnitBase::getFormationReferenceSpeed)
    bool infantry = false;      ///< may share its destination tile with other infantry
};

struct Slot {
    Uint32 objectID = 0;
    Coord tile;
};

struct Plan {
    std::vector<Slot> slots;    ///< one per member, sorted by objectID
    FixPoint groupSpeed = 0;    ///< groupPace(): the slowest member's speed
    bool formation = false;     ///< false for fewer than two members: plain move to the target
    Coord forward = Coord(0, -1);   ///< the facing the shape was laid out for (formation plans)

    /// Destination of objectID, or Coord::Invalid() if it is not a member.
    Coord slotFor(Uint32 objectID) const;
};

/**
    May this member stand on this tile as a destination? Terrain and structures only (the
    member's movement class); other units never make a tile unusable, they move. Only called
    for tiles inside the map.
*/
using CanStand = std::function<bool(const Member& member, const Coord& tile)>;

/// A group is coherent when every member is within this many tiles of the group centroid.
constexpr int kCoherentRadius = 6;
/// Coherent groups keep their shape, with gaps between neighbours shrunk to at most this.
constexpr int kMaxCompressedSpacing = 2;
/// The march: a member more than this many tiles further from its slot than the group's
/// leading member runs at full speed to recover its place.
constexpr int kRecoverLagTiles = 1;
/// The march: a member more than this many tiles ahead of the group's last member waits.
constexpr int kHoldLeadTiles = 2;
/// A march shorter than this many tiles (after the form-up) goes straight to the slots.
constexpr int kFormUpMinMarch = 8;
/// The fallback search for a free tile looks at most this far from the wanted slot.
constexpr int kMaxSearchRadius = 32;

/// Chebyshev tile distance.
int tileDistance(const Coord& a, const Coord& b);

/// Box width for n members: ceil(sqrt(n * 1.5)).
int boxWidth(int memberCount);

/// One of the eight unit steps pointing from 'from' towards 'to'; (0,0) if they are equal.
Coord facing(const Coord& from, const Coord& to);

/**
    Plans a formation move.
    \param  members         the group, in any order (object ids must be distinct); harvesters
                            (joinsFormations) are left out of the plan
    \param  target          the ordered destination tile (inside the map)
    \param  mapWidth        map width in tiles
    \param  mapHeight       map height in tiles
    \param  canStand        terrain/structure query, see CanStand
    \param  infantryPerTile how many infantry may share one destination tile (at least 1)
    \param  shape           the formation shape; the Box keeps a coherent group's own shape
    \return the plan; with fewer than two members every member is sent to the target. For a
            formation, every unit in a sonic member's lane (inSonicLane, plan.forward) is one
            that mayStandInLane, and an MCV (isProtected) takes the target itself in the
            middle row of the shape, as long as every member finds a usable tile within
            kMaxSearchRadius.
*/
Plan plan(std::vector<Member> members, const Coord& target, int mapWidth, int mapHeight,
          const CanStand& canStand, int infantryPerTile, Shape shape = Shape::Box);

/**
    Forms a group up where it stands (the formation buttons and hotkeys, docs/formations.md).
    The shape is laid out around the anchor - the members' centroid (centroidOf), or the
    nearest tile there the anchor member may stand on (the MCV, else the vehicle, else the
    member with the lowest object id) - with its front towards 'forward'. It is always the
    shape itself: a tight Box group forms the box instead of keeping its own shape. Every
    other rule is plan()'s: harvesters left out, the MCV on the anchor, sonic lanes clear,
    slots resolved the same way. Pure and independent of the members' order.
    \param  forward one of the eight unit steps; anything else faces north (0,-1)
    \return the plan; with fewer than two members formation is false and every member keeps
            its own tile
*/
Plan planInPlace(std::vector<Member> members, const Coord& forward, int mapWidth, int mapHeight,
                 const CanStand& canStand, int infantryPerTile, Shape shape);

/// The integer (floor) centroid of the members that join formations; invalid for none.
Coord centroidOf(const std::vector<Member>& members);

/**
    The pace a formation keeps: its slowest member's reference speed, infantry included, so
    the whole block moves at one speed. 0 for no members.
*/
FixPoint groupPace(const std::vector<Member>& members);

/**
    Form-up (docs/formations.md): where each member gathers before the march, so the group
    sets off in its shape. The shape (the slots) is translated back onto the group: centred on
    the members' centroid, then moved forward until its foremost slot is level with the
    foremost member (never past the target), so the leaders wait while the others come up.
    Each wanted tile is resolved like a slot (usable, unique, clear of sonic lanes). Pure and
    independent of the members' order.
    \param  members     the group; members[i] heads for slots[i]
    \param  forward     the formation's facing (Plan::forward)
    \return one form-up tile per member, or nothing when the march from there would be
            shorter than kFormUpMinMarch (the members go straight to their slots)
*/
std::vector<Coord> formUpTiles(const std::vector<Member>& members, const std::vector<Coord>& slots, const Coord& forward,
                               int mapWidth, int mapHeight, const CanStand& canStand, int infantryPerTile);

/**
    Nearest tile to 'wanted' that is inside the map and on which the member may stand, by a
    deterministic outward ring search (Chebyshev rings, then squared distance, then ring
    order). Returns 'wanted' itself if it qualifies and Coord::Invalid() if nothing within
    kMaxSearchRadius does.
*/
Coord nearestStandable(const Member& member, const Coord& wanted, int mapWidth, int mapHeight,
                       const CanStand& canStand);

/// What a formation member does on this cycle's march (FormationOrders::update()).
enum class March : Uint8 {
    Pace = 0,       ///< moves at the group's pace
    Recover = 1,    ///< fell behind its place: its own full speed until it is back
    Hold = 2        ///< ahead of a member that fell behind: waits for it
};

/**
    The one-speed march (docs/formations.md). Every member starts in its place relative to
    its slot, so in formation all members are the same distance from their slots. A member
    more than kHoldLeadTiles closer to its slot than the group's last member waits for it
    (this is also how a scattered group forms up: the leaders wait); otherwise a member more
    than kRecoverLagTiles further from its slot than the leading member runs at full speed
    to recover its place; everyone else keeps the pace.
    \param  behindLeader    own remaining tiles minus the group's smallest remaining tiles
    \param  aheadOfLast     the group's largest remaining tiles minus own remaining tiles
*/
March marchState(int behindLeader, int aheadOfLast);

// ---- Fighting in formation (docs/formations.md): the engagement rules of a hunting formation.
/// An enemy unit this close to a member breaks the formation into free fights.
constexpr int kEngageCloseTiles = 2;
/// The formation breaks once it has lost this fraction (numerator / denominator) of the
/// members it had when the engagement began.
constexpr int kEngageLossNumerator = 1;
constexpr int kEngageLossDenominator = 3;
/// Hit for this long without a member having anything in reach (outranged): break.
constexpr Uint32 kEngageOutrangedMs = 8000;
/// An engagement breaks into free fights at the latest this long after it began.
constexpr Uint32 kEngageMaxMs = 30000;
/// Nothing in reach and no hit for this long: the formation marches on.
constexpr Uint32 kEngageQuietMs = 3000;

/// Why an engagement ended (engageStep).
enum class EngageEnd : Uint8 {
    None = 0,       ///< still engaged
    Resume = 1,     ///< nothing left in reach: the formation marches on
    Close = 2,      ///< an enemy unit within kEngageCloseTiles of a member
    Losses = 3,     ///< a third of the members lost
    Outranged = 4,  ///< hit from beyond every member's reach
    Quiet = 5,      ///< kEngageOutrangedMs of hits with nothing in reach
    Timeout = 6,    ///< kEngageMaxMs
    Count = 7
};

/// One engaged formation as the rules see it (all cycles are game cycles).
struct EngageView {
    int size = 0;               ///< members when the engagement began
    int members = 0;            ///< members still in the formation
    Uint32 now = 0;
    Uint32 started = 0;         ///< cycle the engagement began
    Uint32 lastInReach = 0;     ///< last cycle a member had an enemy target in reach
    Uint32 lastHit = 0;         ///< last cycle a member was hit by an enemy
    bool inReach = false;       ///< a member has an enemy target in reach now
    bool close = false;         ///< an enemy unit within kEngageCloseTiles of a member now
    bool outranged = false;     ///< a member was just hit by an enemy beyond every member's reach
};

/**
    The engagement rules (docs/formations.md, "Fighting in formation"), a pure function: a
    break (Close, Losses, Outranged, Quiet, Timeout, in that order), the re-march (Resume:
    nothing in reach and no hit for kEngageQuietMs) or None (keep fighting in place).
*/
EngageEnd engageStep(const EngageView& view);

/// The token range a formation command may carry: 24 bits, never zero.
constexpr Uint32 kTokenMask = 0xFFFFFFu;
inline bool isValidToken(Uint32 token) { return token != 0 && token <= kTokenMask; }

/**
    A group token that is unique per issuer for about 4.6 hours of game time: 20 bits of the
    game cycle and a 1-15 index for several groups ordered in the same cycle. Never zero.
*/
inline Uint32 makeToken(Uint32 gameCycle, Uint32 indexInCycle) {
    return ((gameCycle & 0xFFFFFu) << 4) | (1u + (indexInCycle % 15u));
}

/// The simulation-wide group id: the issuing player in the top byte, the token below.
inline Uint32 makeGroupID(Uint8 playerID, Uint32 token) {
    return (static_cast<Uint32>(playerID) << 24) | (token & kTokenMask);
}

/// Game cycles since the group was ordered, read from the 20 cycle bits of its token
/// (makeToken; exact for about 4.6 hours of game time).
inline Uint32 cyclesSinceOrdered(Uint32 groupID, Uint32 gameCycle) {
    return (gameCycle - ((groupID & kTokenMask) >> 4)) & 0xFFFFFu;
}

} // namespace FormationPlanner

#endif // FORMATIONPLANNER_H
