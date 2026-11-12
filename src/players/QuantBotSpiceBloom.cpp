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

// QuantBot spice blooms: SpiceBloomHarvest (docs/ai-scenario-matrix.md, "Spice bloom harvest").
// A bloom bursts when a weapon hits it, spilling thin spice over the plain sand within five
// tiles; a ground unit that stops on it sets it off and dies. A house with the switch on sends
// one cheap idle ranged unit to shoot a bloom that its harvesters can use, and leaves a bloom
// alone while spice it knows of already lies around it. Everything read is what the house has
// explored or currently sees; nothing is remembered that a reloaded game could not rebuild.

#include "QuantBotInternal.h"

#include <players/QuantBotSpiceBloomPolicy.h>

#include <algorithm>

bool QuantBot::spiceBloomHarvestScope() const {
    if (botConfig().force.spiceBloomHarvest <= 0 || !autonomousCustomArmy() || fighterOnly() || isAlliedWithHuman())
        return false;
    const auto& init = getGameInitSettings();
    return !init.isCivilizationWars() && init.getModName() != "CivWars";
}

void QuantBot::updateSpiceBloomHarvest(const HarvesterRaidPlan& raid) {
    if (botConfig().force.spiceBloomHarvest <= 0
        || !QuantBotSpiceBloomPolicy::evaluationDue(getGameCycleCount()) || !spiceBloomHarvestScope()) return;
    AITelemetry::PerformanceScope scope("ai.spiceBloomHarvest", getGameCycleCount(), getHouse()->getHouseID());
    orderSpiceBloomShot(raid);
}

bool QuantBot::spiceBloomEmergency() const {
    // A withdrawal, recovery or garrison holds the army at home.
    if (reserveHeldHome()) return true;
    // This pass's army survey (recovery-enabled houses): a core building is under attack.
    if (lastSurveyCycle == getGameCycleCount() && lastSurvey.coreUnderAttack) return true;
    // A scramble to one of our buildings or a live hostile contact; standing guard posts are not.
    for (const auto& assignment : defenceAssignments) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(assignment.first));
        if (unit && !vanillaCoreGuardAssignment(unit) && activeDefenceAssignment(unit)) return true;
    }
    return false;
}

bool QuantBot::spiceBloomShooter(const UnitBase* unit, const HarvesterRaidPlan& raid) const {
    namespace Policy = QuantBotSpiceBloomPolicy;
    // Our own ground units only: a deviated enemy unit is ours for moments.
    if (!orderableCombatUnit(unit) || !unit->isAGroundUnit() || unit->isInFormation()
        || unit->getOriginalHouseID() != getHouse()->getHouseID()) return false;
    // Idle: no target, no order of its own, guarding rather than hunting or retreating.
    if (unit->hasATarget() || unit->wasForced() || unit->getAttackPos().isValid()
        || (unit->getAttackMode() != GUARD && unit->getAttackMode() != AREAGUARD)) return false;
    const Uint32 item = unit->getItemID();
    if (item == Unit_Deviator || item == Unit_SonicTank || !Policy::burstingBullet(unit->getBulletType())
        || unit->getWeaponRange() < Policy::kMinimumRange) return false;
    const Uint32 id = unit->getObjectID();
    if (huntPartyOf(id) || forceHeld(id) || styleRaiders.count(id) || duelOpeningRaider(unit) || raid.contains(unit))
        return false;
    return unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::BaseDefence, UnitRole::LiveDefence,
                           UnitRole::Defence, UnitRole::Wave, UnitRole::Escort, UnitRole::ColonyGuard})
        == UnitRole::Free;
}

bool QuantBot::orderSpiceBloomShot(const HarvesterRaidPlan& raid) {
    namespace Policy = QuantBotSpiceBloomPolicy;
    const Map& map = getMap();
    if (!spiceBloomSitesScanned) {
        // Blooms are only ever placed by the map, so one scan finds every bloom there will be.
        spiceBloomSitesScanned = true;
        for (int x = 0; x < map.getSizeX(); ++x)
            for (int y = 0; y < map.getSizeY(); ++y) {
                const Tile* tile = map.getTile(x, y);
                if (tile->isSpiceBloom() || tile->isSpecialBloom())
                    spiceBloomSites.push_back({Coord(x, y), tile->isSpecialBloom()});
            }
    }
    spiceBloomSites.erase(std::remove_if(spiceBloomSites.begin(), spiceBloomSites.end(),
        [&](const SpiceBloomSite& site) {
            const Tile* tile = map.getTile(site.at);
            return site.special ? !tile->isSpecialBloom() : !tile->isSpiceBloom();
        }), spiceBloomSites.end());
    if (std::none_of(spiceBloomSites.begin(), spiceBloomSites.end(),
                     [](const SpiceBloomSite& site) { return !site.special; })
        || getAIHarvesterCount() <= 0 || spiceBloomEmergency()) return false;

    // One shot at a time: a unit of ours already ordered at a bloom fires first. Our orders are
    // forced, and firing ends them; an unforced one is a walk the engine gave up for want of a
    // path, which would leave the unit standing inert, so it is handed back to the controllers.
    std::vector<const UnitBase*> shooters;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() != getHouse()) continue;
        const Coord& aim = unit->getAttackPos();
        if (aim.isValid() && map.tileExists(aim) && map.getTile(aim)->isSpiceBloom()) {
            if (unit->wasForced() || !unit->isActive()) return false;
            doMove2Pos(unit, unit->getX(), unit->getY(), false);
            continue;
        }
        if (spiceBloomShooter(unit, raid)) shooters.push_back(unit);
    }
    if (shooters.empty()) return false;

    // Our buildings, harvester dropoffs and the enemy buildings this team has explored.
    const int team = getHouse()->getTeamID();
    std::vector<const StructureBase*> own, dropoffs, enemy;
    for (const auto* structure : getStructureList()) {
        if (structure->getHealth() <= 0 || structure->getItemID() == Structure_Wall || !structure->getOwner()) continue;
        if (structure->getOwner() == getHouse()) {
            own.push_back(structure);
            if (structure->acceptsHarvesterDropoff()) dropoffs.push_back(structure);
        } else if (structure->getOwner()->getTeamID() != team
                   && map.getTile(structure->getLocation())->isExploredByTeam(team)) {
            enemy.push_back(structure);
        }
    }
    if (dropoffs.empty()) return false;
    const auto distance = [](const StructureBase* structure, Coord at) {
        return blockDistance(at, structure->getClosestPoint(at)).lround();
    };

    const int level = botConfig().force.spiceBloomHarvest;
    const int spill = Policy::kSpillRadius, measure = Policy::kMeasureRadius;
    struct Eligible { int dropoffDistance; size_t site; int region; int64_t knownSpice; FixPoint clearance; };
    std::vector<Eligible> eligible;
    for (size_t index = 0; index < spiceBloomSites.size(); ++index) {
        auto& site = spiceBloomSites[index];
        const Coord at = site.at;
        if (site.special || !map.getTile(at)->isExploredByTeam(team)) continue;
        // Reachable for our harvesters: a dropoff of ours in reach, on the same ground.
        const int region = map.vehicleTerrainRegion(at);
        if (region == 0) continue;
        int dropoff = -1;
        for (const auto* structure : dropoffs) {
            const int d = distance(structure, at);
            if (d <= Policy::kDropoffReach && (dropoff < 0 || d < dropoff)
                && map.vehicleTerrainRegion(structure->getLocation()) == region) dropoff = d;
        }
        if (dropoff < 0) continue;
        int ownDistance = -1, enemyDistance = -1;
        for (const auto* structure : own) {
            const int d = distance(structure, at);
            if (ownDistance < 0 || d < ownDistance) ownDistance = d;
        }
        for (const auto* structure : enemy) {
            const int d = distance(structure, at);
            if (enemyDistance < 0 || d < enemyDistance) enemyDistance = d;
        }
        if (!Policy::ownSide(ownDistance, enemyDistance)) continue;
        // No observed threat over the field to be, and nobody beside the impact.
        if (dangerAt(at - Coord(spill, spill), Coord(2 * spill + 1, 2 * spill + 1)) > 0) continue;
        bool occupied = false;
        for (int dx = -1; dx <= 1 && !occupied; ++dx)
            for (int dy = -1; dy <= 1 && !occupied; ++dy)
                occupied = map.tileExists(at.x + dx, at.y + dy) && map.getTile(at.x + dx, at.y + dy)->hasAGroundObject();
        if (occupied) continue;
        // The waste test: the spice this team knows of within the spill radius and its margin.
        int tiles = 0, explored = 0;
        int64_t knownSpice = 0;
        for (int dx = -measure; dx <= measure; ++dx)
            for (int dy = -measure; dy <= measure; ++dy) {
                if (dx * dx + dy * dy > measure * measure || !map.tileExists(at.x + dx, at.y + dy)) continue;
                ++tiles;
                const Tile* tile = map.getTile(at.x + dx, at.y + dy);
                if (!tile->isExploredByTeam(team)) continue;
                ++explored;
                knownSpice += tile->getSpice().lround();
            }
        if (!Policy::enoughKnown(explored, tiles)) continue;
        if (Policy::wasteful(level, knownSpice)) {
            if (!site.wasteNoted) {
                site.wasteNoted = true;
                ++spiceBloomStats.wasteful;
                traceDecision("spice_bloom_wasteful", AITelemetry::Record()
                    .set("x", at.x).set("y", at.y).set("level", level).set("known_spice", knownSpice)
                    .set("limit", Policy::wasteLimit(level)).set("explored_tiles", explored).set("tiles", tiles));
            }
            continue;
        }
        // A shooter stops within its weapon range of the bloom: never on another bloom.
        FixPoint clearance = FixPoint(map.getSizeX() + map.getSizeY());
        for (const auto& other : spiceBloomSites)
            if (!(other.at == at)) clearance = std::min(clearance, blockDistance(at, other.at));
        eligible.push_back({dropoff, index, region, knownSpice, clearance});
    }
    // Nearest our dropoffs first; the site order (the map scan) breaks ties.
    std::sort(eligible.begin(), eligible.end(), [](const Eligible& a, const Eligible& b) {
        return std::tie(a.dropoffDistance, a.site) < std::tie(b.dropoffDistance, b.site);
    });
    for (const auto& bloom : eligible) {
        const Coord at = spiceBloomSites[bloom.site].at;
        const UnitBase* best = nullptr;
        Policy::Shooter bestKey;
        for (const auto* unit : shooters) {
            const int gap = blockDistance(unit->getLocation(), at).lround();
            if (gap > Policy::kShooterReach || FixPoint(unit->getWeaponRange()) >= bloom.clearance
                || map.vehicleTerrainRegion(unit->getLocation()) != bloom.region
                || dangerAt(unit->getLocation()) > 0) continue;
            const Policy::Shooter key{
                currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].price,
                unit->isMoving(), gap, unit->getObjectID()};
            if (!best || Policy::betterShooter(key, bestKey)) { best = unit; bestKey = key; }
        }
        if (!best) continue;
        doAttackPos(best, at.x, at.y, true);
        ++spiceBloomStats.orders;
        traceDecision("spice_bloom_order", AITelemetry::Record()
            .set("unit", best->getObjectID()).set("item", best->getItemID()).set("price", bestKey.price)
            .set("distance", bestKey.distance).set("x", at.x).set("y", at.y).set("level", level)
            .set("known_spice", bloom.knownSpice).set("dropoff_distance", bloom.dropoffDistance));
        return true;
    }
    return false;
}
