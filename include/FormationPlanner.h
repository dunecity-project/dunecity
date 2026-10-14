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
    Armour = 0,     ///< tanks, quads, trikes, devastators, sonic tanks: the front rows
    Ranged = 1,     ///< launchers, siege tanks, deviators, rocket trikes: behind the armour
    Infantry = 2,   ///< soldiers, troopers, saboteurs: flanks of the last row, then the back
    Support = 3     ///< harvesters and MCVs: the rear
};

/// The formation role of a unit type (the Box shape's rows).
Role roleOf(Uint32 itemID);

/**
    The shapes a group can be ordered into (docs/formations.md). Every shape faces the
    direction from the group's centroid to the target and keeps the sonic-lane rule.
*/
enum class Shape : Uint8 {
    Box = 0,        ///< rows by role: armour, ranged, infantry, support (the default)
    Combined = 1,   ///< tanks in front, ranged behind, light vehicles on the flanks, scouts ahead
    Line = 2,       ///< wide and shallow: the most frontage facing the target
    Column = 3      ///< narrow and deep, for passes and bridges
};
constexpr int kShapeCount = 4;
/// Shape by index; anything out of range is the Box.
Shape shapeFromIndex(int index);
/// English name for the interface ("Box", "Combined arms", "Line", "Column").
const char* shapeName(Shape shape);

/// Finer unit class used by the shapes other than the Box.
enum class UnitClass : Uint8 {
    Sonic = 0,      ///< sonic tank, sonic trike: their wave hits friends, so nothing stands ahead
    Heavy = 1,      ///< tanks, devastators, flame tanks and other armour: the front
    Light = 2,      ///< quads, trikes, raider trikes: flanks and scouts
    Ranged = 3,     ///< launchers, siege tanks, deviators, rocket trikes: behind the front
    Infantry = 4,
    Support = 5     ///< harvesters and MCVs
};
UnitClass classOf(Uint32 itemID);
/// Does this unit fire a sonic wave that damages everything in its lane, friends included?
bool isSonic(Uint32 itemID);

/**
    The sonic-lane rule: is 'other' in the firing lane ahead of a sonic unit standing on
    'sonic', for a formation facing 'forward' (one of the eight unit steps)? The lane is
    every tile strictly ahead (positive progress along 'forward') whose lateral offset is at
    most one formation column: 1 tile for a straight facing, the diagonal column pitch
    (|dx - dy| or |dx + dy| <= 2) for a diagonal one.
*/
bool inSonicLane(const Coord& sonic, const Coord& other, const Coord& forward);

/// Combined arms: how many scouts a group of memberCount with lightCount light vehicles sends ahead (0-3).
int scoutCount(int memberCount, int lightCount);
/// Combined arms: how many tiles ahead of the front row the scouts go (4 to 6, growing with the group).
int scoutDistance(int memberCount);
/// Line: at most this many cells abreast (more sonic units widen the front row).
constexpr int kLineMaxWidth = 16;
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
    FixPoint groupSpeed = 0;    ///< groupPace(): the slowest vehicle's speed
    bool formation = false;     ///< false for fewer than two members: plain move to the target
    Coord forward = Coord(0, -1);   ///< the facing the shape was laid out for (formation plans)
    std::vector<Uint32> scouts; ///< Combined arms: the members sent ahead as scouts, sorted

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
/// Catch-up: a member this many tiles behind the group's most advanced member runs at full speed.
constexpr int kCatchUpLagTiles = 5;
/// Catch-up: a member this far from the group centroid runs at full speed.
constexpr int kCatchUpCentroidTiles = 10;
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
    \param  members         the group, in any order (object ids must be distinct)
    \param  target          the ordered destination tile (inside the map)
    \param  mapWidth        map width in tiles
    \param  mapHeight       map height in tiles
    \param  canStand        terrain/structure query, see CanStand
    \param  infantryPerTile how many infantry may share one destination tile (at least 1)
    \param  shape           the formation shape; the Box keeps a coherent group's own shape
    \return the plan; with fewer than two members every member is sent to the target. For a
            formation, no slot is in a sonic member's lane (inSonicLane, plan.forward) as long
            as every member finds a usable tile within kMaxSearchRadius.
*/
Plan plan(std::vector<Member> members, const Coord& target, int mapWidth, int mapHeight,
          const CanStand& canStand, int infantryPerTile, Shape shape = Shape::Box);

/**
    The pace a formation keeps: its slowest vehicle's reference speed. Infantry walk far
    slower than any vehicle (a soldier sets 0.24 against a tank's 0.80) and holding a mixed
    wave to that crawl was measured to cost it a quarter of its first-contact fire, so
    infantry set the pace only for an all-infantry group. 0 for no members.
*/
FixPoint groupPace(const std::vector<Member>& members);

/**
    Nearest tile to 'wanted' that is inside the map and on which the member may stand, by a
    deterministic outward ring search (Chebyshev rings, then squared distance, then ring
    order). Returns 'wanted' itself if it qualifies and Coord::Invalid() if nothing within
    kMaxSearchRadius does.
*/
Coord nearestStandable(const Member& member, const Coord& wanted, int mapWidth, int mapHeight,
                       const CanStand& canStand);

/**
    Speed-matching exception. A formation member normally moves at the group speed; it runs
    at its own full speed while it is lagging: more than kCatchUpLagTiles further from its
    slot than the group's most advanced member is from its own, or more than
    kCatchUpCentroidTiles from the group centroid.
    \param  lagTiles            own remaining distance minus the group's smallest remaining distance
    \param  centroidDistance    tile distance to the group centroid
*/
bool runsAtFullSpeed(int lagTiles, int centroidDistance);

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

} // namespace FormationPlanner

#endif // FORMATIONPLANNER_H
