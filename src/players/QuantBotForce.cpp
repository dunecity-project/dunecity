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

// QuantBot force concentration (docs/formations.md, "Concentrating force"): focus fire,
// converging on a fight, and mustering distant defenders before they engage. The pooling and
// recall rules of the base defence live in scrambleWholeArmyToDefend (QuantBotArmy.cpp).
//
// Deterministic: every scan runs in the fixed unit/structure list order, each unit's decision
// depends only on the simulation state (never on another unit's decision this pass), musters
// are a vector in creation order with ordered member sets, and only integer arithmetic decides.

#include "QuantBotInternal.h"

using QuantBotInternal::mobileCombatItem;
namespace Force = ForceConcentrationPolicy;

namespace {

int tiles(Coord a, Coord b) { return blockDistance(a, b).lround(); }

Force::TargetKey targetKeyOf(const ObjectBase* object) {
    Force::TargetKey key;
    key.id = object->getObjectID();
    key.health = object->getHealth().lround();
    if (object->isAStructure()) {
        key.cls = object->getItemID() == Structure_Wall ? Force::Wall
            : object->canAttack() ? Force::ArmedBuilding : Force::Building;
    } else {
        key.cls = object->canAttack() && mobileCombatItem(object->getItemID()) ? Force::ArmedUnit : Force::Unarmed;
    }
    return key;
}

bool armed(const ObjectBase* object) {
    const int cls = targetKeyOf(object).cls;
    return cls == Force::ArmedUnit || cls == Force::ArmedBuilding;
}

Force::ValueKey valueKeyOf(const ObjectBase* object) {
    const auto key = targetKeyOf(object);
    Force::ValueKey value;
    value.cls = key.cls;
    value.health = std::max<int64_t>(1, key.health);
    value.id = key.id;
    if (key.cls == Force::ArmedUnit || key.cls == Force::ArmedBuilding) {
        const auto& data = currentGame->objectData.data[object->getItemID()][object->getOriginalHouseID()];
        value.dpf = int64_t(std::max(0, data.weapondamage)) * 1000 / std::max(1, data.weaponreloadtime);
    }
    return value;
}

/// One volley of \a unit: its weapon damage (the per-hit damage the target loses).
int64_t volleyOf(const UnitBase* unit) {
    return std::max(1, currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].weapondamage);
}

} // namespace

bool QuantBot::focusFire(const UnitBase* unit, const std::vector<const ObjectBase*>& enemies) {
    const int myTeam = getHouse()->getTeamID();
    const ObjectBase* current = unit->getTarget();
    if (current && (current->getHealth() <= 0 || !current->getOwner() || current->getOwner()->getTeamID() == myTeam))
        current = nullptr;
    const Coord at = unit->getLocation();
    // Tile pre-filter; isInWeaponRange() decides.
    const int reach = unit->getWeaponRange() + 3;
    const ObjectBase* best = nullptr;
    Force::TargetKey bestKey;
    for (const auto* enemy : enemies) {
        if (tiles(at, enemy->getClosestPoint(at)) > reach) continue;
        if (!unit->canAttack(enemy) || !unit->isInWeaponRange(enemy)) continue;
        const auto key = targetKeyOf(enemy);
        if (key.cls == Force::Wall) continue;
        if (!best || Force::betterTarget(key, bestKey)) { best = enemy; bestKey = key; }
    }
    if (!best) return false;
    if (current && !Force::shouldSwitch(unit->isInWeaponRange(current), targetKeyOf(current), bestKey)) return false;
    doAttackObject(unit, best, false);
    ++forceStats.focusSwitches;
    return true;
}

void QuantBot::focusFireNoOverkill(const std::vector<const UnitBase*>& hunters, const std::vector<const ObjectBase*>& enemies) {
    const int myTeam = getHouse()->getTeamID();
    auto hostile = [&](const ObjectBase* target) {
        return target && target->getHealth() > 0 && target->getOwner() && target->getOwner()->getTeamID() != myTeam;
    };
    // One volley of everyone already shooting at a target in reach (NOK-AV's assigned damage).
    std::map<Uint32, int64_t> assigned;
    for (const auto* unit : hunters) {
        const auto* target = unit->getTarget();
        if (hostile(target) && unit->isInWeaponRange(target)) assigned[target->getObjectID()] += volleyOf(unit);
    }
    // Object-id order: the same assignment on every peer whatever the unit list order.
    for (const auto* unit : hunters) {
        const ObjectBase* current = hostile(unit->getTarget()) ? unit->getTarget() : nullptr;
        const bool currentInRange = current && unit->isInWeaponRange(current);
        const int64_t mine = currentInRange ? volleyOf(unit) : 0;
        const Coord at = unit->getLocation();
        const int reach = unit->getWeaponRange() + 3;
        const ObjectBase* best = nullptr;
        Force::ValueKey bestKey;
        for (const auto* enemy : enemies) {
            if (enemy == current || tiles(at, enemy->getClosestPoint(at)) > reach) continue;
            if (!unit->canAttack(enemy) || !unit->isInWeaponRange(enemy)) continue;
            const auto key = valueKeyOf(enemy);
            if (key.cls == Force::Wall) continue;
            const auto found = assigned.find(key.id);
            if (found != assigned.end() && Force::overkilled(found->second, key.health)) continue;
            if (!best || Force::betterValue(key, bestKey)) { best = enemy; bestKey = key; }
        }
        if (!best) continue;
        if (current) {
            const auto currentKey = valueKeyOf(current);
            const bool overkill = Force::overkilled(assigned[current->getObjectID()] - mine, currentKey.health);
            if (!Force::shouldSwitchValue(currentInRange, overkill, currentKey, bestKey)) continue;
            assigned[current->getObjectID()] -= mine;
        }
        assigned[best->getObjectID()] += volleyOf(unit);
        doAttackObject(unit, best, false);
        ++forceStats.focusSwitches;
    }
}

void QuantBot::concentrateForces() {
    const bool noOverkill = forceVariant(kForceNoOverkill);
    const bool focus = forceVariant(kForceFocusFire) && !noOverkill, convergeOn = forceVariant(kForceConverge);
    if (forceVariant(kForceMuster) || !musters.empty()) updateMusters();
    if (!focus && !convergeOn && !noOverkill) return;
    AITelemetry::PerformanceScope perfScope("ai.concentrateForces", getGameCycleCount(), getHouse()->getHouseID());
    const int myTeam = getHouse()->getTeamID();
    // Visible enemy ground units and buildings, in list order.
    std::vector<const ObjectBase*> enemies;
    for (const auto* unit : getUnitList())
        if (unit->getOwner() && unit->getOwner()->getTeamID() != myTeam && unit->getHealth() > 0 && unit->isActive()
            && !unit->isAFlyingUnit() && unit->getItemID() != Unit_Sandworm && unit->isVisible(myTeam))
            enemies.push_back(unit);
    for (const auto* structure : getStructureList())
        if (structure->getOwner() && structure->getOwner()->getTeamID() != myTeam && structure->getHealth() > 0
            && structure->isVisible(myTeam))
            enemies.push_back(structure);
    if (enemies.empty()) return;
    // Our hunters: the wave members and the base defenders (engine Hunt, not forced).
    auto hunter = [&](const UnitBase* unit) {
        return unit->getOwner() == getHouse() && unit->getHealth() > 0 && unit->isActive() && unit->isRespondable()
            && unit->isAGroundUnit() && unit->canAttack() && mobileCombatItem(unit->getItemID())
            && unit->getItemID() != Unit_Deviator && unit->getAttackMode() == HUNT && !unit->wasForced()
            && unitRole(unit, {UnitRole::Human, UnitRole::Repair}) == UnitRole::Free;
    };
    // The fights: where a hunter of ours is shooting at an armed enemy in its reach. Taken
    // before any order of this pass, so no unit's decision depends on another's.
    std::vector<Coord> fights;
    if (convergeOn) {
        for (const auto* unit : getUnitList()) {
            if (!hunter(unit)) continue;
            const auto* target = unit->getTarget();
            if (target && target->getHealth() > 0 && target->getOwner() && target->getOwner()->getTeamID() != myTeam
                && armed(target) && unit->isInWeaponRange(target)) fights.push_back(target->getLocation());
        }
    }
    if (noOverkill) {
        std::vector<const UnitBase*> hunters;
        for (const auto* unit : getUnitList()) if (hunter(unit)) hunters.push_back(unit);
        std::sort(hunters.begin(), hunters.end(),
            [](const UnitBase* a, const UnitBase* b) { return a->getObjectID() < b->getObjectID(); });
        focusFireNoOverkill(hunters, enemies);
    }
    for (const auto* unit : getUnitList()) {
        if (!hunter(unit)) continue;
        if (focus && focusFire(unit, enemies)) continue;
        if (fights.empty() || unitRole(unit, {UnitRole::Wave}) != UnitRole::Wave || unit->formationContact()) continue;
        // Not fighting: the nearest fight of our army (ties to the first in list order, which
        // is the same on every peer).
        const Coord at = unit->getLocation();
        int fightDistance = -1;
        Coord fight = Coord::Invalid();
        for (const Coord& point : fights) {
            const int distance = tiles(at, point);
            if (fightDistance < 0 || distance < fightDistance) { fightDistance = distance; fight = point; }
        }
        const auto* target = unit->getTarget();
        const int targetDistance = target && target->getHealth() > 0 ? tiles(at, target->getClosestPoint(at)) : -1;
        if (!Force::converge(fightDistance, targetDistance)) continue;
        if (unit->isHuntingToward() && tiles(unit->getDestination(), fight) <= Force::kConvergeSameGoalTiles) continue;
        doHuntToward(unit, fight.x, fight.y);
        ++forceStats.converges;
    }
}

const QuantBot::Muster* QuantBot::musterOf(Uint32 id) const {
    for (const auto& muster : musters) if (muster.members.count(id)) return &muster;
    return nullptr;
}

void QuantBot::startMuster(const ObjectBase* building, const std::vector<Uint32>& ids) {
    if (!building || ids.empty()) return;
    Muster* muster = nullptr;
    for (auto& existing : musters) if (existing.building == building->getObjectID()) muster = &existing;
    const UnitBase* anchor = nullptr;
    int64_t sx = 0, sy = 0, n = 0;
    for (const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        sx += unit->getLocation().x; sy += unit->getLocation().y; ++n;
        if (!anchor || (anchor->isInfantry() && !unit->isInfantry())) anchor = unit;
    }
    if (!anchor) return;
    if (!muster) {
        // On the defenders' side of the building, kMusterStandoffTiles from it, out of reach.
        const Coord from(int(sx / n), int(sy / n));
        const Coord edge = building->getClosestPoint(from);
        const auto offset = BlobWavePolicy::scaled(from.x - edge.x, from.y - edge.y, Force::kMusterStandoffTiles);
        Muster created;
        created.building = building->getObjectID();
        created.since = getGameCycleCount();
        created.point = huntPartyPoint(edge + Coord(offset.first, offset.second), anchor, true);
        musters.push_back(created);
        muster = &musters.back();
        ++forceStats.musters;
    }
    const int radius = BlobWavePolicy::blobRadius(int(muster->members.size() + ids.size())) + 1;
    std::set<int64_t> reserved;
    for (const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        muster->members.insert(id);
        groundSquad.erase(id);
        Coord slot = huntPartySlot(unit, muster->point, radius, reserved);
        if (slot.isInvalid()) slot = muster->point;
        doSetAttackMode(unit, AREAGUARD);
        doMove2Pos(unit, slot.x, slot.y, false);
    }
    traceDecision("defence_muster", AITelemetry::Record().set("building", building->getObjectID())
        .set("x", muster->point.x).set("y", muster->point.y).set("members", int(muster->members.size())));
}

void QuantBot::updateMusters() {
    const Uint32 now = getGameCycleCount();
    const int myTeam = getHouse()->getTeamID();
    for (auto& muster : musters) {
        for (auto it = muster.members.begin(); it != muster.members.end();) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(*it));
            const bool keep = unit && unit->getOwner() == getHouse() && unit->getHealth() > 0
                && unitRole(unit, {UnitRole::Human, UnitRole::Repair}) == UnitRole::Free;
            it = keep ? std::next(it) : muster.members.erase(it);
        }
        const auto* building = getObject(muster.building);
        // Is the attack still on? Any visible enemy fighter near the building.
        bool contested = false;
        if (building && building->getHealth() > 0)
            for (const auto* enemy : getUnitList())
                if (enemy->getOwner() && enemy->getOwner()->getTeamID() != myTeam && enemy->getHealth() > 0
                    && enemy->isActive() && enemy->canAttack() && !enemy->isAFlyingUnit() && enemy->isVisible(myTeam)
                    && tiles(enemy->getLocation(), building->getClosestPoint(enemy->getLocation())) <= Force::kPoolThreatTiles) {
                    contested = true; break;
                }
        const int members = int(muster.members.size());
        const int radius = BlobWavePolicy::blobRadius(members) + 1;
        int there = 0;
        bool contact = false;
        for (const Uint32 id : muster.members) {
            const auto* unit = static_cast<const UnitBase*>(getObject(id));
            if (tiles(unit->getLocation(), muster.point) <= radius) ++there;
            contact = contact || unit->formationContact() != nullptr;
        }
        const Uint32 elapsed = ArmyPosturePolicy::elapsed(now, muster.since);
        const bool quorum = int64_t(there) * 10000 >= int64_t(Force::kMusterQuorumBps) * std::max(1, members);
        if (members > 0 && contested && !quorum && !contact && elapsed < Uint32(MILLI2CYCLES(Force::kMusterMaxMs))) {
            // Re-send whoever lost its walk.
            std::set<int64_t> reserved;
            for (const Uint32 id : muster.members) {
                const auto* unit = static_cast<const UnitBase*>(getObject(id));
                if (!unit->isActive() || !unit->isRespondable() || tiles(unit->getLocation(), muster.point) <= radius
                    || unit->isMoving() || unit->hasPendingPathRequest() || unit->hasATarget()) continue;
                Coord slot = huntPartySlot(unit, muster.point, radius, reserved);
                if (slot.isInvalid()) slot = muster.point;
                doSetAttackMode(unit, AREAGUARD);
                doMove2Pos(unit, slot.x, slot.y, false);
            }
            continue;
        }
        // Engage together: every member hunts its way to the building, as a scrambled defender.
        for (const Uint32 id : muster.members) {
            const auto* unit = static_cast<const UnitBase*>(getObject(id));
            if (!unit->isActive() || !unit->isRespondable()) continue;
            if (contested && building) {
                const Coord to = building->getClosestPoint(unit->getLocation());
                doHuntToward(unit, to.x, to.y);
                defenceAssignments[id] = muster.building;
            } else doSetAttackMode(unit, HUNT);
        }
        ++forceStats.musterReleases;
        traceDecision("defence_muster_release", AITelemetry::Record().set("building", muster.building)
            .set("members", members).set("there", there).set("contact", contact).set("contested", contested)
            .set("elapsed_cycles", int(elapsed)));
        muster.members.clear();
    }
    musters.erase(std::remove_if(musters.begin(), musters.end(),
        [](const Muster& muster) { return muster.members.empty(); }), musters.end());
}
