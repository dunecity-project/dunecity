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

int QuantBot::forceBits() const {
    if (forceVariantOverride != kNoForceOverride) return forceVariantOverride;
    const auto& force = getQuantBotConfig().force;
    return (force.focusFire ? kForceFocusFire : 0) | (force.converge ? kForceConverge : 0)
        | (force.bundle ? kForceBundle : 0) | (force.bundle && force.launchFromBundle ? kForceBundleGate : 0)
        | (force.perimeterHold ? kForcePerimeterHold : 0) | (force.perimeterTurrets ? kForceThreatTurrets : 0)
        | (force.sizedDefence ? kForceSizedDefence : 0);
}

int QuantBot::waveReadinessPercent() const {
    if (!autonomousCustomHardBrutal()) return 100;
    if (forceVariantOverride != kNoForceOverride)
        return (forceVariantOverride & kForceBigWaves) ? 150 : (forceVariantOverride & kForceSmallWaves) ? 60 : 100;
    return std::clamp(getQuantBotConfig().force.waveReadinessPercent, 10, 300);
}

int QuantBot::launchReadinessPercent() {
    const int percent = waveReadinessPercent();
    if (percent >= 100 || !forceVariant(kForceStrengthReadiness)) return percent;
    if (!(forceVariant(kForceEstimate) ? outnumberedByEstimate() : outnumberedByObservedArmy())) return percent;
    ++forceStats.fullReadiness;
    return 100;
}

namespace {

/// A mobile ground fighter (what the army comparisons count), alive and on the map.
bool groundFighter(const UnitBase* unit) {
    return unit->getOwner() && unit->getHealth() > 0 && unit->isActive() && unit->isAGroundUnit()
        && unit->canAttack() && mobileCombatItem(unit->getItemID());
}

int64_t healthValue(const UnitBase* unit) {
    return CombatPowerPolicy::unitValue(currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price,
                                        unit->getHealth().lround(), unit->getMaxHealth());
}

} // namespace

bool QuantBot::outnumberedByObservedArmy() const {
    const int myTeam = getHouse()->getTeamID();
    int64_t own = 0, hostile = 0;
    for (const auto* unit : getUnitList()) {
        if (!groundFighter(unit)) continue;
        const bool mine = unit->getOwner() == getHouse();
        if (!mine && (unit->getOwner()->getTeamID() == myTeam || !unit->isVisible(myTeam))) continue;
        (mine ? own : hostile) += healthValue(unit);
    }
    return Force::outnumbered(own, hostile);
}

void QuantBot::updateEnemySightings() {
    const int myTeam = getHouse()->getTeamID();
    const Uint32 now = getGameCycleCount();
    for (const auto* unit : getUnitList())
        if (groundFighter(unit) && unit->getOwner()->getTeamID() != myTeam && unit->isVisible(myTeam))
            enemySightings[unit->getObjectID()] = {healthValue(unit), now};
    // Forget the dead, the captured and the long unseen (id order: the same on every peer).
    const Uint32 memory = Uint32(MILLI2CYCLES(Force::kSightingMemoryMs));
    for (auto it = enemySightings.begin(); it != enemySightings.end();) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(it->first));
        const bool gone = !unit || unit->getHealth() <= 0 || !unit->getOwner() || unit->getOwner()->getTeamID() == myTeam
            || ArmyPosturePolicy::elapsed(now, it->second.cycle) > memory;
        it = gone ? enemySightings.erase(it) : std::next(it);
    }
}

int64_t QuantBot::enemyArmyEstimate() const {
    int64_t total = 0;
    for (const auto& [id, sighting] : enemySightings) total += sighting.value;
    return total;
}

int64_t QuantBot::ownGroundArmyValue() const {
    int64_t own = 0;
    for (const auto* unit : getUnitList())
        if (unit->getOwner() == getHouse() && groundFighter(unit)) own += healthValue(unit);
    return own;
}

bool QuantBot::outnumberedByEstimate() const {
    const Uint32 now = getGameCycleCount();
    if (estimateCycle != now) {
        estimateCycle = now;
        estimateOutnumbered = Force::outnumbered(ownGroundArmyValue(), enemyArmyEstimate());
    }
    return estimateOutnumbered;
}

int64_t QuantBot::groundValueOf(const std::vector<Uint32>& ids) const {
    int64_t total = 0;
    for (const Uint32 id : ids)
        if (const auto* unit = dynamic_cast<const UnitBase*>(getObject(id)); unit && unit->getHealth() > 0)
            total += healthValue(unit);
    return total;
}

std::vector<Uint32> QuantBot::homeReserve(const std::vector<Uint32>& ids) const {
    std::vector<Uint32> kept;
    const int share = Force::reserveShareBps(ownGroundArmyValue(), enemyArmyEstimate());
    if (share <= 0) return kept;
    const Coord home = protectedRally.isValid() ? protectedRally : findBaseCentre(getHouse()->getHouseID());
    if (home.isInvalid()) return kept;
    struct Entry { Uint32 id; int distance; int64_t value; };
    std::vector<Entry> entries;
    int64_t total = 0;
    for (const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getHealth() <= 0) continue;
        const int64_t value = healthValue(unit);
        entries.push_back({id, tiles(unit->getLocation(), home), value});
        total += value;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    int64_t value = 0;
    for (const auto& entry : entries) {
        if (value * 10000 >= total * share) break;
        kept.push_back(entry.id);
        value += entry.value;
    }
    return kept;
}

bool QuantBot::standsInBundle(const UnitBase* unit) const {
    if (!unit) return false;
    constexpr int kBundledTiles = 8;
    const Coord at = unit->getLocation();
    return (bundlePoint.isValid() && tiles(at, bundlePoint) <= kBundledTiles)
        || (bundleCentre.isValid() && tiles(at, bundleCentre) <= kBundledTiles);
}

// ---- Defending at the base perimeter (round 5: U and V, on by default) -----------------------

const QuantBot::Perimeter& QuantBot::perimeter() const {
    const Uint32 now = getGameCycleCount();
    if (perimeterCycle == now) return perimeterCache;
    perimeterCycle = now;
    perimeterCache = Perimeter{};
    const Coord anchor = findBaseCentre(getHouse()->getHouseID());
    if (anchor.isInvalid()) return perimeterCache;
    const Coord forward = observedFrontDirection(anchor);
    const int length = int(Force::isqrt(int64_t(forward.x) * forward.x + int64_t(forward.y) * forward.y));
    if (length <= 0) return perimeterCache;
    // The outermost own building of this base along the front (structure list order; the
    // maximum does not depend on it).
    int depth = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
            || tiles(structure->getLocation(), anchor) > Force::kPerimeterBaseTiles) continue;
        const Coord size = getStructureSize(structure->getItemID());
        for (const Coord corner : {structure->getLocation(), structure->getLocation() + size - Coord(1, 1)}) {
            const int along = int((int64_t(corner.x - anchor.x) * forward.x + int64_t(corner.y - anchor.y) * forward.y)
                                  / length);
            depth = std::max(depth, along);
        }
    }
    perimeterCache.anchor = anchor;
    perimeterCache.forward = forward;
    perimeterCache.length = length;
    perimeterCache.depth = depth;
    const int reach = depth + Force::kPerimeterStandoffTiles;
    const Coord desired(anchor.x + int(int64_t(forward.x) * reach / length), anchor.y + int(int64_t(forward.y) * reach / length));
    auto usable = [&](Coord p) {
        if (!getMap().tileExists(p)) return false;
        const auto* tile = getMap().getTile(p);
        return !tile->isMountain() && !tile->hasAStructure() && !tile->isSpiceBloom();
    };
    for (int ring = 0; ring <= 6 && perimeterCache.point.isInvalid(); ++ring)
        for (int dy = -ring; dy <= ring && perimeterCache.point.isInvalid(); ++dy)
            for (int dx = -ring; dx <= ring && perimeterCache.point.isInvalid(); ++dx)
                if (std::max(std::abs(dx), std::abs(dy)) == ring && usable(desired + Coord(dx, dy)))
                    perimeterCache.point = desired + Coord(dx, dy);
    return perimeterCache;
}

int QuantBot::perimeterTurretGoal() const {
    if (!forceVariant(kForceThreatTurrets)) return 0;
    return Force::perimeterTurretGoal(ownGroundArmyValue(), enemyArmyEstimate());
}

int QuantBot::perimeterTurrets() const {
    const Perimeter& p = perimeter();
    if (p.length <= 0) return 0;
    int count = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
            || structure->getItemID() != Structure_RocketTurret) continue;
        const int dx = structure->getLocation().x - p.anchor.x, dy = structure->getLocation().y - p.anchor.y;
        const int along = int((int64_t(dx) * p.forward.x + int64_t(dy) * p.forward.y) / p.length);
        const int across = int((int64_t(dy) * p.forward.x - int64_t(dx) * p.forward.y) / p.length);
        if (Force::onPerimeterBand(along, across, p.depth)) ++count;
    }
    return count;
}

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
    if (!splits.empty()) updateSplits();
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
    // A hunting launcher with an enemy already inside its close band belongs to the escape in
    // the unit pass (escapeCloseThreatWhileHunting): an attack or hunt-toward order here would
    // clear its spacing step. Once it stands off again it is an ordinary hunter and takes the
    // focus target like everyone else.
    auto escaping = [&](const UnitBase* unit) { return closeThreatWhileHunting(unit) != nullptr; };
    if (noOverkill) {
        std::vector<const UnitBase*> hunters;
        for (const auto* unit : getUnitList()) if (hunter(unit) && !escaping(unit)) hunters.push_back(unit);
        std::sort(hunters.begin(), hunters.end(),
            [](const UnitBase* a, const UnitBase* b) { return a->getObjectID() < b->getObjectID(); });
        focusFireNoOverkill(hunters, enemies);
    }
    for (const auto* unit : getUnitList()) {
        if (!hunter(unit) || escaping(unit)) continue;
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

// ---- Bundling the idle reserve (kForceBundle, on by default) ---------------------------------
//
// Before 1.0.602 the idle army stood at a forward rally (75% own base + 25% enemy base) or at
// its own centre of mass, so a wave left as one body. Bundling brings that back with the
// observed enemy base only, a bounded tile search every 30 s and at most kBundleOrdersPerPass
// orders per pass, farthest first; bundled troops keep no role, so base defence takes them like
// any other. The point, its clock and the launch gate's wait are saved (9863).

Coord QuantBot::forwardBundlePoint() {
    const Uint32 now = getGameCycleCount();
    const bool unsafe = bundlePoint.isValid() && (!getMap().tileExists(bundlePoint) || dangerAt(bundlePoint) != 0);
    if (!unsafe && bundleCycle != std::numeric_limits<Uint32>::max() && now >= bundleCycle
        && now - bundleCycle < Uint32(MILLI2CYCLES(30000))) return bundlePoint;
    bundleCycle = now;
    const Coord home = armyAssemblyAnchor();
    if (home.isInvalid()) { bundlePoint = Coord::Invalid(); return bundlePoint; }
    const Coord towards = observedFrontDirection(home);
    const Coord desired(Force::along(home.x, home.x + towards.x, Force::kBundleForwardPercent),
                        Force::along(home.y, home.y + towards.y, Force::kBundleForwardPercent));
    auto usable = [&](Coord p) {
        if (!getMap().tileExists(p)) return false;
        const auto* tile = getMap().getTile(p);
        return !tile->isMountain() && !tile->hasAStructure() && !tile->isSpiceBloom() && dangerAt(p) == 0;
    };
    bundlePoint = Coord::Invalid();
    for (int ring = 0; ring <= 8 && bundlePoint.isInvalid(); ++ring)
        for (int dy = -ring; dy <= ring && bundlePoint.isInvalid(); ++dy)
            for (int dx = -ring; dx <= ring && bundlePoint.isInvalid(); ++dx)
                if (std::max(std::abs(dx), std::abs(dy)) == ring && usable(desired + Coord(dx, dy)))
                    bundlePoint = desired + Coord(dx, dy);
    if (bundlePoint.isInvalid()) bundlePoint = home;
    traceDecision("force_bundle_point", AITelemetry::Record().set("x", bundlePoint.x).set("y", bundlePoint.y)
        .set("home_x", home.x).set("home_y", home.y).set("forward_x", towards.x).set("forward_y", towards.y));
    return bundlePoint;
}

bool QuantBot::bundleUnit(const UnitBase* unit, int radius, std::vector<BundleWalker>& walkers) {
    if (!unit || !unit->isRespondable() || unit->hasATarget() || unit->wasForced()
        || unit->getAttackMode() == HUNT || unit->getAttackMode() == RETREAT || holdsOpeningPosition(unit)
        || unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::Defence, UnitRole::ColonyGuard,
                           UnitRole::Escort, UnitRole::Wave}) != UnitRole::Free) return false;
    // A withdrawal or recovery brings reinforcements to the protected shelter: the home rally
    // (moveToOptimalSquadPosition) decides then, exactly as without bundling.
    if (reserveHeldHome()) return false;
    const Coord from = unit->getLocation();
    Coord at = bundlePoint;
    // The pre-1.0.602 rule: the closer of the forward rally and the army's centre of mass.
    if (bundleCentre.isValid() && (at.isInvalid() || tiles(from, bundleCentre) < tiles(from, at))) at = bundleCentre;
    if (at.isInvalid()) return false;
    if (unit->isMoving()) return true;
    const_cast<UnitBase*>(unit)->setGuardPoint(at);
    if (unit->getAttackMode() != AREAGUARD) doSetAttackMode(unit, AREAGUARD);
    if (tiles(from, at) <= radius || unit->hasPendingPathRequest()) return true;
    walkers.push_back({unit->getObjectID(), tiles(from, at), at});
    return true;
}

void QuantBot::orderBundleWalkers(std::vector<BundleWalker>& walkers, int radius) {
    if (walkers.empty() || currentGame->getPathRequestQueueSize() > 150) return;
    std::sort(walkers.begin(), walkers.end(), [](const BundleWalker& a, const BundleWalker& b) {
        return Force::bundleOrderFirst(a.distance, a.id, b.distance, b.id);
    });
    const size_t orders = std::min<size_t>(walkers.size(), Force::kBundleOrdersPerPass);
    std::set<int64_t> reserved;
    for (size_t i = 0; i < orders; ++i) {
        const auto* unit = static_cast<const UnitBase*>(getObject(walkers[i].id));
        Coord slot = huntPartySlot(unit, walkers[i].at, radius, reserved);
        if (slot.isInvalid()) slot = walkers[i].at;
        doMove2Pos(unit, slot.x, slot.y, false);
        ++forceStats.bundleOrders;
    }
}

void QuantBot::saveForceState(OutputStream& stream) const {
    stream.writeSint32(bundlePoint.x);
    stream.writeSint32(bundlePoint.y);
    stream.writeSint32(bundleCentre.x);
    stream.writeSint32(bundleCentre.y);
    stream.writeUint32(bundleCycle);
    stream.writeUint32(bundleGateSince);
    // Round 5: the remembered enemy army (id order) and the musters (creation order).
    stream.writeUint32(Uint32(enemySightings.size()));
    for (const auto& [id, sighting] : enemySightings) {
        stream.writeUint32(id);
        stream.writeSint64(sighting.value);
        stream.writeUint32(sighting.cycle);
    }
    stream.writeUint32(Uint32(musters.size()));
    for (const auto& muster : musters) {
        stream.writeUint32(muster.building);
        stream.writeSint32(muster.point.x);
        stream.writeSint32(muster.point.y);
        stream.writeUint32(muster.since);
        stream.writeUint32(Uint32(muster.members.size()));
        for (const Uint32 id : muster.members) stream.writeUint32(id);
    }
}

void QuantBot::loadForceState(InputStream& stream) {
    bundlePoint.x = stream.readSint32();
    bundlePoint.y = stream.readSint32();
    bundleCentre.x = stream.readSint32();
    bundleCentre.y = stream.readSint32();
    bundleCycle = stream.readUint32();
    bundleGateSince = stream.readUint32();
    const auto valid = [&](Coord p) { return p.isInvalid() || getMap().tileExists(p); };
    if (!valid(bundlePoint) || !valid(bundleCentre)) throw std::runtime_error("Invalid force bundle state");
    constexpr Uint32 kMaxEntries = 65536;
    enemySightings.clear();
    const Uint32 sightings = stream.readUint32();
    if (sightings > kMaxEntries) throw std::runtime_error("Invalid force sightings");
    for (Uint32 i = 0; i < sightings; ++i) {
        const Uint32 id = stream.readUint32();
        Sighting sighting;
        sighting.value = stream.readSint64();
        sighting.cycle = stream.readUint32();
        if (sighting.value < 0 || !enemySightings.emplace(id, sighting).second)
            throw std::runtime_error("Invalid force sighting");
    }
    musters.clear();
    const Uint32 count = stream.readUint32();
    if (count > kMaxEntries) throw std::runtime_error("Invalid force musters");
    for (Uint32 i = 0; i < count; ++i) {
        Muster muster;
        muster.building = stream.readUint32();
        muster.point.x = stream.readSint32();
        muster.point.y = stream.readSint32();
        muster.since = stream.readUint32();
        const Uint32 members = stream.readUint32();
        if (members > kMaxEntries || !getMap().tileExists(muster.point)) throw std::runtime_error("Invalid force muster");
        for (Uint32 m = 0; m < members; ++m) muster.members.insert(stream.readUint32());
        musters.push_back(std::move(muster));
    }
    estimateCycle = perimeterCycle = std::numeric_limits<Uint32>::max();
}

int QuantBot::bundledShareBps(const std::vector<Uint32>& ids, int radius) {
    int64_t total = 0, bundledValue = 0;
    for (const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getHealth() <= 0) continue;
        const int64_t price = std::max(100, currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
        total += price;
        const Coord at = unit->getLocation();
        if ((bundlePoint.isValid() && tiles(at, bundlePoint) <= radius)
            || (bundleCentre.isValid() && tiles(at, bundleCentre) <= radius)) bundledValue += price;
    }
    return total > 0 ? int(bundledValue * 10000 / total) : 10000;
}

// ---- Two-angle attack (kForceSplit / kForceFlank) --------------------------------------------
//
// The wave splits into two groups that walk (Area Guard, so they answer what they meet) to
// release points kSplitStandoffTiles from the objective, on directions 90 degrees apart (S:
// +-45 degrees off the approach; T: the main force head-on, the flank at 90 degrees). The first
// group there waits; both then hunt towards the objective at once. A group caught in a real fight
// on the way brings the other straight to it, and kSplitMaxMs bounds the wait.

const QuantBot::Split* QuantBot::splitOf(Uint32 id) const {
    for (const auto& split : splits)
        if (split.members[0].count(id) || split.members[1].count(id)) return &split;
    return nullptr;
}

bool QuantBot::startSplit(const std::vector<const UnitBase*>& units, const ObjectBase* objective) {
    if (!objective) return false;
    std::vector<const UnitBase*> walkers;
    int64_t sx = 0, sy = 0;
    const UnitBase* anchor = nullptr;
    for (const auto* unit : units) {
        if (!unit || !unit->isActive() || !unit->isAGroundUnit()) continue;
        walkers.push_back(unit);
        sx += unit->getLocation().x; sy += unit->getLocation().y;
        if (!anchor || (anchor->isInfantry() && !unit->isInfantry())) anchor = unit;
    }
    if (int(walkers.size()) < Force::kSplitMinMembers) return false;
    const Coord centre(int(sx / int64_t(walkers.size())), int(sy / int64_t(walkers.size())));
    const Coord target = objective->getClosestPoint(centre);
    if (tiles(centre, target) < Force::kSplitMinTiles) return false;
    auto release = [&](int degrees) {
        const auto p = Force::releasePoint({target.x, target.y}, {centre.x, centre.y}, Force::kSplitStandoffTiles, degrees);
        const Coord desired(std::clamp(p.x, 0, getMap().getSizeX() - 1), std::clamp(p.y, 0, getMap().getSizeY() - 1));
        return huntPartyPoint(desired, anchor, true);
    };
    const bool flank = forceVariant(kForceFlank) && !forceVariant(kForceSplit);
    Split split;
    if (flank) {
        // Group 0 the flank (the side whose release point is nearer), group 1 the main force.
        const Coord left = release(90), right = release(-90);
        split.point[0] = tiles(centre, right) < tiles(centre, left) ? right : left;
        split.point[1] = release(0);
    } else {
        split.point[0] = release(45);
        split.point[1] = release(-45);
    }
    if (split.point[0] == split.point[1] || tiles(split.point[0], split.point[1]) < Force::kSplitStandoffTiles / 2)
        return false;
    std::vector<Force::SplitMember> members;
    for (const auto* unit : walkers)
        members.push_back({unit->getObjectID(),
            std::max(100, currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price),
            tiles(unit->getLocation(), split.point[0]) - tiles(unit->getLocation(), split.point[1])});
    const auto groups = Force::splitGroups(members, flank ? Force::kFlankShareBps : 5000);
    for (size_t i = 0; i < walkers.size(); ++i) split.members[groups[i]].insert(walkers[i]->getObjectID());
    split.since = getGameCycleCount();
    split.objective = objective->getObjectID();
    split.target = target;
    for (int g = 0; g < 2; ++g) {
        const int radius = Force::bundleRadius(int(split.members[g].size()));
        std::set<int64_t> reserved;
        for (const auto* unit : walkers) {
            if (!split.members[g].count(unit->getObjectID())) continue;
            Coord slot = huntPartySlot(unit, split.point[g], radius, reserved);
            if (slot.isInvalid()) slot = split.point[g];
            doSetAttackMode(unit, AREAGUARD);
            const_cast<UnitBase*>(unit)->setGuardPoint(slot);
            doMove2Pos(unit, slot.x, slot.y, false);
        }
    }
    ++forceStats.splits;
    traceDecision("force_split", AITelemetry::Record().set("objective", split.objective)
        .set("target_x", target.x).set("target_y", target.y).set("flank", flank)
        .set("x0", split.point[0].x).set("y0", split.point[0].y).set("n0", int(split.members[0].size()))
        .set("x1", split.point[1].x).set("y1", split.point[1].y).set("n1", int(split.members[1].size())));
    splits.push_back(std::move(split));
    return true;
}

void QuantBot::updateSplits() {
    const Uint32 now = getGameCycleCount();
    const int myTeam = getHouse()->getTeamID();
    int orders = Force::kSplitOrdersPerPass;
    for (auto it = splits.begin(); it != splits.end();) {
        Split& split = *it;
        int alive[2] = {0, 0}, arrived[2] = {0, 0}, fighting[2] = {0, 0};
        Coord fight[2] = {Coord::Invalid(), Coord::Invalid()};
        for (int g = 0; g < 2; ++g) {
            const int radius = Force::bundleRadius(int(split.members[g].size())) + 3;
            for (auto m = split.members[g].begin(); m != split.members[g].end();) {
                const auto* unit = dynamic_cast<const UnitBase*>(getObject(*m));
                // Defence, repair or a human order takes a member; so does anything that made it hunt.
                const bool keep = unit && unit->getOwner() == getHouse() && unit->getHealth() > 0
                    && unit->getAttackMode() != HUNT
                    && unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::Defence}) == UnitRole::Free;
                if (!keep) { m = split.members[g].erase(m); continue; }
                ++alive[g];
                if (tiles(unit->getLocation(), split.point[g]) <= radius) ++arrived[g];
                const auto* target = unit->getTarget();
                if (target && target->getHealth() > 0 && target->getOwner() && target->getOwner()->getTeamID() != myTeam
                    && armed(target) && unit->isInWeaponRange(target)) {
                    ++fighting[g];
                    if (fight[g].isInvalid()) fight[g] = target->getLocation();
                }
                ++m;
            }
        }
        const auto* objective = getObject(split.objective);
        const bool objectiveGone = !objective || objective->getHealth() <= 0 || !objective->getOwner()
            || objective->getOwner()->getTeamID() == myTeam;
        const int fightingGroup = Force::splitFighting(fighting[0], alive[0]) ? 0
            : Force::splitFighting(fighting[1], alive[1]) ? 1 : -1;
        const bool timeout = ArmyPosturePolicy::elapsed(now, split.since) >= Uint32(MILLI2CYCLES(Force::kSplitMaxMs));
        const bool ready = Force::splitReady(arrived, alive);
        if (alive[0] == 0 || alive[1] == 0 || objectiveGone || fightingGroup >= 0 || timeout || ready) {
            // Attack together: towards the objective, or towards a group's fight.
            const Coord goal = fightingGroup >= 0 ? fight[fightingGroup] : split.target;
            for (int g = 0; g < 2; ++g)
                for (const Uint32 id : split.members[g]) {
                    const auto* unit = static_cast<const UnitBase*>(getObject(id));
                    if (!unit->isActive() || !unit->isRespondable()) continue;
                    if (unit->hasATarget() || objectiveGone) doSetAttackMode(unit, HUNT);
                    else doHuntToward(unit, goal.x, goal.y);
                }
            if (timeout && !ready && fightingGroup < 0) ++forceStats.splitTimeouts;
            ++forceStats.splitReleases;
            traceDecision("force_split_release", AITelemetry::Record().set("objective", split.objective)
                .set("alive0", alive[0]).set("arrived0", arrived[0]).set("fighting0", fighting[0])
                .set("alive1", alive[1]).set("arrived1", arrived[1]).set("fighting1", fighting[1])
                .set("ready", ready).set("timeout", timeout).set("objective_gone", objectiveGone)
                .set("elapsed_cycles", int(ArmyPosturePolicy::elapsed(now, split.since))));
            it = splits.erase(it);
            continue;
        }
        // Still walking or waiting: re-send whoever stands idle away from its point.
        for (int g = 0; g < 2 && orders > 0; ++g) {
            const int radius = Force::bundleRadius(int(split.members[g].size()));
            std::set<int64_t> reserved;
            for (const Uint32 id : split.members[g]) {
                if (orders <= 0) break;
                const auto* unit = static_cast<const UnitBase*>(getObject(id));
                if (!unit->isActive() || !unit->isRespondable() || unit->isMoving() || unit->hasPendingPathRequest()
                    || unit->hasATarget() || tiles(unit->getLocation(), split.point[g]) <= radius + 3) continue;
                Coord slot = huntPartySlot(unit, split.point[g], radius, reserved);
                if (slot.isInvalid()) slot = split.point[g];
                if (unit->getAttackMode() != AREAGUARD) doSetAttackMode(unit, AREAGUARD);
                const_cast<UnitBase*>(unit)->setGuardPoint(slot);
                doMove2Pos(unit, slot.x, slot.y, false);
                --orders;
            }
        }
        ++it;
    }
}
