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

/// The formation role of a unit type.
Role roleOf(Uint32 itemID);

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
    \return the plan; with fewer than two members every member is sent to the target
*/
Plan plan(std::vector<Member> members, const Coord& target, int mapWidth, int mapHeight,
          const CanStand& canStand, int infantryPerTile);

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
