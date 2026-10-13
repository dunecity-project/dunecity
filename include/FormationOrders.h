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

#ifndef FORMATIONORDERS_H
#define FORMATIONORDERS_H

#include <FormationPlanner.h>

#include <vector>

class UnitBase;

/**
    The engine side of formation movement (docs/formations.md): builds planner input from
    real units and the live map, and maintains formation membership in the simulation.
    Everything here that changes state runs inside the deterministic simulation (the
    command executor, AI players, the per-cycle update); planGroup() only reads.
*/
namespace FormationOrders {

/// AI group moves use formations from this many eligible units; the player from two.
constexpr int kMinAIGroup = 3;
constexpr int kMinPlayerGroup = 2;

/// May this unit move in a formation? An active ground unit on the map; never aircraft or worms.
bool eligible(const UnitBase* unit);

/// May this unit stand on tile as a formation destination (terrain and structures only)?
bool canStand(const UnitBase* unit, const Coord& tile);

/// The planner's view of one unit.
FormationPlanner::Member memberOf(const UnitBase* unit);

/**
    THE planning entry point: formation slots for these units towards target on the live map.
    Ineligible units are left out of the plan (plan.slotFor() returns Coord::Invalid() for
    them); with fewer than two eligible units plan.formation is false. Reads only.
*/
FormationPlanner::Plan planGroup(const std::vector<const UnitBase*>& units, const Coord& target);

/**
    The destination a formation order really uses: 'slot' if the unit may still stand there,
    otherwise the nearest tile it may stand on (the slot may have become a building since the
    plan was made). Deterministic, so every peer executing the same order agrees.
*/
Coord revalidatedSlot(const UnitBase* unit, const Coord& slot);

/**
    Makes the units members of formation 'groupID' and sets the group's speed cap to the
    slowest member's reference speed, including members that joined earlier.
    \param  slots   one per unit: the formation slot, or Coord::Invalid() for a member that
                    keeps its own order and only matches the group's pace
*/
void join(const std::vector<UnitBase*>& units, Uint32 groupID, const std::vector<Coord>& slots);

/**
    Per-cycle membership upkeep, run before any unit moves: members that arrived, made
    contact, were left alone or can no longer move leave their formation, and each
    remaining member learns whether it is lagging and may run at full speed.
*/
void update();

} // namespace FormationOrders

#endif // FORMATIONORDERS_H
