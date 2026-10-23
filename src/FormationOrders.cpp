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

#include <FormationOrders.h>

#include <Game.h>
#include <globals.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <units/UnitBase.h>

#include <map>
#include <set>

namespace FormationOrders {

namespace {
/// Formations with a facing and a sonic member, recomputed by update() at the start of
/// every cycle (so identical on every peer and after a load): only their members pay for
/// the lane check when they step.
std::set<Uint32> sonicGroups;
} // namespace

bool eligible(const UnitBase* unit) {
    return unit != nullptr && unit->isActive() && unit->getHealth() > 0
        && unit->isAGroundUnit() && !unit->isAFlyingUnit() && !unit->isPickedUp()
        && unit->getItemID() != Unit_Sandworm && FormationPlanner::joinsFormations(unit->getItemID())
        && currentGameMap != nullptr && currentGameMap->tileExists(unit->getLocation());
}

bool canStand(const UnitBase* unit, const Coord& tile) {
    if(currentGameMap == nullptr || !currentGameMap->tileExists(tile)) return false;
    const Tile* pTile = currentGameMap->getTile(tile);
    if(pTile->hasAStructure()) return false;
    // Only infantry climb mountains.
    return unit->isInfantry() || !pTile->isMountain();
}

FormationPlanner::Member memberOf(const UnitBase* unit) {
    FormationPlanner::Member member;
    member.objectID = unit->getObjectID();
    member.itemID = unit->getItemID();
    member.tile = unit->getLocation();
    member.role = FormationPlanner::roleOf(unit->getItemID());
    member.speed = unit->getFormationReferenceSpeed();
    member.infantry = unit->isInfantry();
    return member;
}

namespace {
/// The planner's view of the eligible units (each once) and their live terrain query.
struct PlannerInput {
    std::vector<FormationPlanner::Member> members;
    std::map<Uint32, const UnitBase*> byID;
    FormationPlanner::CanStand query() const {
        return [this](const FormationPlanner::Member& member, const Coord& tile) {
            return canStand(byID.at(member.objectID), tile);
        };
    }
};

PlannerInput plannerInput(const std::vector<const UnitBase*>& units) {
    PlannerInput input;
    for(const UnitBase* unit : units) {
        if(!eligible(unit) || input.byID.count(unit->getObjectID())) continue;
        input.byID[unit->getObjectID()] = unit;
        input.members.push_back(memberOf(unit));
    }
    return input;
}
} // namespace

FormationPlanner::Plan planGroup(const std::vector<const UnitBase*>& units, const Coord& target,
                                 FormationPlanner::Shape shape) {
    const PlannerInput input = plannerInput(units);
    if(currentGameMap == nullptr || !currentGameMap->tileExists(target)) return FormationPlanner::Plan();
    return FormationPlanner::plan(input.members, target, currentGameMap->getSizeX(), currentGameMap->getSizeY(),
                                  input.query(), NUM_INFANTRY_PER_TILE, shape);
}

FormationPlanner::Plan planInPlace(const std::vector<const UnitBase*>& units, const Coord& forward,
                                   FormationPlanner::Shape shape) {
    const PlannerInput input = plannerInput(units);
    if(currentGameMap == nullptr) return FormationPlanner::Plan();
    return FormationPlanner::planInPlace(input.members, forward, currentGameMap->getSizeX(), currentGameMap->getSizeY(),
                                         input.query(), NUM_INFANTRY_PER_TILE, shape);
}

Coord revalidatedSlot(const UnitBase* unit, const Coord& slot) {
    if(canStand(unit, slot)) return slot;
    const FormationPlanner::Member member = memberOf(unit);
    const Coord found = FormationPlanner::nearestStandable(member, slot,
        currentGameMap->getSizeX(), currentGameMap->getSizeY(),
        [unit](const FormationPlanner::Member&, const Coord& tile) { return canStand(unit, tile); });
    return found.isValid() ? found : unit->getLocation();
}

void join(const std::vector<UnitBase*>& units, Uint32 groupID, const std::vector<Coord>& slots, bool hunt, Uint8 facing,
          Resume resume) {
    if(groupID == 0 || units.empty()) return;
    const House* owner = units.front()->getOwner();
    for(size_t i = 0; i < units.size(); ++i)
        units[i]->joinFormation(groupID, i < slots.size() ? slots[i] : Coord::Invalid(), hunt, facing, resume);
    // The group's pace is its slowest member's, infantry included (FormationPlanner::groupPace),
    // and that member's type sets every capped member's step time. Members that joined
    // earlier through their own formation commands count as well.
    std::vector<FormationPlanner::Member> members;
    for(const UnitBase* unit : unitList)
        if(unit->getFormationGroup() == groupID && unit->getOwner() == owner) members.push_back(memberOf(unit));
    const FixPoint cap = FormationPlanner::groupPace(members);
    Uint32 paceItem = ItemID_Invalid, paceID = 0;
    for(const auto& member : members)
        if(member.speed == cap && (paceItem == ItemID_Invalid || member.objectID < paceID)) {
            paceItem = member.itemID;
            paceID = member.objectID;
        }
    for(UnitBase* unit : unitList)
        if(unit->getFormationGroup() == groupID && unit->getOwner() == owner) unit->setFormationPace(cap, paceItem);
}

void breakHunt(Uint32 groupID, const ObjectBase* enemy) {
    if(groupID == 0) return;
    std::vector<UnitBase*> members;
    for(UnitBase* unit : unitList)
        if(unit->getFormationGroup() == groupID) members.push_back(unit);
    for(UnitBase* unit : members) {
        const bool hunter = unit->isFormationHuntToward();
        unit->leaveFormation();
        // The rigid march is over: whoever is not already fighting goes for the contact.
        unit->engageFormationContact(enemy, hunter);
    }
}

bool stepEntersSonicLane(const UnitBase* unit, const Coord& next) {
    if(!unit->isInFormation() || !sonicGroups.count(unit->getFormationGroup())) return false;
    // Forming up the group crosses itself on the way into its shape, whose form-up tiles keep
    // the lanes clear (FormationPlanner::formUpTiles): the rule holds from the march on.
    if(unit->isFormationForming()) return false;
    const Coord forward = FormationPlanner::facingFromCode(unit->getFormationFacing());
    if(forward == Coord(0, 0)) return false;
    // An AI hunting formation may march before it has formed up (kHuntFormUpMaxMs). A sonic
    // member still recovering its place there has no lane yet: it moves up to its front-row
    // place without waiting for the members ahead of it, and nobody waits for its lane -
    // otherwise it and the members around it wait for each other for ever.
    auto sonicOutOfPlace = [](const UnitBase* member) {
        return member->isFormationHuntToward() && FormationPlanner::isSonic(member->getItemID())
            && member->getFormationMarch() == FormationPlanner::March::Recover;
    };
    if(sonicOutOfPlace(unit)) return false;
    const bool sonic = FormationPlanner::isSonic(unit->getItemID());
    const Coord& here = unit->getLocation();
    for(const UnitBase* other : unitList) {
        if(other == unit || other->getFormationGroup() != unit->getFormationGroup() || sonicOutOfPlace(other)) continue;
        const bool otherSonic = FormationPlanner::isSonic(other->getItemID());
        if(!sonic && !otherSonic) continue;
        const Coord positions[2] = {other->getLocation(), other->isMoving() ? other->getNextSpot() : other->getLocation()};
        for(const Coord& at : positions) {
            if(at.isInvalid()) continue;
            // Into another sonic unit's lane, or another member into this sonic unit's lane -
            // unless it is a sonic unit of the same type, which the wave does not hurt.
            if(otherSonic && FormationPlanner::inSonicLane(at, next, forward)
               && !FormationPlanner::inSonicLane(at, here, forward)
               && !FormationPlanner::mayStandInLane(other->getItemID(), unit->getItemID())) return true;
            if(sonic && FormationPlanner::inSonicLane(next, at, forward)
               && !FormationPlanner::inSonicLane(here, at, forward)
               && !FormationPlanner::mayStandInLane(unit->getItemID(), other->getItemID())) return true;
        }
    }
    return false;
}

namespace {

/// Plans the form-up of the members that joined group 'joined' this cycle and sends each to its
/// form-up tile (FormationPlanner::formUpTiles). Nothing to do for a short march.
void planFormUp(const std::vector<UnitBase*>& joined) {
    std::vector<FormationPlanner::Member> members;
    std::vector<Coord> slots;
    std::map<Uint32, const UnitBase*> byID;
    for(const UnitBase* unit : joined) {
        members.push_back(memberOf(unit));
        slots.push_back(unit->getFormationSlot());
        byID[unit->getObjectID()] = unit;
    }
    Coord forward = FormationPlanner::facingFromCode(joined.front()->getFormationFacing());
    if(forward == Coord(0, 0)) {
        // Formations without a facing face from the group to its slots.
        Sint64 sumX = 0, sumY = 0, slotX = 0, slotY = 0;
        for(size_t i = 0; i < members.size(); ++i) {
            sumX += members[i].tile.x; sumY += members[i].tile.y; slotX += slots[i].x; slotY += slots[i].y;
        }
        const Sint64 n = static_cast<Sint64>(members.size());
        forward = FormationPlanner::facing(Coord(static_cast<int>(sumX / n), static_cast<int>(sumY / n)),
                                           Coord(static_cast<int>(slotX / n), static_cast<int>(slotY / n)));
    }
    const FormationPlanner::CanStand query = [&byID](const FormationPlanner::Member& member, const Coord& tile) {
        return canStand(byID.at(member.objectID), tile);
    };
    const auto tiles = FormationPlanner::formUpTiles(members, slots, forward, currentGameMap->getSizeX(),
                                                     currentGameMap->getSizeY(), query, NUM_INFANTRY_PER_TILE);
    for(size_t i = 0; i < tiles.size(); ++i) joined[i]->startFormUp(tiles[i]);
}

/// How far a member that is not in place steps aside for a member that wants its tile.
constexpr int kStepAsideTiles = 3;
/// The longest ring of members on each other's form-up tiles that stepAsideForFormUp follows.
constexpr int kMaxRing = 16;

/// A member of group 'groupID' standing still on 'tile' although it is forming up somewhere
/// else, or nullptr.
UnitBase* displacedMemberOn(const Coord& tile, Uint32 groupID) {
    if(!currentGameMap->tileExists(tile)) return nullptr;
    const Tile* pTile = currentGameMap->getTile(tile);
    std::vector<Uint32> ids;
    if(const ObjectBase* object = pTile->getNonInfantryGroundObject()) ids.push_back(object->getObjectID());
    for(Uint32 id : pTile->getInfantryList()) ids.push_back(id);
    for(Uint32 id : ids) {
        auto* unit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(id));
        if(unit != nullptr && unit->getFormationGroup() == groupID && unit->isFormationForming() && !unit->isMoving()
           && unit->getLocation() != unit->getFormationFormUp()) return unit;
    }
    return nullptr;
}

/**
    Forming up in a crowd (docs/formations.md): units cannot swap tiles, so members standing on
    each other's form-up tiles in a ring (two that want each other's tiles, or more) wait for
    each other for ever, as can happen when a group forms up where it stands
    (OrderKind::FormUp); so does a member whose tile is held by one that has given its own walk
    up. When a member standing still off its form-up tile finds a member of its group that is
    not in place standing still on that tile, and that one has given up or the two are in such
    a ring, the one on the tile steps aside to the nearest free tile within kStepAsideTiles that
    is no member's form-up tile, and the other walks on; the one that stepped aside walks back
    when its group marches (UnitBase::endFormUp). Only members that walk (Resume::Move,
    ForcedMove) step aside; hunters and attack-move walks keep their orders. In unit-list
    order, each unit at most once a cycle, from live state only: identical on every peer and
    after a load.
*/
void stepAsideForFormUp() {
    std::set<Uint32> handled;
    for(UnitBase* unit : unitList) {
        if(!unit->isFormationForming() || unit->isMoving() || handled.count(unit->getObjectID())) continue;
        const Coord tile = unit->getFormationFormUp();
        if(unit->getLocation() == tile) continue;
        const Uint32 groupID = unit->getFormationGroup();
        UnitBase* blocker = displacedMemberOn(tile, groupID);
        if(blocker == nullptr || blocker == unit || handled.count(blocker->getObjectID())) continue;
        bool stuck = blocker->formUpReady();
        if(!stuck) {
            // Follow the members standing on each other's form-up tiles: back to this one is a ring.
            const UnitBase* next = blocker;
            for(int hop = 0; hop < kMaxRing && next != nullptr && next != unit; ++hop)
                next = displacedMemberOn(next->getFormationFormUp(), groupID);
            stuck = next == unit;
        }
        if(!stuck) continue;
        const Resume resume = blocker->getFormationResume();
        if(resume != Resume::Move && resume != Resume::ForcedMove) continue;
        std::set<std::pair<int, int>> formUpTiles;
        for(const UnitBase* member : unitList)
            if(member->getFormationGroup() == unit->getFormationGroup() && member->isFormationForming())
                formUpTiles.insert({member->getFormationFormUp().x, member->getFormationFormUp().y});
        const Coord from = blocker->getLocation();
        const Coord aside = FormationPlanner::nearestStandable(memberOf(blocker), from,
            currentGameMap->getSizeX(), currentGameMap->getSizeY(),
            [&](const FormationPlanner::Member&, const Coord& at) {
                return FormationPlanner::tileDistance(at, from) <= kStepAsideTiles && !formUpTiles.count({at.x, at.y})
                    && canStand(blocker, at) && !currentGameMap->getTile(at)->hasAGroundObject();
            });
        if(aside.isInvalid()) continue;
        blocker->doMove2Pos(aside.x, aside.y, resume == Resume::ForcedMove);
        unit->startFormUp(tile);
        handled.insert(unit->getObjectID());
        handled.insert(blocker->getObjectID());
    }
}

} // namespace

void update() {
    // Hunting formations first: the first member in unit-list order that has made contact
    // breaks its whole group, identically on every peer and after a load.
    std::map<Uint32, Uint32> contacts;    // group id -> the enemy met
    for(const UnitBase* unit : unitList) {
        if(!unit->isInFormation() || !unit->isFormationHunt() || contacts.count(unit->getFormationGroup())) continue;
        if(const ObjectBase* enemy = unit->formationContact()) contacts[unit->getFormationGroup()] = enemy->getObjectID();
    }
    for(const auto& [groupID, enemyID] : contacts)
        breakHunt(groupID, currentGame->getObjectManager().getObject(enemyID));

    struct Group {
        int members = 0;
        int forming = 0, ready = 0;
        int leastRemaining = 0, mostRemaining = 0, marching = 0;
        bool laneWait = false;
    };
    // Ordered by group id and filled in unit-list order: identical on every peer.
    std::map<Uint32, Group> groups;
    for(UnitBase* unit : unitList) {
        if(!unit->isInFormation()) continue;
        if(unit->formationMarchEnded()) { unit->leaveFormation(); continue; }
        ++groups[unit->getFormationGroup()].members;
    }
    // The last member left has no one to keep pace with. A hunter still forming up walks in
    // Guard to its form-up tile; it hunts on to its slot instead, as its march would have.
    for(UnitBase* unit : unitList) {
        if(!unit->isInFormation() || groups.at(unit->getFormationGroup()).members >= 2) continue;
        const bool resumeHunt = unit->isFormationHuntToward() && unit->isFormationForming();
        const Coord slot = unit->getFormationSlot();
        unit->leaveFormation();
        if(resumeHunt && slot.isValid()) unit->doHuntToward(slot.x, slot.y);
    }
    // The members that joined this cycle (commands and AI orders run before this) form up
    // together: one plan per group, in unit-list order.
    {
        std::map<Uint32, std::vector<UnitBase*>> joined;
        for(UnitBase* unit : unitList) {
            if(!unit->isFormationPending()) continue;
            unit->clearFormationPending();
            joined[unit->getFormationGroup()].push_back(unit);
        }
        for(const auto& [groupID, units] : joined) planFormUp(units);
    }


    // The groups whose members keep the sonic lanes clear on this cycle's steps.
    sonicGroups.clear();
    for(const UnitBase* unit : unitList)
        if(unit->isInFormation() && unit->getFormationFacing() != 0 && FormationPlanner::isSonic(unit->getItemID()))
            sonicGroups.insert(unit->getFormationGroup());
    // A member whose next step would enter a sonic lane waits for the others to move on.
    auto laneBlocked = [](const UnitBase* unit) {
        return sonicGroups.count(unit->getFormationGroup()) && !unit->isMoving() && unit->hasNextSpot()
            && stepEntersSonicLane(unit, unit->getNextSpot());
    };

    // Members on each other's form-up tiles make way before the group's readiness is counted.
    stepAsideForFormUp();

    // Form-up: a group marches once every forming member stands on its form-up tile (or cannot
    // get closer, or waits at a sonic lane); then each is sent on to its slot. An AI hunting
    // formation marches at the latest kHuntFormUpMaxMs after it was ordered.
    const Uint32 now = currentGame->getGameCycleCount();
    for(const UnitBase* unit : unitList) {
        if(!unit->isFormationForming()) continue;
        Group& group = groups.at(unit->getFormationGroup());
        ++group.forming;
        group.ready += (unit->formUpReady() || laneBlocked(unit)) ? 1 : 0;
    }
    for(UnitBase* unit : unitList) {
        if(!unit->isFormationForming()) continue;
        const Group& group = groups.at(unit->getFormationGroup());
        const bool overdue = unit->isFormationHuntToward()
            && FormationPlanner::cyclesSinceOrdered(unit->getFormationGroup(), now) >= Uint32(MILLI2CYCLES(kHuntFormUpMaxMs));
        if(group.ready == group.forming || overdue) unit->endFormUp();
    }

    // The march: remaining distances of the marching members.
    for(const UnitBase* unit : unitList) {
        if(!unit->isInFormation() || unit->isFormationForming()) continue;
        Group& group = groups.at(unit->getFormationGroup());
        const int remaining = unit->formationRemainingTiles();
        group.leastRemaining = group.marching == 0 ? remaining : std::min(group.leastRemaining, remaining);
        group.mostRemaining = group.marching == 0 ? remaining : std::max(group.mostRemaining, remaining);
        ++group.marching;
        // The group does not wait for a member held up at a sonic lane, or the two would wait
        // for each other for ever.
        group.laneWait = group.laneWait || laneBlocked(unit);
    }
    // One speed (docs/formations.md): forming members walk to their form-up tiles at full speed
    // and wait there; marching members keep the pace, a member that fell behind its place
    // recovers it at full speed, and the members ahead of one that fell further behind wait.
    for(UnitBase* unit : unitList) {
        if(!unit->isInFormation()) continue;
        if(unit->isFormationForming()) {
            // A walking member sent aside by stepAsideForFormUp is ready but must still get there.
            const Resume resume = unit->getFormationResume();
            const bool steppingAside = (resume == Resume::Move || resume == Resume::ForcedMove)
                && unit->getLocation() != unit->getFormationFormUp() && unit->getDestination() != unit->getFormationFormUp()
                && unit->getDestination() != unit->getLocation();
            unit->setFormationMarch(unit->formUpReady() && !steppingAside ? FormationPlanner::March::Hold
                                                                            : FormationPlanner::March::Recover);
            continue;
        }
        const Group& group = groups.at(unit->getFormationGroup());
        const int remaining = unit->formationRemainingTiles();
        unit->setFormationMarch(FormationPlanner::marchState(remaining - group.leastRemaining,
            group.laneWait ? 0 : group.mostRemaining - remaining));
    }
    // A holding member in the way of a formation-mate that wants to step onto its tile gives way:
    // it keeps the pace for this cycle, so the member behind can close up. Without it, members
    // waiting in a chokepoint for the ones still behind it blocked their only way through: a
    // 22-unit Box or Pincer never got through a 5-tile canyon gap (docs/formations.md,
    // "Scenario tests").
    // Repeated until no further holder is released, so a file of holders steps on together.
    // Tiles and the unit list decide everything: identical on every peer.
    std::map<std::pair<int, int>, UnitBase*> holders;
    std::vector<const UnitBase*> pushing;
    for(UnitBase* unit : unitList) {
        if(!unit->isInFormation() || unit->isFormationForming()) continue;
        if(unit->getFormationMarch() == FormationPlanner::March::Hold)
            holders[{unit->getLocation().x, unit->getLocation().y}] = unit;
        else if(!unit->isMoving() && unit->hasNextSpot())
            pushing.push_back(unit);
    }
    while(!pushing.empty() && !holders.empty()) {
        std::vector<const UnitBase*> released;
        for(const UnitBase* unit : pushing) {
            const auto it = holders.find({unit->getNextSpot().x, unit->getNextSpot().y});
            if(it == holders.end() || it->second->getFormationGroup() != unit->getFormationGroup()) continue;
            UnitBase* holder = it->second;
            holders.erase(it);
            holder->setFormationMarch(FormationPlanner::March::Pace);
            if(holder->hasNextSpot()) released.push_back(holder);
        }
        pushing = std::move(released);
    }
}

} // namespace FormationOrders
