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
        && unit->getItemID() != Unit_Sandworm
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

FormationPlanner::Plan planGroup(const std::vector<const UnitBase*>& units, const Coord& target,
                                 FormationPlanner::Shape shape) {
    std::vector<FormationPlanner::Member> members;
    std::map<Uint32, const UnitBase*> byID;
    for(const UnitBase* unit : units) {
        if(!eligible(unit) || byID.count(unit->getObjectID())) continue;
        byID[unit->getObjectID()] = unit;
        members.push_back(memberOf(unit));
    }
    if(currentGameMap == nullptr || !currentGameMap->tileExists(target)) return FormationPlanner::Plan();
    const FormationPlanner::CanStand query = [&byID](const FormationPlanner::Member& member, const Coord& tile) {
        return canStand(byID.at(member.objectID), tile);
    };
    return FormationPlanner::plan(members, target, currentGameMap->getSizeX(), currentGameMap->getSizeY(),
                                  query, NUM_INFANTRY_PER_TILE, shape);
}

Coord revalidatedSlot(const UnitBase* unit, const Coord& slot) {
    if(canStand(unit, slot)) return slot;
    const FormationPlanner::Member member = memberOf(unit);
    const Coord found = FormationPlanner::nearestStandable(member, slot,
        currentGameMap->getSizeX(), currentGameMap->getSizeY(),
        [unit](const FormationPlanner::Member&, const Coord& tile) { return canStand(unit, tile); });
    return found.isValid() ? found : unit->getLocation();
}

void join(const std::vector<UnitBase*>& units, Uint32 groupID, const std::vector<Coord>& slots, bool hunt, Uint8 facing) {
    if(groupID == 0 || units.empty()) return;
    const House* owner = units.front()->getOwner();
    for(size_t i = 0; i < units.size(); ++i)
        units[i]->joinFormation(groupID, i < slots.size() ? slots[i] : Coord::Invalid(), hunt, facing);
    // The group's pace is its slowest vehicle's; infantry set it only for an all-infantry
    // group (see FormationPlanner::groupPace). Members that joined earlier through their
    // own formation commands count as well.
    std::vector<FormationPlanner::Member> members;
    for(const UnitBase* unit : unitList)
        if(unit->getFormationGroup() == groupID && unit->getOwner() == owner) members.push_back(memberOf(unit));
    const FixPoint cap = FormationPlanner::groupPace(members);
    for(UnitBase* unit : unitList)
        if(unit->getFormationGroup() == groupID && unit->getOwner() == owner) unit->setFormationSpeedCap(cap);
}

void breakHunt(Uint32 groupID, const ObjectBase* enemy) {
    if(groupID == 0) return;
    std::vector<UnitBase*> members;
    for(UnitBase* unit : unitList)
        if(unit->getFormationGroup() == groupID) members.push_back(unit);
    for(UnitBase* unit : members) {
        unit->leaveFormation();
        // The rigid march is over: whoever is not already fighting goes for the contact.
        unit->engageFormationContact(enemy);
    }
}

bool stepEntersSonicLane(const UnitBase* unit, const Coord& next) {
    if(!unit->isInFormation() || !sonicGroups.count(unit->getFormationGroup())) return false;
    const Coord forward = FormationPlanner::facingFromCode(unit->getFormationFacing());
    if(forward == Coord(0, 0)) return false;
    const bool sonic = FormationPlanner::isSonic(unit->getItemID());
    const Coord& here = unit->getLocation();
    for(const UnitBase* other : unitList) {
        if(other == unit || other->getFormationGroup() != unit->getFormationGroup()) continue;
        const bool otherSonic = FormationPlanner::isSonic(other->getItemID());
        if(!sonic && !otherSonic) continue;
        const Coord positions[2] = {other->getLocation(), other->isMoving() ? other->getNextSpot() : other->getLocation()};
        for(const Coord& at : positions) {
            if(at.isInvalid()) continue;
            // Into another sonic unit's lane, or another member into this sonic unit's lane.
            if(otherSonic && FormationPlanner::inSonicLane(at, next, forward)
               && !FormationPlanner::inSonicLane(at, here, forward)) return true;
            if(sonic && FormationPlanner::inSonicLane(next, at, forward)
               && !FormationPlanner::inSonicLane(here, at, forward)) return true;
        }
    }
    return false;
}

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
        Sint64 sumX = 0, sumY = 0;
        int leastRemaining = 0;
    };
    // Ordered by group id and filled in unit-list order: identical on every peer.
    std::map<Uint32, Group> groups;
    for(UnitBase* unit : unitList) {
        if(!unit->isInFormation()) continue;
        if(unit->formationMarchEnded()) { unit->leaveFormation(); continue; }
        Group& group = groups[unit->getFormationGroup()];
        const int remaining = unit->formationRemainingTiles();
        group.leastRemaining = group.members == 0 ? remaining : std::min(group.leastRemaining, remaining);
        ++group.members;
        group.sumX += unit->getLocation().x;
        group.sumY += unit->getLocation().y;
    }

    // The groups whose members keep the sonic lanes clear on this cycle's steps.
    sonicGroups.clear();
    for(const UnitBase* unit : unitList)
        if(unit->isInFormation() && unit->getFormationFacing() != 0 && FormationPlanner::isSonic(unit->getItemID()))
            sonicGroups.insert(unit->getFormationGroup());
    for(UnitBase* unit : unitList) {
        if(!unit->isInFormation()) continue;
        const Group& group = groups.at(unit->getFormationGroup());
        // The last member left has no one to keep pace with.
        if(group.members < 2) { unit->leaveFormation(); continue; }
        const Coord centroid(static_cast<int>(group.sumX / group.members), static_cast<int>(group.sumY / group.members));
        unit->setFormationCatchUp(FormationPlanner::runsAtFullSpeed(
            unit->formationRemainingTiles() - group.leastRemaining,
            FormationPlanner::tileDistance(unit->getLocation(), centroid)));
    }
}

} // namespace FormationOrders
