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

class ObjectBase;
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

/// The shape AI players form their groups into: tanks in front, scouts ahead.
constexpr FormationPlanner::Shape kAIShape = FormationPlanner::Shape::Combined;

/// An AI hunting formation (Resume::HuntToward) marches at the latest this long after it was
/// ordered, formed up or not: a whole army forming up in a crowded base can otherwise take
/// minutes, the members that are not in place catch up on the march (docs/formations.md).
constexpr Uint32 kHuntFormUpMaxMs = 15000;

/**
    What a formation order does once its members are on their way. Sent in bits 24-27 of a
    formation command's last parameter (docs/formations.md).
*/
enum class OrderKind : Uint8 {
    Move = 0,           ///< CMD_UNIT_FORMATION_MOVE: forced move to the slot
    AttackMove = 1,     ///< CMD_UNIT_FORMATION_HUNT: attack-move to the slot, hunting in formation
    Patrol = 2          ///< CMD_UNIT_FORMATION_HUNT: patrol between here and the slot, hunting in formation
};

/**
    How a member's order towards its slot is given again when its group has formed up and
    the march begins (UnitBase::endFormUp; saved per member from SAVEGAMEVERSION 9860).
*/
enum class Resume : Uint8 {
    Move = 0,           ///< an unforced move to the slot (AI doFormationMove without force)
    ForcedMove = 1,     ///< a forced move to the slot (player formation moves, forced AI moves)
    Walk = 2,           ///< the attack-move or patrol in the order queue walks on to the slot
    HuntToward = 3      ///< doHuntToward the slot (AI hunting formations)
};

/**
    A formation command's last parameter: the 24-bit group token, the order kind in bits
    24-27 and the formation's facing code (FormationPlanner::facingCode) in bits 28-31.
    Protocol 71 sent the bare token, which reads as a Move without a facing.
*/
inline Uint32 packParameter(OrderKind kind, Uint8 facing, Uint32 token) {
    return (static_cast<Uint32>(facing & 0xF) << 28) | ((static_cast<Uint32>(kind) & 0xF) << 24)
        | (token & FormationPlanner::kTokenMask);
}
inline Uint32 tokenOf(Uint32 parameter) { return parameter & FormationPlanner::kTokenMask; }
inline Uint32 kindOf(Uint32 parameter) { return (parameter >> 24) & 0xF; }
inline Uint8 facingOf(Uint32 parameter) { return static_cast<Uint8>(parameter >> 28); }

/// May this unit move in a formation? An active ground unit on the map; never aircraft or worms.
bool eligible(const UnitBase* unit);

/// May this unit stand on tile as a formation destination (terrain and structures only)?
bool canStand(const UnitBase* unit, const Coord& tile);

/// The planner's view of one unit.
FormationPlanner::Member memberOf(const UnitBase* unit);

/**
    THE planning entry point: formation slots for these units towards target on the live map,
    in the given shape. Ineligible units are left out of the plan (plan.slotFor() returns
    Coord::Invalid() for them); with fewer than two eligible units plan.formation is false.
    Reads only.
*/
FormationPlanner::Plan planGroup(const std::vector<const UnitBase*>& units, const Coord& target,
                                 FormationPlanner::Shape shape);

/**
    The destination a formation order really uses: 'slot' if the unit may still stand there,
    otherwise the nearest tile it may stand on (the slot may have become a building since the
    plan was made). Deterministic, so every peer executing the same order agrees.
*/
Coord revalidatedSlot(const UnitBase* unit, const Coord& slot);

/**
    Makes the units members of formation 'groupID' and sets the group's speed cap to the
    slowest member's reference speed (infantry included), including members that joined
    earlier. Members with a slot form up first (FormationPlanner::formUpTiles), planned for
    the whole group by the next update() - the same cycle for commands and AI orders.
    \param  slots   one per unit: the formation slot, or Coord::Invalid() for a member that
                    keeps its own order and only matches the group's pace
    \param  hunt    a hunting formation: the first contact any member makes breaks the whole
                    group into a fight (breakHunt)
    \param  facing  the formation's facing code (FormationPlanner::facingCode of plan.forward);
                    with one, the march keeps the sonic lanes clear (stepEntersSonicLane)
    \param  resume  the order each unit was just given towards its slot, given again after
                    the form-up
*/
void join(const std::vector<UnitBase*>& units, Uint32 groupID, const std::vector<Coord>& slots,
          bool hunt = false, Uint8 facing = 0, Resume resume = Resume::Move);

/**
    The sonic-lane rule on the march (docs/formations.md): would 'unit', a formation member
    with a facing, put itself into the lane ahead of a sonic member of its formation by
    stepping onto 'next' - or, if it is a sonic unit itself, bring another member into its
    own lane? Positions count as the tile each member stands on and, while it moves, the tile
    it moves to. A pair that already shares a lane does not stop either unit. A unit that
    would enter a lane waits, as for a moving blocker.
*/
bool stepEntersSonicLane(const UnitBase* unit, const Coord& next);

/**
    Ends hunting formation 'groupID' because a member met 'enemy': every member leaves the
    formation and drops its pace, and each one that fights and has no target of its own
    attacks 'enemy', or closes in on it when its attack mode would not keep a target that
    far (UnitBase::engageFormationContact; unforced, so attack-move leashes and Hunt take
    over from there).
    Called from the per-cycle update and when a member is hit; deterministic on every peer.
*/
void breakHunt(Uint32 groupID, const ObjectBase* enemy);

/**
    Per-cycle membership upkeep, run before any unit moves: a hunting formation in which any
    member has made contact breaks (breakHunt); the groups joined this cycle plan their
    form-up; members that arrived, made contact, were left alone or can no longer move leave
    their formation; a group whose members all stand on their form-up tiles (or cannot get
    closer) starts its march; and every member learns this cycle's march
    (FormationPlanner::marchState): forming members walk to their form-up tiles at full speed
    and wait there, marching members keep the pace, recover a lost place or wait for a member
    that fell behind.
*/
void update();

} // namespace FormationOrders

#endif // FORMATIONORDERS_H
