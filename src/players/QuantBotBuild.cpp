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

// QuantBot build and economy: the build pass and its planning stages (one pass state in
// players/QuantBotBuildPass.h; producer turns in QuantBotBuildProducers.cpp and yard turns
// in QuantBotBuildYard.cpp), repair-yard and factory-loss gates, production multipliers
// and city building management.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>
#include <players/QuantBotArmyCounterPolicy.h>
#include <cstdlib>
#include <string_view>

using QuantBotInternal::rocketCoverageTier;

int QuantBot::recentFactoryLossCount() const {
    return std::count_if(recentStructureLosses.begin(), recentStructureLosses.end(), [&](const auto& loss) {
        return loss.item == Structure_HeavyFactory && getGameCycleCount()-loss.cycle < MILLI2CYCLES(120000);
    });
}

bool QuantBot::campaignEconomySupport() const {
    return currentGame && !currentGame->isCitySimEnabled() && !builderOnly()
        && campaignPolicyApplies() && isAlliedWithHuman();
}

int QuantBot::campaignRefineryReplacementCost(BuildPass& pass) {
    if (!botConfig().force.campaignRefineryDefence || !campaignEconomySupport()
        || lastCalculatedSpice <= 0 || getHouse()->getNumLostItems(Structure_Refinery) == 0)
        return 0;
    const int target = QuantBotBuildPolicy::desiredSpiceRefineries(
        pass.spiceHarvesterTarget, pass.itemCount[Unit_Harvester]);
    if (pass.itemCount[Structure_Refinery] >= target) return 0;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getItemID() == Structure_ConstructionYard
            && campaignAvailableToBuild(static_cast<const BuilderBase*>(structure), Structure_Refinery))
            return buildingCapitalCost(pass, Structure_Refinery);
    return 0;
}

bool QuantBot::simpleTechPolicyActive() const {
    return currentGame && botConfig().force.simpleTechSavings && gameMode == GameMode::Custom
        && !campaignPolicyApplies() && !builderOnly()
        && (difficulty == Difficulty::Medium || difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal);
}

bool QuantBot::economicFleetRatioActive() const {
    const auto& force = botConfig().force;
    return (force.economicFleetRatios || (force.cityFleetRatios > 0 && cityEconomyExperimentScope()))
        && currentGame && currentGameMap
        && lastCalculatedSpice > 0 && (difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal)
        && !builderOnly()
        && !ModManager::instance().isTornieFamilyContentActive()
        && !currentGame->getGameInitSettings().isCivilizationWars()
        && currentGame->getGameInitSettings().getModName() != "CivWars"
        && (!campaignPolicyApplies() || isAlliedWithHuman());
}

// Read only while economicFleetRatioActive(): an explicit EconomicFleetRatios
// configuration keeps its values, otherwise CityFleetRatios' fixed level applies.
QuantBotBuildPolicy::FleetRatioLevel QuantBot::fleetRatioTreatment() const {
    const auto& force = botConfig().force;
    if (force.economicFleetRatios)
        return {force.fleetHarvestersPerBay, force.fleetCarryallQuarters, force.fleetRepairYards};
    return QuantBotBuildPolicy::cityFleetRatioLevel(force.cityFleetRatios);
}

void QuantBot::planFleetResourceBudget(BuildPass& pass) {
    pass.fleetRatios = economicFleetRatioActive();
    if (!pass.fleetRatios) return;
    // Recompute from current deterministic state. Mobile traffic does not erase
    // a field's long-term accessibility; mountains and structure footprints do.
    const int width = getMap().getSizeX(), height = getMap().getSizeY();
    fleetSurveySeen.assign(size_t(width) * height, 0);
    fleetSurveyQueue.clear();
    fleetSurveyQueue.reserve(size_t(width) * height);
    auto visit = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const int index = y * width + x;
        if (fleetSurveySeen[index]) return;
        fleetSurveySeen[index] = 1;
        const auto* tile = getMap().getTile(x, y);
        if (!tile->isMountain() && !tile->hasAStructure()) fleetSurveyQueue.push_back(index);
    };
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
            || (structure->getItemID() != Structure_ConstructionYard
                && !structure->acceptsHarvesterDropoff())) continue;
        const Coord at = structure->getLocation(), size = getStructureSize(structure->getItemID());
        for (int x = at.x - 1; x <= at.x + size.x; ++x) {
            visit(x, at.y - 1); visit(x, at.y + size.y);
        }
        for (int y = at.y; y < at.y + size.y; ++y) {
            visit(at.x - 1, y); visit(at.x + size.x, y);
        }
    }
    int64_t available = 0;
    size_t cursor = 0;
    int distance = 0;
    while (cursor < fleetSurveyQueue.size()) {
        const size_t layerEnd = fleetSurveyQueue.size();
        while (cursor < layerEnd) {
            const int index = fleetSurveyQueue[cursor++], x = index % width, y = index / width;
            const auto* tile = getMap().getTile(x, y);
            if (tile->hasSpice()) {
                available += tile->getSpice().lround();
                if (pass.fleetNearestSpiceDistance < 0) pass.fleetNearestSpiceDistance = distance;
            }
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy) visit(x + dx, y + dy);
        }
        ++distance;
    }
    // Initial occupied controllers define competition, never runtime House
    // objects: eliminations cannot activate a different map/format scope.
    pass.spiceCompetitors = 0;
    for (const auto& house : currentGame->getGameInitSettings().getHouseInfoList())
        if (!house.playerInfoList.empty()) ++pass.spiceCompetitors;
    pass.fleetSpiceShare = int(std::min<int64_t>(std::numeric_limits<int>::max(),
        available / std::max(1, pass.spiceCompetitors)));
}

namespace {
bool repairWaitingUnit(const UnitBase* unit, const House* owner) {
    if (unit->getOwner() != owner || unit->getHealth() <= 0 || !unit->isAGroundUnit()
        || unit->isInfantry() || unit->getItemID() == Unit_Saboteur
        || !unit->isEligibleForRepair() || unit->getHealth() >= unit->getMaxHealth()) return false;
    const auto* target = unit->getTarget();
    const bool repairTrip = target && target->getOwner() == owner
        && target->getItemID() == Structure_RepairYard;
    return repairTrip || (unit->isBadlyDamaged() && !unit->wasForced());
}
bool repairGroundWaiting(const UnitBase* unit, const House* owner) {
    if (!repairWaitingUnit(unit, owner) || !unit->isActive() || unit->isPickedUp()) return false;
    const auto* ground = static_cast<const GroundUnit*>(unit);
    return !ground->hasBookedCarrier() && !ground->isAwaitingPickup();
}
bool repairingOccupant(const RepairYard* yard) {
    const auto* unit = yard->getContainedRepairUnit();
    return unit && unit->getHealth() > 0 && unit->getHealth() < unit->getMaxHealth();
}
}

bool QuantBot::repairCapacityDemandActive() const {
    return botConfig().force.repairCapacityDemand && currentGame && gameMode == GameMode::Custom
        && !campaignPolicyApplies();
}

bool QuantBot::repairYardAdmitted(const BuildPass& pass) const {
    const int committed = pass.itemCount[Structure_RepairYard];
    if (!pass.repairCapacityDemand) return canAddRepairYard(committed);
    return QuantBotBuildPolicy::repairCapacityAdmitted(canAddRepairYard(committed),
        getHouse()->getNumItems(Structure_RepairYard), committed, pass.repairTarget);
}

void QuantBot::planRepairCapacity(BuildPass& pass, std::set<Uint32>& repairQueue) {
    pass.repairCapacityDemand = repairCapacityDemandActive();
    if (!pass.repairCapacityDemand && !AITelemetry::log().enabled()) return;
    const int actual = getHouse()->getNumItems(Structure_RepairYard);
    const int committed = pass.itemCount[Structure_RepairYard];
    // getRepairUnit() can retain a historical pointer after deployment. Only
    // actual containment proves a running job or a completed return wait.
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getItemID() != Structure_RepairYard
            || structure->getHealth() <= 0) continue;
        const auto* yard = static_cast<const RepairYard*>(structure);
        if (const auto* unit = yard->getContainedRepairUnit()) {
            repairQueue.erase(unit->getObjectID());
        }
    }
    for (auto it = repairQueue.begin(); it != repairQueue.end();) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(*it));
        if (!unit || !repairWaitingUnit(unit, getHouse())) { it = repairQueue.erase(it); continue; }
        if (!repairGroundWaiting(unit, getHouse())) {
            if (unit->isPickedUp() || static_cast<const GroundUnit*>(unit)->hasBookedCarrier())
                ++pass.inFlightRepairVehicles;
            else ++pass.transportBlockedRepairVehicles;
            it = repairQueue.erase(it);
        } else ++it;
    }
    Uint32 pressured = NONE_ID;
    int bestWaiting = -1;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getItemID() != Structure_RepairYard
            || structure->getHealth() <= 0) continue;
        const Coord at = structure->getLocation(), size = structure->getStructureSize();
        QuantBotBuildPolicy::RepairServiceAccess access;
        access.survey(at.x, at.y, size.x, size.y, [&](int x, int y) {
            if (!getMap().tileExists(x, y)) return false;
            const auto* tile = getMap().getTile(x, y);
            return !tile->isMountain() && !tile->hasAStructure();
        });
        int usable = 0, busy = 0, waiting = 0;
        for (const auto* other : getStructureList()) {
            if (other->getOwner() != getHouse() || other->getItemID() != Structure_RepairYard
                || other->getHealth() <= 0) continue;
            const Coord otherAt = other->getLocation(), otherSize = other->getStructureSize();
            if (!access.touches(otherAt.x, otherAt.y, otherSize.x, otherSize.y)) continue;
            ++usable;
            if (repairingOccupant(static_cast<const RepairYard*>(other))) ++busy;
        }
        for (const auto* unit : getUnitList()) {
            if (!repairGroundWaiting(unit, getHouse())
                || !access.reaches(unit->getX(), unit->getY())) continue;
            ++waiting;
            access.addWaiting(unit->getX(), unit->getY());
            // Reuse the legacy set to deduplicate the observational total.
            pass.serviceableRepairVehicles += int(repairQueue.erase(unit->getObjectID()));
        }
        const bool pressure = usable > 0 && usable == busy && waiting >= 2
            && getHouse()->getCredits() >= UNIT_REPAIRCOST && committed == actual;
        const bool retained = pressure && structure->getObjectID() == repairPressureYard;
        if (retained || (pressured == NONE_ID && (pressure || waiting > bestWaiting))) {
            pass.usableLocalRepairBays = usable;
            pass.busyLocalRepairBays = busy;
            pass.waitingLocalRepairVehicles = waiting;
            bestWaiting = waiting;
            if (pass.repairCapacityDemand) repairPlacementAccess = access;
            if (pressure) pressured = structure->getObjectID();
        }
    }
    repairPlacementReady = pass.repairCapacityDemand && pressured != NONE_ID;
    for (auto it = repairQueue.begin(); it != repairQueue.end();) {
        const auto* unit = static_cast<const UnitBase*>(getObject(*it));
        const auto* target = unit->getTarget();
        if (unit->getAttackMode() == CARRYALLREQUESTED && target
            && target->getOwner() == getHouse() && target->getItemID() == Structure_RepairYard) {
            ++pass.transportBlockedRepairVehicles;
            it = repairQueue.erase(it);
        } else ++it;
    }
    pass.accessBlockedRepairVehicles = int(repairQueue.size());
    if (pass.repairCapacityDemand) {
        pass.repairPressureCycles = QuantBotBuildPolicy::observeRepairPressure(
            pressured, getGameCycleCount(), repairPressureYard, repairPressureSince);
        pass.repairBaseline = QuantBotBuildPolicy::firstRepairYardTarget(pass.combatVehicles, pass.actualHarvesters);
        pass.repairTarget = QuantBotBuildPolicy::repairCapacityTarget(pass.repairBaseline > 0,
            actual, committed, pass.usableLocalRepairBays, pass.busyLocalRepairBays,
            pass.waitingLocalRepairVehicles, pass.repairPressureCycles);
        pass.repairCapacityReason = committed > actual ? "pending_capacity"
            : actual == 0 ? "first_repair_or_technology"
            : pressured == NONE_ID ? "no_serviceable_saturation"
            : pass.repairTarget > actual ? "sustained_serviceable_backlog" : "observing_backlog";
    }
    if (AITelemetry::log().enabled())
        traceDecision("repair_capacity", AITelemetry::Record()
            .set("treatment", pass.repairCapacityDemand).set("actual", actual).set("committed", committed)
            .set("busy", pass.observedBusyRepairBays).set("completed_waiting", pass.completedRepairReturns)
            .set("serviceable_waiting", pass.serviceableRepairVehicles)
            .set("transport_blocked", pass.transportBlockedRepairVehicles)
            .set("transport_inflight", pass.inFlightRepairVehicles)
            .set("access_blocked", pass.accessBlockedRepairVehicles)
            .set("usable_local", pass.usableLocalRepairBays).set("busy_local", pass.busyLocalRepairBays)
            .set("waiting_local", pass.waitingLocalRepairVehicles).set("pressure_cycles", pass.repairPressureCycles)
            .set("target", pass.repairTarget).set("reason", pass.repairCapacityReason)
            .set("quality", AITelemetry::log().repairQuality(getHouse()->getHouseID(), getGameCycleCount())));
}

Coord QuantBot::repairCapacitySite(const BuildPass& pass) {
    const Coord site = findPlaceLocation(Structure_RepairYard);
    if (!pass.repairCapacityDemand || getHouse()->getNumItems(Structure_RepairYard) == 0) return site;
    if (!repairYardAdmitted(pass) || !repairPlacementReady || site.isInvalid()) return Coord::Invalid();
    const Coord size = getStructureSize(Structure_RepairYard);
    return repairPlacementAccess.waitingFor(site.x, site.y, size.x, size.y) >= 2 ? site : Coord::Invalid();
}

bool QuantBot::canAddRepairYard(int includingQueued) const {
    if (economicFleetRatioActive())
        return includingQueued < fleetRatioTreatment().repairYards;
    return (currentGame && currentGame->isCitySimEnabled())
        || difficulty != Difficulty::Medium
        || includingQueued < initialItemCount[Structure_RepairYard]
        // The savings ladder funds one real repair/technology prerequisite on
        // Medium too. This is not permission for repeat capacity or campaigns.
        || (includingQueued == 0 && currentGame
            && simpleTechPolicyActive()
            && gameMode == GameMode::Custom && !campaignPolicyApplies() && !builderOnly());
}

bool QuantBot::civWarsRules() const {
    if (!currentGame || !botConfig().civWarsStrategy) return false;
    const auto& init = currentGame->getGameInitSettings();
    if (!init.getGameOptions().researchLab
        || !(init.isCivilizationWars() || init.getModName() == "CivWars")) return false;
    // Only duels beat switch-off opponents in direct tests. Use the original
    // roster so eliminations never activate this strategy during a team game.
    const auto& houses = init.getHouseInfoList();
    return houses.size() == 2 && houses[0].team != houses[1].team
        && !houses[0].playerInfoList.empty() && !houses[1].playerInfoList.empty();
}

bool QuantBot::isCivWarsMatch() const {
    return civWarsRules() && currentGame->getGameInitSettings().isCivilizationWars();
}

bool QuantBot::palaceExperimentScope() const {
    return currentGame && gameMode == GameMode::Custom && difficulty >= Difficulty::Hard
        && !builderOnly() && !campaignPolicyApplies()
        && (currentGame->getGameInitSettings().getGameType() == GameType::CustomGame
            || currentGame->getGameInitSettings().getGameType() == GameType::CustomMultiplayer)
        && currentGame->getGameInitSettings().getInitialSpiceTilePermille() > 0;
}

bool QuantBot::palaceWeaponDue() const {
    const int level = botConfig().force.palaceWaveTiming;
    if (level == 0 || !palaceExperimentScope()) return true;
    int hunters = 0, engaged = 0;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() != getHouse() || unit->getHealth() <= 0 || !unit->isActive()
            || !unit->isAGroundUnit() || !unit->canAttack() || !unit->isRespondable()
            || unit->getAttackMode() != HUNT || unit->getItemID() == Unit_Saboteur) continue;
        ++hunters;
        const auto* target = unit->getTarget();
        if (target && target->getOwner() && target->getOwner()->getTeamID() != getHouse()->getTeamID()
            && blockDistance(unit->getLocation(), target->getLocation()) <= unit->getWeaponRange()+5)
            ++engaged;
    }
    // A bounded, world-derived release window avoids a saved timing cache and never
    // strands a ready Palace when no viable army can depart.
    const Uint32 period = MILLI2CYCLES(level == 1 ? 60000 : 120000);
    return (level == 1 ? hunters >= 3 : engaged >= 3)
        || getGameCycleCount() % period < Uint32(MILLI2CYCLES(5000));
}

const ObjectBase* QuantBot::palaceValueTarget() const {
    const int level = botConfig().force.palaceValueTarget;
    if (level == 0 || !palaceExperimentScope()) return nullptr;
    struct Asset { const ObjectBase* object; int value; bool enemy; };
    std::vector<Asset> assets;
    const auto append = [&](const ObjectBase* object) {
        if (!object->getOwner() || object->getOwner()->isNatives() || !object->isActive()
            || object->getHealth() <= 0 || object->isAFlyingUnit()) return;
        const auto item = object->getItemID();
        const bool economy = item == Structure_Refinery || QuantBotInternal::harvesterItem(item)
            || item == Structure_HeavyFactory || item == Structure_LightFactory
            || item == Structure_HighTechFactory || item == Structure_ConstructionYard;
        if (!economy && (level == 1 || !object->isAUnit() || !object->canAttack())) return;
        const int value = currentGame->objectData.data[item][object->getProductionHouseID()].price;
        if (value > 0) assets.push_back({object, value, object->getOwner()->getTeamID() != getHouse()->getTeamID()});
    };
    for (const auto* structure : getStructureList()) append(structure);
    for (const auto* unit : getUnitList()) append(unit);
    const ObjectBase* best = nullptr;
    int64_t bestScore = 0;
    for (const auto& candidate : assets) {
        if (!candidate.enemy) continue;
        int64_t score = 0;
        for (const auto& asset : assets) {
            const int distance = std::max(std::abs(candidate.object->getX()-asset.object->getX()),
                                          std::abs(candidate.object->getY()-asset.object->getY()));
            if (distance <= 5) score += int64_t(asset.value)*(6-distance)*(asset.enemy ? 1 : -2);
        }
        if (score > bestScore || (score == bestScore && score > 0 && best
            && candidate.object->getObjectID() < best->getObjectID())) {
            bestScore = score;
            best = candidate.object;
        }
    }
    return best;
}

void QuantBot::firePalaceWeapon(const Palace* pPalace) {
    if (!pPalace->isSpecialWeaponReady() || !palaceWeaponDue()) return;
    const int houseID = getHouse()->getHouseID();
    if (houseID != HOUSE_HARKONNEN && houseID != HOUSE_SARDAUKAR) {
        doSpecialWeapon(pPalace);
        placementCache.clear(); // Palace summons can occupy previously free sites.
        return;
    }
    if (const auto* valued = palaceValueTarget()) {
        traceDecision("palace_value_aim", AITelemetry::Record().set("target", valued->getObjectID())
            .set("item", valued->getItemID()).set("x", valued->getX()).set("y", valued->getY())
            .set("target_level", botConfig().force.palaceValueTarget)
            .set("timing_level", botConfig().force.palaceWaveTiming));
        doLaunchDeathhand(pPalace, valued->getX(), valued->getY());
        return;
    }
    int enemyHouseID = -1;
    int enemyHouseBuildingCount = 0;
    for (int i = 0; i < NUM_HOUSES; i++) {
        // The Natives have no base to strike.
        if (getHouse(i) != nullptr && !getHouse(i)->isNatives() && getHouse(i)->getTeamID() != getHouse()->getTeamID()
            && getHouse(i)->getNumStructures() > enemyHouseBuildingCount) {
            enemyHouseBuildingCount = getHouse(i)->getNumStructures();
            enemyHouseID = i;
        }
    }
    if (enemyHouseID != -1) {
        Coord target = findBestDeathHandTarget(enemyHouseID);
        if (target.isValid()) doLaunchDeathhand(pPalace, target.x, target.y);
    }
}

void QuantBot::fighterPass() {
    // The Fighter's share of build(): the retreat point and the palace weapons. It never
    // produces, places, repairs or cancels anything (docs/ai-roles.md).
    if (squadRetreatLocation.isInvalid()) squadRetreatLocation = findSquadRetreatLocation();
    for (const StructureBase* pStructure : getStructureList()) {
        if (pStructure->getOwner() == getHouse() && pStructure->getItemID() == Structure_Palace)
            firePalaceWeapon(static_cast<const Palace*>(pStructure));
    }
}

void QuantBot::build(int militaryValue) {
    AITelemetry::PerformanceScope perfScope("ai.build", getGameCycleCount(), getHouse()->getHouseID());
    AITelemetry::PerformanceScope phaseScope("ai.build.evaluate",getGameCycleCount(),getHouse()->getHouseID());
    BuildPass pass(militaryValue);
    resetBuildPass();
    planBuildInventory(pass);
    planBuildBudget(pass);
    planCityState(pass);
    planStrategicReserve(pass);
    planSpiceEconomy(pass);
    planPowerAndYards(pass);
    planCityPopulation(pass);
    planCivicNeedAndTrips(pass);
    traceBuildState(pass);
    planUnitMix(pass);
    planCapitalForecast(pass);
    planCityBootstrap(pass);
    planOpeningProduction(pass);
    planDefenceCoverage(pass);
    planVanillaCoreDefence(pass);
    planCityResearchOrder(pass);
    planTechSavings(pass);
    planCapitalCandidates(pass);
    planVanillaBoldCapital(pass);
    chooseCapitalPlan(pass);
    if (!pass.vanillaBoomArmy) planTechResearch(pass);
    if (pass.techSavingActive && pass.techResearch) startTechResearch(pass);

    phaseScope.next("ai.build.orders");
    planBuildOrder(pass);
    for (const auto& entry : pass.planningOrder) {
        const auto* pStructure = dynamic_cast<const StructureBase*>(getObject(entry.second));
        if (pStructure && pStructure->getOwner() == getHouse()) {
            buildRepair(pass, pStructure);

            // Special weapon launch logic (not for support AI)
            if (pStructure->getItemID() == Structure_Palace && !builderOnly()) {
                firePalaceWeapon(static_cast<const Palace*>(pStructure));
            }

            if (pStructure->isABuilder()) buildProducer(pass, pStructure);
        }
    }
    if (!civWarsRules() && !researchRequired(pass)) startTechResearch(pass);

    traceCapitalOutcome(pass);
    // MULTIPLAYER FIX: Use deterministic timer instead of random
    buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
}

void QuantBot::planCityResearchOrder(BuildPass& pass) {
    const int level = botConfig().force.cityResearchOrder;
    if (level == 0 || !pass.citySimEnabled || gameMode != GameMode::Custom
        || campaignPolicyApplies() || !researchLabPolicyActive()
        || !ModManager::instance().isDuneCityContentActive()
        || (currentGame->getGameInitSettings().getGameType() != GameType::CustomGame
            && currentGame->getGameInitSettings().getGameType() != GameType::CustomMultiplayer)
        || currentGame->getGameInitSettings().getInitialSpiceTilePermille() <= 0
        // Civilization Wars keeps its required research: the order was never measured there.
        || currentGame->getGameInitSettings().isCivilizationWars()
        || currentGame->getGameInitSettings().getModName() == "CivWars") return;

    // Use the future strategic mix, not the tech-filtered current mix, which
    // would deadlock a tech-1 opening before its first combat supplier.
    const auto& ratios = botConfig().getRatios(getHouse()->getHouseID());
    UnitMixPolicy::GroupWeights groups{};
    groups[0] = static_cast<int>(ratios.tank * 10000);
    groups[1] = static_cast<int>(ratios.siegeTank * 10000);
    groups[2] = static_cast<int>(ratios.launcher * 10000);
    groups[3] = static_cast<int>(ratios.special * 10000);
    groups[4] = static_cast<int>(ratios.ornithopter * 10000);
    if (pass.strategyMixProfile) groups = pass.strategyMixProfile->prior;
    const auto intended = UnitMixPolicy::expandGroups(groups, fieldableMixSlots(9));
    const auto& data = currentGame->objectData.data;
    const int house = getHouse()->getHouseID();
    std::array<int, Num_ItemID> memo;
    memo.fill(-1);
    auto technology = [&](auto&& self, Uint32 item) -> int {
        if (item >= Num_ItemID) return 10;
        int& result = memo[item];
        if (result >= 0) return result;
        result = 10; // Cyclic/disabled prerequisites are not a research plan.
        const auto& spec = data[item][house];
        if (!spec.enabled) return result;
        int required = spec.techLevel;
        if (isUnit(item) && spec.builder != ItemID_Invalid)
            required = std::max(required, self(self, Uint32(spec.builder)));
        for (Uint32 dependency = Structure_FirstID; dependency < spec.prerequisiteStructuresSet.size(); ++dependency)
            if (isStructure(dependency) && spec.prerequisiteStructuresSet[dependency])
                required = std::max(required, self(self, dependency));
        return result = required;
    };
    int ceiling = getHouse()->getTechLevel();
    auto need = [&](Uint32 item) {
        const int required = technology(technology, item);
        if (required <= 9) ceiling = std::max(ceiling, required);
    };
    for (size_t slot = 0; slot < intended.size(); ++slot)
        if (intended[slot] > 0) need(kMixItems[slot]);
    if (pass.spiceEconomyViable) { need(Unit_Harvester); need(Unit_Carryall); }
    if (pass.rockExpansionNeeded || pass.colonisationNeeded) need(Unit_MCV);
    if (pass.hostileAircraft > 0 || pass.defendingEconomy) need(Structure_RocketTurret);
    pass.cityResearchAllowed = QuantBotBuildPolicy::cityResearchOrderAdmits(level,
        getHouse()->getTechLevel(), ceiling,
        pass.forecastNetIncome / QuantBotSpendingPolicy::horizonMinutes, pass.activeProductionBurn);
}

bool QuantBot::researchLabPolicyActive() const {
    return currentGame && currentGame->getGameInitSettings().getGameOptions().researchLab
        && botConfig().force.researchLab && !switchedOff() && !fighterOnly();
}

bool QuantBot::researchRequired(const BuildPass& pass) const {
    return pass.cityResearchAllowed && researchLabPolicyActive() && (currentGame->getGameInitSettings().isCivilizationWars()
        || (botConfig().force.researchLabProgression && getHouse()->getTechLevel() < 9
            && (openingFleetIncomplete(pass) || pass.dangerousCrimeProperties > 0))
        || (botConfig().force.cityContinuousResearch && pass.citySimEnabled
            && gameMode == GameMode::Custom && !campaignPolicyApplies()
            && lastCalculatedSpice > 0 && getHouse()->getTechLevel() < 9));
}

bool QuantBot::economyReadyForCapital(BuildPass& pass) {
    const bool spiceIncome = pass.spiceEconomyViable && getAIRefineryBays() > 0
        && pass.actualHarvesters >= std::max(1,std::min(2,pass.mapSpiceHarvesterTarget));
    const bool taxIncome = pass.citySimEnabled && getHouse()->getCapacity() > 0
        && pass.forecastTax > pass.forecastUpkeep;
    return (spiceIncome || taxIncome) && pass.forecastNetIncome > 0
        && !cityBootstrapNeeded(pass) && (researchRequired(pass) || !openingFleetIncomplete(pass))
        && getHouse()->hasPower()
        && (!(pass.citySimEnabled || pass.powerRules)
            || getHouse()->getProducedPower() >= getHouse()->getPowerRequirement())
        && !pass.defendingEconomy && !pass.airEngaged
        && (civWarsRules() || researchRequired(pass) || pass.dangerousCrimeProperties <= 0);
}

// CityResearchOrder (pass.cityResearchAllowed) defers new studies only: it is
// checked where research is required, planned or started, never here, so a
// deferred study leaves the Research Lab building ordinary optional capital.
bool QuantBot::techInvestmentReady(BuildPass& pass) {
    return researchLabPolicyActive() && economyReadyForCapital(pass);
}

bool QuantBot::canFundWorkingCapital(BuildPass& pass, int cost, Uint32 supplier) {
    QuantBotBuildPolicy::TechInvestment investment;
    investment.enabled = investment.economyReady = investment.powerReady = true;
    investment.cash = std::min(pass.money,getHouse()->getCredits());
    investment.cost = cost;
    investment.projectedCash = pass.cashFlow.projectedCash;
    investment.orderedCost = pass.capitalOrderedCost;
    // Working capital and the next protected purchase are separate obligations.
    // Queue remainders are already deducted from pass.money; never charge them twice.
    investment.reserve = int64_t(std::max({1000,pass.cashBuffer,pass.economyReserve,pass.cityWorkingReserve}))
        + std::max({pass.strategicReserveCost,pass.civicReserveCost,cityBootstrapReserve(pass),
            harvesterInvestmentReserve(pass),capitalReserve(pass,supplier)});
    if (civWarsRules()) {
        // Existing queues are funded above. Unordered repeatable military/city
        // capacity is not a debt that may keep required technology locked.
        investment.projectedCash = investment.cash;
        investment.orderedCost = 0;
        investment.reserve = std::max(300,pass.cityWorkingReserve);
    }
    return QuantBotBuildPolicy::canFundTechInvestment(investment);
}

bool QuantBot::canFundTechInvestment(BuildPass& pass, int cost) {
    if (pass.techSavings)
        return pass.techSavingActive && pass.techResearch
            && getHouse()->getCredits() >= cost
            && spendingAdmits(pass, Structure_ResearchLab, cost, true);
    if (!techInvestmentReady(pass)) return false;
    if (!researchRequired(pass)) return canFundWorkingCapital(pass,cost,NONE_ID);
    // Technology is a prerequisite, not an optional expansion that must wait for
    // the unlocked worker fleet or repeated city/factory allocations to finish.
    // Queued purchases are already deducted from pass.money; keep real operating
    // cash, never borrow expected receipts or spend another queue's commitment.
    QuantBotBuildPolicy::TechInvestment investment;
    investment.enabled = investment.economyReady = investment.powerReady = true;
    investment.cash = std::min(pass.money,getHouse()->getCredits());
    investment.projectedCash = investment.cash;
    investment.cost = cost;
    investment.reserve = std::max(300,pass.cityWorkingReserve);
    return QuantBotBuildPolicy::canFundTechInvestment(investment);
}

bool QuantBot::cityOpeningCapitalFunded(BuildPass& pass, int cost, Uint32 supplier) {
    // Protect a tax-dependent opening, not productive spice-backed expansion.
    // Financial runway must not inherit research's fleet/crime prerequisites.
    if (pass.citySpiceCapacityRunway) return canFundWorkingCapital(pass,cost,supplier);
    return !pass.citySimEnabled || !botConfig().force.cityOpeningCapital
        || pass.spiceEconomyViable || canFundWorkingCapital(pass,cost,supplier);
}

bool QuantBot::canFundProductionUpgrade(BuildPass& pass, const BuilderBase* builder) {
    if (pass.techSavings && pass.techSavingActive && pass.techUpgrade
        && pass.techCandidate.builder == builder->getObjectID())
        return getHouse()->getCredits() >= builder->getUpgradeCost();
    // Yard upgrades unlock concrete, power, civics and reactive turrets. They
    // keep their existing prerequisite budgets; only producer tech is optional.
    if (!pass.citySimEnabled || (!botConfig().force.cityOpeningCapital && !pass.citySpiceCapacityRunway)
        || builder->getItemID()==Structure_ConstructionYard
        || pass.defendingEconomy || pass.airEngaged) return true;
    const bool constructionRecovery=builder->getItemID()==Structure_HeavyFactory
        && !campaignAvailableToBuild(builder,Unit_MCV)
        && (pass.itemCount[Structure_ConstructionYard]==0 || pass.rockExpansionNeeded || pass.colonisationNeeded);
    if (pass.citySpiceCapacityRunway) {
        const auto& data = currentGame->objectData.data;
        const int houseID = getHouse()->getHouseID();
        if (pass.existingMilitaryCapacity == 0
            || (builder->getItemID() == Structure_HeavyFactory && openingFleetIncomplete(pass)
                && builder->getCurrentUpgradeLevel() < data[Unit_Harvester][houseID].upgradeLevel)
            || (builder->getItemID() == Structure_HighTechFactory && needsFirstTransport(pass)
                && builder->getCurrentUpgradeLevel() < data[Unit_Carryall][houseID].upgradeLevel)) return true;
    }
    return constructionRecovery
        || cityOpeningCapitalFunded(pass,builder->getUpgradeCost(),builder->getObjectID());
}

void QuantBot::planTechResearch(BuildPass& pass) {
    const bool required = researchRequired(pass);
    if (!pass.cityResearchAllowed || (!required && !civWarsRules()) || getHouse()->isTechResearchActive() || getHouse()->getTechLevel() >= 9
        || !techInvestmentReady(pass)) return;
    if (pass.techIncomeLevel > 0 || pass.cityTechRefineries > 0) {
        const bool ownsLab = getHouse()->getNumItems(Structure_ResearchLab) > 0;
        const int cost = ownsLab ? getHouse()->getTechResearchCost()
            : buildingCapitalCost(pass, Structure_ResearchLab);
        // A rate- or refinery-held project must not keep an unactionable research
        // reserve away from current-tier units after startTechResearch refuses it.
        if (!incomeTechAdmits(pass, Structure_ResearchLab, cost, ownsLab)) return;
    }
    if (getHouse()->getNumItems(Structure_ResearchLab) == 0) {
        if (required)
            pass.civWarsResearchReserve = QuantBotBuildPolicy::civWarsResearchSaving(
                buildingCapitalCost(pass,Structure_ResearchLab),pass.cityWorkingReserve,pass.forecastFunding);
        return;
    }
    // The optional duel strategy retains its army pacing. Basic progression must
    // also work for teams, Easy/Medium and Builder/Defend houses that never launch.
    if (!required) {
        const int tech = getHouse()->getTechLevel();
        if ((tech >= 4 && getHouse()->getNumItems(Structure_HeavyFactory) == 0)
            || (tech >= 5 && getHouse()->getNumItems(Structure_RepairYard) == 0)) return;
        const bool firstWave = customWave.launched != 0;
        const int groundValue = tech >= 4 && firstWave ? ownGroundArmyValue() : 0;
        if (!QuantBotBuildPolicy::civWarsResearchPacing(tech,firstWave,groundValue,minWaveValue())) return;
    }
    startTechResearch(pass);
    if (!getHouse()->isTechResearchActive())
        pass.civWarsResearchReserve = QuantBotBuildPolicy::civWarsResearchSaving(
            getHouse()->getTechResearchCost(),pass.cityWorkingReserve,pass.forecastFunding);
}

void QuantBot::startTechResearch(BuildPass& pass) {
    if (!pass.cityResearchAllowed || !researchLabPolicyActive() || getHouse()->isTechResearchActive() || getHouse()->getTechLevel() >= 9) return;
    const int cost = getHouse()->getTechResearchCost();
    if (!spendingAdmits(pass, Structure_ResearchLab, cost, true)) return;
    if (!canFundTechInvestment(pass,cost)) return;
    const StructureBase* lab = nullptr;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getItemID() == Structure_ResearchLab
            && structure->isActive() && structure->getHealth() > 0
            && (!lab || structure->getObjectID() < lab->getObjectID())) lab = structure;
    if (!lab) return;
    const int from = getHouse()->getTechLevel();
    if (!const_cast<House*>(getHouse())->startTechResearch(lab->getObjectID())) return;
    // Preserve the installed planner's admission policy, but book the engine's
    // actual start payment: installment studies collect their quote over time.
    const bool installments = getHouse()->isTechResearchInstallment();
    const int upfrontCost = installments ? 0 : cost;
    pass.money -= upfrontCost;
    pass.capitalOrderedCost += upfrontCost;
    if (pass.techSavings && pass.techResearch) {
        pass.techSavingActive = false;
        pass.civWarsResearchReserve = 0;
    }
    if (AITelemetry::log().enabled()) {
        pass.capitalOrders.set(std::to_string(lab->getObjectID())+":research",
            AITelemetry::Record().set("item",Structure_ResearchLab).set("price",cost)
                .set("upfront_cost",upfrontCost).set("installments",installments).set("rule","tech_research"));
        traceDecision("research_started",AITelemetry::Record().set("capital_plan",pass.capitalDecision)
            .set("lab",lab->getObjectID())
            .set("from",from).set("to",from+1).set("cost",cost)
            .set("upfront_cost",upfrontCost).set("installments",installments)
            .set("cycles",getHouse()->techResearchCycles(from+1))
            .set("spendable_after",pass.money).set("state",decisionState(pass)));
    }
}

void QuantBot::resetBuildPass() {
    refreshTacticalDanger();
    repairPlacementReady = false;
    planningBuilder = NONE_ID;
    // One lot per construction pass across every yard, enforced on the side
    // that actually demolishes rather than inferred from how many orders were
    // accepted: several yards can each be carrying a clearance reservation
    // from earlier passes and would otherwise all commit in the same call.
    batteryClearancesThisPass = 0;
    // Anchored searches belong to one planning builder; a new pass starts with
    // none, so none of last pass's answers are reused.
    turretSearchBuilder = NONE_ID;
    recentStructureLosses.erase(std::remove_if(recentStructureLosses.begin(), recentStructureLosses.end(),
        [&](const auto& loss) { return getGameCycleCount() - loss.cycle >= MILLI2CYCLES(900000); }), recentStructureLosses.end());
    cityProductionPlots.clear();planningCityProductionPlots=false;
    clearPlacementCache();
    for (auto it = roadRedirectRetryCycle.begin(); it != roadRedirectRetryCycle.end();) {
        if (getGameCycleCount() >= it->second) it = roadRedirectRetryCycle.erase(it);
        else ++it;
    }
    cityServiceSearch.reset();
    cityTurretSearch.reset();
    for (auto it = reservedStructures.begin(); it != reservedStructures.end();) {
        const auto* builder = dynamic_cast<const BuilderBase*>(currentGame->getObjectManager().getObject(it->first));
        if (!builder || builder->getOwner() != getHouse() || builder->getProductionQueueSize() == 0)
            it = reservedStructures.erase(it);
        else ++it;
    }
}

void QuantBot::planBuildInventory(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    const bool simple = simpleTechPolicyActive();

    for (int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
        pass.itemCount[i] = getHouse()->getNumItems(i);
    }

    pass.activeHeavyFactoryCount = 0;
    pass.carryallBuildAvailable = false;
    pass.mcvBuildAvailable = false;
    pass.windBuildAvailable = false;
    pass.nuclearBuildAvailable = false;
    pass.activeLightFactoryCount = 0;
    pass.activeHighTechFactoryCount = 0;
    pass.ornithopterFactoryCount = 0;
    pass.ornithopterCapableFactoryCount = 0;
    pass.activeRepairYardCount = 0;
    pass.queuedProductionCost = 0;
    pass.queuedTechnologyCost = 0;
    pass.committedGroundUnits = 0;
    pass.committedGroundValue = 0;
    pass.queuedMilitaryValue = 0;
    pass.mcvUpgradesInProgress = 0;

    // Let's try just running this once...
    if (squadRallyLocation.isInvalid()) {
        squadRallyLocation = findSquadRallyLocation();
        squadRetreatLocation = findSquadRetreatLocation();
        if (gameMode != GameMode::Campaign) {
            retreatAllUnits();
        }
    }

    // Next add in the objects we are building
    for (const StructureBase* pStructure : getStructureList()) {
        if (pStructure->getOwner() == getHouse()) {
            if (pStructure->isABuilder()) {
                const BuilderBase* pBuilder = static_cast<const BuilderBase*>(pStructure);
                if (pBuilder->getItemID() == Structure_ConstructionYard && pBuilder->getHealth() > 0) {
                    if (campaignAvailableToBuild(pBuilder,Structure_WindTrap)) pass.windBuildAvailable = true;
                    if (campaignAvailableToBuild(pBuilder,Structure_NuclearPlant)) pass.nuclearBuildAvailable = true;
                }
                if (pBuilder->getItemID() == Structure_HighTechFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_Carryall)) pass.carryallBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HighTechFactory && pBuilder->getHealth() > 0
                    && !pBuilder->isUpgrading() && !pBuilder->isOnHold()
                    && (campaignAvailableToBuild(pBuilder,Unit_Ornithopter)
                        || (ModManager::instance().isTornieFamilyContentActive()
                            && campaignAvailableToBuild(pBuilder,Unit_EliteOrnithopter)))) ++pass.ornithopterCapableFactoryCount;
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_Harvester)) pass.harvesterFactories.push_back(pBuilder);
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_MCV)) pass.mcvBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->isUpgrading()
                    && !campaignAvailableToBuild(pBuilder,Unit_MCV)) ++pass.mcvUpgradesInProgress;
                if (pBuilder->isUpgrading()) {
                    const int unpaid = std::max(0,pBuilder->getUpgradeCost() - pBuilder->getUpgradeProgress().lround());
                    pass.queuedProductionCost += unpaid;
                    if (simple) pass.queuedTechnologyCost += unpaid;
                }
                if (pBuilder->getProductionQueueSize() > 0) {
                    int builderQueuedCost = 0;
                    bool technology = false;
                    for (const auto& queued : pBuilder->getBuildList()) {
                        pass.itemCount[queued.itemID] += queued.num;
                        builderQueuedCost += queued.num * queued.price;
                        if (simple && queued.num > 0) {
                            technology |= isStructure(queued.itemID) && QuantBotBuildPolicy::spendingBucket(queued.itemID)
                                == QuantBotSpendingPolicy::Bucket::Production;
                            const size_t slot = compositionSlot(queued.itemID);
                            if (isInfantryUnit(queued.itemID) || (slot < UnitMixPolicy::kSlots
                                && slot != UnitMixPolicy::kOrnithopterSlot && !isSandwormUnit(queued.itemID))) {
                                const int people = queued.itemID == Unit_Infantry || queued.itemID == Unit_Troopers ? 3
                                    : queued.itemID == Unit_Infantry5 || queued.itemID == Unit_Troopers5 ? 5 : 1;
                                pass.committedGroundUnits += queued.num * people;
                                pass.committedGroundValue += queued.num * data[queued.itemID][houseID].price;
                            }
                        }
                        if (QuantBotBuildPolicy::militaryItem(queued.itemID))
                            pass.queuedMilitaryValue += queued.num * data[queued.itemID][ModManager::instance().isTornieFamilyContentActive()
                                ? pBuilder->getTechnologyHouseID() : houseID].price;
                    }
                    // Starport cargo is paid for when ordered. It still counts
                    // toward the fleet, but must not reserve the same cash again.
                    if (pBuilder->getItemID()!=Structure_StarPort) {
                        const int unpaid = std::max(0, builderQueuedCost - pBuilder->getProductionProgress().lround());
                        pass.queuedProductionCost += unpaid;
                        if (technology) pass.queuedTechnologyCost += unpaid;
                    }
                    if (pBuilder->getItemID() == Structure_HeavyFactory) {
                        pass.activeHeavyFactoryCount++;
                    }
                    else if (pBuilder->getItemID() == Structure_LightFactory) {
                        ++pass.activeLightFactoryCount;
                    }
                    else if (pBuilder->getItemID() == Structure_HighTechFactory) {
                        pass.activeHighTechFactoryCount++;
                        if (QuantBotInternal::ornithopterItem(pBuilder->getCurrentProducedItem())
                            && !pBuilder->isOnHold() && !pBuilder->isUpgrading()) ++pass.ornithopterFactoryCount;
                    }
                }
                if (repairCapacityDemandActive() && pBuilder->getProductionQueueSize() > 0) {
                    const auto reserved = reservedStructures.find(pBuilder->getObjectID());
                    if (reserved != reservedStructures.end() && reserved->second.item == Structure_RepairYard
                        && std::none_of(pBuilder->getBuildList().begin(), pBuilder->getBuildList().end(),
                            [](const auto& offer) { return offer.itemID == Structure_RepairYard && offer.num > 0; }))
                        ++pass.itemCount[Structure_RepairYard];
                }
            }
            else if (pStructure->getItemID() == Structure_RepairYard) {
                const RepairYard* pRepairYard = static_cast<const RepairYard*>(pStructure);
                if (!pRepairYard->isFree()) {
                    pass.activeRepairYardCount++;
                }

            }

            // Unit deployment position - disabled, just deploy units normally
            // Production buildings will deploy units at their default position
        }


    }
    if (ModManager::instance().isTornieFamilyContentActive()) {
        // The existing economy plans in workers and unload bays, not raw building
        // IDs. Preserve the variant entries as well for reconstruction and queues.
        pass.itemCount[Unit_Harvester] += pass.itemCount[Unit_CapacityHarvester] + pass.itemCount[Unit_FastHarvester];
        pass.itemCount[Structure_Refinery] += 2 * pass.itemCount[Structure_Doublefinery] + pass.itemCount[Structure_Worfinery];
    }
}

int QuantBot::rawPlannedItemCount(const BuildPass& pass, Uint32 item) const {
    int count = pass.itemCount[item];
    if ((item == Structure_Refinery || item == Unit_Harvester) && ModManager::instance().isTornieFamilyContentActive()) {
        if (item == Structure_Refinery)
            count -= 2 * pass.itemCount[Structure_Doublefinery] + pass.itemCount[Structure_Worfinery];
        else if (item == Unit_Harvester)
            count -= pass.itemCount[Unit_CapacityHarvester] + pass.itemCount[Unit_FastHarvester];
    }
    return count;
}

void QuantBot::planBuildBudget(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    pass.money = getHouse()->getCredits();
    const bool simple = simpleTechPolicyActive();
    pass.techSavings = simple;
    pass.militaryValue += pass.queuedMilitaryValue;
    // Effective military planning budget for this pass, computed once now that the
    // queue-inclusive army value and the cash are known. It equals the configured
    // militaryValueLimit unless this is Brutal with an explicit unit-count override,
    // in which case it is rolling headroom above the committed value so production
    // only stops at the engine's unit-count limits, reserves, costs and prerequisites.
    // The serialized militaryValueLimit is never modified.
    pass.militaryBudgetOverridden = overridesMilitaryValueCap();
    int largestMilitaryUnitValue = 0;
    if (pass.militaryBudgetOverridden)
        for (Uint32 item = Unit_FirstID; item <= (ModManager::instance().isTornieFamilyContentActive() ? ItemID_LastID : Unit_LastID); ++item)
            if (QuantBotBuildPolicy::militaryItem(item) && data[item][houseID].enabled)
                largestMilitaryUnitValue = std::max(largestMilitaryUnitValue, data[item][houseID].price);
    pass.militaryBudget = planningMilitaryBudget(pass.militaryValue, pass.money, largestMilitaryUnitValue);
    // M (SpendAtCap, test-only): the budget above the configured limit is for the heavy factory's
    // siege tanks, launchers and specials; light vehicles, infantry, aircraft and tanks stop at it.
    // (Custom Medium's surplus, planningMilitaryBudget(), buys anything.)
    pass.surplusArmy = !pass.militaryBudgetOverridden && difficulty != Difficulty::Medium
        && forceVariant(kForceSpendAtCap) && pass.militaryBudget > militaryValueLimit;
    pass.lightBudget = pass.surplusArmy ? militaryValueLimit : pass.militaryBudget;
    if (pass.surplusArmy) forceStats.surplusPeak = std::max(forceStats.surplusPeak, pass.militaryValue - militaryValueLimit);
    pass.citySimEnabled = currentGame && currentGame->isCitySimEnabled();
    planCityBoom(pass);
    // One reading of the remaining field, shared by the space decision here and
    // by the cash decisions further down. A tiny field that supports no worker
    // must not count as viable for one and not the other: "any spice at all"
    // would reserve every corner tile for production the house can never fund.
    // Under city sim this is exactly mapSpiceHarvesterTarget below, because
    // vanillaEconomy is false there; the city divisor is also the stricter one.
    pass.spiceCompetitors = 0;
    for (int h = 0; h < NUM_HOUSES; ++h)  // the Natives harvest nothing
        if (getHouse(h) && !getHouse(h)->isNatives() && getHouse(h)->getNumStructures() > 0) ++pass.spiceCompetitors;
    planFleetResourceBudget(pass);
    pass.citySpiceWorkerTarget = QuantBotBuildPolicy::desiredSpiceHarvesters(
        pass.fleetRatios ? pass.fleetSpiceShare : lastCalculatedSpice,
        pass.fleetRatios ? 1 : pass.spiceCompetitors, harvesterLimit);
    if (pass.cityBoomWorkers > 0) pass.citySpiceWorkerTarget = pass.cityBoomWorkers;
    if (civWarsRules())
        pass.citySpiceWorkerTarget = QuantBotBuildPolicy::civWarsWorkerTarget(
            pass.citySpiceWorkerTarget, getHouse()->getTechLevel(), customWave.launched != 0,
            static_cast<int>(difficulty));
    // Space is reserved on the same terms as cash. A house whose field supports
    // no worker and which has no tax base yet cannot pay for either footprint,
    // and on a cramped slot the two reserved areas plus their access margin are
    // the whole buildable surface: the lots that would earn that money get
    // "no_site" instead. On the captured 192x192 start that cost two houses
    // every zone site for 43,000 cycles. Reserve production space once the city
    // funds it.
    const bool cityFundsProduction = !pass.citySimEnabled || pass.citySpiceWorkerTarget > 0
        || (getHouse()->getCapacity()>0 && currentGame->getCitySimulation()
            && currentGame->getCitySimulation()->getHouseState(houseID).taxBaseEighths > 0);
    if (pass.citySimEnabled && cityFundsProduction && pass.itemCount[Structure_ConstructionYard]>0) {
        planningCityProductionPlots=true;
        for (Uint32 item:{Structure_HeavyFactory,Structure_RepairYard}) {
            if (!data[item][houseID].enabled || data[item][houseID].techLevel>getHouse()->getTechLevel()) continue;
            if (botConfig().force.cityProductionPrerequisites) {
                // A standing or queued supplier has already claimed its space.
                // Reserving its next copy can exclude every demanded city lot.
                if (pass.itemCount[item]>0) continue;
                // Reserve the next missing link, not two future buildings that
                // cannot exist without it. On the Sardaukar save, the heavy and
                // repair plots excluded every site for their Light Factory.
                for (int depth=0; depth<Structure_LastID; ++depth) {
                    Uint32 missing=NONE_ID;
                    for (Uint32 prerequisite=Structure_FirstID; prerequisite<=Structure_LastID; ++prerequisite)
                        if (data[item][houseID].prerequisiteStructuresSet[prerequisite]
                            && pass.itemCount[prerequisite]==0) { missing=prerequisite; break; }
                    if (missing==NONE_ID) break;
                    item=missing;
                }
                if (!data[item][houseID].enabled || data[item][houseID].techLevel>getHouse()->getTechLevel()
                    || std::any_of(cityProductionPlots.begin(),cityProductionPlots.end(),
                        [item](const auto& plot) { return plot.item==item; })) continue;
            }
            clearPlacementCache(false);
            const Coord site=findPlaceLocation(item);
            if (site.isValid()) cityProductionPlots.push_back({item,site});
        }
        planningCityProductionPlots=false;clearPlacementCache(false);
        if (AITelemetry::log().enabled()) {
            AITelemetry::Record plots;
            for (const auto& plot:cityProductionPlots) plots.set(std::to_string(plot.item),
                AITelemetry::Record().set("x",plot.location.x).set("y",plot.location.y));
            traceDecision("city_production_plots",AITelemetry::Record().set("plots",plots));
        }
    }
    // INVALID is an absent/disabled market entry; zero is merely sold out and
    // will restock. Check the catalogue, not whether a factory already exists.
    pass.starportMarketAvailable = [&] {
        for (int item=Unit_FirstID;item<=(ModManager::instance().isTornieFamilyContentActive() ? ItemID_LastID : Unit_LastID);++item)
            if (isUnit(item) && data[item][houseID].enabled && getHouse()->getChoam().getNumAvailable(item)>=0) return true;
        return false;
    }();
    // A queued refinery supplies a free worker when placed. Reserve that
    // worker alongside factory production and paid imports in every game mode.
    pass.engineHarvesterLimit = getHouse()->getMaxHarvesters();
    pass.actualHarvesters = getAIHarvesterCount() + getHouse()->getNumItems(Unit_RebelHarvester);
    int pendingRefineries = std::max(0,pass.itemCount[Structure_Refinery] - getAIRefineryBays());
    if (ModManager::instance().isTornieFamilyContentActive())
        pendingRefineries -= std::max(0,pass.itemCount[Structure_Worfinery] - getHouse()->getNumItems(Structure_Worfinery));
    pass.itemCount[Unit_Harvester] += pass.engineHarvesterLimit > 0
        ? std::min(pendingRefineries, std::max(0,pass.engineHarvesterLimit
            - pass.itemCount[Unit_Harvester] - pass.itemCount[Unit_RebelHarvester]))
        : pendingRefineries;
    pass.combatVehicles = 0; pass.busyTransports = 0; pass.busyRepairBays = 0;
    std::set<Uint32> repairQueue;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || unit->getHealth()<=0) continue;
        if (unit->getItemID()==Unit_Carryall && static_cast<const Carryall*>(unit)->isBooked()) ++pass.busyTransports;
        if (!unit->isAGroundUnit() || unit->isInfantry() || unit->getItemID()==Unit_Saboteur) continue;
        const auto* ground=static_cast<const GroundUnit*>(unit);
        if (unit->canAttack() && unit->getItemID()!=Unit_Sandworm) ++pass.combatVehicles;
        if (unit->getAttackMode()==CARRYALLREQUESTED && !ground->hasBookedCarrier()) pass.pickupQueue.insert(unit->getObjectID());
        const auto* target=unit->getTarget();
        // A borrowed unit is never admitted to our bays, so it is not repair demand and must not
        // reserve a bay or justify building another one.
        if (ground->isEligibleForRepair()
            && (unit->isBadlyDamaged() || (target && target->getItemID()==Structure_RepairYard
            && target->getOwner()==getHouse() && unit->getHealth()<unit->getMaxHealth()))) repairQueue.insert(unit->getObjectID());
    }
    const bool repairDemandActive = repairCapacityDemandActive();
    // These new occupancy counters feed structured repair/state telemetry, not
    // the independent CSV sampler or the legacy support/transport targets.
    const bool observeRepairOccupancy = repairDemandActive || AITelemetry::log().enabled();
    for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse()) continue;
        if (structure->getItemID() == Structure_Doublefinery && ModManager::instance().isTornieFamilyContentActive()) {
            const auto* refinery = static_cast<const Refinery*>(structure);
            for (int bay = 0; bay < refinery->getNumBays(); ++bay) {
                const auto* unit = dynamic_cast<const Harvester*>(refinery->getContainedHarvester(bay));
                if (unit && unit->getAmountOfSpice() <= 0 && unit->getGuardPoint().isValid()
                    && !unit->hasBookedCarrier()
                    && blockDistance(structure->getLocation(),unit->getGuardPoint()) >= MIN_CARRYALL_LIFT_DISTANCE)
                    pass.pickupQueue.insert(unit->getObjectID());
            }
            continue;
        }
        const GroundUnit* returnUnit=nullptr;
        if (structure->getItemID()==Structure_RepairYard) {
            const auto* yard = static_cast<const RepairYard*>(structure);
            const auto* contained = observeRepairOccupancy ? yard->getContainedRepairUnit() : nullptr;
            if (contained && structure->getHealth() > 0) {
                if (contained->getHealth() > 0 && contained->getHealth() < contained->getMaxHealth())
                    ++pass.observedBusyRepairBays;
                else if (contained->getHealth() >= contained->getMaxHealth()) ++pass.completedRepairReturns;
            }
            const auto* unit = repairDemandActive ? contained : yard->getRepairUnit();
            if (unit && unit->getHealth()<unit->getMaxHealth()) {
                ++pass.busyRepairBays;repairQueue.insert(unit->getObjectID());
            } else returnUnit=dynamic_cast<const GroundUnit*>(unit);
        } else if (structure->getItemID()==Structure_Refinery) {
            const auto* unit=dynamic_cast<const Harvester*>(static_cast<const Refinery*>(structure)->getContainedHarvester());
            if (unit && unit->getAmountOfSpice()<=0) returnUnit=unit;
        }
        // A finished job waiting for its return flight is transport pressure,
        // not a reason to buy another repair/unloading bay.
        if (returnUnit && returnUnit->getGuardPoint().isValid() && !returnUnit->hasBookedCarrier()
            && blockDistance(structure->getLocation(),returnUnit->getGuardPoint())>=MIN_CARRYALL_LIFT_DISTANCE)
            pass.pickupQueue.insert(returnUnit->getObjectID());
    }
    pass.waitingRepairVehicles = std::max(0,int(repairQueue.size())-pass.busyRepairBays);
    pass.repairBaseline = QuantBotBuildPolicy::baselineRepairYards(pass.combatVehicles,pass.actualHarvesters);
    pass.repairTarget = QuantBotBuildPolicy::supportQueueTarget(pass.repairBaseline,
        getHouse()->getNumItems(Structure_RepairYard),pass.itemCount[Structure_RepairYard],pass.busyRepairBays,pass.waitingRepairVehicles);
    planRepairCapacity(pass, repairQueue);
    pass.powerRules = getHouse()->isPowerRequired();
    pass.turretPowerRequired = getGameInitSettings().getGameOptions().rocketTurretsNeedPower;
    // Power consumption is independent of the funded spice-production policy.
    // Keep the engine's power rules while reusing the established non-city
    // economy consistently, including its worker, factory and MCV allocation.
    pass.vanillaEconomy = !pass.citySimEnabled && (!pass.powerRules
        || (difficulty==Difficulty::Brutal && gameMode==GameMode::Custom
            && botConfig().force.fundedClassicProduction && !campaignPolicyApplies()));
    pass.campaignEconomyPush = pass.vanillaEconomy && campaignPolicyApplies()
        && !builderOnly() && !isCampaignEnemy() && difficulty>=Difficulty::Hard;
    // Buildings pay gradually. Previously the same unspent cash funded more
    // orders every pass even though it was already committed to existing queues.
    const int reservedCommitments = pass.queuedProductionCost - (simple ? pass.queuedTechnologyCost : 0);
    pass.clearanceCommitmentShortfall = std::max(0, reservedCommitments - pass.money);
    pass.money = std::max(0, pass.money - reservedCommitments);
    // A quota mission is won by the credits its house has banked, and on some maps by nothing
    // else. From half the quota a campaign helper on that house stops placing orders so the
    // remaining income reaches the quota instead of becoming more army.
    if (botConfig().force.campaignHelperQuotaSaving && !pass.citySimEnabled
        && campaignPolicyApplies() && isAlliedWithHuman()
        && (currentGame->winFlags & WINLOSEFLAGS_QUOTA) != 0 && getHouse()->getQuota() > 0
        && getHouse() == currentGame->getOutcomeHouse()
        && getHouse()->getCapacity() >= getHouse()->getQuota()
        && getHouse()->getStoredCredits() * 2 >= getHouse()->getQuota()) pass.money = 0;
    // Keep the experimental policy's current working-capital convention;
    // ordinary production retains its existing economic cash buffer.
    pass.economyReserve = pass.vanillaEconomy && !pass.techSavings ? std::max(2000,
        data[Structure_Refinery][houseID].price + 2 * data[Unit_Harvester][houseID].price) : 0;
}

QuantBot::CityBoomKind QuantBot::cityBoomSpiceKind(const CitySpiceShare* share) const {
    const auto kind = cityBoomKind();
    const auto& f = botConfig().force;
    if (!f.cityBoomSpiceGate || kind == CityBoomKind::Native || kind == CityBoomKind::ShortTax) return kind;
    const Uint32 now = getGameCycleCount();
    if (cityBoomSpiceGateCycle == now) return cityBoomSpiceGateCache.kind;
    cityBoomSpiceGateCycle = now;
    auto& gate = cityBoomSpiceGateCache;
    gate = {};
    gate.kind = kind;
    switch (kind) {
        case CityBoomKind::Workers: gate.kindWorkers = std::max(1, f.cityBoomWorkerGoal); gate.kindBays = 2; break;
        case CityBoomKind::Refineries:
            gate.kindBays = std::max(1, f.cityBoomRefineryGoal); gate.kindWorkers = 2 * gate.kindBays; break;
        case CityBoomKind::Greedy: gate.kindWorkers = 20; gate.kindBays = 8; break;
        case CityBoomKind::Reactive: gate.kindWorkers = 24; gate.kindBays = 8; break;
        default: return kind;
    }
    const auto& field = share ? *share : citySpiceShare();
    const auto& data = currentGame->objectData.data;
    const int house = getHouse()->getHouseID();
    const int64_t workerPrice = std::max(0, data[Unit_Harvester][house].price);
    const int64_t refineryPrice = std::max(0, data[Structure_Refinery][house].price);
    // Per worker: harvester price + (refinery price - its free harvester)
    // / workers-per-bay. Cross multiplication retains fractional bay shares.
    gate.workerCapitalNumerator = std::max<int64_t>(0, workerPrice * gate.kindWorkers
        + (refineryPrice - workerPrice) * gate.kindBays);
    const int64_t horizonYield = int64_t(kCityBoomWorkerSpicePerMinute) * std::max(1, f.cityBoomPaybackMinutes);
    gate.workerCap = static_cast<int>(std::min<int64_t>(std::numeric_limits<int>::max(), field.ownSpice / horizonYield));
    const int64_t bayCap = (int64_t(gate.workerCap) * gate.kindBays + gate.kindWorkers - 1) / gate.kindWorkers;
    gate.bayCap = static_cast<int>(std::min<int64_t>(
        kind == CityBoomKind::Workers ? 2 : std::numeric_limits<int>::max(), bayCap));
    gate.inert = gate.workerCap == 0 || gate.workerCapitalNumerator > horizonYield * gate.kindWorkers;
    if (gate.inert) gate.kind = CityBoomKind::Native;
    return gate.kind;
}

void QuantBot::planCityBoom(BuildPass& pass) {
    pass.cityBoom = cityBoomKind();
    if (pass.cityBoom == CityBoomKind::Native) return;
    const auto& f = botConfig().force;
    // One direct share reading per build pass. Other boom decisions share the
    // same cycle-derived gate; no decision-bearing state survives a cycle.
    const auto* share = f.cityBoomSpiceGate && pass.cityBoom != CityBoomKind::ShortTax ? &citySpiceShare() : nullptr;
    if (share) pass.cityBoom = cityBoomSpiceKind(share);
    const bool opening = customWave.launched == 0, spice = share ? share->ownSpice > 0 : lastCalculatedSpice > 0;
    const int workers = getAIHarvesterCount(), bays = getAIRefineryBays();
    const int engineLimit = getHouse()->getMaxHarvesters();
    const int limit = engineLimit > 0 ? std::min(harvesterLimit, engineLimit) : harvesterLimit;
    switch (pass.cityBoom) {
        case CityBoomKind::Workers:
            if (opening && spice) {
                pass.cityBoomWorkers = std::min(limit, f.cityBoomWorkerGoal);
                pass.cityBoomBays = 2;
                pass.cityBoomFunding = workers < pass.cityBoomWorkers || bays < pass.cityBoomBays;
            }
            pass.cityBoomArmy = opening && !pass.cityBoomFunding;
            break;
        case CityBoomKind::Refineries:
            if (opening && spice) {
                pass.cityBoomBays = f.cityBoomRefineryGoal;
                pass.cityBoomWorkers = std::min(limit, 2 * pass.cityBoomBays);
                pass.cityBoomFunding = workers < pass.cityBoomWorkers || bays < pass.cityBoomBays;
            }
            pass.cityBoomArmy = opening && !pass.cityBoomFunding;
            break;
        case CityBoomKind::Greedy:
            pass.cityBoomFunding = getGameCycleCount() < MILLI2CYCLES(f.cityBoomGreedyMinutes * 60000);
            if (pass.cityBoomFunding && spice) {
                pass.cityBoomWorkers = std::min(limit, 20);
                pass.cityBoomBays = 8;
            }
            pass.cityBoomArmy = opening && !pass.cityBoomFunding;
            break;
        case CityBoomKind::ShortTax:
            pass.cityBoomFunding = getGameCycleCount() < MILLI2CYCLES(f.cityBoomTaxMinutes * 60000);
            if (pass.cityBoomFunding && spice) {
                pass.cityBoomWorkers = std::min(limit, 4);
                pass.cityBoomBays = 2;
            }
            break;
        case CityBoomKind::Reactive:
            pass.cityBoomFunding = cityBoomSurvey().funding;
            pass.cityBoomArmy = !pass.cityBoomFunding;
            if (spice) {
                pass.cityBoomWorkers = std::min(limit, pass.cityBoomFunding ? 24 : 4);
                pass.cityBoomBays = pass.cityBoomFunding ? 8 : 2;
            }
            break;
        case CityBoomKind::Native: break;
    }
    if (share) {
        const auto& gate = cityBoomSpiceGateCache;
        pass.cityBoomSpiceGate = !gate.inert;
        pass.cityBoomWorkerCap = gate.workerCap;
        pass.cityBoomBayCap = gate.bayCap;
        const int64_t horizonYield = int64_t(kCityBoomWorkerSpicePerMinute) * std::max(1, f.cityBoomPaybackMinutes);
        const auto* sim = currentGame->getCitySimulation();
        const int house = getHouse()->getHouseID();
        const auto& state = sim->getHouseState(house);
        const int taxIncome = DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(
            state.taxBaseEighths, sim->getCityTax(house), state.avgLandValue));
        int64_t taxCapital = 0;
        // Standing city/support replacement capital, not historical spending
        // or the later chooseCityEconomy marginal-zone forecast. Count real
        // owned zones, civics, power and police cover; tile-only auto-roads
        // have no recorded purchase capital and are deliberately not imputed.
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || structure->getHealth() <= 0 || !structure->isActive()) continue;
            const int item = structure->getItemID();
            if (DuneCity::isCityOnlyStructure(item) || item == Structure_Palace || item == Structure_WindTrap
                || DuneCity::getPoliceCoverage(item) > 0)
                taxCapital += std::max(0, currentGame->objectData.data[item][structure->getProductionHouseID()].price);
        }
        const int64_t workerYield = int64_t(kCityBoomWorkerSpicePerMinute) * gate.kindWorkers;
        const bool taxFaster = taxIncome > 0 && taxCapital > 0
            && taxCapital * workerYield < gate.workerCapitalNumerator * taxIncome;
        if (!gate.inert) {
            // Keep caps binding after a timed/opening boom ends as well; zero
            // targets would reactivate the native unlimited fallback lanes.
            pass.cityBoomWorkers = std::min({limit, gate.workerCap,
                pass.cityBoomWorkers > 0 ? pass.cityBoomWorkers : gate.kindWorkers});
            pass.cityBoomBays = std::min(gate.bayCap,
                pass.cityBoomBays > 0 ? pass.cityBoomBays : gate.kindBays);
            const bool saturated = workers >= gate.workerCap && bays >= gate.bayCap;
            const bool exhausted = bays > 0
                && share->ownSpice * gate.kindBays < horizonYield * gate.kindWorkers * bays;
            // A further bay also promises a worker. Do not strand funding
            // waiting for a bay that the worker cap correctly refuses.
            const bool noWorkerSlot = workers >= gate.workerCap && bays < pass.cityBoomBays;
            if (saturated || exhausted || noWorkerSlot) {
                pass.cityBoomFunding = false;
                pass.cityBoomArmy = pass.cityBoom == CityBoomKind::Reactive || opening;
            }
            if (taxFaster) {
                pass.cityBoomFunding = false;
                pass.cityBoomArmy = false; // Yield to actual city investment, not an army-only allocation.
            }
        }
        if (AITelemetry::log().enabled()) {
            const std::array<int64_t, 6> decision{{gate.workerCap, gate.bayCap, pass.cityBoomFunding,
                pass.cityBoomArmy, gate.inert, taxFaster}};
            if (!cityBoomSpiceGateTraceValid || decision != lastCityBoomSpiceGateTrace) {
                const int64_t spicePayback = (gate.workerCapitalNumerator + workerYield - 1) / workerYield;
                const int64_t taxPayback = taxIncome > 0 && taxCapital > 0 ? (taxCapital + taxIncome - 1) / taxIncome : -1;
                traceDecision("city_boom_spice_gate", AITelemetry::Record()
                    .set("own_spice", share->ownSpice).set("reachable_spice", share->reachableSpice).set("rivals", share->rivals)
                    .set("worker_cap", gate.workerCap).set("bay_cap", gate.bayCap).set("funding", pass.cityBoomFunding)
                    .set("spice_payback_min", spicePayback).set("tax_payback_min", taxPayback).set("inert", gate.inert)
                    .set("tax_faster", taxFaster).set("tax_per_min", taxIncome).set("tax_capital", taxCapital)
                    .set("tax_payback_source", "standing_city_support_replacement_capital")
                    .set("payback_rounding", "ceiling_minutes_minus_one_if_no_tax")
                    .set("worker_yield_per_min", kCityBoomWorkerSpicePerMinute).set("horizon_min", f.cityBoomPaybackMinutes));
                cityBoomSpiceGateTraceValid = true;
                lastCityBoomSpiceGateTrace = decision;
            }
        }
    }
    if (pass.cityBoomFunding) {
        pass.militaryBudget = std::min(pass.militaryBudget, pass.cityBoom == CityBoomKind::Greedy ? 0 : 4000);
        pass.lightBudget = pass.militaryBudget;
        pass.surplusArmy = false;
    }
}

void QuantBot::planCityState(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    pass.cityLandValueServices = cityLandValueServiceScope();
    // Per-house city stats — CitySimulation now tracks these per player.
    if (pass.cityBoom == CityBoomKind::ShortTax
        && !(pass.cityLandValueServices && botConfig().force.cityGrowthStageTax)) {
        auto* sim = currentGame->getCitySimulation();
        const int tax = pass.cityBoomFunding ? 3 : DuneCity::kDefaultCityTaxRate;
        if (sim->getCityTax(houseID) != tax) {
            // Like AI police funding below, this is a deterministic simulation
            // order. Same-cycle queued UI commands have already been consumed.
            sim->setCityTax(houseID, tax);
            traceDecision("city_boom_tax", AITelemetry::Record().set("rate", tax)
                .set("opening", pass.cityBoomFunding).set("stop_minute", botConfig().force.cityBoomTaxMinutes));
        }
    }
    pass.ownResPop = 0; pass.ownComPop = 0; pass.ownIndPop = 0; pass.ownTotalPop = 0; pass.ownTaxBaseEighths = 0;
    pass.ownAvgLandValue = 0;
    pass.ownResValve = 0; pass.ownComValve = 0; pass.ownIndValve = 0;

    if (pass.citySimEnabled) {
        auto* citySim = currentGame->getCitySimulation();
        if (citySim) {
            const auto& hs = citySim->getHouseState(getHouse()->getHouseID());
            pass.ownResPop = hs.resPop;
            pass.ownComPop = hs.comPop;
            pass.ownIndPop = hs.indPop;
            pass.ownTotalPop = hs.getTotalPop();
            pass.ownTaxBaseEighths = hs.taxBaseEighths;
            pass.ownAvgLandValue = hs.avgLandValue;
            pass.ownResValve = hs.resValve;
            pass.ownComValve = hs.comValve;
            pass.ownIndValve = hs.indValve;
        }
    }

    if (pass.cityLandValueServices && botConfig().force.cityGrowthStageTax) {
        auto* sim = currentGame->getCitySimulation();
        const int population = QuantBotCityPolicy::displayPopulation(pass.ownTotalPop);
        const int threshold = botConfig().force.cityGrowthTaxPopulation;
        const int tax = population < threshold ? 3 : DuneCity::kDefaultCityTaxRate;
        if (sim->getCityTax(houseID) != tax) {
            // Like ShortTax and police budgets, apply the deterministic AI order
            // directly after this cycle's command phase. No unsaved phase latch.
            const int previous = sim->getCityTax(houseID);
            sim->setCityTax(houseID,tax);
            traceDecision("city_growth_tax_stage",AITelemetry::Record()
                .set("previous",previous).set("rate",tax).set("display_population",population)
                .set("population_threshold",threshold).set("growth_stage",population<threshold));
        }
    }

    if (pass.citySimEnabled && !builderOnly() && gameMode == GameMode::Custom) {
        auto* sim = currentGame->getCitySimulation();
        const auto& hs = sim->getHouseState(houseID);
        const int recentLosses = std::count_if(recentStructureLosses.begin(),recentStructureLosses.end(),[&](const auto& loss) {
            return getGameCycleCount()-loss.cycle < MILLI2CYCLES(180000);
        });
        const int taxIncome = DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(pass.ownTaxBaseEighths,sim->getCityTax(getHouse()->getHouseID()),pass.ownAvgLandValue));
        const int powerCost = getHouse()->isPowerRequired() ? getHouse()->getPowerRequirement()/8 : 0;
        // Include queued services, so their completion cannot silently push
        // recurring costs above the limit between periodic city scans.
        int committedPoliceMilli=0;
        for (Uint32 item:{Structure_PoliceStation,Structure_GunTurret,Structure_RocketTurret})
            committedPoliceMilli+=pass.itemCount[item]*DuneCity::annualPerMinute((DuneCity::getPoliceAnnualCost(item)*1000).lround());
        const int committedPoliceCost=(committedPoliceMilli+999)/1000;
        const int budgetPercent=CityServiceInvestmentPolicy::policingBudgetPercent(pass.money,taxIncome,powerCost,committedPoliceCost);
        int affordable=CityServiceInvestmentPolicy::affordablePoliceFunding(taxIncome,powerCost,committedPoliceCost,budgetPercent);
        CityLandValueBudget serviceBudget;
        if (pass.cityLandValueServices) {
            serviceBudget=cityLandValueBudget(pass.money,committedPoliceCost);
            affordable=serviceBudget.funding;
        }
        // Cuts enforce the recurring limit immediately. Recovered headroom
        // waits for review; the optional longer small-increase review prevents
        // the deadband from leaving a solvent city's funding permanently low.
        const int funding=CityServiceInvestmentPolicy::smoothedPoliceFunding(hs.policeFundingPercent,
            affordable,getGameCycleCount(),lastPoliceBudgetReviewCycle,botConfig().policeFundingRecovery);
        if (funding != hs.policeFundingPercent) {
            const int previousFunding=hs.policeFundingPercent;
            lastPoliceBudgetReviewCycle=getGameCycleCount();
            // AI updates already run deterministically on every peer, after
            // this cycle's commands have executed. A same-cycle queued budget
            // command is never consumed; apply it like other AI economic orders.
            sim->setPoliceFundingPercent(houseID,funding);
            traceDecision("city_police_budget",AITelemetry::Record().set("previous",previousFunding)
                .set("funding",funding).set("tax_income",taxIncome).set("power_cost",powerCost)
                .set("nominal_cost",hs.nominalPoliceCost).set("cash",pass.money).set("recent_losses",recentLosses)
                .set("budget_percent",budgetPercent).set("committed_police_cost",committedPoliceCost)
                .set("reason",funding < previousFunding ? "police_budget_limit" : "police_budget_headroom"));
            if (pass.cityLandValueServices)
                traceDecision("city_land_value_funding_target",AITelemetry::Record()
                    .set("previous",previousFunding).set("funding",funding)
                    .set("target",serviceBudget.targetFunding).set("affordable",affordable)
                    .set("spice_receipts_per_minute",serviceBudget.spiceIncome)
                    .set("actual_tax_per_minute",serviceBudget.taxIncome)
                    .set("power_cost",serviceBudget.powerCost).set("police_allowance",serviceBudget.allowance)
                    .set("committed_police_cost",committedPoliceCost).set("spendable",pass.money)
                    .set("working_cash_floor",2000));
        }
    }

    // Strategic structures must eventually outrank repeatable choices such as
    // factories, zones, and reactive turrets. These timers stay runtime-only so
    // loading an older save starts a fresh bounded wait without changing save data.
    pass.currentBuildCycle = getGameCycleCount();
    // Forecast two minutes from the last thirty seconds of actual demand.
    // Sample on simulation cycles and save it so peers/reloads agree. Falling
    // or flat demand removes the measured trend; latent zone load is reserved below.
    if (pass.citySimEnabled) {
        const int required = getHouse()->getPowerRequirement();
        if (powerDemandSampleCycle == 0) {
            powerDemandSampleCycle = pass.currentBuildCycle;
            powerDemandSample = required;
        } else if (pass.currentBuildCycle-powerDemandSampleCycle >= MILLI2CYCLES(30000)) {
            projectedPowerDemandGrowth = QuantBotBuildPolicy::projectedPowerGrowth(
                powerDemandSample,required,pass.currentBuildCycle-powerDemandSampleCycle,MILLI2CYCLES(120000));
            powerDemandSampleCycle = pass.currentBuildCycle;
            powerDemandSample = required;
        }
    }
}

bool QuantBot::cityLandValueServiceScope() const {
    const auto& force=botConfig().force;
    return (force.cityLandValueServices || force.cityGrowthStageTax) && citySpiceFfaScope(4);
}

QuantBot::CityLandValueBudget QuantBot::cityLandValueBudget(int spendable, int fullPoliceCost) const {
    CityLandValueBudget budget;
    const auto* sim=currentGame->getCitySimulation();
    const int house=getHouse()->getHouseID();
    const auto& state=sim->getHouseState(house);
    budget.targetFunding=botConfig().force.cityGrowthStageTax
        ? 100 : botConfig().force.cityLandValueFundingPercent;
    budget.taxIncome=DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(
        state.taxBaseEighths,sim->getCityTax(house),state.avgLandValue));
    // Gross, actually refined spice, never an assumed worker yield. Historical
    // receipts cease to back a service when its workers, bays or local field go.
    // The separate cash limit below also prevents storage losses becoming cash.
    if (getAIHarvesterCount()>0 && getAIRefineryBays()>0
        && getHouse()->getHarvestedSpice()>0 && citySpiceShare().ownSpice>0) {
        const int64_t elapsed=std::max<int64_t>(DuneCity::kCyclesPerMinute,getGameCycleCount());
        budget.spiceIncome=int(std::min<int64_t>(std::numeric_limits<int>::max(),
            int64_t(getHouse()->getHarvestedSpice().lround())*DuneCity::kCyclesPerMinute/elapsed));
    }
    budget.powerCost=getHouse()->isPowerRequired() ? getHouse()->getPowerRequirement()/8 : 0;
    const int income=int(std::min<int64_t>(std::numeric_limits<int>::max(),
        int64_t(budget.spiceIncome)+budget.taxIncome));
    const int percent=CityServiceInvestmentPolicy::policingBudgetPercent(
        spendable,income,budget.powerCost,fullPoliceCost);
    // Keep the native recurring-income share for army/worker/tech spending.
    // In addition, the next minute's entire police bill must fit above 2000
    // uncommitted cash; starting money never masquerades as recurring income.
    budget.allowance=std::min(std::max(0,spendable-2000),
        CityServiceInvestmentPolicy::policingAllowance(income,budget.powerCost,percent));
    budget.funding=fullPoliceCost>0
        ? int(std::min<int64_t>(budget.targetFunding,int64_t(budget.allowance)*100/fullPoliceCost))
        : budget.allowance>0 ? budget.targetFunding : 0;
    return budget;
}

bool QuantBot::cityFirstPoliceAllocationReady() const {
    // Bound the allocation by live/queued service, not the unsaved built-item
    // statistics. Losing the station makes a fresh solvent rebuild eligible.
    if (!cityLandValueServiceScope() || getHouse()->getNumItems(Structure_PoliceStation)>0
        || getHouse()->getNumItems(Structure_HeavyFactory)==0
        || getAIHarvesterCount()==0 || getAIRefineryBays()==0
        || getHouse()->getHarvestedSpice()<=0 || getHouse()->getCityTaxReceipts()<=0) return false;
    const auto& state=currentGame->getCitySimulation()->getHouseState(getHouse()->getHouseID());
    const int zones=getHouse()->getNumItems(Structure_ZoneResidential)
        +getHouse()->getNumItems(Structure_ZoneCommercial)+getHouse()->getNumItems(Structure_ZoneIndustrial);
    if (state.getTotalPop()<=0 || zones<CityEconomyInvestmentPolicy::kBootstrapZoneSeed) return false;
    return std::none_of(reservedStructures.begin(),reservedStructures.end(),[](const auto& plan) {
        return plan.second.item==Structure_PoliceStation;
    });
}

void QuantBot::planStrategicReserve(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    const Uint32 noEligibilityCycle = std::numeric_limits<Uint32>::max();
    pass.anyConstructionYardCanBuildIX = false;
    pass.anyConstructionYardCanBuildRefinery = false;
    bool anyConstructionYardCanBuildPalace = false;
    for (const StructureBase* pStructure : getStructureList()) {
        if (pStructure->getOwner() != getHouse()
            || pStructure->getItemID() != Structure_ConstructionYard
            || !pStructure->isABuilder()) {
            continue;
        }

        const BuilderBase* pConstructionYard = static_cast<const BuilderBase*>(pStructure);
        pass.anyConstructionYardCanBuildIX |= pConstructionYard->isAvailableToBuild(Structure_IX);
        pass.anyConstructionYardCanBuildRefinery |= pConstructionYard->isAvailableToBuild(Structure_Refinery);
        anyConstructionYardCanBuildPalace |= pConstructionYard->isAvailableToBuild(Structure_Palace);
    }

    pass.customStrategicPlanning = gameMode == GameMode::Custom && !builderOnly();
    pass.stablePower = getHouse()->hasPower();
    // itemCount already includes every queued and unplaced palace from all
    // builders, so parallel construction yards cannot exceed the target.
    pass.palaceTarget = QuantBotBuildPolicy::palaceTarget(
        getGameInitSettings().getGameOptions().onlyOnePalace, pass.citySimEnabled,
        pass.ownTotalPop * DuneCity::CitySimulation::kPopDisplayMultiplier,
        static_cast<int>(difficulty));
    const bool palaceAllowedNow = pass.itemCount[Structure_Palace] < pass.palaceTarget;
    const bool ixEligible = pass.customStrategicPlanning
        && pass.itemCount[Structure_IX] == 0
        && pass.itemCount[Structure_HeavyFactory] > 0
        && pass.itemCount[Structure_HighTechFactory] > 0
        && pass.itemCount[Structure_RepairYard] > 0
        && pass.anyConstructionYardCanBuildIX;
    const bool palaceEligible = pass.customStrategicPlanning
        && palaceAllowedNow
        && pass.itemCount[Structure_HeavyFactory] > 0
        && pass.itemCount[Structure_LightFactory] > 0
        && anyConstructionYardCanBuildPalace;

    auto updateEligibilityTimer = [&](bool eligible, Uint32& eligibleSinceCycle) {
        if (!eligible) {
            eligibleSinceCycle = noEligibilityCycle;
        } else if (eligibleSinceCycle == noEligibilityCycle) {
            eligibleSinceCycle = pass.currentBuildCycle;
        }
    };
    updateEligibilityTimer(ixEligible, ixEligibleSinceCycle);
    updateEligibilityTimer(palaceEligible, palaceEligibleSinceCycle);

    Uint32 ixWaitMs = 75000;
    Uint32 palaceWaitMs = 120000;
    switch (difficulty) {
        case Difficulty::Medium:
            ixWaitMs = 50000;
            palaceWaitMs = 90000;
            break;
        case Difficulty::Hard:
            ixWaitMs = 35000;
            palaceWaitMs = 60000;
            break;
        case Difficulty::Brutal:
            ixWaitMs = 20000;
            palaceWaitMs = 45000;
            break;
        case Difficulty::Defend:
        case Difficulty::Easy:
            break;
    }

    pass.ixOverdue = ixEligible
        && pass.currentBuildCycle - ixEligibleSinceCycle >= MILLI2CYCLES(ixWaitMs);
    pass.palaceOverdue = palaceEligible
        && pass.currentBuildCycle - palaceEligibleSinceCycle >= MILLI2CYCLES(palaceWaitMs);
    pass.strategicReserveItem = NONE_ID;
    if (pass.stablePower && pass.ixOverdue) {
        pass.strategicReserveItem = Structure_IX;
    } else if (pass.stablePower && pass.palaceOverdue) {
        pass.strategicReserveItem = Structure_Palace;
    }
    // Reserve only the cost of a feasible order. An unplaceable structure must
    // not freeze production indefinitely, and excess funds remain spendable.
    if (pass.strategicReserveItem != NONE_ID && !findPlaceLocation(pass.strategicReserveItem).isValid()) {
        pass.strategicReserveItem = NONE_ID;
    }
    pass.strategicReserveCost = pass.strategicReserveItem == NONE_ID ? 0
        : data[pass.strategicReserveItem][houseID].price;
}

void QuantBot::planSpiceEconomy(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    fieldExpansion = surveyFieldExpansion();
    if (pass.itemCount[Structure_Refinery] > getHouse()->getNumItems(Structure_Refinery)) {
        fieldExpansion.refinery = Coord::Invalid();
        fieldExpansion.yard = Coord::Invalid();
    }
    pass.spiceShare = pass.fleetRatios ? pass.fleetSpiceShare
        : lastCalculatedSpice / std::max(1, pass.spiceCompetitors);
    // Same competitor count and, under city sim, the same target the space
    // reservation above already used.
    // The campaign economy push no longer plans as the only house on the map: a target of 43-56
    // harvesters left the Hard partner without an army for 12 minutes in missions 20-22.
    pass.mapSpiceHarvesterTarget = pass.vanillaEconomy
        ? DuneCity::vanillaHarvesterTarget(lastCalculatedSpice, style().greedyHarvest ? 1 : pass.spiceCompetitors, harvesterLimit)
        : pass.citySpiceWorkerTarget;
    pass.spiceHarvesterTarget = pass.citySimEnabled ? pass.mapSpiceHarvesterTarget
        : QuantBotBuildPolicy::refineryThroughputHarvesterTarget(
            pass.spiceShare,pass.mapSpiceHarvesterTarget,getAIRefineryBays(),harvesterLimit);
    pass.fundedHarvesterTarget = pass.citySimEnabled
        ? CityEconomyInvestmentPolicy::factoryHarvesterTarget(pass.spiceHarvesterTarget,harvesterLimit)
        : QuantBotBuildPolicy::fundedSpiceHarvesters(pass.mapSpiceHarvesterTarget,getAIRefineryBays());
    pass.vanillaPlan = vanillaBoldPlan(vanillaBoomReceipt);
    const auto& force = botConfig().force;
    const bool spiceGate = force.vanillaBoomSpiceGate
        && (force.vanillaHarvesterBoom || force.vanillaRefineryBoom) && vanillaBoomGame();
    QuantBotVanillaPolicy::SpiceCaps spiceCap;
    if (spiceGate) {
        // Build-pass scratch only: live simulation state, not fog memory, saved state or a survey cache.
        // A structure claims once per terrain region occupied by its footprint, not once per tile.
        struct Anchor { const StructureBase* structure; int region; bool own; };
        std::vector<Anchor> anchors;
        anchors.reserve(getStructureList().size());
        bool ownReachable = false;
        for (const auto* structure : getStructureList()) {
            const auto item = structure->getItemID();
            if ((item != Structure_ConstructionYard && item != Structure_Refinery) || structure->getHealth() <= 0)
                continue;
            const bool own = structure->getOwner() == getHouse();
            const Coord origin = structure->getLocation(), size = structure->getStructureSize();
            const auto first = anchors.size();
            for (int y = origin.y; y < origin.y + size.y; ++y)
                for (int x = origin.x; x < origin.x + size.x; ++x) {
                    const int region = getMap().vehicleTerrainRegion(Coord(x, y));
                    if (!region || std::any_of(anchors.begin() + first, anchors.end(),
                        [region](const Anchor& anchor) { return anchor.region == region; })) continue;
                    anchors.push_back({structure, region, own});
                    ownReachable |= own;
                }
        }
        int64_t share = 0;
        if (ownReachable) for (int y = 0; y < getMap().getSizeY(); ++y)
            for (int x = 0; x < getMap().getSizeX(); ++x) {
                const auto spice = getMap().getTile(x, y)->getSpice().lround();
                if (spice <= 0) continue;
                const Coord at(x, y);
                const int region = getMap().vehicleTerrainRegion(at);
                if (!region) continue;
                int64_t best = std::numeric_limits<int64_t>::max();
                int nearest = 0, ownNearest = 0;
                for (const auto& anchor : anchors) {
                    if (anchor.region != region) continue;
                    const Coord point = anchor.structure->getClosestPoint(at);
                    const int64_t dx = int64_t(x) - point.x, dy = int64_t(y) - point.y;
                    const int64_t distance = dx * dx + dy * dy;
                    if (distance < best) {
                        best = distance;
                        nearest = 1;
                        ownNearest = anchor.own;
                    } else if (distance == best) {
                        ++nearest;
                        ownNearest += anchor.own;
                    }
                }
                share += QuantBotVanillaPolicy::spiceTileShare(spice, nearest, ownNearest);
            }
        spiceCap = QuantBotVanillaPolicy::spiceCaps(share, force.vanillaBoomSpicePerRefinery);
        const int currentWorkers = getAIHarvesterCount();
        pass.vanillaPlan = QuantBotVanillaPolicy::spiceGatedPlan(pass.vanillaPlan, spiceCap,
            getHouse()->getNumItems(Structure_Refinery), currentWorkers);
        vanillaBoomReceipt.plan = pass.vanillaPlan;
        vanillaBoomReceipt.reachableSpiceShare = share;
        vanillaBoomReceipt.refineryCap = spiceCap.refineries;
        vanillaBoomReceipt.harvesterCap = spiceCap.workers;
        vanillaBoomReceipt.currentWorkers = currentWorkers;
    }
    if (pass.vanillaPlan != QuantBotVanillaPolicy::Plan::None) {
        const int sustainable = std::min(harvesterLimit, pass.spiceShare / 2000);
        const int workerPrice = std::max(1, data[Unit_Harvester][houseID].price);
        const int64_t economicGap = std::max<int64_t>(0,
            vanillaBoomReceipt.leaderEconomy * force.vanillaReactiveEconomyRatioBps / 10000
                - vanillaBoomReceipt.ownEconomy);
        const int reactiveWorkers = std::max(8, getAIHarvesterCount()
            + int(std::min<int64_t>(harvesterLimit, (economicGap + workerPrice - 1) / workerPrice)));
        pass.vanillaBoomTargets = QuantBotVanillaPolicy::targets(pass.vanillaPlan,
            force.vanillaHarvesterBoomWorkers, force.vanillaRefineryBoomRefineries, reactiveWorkers, sustainable);
        if (spiceGate && (pass.vanillaPlan == QuantBotVanillaPolicy::Plan::Refinery
                          || pass.vanillaPlan == QuantBotVanillaPolicy::Plan::Harvester))
            pass.vanillaBoomTargets = QuantBotVanillaPolicy::capSpiceTargets(pass.vanillaBoomTargets, spiceCap);
        pass.spiceHarvesterTarget = pass.mapSpiceHarvesterTarget = pass.fundedHarvesterTarget
            = pass.vanillaBoomTargets.workers;
        pass.vanillaBoomArmy = QuantBotVanillaPolicy::armyPhase(pass.vanillaPlan);
        pass.vanillaBoldTechDeferred = QuantBotVanillaPolicy::deferTech(pass.vanillaPlan,
            getAIHarvesterCount(), getHouse()->getNumItems(Structure_Refinery),
            getHouse()->getNumItems(Structure_HeavyFactory), pass.vanillaBoomTargets);
        pass.vanillaBoldMilitaryHeld = pass.vanillaPlan == QuantBotVanillaPolicy::Plan::GreedyEconomy
            && pass.itemCount[Unit_Harvester] < pass.spiceHarvesterTarget;
        // At the stop, income no longer replenishes a generic future-refinery cash pool.
        // Missing bootstrap workers and the selected real prerequisite retain their own quotes.
        if (pass.vanillaBoomArmy) pass.economyReserve = 0;
        if (pass.vanillaBoldTechDeferred
            && (pass.strategicReserveItem == Structure_IX || pass.strategicReserveItem == Structure_Palace)) {
            pass.strategicReserveItem = NONE_ID;
            pass.strategicReserveCost = 0;
        }
    }
    if (pass.cityBoomWorkers > 0) pass.spiceHarvesterTarget = pass.fundedHarvesterTarget = pass.cityBoomWorkers;
    if (pass.cityBoom == CityBoomKind::Reactive && lastCalculatedSpice <= 0)
        pass.spiceHarvesterTarget = pass.fundedHarvesterTarget = 0;
    if (pass.fleetRatios) {
        const auto fleet = fleetRatioTreatment();
        const auto targets = QuantBotBuildPolicy::economicFleetTargets(pass.fleetSpiceShare,
            harvesterLimit, getAIRefineryBays(), pass.itemCount[Unit_Harvester],
            fleet.harvestersPerBay, fleet.carryallQuarters, fleet.repairYards,
            data[Unit_Harvester][houseID].price, data[Structure_Refinery][houseID].price,
            data[Unit_Carryall][houseID].price, !pass.harvesterFactories.empty());
        pass.mapSpiceHarvesterTarget = targets.sustainable;
        pass.spiceHarvesterTarget = pass.fundedHarvesterTarget = targets.workers;
        pass.fleetRefineryTarget = targets.refineries;
        pass.repairBaseline = pass.repairTarget = targets.repairYards;
    }
    pass.citySpiceCapacityRunway = botConfig().force.citySpiceCapacityRunway && pass.citySimEnabled
        && gameMode == GameMode::Custom && !campaignPolicyApplies()
        && lastCalculatedSpice > 0
        && CityEconomyInvestmentPolicy::spiceEconomyViable(pass.mapSpiceHarvesterTarget);
    pass.busyRefineries = 0; pass.freeRefineries = 0; pass.unbookedRefineries = 0; pass.waitingHarvesters = 0; pass.waitingCargo = 0; pass.blockedRefineryReturns = 0;
    pass.fieldReturnersBlocked = 0;
    auto refineryWaitingForTransport=[](const StructureBase* structure) -> int {
        const auto* refinery=dynamic_cast<const Refinery*>(structure);
        if (refinery && ModManager::instance().isTornieFamilyContentActive()) {
            int waiting = 0;
            for (int bay = 0; bay < refinery->getNumBays(); ++bay) {
                const auto* worker = dynamic_cast<const Harvester*>(refinery->getContainedHarvester(bay));
                if (worker && worker->getAmountOfSpice() <= 0) ++waiting;
            }
            return waiting;
        }
        const auto* worker=refinery ? dynamic_cast<const Harvester*>(refinery->getContainedHarvester()) : nullptr;
        return worker && worker->getAmountOfSpice()<=0;
    };
    {
        for(const auto* structure:getStructureList()) if(structure->getOwner()==getHouse()&&structure->acceptsHarvesterDropoff()) {
            const auto* refinery = dynamic_cast<const Refinery*>(structure);
            if (refinery && ModManager::instance().isTornieFamilyContentActive()) {
                const int busy = refinery->getNumOccupiedBays(), free = refinery->getNumBays() - busy;
                pass.busyRefineries += busy;
                pass.freeRefineries += free;
                pass.unbookedRefineries += std::max(0,free - int(structure->getHarvesterDropoffBookings()));
            } else {
                if(structure->isHarvesterDropoffFree())++pass.freeRefineries;else ++pass.busyRefineries;
                if(structure->isHarvesterDropoffFree() && structure->getHarvesterDropoffBookings()==0)++pass.unbookedRefineries;
            }
            pass.blockedRefineryReturns+=refineryWaitingForTransport(structure);
            pass.refineryQueues.set(std::to_string(structure->getObjectID()),AITelemetry::Record()
                .set("x",structure->getX()).set("y",structure->getY()).set("busy",!structure->isHarvesterDropoffFree())
                .set("waiting_for_return_transport",refineryWaitingForTransport(structure))
                .set("bookings",structure->getHarvesterDropoffBookings()));
        }
        for(const auto* unit:getUnitList()) if(unit->getOwner()==getHouse()) {
            const auto* harvester=dynamic_cast<const Harvester*>(unit);
            const auto* target=harvester?dynamic_cast<const StructureBase*>(harvester->getTarget()):nullptr;
            if(harvester&&harvester->isActive()&&harvester->isReturning()&&harvester->getAmountOfSpice()>0
                && target&&target->getOwner()==getHouse()&&target->acceptsHarvesterDropoff()&&!target->isHarvesterDropoffFree()
                && !refineryWaitingForTransport(target)) {
                ++pass.waitingHarvesters;
                pass.fieldReturnersBlocked+=blockDistance(harvester->getLocation(),target->getClosestPoint(harvester->getLocation()))
                    >=MIN_CARRYALL_LIFT_DISTANCE;
                pass.waitingCargo+=std::min(int(HARVESTERMAXSPICE),harvester->getAmountOfSpice().lround());
            }
        }
    }
    if(pass.waitingHarvesters<pass.unbookedRefineries+2)refineryQueueSince=std::numeric_limits<Uint32>::max();
    else if(refineryQueueSince==std::numeric_limits<Uint32>::max())refineryQueueSince=getGameCycleCount();
    pass.unloadingBacklog = CityEconomyInvestmentPolicy::unloadingQueueNeedsBay(pass.waitingHarvesters,pass.unbookedRefineries,
        pass.itemCount[Structure_Refinery]-getAIRefineryBays(),
        refineryQueueSince!=std::numeric_limits<Uint32>::max()&&getGameCycleCount()-refineryQueueSince>=MILLI2CYCLES(10000));
    if(pass.citySimEnabled&&(rockSurveyCycle==std::numeric_limits<Uint32>::max()
        || getGameCycleCount()-rockSurveyCycle>=MILLI2CYCLES(15000))) {
        // Free rock is not the same as somewhere to build. The placement search
        // applies the rules a factory actually has to satisfy — access, safety,
        // and roads it may compromise on before giving up — so its verdict is
        // what "no room left" means here. It runs at the survey cadence, not
        // once per pass, and before the site survey: whether the base is built
        // out is what tells colonisation the missing core cannot be finished
        // here, so it must be measured first rather than read from last survey.
        baseProductionRoomBlocked=!findPlaceLocation(Structure_HeavyFactory).isValid();
        rockSurveyCycle=getGameCycleCount();
        rockExpansionSite=findRockExpansionSite();
    }
    pass.rockExpansionNeeded = pass.citySimEnabled&&colonySiteOpen()
        && (availableBaseRock<48 || (pass.unloadingBacklog&&!findPlaceLocation(Structure_Refinery).isValid()));
    pass.cityWorkingReserve = data[Unit_Tank][houseID].price
        + (pass.itemCount[Unit_Harvester] < pass.fundedHarvesterTarget ? data[Unit_Harvester][houseID].price : 0)
        + (!pass.citySimEnabled && pass.itemCount[Structure_Refinery] < QuantBotBuildPolicy::desiredSpiceRefineries(
            pass.mapSpiceHarvesterTarget,pass.itemCount[Unit_Harvester]) ? data[Structure_Refinery][houseID].price : 0);
    pass.orderedSpiceHarvester = false;
    pass.transportTechAvailable = gameMode == GameMode::Custom && lastCalculatedSpice > 0
        && data[Unit_Carryall][houseID].enabled && data[Structure_HighTechFactory][houseID].enabled
        && getHouse()->getTechLevel() >= data[Unit_Carryall][houseID].techLevel
        && getHouse()->getTechLevel() >= data[Structure_HighTechFactory][houseID].techLevel
        && !getHouse()->isAirUnitLimitReached();
    // Brutal's economy step (docs/ai-scenario-matrix.md, "Difficulty separation"): it keeps an 8-12
    // worker opening floor and equal (not double) army capital per worker while it grows its fleet up
    // to its larger harvester limit. DuneCity custom games had it already (helpers included); vanilla
    // custom games the house plays itself have it since 1.0.849, while it faces no more enemy houses
    // than its team has and is not outnumbered - against four AI Player Hard houses (All against
    // Atreides) the economy-first opening lost 6 of 6.
    pass.brutalCityEconomy = pass.citySimEnabled && gameMode == GameMode::Custom
        && difficulty == Difficulty::Brutal;
    int ownTeamHouses = 0, enemyHouses = 0;
    if (!pass.brutalCityEconomy && difficulty == Difficulty::Brutal)
        for (int i = 0; i < NUM_HOUSES; ++i)
            // The Natives are always "alive" but are no enemy house.
            if (const House* other = currentGame->getHouse(i); other && !other->isNatives() && other->isAlive())
                ++(other->getTeamID() == getHouse()->getTeamID() ? ownTeamHouses : enemyHouses);
    pass.brutalEconomy = pass.brutalCityEconomy || (gameMode == GameMode::Custom && difficulty == Difficulty::Brutal
        && !builderOnly() && !campaignPolicyApplies()
        && enemyHouses <= ownTeamHouses && !outnumberedByEstimate());
    pass.openingWorkerFloor = pass.techSavings
        || (pass.citySimEnabled && gameMode == GameMode::Custom) || pass.brutalEconomy;
}

bool QuantBot::openingFleetIncomplete(const BuildPass& pass) const {
    if (pass.cityBoomWorkers > 0)
        return !getHouse()->isGroundUnitLimitReached() && pass.itemCount[Unit_Harvester] < pass.cityBoomWorkers;
    if (pass.cityBoom == CityBoomKind::Reactive) return false;
    return pass.openingWorkerFloor
        && !getHouse()->isGroundUnitLimitReached()
        && CityEconomyInvestmentPolicy::openingWorkersNeeded(pass.itemCount[Unit_Harvester],pass.fundedHarvesterTarget,pass.brutalEconomy);
}

bool QuantBot::openingWorkersNeeded(BuildPass& pass) {
    // A port's listed worker offer is a real supplier, even while sold out.
    // Do not make emergency borrowing depend on an already-built factory.
    return openingFleetIncomplete(pass)
        && (!pass.harvesterFactories.empty()
            || (pass.techSavings && getHouse()->getNumItems(Structure_StarPort) > 0
                && currentGame->objectData.data[Unit_Harvester][getHouse()->getHouseID()].enabled
                && getHouse()->getChoam().getNumAvailable(Unit_Harvester) >= 0));
}

// Keep money for the missing workers when market stock or a delivery is
// temporarily unavailable. Count queued workers, so the reserve releases
// as soon as the fleet is funded rather than waiting for it to arrive.
int QuantBot::harvesterInvestmentReserve(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (pass.vanillaPlan != QuantBotVanillaPolicy::Plan::None
        && pass.itemCount[Unit_Harvester] < pass.spiceHarvesterTarget && getAIRefineryBays() > 0
        && (!pass.harvesterFactories.empty() || getHouse()->getNumItems(Structure_StarPort) > 0)
        && !getHouse()->isGroundUnitLimitReached())
        return data[Unit_Harvester][houseID].price;
    const int replacement = cityHarvesterReplacementFloor(pass);
    if (pass.itemCount[Unit_Harvester] < replacement && !getHouse()->isGroundUnitLimitReached()
        && (!pass.harvesterFactories.empty() || getHouse()->getNumItems(Structure_StarPort) > 0)) {
        int price = data[Unit_Harvester][houseID].price;
        for (const auto* factory : pass.harvesterFactories)
            price = std::max(price, purchasePrice(factory, Unit_Harvester));
        if (getHouse()->getNumItems(Structure_StarPort) > 0)
            price = std::max(price, getHouse()->getChoam().getPrice(Unit_Harvester));
        return (replacement - pass.itemCount[Unit_Harvester]) * price;
    }
    if (!campaignPolicyApplies() || !isAlliedWithHuman()
        || gameMode != GameMode::Custom || builderOnly() || !(pass.vanillaEconomy || pass.citySimEnabled)
        || lastCalculatedSpice <= 0 || getHouse()->isGroundUnitLimitReached()
        || getAIRefineryBays() == 0
        || (pass.harvesterFactories.empty() && getHouse()->getNumItems(Structure_StarPort) == 0)) return 0;
    int target = pass.vanillaEconomy ? pass.spiceHarvesterTarget : pass.fundedHarvesterTarget;
    const auto& market = getHouse()->getChoam();
    if (getHouse()->getNumItems(Structure_StarPort) > 0 && isAlliedWithHuman()
        && market.getPrice(Unit_Harvester) > 0
        && market.getPrice(Unit_Harvester) < data[Unit_Harvester][houseID].price
        && getHouse()->getMaxHarvesters() > 0) target = getHouse()->getMaxHarvesters();
    if (getHouse()->getMaxHarvesters() > 0) target = std::min(target, getHouse()->getMaxHarvesters());
    return std::max(0, target-pass.itemCount[Unit_Harvester]) * data[Unit_Harvester][houseID].price;
}

bool QuantBot::needsFirstTransport(BuildPass& pass) {
    if (pass.fleetRatios && fleetRatioTreatment().carryallQuarters == 0) return false;
    // Preserve early carryalls while Brutal continues growing its fleet.
    if (pass.brutalEconomy ? CityEconomyInvestmentPolicy::openingWorkersNeeded(pass.itemCount[Unit_Harvester],pass.fundedHarvesterTarget)
                          : openingWorkersNeeded(pass)) return false;
    return QuantBotBuildPolicy::firstTransportNeeded(pass.transportTechAvailable,
        pass.itemCount[Structure_HeavyFactory],getAIHarvesterCount(),
        pass.itemCount[Unit_Carryall]);
}

void QuantBot::planPowerAndYards(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    const auto windSize = getStructureSize(Structure_WindTrap);
    pass.generationCostPerThousand = 1000 * (data[Structure_WindTrap][houseID].price
        + windSize.x*windSize.y*data[Structure_Slab1][houseID].price)
        / std::max(1,-data[Structure_WindTrap][houseID].power);
    pass.largestGenerator = 0;
    pass.currentZonePower = 0; pass.matureZonePower = 0; pass.committedPowerDemand = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse()) continue;
        const int power = data[structure->getItemID()][structure->getProductionHouseID()].power;
        if (power < 0) {
            pass.largestGenerator = std::max(pass.largestGenerator, -power);
            const auto size = getStructureSize(structure->getItemID());
            const int generationCost = data[structure->getItemID()][structure->getProductionHouseID()].price
                + size.x*size.y*data[Structure_Slab1][houseID].price;
            pass.generationCostPerThousand = std::min(pass.generationCostPerThousand,1000*generationCost/-power);
        }
        if (pass.citySimEnabled && DuneCity::isCityZoneStructure(structure->getItemID())) {
            const auto* zone = static_cast<const ZoneStructure*>(structure);
            pass.currentZonePower += zone->getZonePowerDraw();
            const Coord pos = zone->getLocation();
            if (getMap().tileExists(pos.x,pos.y)
                && DuneCity::getStructurePopulation(zone,getMap().getTile(pos.x,pos.y)->getCityZoneDensity())
                    < DuneCity::getZonePopulation(zone->getItemID(),1))
                ++pass.developingZones[zone->getItemID()-Structure_ZoneResidential];
            pass.matureZonePower += DuneCity::getZonePower(structure->getItemID(), 3);
        }
    }
    if (pass.citySimEnabled) for (Uint32 item=Structure_FirstID; item<=(ModManager::instance().isTornieFamilyContentActive() ? ItemID_LastID : Structure_LastID); ++item)
        if (isStructure(item)) pass.committedPowerDemand += std::max(0,rawPlannedItemCount(pass,item)-getHouse()->getNumItems(item))
            * (DuneCity::isCityZoneStructure(item) ? DuneCity::getZonePower(item,3) : std::max(0,data[item][houseID].power));
    if (currentGame->getGameInitSettings().getGameOptions().researchLab
        && (!pass.citySimEnabled || !ModManager::instance().isTornieFamilyContentActive()))
        pass.committedPowerDemand += std::max(0,pass.itemCount[Structure_ResearchLab]
            - getHouse()->getNumItems(Structure_ResearchLab)) * std::max(0,data[Structure_ResearchLab][houseID].power);
    int matureZonePower = pass.matureZonePower;
    if (botConfig().force.cityPowerGrowthHorizon && pass.citySimEnabled
        && gameMode == GameMode::Custom && !campaignPolicyApplies()
        && lastCalculatedSpice > 0
        && pass.money < int64_t(buildingCapitalCost(pass,Structure_WindTrap))
            + std::max(1000,pass.cityWorkingReserve)) {
        const int zonePower = std::max({DuneCity::getZonePower(Structure_ZoneResidential,3),
            DuneCity::getZonePower(Structure_ZoneCommercial,3),DuneCity::getZonePower(Structure_ZoneIndustrial,3)});
        matureZonePower = CityEconomyInvestmentPolicy::boundedMatureZonePower(pass.currentZonePower,
            pass.matureZonePower,zonePower,getHouse()->getNumItems(Structure_ConstructionYard));
    }
    pass.zoneGrowthHeadroom = QuantBotBuildPolicy::cityGrowthPowerHeadroom(
        pass.currentZonePower,matureZonePower,projectedPowerDemandGrowth,pass.committedPowerDemand);
    pass.cityPowerReserve = pass.citySimEnabled ? QuantBotBuildPolicy::cityPowerReserve(
        getHouse()->getPowerRequirement(), pass.largestGenerator) + pass.zoneGrowthHeadroom : 0;
    pass.nuclearPlan = pass.citySimEnabled && !needsFirstTransport(pass)
        && pass.itemCount[Structure_HeavyFactory] > 0
        && pass.itemCount[Structure_NuclearPlant] == getHouse()->getNumItems(Structure_NuclearPlant)
        && pass.itemCount[Structure_WindTrap] == getHouse()->getNumItems(Structure_WindTrap)
        && QuantBotBuildPolicy::planNuclearInvestment(getHouse()->getPowerRequirement(),
            getHouse()->getProducedPower(),pass.cityPowerReserve,std::max(1,-data[Structure_WindTrap][houseID].power))
        && pass.nuclearBuildAvailable;
    if (pass.nuclearPlan && botConfig().force.cityPowerSizing && pass.windBuildAvailable) {
        // Positive income can finance installments, but it cannot justify
        // occupying the only yard with 2,000 power for a one-windtrap top-up.
        const int windOutput=std::max(1,-data[Structure_WindTrap][houseID].power);
        const int need=std::max(0,getHouse()->getPowerRequirement()+pass.cityPowerReserve
            -getHouse()->getProducedPower());
        const int windNeeded=(need+windOutput-1)/windOutput;
        const int sites=availableBaseFootprints>=QuantBotColonisationPolicy::kCrampedFootprints
            ? std::max(availableBaseFootprints,windNeeded) : availableBaseFootprints;
        const int reserve=std::max(1000,pass.cityWorkingReserve);
        // Funding is judged by installmentOrder later; test useful capacity here.
        pass.nuclearPlan=QuantBotBuildPolicy::preferNuclearPower(need,windOutput,
            data[Structure_WindTrap][houseID].price,data[Structure_NuclearPlant][houseID].price,
            sites,std::max(pass.money,data[Structure_NuclearPlant][houseID].price+reserve),reserve);
    }
    if (pass.nuclearPlan) pass.nuclearPlan=findPlaceLocation(Structure_NuclearPlant).isValid();
    pass.cityYardTarget = pass.citySimEnabled ? QuantBotBuildPolicy::cityConstructionYardTarget(
        pass.money, pass.ownResValve, pass.ownComValve, pass.ownIndValve) : 0;
    // While new rock exists, production yards stop at the home formation's
    // share plus each colony's founding yard. Another MCV beyond that is worth
    // buying only as a colonist, so only while one may set out, and one at a
    // time; a colony blocked for now (unfinished core, lost colonists) waits
    // rather than turning into a further yard at home. A built-out base buys
    // no production yard at all: there is no footprint left to deploy one on.
    // A roomy base on a map with no other rock keeps the plain capacity
    // target: there is nowhere else to grow. The cap changes a decision only
    // when the target asks for more than the yards and MCVs already held, so
    // the formation flood runs only then. Campaigns and helpers never
    // colonise and keep the target they had.
    const bool builtOut=baseBuiltOut();
    if((rockExpansionSite.isValid()||builtOut)
        && pass.cityYardTarget>pass.itemCount[Structure_ConstructionYard]+pass.itemCount[Unit_MCV]
        && QuantBotColonisationPolicy::colonisationAllowed(colonisationDemand(0,0)))
        pass.cityYardTarget=std::min(pass.cityYardTarget,QuantBotColonisationPolicy::productionYardCeiling(
            yardFormations(),getHouse()->getNumItems(Structure_ConstructionYard),colonySiteOpen(),builtOut));
    // force.smallMapYardCap (short-map ladder, HANDOVER 2026-10-24): on a small map (neither
    // side above 96 tiles) a custom Hard/Brutal bot holds Medium's two city yards until its
    // first wave launches; MCVs for a third yard delayed its heavy factory and army behind
    // Medium's. Exhausted rock (below) still adds a yard, and a lost yard leaves capacity
    // under two, so it is still rebuilt.
    if(botConfig().force.smallMapYardCap && difficulty>=Difficulty::Hard && gameMode==GameMode::Custom
        && !builderOnly() && !campaignPolicyApplies() && customWave.launched==0
        && std::max(currentGameMap->getSizeX(),currentGameMap->getSizeY())<=96)
        pass.cityYardTarget=std::min(pass.cityYardTarget,2);
    if(pass.rockExpansionNeeded)pass.cityYardTarget=std::max(pass.cityYardTarget,getHouse()->getNumItems(Structure_ConstructionYard)+1);
    pass.yardLimit = getGameInitSettings().getGameOptions().maximumNumberOfConstructionYardsOverride;
    if(pass.yardLimit > 0) pass.cityYardTarget = std::min(pass.cityYardTarget, pass.yardLimit);
    pass.cityConstructionCapacity = pass.itemCount[Structure_ConstructionYard] + pass.itemCount[Unit_MCV];
    // Settling free rock is a space decision, not a capacity one: it is due
    // when the city has nowhere left to build and somewhere safe to go, and it
    // is the only thing that keeps a built-out city growing. It runs beside
    // the yard target rather than through it, so production capacity is still
    // governed by that target alone.
    pass.colonisationNeeded = colonisationMcvDue(pass.itemCount[Unit_MCV],pass.yardLimit)
        || (fieldExpansion.yard.isValid() && pass.itemCount[Unit_MCV] == 0
            && (pass.yardLimit <= 0 || pass.itemCount[Structure_ConstructionYard] < pass.yardLimit));
    // Optional construction capacity must not occupy the first heavy factory while a small start
    // is funding its first combat wave. A lost yard or exhausted home rock can still buy an MCV.
    pass.postponeCapacityMcv = (openingCombatBudgetActive() || campaignCombatBudgetActive())
        && pass.itemCount[Structure_ConstructionYard] > 0 && !pass.rockExpansionNeeded && !pass.colonisationNeeded;
    if (pass.vanillaBoomArmy && pass.itemCount[Structure_ConstructionYard] > 0)
        pass.postponeCapacityMcv = true;
    if (pass.postponeCapacityMcv) pass.cityYardTarget = std::min(pass.cityYardTarget, pass.cityConstructionCapacity);
}

void QuantBot::planCityPopulation(BuildPass& pass) {
    // Custom-game city size ceiling for this bot, including shared-house
    // helpers. Campaign games keep their own separate gates.
    pass.populationCeiling = getCityPopulationLimit(getMap().getSizeX()*getMap().getSizeY());
    // The land allowance scales with the effective ceiling, including Hard's raised one.
    pass.cityGrowthLimits = pass.populationCeiling > 0
        ? QuantBotCityPolicy::limits(pass.populationCeiling)
        : QuantBotCityPolicy::Limits{};
    if (civWarsRules()) {
        const int target = QuantBotBuildPolicy::civWarsZoneTarget(customWave.launched != 0,static_cast<int>(difficulty));
        auto& cap = pass.cityGrowthLimits.sharedZoneCap;
        cap = cap == QuantBotCityPolicy::kUnlimited ? target : std::min(cap,target);
    }
    pass.cityZonesIncludingQueued = pass.itemCount[Structure_ZoneResidential]
        + pass.itemCount[Structure_ZoneCommercial] + pass.itemCount[Structure_ZoneIndustrial];
    int cityPendingPopulation = 0;
    int currentCityPopulation = 0;
    if(pass.populationCeiling > 0) {
        for(Uint32 item=Structure_FirstID; item<=Structure_LastID; ++item)
            cityPendingPopulation += std::max(0,pass.itemCount[item]-getHouse()->getNumItems(item))*initialCityPopulation(item);
        for(const auto* structure : getStructureList()) {
            if(structure->getOwner()!=getHouse()) continue;
            const auto item=structure->getItemID();
            const Coord pos=structure->getLocation();
            if(!getMap().tileExists(pos.x,pos.y)) continue;
            const int level=DuneCity::isCityZoneStructure(item)
                ? getMap().getTile(pos.x,pos.y)->getCityZoneDensity()
                : DuneCity::effectiveCityLevel(item,std::max(1,int(structure->getCityOccupancy())));
            const int population=DuneCity::getStructurePopulation(structure,level)
                + (item==Structure_Palace ? DuneCity::getPalaceCommercialPopulation(level) : 0);
            currentCityPopulation += population;
            cityPendingPopulation += std::max(0,initialCityPopulation(item)-population);
        }
    }
    pass.cityDisplayPopulation = QuantBotCityPolicy::displayPopulation(currentCityPopulation);
    pass.cityPendingDisplayPopulation = QuantBotCityPolicy::displayPopulation(cityPendingPopulation);
}

int QuantBot::initialCityPopulation(Uint32 item) {
    return isStructure(item) ? DuneCity::getZonePopulation(item,1)
        + (item==Structure_Palace ? DuneCity::getPalaceCommercialPopulation(1) : 0) : 0;
}

bool QuantBot::cityAdmitsStructure(BuildPass& pass, Uint32 item) {
    // City capacity keeps one generator in flight. Emergency wind can still
    // restore current power while a reactor is pending, but another capital
    // path must not turn that recovery into a second reactor reservation.
    if (pass.techSavings && pass.citySimEnabled && item == Structure_NuclearPlant
        && powerGenerationPending(pass)) return false;
    if (pass.populationCeiling <= 0)
        return pass.cityGrowthLimits.sharedZoneCap == QuantBotCityPolicy::kUnlimited
            || !DuneCity::isCityZoneStructure(item)
            || QuantBotCityPolicy::allowsZone(pass.cityZonesIncludingQueued,pass.cityGrowthLimits.sharedZoneCap);
    if (!isStructure(item)) return true;
    // A Silo/refinery also houses industrial workers, but refusing the vault
    // at the population ceiling destroys the existing city's income. Zoning
    // keeps its cap; physical storage remains available to bank that income.
    if (botConfig().force.storageCapacity && !DuneCity::isCityZoneStructure(item)
        && currentGame->objectData.data[item][getHouse()->getHouseID()].capacity>0) return true;
    if(DuneCity::isCityZoneStructure(item)
        && !QuantBotCityPolicy::allowsZone(pass.cityZonesIncludingQueued,pass.cityGrowthLimits.sharedZoneCap)) return false;
    const int added=QuantBotCityPolicy::displayPopulation(initialCityPopulation(item));
    // Zero-population services, power and roads remain available even in an oversized save.
    return added==0 || QuantBotCityPolicy::allowsPopulation(pass.cityDisplayPopulation,
        pass.cityPendingDisplayPopulation,added,pass.populationCeiling);
}

void QuantBot::planCivicNeedAndTrips(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    pass.campaignPowerBudget = campaignPolicyApplies()
        && (difficulty==Difficulty::Easy || difficulty==Difficulty::Medium)
        && !pass.citySimEnabled && !pass.vanillaEconomy;


    // What the city simulation itself treats as a structural civic cap, taken
    // from its own population thresholds rather than from the clip flag.
    // civicDemandBlocked only marks a city update at which a positive valve was
    // pinned to zero; it goes quiet again during the phase where the raw valve
    // is negative, even though the population is still over the threshold and
    // the civic is still absent. Required infrastructure must not lose its
    // priority between those phases, so the need is derived from
    // DuneCity::missingDemandCivics - no second set of thresholds exists here -
    // and the clip mask is kept separately as evidence that demand is being
    // pinned right now.
    //
    // Counts are queue-inclusive, so an order placed by any yard quiets the
    // need immediately and a second yard cannot buy a duplicate.
    pass.civicDemandClipped = pass.citySimEnabled
        ? currentGame->getCitySimulation()->getHouseState(houseID).civicDemandBlocked : uint8_t(0);
    pass.civicStructuralNeed = [&]() -> uint8_t {
        if (!pass.citySimEnabled) return 0;
        DuneCity::ValveInputs thresholds;
        thresholds.resPop = pass.ownResPop;
        thresholds.comPop = pass.ownComPop;
        thresholds.indPop = pass.ownIndPop;
        thresholds.hasStadium = pass.itemCount[Structure_Stadium] > 0;
        thresholds.hasAirport = pass.itemCount[Structure_Airport] > 0;
        // Industry's seaport requirement is not a civic this rule buys; the
        // starport rules below own it, so it is never reported missing here.
        thresholds.hasStarport = true;
        return DuneCity::missingDemandCivics(thresholds);
    }();


    // Estimate travel from existing refineries, with a bounded local spice scan.
    // Missing nearby fields retain a conservative long trip instead of inventing
    // local resources. Future receipts are always capped by the remaining share.
    pass.refineryTripCycles = -1; pass.walkingTripCycles = -1; pass.refineryFieldRisk = 0;
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->acceptsHarvesterDropoff()) {
            forecastSpiceTrip(pass, structure->getLocation()); break;
        }
    pass.fillCycles = (FixPoint(HARVESTERMAXSPICE)/HARVESTSPEED).lround();
    pass.unloadCycles = HARVESTERMAXSPICE*8/5;
    pass.bayAnnualIncome = int(DuneCity::kCyclesPerMinute)*15/32;
    pass.citySpiceTransition = botConfig().force.citySpiceTransition && pass.citySimEnabled
        && gameMode == GameMode::Custom && !campaignPolicyApplies()
        && lastCalculatedSpice > 0
        && CityEconomyInvestmentPolicy::spiceTransitionDue(pass.spiceShare,pass.actualHarvesters,
            getAIRefineryBays(),workerAnnualIncome(pass),pass.bayAnnualIncome);
    if (pass.citySpiceTransition) {
        const bool workerReserved = pass.itemCount[Unit_Harvester] < pass.fundedHarvesterTarget;
        // Accepted orders finish; only new fleet commitments stop. Losses below
        // two working harvesters reopen normal recovery on the next build pass.
        pass.spiceHarvesterTarget = std::min(pass.spiceHarvesterTarget,pass.itemCount[Unit_Harvester]);
        pass.fundedHarvesterTarget = std::min(pass.fundedHarvesterTarget,pass.itemCount[Unit_Harvester]);
        if (workerReserved) pass.cityWorkingReserve -= currentGame->objectData.data[Unit_Harvester][houseID].price;
    }

    pass.cityRefineryCatchup = false; pass.cityRefineryOpening = false;
}

AITelemetry::Record QuantBot::decisionState(BuildPass& pass) {
    return AITelemetry::Record().set("credits", getHouse()->getCredits()).set("spendable", pass.money)
        .set("military", pass.militaryValue).set("military_limit", militaryValueLimit)
        .set("military_budget", pass.militaryBudget)
        .set("city_boom_kind", static_cast<int>(pass.cityBoom)).set("city_boom_funding", pass.cityBoomFunding)
        .set("city_boom_army", pass.cityBoomArmy).set("city_boom_workers", pass.cityBoomWorkers)
        .set("city_boom_bays", pass.cityBoomBays)
        .set("infantry_value", pass.infantryValue).set("infantry_target_percent", pass.infantryPercent)
        .set("light_vehicle_value", pass.lightVehicleValue).set("vehicle_plan_value", pass.vehiclePlanValue)
        .set("military_budget_source", pass.militaryBudgetOverridden ? "override_rolling_headroom" : "configured")
        .set("unit_count_override", getGameInitSettings().getGameOptions().maximumNumberOfUnitsOverride)
        .set("unit_limit_reached", getHouse()->isGroundUnitLimitReached()).set("max_units", getHouse()->getMaxUnits())
        .set("power_rules_enabled", pass.powerRules).set("rocket_turrets_need_power", pass.turretPowerRequired).set("city_effects_enabled", pass.citySimEnabled)
        .set("waiting_to_unload",pass.waitingHarvesters).set("unloading_backlog",pass.unloadingBacklog)
        .set("free_refineries",pass.freeRefineries).set("busy_refineries",pass.busyRefineries)
        .set("free_base_rock",availableBaseRock).set("rock_expansion_needed",pass.rockExpansionNeeded)
        .set("base_free_footprints",availableBaseFootprints)
        .set("base_production_room_blocked",baseProductionRoomBlocked)
        .set("base_built_out",baseBuiltOut())
        .set("colonisation_due",pass.colonisationNeeded).set("colonists_lost",colonistsLost())
        .set("economy_reserve", pass.economyReserve).set("queued_production_cost", pass.queuedProductionCost).set("queued_military_value", pass.queuedMilitaryValue)
        .set("power_produced", getHouse()->getProducedPower()).set("power_required", getHouse()->getPowerRequirement())
        .set("zone_power_current",pass.currentZonePower).set("zone_power_mature",pass.matureZonePower)
        .set("zone_growth_headroom",pass.zoneGrowthHeadroom).set("committed_power_demand",pass.committedPowerDemand)
        .set("nuclear_investment_due",pass.nuclearPlan)
        .set("city_power_reserve_target", pass.cityPowerReserve).set("largest_generator_nominal", pass.largestGenerator)
        .set("res_count", pass.itemCount[Structure_ZoneResidential]).set("com_count", pass.itemCount[Structure_ZoneCommercial])
        .set("ind_count", pass.itemCount[Structure_ZoneIndustrial])
        .set("construction_yards", pass.itemCount[Structure_ConstructionYard])
        .set("mcvs_including_queued", pass.itemCount[Unit_MCV])
        .set("city_yard_target", pass.cityYardTarget)
        .set("city_mcv_cash", pass.citySimEnabled ? pass.money : 0)
        .set("city_mcv_working_reserve", pass.citySimEnabled ? std::max(1000, pass.cityWorkingReserve) : 0)
        .set("city_spice_capacity_runway",pass.citySpiceCapacityRunway)
        .set("city_mcv_shortfall", pass.citySimEnabled ? std::max(0, pass.cityYardTarget - pass.cityConstructionCapacity) : 0)
        .set("vanilla_yard_target", pass.vanillaEconomy ? DuneCity::vanillaYardTarget(
            pass.money, getAIHarvesterCount()) : 0)
        .set("mcv_upgrade_in_progress", pass.mcvUpgradesInProgress > 0)
        .set("mcv_upgrades_in_progress", pass.mcvUpgradesInProgress)
        .set("mcv_shortfall", pass.vanillaEconomy ? DuneCity::vanillaMcvShortfall(
            pass.money, getAIHarvesterCount(), pass.itemCount[Structure_ConstructionYard], pass.itemCount[Unit_MCV]) : 0)
        .set("construction_plans_first", pass.citySimEnabled)
        .set("res_demand", pass.ownResValve).set("com_demand", pass.ownComValve).set("ind_demand", pass.ownIndValve)
        .set("res_pop", pass.ownResPop).set("com_pop", pass.ownComPop).set("ind_pop", pass.ownIndPop)
        .set("land_value", pass.ownAvgLandValue).set("spice_remaining", lastCalculatedSpice)
        .set("spice_share", pass.spiceShare).set("map_harvester_target", pass.mapSpiceHarvesterTarget)
        .set("fleet_ratios",pass.fleetRatios).set("fleet_spice_share",pass.fleetSpiceShare)
        .set("fleet_nearest_spice_distance",pass.fleetNearestSpiceDistance)
        .set("fleet_refinery_target",pass.fleetRefineryTarget)
        .set("city_spice_transition",pass.citySpiceTransition)
        .set("harvester_target", pass.spiceHarvesterTarget).set("refinery_harvester_target",
            QuantBotBuildPolicy::refineryThroughputHarvesterTarget(pass.spiceShare, 0,
                getAIRefineryBays(), harvesterLimit))
        .set("repair_baseline",pass.repairBaseline).set("repair_target",pass.repairTarget)
        .set("combat_vehicles",pass.combatVehicles).set("repair_waiting",pass.waitingRepairVehicles)
        .set("harvester_investment_reserve", harvesterInvestmentReserve(pass))
        .set("harvester_ai_limit", harvesterLimit).set("campaign_economy_push",pass.campaignEconomyPush)
        .set("campaign_city_policy",campaignCityEconomy()).set("campaign_city_baseline",campaignBaseline.allowance)
        .set("campaign_city_zone_cap",campaignCityLimits().sharedZoneCap).set("campaign_city_post_spice",campaignPostSpice())
        .set("campaign_city_income_goal",QuantBotCityCampaignPolicy::totalIncomeGoalPerMinute(campaignBaseline))
        .set("campaign_city_income_forecast",campaignIncomeForecastPerMinute())
        .set("city_population_limit",pass.cityGrowthLimits.displayPopulationLimit)
        .set("city_population_display",pass.cityDisplayPopulation)
        .set("city_population_pending",pass.cityPendingDisplayPopulation)
        .set("city_zone_cap",pass.cityGrowthLimits.sharedZoneCap)
        .set("city_zones_including_queued",pass.cityZonesIncludingQueued)
        .set("harvester_engine_limit", getHouse()->getMaxHarvesters())
        .set("funded_harvester_target", pass.vanillaEconomy ? pass.spiceHarvesterTarget : pass.fundedHarvesterTarget)
        .set("storage_capacity", getHouse()->getCapacity())
        .set("nuclear_pending", pass.itemCount[Structure_NuclearPlant] - getHouse()->getNumItems(Structure_NuclearPlant))
        .set("windtrap_pending", pass.itemCount[Structure_WindTrap] - getHouse()->getNumItems(Structure_WindTrap))
        .set("stadium_committed", pass.itemCount[Structure_Stadium]).set("airport_committed", pass.itemCount[Structure_Airport])
        .set("strategic_item", pass.strategicReserveItem).set("strategic_reserve", pass.strategicReserveCost);
}

bool QuantBot::campaignPowerNeeded(BuildPass& pass, int nextDemand) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    int demand=0;
    for (Uint32 item=Structure_FirstID;item<=(ModManager::instance().isTornieFamilyContentActive() ? ItemID_LastID : Structure_LastID);++item)
        if (isStructure(item)) demand+=std::max(0,rawPlannedItemCount(pass,item)-getHouse()->getNumItems(item))*std::max(0,data[item][houseID].power);
    return pass.campaignPowerBudget && CampaignDifficultyPolicy::needsWindtrap(
        getHouse()->getProducedPower(),getHouse()->getPowerRequirement(),demand,nextDemand,
        pass.itemCount[Structure_WindTrap]>getHouse()->getNumItems(Structure_WindTrap)
        || pass.itemCount[Structure_NuclearPlant]>getHouse()->getNumItems(Structure_NuclearPlant));
}

bool QuantBot::powerGenerationPending(BuildPass& pass) {
    return pass.itemCount[Structure_NuclearPlant] > getHouse()->getNumItems(Structure_NuclearPlant)
        || pass.itemCount[Structure_WindTrap] > getHouse()->getNumItems(Structure_WindTrap);
}

Uint32 QuantBot::chooseCityZone(BuildPass& pass, const BuilderBase* builder, bool bootstrap, bool missingOnly) {
    if (!campaignAllowsZone(pass.itemCount[Structure_ZoneResidential]+pass.itemCount[Structure_ZoneCommercial]+pass.itemCount[Structure_ZoneIndustrial])) return Uint32(NONE_ID);

    auto ranked = QuantBotBuildPolicy::rankZones(
        pass.itemCount[Structure_ZoneResidential], pass.itemCount[Structure_ZoneCommercial],
        pass.itemCount[Structure_ZoneIndustrial], pass.ownResValve, pass.ownComValve, pass.ownIndValve, bootstrap);
    // The site scorer still rewards residential infill. It must not
    // override the chosen zone type and suppress stronger jobs demand.
    Uint32 selected = NONE_ID;
    AITelemetry::Record candidates;
    for (Uint32 candidate : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
        const int demand = candidate == Structure_ZoneResidential ? pass.ownResValve
            : candidate == Structure_ZoneCommercial ? pass.ownComValve : pass.ownIndValve;
        int rank = -1;
        for (int i = 0; i < 3; ++i) if (ranked[i] == candidate) rank = i;
        candidates.set(std::to_string(candidate), AITelemetry::Record().set("rank", rank)
            .set("demand", demand).set("normalized_demand", QuantBotBuildPolicy::normalizedZoneDemand(candidate, demand))
            .set("count_including_queued", pass.itemCount[candidate]));
    }
    AITelemetry::Record evaluated;
    for (Uint32 candidate : ranked) {
        if (candidate == NONE_ID || (missingOnly && pass.itemCount[candidate] > 0)) continue;
        // Demand and site suitability select the tax candidate; the economic
        // comparison below decides whether more spice capacity is better.
        const char* reason = "lower_rank_not_evaluated";
        if (selected == NONE_ID) {
            if (!cityAdmitsStructure(pass, candidate)) reason = "city_limit";
            else if (!campaignAvailableToBuild(builder,candidate)) reason = "unavailable";
            else if (!findPlaceLocation(candidate).isValid()) reason = "no_site";
            else { selected = candidate; reason = "selected"; }
        }
        evaluated.set(std::to_string(candidate), reason);
    }
    // No-site retries are sampled; successful choices are always recorded.
    if (AITelemetry::log().enabled() && (selected != NONE_ID || !lastZoneTraceCycle.count(builder->getObjectID())
            || getGameCycleCount() - lastZoneTraceCycle[builder->getObjectID()] >= MILLI2CYCLES(30000))) {
        lastZoneTraceCycle[builder->getObjectID()] = getGameCycleCount();
        zoneDecisionIds[builder->getObjectID()] = traceDecision("zone_evaluation", AITelemetry::Record()
            .set("builder", builder->getObjectID()).set("bootstrap", bootstrap)
            .set("rule", "normalized_demand_band_then_committed_balance").set("selected", selected)
            .set("expansion_policy", "demand_led_tax_candidate")
            .set("result", selected != NONE_ID ? "selected"
                : ranked[0] == NONE_ID ? "no_positive_demand" : "no_available_site_or_building")
            .set("state", decisionState(pass)).set("candidates", candidates).set("evaluated", evaluated));
    }
    return selected;
}

bool QuantBot::canBuildMilitaryVehicle(BuildPass& pass, const BuilderBase* factory) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (getHouse()->isGroundUnitLimitReached()) return false;
    for (Uint32 item : {Unit_Tank,Unit_SiegeTank,Unit_Launcher,Unit_Devastator,Unit_SonicTank,Unit_Deviator})
        if (factory->isAvailableToBuild(item) && data[item][houseID].price <= pass.money
            && int64_t(pass.militaryValue) + data[item][houseID].price <= pass.militaryBudget) return true;
    return false;
}

QuantBot::CivicPlan QuantBot::planCivicForYard(BuildPass& pass, const BuilderBase* yard) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    CivicPlan plan;
    if (!pass.citySimEnabled || yard == nullptr || pass.civicStructuralNeed == 0) return plan;
    for (const Uint32 candidate : {Uint32(Structure_Stadium), Uint32(Structure_Airport)}) {
        const uint8_t flag = candidate == Uint32(Structure_Stadium)
            ? uint8_t(DuneCity::NeedStadium) : uint8_t(DuneCity::NeedAirport);
        if (!(pass.civicStructuralNeed & flag)) continue;
        // A mod that disables the civic, or a tech level that has not
        // reached it, is not a cap this house can lift.
        if (!data[candidate][houseID].enabled
            || data[candidate][houseID].techLevel > getHouse()->getTechLevel()) {
            plan.reason = "civic_not_in_mod_or_tech";
            continue;
        }
        plan.civic = candidate;
        break;
    }
    if (plan.civic == NONE_ID) return plan;
    plan.reason = "chain_exhausted";
    Uint32 step = plan.civic;
    for (int depth = 0; depth < Structure_LastID && step != NONE_ID; ++depth) {
        if (!data[step][houseID].enabled || data[step][houseID].techLevel > getHouse()->getTechLevel()) {
            plan.reason = "prerequisite_not_in_mod_or_tech";
            return plan;
        }
        if (pass.itemCount[step] > getHouse()->getNumItems(step)) {
            plan.pending = true;
            plan.reason = step == plan.civic ? "civic_queued" : "prerequisite_queued";
            return plan;
        }
        if (campaignAvailableToBuild(yard, step)) {
            if (!findPlaceLocation(step).isValid()) {
                plan.reason = step == plan.civic ? "civic_no_site" : "prerequisite_no_site";
                return plan;
            }
            // Ordering into a power deficit is not progress: the city would
            // brown out the zones this civic is meant to unlock. Leave the
            // yard to the generator rules and claim it on a later pass.
            if (data[step][houseID].power
                    > getHouse()->getProducedPower() - getHouse()->getPowerRequirement()) {
                plan.reason = "power_headroom";
                return plan;
            }
            plan.order = step;
            plan.reason = step == plan.civic ? "civic" : "prerequisite";
            return plan;
        }
        if (yard->getCurrentUpgradeLevel() < data[step][houseID].upgradeLevel
            && yard->getMaxUpgradeLevel() >= data[step][houseID].upgradeLevel) {
            plan.upgrade = true;
            plan.reason = "yard_upgrade";
            return plan;
        }
        Uint32 missing = NONE_ID;
        bool ordered = false;
        for (int prerequisite = Structure_FirstID; prerequisite <= Structure_LastID; ++prerequisite) {
            if (!data[step][houseID].prerequisiteStructuresSet[prerequisite]
                || getHouse()->getNumItems(prerequisite) > 0) continue;
            // Queue-inclusive: a prerequisite already ordered is waited for,
            // never ordered a second time.
            if (pass.itemCount[prerequisite] > 0) { ordered = true; continue; }
            missing = Uint32(prerequisite);
            break;
        }
        if (missing == NONE_ID && ordered) {
            plan.pending = true;
            plan.reason = "prerequisite_queued";
            return plan;
        }
        step = missing;
    }
    return plan;
}

void QuantBot::forecastSpiceTrip(BuildPass& pass, Coord site) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (pass.refineryTripCycles >= 0 || site.isInvalid()) return;
    int distance = 65;
    Coord field = Coord::Invalid();
    for (int dy=-32;dy<=32;dy+=2) for (int dx=-32;dx<=32;dx+=2) {
        const Coord p(site.x+dx,site.y+dy);
        if (!getMap().tileExists(p.x,p.y) || !getMap().getTile(p.x,p.y)->hasSpice()) continue;
        const int d=std::abs(dx)+std::abs(dy);
        if (d<distance) {distance=d;field=p;}
    }
    pass.refineryFieldRisk=dangerAt(site,getStructureSize(Structure_Refinery))
        + (field.isValid() ? dangerAt(field,Coord(1,1)) : 0);
    const FixPoint speed=data[Unit_Harvester][houseID].maxspeed*0.75_fix;
    pass.walkingTripCycles=speed>0 ? (FixPoint(2*distance*TILESIZE)/speed).lround()
        : CityEconomyInvestmentPolicy::horizonCycles;
    pass.refineryTripCycles=pass.walkingTripCycles;
    const int workers=std::max(1,pass.actualHarvesters);
    const int transported=std::min(workers,getHouse()->getNumItems(Unit_Carryall)*5);
    const FixPoint airSpeed=data[Unit_Carryall][houseID].maxspeed*0.75_fix;
    if (transported>0 && airSpeed>0 && distance>=MIN_CARRYALL_LIFT_DISTANCE) {
        // Two flights plus a conservative pickup/landing allowance. Credit
        // only the share supported by existing aircraft, never queued ones.
        const int flown=(FixPoint(2*distance*TILESIZE)/airSpeed).lround()+MILLI2CYCLES(12000);
        pass.refineryTripCycles=int((int64_t(pass.walkingTripCycles)*(workers-transported)
            + int64_t(std::min(pass.walkingTripCycles,flown))*transported)/workers);
    }
}

int QuantBot::workerAnnualIncome(BuildPass& pass) {
    return HARVESTERMAXSPICE*int(DuneCity::kCyclesPerMinute)
        / std::max(1,pass.fillCycles+pass.unloadCycles+std::max(0,pass.refineryTripCycles));
}

int QuantBot::cityHarvesterReplacementFloor(BuildPass& pass) {
    const auto& force = botConfig().force;
    // Batch3 CityWorkerReplacement applies the City FFA floor to every City
    // economy roster with its own payback horizon; the FFA switch keeps precedence.
    const bool ffa = force.cityHarvesterReplacement && citySpiceFfaScope(4);
    if (!ffa && (force.cityWorkerReplacement == 0 || !cityEconomyExperimentScope())) return -1;
    int lost = getHouse()->getNumLostItems(Unit_Harvester);
    if (ModManager::instance().isTornieFamilyContentActive())
        lost += getHouse()->getNumLostItems(Unit_CapacityHarvester)
            + getHouse()->getNumLostItems(Unit_FastHarvester);
    if (lost == 0 || pass.bayAnnualIncome <= 0 || pass.fillCycles <= 0) return -1;
    // The spending readiness floor is populated only with TechSavings enabled.
    // Existing live-threat and power/civic emergencies always retain priority.
    if (pass.defendingEconomy || pass.groundThreatAsset.isValid() || pass.airEngaged
        || pass.campaignThreatProduction || pass.vanillaFfaThreatProduction
        || (pass.techSavings && spendingEmergency(pass, Unit_Tank))
        || spendingEmergency(pass, Structure_WindTrap)
        || spendingEmergency(pass, Unit_MCV)
        || spendingEmergency(pass, Unit_Carryall)
        || std::any_of(pass.emergencyCivic.begin(), pass.emergencyCivic.end(),
            [](bool emergency) { return emergency; })) return -1;
    if (!pass.cityReplacementEvaluated) {
        pass.cityReplacementEvaluated = true;
        const int64_t spice = citySpiceShare().ownSpice;
        if (spice <= 0) return -1;
        // 200 spice per worker-minute is the measured native FFA winner yield,
        // not the optimistic trip forecast. Use the forecast only for bay capacity.
        const int minutes = std::max(1, ffa ? force.cityHarvesterReplacementMinutes
            : QuantBotBuildPolicy::cityWorkerReplacementMinutes(force.cityWorkerReplacement));
        pass.cityReplacementSpiceCap = int(std::min<int64_t>(INT_MAX,
            spice / (int64_t(200) * minutes)));
        pass.cityReplacementCapacity = int(std::min<int64_t>(INT_MAX,
            int64_t(getAIRefineryBays()) * pass.bayAnnualIncome
                / std::max(1, workerAnnualIncome(pass))));
        int limit = std::max(0, harvesterLimit);
        if (getHouse()->getMaxHarvesters() > 0)
            limit = std::min(limit, getHouse()->getMaxHarvesters());
        pass.cityReplacementFloor = std::min({pass.cityReplacementSpiceCap,
            pass.cityReplacementCapacity, limit});
    }
    return pass.cityReplacementFloor;
}

bool QuantBot::cityHarvesterReplacementFactoryReady(BuildPass& pass) {
    return std::any_of(pass.harvesterFactories.begin(), pass.harvesterFactories.end(),
        [](const BuilderBase* factory) {
            return !factory->isUpgrading() && !factory->isOnHold()
                && factory->getProductionQueueSize() == 0
                && factory->isAvailableToBuild(Unit_Harvester);
        });
}

bool QuantBot::factoryPrefersHarvester(BuildPass& pass, const BuilderBase* factory) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    const int replacement = cityHarvesterReplacementFloor(pass);
    if (replacement >= 0) return pass.itemCount[Unit_Harvester] < replacement;
    if (pass.cityBoomWorkers > 0) return pass.itemCount[Unit_Harvester] < pass.cityBoomWorkers;
    if (pass.cityBoom == CityBoomKind::Reactive) return false;
    if ((pass.campaignThreatProduction || pass.vanillaFfaThreatProduction)
        && canBuildMilitaryVehicle(pass, factory)) return false;
    if (harvesterInvestmentReserve(pass) > 0) return true;
    const int workers=pass.itemCount[Unit_Harvester],refs=pass.itemCount[Structure_Refinery];
    if (QuantBotSpendingPolicy::marginalSpice(pass.spiceShare,
        std::min(workers*workerAnnualIncome(pass),refs*pass.bayAnnualIncome),
        std::min((workers+1)*workerAnnualIncome(pass),refs*pass.bayAnnualIncome),
        data[Unit_Harvester][houseID].buildtime*15,CityEconomyInvestmentPolicy::horizonCycles,
        DuneCity::kCyclesPerMinute)<=data[Unit_Harvester][houseID].price) return false;
    return CityEconomyInvestmentPolicy::preferFactoryHarvester(pass.itemCount[Unit_Harvester],
        pass.citySimEnabled ? pass.fundedHarvesterTarget : pass.spiceHarvesterTarget,pass.militaryValue,pass.militaryBudget,
        data[Unit_Harvester][houseID].price,canBuildMilitaryVehicle(pass, factory),
        pass.openingWorkerFloor,pass.brutalEconomy);
}

Uint32 QuantBot::chooseCityEconomy(BuildPass& pass, const BuilderBase* builder, bool opening) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    using namespace CityEconomyInvestmentPolicy;
    pass.lastEconomyInvestment = {};
    pass.cityRefineryCatchup = false;
    pass.cityRefineryOpening = false;
    const Uint32 zone = chooseCityZone(pass, builder, opening);
    const auto* sim = currentGame->getCitySimulation();
    if (!sim) return zone;
    const int tax = sim->getCityTax(getHouse()->getHouseID());
    const int land = pass.ownAvgLandValue>0 ? pass.ownAvgLandValue : 128;
    const int workers = pass.itemCount[Unit_Harvester], refs = pass.itemCount[Structure_Refinery];
    const bool expandingOpening = CityEconomyInvestmentPolicy::openingWorkersNeeded(workers,pass.fundedHarvesterTarget,pass.brutalCityEconomy)
        && pass.brutalCityEconomy;
    const bool openingRefinery = openingRefineryInvestment(pass.brutalCityEconomy,workers,pass.fundedHarvesterTarget,refs);
    const bool wantedWorker = workers < std::min(pass.spiceHarvesterTarget,harvesterLimit);
    bool capacityNeeded = pass.unloadingBacklog, refineryUseful = false;
    int workerIncome = 0, bayIncome = 0;
    bool hedge = zone == Structure_ZoneResidential && pass.itemCount[zone] == 0;
    Investment residential, refinery;
    auto setupInvestment = [&](Uint32 item, Coord site, int power) {
        const Coord size = getStructureSize(item);
        int foundation = 0;
        // Only a game that requires concrete pays for the missing tiles.
        if (getGameInitSettings().getGameOptions().concreteRequired)
            for (int y=site.y;y<site.y+size.y;++y) for (int x=site.x;x<site.x+size.x;++x) {
                if (!getMap().tileExists(x,y)) continue;
                const auto* tile = getMap().getTile(x,y);
                foundation += !tile->hasPreparedFoundation();
            }
        // Amortize available generation (including foundation) even with spare power.
        Investment result;
        result.cost = data[item][houseID].price + foundation*data[Structure_Slab1][houseID].price
            + (power*pass.generationCostPerThousand+999)/1000;
        // Roads have no recurring cost; allow for separate power upkeep.
        result.annualUpkeep = power/8;
        return result;
    };
    if (zone != NONE_ID) {
        const Coord site = findPlaceLocation(zone);
        const bool industry = zone == Structure_ZoneIndustrial;
        const int pollution = sim->getPollutionDensityMap().worldGet(site.x,site.y);
        const int crime = sim->getCrimeRateMap().worldGet(site.x,site.y);
        const int localValue = sim->getLandValueMap().worldGet(site.x,site.y);
        // Forecast low density on ordinary land, medium on good clean land.
        // Do not value a newly zoned plot as an instant high-density tower.
        const int level = localValue>=128 && pollution<=DuneCity::kPollutionGrowthThreshold && crime<128 ? 2 : 1;
        const int population = DuneCity::getZonePopulation(zone,level);
        const int power = DuneCity::getZonePower(zone,level);
        const int unfinished = pass.developingZones[zone-Structure_ZoneResidential]
            + std::max(0,pass.itemCount[zone]-getHouse()->getNumItems(zone));
        const int demand = zone == Structure_ZoneResidential ? pass.ownResValve : industry ? pass.ownIndValve : pass.ownComValve;
        residential = setupInvestment(zone,site,power);
        residential.annualIncome = DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(zone,population,level),tax,land));
        if (zone != Structure_ZoneResidential) {
            // One C/I job supports eight residents. Credit only half the
            // tax of existing/pending housing currently short of jobs.
            const int waitingR = pass.developingZones[0] + std::max(0,pass.itemCount[Structure_ZoneResidential]
                - getHouse()->getNumItems(Structure_ZoneResidential));
            const int shortage = std::max(0,pass.ownResPop+waitingR*16-(pass.ownComPop+pass.ownIndPop)*8);
            residential.annualIncome += DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(
                Structure_ZoneResidential,std::min(shortage,population*8),0),tax,land)/2);
        }
        residential.delayCycles = DuneCity::getCityBuildTime(zone,data[zone][houseID].buildtime)*15
            + DuneCity::cyclesPerCityYear(); // construction plus a city year of growth/foundation allowance
        residential.confidence = zoneConfidence(demand,zone==Structure_ZoneResidential ? 2000 : 1500,
            industry ? 0 : pollution,crime,unfinished);
    }
    Coord refinerySite = Coord::Invalid();
    // A fixed fleet ratio refuses the order at its target; never select it then.
    if ((!pass.citySpiceTransition || pass.unloadingBacklog)
        && (pass.spiceShare>0 || pass.unloadingBacklog) && campaignAvailableToBuild(builder,Structure_Refinery)
        && (!pass.fleetRatios || refs < pass.fleetRefineryTarget))
        refinerySite = findPlaceLocation(Structure_Refinery);
    Coord forecastSite=refinerySite;
    if(forecastSite.isInvalid()) for(const auto* structure:getStructureList())
        if(structure->getOwner()==getHouse()&&structure->getItemID()==Structure_Refinery){forecastSite=structure->getLocation();break;}
    if (forecastSite.isValid()) {
        forecastSpiceTrip(pass, forecastSite);
        const int roundTrip=pass.fillCycles+pass.unloadCycles+std::max(0,pass.refineryTripCycles);
        workerIncome=workerAnnualIncome(pass);
        bayIncome=pass.bayAnnualIncome;
        capacityNeeded = pass.unloadingBacklog || processingCapacityNeeded(refs,workers,workerIncome,bayIncome);
        if(refinerySite.isValid()) {
            refineryUseful = considerRefinery(capacityNeeded,wantedWorker,!pass.harvesterFactories.empty(),workers < 2 || openingRefinery);
            const bool freeWorker = workers < harvesterLimit;
            const int power = std::max(0,data[Structure_Refinery][houseID].power);
            refinery = setupInvestment(Structure_Refinery,refinerySite,power);
            refinery.annualIncome = marginalSpiceIncome(workers,refs,freeWorker,workerIncome,bayIncome);
            const int refineryBuildCycles = data[Structure_Refinery][houseID].buildtime*15;
            refinery.delayCycles = refineryBuildCycles + roundTrip;
            refinery.projectedProceeds = refineryProceeds(workers,refs,freeWorker,workerIncome,bayIncome,
                refineryBuildCycles,roundTrip,pass.unloadCycles,HARVESTERMAXSPICE,refinery.annualUpkeep);
            const int before=std::min(workers*workerIncome,refs*bayIncome);
            const int after=std::min((workers+int(freeWorker))*workerIncome,(refs+1)*bayIncome);
            const int finiteReturn=QuantBotSpendingPolicy::marginalSpice(pass.spiceShare,before,after,
                refineryBuildCycles,horizonCycles,DuneCity::kCyclesPerMinute);
            refinery.projectedProceeds=std::min(refinery.projectedProceeds,
                std::max(0,finiteReturn-refinery.annualUpkeep*4));
            if (pass.refineryFieldRisk>0) refinery.confidence/=2;
            if(pass.unloadingBacklog) {
                refinery.projectedProceeds=std::max(refinery.projectedProceeds,
                    std::min(pass.waitingCargo,2*int(HARVESTERMAXSPICE))-refinery.annualUpkeep*4);
                refinery.confidence=1000; // Already harvested cargo, no remaining-spice risk.
            }
        }
    }
    const int taxIncome = DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(pass.ownTaxBaseEighths,tax,land));
    int developingIncome = 0;
    for (Uint32 kind : {Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial}) {
        const int pending = pass.developingZones[kind-Structure_ZoneResidential]
            + std::max(0,pass.itemCount[kind]-getHouse()->getNumItems(kind));
        const int demand = kind==Structure_ZoneResidential ? pass.ownResValve
            : kind==Structure_ZoneCommercial ? pass.ownComValve : pass.ownIndValve;
        if (demand > 0) developingIncome += pending * DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(
            DuneCity::taxablePopulationEighths(kind,DuneCity::getZonePopulation(kind,1),1),tax,land)/2);
    }
    const int fleetIncome = workers * workerIncome;
    // Positive demand and marginal return decide growth, independently of
    // the current tax/spice ratio. Only the very first housing seed is special.
    Uint32 selected = preferRefinery(refinery,residential,refineryUseful,hedge,capacityNeeded) ? Structure_Refinery : zone;
    pass.cityRefineryCatchup = selected == Structure_Refinery && capacityNeeded;
    pass.cityRefineryOpening = selected == Structure_Refinery && openingRefinery;
    if (pass.cityBoomBays > 0) {
        if (refs >= pass.cityBoomBays) selected = zone;
        else if (pass.cityBoomFunding && pass.cityBoom != CityBoomKind::ShortTax && refinerySite.isValid())
            selected = Structure_Refinery;
        if (pass.cityBoom == CityBoomKind::ShortTax && refs >= 2 && zone != NONE_ID) selected = zone;
        pass.cityRefineryCatchup = selected == Structure_Refinery && capacityNeeded;
        pass.cityRefineryOpening = selected == Structure_Refinery && openingFleetIncomplete(pass);
    }
    if (selected == zone && zone != NONE_ID && residential.confidence == 0) selected = NONE_ID;
    if (pass.cityBoomArmy && DuneCity::isCityZoneStructure(selected)) selected = NONE_ID;
    pass.lastEconomyInvestment=selected==Structure_Refinery ? refinery : selected==zone ? residential : Investment{};
    // Save for a winning refinery instead of spending its money on another
    // cheap lot every pass. The caller preserves urgent non-economic work.
    const bool funded = selected!=NONE_ID && pass.money>=data[selected][houseID].price;
    if (AITelemetry::log().enabled()) {
        lastEconomyTraceCycle[builder->getObjectID()]=getGameCycleCount();
        auto describe = [](const Investment& i) { return AITelemetry::Record().set("cost",i.cost)
            .set("annual_income",i.annualIncome).set("annual_upkeep",i.annualUpkeep)
            .set("delay_cycles",i.delayCycles).set("confidence_per_mille",i.confidence).set("proceeds",i.proceeds()); };
        traceDecision("city_economy_comparison",AITelemetry::Record().set("builder",builder->getObjectID())
            .set("selected",selected).set("zone",zone).set("hedge",hedge).set("funded",funded)
            .set("capacity_needed",capacityNeeded).set("refinery_useful",refineryUseful)
            .set("unloading_backlog",pass.unloadingBacklog).set("waiting_to_unload",pass.waitingHarvesters).set("waiting_cargo",pass.waitingCargo)
            .set("refinery_site_valid",refinerySite.isValid())
            .set("refinery_available",campaignAvailableToBuild(builder,Structure_Refinery))
            .set("refinery_placement",placementScoreDetails[Structure_Refinery])
            .set("brutal_opening",expandingOpening).set("opening_refinery",openingRefinery)
            .set("wanted_included_worker",wantedWorker)
            .set("worker_income",workerIncome).set("bay_capacity",bayIncome)
            .set("generation_cost_per_thousand",pass.generationCostPerThousand)
            .set("workers",workers).set("refineries",refs)
            .set("factory_can_supply",!pass.harvesterFactories.empty())
            .set("tax_income",taxIncome).set("developing_tax_income",developingIncome)
            .set("forecast_fleet_income",fleetIncome)
            .set("sustainable_workers",pass.spiceHarvesterTarget).set("horizon_cycles",horizonCycles)
            .set("spice_share",pass.spiceShare).set("trip_cycles",pass.refineryTripCycles).set("field_risk",pass.refineryFieldRisk)
            .set("tax_candidate",describe(residential)).set("refinery_candidate",describe(refinery)));
    }
    return selected;
}

void QuantBot::traceBuildState(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    pass.emitStatsLog = false;

    if (!builderOnly() && (pass.militaryValue > 0 || getHouse()->getNumStructures() > 0)) {
        const Uint32 currentCycle = getGameCycleCount();
        if(currentCycle - lastStatsLogCycle >= MILLI2CYCLES(30000)) {
            pass.emitStatsLog = true;
            if (gameMode == GameMode::Custom) {
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d  built: %d  kill: %d  loss: %d remaining spice: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), pass.militaryValue, militaryValueLimit, getHouse()->getUnitBuiltValue(),
                    getHouse()->getKillValue(), getHouse()->getLossValue(), lastCalculatedSpice, getAIHarvesterCount(), harvesterLimit);
            } else {
                // Campaign mode - include initial military value and multiplier
                const QuantBotConfig& config = botConfig();
                const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d (init: %d, mult: %.1fx)  built: %d  kill: %d  loss: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), pass.militaryValue, militaryValueLimit, initialMilitaryValue, diffSettings.militaryValueMultiplier,
                    getHouse()->getUnitBuiltValue(), getHouse()->getKillValue(), getHouse()->getLossValue(), getAIHarvesterCount(), harvesterLimit);
            }
            lastStatsLogCycle = currentCycle;
        }
    }


    if (AITelemetry::log().enabled() && (telemetryState == 0
            || getGameCycleCount() - lastTelemetrySnapshotCycle >= MILLI2CYCLES(30000))) {
        lastTelemetrySnapshotCycle = getGameCycleCount();
        AITelemetry::Record actual, queued;
        for (int i = ItemID_FirstID; i <= ItemID_LastID; ++i) {
            const int count = getHouse()->getNumItems(i);
            if (count) actual.set(std::to_string(i), count);
            if (pass.itemCount[i] != count) queued.set(std::to_string(i), pass.itemCount[i] - count);
        }
        AITelemetry::Record harvesters;
        for (const UnitBase* unit : getUnitList()) {
            if (unit->getOwner() != getHouse()) continue;
            if (const auto* harvester = dynamic_cast<const Harvester*>(unit)) {
                const auto pos = unit->getLocation(), dest = unit->getDestination();
                harvesters.set(std::to_string(unit->getObjectID()), AITelemetry::Record()
                    .set("x", pos.x).set("y", pos.y).set("destination_x", dest.x).set("destination_y", dest.y)
                    .set("active", unit->isActive()).set("harvesting", harvester->isHarvesting())
                    .set("returning", harvester->isReturning()).set("cargo", harvester->getAmountOfSpice().lround())
                    .set("health", unit->getHealth().lround()).set("mode", unit->getAttackMode()));
            }
        }
        AITelemetry::Record cityHealth;
        if (pass.citySimEnabled && currentGame->getCitySimulation()) {
            auto* sim = currentGame->getCitySimulation();
            const auto& hs = sim->getHouseState(getHouse()->getHouseID());
            int samples = 0, pollution = 0, crime = 0, traffic = 0;
            int resCrime = 0, comCrime = 0, extremeCrime = 0, slowPollution = 0, blockedPollution = 0;
            // Keep telemetry in the same named bands shown to players and
            // defined by Micropolis's crime overlay.
            int crimeBands[4] = {};
            for (const StructureBase* structure : getStructureList()) {
                if (structure->getOwner() != getHouse()) continue;
                const Uint32 type = structure->getItemID();
                if (type < Structure_ZoneResidential || type > Structure_ZoneIndustrial) continue;
                const Coord p = structure->getLocation();
                ++samples;
                pollution += sim->getPollutionDensityMap().worldGet(p.x, p.y);
                const int localCrime = sim->getCrimeRateMap().worldGet(p.x, p.y);
                const int localPollution = sim->getPollutionDensityMap().worldGet(p.x, p.y);
                crime += localCrime;
                ++crimeBands[localCrime < 64 ? 0 : localCrime < 128 ? 1 : localCrime < 192 ? 2 : 3];
                resCrime += type == Structure_ZoneResidential && localCrime > 100;
                comCrime += type == Structure_ZoneCommercial && localCrime > 80;
                extremeCrime += localCrime > 190;
                if (type != Structure_ZoneIndustrial) {
                    slowPollution += localPollution >= 80;
                    blockedPollution += localPollution >= 160;
                }
                traffic += sim->getTrafficDensityMap().worldGet(p.x, p.y);
            }
            cityHealth.set("unemployment", hs.unemploymentRate).set("tax_rate", sim->getCityTax(getHouse()->getHouseID()))
                .set("last_tax_revenue", hs.budget.getLastTaxRevenue()).set("police_expense", hs.lastPoliceExpense)
                .set("tax_base_eighths", hs.taxBaseEighths)
                .set("road_expense", 0)
                .set("zone_origin_samples", samples).set("pollution_mean", samples ? pollution / samples : 0)
                .set("crime_mean", samples ? crime / samples : 0).set("traffic_mean", samples ? traffic / samples : 0)
                .set("res_crime_above100",resCrime).set("com_crime_above80",comCrime).set("crime_above190",extremeCrime)
                .set("res_com_pollution_atleast80",slowPollution).set("res_com_pollution_atleast160",blockedPollution)
                .set("crime_bands", AITelemetry::Record().set("safe_0_63",crimeBands[0])
                    .set("light_64_127",crimeBands[1]).set("moderate_128_191",crimeBands[2])
                    .set("dangerous_192_250",crimeBands[3]));
            if (telemetryState == 0 || getGameCycleCount()-lastCityBuildingSnapshotCycle >= MILLI2CYCLES(120000)) {
                lastCityBuildingSnapshotCycle = getGameCycleCount();
                AITelemetry::Record buildings;
                int count = 0;
                for (const auto* structure : getStructureList()) {
                    if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
                    const auto item = structure->getItemID();
                    const auto pos = structure->getLocation(), size = structure->getStructureSize();
                    const auto* tile = getMap().getTile(pos.x,pos.y);
                    if (!tile) continue;
                    const auto role = DuneCity::getStructureCityRole(item);
                    const int level = DuneCity::isCityZoneStructure(item) ? tile->getCityZoneDensity()
                        : role == DuneCity::CityRole::None ? 0 : std::max(1,int(structure->getCityOccupancy()));
                    int generation = 0;
                    if (const auto* reactor = dynamic_cast<const NuclearPlant*>(structure)) generation = reactor->getProducedPower();
                    else if (const auto* windtrap = dynamic_cast<const WindTrap*>(structure)) generation = windtrap->getProducedPower();
                    buildings.set(std::to_string(structure->getObjectID()), AITelemetry::Record()
                        .set("item",item).set("x",pos.x).set("y",pos.y).set("width",size.x).set("height",size.y)
                        .set("health",structure->getHealth().lround()).set("max_health",structure->getMaxHealth())
                        .set("role",static_cast<int>(role)).set("level",level).set("max_level",DuneCity::getStructureMaxLevel(item))
                        .set("population",DuneCity::getStructurePopulation(structure,level))
                        .set("res_supply",DuneCity::getStructureResidentialSupply(structure,level))
                        .set("com_supply",DuneCity::getCommercialSupply(item,level)).set("ind_supply",DuneCity::getIndustrialSupply(item,level))
                        .set("palace_com_population",item == Structure_Palace ? DuneCity::getPalaceCommercialPopulation(level) : 0)
                        .set("land_value",sim->getLandValueMap().worldGet(pos.x,pos.y))
                        .set("population_density",sim->getPopulationDensityMap().worldGet(pos.x,pos.y))
                        .set("pollution",sim->getPollutionDensityMap().worldGet(pos.x,pos.y))
                        .set("crime",sim->getCrimeRateMap().worldGet(pos.x,pos.y))
                        .set("crime_before_police",sim->getCrimeBeforePoliceMap().worldGet(pos.x,pos.y))
                        .set("police_coverage",sim->getPoliceCoverageMap().worldGet(pos.x,pos.y))
                        .set("traffic_density",sim->getTrafficDensityMap().worldGet(pos.x,pos.y))
                        .set("growth_rate",sim->getGrowthRateMap().worldGet(pos.x,pos.y))
                        .set("pollution_emission",DuneCity::getPollutionEmission(item,level))
                        .set("land_value_bonus",DuneCity::getParkLandValueBonus(item))
                        .set("police_strength",DuneCity::getPoliceCoverage(item)).set("police_cost",DuneCity::getPoliceAnnualCost(item).lround()).set("police_cost_milli",(DuneCity::getPoliceAnnualCost(item)*1000).lround())
                        .set("power_nominal",currentGame->objectData.data[item][structure->getProductionHouseID()].power)
                        .set("power_generated",generation));
                    ++count;
                }
                traceDecision("city_building_snapshot", AITelemetry::Record().set("buildings",buildings).set("count",count)
                    .set("police_funding",hs.policeFundingPercent).set("state",decisionState(pass)));
            }
        }

        int actualGeneratorPower = 0;
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse()) continue;
            if (const auto* wind = dynamic_cast<const WindTrap*>(structure)) actualGeneratorPower += wind->getProducedPower();
            else if (const auto* nuclear = dynamic_cast<const NuclearPlant*>(structure)) actualGeneratorPower += nuclear->getProducedPower();
            else if (const auto* advanced = dynamic_cast<const AdvancedWindTrap*>(structure)) actualGeneratorPower += advanced->getProducedPower();
            else if (const auto* scout = dynamic_cast<const Scoutpost*>(structure)) actualGeneratorPower += scout->getProducedPower();
        }
        // Observer-only comparison data, never fed back into AI decisions.
        AITelemetry::Record opponents;
        for (int h = 0; h < NUM_HOUSES; ++h) {
            const auto* house = currentGame->getHouse(h);
            if (!house || house->isNatives()) continue;  // no opponent: worms and crime troopers only
            int survivingMilitary = 0;
            for (Uint32 type=Unit_FirstID; type<=Unit_LastID; ++type)
                if (type != Unit_Carryall && type != Unit_Harvester && type != Unit_MCV && type != Unit_Sandworm)
                    survivingMilitary += house->getNumItems(type) * currentGame->objectData.data[type][h].price;
            opponents.set(std::to_string(h), AITelemetry::Record()
                .set("team", house->getTeamID()).set("alive", house->isAlive())
                .set("credits", house->getCredits()).set("military", survivingMilitary).set("military_basis", "current_unit_counts")
                .set("harvesters", house->getNumItems(Unit_Harvester))
                .set("refineries", house->getNumItems(Structure_Refinery))
                .set("heavy_factories", house->getNumItems(Structure_HeavyFactory))
                .set("construction_yards", house->getNumItems(Structure_ConstructionYard))
                .set("economy_totals", AITelemetry::log().economyTotals(h)).set("combat_rewards", house->combatRewardStats(currentGame->objectData))
                .set("deviation_rewards", house->deviationRewardStats()));
        }
        telemetryState = AITelemetry::log().write(getGameCycleCount(), getHouse()->getHouseID(), getPlayerID(), "state_snapshot",
            AITelemetry::Record().set("ai", "QuantBot").set("name", getPlayername())
                .set("difficulty", static_cast<int>(difficulty)).set("support", builderOnly())
                .set("house_name", getHouseNameByNumber(static_cast<HOUSETYPE>(getHouse()->getHouseID())))
                .set("team", getHouse()->getTeamID())
                .set("state", decisionState(pass)).set("city_health", cityHealth).set("actual", actual).set("queued", queued)
                .set("harvesters", harvesters).set("refinery_queues",pass.refineryQueues).set("house_comparison", opponents)
                .set("power_accounting", AITelemetry::Record().set("generators", actualGeneratorPower)
                    .set("reported", getHouse()->getProducedPower())
                    .set("difference", getHouse()->getProducedPower() - actualGeneratorPower))
                .set("economy_totals", AITelemetry::log().economyTotals(houseID))
                .set("economy", AITelemetry::Record()
                    .set("harvested_spice_total", getHouse()->getHarvestedSpice().lround())
                    .set("stored_spice_credits", getHouse()->getStoredCredits().lround())
                    .set("city_credit_balance", getHouse()->getCityCredits().lround())
                    .set("starting_credit_balance", getHouse()->getStartingCredits().lround()))
                .set("heavy_busy", pass.activeHeavyFactoryCount).set("light_busy",pass.activeLightFactoryCount).set("repair_busy", pass.observedBusyRepairBays)
                .set("built_value", getHouse()->getUnitBuiltValue()).set("kill_value", getHouse()->getKillValue())
                .set("loss_value", getHouse()->getLossValue()));
        if (pass.cityBoom == CityBoomKind::Reactive) {
            const auto& survey = cityBoomSurvey();
            traceDecision("city_boom_reactive", AITelemetry::Record()
                .set("funding", survey.funding).set("threatened", survey.threatened)
                .set("own_army", survey.ownArmy).set("strongest_reachable_army", survey.strongestArmy)
                .set("own_nearby_army", survey.ownNearbyArmy).set("strongest_nearby_army", survey.nearbyArmy)
                .set("own_economy", survey.ownEconomy).set("enemy_economy_leader", survey.leaderEconomy)
                .set("army_ratio_bps", botConfig().force.cityBoomArmyRatioBps)
                .set("economy_ratio_bps", botConfig().force.cityBoomEconomyRatioBps)
                .set("target_team", survey.targetTeam));
        }
    }
}

void QuantBot::planUnitMix(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    pass.trooperSupport = gameMode == GameMode::Custom && botConfig().force.trooperSupport
        && ModManager::instance().isDuneCityContentActive();
    // Infantry retains its separate difficulty quota. Vehicle openings use
    // the current technology and actual production availability below.
    pass.infantryPercent = 10;
    switch (difficulty) {
        case Difficulty::Defend:
        case Difficulty::Easy:
            pass.infantryPercent = 18;
            break;
        case Difficulty::Medium:
            pass.infantryPercent = 15;
            break;
        case Difficulty::Hard:
            pass.infantryPercent = 12;
            break;
        case Difficulty::Brutal:
            break;
    }
    // An Infantry mix strategy fields its own share on foot (docs/ai-play-styles.md, "Strategies").
    pass.strategyMixProfile = strategyMix();
    if (pass.strategyMixProfile && pass.strategyMixProfile->infantryPercent > 0) pass.infantryPercent = pass.strategyMixProfile->infantryPercent;

    constexpr auto& mixItems = kMixItems;
    constexpr size_t kSlots = UnitMixPolicy::kSlots;
    constexpr size_t kOrnithopterSlot = UnitMixPolicy::kOrnithopterSlot;
    UnitMixPolicy::Weights damage{}, rewardMilli{}, killBonusMilli{}, lostValue{}, scores{}, defaults{}, prices{}, lossMilli{};
    UnitMixPolicy::Slots available{};
    bool launcherProductionAvailable = false;
    for (const auto* structure : getStructureList()) {
        const auto* builder = dynamic_cast<const BuilderBase*>(structure);
        if (!builder || builder->getOwner() != getHouse()
            || (builder->getItemID() != Structure_LightFactory && builder->getItemID() != Structure_HeavyFactory
                && builder->getItemID() != Structure_HighTechFactory
                && !(pass.techSavings && builder->getItemID() == Structure_StarPort))) continue;
        for (size_t i=0; i<kSlots; ++i) {
            const bool producible = campaignAvailableToBuild(builder,mixItems[i]);
            available[i] |= producible;
            if ((pass.techSavings || AITelemetry::log().enabled()) && producible
                && builder->getHealth() > 0 && builder->isActive()
                && !getHouse()->isUnitLimitReached(mixItems[i])
                && (builder->getItemID() != Structure_StarPort
                    || (getHouse()->getChoam().getNumAvailable(mixItems[i]) > 0
                        && purchasePrice(builder, mixItems[i]) < data[mixItems[i]][houseID].price)))
                pass.compositionAvailable[i] = true;
            if (i == 2 && builder->getHealth() > 0 && builder->isActive())
                launcherProductionAvailable |= producible;
        }
    }
    // Losing a producer is a rebuilding need, not evidence that its units
    // should disappear from the strategic army mix. Use saved house history
    // so this also survives loading a game after the factory was destroyed.
    // Actual production still uses each factory's normal build/upgrade gates.
    if (getHouse()->getNumLostItems(Structure_HeavyFactory) > 0) {
        auto recoverableHeavyUnit = [&](Uint32 unit) {
            const auto& spec = data[unit][houseID];
            if (!spec.enabled || spec.price <= 0 || spec.techLevel > getHouse()->getTechLevel()
                || spec.builder != Structure_HeavyFactory) return false;
            for (int prerequisite = Structure_FirstID; prerequisite <= Structure_LastID; ++prerequisite)
                if (spec.prerequisiteStructuresSet[prerequisite]
                    && getHouse()->getNumItems(prerequisite) == 0
                    && getHouse()->getNumLostItems(prerequisite) == 0) return false;
            return true;
        };
        for (size_t i = 0; i < UnitMixPolicy::kOrnithopterSlot; ++i) available[i] |= recoverableHeavyUnit(mixItems[i]);
    }
    if (getHouse()->isAirUnitLimitReached()) available[kOrnithopterSlot]=false;
    // The starting mix (QuantBot::startingPrior): the configured opening or the strategy's prior in
    // its place, within the starting-mix bounds; the learning below blends combat evidence into it.
    defaults = startingPrior(available);
    int64_t totalDamage = 0, totalRewardMilli = 0, totalLostValue = 0;
    for (size_t i=0; i<kSlots; ++i) {
        const Uint32 item = mixItems[i];
        // Each special keeps the special group's former prior price, so a house with one special
        // learns exactly as it did when the three shared one slot.
        const bool special = i >= UnitMixPolicy::kFirstSpecialSlot && i < kOrnithopterSlot;
        int priorPrice = special ? 700 : std::max(1, data[item][houseID].price);
        damage[i] = std::max(0, getHouse()->getNumItemDamageInflicted(item));
        rewardMilli[i] = getHouse()->getCombatReward(item).total();
        killBonusMilli[i] = getHouse()->getCombatReward(item).killBonusMilli;
        lostValue[i] = int64_t(getHouse()->getNumLostItems(item)) * data[item][houseID].price;
        const bool enabled = data[item][houseID].enabled && data[item][houseID].price > 0
            && data[item][houseID].techLevel <= getHouse()->getTechLevel();
        totalDamage += damage[i];
        totalRewardMilli += rewardMilli[i];
        totalLostValue += lostValue[i];
        prices[i] = priorPrice * 1000;
        lossMilli[i] = lostValue[i] * 1000;
        if (!enabled || !available[i]) { available[i] = false; continue; }
        scores[i] = UnitMixPolicy::performanceScore(rewardMilli[i], lostValue[i]*1000, priorPrice*1000);
    }
    // A test style may hold the starting mix all game (the "learning off" arm; inert in real games).
    pass.learningUnitMix = totalRewardMilli >= 3000000 && style().learning;
    performanceHistory.update(getGameCycleCount(),rewardMilli,lossMilli);
    if (pass.learningUnitMix) {
        for (size_t i=0;i<kSlots;++i) scores[i]=available[i]
            ? UnitMixPolicy::performanceScore(performanceHistory.reward[i],performanceHistory.loss[i],prices[i]) : 0;
        scores=UnitMixPolicy::exploredScores(scores,performanceHistory.reward,performanceHistory.loss,prices,available);
    }
    const auto allocationWeights = UnitMixPolicy::sharpenScores(scores);
    const auto rawMix = UnitMixPolicy::normalize(allocationWeights);
    const int performanceConfidenceBps = UnitMixPolicy::evidenceConfidenceBps(totalLostValue, pass.militaryValue);
    // The strategy's hold scales the fielded army the evidence is weighed against (100 = as is).
    const int64_t priorArmyValue = pass.strategyMixProfile
        ? int64_t(pass.militaryValue) * pass.strategyMixProfile->holdPercent / 100 : int64_t(pass.militaryValue);
    // Funding a powered classic economy must not change its existing learning model.
    const bool evidenceWeightedLearning = !pass.citySimEnabled && !pass.powerRules;
    pass.unitMix = UnitMixPolicy::allocate(scores, defaults, pass.learningUnitMix, evidenceWeightedLearning,
                                                  totalLostValue, priorArmyValue);
    // Test-only play styles (docs/ai-play-styles.md): inert in every real game.
    pass.unitMix = styledUnitMix(pass.unitMix, available);
    // VanillaArmyCounters: re-weight the buildable part of the mix toward the
    // live army of the strongest reachable hostile team. The anti-air floor below still
    // protects its minimum share afterwards. Initial map spice (not current spice) keeps
    // an initially spiceless map inert, and VanillaArmyCounterMapScope thin-spice maps.
    if (botConfig().force.vanillaArmyCounters && QuantBotArmyCounterPolicy::active(vanillaFfaTuningGame(),
            currentGame->getGameInitSettings().getInitialSpiceTilePermille()) && !armyCountersMapExcluded())
        pass.unitMix = vanillaArmyCounterMix(pass, pass.unitMix);
    // Damage-based learning does not value air deterrence, so protect a minimum
    // mobile anti-air share while a visible hostile wing is active.
    // The floor only exists while hostile aircraft are genuinely visible and
    // launchers are genuinely producible, so it cannot become a standing tax.
    const int observedHostileAircraft = (autonomousCustomArmy() || pass.trooperSupport)
        ? visibleHostileAircraft() : 0;
    // Only DuneCity's enabled tracking rockets answer moving aircraft.
    pass.hostileAircraft = getGameInitSettings().getGameOptions().trooperAntiAir
        && ModManager::instance().isDuneCityContentActive() ? observedHostileAircraft : 0;
    if (pass.cityBoom == CityBoomKind::Greedy && pass.cityBoomFunding && observedHostileAircraft == 0)
        pass.trooperSupport = false;
    const bool launcherFloorAuthorised = observedHostileAircraft > 0 && available[2]
        && launcherProductionAvailable;
    const int launcherBpsBeforeFloor = pass.unitMix[2];
    const int launcherFloorBps = launcherFloorAuthorised
        ? UnitMixPolicy::kMobileAntiAirFloorBps : 0;
    if (launcherFloorAuthorised)
        pass.unitMix = UnitMixPolicy::applyFloor(pass.unitMix, 2, launcherFloorBps);
    const bool launcherFloorApplied = pass.unitMix[2] > launcherBpsBeforeFloor;
    // Troopers supplement the launcher share and the rocket turrets a city already
    // has; they are the close-range part of that answer, not the whole of it. The
    // demand is the same observation that protects the launcher share - reused here,
    // so no factory pass performs another army or map scan - and it adds no budget of
    // its own: the infantry quota, the city's working reserve and every build
    // prerequisite below still decide whether anything is actually ordered.
    pass.cityAntiAirInfantryDemand = pass.citySimEnabled && observedHostileAircraft > 0;
    lastUnitMixBps = pass.unitMix;
    // Observational record of exactly this pass: the same scores and the same
    // intended shares the orders below are placed from. Reading it cannot move a
    // unit, and nothing here recomputes anything - it only copies.
    for (size_t i = 0; i < kSlots; ++i) {
        allocationSnapshot.slots[i] = {mixItems[i], scores[i], pass.unitMix[i], available[i]};
    }
    allocationSnapshot.ready = true;
    allocationSnapshot.cycle = getGameCycleCount();
    allocationSnapshot.learning = pass.learningUnitMix;
    allocationSnapshot.militaryValue = pass.militaryValue;
    allocationSnapshot.antiAirFloorBps = launcherFloorAuthorised ? launcherFloorBps : 0;
    allocationSnapshot.infantryQuotaBps = pass.infantryPercent * 100;
    const int rawOrnithopterBps = rawMix[kOrnithopterSlot];
    pass.tankPercent = FixPoint(pass.unitMix[0])/10000;
    pass.siegePercent = FixPoint(pass.unitMix[1])/10000;
    pass.launcherPercent = FixPoint(pass.unitMix[2])/10000;
    const int specialBps = pass.unitMix[3] + pass.unitMix[4] + pass.unitMix[5];
    pass.specialPercent = FixPoint(specialBps)/10000;
    pass.ornithopterPercent = FixPoint(pass.unitMix[kOrnithopterSlot])/10000;
    pass.lightVehicleBps = pass.unitMix[7] + pass.unitMix[8] + pass.unitMix[9];
    const int lightVehiclePercent = pass.lightVehicleBps / 100;

    pass.lightVehicleValue = data[Unit_Trike][houseID].price * pass.itemCount[Unit_Trike]
        + data[Unit_RaiderTrike][houseID].price * pass.itemCount[Unit_RaiderTrike]
        + data[Unit_Quad][houseID].price * pass.itemCount[Unit_Quad];
    pass.infantryValue = data[Unit_Soldier][houseID].price * pass.itemCount[Unit_Soldier]
        + data[Unit_Infantry][houseID].price * pass.itemCount[Unit_Infantry]
        + data[Unit_Trooper][houseID].price * pass.itemCount[Unit_Trooper]
        + data[Unit_Troopers][houseID].price * pass.itemCount[Unit_Troopers];
    pass.lightVehicleCount = pass.itemCount[Unit_Trike] + pass.itemCount[Unit_RaiderTrike] + pass.itemCount[Unit_Quad];
    pass.infantryCount = pass.itemCount[Unit_Soldier] + pass.itemCount[Unit_Infantry]
        + pass.itemCount[Unit_Trooper] + pass.itemCount[Unit_Troopers];
    if (ModManager::instance().isTornieFamilyContentActive()) {
        for (const Uint32 item : {Unit_Troopers5, Unit_Infantry5, Unit_GasInfantry, Unit_FlameTrooper}) {
            pass.infantryValue += data[item][houseID].price * pass.itemCount[item];
            pass.infantryCount += pass.itemCount[item];
        }
        for (const Uint32 item : {Unit_FlameQuad, Unit_RaiderQuad}) {
            pass.lightVehicleValue += data[item][houseID].price * pass.itemCount[item];
            pass.lightVehicleCount += pass.itemCount[item];
        }
    }
    if (pass.techSavings || AITelemetry::log().enabled()) {
        for (Uint32 item = Unit_FirstID; item <= ItemID_LastID; ++item) {
            const size_t slot = compositionSlot(item);
            if (slot == UnitMixPolicy::kSlots) continue;
            pass.compositionValue[slot] += int64_t(pass.itemCount[item]) * data[item][houseID].price;
        }
        for (size_t i = 0; i < kSlots; ++i) {
            pass.compositionPrice[i] = data[mixItems[i]][houseID].price;
            pass.compositionArmyValue += int(pass.compositionValue[i]);
        }
    }
    pass.trooperCount = pass.itemCount[Unit_Trooper] + 3 * pass.itemCount[Unit_Troopers]
        + (ModManager::instance().isTornieFamilyContentActive() ? 5 * pass.itemCount[Unit_Troopers5] : 0);
    int hostileArmourValue = 0;
    if (pass.trooperSupport) {
        for (const auto& [id, sighting] : enemySightings) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
            if (!unit || !unit->isAGroundUnit() || unit->isInfantry() || !unit->canAttack()
                || QuantBotBuildPolicy::isLightRaider(unit->getItemID())
                || unit->getItemID() == Unit_Sandworm) continue;
            hostileArmourValue = int(std::min<int64_t>(std::numeric_limits<int>::max(),
                int64_t(hostileArmourValue) + sighting.value));
        }
        pass.trooperTarget = QuantBotBuildPolicy::trooperSupportTarget(pass.hostileAircraft, hostileArmourValue);
        // The explicit Infantry strategy keeps its existing percentage/budget
        // ceiling instead of being clipped to the ordinary mix's escort squad.
        if (pass.strategyMixProfile && pass.strategyMixProfile->infantryPercent > 0)
            pass.trooperTarget = std::max(pass.trooperTarget,
                (pass.techSavings ? pass.militaryValue : pass.lightBudget)
                    / std::max(1, data[Unit_Trooper][houseID].price));
    }


    const int productionCash = std::max(0, pass.money
        - std::max(pass.strategicReserveCost, pass.economyReserve) - (pass.citySimEnabled ? pass.cityWorkingReserve : 0));
    const int troopStrategicReserve = pass.trooperSupport && pass.strategicReserveItem == Structure_Palace
        ? 0 : pass.strategicReserveCost;
    // The bounded infantry lane precedes an optional Palace, not working cash,
    // the next zone, or a required economic/production investment.
    pass.cityWorCashReserve = int64_t(std::max(troopStrategicReserve, pass.economyReserve))
        + ((!pass.trooperSupport || pass.citySimEnabled)
            ? int64_t(pass.cityWorkingReserve) + std::max(0, data[Structure_ZoneResidential][houseID].price) : 0);
    const int fundedArmyValue = QuantBotBuildPolicy::fundedArmyTarget(pass.militaryValue, pass.militaryBudget, productionCash);
    int plannedInfantryValue = pass.infantryValue;
    pass.trooperReserveValue = 0;
    // Reserve the actual standing/reactive mix, not just already-built infantry.
    // Otherwise vehicle factories consume each arriving credit before the WOR
    // can afford its first troop. An unavailable/pending producer claims nothing.
    const auto& trooper = data[Unit_Trooper][houseID];
    if (pass.trooperSupport && getHouse()->getNumItems(Structure_WOR) > 0
        && trooper.enabled && trooper.price > 0 && trooper.techLevel <= getHouse()->getTechLevel()
        && !getHouse()->isGroundUnitLimitReached()
        && int64_t(pass.militaryValue) + trooper.price <= pass.lightBudget) {
        const int missing = std::max(0, QuantBotBuildPolicy::trooperSupportFloor(pass.hostileAircraft) - pass.trooperCount);
        pass.trooperReserveValue = int(std::min<int64_t>(INT_MAX, int64_t(missing) * trooper.price));
        if (pass.techSavings && pass.hostileAircraft == 0) {
            const int quotaRoom = int(std::max<int64_t>(0,
                int64_t(std::max(0, pass.militaryValue - pass.infantryValue)) * pass.infantryPercent
                    / std::max(1, 100 - pass.infantryPercent) - pass.infantryValue));
            pass.trooperReserveValue = std::min(pass.trooperReserveValue, quotaRoom);
        }
        // Unbuilt troops claim only genuinely funded headroom in this mix.
        // Their full cap reservation and next-order cash are protected below
        // by the common producer admission path, not charged as an existing army.
        plannedInfantryValue = int(std::min<int64_t>(INT_MAX,
            int64_t(pass.infantryValue) + std::min(pass.trooperReserveValue,
                std::max(0, fundedArmyValue - pass.militaryValue))));
    }
    pass.vehiclePlanValue = pass.techSavings ? pass.compositionArmyValue
        : std::max(0, fundedArmyValue - plannedInfantryValue);
    if (pass.emitStatsLog && AITelemetry::log().enabled()) {
        traceDecision("unit_mix", AITelemetry::Record().set("basis", !pass.learningUnitMix ? "configured" : evidenceWeightedLearning ? "lifetime_evidence_weighted_value_per_loss" : "lifetime_value_per_loss")
            .set("tank_bps", (pass.tankPercent * 10000).lround()).set("siege_bps", (pass.siegePercent * 10000).lround())
            .set("special_bps", specialBps).set("devastator_bps", pass.unitMix[3]).set("sonic_tank_bps", pass.unitMix[4])
            .set("deviator_bps", pass.unitMix[5]).set("launcher_bps", (pass.launcherPercent * 10000).lround())
            .set("ornithopter_bps", (pass.ornithopterPercent * 10000).lround()).set("raw_ornithopter_bps", rawOrnithopterBps)
            .set("light_vehicle_percent", lightVehiclePercent).set("light_vehicle_bps", pass.lightVehicleBps)
            .set("trike_bps", pass.unitMix[7]).set("raider_trike_bps", pass.unitMix[8]).set("quad_bps", pass.unitMix[9])
            .set("allocation_types", int(kSlots)).set("tech_level", getHouse()->getTechLevel())
            .set("opening_light_bps", defaults[7]+defaults[8]+defaults[9]).set("total_damage", totalDamage)
            .set("total_reward_milli", totalRewardMilli).set("total_lost_value", totalLostValue)
            .set("launcher_floor_bps", launcherFloorBps)
            .set("launcher_floor_applied", launcherFloorApplied)
            .set("launcher_production_available", launcherProductionAvailable)
            .set("launcher_bps_before_floor", launcherBpsBeforeFloor)
            .set("visible_hostile_aircraft", observedHostileAircraft)
            .set("trooper_support", pass.trooperSupport).set("trooper_target", pass.trooperTarget)
            .set("troopers_committed", pass.trooperCount).set("hostile_armour_value", hostileArmourValue)
            .set("trooper_support_floor", QuantBotBuildPolicy::trooperSupportFloor(pass.hostileAircraft))
            .set("infantry_value", pass.infantryValue).set("military_value", pass.militaryValue)
            .set("planned_infantry_value", plannedInfantryValue)
            .set("trooper_reserved_value", pass.trooperReserveValue)
            .set("performance_exponent_milli",1500).set("performance_confidence_bps", performanceConfidenceBps).set("funded_army_value",fundedArmyValue)
            .set("existing_composition", pass.techSavings).set("composition_army_value", pass.compositionArmyValue)
            .set("vehicle_plan_value",pass.vehiclePlanValue).set("queued_military_value",pass.queuedMilitaryValue).set("mix_inputs", [&]() {
                AITelemetry::Record inputs;
                for (size_t i=0; i<kSlots; ++i) inputs.set(std::to_string(mixItems[i]),
                    AITelemetry::Record().set("damage", damage[i]).set("reward_milli", rewardMilli[i]).set("kill_bonus_milli", killBonusMilli[i])
                        .set("lost_value", lostValue[i]).set("score", scores[i]).set("allocation_weight",allocationWeights[i])
                        .set("lifetime_reward_milli",performanceHistory.reward[i]).set("lifetime_loss_milli",performanceHistory.loss[i])
                        .set("available", available[i]).set("opening_bps", defaults[i]).set("target_bps", pass.unitMix[i]));
                for (size_t i=0; i<kSlots; ++i) inputs.set("existing_"+std::to_string(mixItems[i]),
                    AITelemetry::Record().set("committed_value", pass.compositionValue[i])
                        .set("target_bps", pass.unitMix[i]));
                return inputs;
            }()).set("infantry_percent", pass.infantryPercent));
    }

    // lets analyse damage inflicted

    if (pass.emitStatsLog) {
        logDebug("Adaptive unit mix: damage=%lld light=%d bps", static_cast<long long>(totalDamage), pass.lightVehicleBps);

        logDebug("  Tank: %d/%d %f Siege: %d/%d %f Special: %d/%d %f Launch: %d/%d %f Orni: %d/%d %f",
            getHouse()->getNumItemDamageInflicted(Unit_Tank), getHouse()->getNumLostItems(Unit_Tank) * 300, pass.tankPercent.toDouble(),
            getHouse()->getNumItemDamageInflicted(Unit_SiegeTank), getHouse()->getNumLostItems(Unit_SiegeTank) * 600, pass.siegePercent.toDouble(),
            getHouse()->getNumItemDamageInflicted(Unit_SonicTank) + getHouse()->getNumItemDamageInflicted(Unit_Devastator) + getHouse()->getNumItemDamageInflicted(Unit_Deviator),
            getHouse()->getNumLostItems(Unit_SonicTank) * 600 + getHouse()->getNumLostItems(Unit_Devastator) * 800 + getHouse()->getNumLostItems(Unit_Deviator) * 750,
            specialBps / 10000.0,
            getHouse()->getNumItemDamageInflicted(Unit_Launcher), getHouse()->getNumLostItems(Unit_Launcher) * 450, pass.launcherPercent.toDouble(),
            getHouse()->getNumItemDamageInflicted(Unit_Ornithopter), getHouse()->getNumLostItems(Unit_Ornithopter) * data[Unit_Ornithopter][houseID].price, pass.ornithopterPercent.toDouble()
            );
    }

    // End of adaptive unit prioritisation algorithm
}

// VanillaArmyCounters (docs/ai-play-styles.md): the native mix re-weighted toward the units
// that answer the live army of the strongest terrain-reachable hostile team
// (players/QuantBotArmyCounterPolicy.h). Every live unit is read from the simulation, not
// sightings or telemetry, so each peer computes the same mix.
UnitMixPolicy::Mix QuantBot::vanillaArmyCounterMix(const BuildPass& pass, const UnitMixPolicy::Mix& native) const {
    namespace Counter = QuantBotArmyCounterPolicy;
    const int blendBps = botConfig().force.vanillaArmyCounterBlendBps;
    if (blendBps <= 0) return native;
    const auto& data = currentGame->objectData.data;
    const int houseID = getHouse()->getHouseID();
    const int myTeam = getHouse()->getTeamID();
    // A slot is a candidate only while a live factory of ours can build it now: the build
    // list's faction, tech, upgrade and prerequisite rules, and the unit limits. Starport
    // offers are market purchases, not a supplier to plan production around.
    Counter::Candidates candidates{};
    bool anyCandidate = false;
    for (const auto* structure : getStructureList()) {
        const Uint32 factory = structure->getItemID();
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0 || !structure->isActive()
            || (factory != Structure_LightFactory && factory != Structure_HeavyFactory
                && factory != Structure_HighTechFactory)) continue;
        const auto* builder = static_cast<const BuilderBase*>(structure);
        for (size_t i = 0; i < UnitMixPolicy::kSlots; ++i) {
            const Uint32 item = kMixItems[i];
            if (candidates[i].available || !campaignAvailableToBuild(builder, item)
                || getHouse()->isUnitLimitReached(item)) continue;
            const auto& spec = data[item][houseID];
            candidates[i] = {{int(item), spec.price, spec.hitpoints, spec.weapondamage,
                spec.weaponreloadtime, isFlyingUnit(item)}, true};
            anyCandidate = true;
        }
    }
    if (!anyCandidate) return native;

    // The hostile army: armed mobile combat units of other teams, alive and not carried or
    // in a repair bay. Workers, transports, MCVs, saboteurs and worms are no army.
    auto hostileArmy = [&](const UnitBase* unit) {
        const House* owner = unit->getOwner();
        return owner && !owner->isNatives() && owner->getTeamID() != myTeam
            && unit->getHealth() > 0 && unit->isActive() && unit->canAttack()
            && QuantBotInternal::mobileCombatItem(unit->getItemID())
            && Counter::armyType(int(unit->getItemID()));
    };
    auto value = [&](const UnitBase* unit) {
        return CombatPowerPolicy::unitValue(data[unit->getItemID()][unit->getProductionHouseID()].price,
            unit->getHealth().lround(), unit->getMaxHealth());
    };
    // Truly reachable: able to attack one of our core structures over terrain
    // (Map::terrainAttackReachable: aircraft fly, infantry climb mountains, a vehicle needs a
    // connected firing position). A vehicle's answer depends only on its terrain region and
    // weapon range, so the survey asks the cached connectivity once per pair.
    const Map& map = getMap();
    auto core = [&](const StructureBase* structure) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) return false;
        switch (structure->getItemID()) {
            case Structure_Wall: case Structure_Slab1: case Structure_Slab4:
            case Structure_Road: case Structure_PowerLine: return false;
            default: return true;
        }
    };
    struct Reach { int region; int range; bool reachable; };
    std::array<Reach, 32> reach{};
    size_t reaches = 0;
    auto reachable = [&](const UnitBase* unit) {
        if (!unit->isAGroundUnit() || unit->isInfantry()) return true;
        const int region = map.vehicleTerrainRegion(unit->getLocation());
        const int range = unit->getWeaponRange();
        if (region != 0)
            for (size_t i = 0; i < reaches; ++i)
                if (reach[i].region == region && reach[i].range == range) return reach[i].reachable;
        bool result = false;
        for (const auto* structure : getStructureList())
            if (core(structure) && map.terrainAttackReachable(*unit, *structure)) { result = true; break; }
        if (region != 0 && reaches < reach.size()) reach[reaches++] = {region, range, result};
        return result;
    };

    // The strongest team by reachable live value, then that team's composition.
    Counter::Teams<size_t(NUM_HOUSES)> teams;
    for (const auto* unit : getUnitList())
        if (hostileArmy(unit) && reachable(unit))
            teams.add(unit->getOwner()->getTeamID(), int(unit->getItemID()), value(unit));
    const int team = teams.strongest();
    if (team < 0) return native;
    Counter::Composition<32> force;
    for (const auto* unit : getUnitList()) {
        if (!hostileArmy(unit) || unit->getOwner()->getTeamID() != team || !reachable(unit)) continue;
        const Uint32 item = unit->getItemID();
        const auto& spec = data[item][unit->getProductionHouseID()];
        force.add({int(item), spec.price, spec.hitpoints, spec.weapondamage, spec.weaponreloadtime,
            isFlyingUnit(item)}, value(unit));
    }

    const bool originalDamage = getGameInitSettings().getGameOptions().originalUnitDamage;
    const auto target = Counter::counterTarget(force, candidates, OrdinaryDamagePolicy::modeOf(originalDamage));
    const auto mix = Counter::blend(native, candidates, target, blendBps);
    if (pass.emitStatsLog && AITelemetry::log().enabled()) {
        AITelemetry::Record slots;
        for (size_t i = 0; i < UnitMixPolicy::kSlots; ++i)
            slots.set(std::to_string(kMixItems[i]), AITelemetry::Record()
                .set("candidate", Counter::judged(candidates[i])).set("answer_value", target.value[i])
                .set("native_bps", native[i]).set("counter_bps", mix[i]));
        traceDecision("vanilla_army_counter", AITelemetry::Record().set("team", team)
            .set("team_value", force.total).set("unanswered_value", target.unanswered)
            .set("blend_bps", blendBps).set("original_unit_damage", originalDamage).set("slots", slots));
    }
    return mix;
}

// Would ordering one more of \a value still leave infantry inside their share of
// the army? The candidate's own value counts on both sides, so an order that would
// overshoot the share is refused rather than placed and then regretted. Both totals
// already include every queued order and this pass updates them as it accepts more,
// so several producers share one quota instead of each claiming a full share.
bool QuantBot::infantryQuotaAdmits(BuildPass& pass, int value) {
    // Low-tech infantry-only openings still need a dispatchable defence. Once
    // a vehicle type is available, every ordinary infantry order uses the
    // actual army share; the distant cap never creates a standing entitlement.
    if (pass.techSavings && pass.militaryValue < std::max(3000, minWaveValue())
        && std::none_of(pass.compositionAvailable.begin(), pass.compositionAvailable.end(),
            [](bool available) { return available; })) return value > 0;
    return value > 0 && (int64_t(pass.infantryValue) + value) * 100
        <= (int64_t(pass.militaryValue) + value) * pass.infantryPercent;
}

// Without TrooperSupport, a city raises infantry only for the observed wing.
// Rocket infantry can additionally fill the switch's bounded standing role.
bool QuantBot::cityInfantryItem(BuildPass& pass, Uint32 item) {
    return pass.citySimEnabled && (item == Unit_Soldier || item == Unit_Infantry
        || item == Unit_Trooper || item == Unit_Troopers
        || (ModManager::instance().isTornieFamilyContentActive()
            && (item == Unit_Troopers5 || item == Unit_Infantry5 || item == Unit_GasInfantry || item == Unit_FlameTrooper)));
}

// Check troop content and its existing prerequisites; the new WOR supplies itself.
// Its real build list still decides which offers can be queued once it exists.
bool QuantBot::troopContentExists(BuildPass& pass, const BuilderBase* builder) {
    const int houseID = pass.trooperSupport && ModManager::instance().isTornieFamilyContentActive()
        ? builder->getTechnologyHouseID() : getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    for (const Uint32 troop : {Unit_Trooper, Unit_Troopers}) {
        const auto& spec = data[troop][houseID];
        if (!spec.enabled || spec.price <= 0 || spec.techLevel > getHouse()->getTechLevel()
            || spec.builder != Structure_WOR
            || (!pass.trooperSupport && !infantryQuotaAdmits(pass, spec.price))) continue;
        bool prerequisites = true;
        for (int item = Structure_FirstID; item <= Structure_LastID; ++item)
            if (item != Structure_WOR && spec.prerequisiteStructuresSet[item]
                && getHouse()->getNumItems(item) == 0) prerequisites = false;
        if (prerequisites) return true;
    }
    return false;
}

bool QuantBot::trooperOrderAdmitted(BuildPass& pass, Uint32 item, int value) {
    const int troops = QuantBotBuildPolicy::rocketInfantrySize(item);
    const bool supportFloor = pass.trooperCount + troops
        <= QuantBotBuildPolicy::trooperSupportFloor(pass.hostileAircraft);
    return troops > 0 && pass.trooperCount + troops <= pass.trooperTarget
        && ((supportFloor && (!pass.techSavings || pass.hostileAircraft > 0)) || infantryQuotaAdmits(pass, value))
        && int64_t(pass.militaryValue) + value <= pass.lightBudget;
}

Uint32 QuantBot::trooperInfrastructureItem(BuildPass& pass, const BuilderBase* yard, bool requireCash) {
    if (!pass.trooperSupport || pass.itemCount[Structure_WOR] > 0
        || (ModManager::instance().isTornieFamilyContentActive() && pass.itemCount[Structure_Worfinery] > 0)
        || !getHouse()->hasPower() || openingWorkersNeeded(pass)
        || getHouse()->getNumItems(Structure_HeavyFactory) == 0
        || pass.forecastNetIncome <= 0 || !troopContentExists(pass, yard)) return NONE_ID;
    if (pass.techSavings && pass.hostileAircraft == 0
        && !infantryQuotaAdmits(pass, currentGame->objectData.data[Unit_Trooper][getHouse()->getHouseID()].price))
        return NONE_ID;
    const int technology = ModManager::instance().isTornieFamilyContentActive()
        ? yard->getTechnologyHouseID() : getHouse()->getHouseID();
    const auto& spec = currentGame->objectData.data[Structure_WOR][technology];
    if (!spec.enabled || spec.builder != int(yard->getItemID())
        || spec.techLevel > getHouse()->getTechLevel()) return NONE_ID;
    // An offered/pending import supplier keeps priority. A population-locked
    // Starport is not a legal core purchase yet and must not veto useful
    // infantry for the entire low-income game.
    const auto& port = currentGame->objectData.data[Structure_StarPort][technology];
    if (pass.starportMarketAvailable && port.enabled && port.price > 0
        && port.builder == int(yard->getItemID()) && port.techLevel <= getHouse()->getTechLevel()
        && getHouse()->getNumItems(Structure_StarPort) == 0
        && (pass.itemCount[Structure_StarPort] > 0 || getHouse()->meetsStarportPopulation())) return NONE_ID;
    Uint32 candidate = Structure_WOR;
    for (int prerequisite = Structure_FirstID; prerequisite <= Structure_LastID; ++prerequisite)
        if (spec.prerequisiteStructuresSet[prerequisite]
            && getHouse()->getNumItems(prerequisite) == 0) {
            if (pass.itemCount[prerequisite] > 0) return NONE_ID;
            candidate = prerequisite;
            break;
        }
    if (!campaignAvailableToBuild(yard, candidate)) return NONE_ID;
    // A cost estimate for extra generation is not generation. Do not occupy a
    // recovery yard with a WOR that would consume power already promised to
    // queued consumers, even while a larger generator is being paid for.
    if ((pass.citySimEnabled || pass.powerRules)
        && int64_t(getHouse()->getProducedPower()) < int64_t(getHouse()->getPowerRequirement())
            + pass.committedPowerDemand + std::max(0, currentGame->objectData.data[candidate][technology].power))
        return NONE_ID;
    const int64_t reserve = pass.cityWorCashReserve;
    const int64_t fundedCost = buildingCapitalCost(pass, candidate) + reserve;
    // Positive income qualifies the first WOR for protected savings even when
    // this short forecast cannot yet fund it. Actual queueing keeps every cash
    // and city working-capital check; repeat growth must not prevent saving.
    if ((requireCash && pass.money < fundedCost) || !findPlaceLocation(candidate).isValid()) return NONE_ID;
    return candidate;
}

void QuantBot::planCapitalForecast(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    pass.roadMaintenanceAttempted = false;
    const auto& force = botConfig().force;
    const auto& originalHouses = currentGame->getGameInitSettings().getHouseInfoList();
    // Initial occupied houses, not current survivors, define the stable match-size scope.
    pass.vanillaFfaLargeScope = (force.vanillaFfaThreatProduction || force.vanillaFfaFundedTech)
        && vanillaMultiEnemyGame()
        && std::count_if(originalHouses.begin(), originalHouses.end(),
            [](const auto& house) { return !house.playerInfoList.empty(); }) >= 5;
    const bool vanillaFfaThreatPolicy = force.vanillaFfaThreatProduction && pass.vanillaFfaLargeScope;
    const bool vanillaFfaTechPolicy = force.vanillaFfaFundedTech && pass.vanillaFfaLargeScope;
    const bool vanillaFfaEconomyScope = (force.vanillaFfaStarportAfterWave || vanillaFfaThreatPolicy
            || force.vanillaFfaLightWorkingCash || vanillaFfaTechPolicy)
        && vanillaMultiEnemyGame() && lastCalculatedSpice > 0
        && pass.actualHarvesters >= 4 && getAIRefineryBays() > 0
        && getHouse()->getNumItems(Structure_ConstructionYard) > 0
        && !pass.rockExpansionNeeded && !pass.colonisationNeeded;
    const bool vanillaFfaGrowth = vanillaFfaEconomyScope
        && (force.vanillaFfaStarportAfterWave || vanillaFfaThreatPolicy);
    const bool seedNeeded = vanillaFfaTechPolicy && vanillaFfaEconomyScope
        && pass.actualHarvesters < std::min(8, pass.spiceHarvesterTarget);
    const bool checkReadiness = vanillaFfaEconomyScope
        && (force.vanillaFfaLightWorkingCash || (vanillaFfaTechPolicy && !seedNeeded));
    const bool armyReady = !checkReadiness || bundleArmyReady();
    pass.vanillaFfaLightWorkingCash = force.vanillaFfaLightWorkingCash
        && vanillaFfaEconomyScope && !armyReady;
    pass.vanillaFfaTechDeferred = vanillaFfaTechPolicy
        && vanillaFfaEconomyScope && (seedNeeded || !armyReady);
    bool groundEconomyThreat = false;

    pass.defendingEconomy = false;
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->isVisible(getHouse()->getTeamID())
            || unit->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
        const auto* victim=unit->getTarget();
        if (victim && victim->getOwner()==getHouse() && unit->isInWeaponRange(victim)
            && (victim->isAStructure() || QuantBotInternal::harvesterItem(victim->getItemID())
                || victim->getItemID()==Unit_RebelHarvester)) {
            pass.defendingEconomy=true;
            if (vanillaFfaGrowth && !unit->isAFlyingUnit() && !unit->getOwner()->isNatives()
                && unit->getHealth() > 0 && victim->getHealth() > 0 && unit->canAttack())
                groundEconomyThreat = true;
        }
    }
    // A helper lever: until its ground army holds this value, a helper with two refineries
    // funds ground production before its economy, as it does under a live attack.
    const auto& helperForce = botConfig().force;
    const int armyFloor = difficulty == Difficulty::Easy ? helperForce.campaignHelperEasyArmyFloor
        : difficulty == Difficulty::Medium ? helperForce.campaignHelperMediumArmyFloor
        : difficulty == Difficulty::Hard ? helperForce.campaignHelperHardArmyFloor
        : difficulty == Difficulty::Brutal ? helperForce.campaignHelperBrutalArmyFloor : 0;
    const bool belowArmyFloor = armyFloor > 0 && pass.itemCount[Structure_Refinery] >= 2
        && ownGroundArmyValue() < armyFloor;
    pass.campaignThreatProduction = ((helperForce.campaignThreatProduction && pass.defendingEconomy) || belowArmyFloor)
        && !pass.citySimEnabled && !builderOnly()
        && campaignPolicyApplies() && isAlliedWithHuman();
    pass.vanillaFfaThreatProduction = vanillaFfaThreatPolicy && vanillaFfaGrowth
        && groundEconomyThreat && ownGroundArmyValue()
            < ForceConcentrationPolicy::scaledReadiness(houseAttackThreshold(), openingReadinessPercent());
    pass.vanillaFfaStarportDeferred = force.vanillaFfaStarportAfterWave && vanillaFfaGrowth
        && customWave.launched == 0 && pass.itemCount[Structure_StarPort] == 0;
    bool localGroundSupplier = false;
    pass.forecastTax = pass.citySimEnabled ? DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(pass.ownTaxBaseEighths,
        currentGame->getCitySimulation()->getCityTax(getHouse()->getHouseID()),pass.ownAvgLandValue)) : 0;
    pass.forecastUpkeep = pass.citySimEnabled ? getHouse()->getPowerRequirement()/8
        + DuneCity::annualPerMinute(currentGame->getCitySimulation()->getHouseState(houseID).lastPoliceExpense) : 0;
    // Future workers/bays count toward targets and commitments, but their
    // production is not income until delivered. Reforecast every planning pass.
    const int fleetRate=std::min(pass.actualHarvesters*workerAnnualIncome(pass),
        getAIRefineryBays()*pass.bayAnnualIncome);
    pass.forecastSpice = std::min(pass.spiceShare,fleetRate*QuantBotSpendingPolicy::horizonMinutes);
    pass.forecastNetIncome = pass.forecastSpice+(pass.forecastTax-pass.forecastUpkeep)*QuantBotSpendingPolicy::horizonMinutes;
    pass.forecastFunding = std::max(0,pass.money+pass.forecastNetIncome);
    // Batch3 refinery gates (docs/ai-scenario-matrix.md, "Batch3 City economy
    // experiments") size from this pass's spice plan.
    const bool cityEconomy = lastCalculatedSpice > 0 && cityEconomyExperimentScope();
    if (cityEconomy) {
        pass.cityTechRefineries = QuantBotBuildPolicy::cityRefineryGateBays(
            force.cityTechRefineries, pass.spiceHarvesterTarget);
        pass.cityYardRefineries = QuantBotBuildPolicy::cityRefineryGateBays(
            force.cityYardRefineries, pass.spiceHarvesterTarget);
    }
    // Shared-spice-limited sustainable income from the delivered fleet and
    // current tax base, never starting cash or workers still in production.
    // CityCapitalPacing applies the same levels to research-enabled City
    // economy matches only; TechIncomeTiming keeps precedence.
    const bool techIncomeScope = force.techIncomeTiming > 0 && gameMode == GameMode::Custom
        && !campaignPolicyApplies() && !builderOnly()
        && (getGameInitSettings().getModName() == "vanilla"
            || ModManager::instance().isDuneCityContentActive()) && lastCalculatedSpice > 0
        && (difficulty == Difficulty::Medium || difficulty == Difficulty::Hard
            || difficulty == Difficulty::Brutal);
    const int incomeLevel = techIncomeScope ? force.techIncomeTiming
        : cityEconomy && researchLabPolicyActive() ? force.cityCapitalPacing : 0;
    if (incomeLevel > 0) {
        pass.techIncomeLevel = incomeLevel;
        // Lifetime realised gross receipts are saved simulation state. Cap by
        // the current shared-spice/fleet forecast and subtract current upkeep;
        // an idle worker or starting-credit windfall is not income.
        const int64_t receipts = int64_t(getHouse()->getHarvestedSpice().lround())
            + getHouse()->getCityTaxReceipts().lround();
        const int64_t realised = receipts * MILLI2CYCLES(60000)
            / std::max<Uint32>(1, currentGame->getGameCycleCount()) - pass.forecastUpkeep;
        pass.techIncomePerMinute = int(std::max<int64_t>(0, std::min<int64_t>(realised,
            pass.forecastNetIncome / QuantBotSpendingPolicy::horizonMinutes)));
        std::array<int64_t, NUM_HOUSES> army{};
        const Coord base = getHouse()->getCenterOfMainBase();
        bool localThreat = pass.defendingEconomy;
        for (const auto* unit : getUnitList()) {
            const auto* owner = unit->getOwner();
            if (!owner || owner->isNatives() || !unit->isActive() || unit->getHealth() <= 0
                || !unit->canAttack() || !QuantBotInternal::mobileCombatItem(unit->getItemID())) continue;
            const int64_t value = CombatPowerPolicy::unitPower(
                data[unit->getItemID()][unit->getProductionHouseID()].price,
                unit->getHealth().lround(), unit->getMaxHealth());
            if (owner->getTeamID() == getHouse()->getTeamID()) {
                if (owner == getHouse()) pass.techOwnArmy += value;
            } else {
                army[owner->getHouseID()] += value;
                if (base.isValid() && blockDistance(base, unit->getLocation()) <= 20) localThreat = true;
            }
        }
        for (const int64_t value : army) pass.techLargestRival = std::max(pass.techLargestRival, value);
        pass.techRivalsSafe = QuantBotBuildPolicy::incomeTechnologySafe(
            pass.techOwnArmy, pass.techLargestRival, localThreat);
        if (pass.emitStatsLog && AITelemetry::log().enabled())
            traceDecision("income_tech_state", AITelemetry::Record()
                .set("level", pass.techIncomeLevel).set("income_per_minute", pass.techIncomePerMinute)
                .set("own_army", pass.techOwnArmy).set("largest_rival", pass.techLargestRival)
                .set("local_threat", localThreat).set("rivals_safe", pass.techRivalsSafe));
    }
    if ((pass.techIncomeLevel > 0 || pass.cityTechRefineries > 0) && pass.strategicReserveItem != NONE_ID
        && !incomeTechAdmits(pass, pass.strategicReserveItem, pass.strategicReserveCost)) {
        pass.strategicReserveItem = NONE_ID;
        pass.strategicReserveCost = 0;
    }
    pass.existingMilitaryCapacity = 0; pass.constructionCapacity = 0; pass.readyYards = 0;
    pass.continuedMilitaryCost = 0; pass.continuedConstructionCost = 0;
    pass.activeProductionBurn = 0;
    for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || !structure->isABuilder()) continue;
        const auto* builder=static_cast<const BuilderBase*>(structure);
        pass.capitalBuilders.push_back(builder);
        if (builder->getItemID()==Structure_ConstructionYard && !builder->isUpgrading()
            && !builder->isOnHold() && builder->getProductionQueueSize()==0) ++pass.readyYards;
        int capacity=0, cityCapacity=0, unpaid=0, currentBurn=0;
        const bool port=builder->getItemID()==Structure_StarPort;
        const bool yard=builder->getItemID()==Structure_ConstructionYard;
        const FixPoint speed=std::min(builder->getHealth()/builder->getMaxHealth(),builder->getBuildSpeedLimit());
        for (const auto& offer:builder->getBuildList()) {
            const Uint32 item=offer.itemID;
            if (!port) unpaid+=offer.num*int(offer.price);
            int buildtime=data[item][ModManager::instance().isTornieFamilyContentActive()
                ? builder->getTechnologyHouseID() : builder->getOriginalHouseID()].buildtime;
            if (pass.citySimEnabled) buildtime=DuneCity::getCityBuildTime(item,buildtime);
            if (port || buildtime<=0) continue;
            const int cost=(FixPoint(offer.price)*speed*CityEconomyInvestmentPolicy::horizonCycles/(buildtime*15)).lround();
            if (item==builder->getCurrentProducedItem() && !builder->isOnHold()
                && !builder->isUpgrading() && !builder->isWaitingToPlace()
                && !getHouse()->isUnitLimitReached(item)) currentBurn=cost/QuantBotSpendingPolicy::horizonMinutes;
            if (!campaignAvailableToBuild(builder,item)) continue;
            if ((pass.vanillaFfaStarportDeferred || pass.vanillaFfaTechDeferred)
                && builder->getItemID() == Structure_HeavyFactory && builder->getHealth() > 0
                && !builder->isUpgrading() && !builder->isOnHold()
                && QuantBotBuildPolicy::militaryItem(item)) localGroundSupplier = true;
            // MCVs are occasional capital purchases, not a permanent production
            // mix. Their real queue cost is already reserved. Assuming every
            // heavy factory continuously builds MCVs grossly inflates burn.
            if (QuantBotBuildPolicy::militaryItem(item)
                || (item==Unit_Harvester && lastCalculatedSpice>0
                    && pass.itemCount[item]<(pass.citySimEnabled ? pass.fundedHarvesterTarget : pass.spiceHarvesterTarget)))
                capacity=std::max(capacity,cost);
            if (yard && (pass.citySimEnabled
                    ? (item==Structure_ZoneResidential && pass.ownResValve>0)
                        || (item==Structure_ZoneCommercial && pass.ownComValve>0)
                        || (item==Structure_ZoneIndustrial && pass.ownIndValve>0)
                        || (item==Structure_Refinery && pass.unloadingBacklog)
                    : item==Structure_HeavyFactory || (item==Structure_Refinery && lastCalculatedSpice>0)))
                cityCapacity=std::max(cityCapacity,cost);
        }
        unpaid=port ? 0 : std::max(0,unpaid-builder->getProductionProgress().lround());
        pass.activeProductionBurn+=currentBurn;
        pass.laneCapacity[builder->getItemID()]=std::max(pass.laneCapacity[builder->getItemID()],capacity);
        // Include idle and held lines: more factories cannot fix cash starvation.
        pass.existingMilitaryCapacity+=capacity;
        pass.constructionCapacity+=cityCapacity;
        pass.continuedMilitaryCost+=std::max(0,capacity-unpaid);
        pass.continuedConstructionCost+=std::max(0,cityCapacity-unpaid);
        if (AITelemetry::log().enabled()) pass.capitalProducers.set(std::to_string(builder->getObjectID()),
            AITelemetry::Record().set("item",builder->getItemID()).set("queue",builder->getProductionQueueSize())
                .set("upgrading",builder->isUpgrading()).set("hold",builder->isOnHold())
                .set("current_item",builder->getCurrentProducedItem()).set("unpaid",unpaid)
                .set("active_burn_per_minute",currentBurn).set("unit_capacity_cost",capacity)
                .set("construction_capacity_cost",cityCapacity));
    }
    pass.vanillaFfaStarportDeferred &= localGroundSupplier;
    pass.vanillaFfaTechDeferred &= localGroundSupplier;
    if (pass.vanillaFfaTechDeferred
        && (pass.strategicReserveItem == Structure_IX || pass.strategicReserveItem == Structure_Palace)) {
        pass.strategicReserveItem = NONE_ID;
        pass.strategicReserveCost = 0;
    }
    // A first wave above tech4 requires a Repair Yard. Delaying the optional
    // port must not strand that prerequisite behind perpetual unit spending.
    if (pass.vanillaFfaStarportDeferred && getHouse()->getTechLevel() > 4
        && canAddRepairYard(0) && pass.itemCount[Structure_RepairYard] == 0) {
        for (const auto* builder : pass.capitalBuilders) {
            if (builder->getItemID() != Structure_ConstructionYard || builder->isUpgrading()
                || builder->isOnHold() || !campaignAvailableToBuild(builder, Structure_RepairYard)) continue;
            planningBuilder = builder->getObjectID(); clearPlacementCache(false, true);
            const Coord site = findPlaceLocation(Structure_RepairYard);
            if (site.isInvalid()) continue;
            const auto foundation = foundationPlan(builder, Structure_RepairYard, site);
            if (!foundation.complete) continue;
            pass.vanillaFfaWaveCapitalReserve = purchasePrice(builder, Structure_RepairYard);
            for (const auto& slab : foundation.orders)
                pass.vanillaFfaWaveCapitalReserve += purchasePrice(builder, slab.item);
            break;
        }
        planningBuilder = NONE_ID; clearPlacementCache(false, true);
        if (pass.vanillaFfaWaveCapitalReserve <= 0) pass.vanillaFfaStarportDeferred = false;
    }
    pass.cashBuffer = std::max(1000,pass.citySimEnabled ? pass.cityWorkingReserve : pass.economyReserve);
    pass.cashFlow = QuantBotSpendingPolicy::cashFlow(getHouse()->getCredits(),pass.money,pass.forecastNetIncome,
        pass.continuedMilitaryCost+pass.continuedConstructionCost,pass.existingMilitaryCapacity+pass.constructionCapacity,pass.cashBuffer);
    // Optional yards need cash beyond the existing production forecast. Real
    // construction/space recovery keeps its existing priority and budget.
    pass.capacityMcvRunway = botConfig().force.capacityMcvRunway
        && getHouse()->getNumItems(Structure_ConstructionYard)>0
        && !pass.rockExpansionNeeded && !pass.colonisationNeeded;
    pass.capacityMcvOrderedCost = 0;
    // Cash-funded vanilla openings use the established parallel MCV/factory
    // policy. Use actual runway, not a fixed wealth cutoff: as the base grows,
    // the same cash must cover more production as well as its economy pipeline.
    pass.sharedSpending = !builderOnly()
        && (pass.techSavings || !pass.vanillaEconomy || !pass.cashFlow.fundsParallelProduction);
    pass.militaryFunding = std::max(0,pass.forecastFunding-pass.continuedConstructionCost
        + pass.existingMilitaryCapacity-pass.continuedMilitaryCost);
    std::sort(pass.capitalBuilders.begin(),pass.capitalBuilders.end(),[](const auto* a,const auto* b) {
        return a->getObjectID()<b->getObjectID();
    });
}

// One cross-queue capital comparison. Protect only its next purchase; all
// independent builders may use the remainder. Recovery/prerequisite rules
// still run, but optional growth cannot consume another queue's commitment.
int QuantBot::purchasePrice(const BuilderBase* builder, Uint32 item) {
    for (const auto& offer : builder->getBuildList())
        if (offer.itemID == item) return int(offer.price);
    return 0;
}

// Concrete is no longer an investment decision. Every ordinary building is
// prepared in full before it is ordered, in every mode and at every
// difficulty, because half health is a permanent loss of production,
// generation, land value or armour. Walls, roads and the slabs themselves
// are tile states with no placement penalty and stay exempt, and the yard
// an MCV deploys is never queued, so it keeps its own exception.
// An incomplete plan defers the building; it never places it bare.
QuantBotFoundationPolicy::Plan QuantBot::foundationPlan(const BuilderBase* builder, Uint32 item, Coord site, const std::vector<Uint32>& clearedZones) {
    QuantBotFoundationPolicy::Plan plan;
    if (!getGameInitSettings().getGameOptions().concreteRequired
        || !QuantBotBuildPolicy::foundationRequiredForItem(item) || !site.isValid()) {
        plan.complete=true;
        return plan;
    }
    const Coord size=getStructureSize(item);
    if (size.x<=0 || size.y<=0) return plan;
    // A builder with no slab in its list can still take a footprint that
    // roads or existing concrete already prepare; anything else is deferred
    // rather than placed bare.
    return QuantBotFoundationPolicy::planFoundation(site.x,site.y,size.x,size.y,
        campaignAvailableToBuild(builder,Structure_Slab4),
        campaignAvailableToBuild(builder,Structure_Slab1),
        [&](int x,int y) {
            QuantBotFoundationPolicy::TileState state;
            if (!getMap().tileExists(x,y)) return state;
            const Tile* tile=getMap().getTile(x,y);
            state.exists=true;
            state.prepared=tile->hasPreparedFoundation();
            state.road=tile->isRoad();
            const auto* occupant=tile->getNonInfantryGroundObject();
            const bool cleared=occupant && std::find(clearedZones.begin(),clearedZones.end(),
                occupant->getObjectID())!=clearedZones.end();
            // Our own traffic does not stop paving (the footprint rule).
            state.paveable=tile->isRock() && !tile->isMountain()
                && (!tileBlocksOwnPlacement(tile) || cleared) && (!tile->hasCityZone() || cleared);
            state.inBuildRange=getMap().isWithinBuildRange(x,y,getHouse());
            return state;
        });
}

std::vector<QuantBotFoundationPolicy::SlabOrder> QuantBot::foundationOrders(const BuilderBase* builder, Uint32 item, Coord site) {
    return foundationPlan(builder,item,site).orders;
}

// The buildings this bot keeps at full health regardless of wealth. Used
// both by the repair pass below and by the generation investment, which
// prices exactly the repairs a closed power deficit would stop paying for.
// StructureBase charges repairs from the original owner's price table, so
// captured buildings and mod overrides are classified the way the engine
// actually bills them.
Sint32 QuantBot::repairPrice(const StructureBase* structure) {
    auto& data = currentGame->objectData.data;
    return data[structure->getItemID()][structure->getProductionHouseID()].price;
}

bool QuantBot::repairPolicyMaintains(BuildPass& pass, const StructureBase* structure) {
    return QuantBotBuildPolicy::repairMaintainsValue(structure->getItemID(),pass.citySimEnabled,
        structure->getMaxHealth(),repairPrice(structure));
}

bool QuantBot::canFundCapacityMcv(BuildPass& pass, int price, Uint32 supplier) {
    if (pass.vanillaBoomArmy && pass.itemCount[Structure_ConstructionYard] > 0) return false;
    if (pass.itemCount[Structure_ConstructionYard]>0 && !pass.rockExpansionNeeded && !pass.colonisationNeeded
        && (!cityOpeningCapitalFunded(pass,price,supplier) || getAIRefineryBays() < pass.cityYardRefineries)) return false;
    return !pass.capacityMcvRunway || int64_t(pass.cashFlow.projectedCash)-pass.capacityMcvOrderedCost
        >= int64_t(price)+pass.cashBuffer;
}

void QuantBot::planCityBootstrap(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // One construction slot keeps growing the tax base. Recompute it from real
    // queues, so losing a yard or loading a save cannot strand the assignment.
    pass.cityYards = 0; pass.cityGrowthYardsBusy = 0;
    if (pass.citySimEnabled) for (const auto* builder:pass.capitalBuilders) {
        if (builder->getItemID()!=Structure_ConstructionYard) continue;
        ++pass.cityYards;
        const auto reserved=reservedStructures.find(builder->getObjectID());
        if (!builder->isOnHold() && !builder->isUpgrading() && builder->getProductionQueueSize()>0
            && (DuneCity::isCityZoneStructure(builder->getCurrentProducedItem())
                || (reserved!=reservedStructures.end() && DuneCity::isCityZoneStructure(reserved->second.item))))
            ++pass.cityGrowthYardsBusy;
    }
    // Cash the other builders must leave alone while a yard is saving for a
    // required civic or its missing prerequisite. Only what that yard's own
    // funding guard asks for: the foundation it will queue plus one engine
    // installment. Withholding the whole price here is the full-cash rule this
    // planner deliberately does not apply to installment projects, and on a
    // 3,000-credit stadium it would have stopped every factory as well.
    //
    // The demand itself goes quiet as soon as the project is queued, because the
    // queued order already counts in itemCount. Its income priority does not go
    // quiet with it: queuedProductionCost (above) already withholds the whole
    // unpaid remainder of every queue from this pass's spendable money. No extra
    // reservation is added for a queued order, or the same credits would be
    // withheld twice.
    pass.civicReserveCost = 0;
    if (pass.citySimEnabled && pass.civicStructuralNeed != 0) for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getItemID() != Structure_ConstructionYard) continue;
        const auto plan = planCivicForYard(pass, static_cast<const BuilderBase*>(structure));
        if (plan.order == NONE_ID) continue;
        pass.emergencyCivic[plan.order] = true;
        const int price = data[plan.order][houseID].price;
        pass.civicReserveCost = std::max(pass.civicReserveCost,
            CityEconomyInvestmentPolicy::productionStartBudget(price,
                std::max(0, buildingCapitalCost(pass, plan.order) - price), cityBuildTicks(pass, plan.order)));
    }
    // A city map with no usable spice has to grow a tax base out of its
    // starting grant: a processing bay, the port's income ladder and an
    // imported transport all earn nothing there, and 1.0.816 spent the whole
    // 2,000 on exactly those before a single demanded lot was ever zoned.
    // Until the seed is committed and actually taxable, optional tech, repair
    // capacity, peaceful coverage and bargain imports must leave its cash
    // alone. Essential power and a present threat still outrank it, and the
    // counts come from itemCount, so queued orders already hold their place
    // and parallel yards cannot buy the same seed twice.
    pass.spiceEconomyViable =
        CityEconomyInvestmentPolicy::spiceEconomyViable(pass.mapSpiceHarvesterTarget);
    // A lot pays for its own foundation and for the roads that connect it;
    // an unconnected 2x2 never develops, so it is part of the lot's price.
    pass.cityBootstrapLotCost = buildingCapitalCost(pass, Structure_ZoneResidential)
        + 2 * std::max(0, data[Structure_Road][houseID].price);
    // Somewhere to put the money. Capacity comes from the mod's own tables,
    // not from an assumption about which building stores credits: take the
    // cheapest item this house can actually build and place that reports any
    // capacity at all. In the shipped tree that is the Refinery (1,005) and
    // then the Silo (10,000) behind it - so on a spiceless map a refinery is
    // bought for its vault and its tech, never to feed a harvester, and the
    // distinction is in the rule that buys it, not in the building.
    pass.cityStorageCapacityIncludingQueued = getHouse()->getCapacity();
    for (Uint32 item = Structure_FirstID; item <= (ModManager::instance().isTornieFamilyContentActive() ? ItemID_LastID : Structure_LastID); ++item)
        if (isStructure(item)) pass.cityStorageCapacityIncludingQueued += std::max(0,
            rawPlannedItemCount(pass,item) - getHouse()->getNumItems(item)) * std::max(0, data[item][houseID].capacity);
    // City-match Construction Yards store 1000 credits (1.0.862), a buffer for the
    // player's opening. The AI still buys its first real storage (the Refinery,
    // which also unlocks the Silo and on a cramped base extends its build range)
    // exactly as when yards stored nothing: storage from yards alone is "missing".
    if (pass.citySimEnabled
        && pass.cityStorageCapacityIncludingQueued <= std::max(0, pass.itemCount[Structure_ConstructionYard])
            * std::max(0, data[Structure_ConstructionYard][houseID].capacity))
        pass.cityStorageCapacityIncludingQueued = 0;
    pass.cityStorageItem = NONE_ID;
    pass.cityStorageCost = 0;
    if (pass.citySimEnabled
        && CityEconomyInvestmentPolicy::cityStorageMissing(pass.cityStorageCapacityIncludingQueued))
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || !structure->isABuilder()) continue;
            const auto* builder = static_cast<const BuilderBase*>(structure);
            if (builder->getItemID() != Structure_ConstructionYard) continue;
            for (Uint32 item = Structure_FirstID; item <= Structure_LastID; ++item) {
                if (data[item][houseID].capacity <= 0) continue;
                if (!campaignAvailableToBuild(builder,item)) continue;
                const int cost = buildingCapitalCost(pass, item);
                if (pass.cityStorageItem != NONE_ID && cost >= pass.cityStorageCost) continue;
                if (!findPlaceLocation(item).isValid()) continue;
                pass.cityStorageItem = item; pass.cityStorageCost = cost;
            }
        }
    // Live, not latched: cityZonesIncludingQueued rises as this pass places
    // orders, so the reserve shrinks lot by lot and several yards in the same
    // pass cannot each withhold - or each buy - the whole seed. Only a city
    // the simulation actually demands can be seeded; with no positive R/C/I
    // demand there is nothing to protect and nothing to wait for, so the
    // ordinary ladder must not be held up either.
    pass.cityParallelBootstrapFunding = false;
}

Uint32 QuantBot::affordableCityZone(BuildPass& pass, const BuilderBase* builder, int budget) {
    if (!pass.citySimEnabled || getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<24)
        return Uint32(NONE_ID);
    const Uint32 zone=chooseCityZone(pass, builder,false);
    if (zone==NONE_ID || purchasePrice(builder,zone)>budget) return NONE_ID;
    if (pass.techSavings) {
        const Coord site=findPlaceLocation(zone);
        if (site.isInvalid()) return NONE_ID;
        const auto foundation=foundationPlan(builder,zone,site);
        if (!foundation.complete) return NONE_ID;
        int cost=purchasePrice(builder,zone);
        for (const auto& slab:foundation.orders) cost+=purchasePrice(builder,slab.item);
        if (cost>budget || !spendingAdmits(pass,zone,cost)) return NONE_ID;
    }
    return zone;
}

int QuantBot::buildingCapitalCost(BuildPass& pass, Uint32 item) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    const Coord size=getStructureSize(item);
    const int power=pass.citySimEnabled || pass.powerRules ? std::max(0,data[item][houseID].power) : 0;
    // Without concrete there is no foundation to budget for.
    const int foundation=getGameInitSettings().getGameOptions().concreteRequired
        ? size.x*size.y*data[Structure_Slab1][houseID].price : 0;
    return data[item][houseID].price + foundation
        + (power*pass.generationCostPerThousand+999)/1000;
}

// The engine's own installment divisor: BuilderBase::updateProductionProgress()
// charges price/(buildTime*15) per tick, taking the city build time in city
// games. Used to price one installment, never to predict a completion date.
int QuantBot::cityBuildTicks(BuildPass& pass, Uint32 item) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    const int configured = data[item][houseID].buildtime;
    return 15 * (pass.citySimEnabled ? DuneCity::getCityBuildTime(item,configured) : configured);
}

bool QuantBot::cityBootstrapNeeded(BuildPass& pass) {
    // Nothing to hold cash for without a yard that can place a lot.
    return pass.cityYards > 0
        && CityEconomyInvestmentPolicy::cityBootstrapIncomplete(pass.citySimEnabled,
            pass.spiceEconomyViable, pass.cityZonesIncludingQueued,
            pass.cityStorageCapacityIncludingQueued)
        && (!pass.cityParallelBootstrapFunding
            || CityEconomyInvestmentPolicy::cityStorageMissing(pass.cityStorageCapacityIncludingQueued))
        // A house that owns no lot at all holds the seed whatever the
        // current valves say. On the first pass the simulation has not
        // published any demand yet, and a map-placed Starport spent the
        // whole grant on bargains in that one-tick window - the original
        // bankruptcy, on a different map. Once some lots exist, continued
        // protection does need demand, so a finished city releases it.
        // Unbankable income needs no demand reading at all: the vault is
        // worth buying the moment the city can earn anything.
        && (pass.cityZonesIncludingQueued == 0
            || CityEconomyInvestmentPolicy::cityStorageMissing(pass.cityStorageCapacityIncludingQueued)
            || pass.ownResValve > 0 || pass.ownComValve > 0 || pass.ownIndValve > 0);
}

int QuantBot::cityBootstrapReserve(BuildPass& pass) {
    return CityEconomyInvestmentPolicy::cityBootstrapReserve(cityBootstrapNeeded(pass),
        pass.cityZonesIncludingQueued, pass.cityBootstrapLotCost,
        pass.cityStorageItem == NONE_ID
            || !CityEconomyInvestmentPolicy::cityStorageMissing(pass.cityStorageCapacityIncludingQueued)
            ? 0 : pass.cityStorageCost);
}

void QuantBot::planOpeningProduction(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // A large starting grant can pay for the first heavy line AND keep it
    // operating. In that case construction throughput, not income, is the
    // opening constraint. Budget its missing prerequisites and four minutes of
    // production before allowing it ahead of the ordinary refinery/port path.
    pass.firstFactoryCapital = 0; pass.firstFactoryProduction = 0;
    bool firstFactoryLegal=true;
    std::set<Uint32> firstFactoryPrerequisites;
    std::vector<Uint32> factorySteps{Structure_HeavyFactory};
    while (!factorySteps.empty()) {
        const auto item=factorySteps.back();factorySteps.pop_back();
        if (!firstFactoryPrerequisites.insert(item).second || pass.itemCount[item]>0) continue;
        firstFactoryLegal &= data[item][houseID].enabled && data[item][houseID].techLevel<=getHouse()->getTechLevel();
        pass.firstFactoryCapital+=buildingCapitalCost(pass, item);
        for (int prerequisite=Structure_FirstID;prerequisite<=Structure_LastID;++prerequisite)
            if (data[item][houseID].prerequisiteStructuresSet[prerequisite]) factorySteps.push_back(prerequisite);
    }
    for (int item=Unit_FirstID;item<=(ModManager::instance().isTornieFamilyContentActive() ? ItemID_LastID : Unit_LastID);++item) {
        const auto& info=data[item][houseID];
        if (!isUnit(item) || !info.enabled || info.builder!=Structure_HeavyFactory || info.techLevel>getHouse()->getTechLevel()
            || info.upgradeLevel>0 || info.buildtime<=0 || item==Unit_MCV) continue;
        const int time=pass.citySimEnabled ? DuneCity::getCityBuildTime(item,info.buildtime) : info.buildtime;
        pass.firstFactoryProduction=std::max(pass.firstFactoryProduction,
            int(int64_t(info.price)*CityEconomyInvestmentPolicy::horizonCycles/std::max(1,time*15)));
    }
    pass.fundedFactoryOpening = gameMode==GameMode::Custom && pass.itemCount[Structure_HeavyFactory]==0
        && firstFactoryLegal && pass.firstFactoryProduction>0 && pass.militaryValue<pass.militaryBudget
        && pass.money>=pass.firstFactoryCapital+pass.cashBuffer
        && pass.cashFlow.projectedCash>=pass.firstFactoryCapital+pass.firstFactoryProduction+pass.cashBuffer;
    // A funded established city can grow and produce in parallel. Its cash
    // must cover both commitments, rather than treating one tiny tax receipt
    // as permission to spend a scarce opening grant on optional technology.
    const int bootstrapCommitment=CityEconomyInvestmentPolicy::cityBootstrapReserve(true,
        pass.cityZonesIncludingQueued,pass.cityBootstrapLotCost,pass.cityStorageItem==NONE_ID ? 0 : pass.cityStorageCost);
    const bool establishedCityIncome=getHouse()->getNumItems(Structure_HeavyFactory)>0
        && getHouse()->getCapacity()>0 && pass.ownTaxBaseEighths>0 && pass.forecastTax>pass.forecastUpkeep;
    pass.cityParallelBootstrapFunding=establishedCityIncome || (pass.ownTaxBaseEighths>0
        && pass.money>=pass.firstFactoryCapital+pass.cashBuffer+bootstrapCommitment
        && pass.cashFlow.projectedCash>=pass.firstFactoryCapital+pass.firstFactoryProduction+pass.cashBuffer+bootstrapCommitment);
    // When cash covers another line AND four minutes of its operation, use
    // the established parallel factory/repair build order. City demand is not
    // a prerequisite for production: factories also supply the city's MCVs.
    pass.nextHeavyRunway = buildingCapitalCost(pass, Structure_HeavyFactory)
        + std::max(pass.firstFactoryProduction,pass.laneCapacity[Structure_HeavyFactory])+pass.cashBuffer;
    pass.fundedCityProduction = pass.citySimEnabled && gameMode==GameMode::Custom
        && pass.cashFlow.fundsParallelProduction && pass.money>=pass.nextHeavyRunway
        && pass.cashFlow.projectedCash>=pass.nextHeavyRunway && pass.militaryValue<pass.militaryBudget;
    pass.desiredWorkers = pass.citySimEnabled ? pass.fundedHarvesterTarget : pass.spiceHarvesterTarget;
    pass.transportBaseline = QuantBotBuildPolicy::carryallTarget(pass.itemCount[Unit_Harvester],pass.combatVehicles,
        getHouse()->getNumItems(Structure_RepairYard));
    pass.transportTarget = QuantBotBuildPolicy::supportQueueTarget(pass.transportBaseline,
        getHouse()->getNumItems(Unit_Carryall),pass.itemCount[Unit_Carryall],pass.busyTransports,int(pass.pickupQueue.size()));
    if (pass.fleetRatios)
        pass.transportBaseline = pass.transportTarget = (std::min(pass.mapSpiceHarvesterTarget,
            pass.itemCount[Unit_Harvester]) * fleetRatioTreatment().carryallQuarters + 3) / 4;
}

void QuantBot::planDefenceCoverage(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Coverage the core still lacks, counting existing and planned turrets.
    // This is a spatial demand, not a count goal: it stops by itself once the
    // yards, refineries and factories have the difficulty's overlap, so it can
    // drive defence proactively without turning into turret spam. Coverage
    // starts once the first refinery pays for it, not after the full fleet.
    pass.coverageTier = rocketCoverageTier(difficulty);
    // Enemy aircraft that have actually reached our territory. A wing counted
    // from the enemy's item list is a plan the base can prepare for between
    // other construction; aircraft over our own buildings are a present loss.
    pass.airEngaged = false;
    visibleGroundTurretThreats.clear();
    const bool collectGround=botConfig().force.threatTurrets && gameMode==GameMode::Custom
        && difficulty>=Difficulty::Hard;
    for (const auto* unit:getUnitList()) {
        if (pass.airEngaged && !collectGround) break;
        if (!unit->isActive() || unit->getHealth()<=0 || !unit->canAttack() || !unit->getOwner()
            || unit->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !unit->isVisible(getHouse()->getTeamID())) continue;
        if (collectGround && !unit->isAFlyingUnit() && unit->getItemID()!=Unit_Saboteur
            && QuantBotBuildPolicy::militaryItem(unit->getItemID())) {
            const int value=data[unit->getItemID()][unit->getProductionHouseID()].price;
            visibleGroundTurretThreats.push_back({unit->getLocation(),std::max(100,value),std::max(1,unit->getWeaponRange())});
        }
        if (pass.airEngaged || !unit->isAFlyingUnit()) continue;
        const int reach=std::max(1,unit->getWeaponRange())+8;
        for (const auto* structure:getStructureList())
            if (structure->getOwner()==getHouse() && structure->getHealth()>0
                && blockDistance(unit->getLocation(),
                    structure->getClosestPoint(unit->getLocation()))<=reach) { pass.airEngaged=true; break; }
    }
    // Core assets that no turret covers at all, as opposed to those short of
    // the difficulty's overlap. First cover is defence; further overlap is a
    // plan that has to share the yards with the city it protects.
    pass.uncoveredCoreAssets = 0;
    // Prime the first-cover count for the rules that consult it before any
    // shortfall is evaluated; later calls refresh it against new reservations.
    coreCoverageShortfall(pass);
    // Cover the base actually demands, used both as the interim emplacement
    // ceiling and to bound proactive coverage below.
    pass.coverageDemand = 0;
    const Uint32 anchorYard=mainConstructionYardID();
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->getHealth()>0)
            pass.coverageDemand+=RocketTurretPolicy::desiredCoverage(structure->getItemID(),pass.coverageTier,
                isExpansionYard(structure->getItemID(),structure->getObjectID(),anchorYard));
    // A visible ground raid needs local firing cover, not the peaceful battery's
    // two-refinery/production-core opening. Reuse this build's visible-unit survey;
    // never inspect an unseen army or search the map per hostile unit.
    if (botConfig().force.threatTurrets && gameMode==GameMode::Custom
        && difficulty>=Difficulty::Hard && !visibleGroundTurretThreats.empty()
        && pass.itemCount[Structure_RocketTurret]<RocketTurretPolicy::threatTurretTotalCap) {
        const int range=std::max(1,data[Structure_RocketTurret][houseID].weaponrange-1);
        int bestMissing=0;
        const bool protectCityZones = botConfig().force.cityZoneDefence && pass.citySimEnabled
            && !campaignPolicyApplies() && lastCalculatedSpice > 0;
        for (const auto* asset:getStructureList()) {
            if (asset->getOwner()!=getHouse() || asset->getHealth()<=0
                || (!RocketTurretPolicy::coreAsset(asset->getItemID())
                    && asset->getItemID()!=Structure_Radar && asset->getItemID()!=Structure_WindTrap
                    && !(protectCityZones && DuneCity::isCityZoneStructure(asset->getItemID())))) continue;
            int value=0;
            int64_t x=0,y=0;
            for (const auto& contact:visibleGroundTurretThreats) {
                if (blockDistance(contact.at,asset->getClosestPoint(contact.at))>range+5) continue;
                value+=contact.value;x+=int64_t(contact.at.x)*contact.value;y+=int64_t(contact.at.y)*contact.value;
            }
            if (value<=0) continue;
            const Coord at=asset->getLocation()+Coord(asset->getStructureSizeX()/2,asset->getStructureSizeY()/2);
            int cover=0;
            for (const auto* turret:getStructureList())
                if (turret->getOwner()==getHouse() && turret->getHealth()>0
                    && turret->getItemID()==Structure_RocketTurret
                    && blockDistance(turret->getLocation(),at)<=range) ++cover;
            for (const auto& entry:reservedStructures)
                if (entry.second.item==Structure_RocketTurret
                    && blockDistance(entry.second.location,at)<=range) ++cover;
            const int goal=RocketTurretPolicy::groundThreatGoal(value,botConfig().force.moreRocketTurrets);
            const int missing=goal-cover;
            if (missing>bestMissing || (missing==bestMissing && missing>0 && value>pass.groundThreatValue)) {
                bestMissing=missing;pass.groundThreatAsset=at;
                pass.groundThreatApproach=Coord(int(x/value),int(y/value));
                pass.groundThreatValue=value;pass.groundTurretGoal=goal;pass.groundTurretCover=cover;
            }
        }
    }
    // Spread core assets can demand an overlap that no legal site reaches, so
    // the shortfall alone never returns to zero. Peaceful pursuit of it then
    // owns every construction slot and its savings for the rest of the game,
    // which is what stopped city growth. Bound the proactive claim by the
    // emplacements the base's own demand justifies; observed aircraft keep the
    // unbounded shortfall, because that is a present loss rather than a plan.
    // Both custom-game economies bound ordinary cover while the production
    // core is unfinished; MoreRocketTurrets permits a small established-base allowance.
    pass.missingCoreInfrastructure = NONE_ID;
    pass.coreInfrastructureReady = true;
    if (gameMode==GameMode::Custom && !builderOnly())
        for (Uint32 item:{Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard}) {
            if (!data[item][houseID].enabled || data[item][houseID].techLevel>getHouse()->getTechLevel()) continue;
            if (getHouse()->getNumItems(item)==0) pass.coreInfrastructureReady=false;
            if (pass.missingCoreInfrastructure==NONE_ID && pass.itemCount[item]==0) pass.missingCoreInfrastructure=item;
        }
    // The StorageCapacity-off control retains the former city core gate.
    pass.cityCoreInfrastructureReady = !pass.citySimEnabled || pass.coreInfrastructureReady;
    // IX unlocks the units and upgrades that make further emplacements worth
    // owning, so a non-city base that can already order it finishes that step
    // before repeating optional cover. City coverage keeps its own established
    // core rule; this adds no new condition there.
    const bool advancedTechPending=!pass.citySimEnabled && gameMode==GameMode::Custom && !builderOnly()
        && pass.itemCount[Structure_HeavyFactory]>0 && pass.itemCount[Structure_IX]==0
        && pass.anyConstructionYardCanBuildIX && findPlaceLocation(Structure_IX).isValid();
    // Keep unloading capacity ahead of optional additional cover as the worker
    // fleet grows. Existing/pending bays count, and an unplaceable refinery
    // cannot reserve the construction queue. The first two rockets still fit.
    const bool refiningCapacityPending=!pass.citySimEnabled && pass.customStrategicPlanning
        && lastCalculatedSpice>=500 && pass.anyConstructionYardCanBuildRefinery
        && pass.itemCount[Structure_Refinery]<(pass.vanillaEconomy
            ? QuantBotBuildPolicy::desiredSpiceRefineries(pass.spiceHarvesterTarget,pass.itemCount[Unit_Harvester])
            : pass.itemCount[Unit_Harvester]/3)
        && findPlaceLocation(Structure_Refinery).isValid();
    pass.earlyRocketLimit=RocketTurretPolicy::earlyRocketLimit(
        botConfig().force.moreRocketTurrets && gameMode==GameMode::Custom,
        pass.coverageTier,getHouse()->getNumStructures());
    pass.routineRocketsUnrestricted=pass.coreInfrastructureReady && !advancedTechPending && !refiningCapacityPending;
    pass.routineRocketsAllowed=pass.routineRocketsUnrestricted
        || pass.itemCount[Structure_RocketTurret]<pass.earlyRocketLimit;
    // A proactive goal keeps its savings only while the forecast cannot also
    // fund a city plot. Growth pays the bills that reach the saved price, so a
    // hold that outlives the forecast starves the economy it is defending.
    // Urgent crime keeps its unconditional reserve.
    pass.cityPlotPrice = data[Structure_ZoneResidential][houseID].price;
    // Reserve the turret's draw and one small city plot, rather than two whole
    // spare windtraps. Normal power planning separately budgets maturation.
    // Outside the city the reserve is the emplacement's own draw: the flat 225
    // was more than two spare windtraps of headroom per turret, and a base
    // countering aircraft built thirteen generators it never needed.
    pass.rocketPowerBuffer = pass.citySimEnabled ? std::max(0,data[Structure_RocketTurret][houseID].power)
        + std::max({DuneCity::getZonePower(Structure_ZoneResidential,1),
                    DuneCity::getZonePower(Structure_ZoneCommercial,1),
                    DuneCity::getZonePower(Structure_ZoneIndustrial,1)})
        : std::max(0,data[Structure_RocketTurret][houseID].power);
    const bool familySupplier = pass.cityBoomWorkers > 0;
    pass.openingSupplierDue = openingFleetIncomplete(pass)
        && pass.itemCount[Structure_Refinery] >= (familySupplier ? 2 : 3)
        && (pass.itemCount[Structure_RocketTurret] >= 2 || (familySupplier && pass.cityBoomFunding))
        && pass.itemCount[Structure_StarPort] == 0 && pass.itemCount[Structure_HeavyFactory] == 0;
    pass.moderateCrimeProperties = 0; pass.dangerousCrimeProperties = 0;
    if (pass.citySimEnabled) for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || structure->getHealth()<=0
            || DuneCity::getStructureCityRole(structure->getItemID())==DuneCity::CityRole::None) continue;
        const auto p=structure->getLocation();
        const int crime=currentGame->getCitySimulation()->getCrimeRateMap().worldGet(p.x,p.y);
        pass.moderateCrimeProperties+=crime>=128;
        pass.dangerousCrimeProperties+=crime>=192;
    }
}

int QuantBot::coreCoverageShortfall(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (!pass.citySimEnabled || pass.itemCount[Structure_Refinery]==0) return 0;
    const int radius=std::max(1,data[Structure_RocketTurret][houseID].weaponrange-1);
    std::vector<Coord> turrets;
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->getHealth()>0
            && structure->getItemID()==Structure_RocketTurret) turrets.push_back(structure->getLocation());
    for (const auto& entry:reservedStructures)
        if (entry.second.item==Structure_RocketTurret) turrets.push_back(entry.second.location);
    int shortfall=0;
    pass.uncoveredCoreAssets=0;
    const Uint32 mainYard=mainConstructionYardID();
    auto account=[&](Uint32 item,Coord origin,Coord size,Uint32 objectID) {
        if (!RocketTurretPolicy::coreAsset(item)) return;
        const int demand=RocketTurretPolicy::desiredCoverage(item,pass.coverageTier,
            isExpansionYard(item,objectID,mainYard));
        int covered=0;
        for (const Coord turret:turrets)
            if (RocketTurretPolicy::coversBuilding(turret,origin,size,radius)) ++covered;
        if (demand>0 && covered==0) ++pass.uncoveredCoreAssets;
        shortfall+=std::max(0,demand-covered);
    };
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->getHealth()>0)
            account(structure->getItemID(),structure->getLocation(),structure->getStructureSize(),
                structure->getObjectID());
    for (const auto& entry:reservedStructures)
        account(entry.second.item,entry.second.location,getStructureSize(entry.second.item),NONE_ID);
    return shortfall;
}

// Earned spice and taxes share storage; grants do not occupy it. Predict the
// surplus after real production, rather than waiting for a late-game factory
// ladder or treating an unbuilt Silo as the only possible vault at tech 1.
Uint32 QuantBot::storageExpansionItem(BuildPass& pass, const BuilderBase* builder) {
    const int houseID = getHouse()->getHouseID();
    const auto& data = currentGame->objectData.data;
    const int capacity = getHouse()->getCapacity();
    if (!botConfig().force.storageCapacity && !pass.techSavings) {
        const int pendingCapacity = std::max(0,pass.itemCount[Structure_Refinery]
            -getAIRefineryBays())*data[Structure_Refinery][houseID].capacity;
        const int projectedCapacity = capacity+pendingCapacity;
        return (getHouse()->getNumItems(Structure_HeavyFactory)>0
                || data[Structure_HeavyFactory][houseID].techLevel>getHouse()->getTechLevel())
            && pass.cityCoreInfrastructureReady && capacity>0 && projectedCapacity<House::MAX_GAME_CREDITS
            && getHouse()->getEarnedCredits()>=projectedCapacity*0.80_fix
            && pass.itemCount[Structure_Silo]==getHouse()->getNumItems(Structure_Silo)
            && campaignAvailableToBuild(builder,Structure_Silo) ? Uint32(Structure_Silo) : Uint32(NONE_ID);
    }
    // A Medium campaign opponent adds a Silo once its storage is 90% full, as SiloFillPercent does.
    const int fill = botConfig().force.siloFillPercent>0 ? botConfig().force.siloFillPercent
        : isCampaignEnemy() && difficulty==Difficulty::Medium ? 90 : 0;
    if (builder->isUpgrading() || builder->getProductionQueueSize()!=0
        || !getHouse()->hasPower()
        || (pass.forecastNetIncome<=0 && fill==0)) return NONE_ID;
    int64_t projectedCapacity = capacity;
    const Uint32 last = ModManager::instance().isTornieFamilyContentActive() ? ItemID_LastID : Structure_LastID;
    for (Uint32 item=Structure_FirstID; item<=last; ++item) {
        if (!isStructure(item) || data[item][houseID].capacity<=0) continue;
        const int pending = std::max(0,rawPlannedItemCount(pass,item)-getHouse()->getNumItems(item));
        // A pending refinery, Silo or other vault gets delivered before another
        // storage project is ordered, including across parallel yards.
        if (pending>0 && item!=Structure_ConstructionYard) return NONE_ID;
        projectedCapacity += int64_t(pending)*data[item][houseID].capacity;
    }
    const int ceiling = House::MAX_GAME_CREDITS-getHouse()->getStartingCredits().lround();
    if (projectedCapacity<=0 || projectedCapacity>=ceiling) return NONE_ID;
    if (fill>0 && lastCalculatedSpice>0
        && getHouse()->getEarnedCredits()*100 >= FixPoint(int(projectedCapacity))*fill
        && campaignAvailableToBuild(builder,Structure_Silo)
        && pass.money>=buildingCapitalCost(pass,Structure_Silo)) {
        const Coord site=findPlaceLocation(Structure_Silo);
        if (site.isValid() && dangerAt(site,getStructureSize(Structure_Silo))==0)
            return Structure_Silo;
    }
    if (pass.forecastNetIncome<=0) return NONE_ID;
    Uint32 choice = NONE_ID;
    int choiceCost = 0, choiceCapacity = 0;
    for (Uint32 item=Structure_FirstID; item<=last; ++item) {
        const int added = data[item][houseID].capacity;
        if (!isStructure(item) || item==Structure_ConstructionYard || added<=0
            || !campaignAvailableToBuild(builder,item)) continue;
        const int cost = buildingCapitalCost(pass,item);
        if (cost<=0 || cost>pass.money || (choice!=NONE_ID
            && int64_t(cost)*choiceCapacity>=int64_t(choiceCost)*added)) continue;
        // Allow a planning/placement interval as well as actual construction.
        // This bounded forecast cannot buy years of empty capacity in advance.
        const FixPoint speed = std::min(builder->getHealth()/builder->getMaxHealth(),builder->getBuildSpeedLimit());
        if (speed<=0) continue;
        const Coord size = getStructureSize(item);
        const int foundationTicks = getGameInitSettings().getGameOptions().concreteRequired
            ? size.x*size.y*cityBuildTicks(pass,Structure_Slab1) : 0;
        const int lead = std::clamp((FixPoint(cityBuildTicks(pass,item)+foundationTicks)/speed).lround()
                + int(MILLI2CYCLES(5000)),int(DuneCity::kCyclesPerMinute),CityEconomyInvestmentPolicy::horizonCycles);
        const int receipts = int(int64_t(pass.forecastNetIncome)*lead/CityEconomyInvestmentPolicy::horizonCycles);
        const int spending = int(int64_t(pass.activeProductionBurn)*lead/DuneCity::kCyclesPerMinute);
        if (!QuantBotBuildPolicy::storageCapacityNeeded(getHouse()->getEarnedCredits().lround(),
                int(projectedCapacity),receipts,spending,pass.capitalOrderedCost)) continue;
        const Coord site = findPlaceLocation(item);
        if (site.isInvalid() || dangerAt(site,size)>0) continue;
        choice=item;choiceCost=cost;choiceCapacity=added;
    }
    return choice;
}

// Coverage worth buying this pass. The unbounded shortfall belongs to
// aircraft that have actually reached the base — owning ornithopters
// somewhere on the map kept a 1v4 chasing an overlap it could never
// complete — and every claim is interleaved with construction unless a
// core asset still has no cover at all.
int QuantBot::proactiveCoverageShortfall(BuildPass& pass) {
    if (!pass.routineRocketsAllowed) return 0;
    const int shortfall=coreCoverageShortfall(pass);
    if (!RocketTurretPolicy::proactiveCoverageTurn(pass.airEngaged,pass.uncoveredCoreAssets,
        nonServiceConstructionOrders)) return 0;
    if (pass.airEngaged) return shortfall;
    return pass.itemCount[Structure_RocketTurret]>=RocketTurretPolicy::coverageTurretCap(pass.coverageDemand)
        ? 0 : shortfall;
}

bool QuantBot::proactiveSavingHold(BuildPass& pass, int price) {
    // Establish a small R/C/I district, then interleave further plots with
    // services. Unlimited cheap fallback lots otherwise prevent even the
    // first turret upgrade or supplier from ever reaching its price.
    const int services=pass.itemCount[Structure_RocketTurret]+pass.itemCount[Structure_PoliceStation];
    const bool growthTurn=pass.cityZonesIncludingQueued < 3+2*services;
    return !growthTurn || pass.cashFlow.projectedCash < price+pass.cityPlotPrice;
}

size_t QuantBot::compositionSlot(Uint32 item) {
    for (size_t i = 0; i < kMixItems.size(); ++i) if (kMixItems[i] == item) return i;
    switch (item) {
    case Unit_EliteOrnithopter: return UnitMixPolicy::kOrnithopterSlot;
    case Unit_FlameQuad: case Unit_RaiderQuad: return UnitMixPolicy::kQuadSlot;
    case Unit_SonicTrike: return UnitMixPolicy::kFirstLightSlot;
    case Unit_RangedTank: case Unit_SlowTank: return 0;
    case Unit_ChemicalSiegeTank: return 1;
    case Unit_TameSandworm: return UnitMixPolicy::kFirstSpecialSlot;
    default: return UnitMixPolicy::kSlots;
    }
}

bool QuantBot::compositionAdmits(BuildPass& pass, Uint32 item) {
    if (!pass.techSavings || !QuantBotBuildPolicy::militaryItem(item)) return true;
    if (pass.militaryEmergency) return true;
    if (pass.techFundingPriority
        && pass.committedGroundUnits >= 6 && pass.committedGroundValue >= pass.defenceMinimumValue
        && !(pass.airEngaged && (CombatPowerPolicy::airCapableItem(int(item))
            || QuantBotBuildPolicy::rocketInfantry(item)))) return false;
    if (isInfantryUnit(item))
        return (pass.trooperSupport && QuantBotBuildPolicy::rocketInfantry(item) && pass.hostileAircraft > 0)
            || infantryQuotaAdmits(pass, currentGame->objectData.data[item][getHouse()->getHouseID()].price);
    const size_t slot = compositionSlot(item);
    return slot == UnitMixPolicy::kSlots
        || slot == UnitMixPolicy::nextExistingSlot(pass.unitMix, pass.compositionValue,
            pass.compositionPrice, pass.compositionAvailable);
}

bool QuantBot::spendingEmergency(BuildPass& pass, Uint32 item) {
    using Bucket = QuantBotSpendingPolicy::Bucket;
    const auto bucket = QuantBotBuildPolicy::spendingBucket(item);
    if (bucket == Bucket::Military)
        return pass.militaryEmergency
            || pass.readyGroundUnits < 6 || pass.readyGroundValue < pass.defenceMinimumValue
            || (pass.airEngaged && (CombatPowerPolicy::airCapableItem(int(item))
                || QuantBotBuildPolicy::rocketInfantry(item)));
    if (item == Structure_StarPort && getHouse()->getNumItems(item) == 0) return true;
    if ((item == Structure_WindTrap || item == Structure_NuclearPlant)
        && (!getHouse()->hasPower() || getHouse()->getProducedPower()
            < getHouse()->getPowerRequirement() + pass.committedPowerDemand)) return true;
    if (DuneCity::isCityZoneStructure(item) && cityBootstrapNeeded(pass)) return true;
    if (item == Structure_PoliceStation && pass.dangerousCrimeProperties > 0) return true;
    if ((item == Unit_Harvester || item == Unit_CapacityHarvester || item == Unit_FastHarvester
            || item == Structure_Refinery) && openingWorkersNeeded(pass)) return true;
    if (item == Unit_Carryall && needsFirstTransport(pass)) return true;
    if (item == Unit_MCV && (pass.itemCount[Structure_ConstructionYard] == 0
            || pass.rockExpansionNeeded || pass.colonisationNeeded)) return true;
    return item < Num_ItemID && pass.emergencyCivic[item];
}

bool QuantBot::incomeTechAdmits(BuildPass& pass, Uint32 item, int cost, bool upgrade) {
    if ((pass.techIncomeLevel == 0 && pass.cityTechRefineries == 0)
        || !QuantBotBuildPolicy::incomeTechnologyProject(item, upgrade, getHouse()->getNumItems(item))) return true;
    // Opening harvest/transport unlocks pay for the income being measured, and
    // must not be held behind that income. Construction/civics upgrades are not
    // optional military technology and are outside this gate altogether.
    if ((!upgrade && item == Structure_HighTechFactory && needsFirstTransport(pass))
        || (item == Structure_ResearchLab && pass.itemCount[Structure_HeavyFactory] == 0
            && pass.itemCount[Structure_LightFactory] == 0)) return true;
    if (upgrade && (item == Structure_HeavyFactory || item == Structure_HighTechFactory)) {
        const auto* producer = dynamic_cast<const BuilderBase*>(getObject(planningBuilder));
        const Uint32 worker = item == Structure_HeavyFactory ? Unit_Harvester : Unit_Carryall;
        const auto& spec = currentGame->objectData.data[worker][getHouse()->getHouseID()];
        const bool required = item == Structure_HeavyFactory ? openingFleetIncomplete(pass) : needsFirstTransport(pass);
        if (required && producer && producer->getItemID() == item && spec.enabled
            && spec.techLevel <= getHouse()->getTechLevel()
            && producer->getCurrentUpgradeLevel() < spec.upgradeLevel) return true;
    }
    if (getAIRefineryBays() < pass.cityTechRefineries) return false;
    return QuantBotBuildPolicy::incomeTechnologyAdmitted(pass.techIncomeLevel, pass.techIncomePerMinute, cost);
}

bool QuantBot::spendingAdmits(BuildPass& pass, Uint32 item, int cost, bool upgrade) {
    if (pass.cityBoomSpiceGate && !upgrade) {
        if (item == Unit_Harvester && pass.itemCount[Unit_Harvester] >= pass.cityBoomWorkerCap) return false;
        if (item == Structure_Refinery && (pass.itemCount[Structure_Refinery] >= pass.cityBoomBayCap
            || pass.itemCount[Unit_Harvester] >= pass.cityBoomWorkerCap)) return false;
    }
    if (!incomeTechAdmits(pass, item, cost, upgrade)) {
        if (pass.emitStatsLog && AITelemetry::log().enabled())
            traceDecision("income_tech_hold", AITelemetry::Record().set("item", item).set("cost", cost)
                .set("upgrade", upgrade).set("income_per_minute", pass.techIncomePerMinute)
                .set("level", pass.techIncomeLevel).set("refinery_bays", pass.cityTechRefineries));
        return false;
    }
    if (!pass.techSavings || cost <= 0) return true;
    if (pass.vanillaPlan != QuantBotVanillaPolicy::Plan::None) {
        if (!upgrade && item == pass.vanillaBoomRequiredItem) return true;
        if (upgrade && pass.vanillaBoomUpgradeBuilder != NONE_ID
            && planningBuilder == pass.vanillaBoomUpgradeBuilder) return true;
        const auto* builder = upgrade && pass.vanillaBoomArmy
            ? dynamic_cast<const BuilderBase*>(getObject(planningBuilder)) : nullptr;
        if (builder && vanillaBoomUpgradeAdmitted(pass, builder)) return true;
    }
    const auto bucket = upgrade ? QuantBotSpendingPolicy::Bucket::Production
        : QuantBotBuildPolicy::spendingBucket(item);
    const auto* producer = upgrade && (item == Structure_HeavyFactory || item == Structure_HighTechFactory)
        ? dynamic_cast<const BuilderBase*>(getObject(planningBuilder)) : nullptr;
    const auto& data = currentGame->objectData.data;
    const int house = getHouse()->getHouseID();
    const bool constructionRecovery = producer && item == Structure_HeavyFactory
        && (pass.itemCount[Structure_ConstructionYard] == 0 || pass.rockExpansionNeeded || pass.colonisationNeeded)
        && producer->getCurrentUpgradeLevel() < data[Unit_MCV][house].upgradeLevel;
    auto unlocks = [&](Uint32 unit) {
        const auto& spec = data[unit][house];
        if (!producer || producer->getItemID() != item || !spec.enabled || spec.builder != int(item)
            || spec.techLevel > getHouse()->getTechLevel()
            || producer->getCurrentUpgradeLevel() >= spec.upgradeLevel
            || spec.upgradeLevel > producer->getMaxUpgradeLevel()) return false;
        for (Uint32 prerequisite = Structure_FirstID; prerequisite < spec.prerequisiteStructuresSet.size(); ++prerequisite)
            if (isStructure(prerequisite) && spec.prerequisiteStructuresSet[prerequisite]
                && getHouse()->getNumItems(prerequisite) == 0) return false;
        return true;
    };
    const bool requiredProducerUnlock = (item == Structure_HeavyFactory
            && openingFleetIncomplete(pass) && unlocks(Unit_Harvester))
        || (item == Structure_HighTechFactory && needsFirstTransport(pass) && unlocks(Unit_Carryall));
    const bool emergency = upgrade ? item == Structure_ConstructionYard || constructionRecovery
        : spendingEmergency(pass, item);
    const bool ownsProject = pass.techResearch ? item == pass.techCandidate.item
        : planningBuilder == pass.techCandidate.builder
            && (upgrade ? pass.techUpgrade : !pass.techUpgrade && item == pass.techCandidate.item);
    const bool firstTechnology = !upgrade
        && (item == Structure_HeavyFactory || item == Structure_RepairYard || item == Structure_HighTechFactory
            || item == Structure_IX || item == Structure_Palace || item == Structure_ResearchLab)
        && getHouse()->getNumItems(item) == 0;
    const bool governedUpgrade = upgrade && (item == Structure_HeavyFactory || item == Structure_HighTechFactory);
    // Legacy overdue/wealth rules must not spend a different first technology
    // or upgrade while this one-project plan is saving or waiting for stability.
    const bool plannedTechnology = !(firstTechnology || governedUpgrade) || requiredProducerUnlock
        || (!upgrade && item == Structure_HighTechFactory && needsFirstTransport(pass))
        || (ownsProject && pass.techSavingActive && pass.techCandidateIndex >= 0 && !pass.techResearch);
    const bool admitted = emergency || plannedTechnology;
    if (!admitted && pass.emitStatsLog && AITelemetry::log().enabled())
        traceDecision("technology_plan_hold", AITelemetry::Record().set("item", item).set("cost", cost)
            .set("bucket", QuantBotSpendingPolicy::bucketName(bucket))
            .set("emergency", emergency).set("reason", "unplanned_technology"));
    return admitted;
}

void QuantBot::planTechSavings(BuildPass& pass) {
    if (!pass.techSavings) {
        // Counterfactual feasibility scans belong to the paired audit, not
        // ordinary telemetry: placement searches are material AI work.
        static const bool audit = [] {
            const char* value = std::getenv("DUNECITY_TECH_SAVINGS_AUDIT");
            return value && value[0] == '1' && value[1] == '\0';
        }();
        if (!audit || !AITelemetry::log().enabled()) return;
    }
    pass.militaryEmergency = false;
    if (pass.techSavings && (pass.defendingEconomy || pass.groundThreatAsset.isValid())) {
        const auto& data = currentGame->objectData.data;
        const int team = getHouse()->getTeamID();
        auto groundCombat = [](const UnitBase* unit) {
            return unit->getOwner() && unit->isActive() && unit->getHealth() > 0
                && unit->isAGroundUnit() && unit->canAttack()
                && QuantBotInternal::mobileCombatItem(unit->getItemID());
        };
        auto localDeficit = [&](Coord at) {
            int64_t hostile = 0, holding = 0;
            for (const auto* unit : getUnitList()) {
                if (!groundCombat(unit)
                    || blockDistance(at, unit->getLocation()) > ForceConcentrationPolicy::kPoolThreatTiles) continue;
                const int64_t power = CombatPowerPolicy::unitPower(
                    data[unit->getItemID()][unit->getProductionHouseID()].price,
                    unit->getHealth().lround(), unit->getMaxHealth());
                if (unit->getOwner()->getTeamID() != team) {
                    if (unit->isVisible(team)) hostile += power;
                } else if (orderableCombatUnit(unit) && !unit->isBadlyDamaged()
                    && unit->getAttackMode() != RETREAT
                    && unitRole(unit, {UnitRole::Repair}) == UnitRole::Free) holding += power;
            }
            const int64_t required = ForceConcentrationPolicy::sizedRequired(hostile);
            if (hostile <= 0 || holding >= required) return false;
            for (const auto* turret : getStructureList()) {
                if (turret->getOwner() != getHouse() || turret->getHealth() <= 0
                    || (turret->getItemID() != Structure_RocketTurret && turret->getItemID() != Structure_GunTurret)
                    || blockDistance(turret->getLocation(), at) > std::max(1, turret->getWeaponRange() - 1)) continue;
                holding += CombatPowerPolicy::turretPower(
                    data[turret->getItemID()][turret->getProductionHouseID()].price,
                    turret->getHealth().lround(), turret->getMaxHealth());
                if (holding >= required) return false;
            }
            return true;
        };
        // The same per-asset local strength and 1.5x ground response used by
        // defence, not a house-wide army or an aircraft elsewhere over the base.
        if (pass.groundThreatAsset.isValid()) pass.militaryEmergency = localDeficit(pass.groundThreatAsset);
        const ObjectBase* previousAsset = nullptr;
        if (!pass.militaryEmergency) for (const auto* unit : getUnitList()) {
            if (!groundCombat(unit) || unit->getOwner()->getTeamID() == team || !unit->isVisible(team)) continue;
            const auto* asset = unit->getTarget();
            if (!asset || asset == previousAsset || asset->getOwner() != getHouse() || asset->getHealth() <= 0
                || (!asset->isAStructure() && !QuantBotInternal::harvesterItem(asset->getItemID())
                    && asset->getItemID() != Unit_RebelHarvester)
                || !unit->isInWeaponRange(asset)) continue;
            previousAsset = asset;
            if (localDeficit(asset->getLocation())) { pass.militaryEmergency = true; break; }
        }
    }
    if (pass.vanillaFfaThreatProduction) pass.militaryEmergency = true;
    pass.defenceMinimumValue = int(std::min<int64_t>(INT_MAX,
        std::max<int64_t>(std::max(3000, minWaveValue()), int64_t(pass.groundThreatValue) * 3 / 2)));
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() != getHouse() || !unit->isActive() || unit->isBadlyDamaged()
            || unit->getHealth() <= 0 || !unit->isAGroundUnit() || !unit->canAttack()
            || unit->getItemID() == Unit_Saboteur || isSandwormUnit(unit->getItemID())) continue;
        ++pass.readyGroundUnits;
        pass.readyGroundValue += currentGame->objectData.data[unit->getItemID()][getHouse()->getHouseID()].price;
    }
    pass.committedGroundUnits += pass.readyGroundUnits;
    pass.committedGroundValue += pass.readyGroundValue;
    pass.techFundingPriority = pass.techSavings && pass.queuedTechnologyCost > 0;
    if (pass.techSavings) pass.strategicReserveCost = 0;
    const int house = getHouse()->getHouseID();
    const auto& data = currentGame->objectData.data;
    pass.techWorkingReserve = std::max(300, pass.citySimEnabled ? pass.cityWorkingReserve : 0);
    const bool incomeReady = pass.forecastNetIncome > 0 && !cityBootstrapNeeded(pass)
        && getHouse()->hasPower()
        && (!(pass.citySimEnabled || pass.powerRules)
            || getHouse()->getProducedPower() >= getHouse()->getPowerRequirement() + pass.committedPowerDemand)
        && !pass.militaryEmergency && pass.dangerousCrimeProperties == 0
        && getHouse()->getNumItems(Structure_ConstructionYard) > 0;
    const bool stable = incomeReady && !openingWorkersNeeded(pass)
        && !(needsFirstTransport(pass) && pass.carryallBuildAvailable);
    const bool missingSupplier = pass.itemCount[Structure_StarPort] == 0
        || pass.itemCount[Structure_HeavyFactory] == 0;
    const bool noCombatLane = UnitMixPolicy::nextExistingSlot(pass.unitMix, pass.compositionValue,
        pass.compositionPrice, pass.compositionAvailable) == UnitMixPolicy::kSlots;
    bool supplierResearch = false;
    if (pass.cityResearchAllowed && pass.techSavings && !pass.techFundingPriority && noCombatLane && researchLabPolicyActive() && getHouse()->getTechLevel() < 9) {
        // Research may unlock the first ground supplier, not require the army
        // that only that supplier can make. Derive its ceiling from enabled
        // units and the real prerequisite graph, including house/mod overrides.
        std::array<int, Num_ItemID> requiredTech;
        requiredTech.fill(-1);
        auto technology = [&](auto&& self, Uint32 item) -> int {
            int& result = requiredTech[item];
            if (result >= 0) return result;
            result = INT_MAX; // A cycle or disabled prerequisite is unreachable.
            if (isStructure(item) && getHouse()->getNumItems(item) > 0) return result = 0;
            const auto& spec = data[item][house];
            if (!spec.enabled) return result;
            int level = spec.techLevel;
            for (Uint32 prerequisite = Structure_FirstID; prerequisite < spec.prerequisiteStructuresSet.size(); ++prerequisite)
                if (isStructure(prerequisite) && spec.prerequisiteStructuresSet[prerequisite])
                    level = std::max(level, self(self, prerequisite));
            return result = level;
        };
        int firstCombatTech = INT_MAX;
        for (const Uint32 unit : kMixItems) {
            const int supplier = data[unit][house].builder;
            if (supplier != Structure_LightFactory && supplier != Structure_HeavyFactory) continue;
            firstCombatTech = std::min(firstCombatTech,
                std::max(technology(technology, unit), technology(technology, Uint32(supplier))));
        }
        supplierResearch = firstCombatTech <= 9 && getHouse()->getTechLevel() < firstCombatTech;
    }
    bool firstSupplier = false;
    auto structure = [&](Uint32 goal) {
        if (pass.vanillaFfaTechDeferred && (goal == Structure_IX || goal == Structure_Palace)) return false;
        if (pass.itemCount[goal] > 0 || !data[goal][house].enabled
            || data[goal][house].techLevel > getHouse()->getTechLevel()) return false;
        Uint32 item = goal;
        for (int depth = 0; depth < Num_ItemID; ++depth) {
            Uint32 missing = NONE_ID;
            // The persisted mask ends at Structure_LastID, not the extended item domain.
            for (Uint32 prerequisite = Structure_FirstID; prerequisite < data[item][house].prerequisiteStructuresSet.size(); ++prerequisite)
                if (isStructure(prerequisite) && data[item][house].prerequisiteStructuresSet[prerequisite]
                    && getHouse()->getNumItems(prerequisite) == 0) { missing = prerequisite; break; }
            if (missing == NONE_ID) break;
            if (pass.itemCount[missing] > 0 || missing == item) return false;
            item = missing;
            if (depth + 1 == Num_ItemID) return false;
        }
        if (pass.vanillaFfaStarportDeferred && item == Structure_StarPort) return false;
        if (!data[item][house].enabled || data[item][house].techLevel > getHouse()->getTechLevel()) return false;
        for (const auto* builder : pass.capitalBuilders) {
            if (builder->getItemID() != Structure_ConstructionYard || builder->isUpgrading()
                || builder->isOnHold() || builder->getHealth() <= 0) continue;
            CapitalCandidate candidate;
            candidate.builder = builder->getObjectID(); candidate.item = item;
            candidate.kind = "tech_progression"; candidate.reason = "next_legal_technology"; candidate.score = 1;
            const bool supplierGoal = (missingSupplier && (goal == Structure_StarPort || goal == Structure_HeavyFactory))
                || (noCombatLane && goal == Structure_LightFactory)
                || (supplierResearch && goal == Structure_ResearchLab);
            if (builder->getCurrentUpgradeLevel() < data[item][house].upgradeLevel
                && builder->isAllowedToUpgrade()) {
                candidate.kind = "tech_upgrade"; candidate.price = candidate.cost = builder->getUpgradeCost();
                if (candidate.price <= 0) continue;
                pass.techCandidate = candidate; pass.techUpgrade = true; firstSupplier = supplierGoal; return true;
            }
            if (!campaignAvailableToBuild(builder, item)) continue;
            if ((pass.citySimEnabled || pass.powerRules) && getHouse()->getProducedPower()
                < getHouse()->getPowerRequirement() + pass.committedPowerDemand + std::max(0, data[item][house].power))
                continue;
            planningBuilder = builder->getObjectID(); clearPlacementCache(false, true);
            candidate.site = findPlaceLocation(item);
            if (!candidate.site.isValid()) continue;
            const auto foundation = foundationPlan(builder, item, candidate.site);
            if (!foundation.complete) continue;
            candidate.price = purchasePrice(builder, item);
            candidate.cost = candidate.price;
            for (const auto& slab : foundation.orders) candidate.cost += purchasePrice(builder, slab.item);
            candidate.foundationCost = candidate.cost - candidate.price;
            if (candidate.price <= 0) continue;
            pass.techCandidate = candidate; firstSupplier = supplierGoal; return true;
        }
        return false;
    };
    auto upgrade = [&](Uint32 supplier) {
        for (const auto* builder : pass.capitalBuilders) {
            if (builder->getItemID() != supplier || !builder->isAllowedToUpgrade()
                || builder->isUpgrading() || builder->isOnHold()
                || builder->getHealth() < builder->getMaxHealth()) continue;
            for (Uint32 unit = Unit_FirstID; unit <= ItemID_LastID; ++unit) {
                const auto& spec = data[unit][house];
                if (!isUnit(unit) || !spec.enabled || spec.builder != int(supplier)
                    || spec.techLevel > getHouse()->getTechLevel()
                    || spec.upgradeLevel <= builder->getCurrentUpgradeLevel()
                    || spec.upgradeLevel > builder->getMaxUpgradeLevel()
                    || (!QuantBotBuildPolicy::militaryItem(unit) && unit != Unit_MCV)) continue;
                bool legal = true;
                for (Uint32 prerequisite = Structure_FirstID; prerequisite < spec.prerequisiteStructuresSet.size(); ++prerequisite)
                    if (isStructure(prerequisite) && spec.prerequisiteStructuresSet[prerequisite]
                        && getHouse()->getNumItems(prerequisite) == 0) { legal = false; break; }
                if (!legal) continue;
                pass.techCandidate.builder = builder->getObjectID(); pass.techCandidate.item = supplier;
                pass.techCandidate.kind = "tech_upgrade"; pass.techCandidate.reason = "unlock_higher_value_units";
                pass.techCandidate.price = pass.techCandidate.cost = builder->getUpgradeCost();
                pass.techCandidate.score = 1; pass.techUpgrade = true; return true;
            }
        }
        return false;
    };
    // A first local supplier is an opening-worker/defensive capability, not
    // optional growth behind an unfunded import prerequisite. First Starport
    // retains priority over the remaining optional progression.
    const bool coreHeavy = pass.itemCount[Structure_HeavyFactory] == 0
        && (openingWorkersNeeded(pass) || noCombatLane);
    const bool candidate = !pass.techFundingPriority && ((coreHeavy && structure(Structure_HeavyFactory))
        || (pass.starportMarketAvailable && structure(Structure_StarPort))
        || (!coreHeavy && structure(Structure_HeavyFactory))
        || (pass.techSavings && noCombatLane && structure(Structure_LightFactory))
        || upgrade(Structure_HeavyFactory)
        || (canAddRepairYard(pass.itemCount[Structure_RepairYard]) && structure(Structure_RepairYard))
        || structure(Structure_HighTechFactory) || structure(Structure_IX)
        || upgrade(Structure_HighTechFactory)
        || (pass.itemCount[Structure_Palace] == 0 && structure(Structure_Palace))
        || (pass.cityResearchAllowed && researchLabPolicyActive() && getHouse()->getTechLevel() < 9
            && !getHouse()->isTechResearchActive() && structure(Structure_ResearchLab)));
    if (pass.cityResearchAllowed && !candidate && !pass.techFundingPriority && researchLabPolicyActive() && getHouse()->getNumItems(Structure_ResearchLab) > 0
        && !getHouse()->isTechResearchActive() && getHouse()->getTechLevel() < 9) {
        for (const auto* lab : getStructureList())
            if (lab->getOwner() == getHouse() && lab->getItemID() == Structure_ResearchLab && lab->getHealth() > 0) {
                pass.techCandidate.builder = lab->getObjectID(); pass.techCandidate.item = Structure_ResearchLab;
                pass.techCandidate.kind = "tech_research"; pass.techCandidate.reason = "unlock_higher_technology";
                pass.techCandidate.price = pass.techCandidate.cost = getHouse()->getTechResearchCost();
                pass.techCandidate.score = 1; pass.techResearch = true; firstSupplier = supplierResearch; break;
            }
    }
    const bool feasible = pass.techCandidate.item != NONE_ID && pass.techCandidate.cost > 0;
    const bool simpleReady = !pass.militaryEmergency && !cityBootstrapNeeded(pass) && getHouse()->hasPower()
        && (firstSupplier || !openingWorkersNeeded(pass))
        && !(needsFirstTransport(pass) && pass.carryallBuildAvailable)
        && getHouse()->getNumItems(Structure_ConstructionYard) > 0;
    pass.techSavingActive = pass.techSavings && feasible && simpleReady;
    pass.techFundingPriority |= pass.techSavingActive;
    if (pass.techSavingActive && !pass.techResearch) {
        pass.techCandidateIndex = int(pass.capitalCandidates.size());
        pass.capitalCandidates.push_back(pass.techCandidate);
    }
    planningBuilder = NONE_ID; clearPlacementCache(false, true);
    if (AITelemetry::log().enabled()) traceDecision("tech_savings_plan", AITelemetry::Record().set("tech_savings", pass.techSavings)
        .set("savings_policy", pass.techSavings ? "installments" : "off")
        .set("income_buckets", 0).set("tech_funding", pass.techFundingPriority)
        .set("feasible", feasible).set("active", pass.techSavingActive).set("stable", stable)
        .set("candidate_kind", !feasible ? "none" : pass.techResearch ? "research" : pass.techUpgrade ? "upgrade" : "structure")
        .set("item", pass.techCandidate.item).set("builder", pass.techCandidate.builder)
        .set("cost", pass.techCandidate.cost).set("cash", getHouse()->getCredits()).set("spendable", pass.money)
        .set("committed_cost", pass.queuedProductionCost).set("forecast_net_income", pass.forecastNetIncome)
        .set("horizon_cycles", CityEconomyInvestmentPolicy::horizonCycles)
        .set("working_reserve", pass.techWorkingReserve).set("defence_minimum", pass.defenceMinimumValue)
        .set("ready_ground_units", pass.readyGroundUnits).set("ready_ground_value", pass.readyGroundValue));
}

void QuantBot::planCapitalCandidates(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    constexpr auto& mixItems = kMixItems;
    if (pass.cityLandValueServices && cityFirstPoliceAllocationReady()
        && !pass.militaryEmergency && !pass.defendingEconomy && !pass.airEngaged
        && !pass.groundThreatAsset.isValid() && pass.civicStructuralNeed==0
        && getHouse()->hasPower()
        && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()
            >=pass.committedPowerDemand+std::max(0,data[Structure_PoliceStation][houseID].power)) {
        int policeMilli=0;
        for (Uint32 item:{Structure_PoliceStation,Structure_GunTurret,Structure_RocketTurret})
            policeMilli+=(pass.itemCount[item]+(item==Structure_PoliceStation ? 1 : 0))
                *DuneCity::annualPerMinute((DuneCity::getPoliceAnnualCost(item)*1000).lround());
        const int cost=buildingCapitalCost(pass,Structure_PoliceStation);
        const int working=std::max({2000,pass.cityWorkingReserve,cityBootstrapReserve(pass)});
        const auto budget=cityLandValueBudget(std::max(0,pass.money-cost),(policeMilli+999)/1000);
        if (int64_t(pass.money)>=int64_t(working)+cost && budget.funding==budget.targetFunding)
            for (const auto* builder:pass.capitalBuilders) {
                if (builder->getItemID()!=Structure_ConstructionYard || builder->isUpgrading()
                    || builder->isOnHold() || builder->getProductionQueueSize()>0
                    || !campaignAvailableToBuild(builder,Structure_PoliceStation)) continue;
                planningBuilder=builder->getObjectID();clearPlacementCache(false,true);
                CapitalCandidate police;
                if (!selectCityServiceInvestment(builder,pass.money,false,police.item,police.site,
                    false,Structure_PoliceStation)) continue;
                police.builder=builder->getObjectID();police.price=purchasePrice(builder,police.item);
                police.cost=cost;police.kind="city_land_value_service";police.reason="first_police_allocation";
                police.score=1; // Explicit one-station allocation, not a return forecast.
                pass.cityLandValuePoliceCandidate=int(pass.capitalCandidates.size());
                pass.capitalCandidates.push_back(police);
                break;
            }
    }
    // A bounded first-WOR allocation uses the same capital reservation as the
    // other suppliers. Requiring its whole price before it may claim savings
    // lets repeat zones and vehicle orders consume every incoming credit.
    if (pass.trooperSupport) for (const auto* builder : pass.capitalBuilders) {
        if (builder->getItemID() != Structure_ConstructionYard || builder->isUpgrading()
            || builder->isOnHold() || builder->getProductionQueueSize() > 0) continue;
        planningBuilder = builder->getObjectID(); clearPlacementCache(false, true);
        const Uint32 item = trooperInfrastructureItem(pass, builder, false);
        if (item == NONE_ID) continue;
        CapitalCandidate infantry;
        infantry.builder = builder->getObjectID(); infantry.item = item;
        infantry.kind = "trooper_support"; infantry.reason = "first_wor";
        infantry.price = purchasePrice(builder, item);
        infantry.cost = buildingCapitalCost(pass, item);
        infantry.site = findPlaceLocation(item);
        infantry.score = 1; // One-off allocation, not a fabricated return forecast.
        pass.capitalCandidates.push_back(infantry);
        break;
    }
    // Fund one genuine floor order through the same supplier allocation. A
    // military-only cash guard cannot stop repeat zones consuming its money.
    if (pass.trooperSupport && pass.trooperReserveValue > 0 && getHouse()->hasPower()
        && pass.forecastNetIncome > 0 && !openingWorkersNeeded(pass) && !needsFirstTransport(pass)
        && pass.civicStructuralNeed == 0 && pass.civicReserveCost == 0 && cityBootstrapReserve(pass) == 0) {
        const int floor = QuantBotBuildPolicy::trooperSupportFloor(pass.hostileAircraft);
        for (const auto* builder : pass.capitalBuilders) {
            if (builder->getItemID() != Structure_WOR || builder->isOnHold()) continue;
            const Uint32 item = supportTrooperOrder(pass, builder, INT_MAX, floor);
            if (item == NONE_ID) continue;
            CapitalCandidate infantry;
            infantry.builder = builder->getObjectID(); infantry.item = item;
            infantry.kind = "trooper_recruitment"; infantry.reason = "trooper_floor";
            infantry.price = purchasePrice(builder, item);
            infantry.cost = infantry.price;
            infantry.score = 1;
            pass.capitalCandidates.push_back(infantry);
            break;
        }
    }
    if (pass.sharedSpending) for (const auto* builder:pass.capitalBuilders) {
        const Uint32 building=builder->getItemID();
        const bool port=building==Structure_StarPort;
        const bool ready=!builder->isUpgrading() && !builder->isOnHold()
            && builder->getProductionQueueSize()==0
            && (!port || static_cast<const StarPort*>(builder)->okToOrder());
        if (building==Structure_ConstructionYard) {
            if (!ready || gameMode!=GameMode::Custom) continue;
            planningBuilder=builder->getObjectID(); clearPlacementCache(false,true);
            // An expansion must unlock and fund its three turrets before its
            // own queue becomes a cheap-zoning lane. Other secured yards can
            // continue growing the economy alongside it.
            if (pass.citySimEnabled && pass.coreInfrastructureReady && expansionTurretsMissing(builder)>0
                && data[Structure_RocketTurret][houseID].enabled
                && data[Structure_RocketTurret][houseID].techLevel<=getHouse()->getTechLevel()) {
                CapitalCandidate defence;
                defence.builder=builder->getObjectID();defence.item=Structure_RocketTurret;
                defence.kind="expansion_defence";defence.reason="secure_expansion";defence.score=5500;
                if (builder->getCurrentUpgradeLevel()<data[Structure_RocketTurret][houseID].upgradeLevel
                    && builder->getMaxUpgradeLevel()>=data[Structure_RocketTurret][houseID].upgradeLevel) {
                    defence.kind="expansion_upgrade";
                    defence.price=defence.cost=builder->getUpgradeCost();
                    if (defence.price>0) pass.capitalCandidates.push_back(defence);
                } else if (campaignAvailableToBuild(builder,Structure_RocketTurret)) {
                    if (pass.turretPowerRequired && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<pass.rocketPowerBuffer)
                        defence.item=Structure_WindTrap;
                    if (campaignAvailableToBuild(builder,defence.item)) {
                        defence.site=defence.item==Structure_RocketTurret ? findCityTurretPlaceLocation(defence.item)
                            : findPlaceLocation(defence.item);
                        defence.price=data[defence.item][houseID].price;defence.cost=buildingCapitalCost(pass, defence.item);
                        if (defence.site.isValid()) pass.capitalCandidates.push_back(defence);
                    }
                }
            }
            if (botConfig().force.cityAdvancedTech && pass.citySimEnabled
                && !campaignPolicyApplies() && lastCalculatedSpice > 0
                && getHouse()->getTechLevel() == 9 && pass.itemCount[Structure_IX] == 0
                && pass.itemCount[Structure_HeavyFactory] > 0 && pass.itemCount[Structure_RepairYard] > 0
                && !openingFleetIncomplete(pass) && !pass.defendingEconomy && !pass.airEngaged
                && getHouse()->hasPower() && pass.forecastNetIncome > 0) {
                Uint32 step = Structure_IX;
                for (int depth = 0; depth < Structure_LastID; ++depth) {
                    Uint32 missing = NONE_ID;
                    for (Uint32 prerequisite = Structure_FirstID; prerequisite <= Structure_LastID; ++prerequisite)
                        if (data[step][houseID].prerequisiteStructuresSet[prerequisite]
                            && pass.itemCount[prerequisite] == 0) { missing = prerequisite; break; }
                    if (missing == NONE_ID) break;
                    step = missing;
                }
                if (campaignAvailableToBuild(builder, step) && getHouse()->getProducedPower()
                    >= getHouse()->getPowerRequirement() + pass.committedPowerDemand
                        + std::max(0, data[step][houseID].power) + 24) {
                    CapitalCandidate technology;
                    technology.builder = builder->getObjectID(); technology.item = step;
                    technology.kind = "city_advanced_tech"; technology.reason = "complete_high_tech";
                    technology.price = purchasePrice(builder, step);
                    technology.cost = buildingCapitalCost(pass, step);
                    technology.foundationCost = getGameInitSettings().getGameOptions().concreteRequired
                        ? getStructureSize(step).x * getStructureSize(step).y * data[Structure_Slab1][houseID].price : 0;
                    technology.site = findPlaceLocation(step);
                    if (technology.site.isValid()
                        && pass.forecastFunding >= technology.cost + pass.cashBuffer) {
                        technology.score = 4200;
                        pass.capitalCandidates.push_back(technology);
                    }
                }
            }
            CapitalCandidate economy;
            economy.builder=builder->getObjectID();
            if (pass.citySimEnabled) {
                economy.item=chooseCityEconomy(pass, builder,false);
                economy.cost=pass.lastEconomyInvestment.cost;
                economy.proceeds=pass.lastEconomyInvestment.proceeds();
                economy.delay=pass.lastEconomyInvestment.delayCycles;
                // The lot's amortized power charge is not a generator purchase.
                // Protect its next real prerequisite as the growth investment:
                // otherwise a cheap Starport unit wins every time the city
                // approaches the windtrap price, and zoning never resumes.
                if (botConfig().force.cityGrowthPower && DuneCity::isCityZoneStructure(economy.item)
                    && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<24
                    && !powerGenerationPending(pass)) {
                    Uint32 generator=NONE_ID;
                    Coord site=Coord::Invalid();
                    for (Uint32 item:{Structure_WindTrap,Structure_NuclearPlant}) {
                        if (!campaignAvailableToBuild(builder,item)) continue;
                        const Coord at=findPlaceLocation(item);
                        if (!at.isValid()) continue;
                        if (generator!=NONE_ID) {
                            if (pass.money<buildingCapitalCost(pass,item)) continue;
                            if (botConfig().force.cityPowerSizing) {
                                const int windOutput=std::max(1,-data[Structure_WindTrap][houseID].power);
                                const int need=std::max(1,getHouse()->getPowerRequirement()+pass.cityPowerReserve
                                    -getHouse()->getProducedPower());
                                const int windNeeded=(need+windOutput-1)/windOutput;
                                const int sites=availableBaseFootprints>=QuantBotColonisationPolicy::kCrampedFootprints
                                    ? std::max(availableBaseFootprints,windNeeded) : std::max(1,availableBaseFootprints);
                                if (!QuantBotBuildPolicy::preferNuclearPower(need,windOutput,
                                        data[Structure_WindTrap][houseID].price,data[Structure_NuclearPlant][houseID].price,
                                        sites,pass.money,std::max({1000,pass.cityWorkingReserve,cityBootstrapReserve(pass)}),
                                        !getHouse()->hasPower())) continue;
                            }
                        }
                        generator=item;site=at;
                    }
                    if (generator!=NONE_ID) {
                        economy.item=generator;economy.site=site;
                        economy.cost=buildingCapitalCost(pass,generator);
                        economy.foundationCost=economy.cost-data[generator][houseID].price;
                        economy.delay=data[generator][houseID].buildtime*15;
                        economy.kind="city_growth_power";
                    }
                }
            } else if (campaignAvailableToBuild(builder,Structure_Refinery)
                && findPlaceLocation(Structure_Refinery).isValid()) {
                forecastSpiceTrip(pass, findPlaceLocation(Structure_Refinery));
                const int workers=pass.itemCount[Unit_Harvester], refs=pass.itemCount[Structure_Refinery];
                const bool freeWorker=workers<pass.desiredWorkers;
                const int before=std::min(workers*workerAnnualIncome(pass),refs*pass.bayAnnualIncome);
                const int after=std::min((workers+int(freeWorker))*workerAnnualIncome(pass),(refs+1)*pass.bayAnnualIncome);
                economy.item=Structure_Refinery;
                economy.cost=buildingCapitalCost(pass, economy.item);
                economy.delay=data[economy.item][houseID].buildtime*15;
                economy.proceeds=QuantBotSpendingPolicy::marginalSpice(pass.spiceShare,before,after,
                    economy.delay,CityEconomyInvestmentPolicy::horizonCycles,DuneCity::kCyclesPerMinute);
                if (pass.refineryFieldRisk>0) economy.proceeds/=2;
                const bool useful=CityEconomyInvestmentPolicy::considerRefinery(pass.unloadingBacklog
                    || CityEconomyInvestmentPolicy::processingCapacityNeeded(refs,workers,workerAnnualIncome(pass),pass.bayAnnualIncome),
                    freeWorker,!pass.harvesterFactories.empty(),workers<2);
                if (!useful) economy.proceeds=0;
                if (pass.unloadingBacklog) economy.proceeds=std::max(economy.proceeds,std::min(pass.waitingCargo,2*int(HARVESTERMAXSPICE)));
            }
            if (economy.item!=NONE_ID) {
                economy.price=data[economy.item][houseID].price;
                economy.score=QuantBotSpendingPolicy::economyScore(economy.proceeds,economy.cost);
                economy.reason=std::string_view(economy.kind)=="city_growth_power" ? "unlock_demanded_city_growth"
                    : economy.score>0 ? "marginal_income" : "no_marginal_income";
                if (pass.cityBoomFunding
                    && ((economy.item == Structure_Refinery && pass.itemCount[economy.item] < pass.cityBoomBays)
                        || (DuneCity::isCityZoneStructure(economy.item)
                            && (pass.cityBoom == CityBoomKind::ShortTax || pass.cityBoom == CityBoomKind::Reactive)))) {
                    economy.score = std::max(economy.score, 5000);
                    economy.reason = "city_boom_allocation"; // Keep the real return forecast, do not inflate it.
                }
                pass.capitalCandidates.push_back(economy);
            }
            if (pass.fleetRatios && pass.itemCount[Structure_Refinery] < pass.fleetRefineryTarget
                && campaignAvailableToBuild(builder, Structure_Refinery)) {
                CapitalCandidate capacity;
                capacity.builder = builder->getObjectID();
                capacity.item = Structure_Refinery;
                capacity.kind = "fleet_ratio_capacity";
                capacity.reason = "selected_workers_per_bay";
                capacity.site = findPlaceLocation(Structure_Refinery);
                capacity.price = purchasePrice(builder, Structure_Refinery);
                capacity.cost = buildingCapitalCost(pass, Structure_Refinery);
                if (capacity.site.isValid() && pass.money >= capacity.cost + 300) {
                    capacity.score = 3500;
                    pass.capitalCandidates.push_back(capacity);
                }
            }
            if (pass.groundThreatAsset.isValid()
                && campaignAvailableToBuild(builder,Structure_RocketTurret)) {
                CapitalCandidate defence;
                defence.builder=builder->getObjectID();defence.item=Structure_RocketTurret;
                defence.kind="ground_defence";defence.reason="ground_threat";
                // The finite core goal must stand before optional spending:
                // a supplier built behind only a rear pair and two new guns
                // was lost with the base. The house-wide cap bounds this claim.
                defence.urgent=true;defence.score=6500;
                if (pass.turretPowerRequired && !hasPowerBufferForTurret(pass)) {
                    defence.item=powerGenerationPending(pass) ? Uint32(NONE_ID) : Uint32(Structure_WindTrap);
                }
                if (defence.item!=NONE_ID && campaignAvailableToBuild(builder,defence.item)) {
                    defence.site=defence.item==Structure_RocketTurret
                        ? findThreatTurretPlaceLocation(pass.groundThreatAsset,pass.groundThreatApproach)
                        : findCoveredDefencePlaceLocation(defence.item,builder->getLocation());
                    defence.price=data[defence.item][houseID].price;defence.cost=buildingCapitalCost(pass,defence.item);
                    if (defence.site.isValid()) pass.capitalCandidates.push_back(defence);
                }
            }
            // The fallback Starport rule never wins a low-cash city's yard:
            // repeated plots and the unprotected first-heavy saving consume its
            // price first. One useful, income-funded supplier is a finite project.
            if (botConfig().force.cityStarportSupply && pass.citySimEnabled
                && !cityBootstrapNeeded(pass) && pass.cityZonesIncludingQueued>=4
                && pass.itemCount[Structure_StarPort]==0 && pass.starportMarketAvailable
                && pass.forecastTax>pass.forecastUpkeep
                && (pass.trooperSupport ? pass.forecastNetIncome>0
                    : pass.forecastFunding>=buildingCapitalCost(pass,Structure_StarPort))
                && campaignAvailableToBuild(builder,Structure_StarPort)) {
                CapitalCandidate supplier;
                supplier.builder=builder->getObjectID();supplier.item=Structure_StarPort;
                supplier.kind="city_supply";supplier.reason="first_starport_supply";supplier.score=5500;
                // Once a real battery stands, fund its mobile reinforcement
                // source once; replacing outlying covers must not own all cash.
                if (pass.groundThreatAsset.isValid()
                    && getHouse()->getNumItems(Structure_RocketTurret)>=RocketTurretPolicy::threatTurretTotalCap/2
                    && campaignAvailableToBuild(builder,Structure_RocketTurret)
                    && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()
                        >=std::max(0,data[supplier.item][houseID].power)+24)
                    supplier.score=7000;
                supplier.price=data[supplier.item][houseID].price;supplier.cost=buildingCapitalCost(pass,supplier.item);
                supplier.site=findCoveredDefencePlaceLocation(supplier.item,builder->getLocation());
                if (supplier.site.isValid()) pass.capitalCandidates.push_back(supplier);
            }
            // Tax-only towns need a small standing battery even when two starting
            // guns overlap every current asset. Fund the complete next rocket,
            // not just its first 200-credit yard upgrade, from healthy income.
            const int cityZones=pass.itemCount[Structure_ZoneResidential]+pass.itemCount[Structure_ZoneCommercial]
                +pass.itemCount[Structure_ZoneIndustrial];
            const bool groundDefenceDue=pass.groundThreatAsset.isValid();
            const bool taxBattery=pass.citySimEnabled && pass.spiceShare<=0 && pass.desiredWorkers==0;
            const bool extraTaxCover=taxBattery && pass.earlyRocketLimit>2
                && pass.itemCount[Structure_RocketTurret]>=2 && !pass.routineRocketsUnrestricted;
            const int earlyBuffer=std::max(0,pass.cityPlotPrice);
            bool earlyRocketDue=false;
            if (taxBattery && pass.forecastNetIncome>0 && pass.earlyRocketLimit>2
                && pass.itemCount[Structure_RocketTurret]<pass.earlyRocketLimit
                && !pass.defendingEconomy && !pass.airEngaged && !groundDefenceDue
                && !openingWorkersNeeded(pass) && !pass.openingSupplierDue && cityZones>=4
                && builder->getHealth()>=builder->getMaxHealth()
                && data[Structure_RocketTurret][houseID].enabled
                && data[Structure_RocketTurret][houseID].techLevel<=getHouse()->getTechLevel()
                && builder->getMaxUpgradeLevel()>=data[Structure_RocketTurret][houseID].upgradeLevel
                && getHouse()->hasPower() && hasPowerBufferForTurret(pass)
                && RocketTurretPolicy::proactiveCoverageTurn(false,pass.uncoveredCoreAssets,nonServiceConstructionOrders)) {
                const int upgrades=std::max(0,data[Structure_RocketTurret][houseID].upgradeLevel
                    -int(builder->getCurrentUpgradeLevel()));
                int64_t investment=int64_t(upgrades)*builder->getUpgradeCost()
                    +data[Structure_RocketTurret][houseID].price;
                // Existing power already covers this rocket. Its cash quote
                // needs concrete, not the ROI quote's amortized new generation.
                if (getGameInitSettings().getGameOptions().concreteRequired) {
                    const Coord size=getStructureSize(Structure_RocketTurret);
                    investment+=int64_t(size.x)*size.y*data[Structure_Slab1][houseID].price;
                }
                earlyRocketDue=investment>0 && int64_t(pass.forecastFunding)>=investment+earlyBuffer;
                // Do not start an optional, only partly priced prerequisite chain.
                // Already-pending prerequisites are funded in queuedProductionCost.
                if (earlyRocketDue) for (Uint32 prerequisite=Structure_FirstID; prerequisite<=Structure_LastID; ++prerequisite)
                    if (data[Structure_RocketTurret][houseID].prerequisiteStructuresSet[prerequisite]
                        && pass.itemCount[prerequisite]==0) { earlyRocketDue=false;break; }
            }
            // Services must be allowed to save their price, not wait for money
            // left after every factory has placed another order. Use the existing
            // marginal coverage search; pending services are included in it.
            if (pass.citySimEnabled && getHouse()->hasPower()) {
                CapitalCandidate service;
                const int coreShortfall=proactiveCoverageShortfall(pass);
                service.builder=builder->getObjectID();service.kind="civic";
                if (pass.moderateCrimeProperties>0 && selectCityServiceInvestment(builder,
                    std::max(data[Structure_PoliceStation][houseID].price,data[Structure_RocketTurret][houseID].price),
                    true,service.item,service.site)) {
                    service.score=pass.dangerousCrimeProperties>0 ? 6000 : 2500;
                    service.reason=pass.dangerousCrimeProperties>0 ? "crime_prevention" : "crime_maintenance";
                    service.urgent=pass.dangerousCrimeProperties>0;
                } else if ((pass.airEngaged || coreShortfall>0
                        || (!openingWorkersNeeded(pass) && pass.itemCount[Structure_Refinery]>0))
                    && (pass.airEngaged || pass.uncoveredCoreAssets>0
                        || nonServiceConstructionOrders>=3 || pass.itemCount[Structure_RocketTurret]==0)
                    && (!pass.openingSupplierDue || pass.airEngaged)
                    && pass.routineRocketsAllowed
                    && (!extraTaxCover || pass.airEngaged || earlyRocketDue)
                    && campaignAvailableToBuild(builder,Structure_RocketTurret)
                    && (!pass.turretPowerRequired || getHouse()->getProducedPower()-getHouse()->getPowerRequirement()>=pass.rocketPowerBuffer)) {
                    int uncovered=0;
                    service.site=findCityTurretPlaceLocation(Structure_RocketTurret,&uncovered);
                    if (service.site.isValid() && uncovered>0) {
                        // Aircraft already in play outrank peaceful coverage, and
                        // an uncovered yard/refinery/factory outranks covering an
                        // ordinary building, but both stay below crime prevention.
                        service.item=Structure_RocketTurret;
                        service.score=pass.airEngaged ? 3000 : pass.uncoveredCoreAssets>0 ? 2600 : 2000;
                        service.reason=pass.airEngaged ? "air_coverage"
                            : pass.uncoveredCoreAssets>0 ? "core_coverage" : "uncovered_base";
                        service.urgent=pass.airEngaged;
                    }
                }
                if (service.item!=NONE_ID && service.site.isValid()) {
                    service.price=data[service.item][houseID].price;service.cost=buildingCapitalCost(pass, service.item);
                    pass.capitalCandidates.push_back(service);
                }
            }
            // force.defencePrerequisites: demanded rocket cover this yard cannot
            // build yet saves for its next unlock step, the yard upgrade and then
            // the first missing prerequisite (the Radar), instead of leaving every
            // credit to plots and Starport bargains until aircraft are over the
            // base. The turret candidates above require the turret to be buildable,
            // so without this a city whose refinery count never reaches the
            // proactive unlock rule's three (a no-spice city keeps one storage
            // refinery) owns no rocket defence when the first wing arrives.
            // Proactive cover waits for a four-zone city opening (a low-cash
            // no-spice city otherwise saved for the upgrade from its third zone
            // on and never grew); aircraft over the base never wait.
            if (earlyRocketDue && campaignAvailableToBuild(builder,Structure_RocketTurret)) {
                CapitalCandidate early;
                early.builder=builder->getObjectID();early.item=Structure_RocketTurret;
                early.kind="early_rocket";early.reason="early_rocket";early.score=3500;
                early.price=data[early.item][houseID].price;early.cost=buildingCapitalCost(pass,early.item);
                early.site=pass.citySimEnabled && pass.uncoveredCoreAssets>0
                    ? findCityTurretPlaceLocation(early.item) : findFrontBatteryPlaceLocation();
                if (early.site.isInvalid() && pass.citySimEnabled && pass.uncoveredCoreAssets>0)
                    early.site=findFrontBatteryPlaceLocation();
                if (early.site.isValid()) pass.capitalCandidates.push_back(early);
            }
            const bool ordinaryUnlockDue=(pass.airEngaged || (pass.uncoveredCoreAssets>0 && cityZones>=4
                    && (!extraTaxCover || earlyRocketDue))) && pass.routineRocketsAllowed;
            if ((botConfig().force.defencePrerequisites || groundDefenceDue) && pass.citySimEnabled && getHouse()->hasPower()
                && (groundDefenceDue || ordinaryUnlockDue || earlyRocketDue)
                && data[Structure_RocketTurret][houseID].enabled
                && data[Structure_RocketTurret][houseID].techLevel<=getHouse()->getTechLevel()
                && !campaignAvailableToBuild(builder,Structure_RocketTurret)) {
                int uncovered=0;
                Coord defenceSite=groundDefenceDue
                    ? findThreatTurretPlaceLocation(pass.groundThreatAsset,pass.groundThreatApproach)
                    : findCityTurretPlaceLocation(Structure_RocketTurret,&uncovered);
                if (defenceSite.isInvalid() && earlyRocketDue) defenceSite=findFrontBatteryPlaceLocation();
                if (defenceSite.isValid() && (groundDefenceDue || uncovered>0 || earlyRocketDue)) {
                    CapitalCandidate unlock;
                    unlock.builder=builder->getObjectID();
                    unlock.score=groundDefenceDue ? 6500 : pass.airEngaged ? 3000 : 2600;
                    unlock.reason=groundDefenceDue ? "ground_threat" : pass.airEngaged ? "air_coverage" : "core_coverage";
                    unlock.urgent=groundDefenceDue || pass.airEngaged;
                    const int rocketUpgrade=data[Structure_RocketTurret][houseID].upgradeLevel;
                    if (builder->getCurrentUpgradeLevel()<rocketUpgrade) {
                        if (builder->getMaxUpgradeLevel()>=rocketUpgrade) {
                            unlock.kind="defence_upgrade";unlock.item=Structure_RocketTurret;
                            unlock.price=unlock.cost=builder->getUpgradeCost();
                        }
                    } else {
                        // First missing prerequisite this yard can order, walking down
                        // through prerequisites of prerequisites. A pending order counts.
                        Uint32 step=Structure_RocketTurret;
                        for (int depth=0; depth<Structure_LastID && step!=NONE_ID; ++depth) {
                            Uint32 missing=NONE_ID;
                            for (Uint32 prerequisite=Structure_FirstID; prerequisite<=Structure_LastID; ++prerequisite)
                                if (data[step][houseID].prerequisiteStructuresSet[prerequisite]
                                    && pass.itemCount[prerequisite]==0) { missing=prerequisite; break; }
                            if (missing==NONE_ID) break;
                            if (campaignAvailableToBuild(builder,missing)) {
                                unlock.kind="defence_prerequisite";unlock.item=missing;
                                unlock.site=groundDefenceDue
                                    ? findCoveredDefencePlaceLocation(missing,builder->getLocation())
                                    : findPlaceLocation(missing);
                                unlock.price=data[missing][houseID].price;unlock.cost=buildingCapitalCost(pass, missing);
                                break;
                            }
                            step=missing;
                        }
                    }
                    if (earlyRocketDue && unlock.price>0) {
                        unlock.reason="early_rocket";unlock.score=3500;
                    }
                    if (unlock.price>0 && (groundDefenceDue || ordinaryUnlockDue
                            || std::string_view(unlock.reason)=="early_rocket")
                        && (unlock.kind==std::string("defence_upgrade") || unlock.site.isValid()))
                        pass.capitalCandidates.push_back(unlock);
                }
            }
            if (pass.itemCount[Structure_RepairYard]>0 && pass.itemCount[Structure_RepairYard]<pass.repairTarget
                && canAddRepairYard(pass.itemCount[Structure_RepairYard]) && !openingWorkersNeeded(pass)
                && campaignAvailableToBuild(builder,Structure_RepairYard)) {
                CapitalCandidate repair;
                repair.builder=builder->getObjectID();repair.item=Structure_RepairYard;repair.kind="repair";
                repair.price=data[repair.item][houseID].price;repair.cost=buildingCapitalCost(pass, repair.item);
                repair.site=findPlaceLocation(repair.item);
                if (repair.site.isValid()) {
                    repair.score=pass.waitingRepairVehicles>=2 ? 3500 : 1800;
                    repair.reason=pass.waitingRepairVehicles>=2 ? "repair_queue" : "repair_fleet_ratio";
                }
                pass.capitalCandidates.push_back(repair);
            }
            // A sold-out Starport cannot meet a growing transport target.
            // Fund the first local supplier before optional factory expansion
            // or repeat zoning; pending High Tech factories already cover it.
            if (pass.itemCount[Unit_Carryall]<pass.transportTarget && pass.itemCount[Structure_HighTechFactory]==0
                && pass.itemCount[Structure_HeavyFactory]>0 && !openingWorkersNeeded(pass)
                && (getHouse()->getChoam().getNumAvailable(Unit_Carryall)<=0 || pass.itemCount[Structure_StarPort]==0)
                && ((pass.actualHarvesters>0 && lastCalculatedSpice>0)
                    || (pass.combatVehicles>0 && getHouse()->getNumItems(Structure_RepairYard)>0))
                && !getHouse()->isAirUnitLimitReached()
                && campaignAvailableToBuild(builder,Structure_HighTechFactory)) {
                CapitalCandidate supplier;
                supplier.builder=builder->getObjectID();supplier.item=Structure_HighTechFactory;
                supplier.kind="transport_production";supplier.price=data[supplier.item][houseID].price;
                supplier.cost=buildingCapitalCost(pass, supplier.item);supplier.site=findPlaceLocation(supplier.item);
                if (supplier.site.isValid()) {
                    supplier.score=pass.itemCount[Unit_Carryall]==0 ? 5500 : 4500;
                    supplier.reason="transport_imports_unavailable";
                    pass.capitalCandidates.push_back(supplier);
                }
            }
            for (Uint32 factory:{Structure_LightFactory,Structure_HeavyFactory,Structure_HighTechFactory}) {
                CapitalCandidate capacity;
                capacity.builder=builder->getObjectID();capacity.item=factory;capacity.kind="production";
                capacity.price=data[factory][houseID].price;capacity.cost=buildingCapitalCost(pass, factory);
                const int actual=getHouse()->getNumItems(factory);
                const int busy=factory==Structure_LightFactory ? pass.activeLightFactoryCount
                    : factory==Structure_HeavyFactory ? pass.activeHeavyFactoryCount : pass.activeHighTechFactoryCount;
                capacity.delay=data[factory][houseID].buildtime*15;
                const int added=int(int64_t(pass.laneCapacity[factory])
                    * std::max(0,CityEconomyInvestmentPolicy::horizonCycles-capacity.delay)
                    / CityEconomyInvestmentPolicy::horizonCycles);
                capacity.capacity=QuantBotSpendingPolicy::additionalProduction(pass.militaryFunding,pass.existingMilitaryCapacity,
                    added,std::max(0,pass.militaryBudget-pass.militaryValue),capacity.cost);
                if (actual==0) {
                    capacity.reason="bootstrap_managed_separately";
                    // The seed economy comes first. Finalize the full founded
                    // quote below: only an already-funded multi-yard bootstrap
                    // may save for this first supplier before it is affordable.
                    if (factory==Structure_HeavyFactory && pass.citySimEnabled
                        && botConfig().force.cityConstructionCapital && !pass.spiceEconomyViable
                        && difficulty>=Difficulty::Hard
                        && pass.itemCount[Structure_StarPort]>0
                        && !builderOnly() && !campaignPolicyApplies()
                        && pass.itemCount[factory]==0 && !cityBootstrapNeeded(pass)
                        && pass.forecastTax>pass.forecastUpkeep
                        && !pass.defendingEconomy && !pass.airEngaged
                        && campaignAvailableToBuild(builder,factory)
                        // Keep the existing city-growth floor after the factory's draw.
                        && (!pass.powerRules || getHouse()->getProducedPower()-getHouse()->getPowerRequirement()
                            >=std::max(0,data[factory][houseID].power)+24)) {
                        capacity.site=findPlaceLocation(factory);
                        if (capacity.site.isValid()) {
                            capacity.kind="city_construction";capacity.score=5000;
                            capacity.reason="first_heavy_factory";
                        }
                    }
                } else if (factory==Structure_LightFactory && int64_t(pass.lightVehicleValue)*10000
                    >= int64_t(pass.vehiclePlanValue)*pass.lightVehicleBps) capacity.reason="light_mix_satisfied";
                else if (pass.itemCount[factory]>actual) capacity.reason="factory_pending";
                else if (busy<actual) capacity.reason="existing_line_available";
                else if (!campaignAvailableToBuild(builder,factory)) capacity.reason="tech_unavailable";
                else if (getHouse()->isUnitLimitReached(factory==Structure_HighTechFactory ? Unit_Ornithopter : Unit_Tank)) capacity.reason="unit_limit";
                else if (capacity.capacity<=0) capacity.reason="income_or_army_bottleneck";
                else if (!findPlaceLocation(factory).isValid()) capacity.reason="no_site";
                else {
                    capacity.score=QuantBotSpendingPolicy::productionScore(capacity.capacity,capacity.cost,pass.militaryValue,pass.militaryBudget);
                    capacity.reason="funded_production_bottleneck";
                }
                pass.capitalCandidates.push_back(capacity);
            }
            continue;
        }
        // Record unavailable worker lanes too: a zero score explains whether
        // stock, time, remaining spice or the fleet limit stopped investment.
        if (building==Structure_HeavyFactory || port) {
            CapitalCandidate worker;
            worker.builder=builder->getObjectID();worker.item=Unit_Harvester;
            worker.price=purchasePrice(builder,Unit_Harvester);
            worker.cost=worker.price;
            worker.delay=port ? MILLI2CYCLES(30000) : data[Unit_Harvester][houseID].buildtime*15;
            const int before=std::min(pass.itemCount[Unit_Harvester]*workerAnnualIncome(pass),pass.itemCount[Structure_Refinery]*pass.bayAnnualIncome);
            const int after=std::min((pass.itemCount[Unit_Harvester]+1)*workerAnnualIncome(pass),pass.itemCount[Structure_Refinery]*pass.bayAnnualIncome);
            worker.proceeds=QuantBotSpendingPolicy::marginalSpice(pass.spiceShare,before,after,
                worker.delay,CityEconomyInvestmentPolicy::horizonCycles,DuneCity::kCyclesPerMinute);
            if (pass.refineryFieldRisk>0) worker.proceeds/=2;
            const int replacement = cityHarvesterReplacementFloor(pass);
            if (!ready) worker.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_Harvester)) worker.reason="tech_unavailable";
            else if (port && getHouse()->getChoam().getNumAvailable(Unit_Harvester)<=0) worker.reason="sold_out";
            else if (getHouse()->isGroundUnitLimitReached() || (pass.engineHarvesterLimit>0
                && pass.itemCount[Unit_Harvester]>=pass.engineHarvesterLimit)) worker.reason="unit_limit";
            else if (replacement >= 0 && pass.itemCount[Unit_Harvester] >= replacement) worker.reason="replacement_floor_met";
            else if (replacement > pass.itemCount[Unit_Harvester] && port
                && cityHarvesterReplacementFactoryReady(pass)) worker.reason="replacement_factory_ready";
            else if (pass.itemCount[Unit_Harvester]>=pass.desiredWorkers && harvesterInvestmentReserve(pass)==0) worker.reason="fleet_target";
            else if (worker.proceeds <= worker.cost && harvesterInvestmentReserve(pass) == 0
                && !(pass.cityBoomFunding && pass.itemCount[Unit_Harvester] < pass.cityBoomWorkers))
                worker.reason = "spice_or_bay_bottleneck";
            else if (!port && harvesterInvestmentReserve(pass)==0 && !factoryPrefersHarvester(pass, builder)) worker.reason="army_balance";
            else {
                worker.score=QuantBotSpendingPolicy::economyScore(worker.proceeds,worker.cost);
                if ((harvesterInvestmentReserve(pass)>0 || openingFleetIncomplete(pass)) && !pass.defendingEconomy)
                    worker.score=std::max(worker.score,5000);
                if (pass.cityBoomFunding && pass.itemCount[Unit_Harvester] < pass.cityBoomWorkers)
                    worker.score = std::max(worker.score, 5000);
                if (pass.itemCount[Unit_Harvester] < replacement)
                    worker.score = std::max(worker.score, 6000);
                worker.reason="marginal_income";
            }
            pass.capitalCandidates.push_back(worker);
        }
        if (port || building==Structure_HighTechFactory) {
            // Establish transport for the working spice fleet before optional
            // buildings or filling every remaining worker slot. This is a
            // capacity requirement. Count queued/paid/in-flight transports.
            // Additional carryalls serve both workers and vehicle repair traffic.
            CapitalCandidate transport;
            transport.builder=builder->getObjectID();transport.item=Unit_Carryall;
            transport.kind="transport";
            transport.price=purchasePrice(builder,Unit_Carryall);transport.cost=transport.price;
            transport.delay=port ? MILLI2CYCLES(30000) : data[Unit_Carryall][houseID].buildtime*15;
            if (pass.itemCount[Unit_Carryall]>=pass.transportTarget) transport.reason="transport_target_met";
            else if ((getAIHarvesterCount()==0 || getAIRefineryBays()==0)
                && (pass.militaryValue==0 || pass.itemCount[Structure_RepairYard]==0))
                transport.reason="no_working_spice_fleet";
            else if (lastCalculatedSpice<=0 && (pass.militaryValue==0 || pass.itemCount[Structure_RepairYard]==0)) transport.reason="spice_depleted";
            else if (!ready) transport.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_Carryall) || transport.price<=0) transport.reason="tech_unavailable";
            else if (port && getHouse()->getChoam().getNumAvailable(Unit_Carryall)<=0) transport.reason="sold_out";
            else if (getHouse()->isAirUnitLimitReached()) transport.reason="unit_limit";
            else {
                transport.score=pass.itemCount[Unit_Carryall]==0 ? 5500
                    : pass.itemCount[Unit_Carryall]*2<pass.transportTarget ? 4500 : 2000;
                transport.reason=pass.itemCount[Unit_Carryall]==0 ? "first_transport_productivity" : "transport_capacity_shortfall";
            }
            pass.capitalCandidates.push_back(transport);
        }
        // An imported colonist uses the ordinary shared capital ranking and
        // the same real stock, cash and delivery checks as local production.
        if (port && pass.colonisationNeeded && !pass.mcvBuildAvailable && pass.itemCount[Unit_MCV]==0) {
            CapitalCandidate colonist;
            colonist.builder=builder->getObjectID();colonist.item=Unit_MCV;
            colonist.kind="construction";
            colonist.price=purchasePrice(builder,Unit_MCV);
            colonist.cost=colonist.price;colonist.delay=MILLI2CYCLES(30000);
            if (!ready) colonist.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_MCV) || colonist.price<=0)
                colonist.reason="tech_unavailable";
            else if (getHouse()->getChoam().getNumAvailable(Unit_MCV)<=0) colonist.reason="sold_out";
            else if (getHouse()->isGroundUnitLimitReached()) colonist.reason="unit_limit";
            else {
                colonist.score=5000;
                colonist.reason="colonisation_without_factory";
            }
            pass.capitalCandidates.push_back(colonist);
        }
        if (ready && building==Structure_HeavyFactory && campaignAvailableToBuild(builder,Unit_MCV)
            && !getHouse()->isGroundUnitLimitReached() && pass.itemCount[Unit_MCV]==0
            && gameMode==GameMode::Custom && !openingWorkersNeeded(pass) && !pass.postponeCapacityMcv) {
            CapitalCandidate mcv;
            mcv.builder=builder->getObjectID();mcv.item=Unit_MCV;mcv.kind="construction";
            mcv.price=data[Unit_MCV][houseID].price;mcv.cost=mcv.price;
            mcv.delay=data[Unit_MCV][houseID].buildtime*15+MILLI2CYCLES(60000);
            const bool cityNeed=pass.citySimEnabled && pass.cityConstructionCapacity<pass.cityYardTarget
                && (pass.rockExpansionNeeded || availableBaseRock>=48)
                && (pass.rockExpansionNeeded || pass.ownResValve>0 || pass.ownComValve>0 || pass.ownIndValve>0);
            // Colonisation is not production capacity, so it does not wait for
            // the yard target. A city that has built out its own rock cannot
            // grow at all until someone settles the next formation, however
            // many yards it already runs.
            const bool colonyNeed=pass.colonisationNeeded;
            const bool vanillaNeed=!pass.citySimEnabled && DuneCity::prioritizeVanillaMcv(pass.money,
                pass.actualHarvesters,pass.itemCount[Structure_ConstructionYard],pass.itemCount[Unit_MCV],mcv.price);
            if (!(cityNeed || colonyNeed || vanillaNeed)) mcv.reason="construction_capacity_available";
            else if (pass.forecastFunding<mcv.cost+std::max(1000,pass.cityWorkingReserve)) mcv.reason="income_bottleneck";
            else if (!canFundCapacityMcv(pass,mcv.price,builder->getObjectID()))
                mcv.reason=pass.citySimEnabled && botConfig().force.cityOpeningCapital
                    ? "city_working_capital" : "existing_production_runway";
            else {
                mcv.capacity=pass.capacityMcvRunway
                    ? std::max(0,pass.cashFlow.projectedCash-mcv.cost-pass.cashBuffer)
                    : pass.forecastFunding-mcv.cost-pass.cityWorkingReserve;
                // Establish demanded city throughput before filling the entire
                // transport ratio. The first Carryall still ranks above this.
                mcv.score=(cityNeed || colonyNeed) ? 5000 : 4500;
                mcv.reason=colonyNeed&&!cityNeed ? "base_rock_exhausted"
                    : pass.rockExpansionNeeded ? "available_rock_exhausted" : "funded_construction_bottleneck";
            }
            pass.capitalCandidates.push_back(mcv);
        }
        if (!ready) continue;
        CapitalCandidate soldier;
        soldier.builder=builder->getObjectID();soldier.kind="military";
        int64_t bestDeficit=-1;
        AITelemetry::Record unitOptions;
        for (const auto& offer:builder->getBuildList()) {
            const Uint32 item=offer.itemID;
            if (!QuantBotBuildPolicy::militaryItem(item)) continue;
            const int value=data[item][houseID].price;
            const int price=int(offer.price);
            int bps=0;
            // Every special has its own mix slot (and so its own share) since 9868.
            for (size_t i=0;i<mixItems.size();++i) if (mixItems[i]==item) bps=pass.unitMix[i];
            if (item==Unit_Soldier || item==Unit_Infantry || item==Unit_Trooper || item==Unit_Troopers) bps=pass.infantryPercent*100;
            const int64_t deficit=int64_t(pass.vehiclePlanValue)*bps-int64_t(pass.itemCount[item])*value*10000;
            int score=QuantBotSpendingPolicy::militaryScore(price,value,pass.militaryValue,pass.militaryBudget,pass.defendingEconomy);
            const char* reason="eligible";
            if (!campaignAvailableToBuild(builder,item)) reason="unavailable";
            else if (getHouse()->isUnitLimitReached(item)) reason="unit_limit";
            else if (port && getHouse()->getChoam().getNumAvailable(item)<=0) reason="sold_out";
            else if (port && price >= value) reason="not_discounted";
            else if (pass.techSavings && !compositionAdmits(pass, item)) reason="existing_composition_satisfied";
            else if (!pass.techSavings && !port && (bps<=0 || deficit<=0)) reason="mix_satisfied";
            // City infantry answer an observed wing and claim the aggregate infantry
            // share, not a full share each. Without this a finished air raid would go
            // on reserving cash for troops nobody is going to order.
            else if (pass.trooperSupport && QuantBotBuildPolicy::rocketInfantry(item)
                && !trooperOrderAdmitted(pass, item, value)) reason="trooper_support_satisfied";
            else if (cityInfantryItem(pass, item) && !pass.cityAntiAirInfantryDemand
                && !(pass.trooperSupport && QuantBotBuildPolicy::rocketInfantry(item))) reason="no_observed_air";
            else if (cityInfantryItem(pass, item) && !infantryQuotaAdmits(pass, value)) reason="infantry_quota_satisfied";
            else if (item==Unit_Ornithopter && int64_t(pass.itemCount[item]+1)*value*10000
                > int64_t(pass.militaryValue+value)*2000) reason="air_composition_cap";
            else if (score<=0) reason="military_limit";
            if (std::string(reason)!="eligible") score=0;
            if (AITelemetry::log().enabled()) unitOptions.set(std::to_string(item),AITelemetry::Record()
                .set("price",price).set("military_value",value).set("committed",pass.itemCount[item])
                .set("target_bps",bps).set("deficit_scaled",deficit).set("score",score).set("reason",reason));
            if (score>soldier.score || (score>0 && score==soldier.score && deficit>bestDeficit)) {
                soldier.item=item;soldier.price=price;soldier.cost=price;soldier.score=score;
                soldier.proceeds=value;soldier.delay=port ? MILLI2CYCLES(30000) : data[item][houseID].buildtime*15;
                soldier.reason=pass.defendingEconomy ? "active_defence" : "military_shortfall";bestDeficit=deficit;
            }
        }
        if (soldier.item!=NONE_ID) pass.capitalCandidates.push_back(soldier);
        if (AITelemetry::log().enabled()) pass.capitalUnitOptions.set(std::to_string(builder->getObjectID()),unitOptions);
    }
    for (auto& candidate:pass.capitalCandidates) if (isStructure(candidate.item) && candidate.score>0
        && candidate.kind!=std::string("expansion_upgrade") && candidate.kind!=std::string("defence_upgrade")
        && std::string_view(candidate.kind)!="tech_progression" && std::string_view(candidate.kind)!="tech_upgrade") {
        const auto* builder=dynamic_cast<const BuilderBase*>(getObject(candidate.builder));
        if (!builder) continue;
        planningBuilder=candidate.builder;clearPlacementCache(false,true);
        const Coord site=candidate.site.isValid() ? candidate.site : findPlaceLocation(candidate.item);
        std::vector<Uint32> removed;
        if (site.isValid()) {
            redevelopmentZones(candidate.item,site,removed);
            for (const auto& foundation:foundationPlan(builder,candidate.item,site,removed).orders)
                candidate.foundationCost+=purchasePrice(builder,foundation.item);
        }
        if (std::string_view(candidate.kind)=="city_construction"
            && pass.money<candidate.price+candidate.foundationCost) {
            // A settled, solvent multi-yard city can fund this one missing
            // supplier inside the ordinary forecast. Without the bounded
            // claim, imports repeatedly spend its receipts before the quote.
            const bool fundedBootstrap=botConfig().force.cityFirstFactorySaving && pass.cityYards>1
                && pass.forecastNetIncome>=candidate.price+candidate.foundationCost+pass.queuedProductionCost;
            if (fundedBootstrap) candidate.reason="funded_first_heavy_bootstrap";
            else { candidate.score=0;candidate.reason="first_heavy_not_affordable"; }
        }
    }
    if (pass.techSavings) for (auto& candidate : pass.capitalCandidates) {
        if (candidate.score <= 0) continue;
        planningBuilder = candidate.builder;
        const auto kind = std::string_view(candidate.kind);
        const bool upgrade = kind == "tech_upgrade" || kind == "defence_upgrade" || kind == "expansion_upgrade";
        const auto* builder = dynamic_cast<const BuilderBase*>(getObject(candidate.builder));
        const Uint32 budgetItem = upgrade && builder ? builder->getItemID() : candidate.item;
        if (!spendingAdmits(pass, budgetItem, candidate.price + candidate.foundationCost, upgrade)) {
            candidate.score = 0;
            candidate.reason = "unplanned_technology";
        }
    }
    if (pass.cityBoom == CityBoomKind::Greedy && pass.cityBoomFunding)
        for (auto& candidate : pass.capitalCandidates) {
            const auto kind = std::string_view(candidate.kind), reason = std::string_view(candidate.reason);
            if (!candidate.urgent && (candidate.item == Structure_RocketTurret || candidate.item == Structure_GunTurret
                || kind == "early_rocket" || kind == "expansion_defence" || kind == "expansion_upgrade"
                || kind == "defence_upgrade" || kind == "defence_prerequisite"
                || reason == "core_coverage" || reason == "uncovered_base")) {
                candidate.score = 0;
                candidate.reason = "city_greedy_defence_deferred";
            }
        }
}

void QuantBot::planVanillaBoldCapital(BuildPass& pass) {
    using Plan = QuantBotVanillaPolicy::Plan;
    if (pass.vanillaPlan == Plan::None) return;
    if (pass.vanillaBoomArmy) {
        pass.techWorkingReserve = 0;
        pass.techSavingActive = pass.techResearch = pass.techUpgrade = false;
        pass.techFundingPriority = false;
    }
    for (auto& candidate : pass.capitalCandidates) {
        bool admitted = !(pass.vanillaBoldTechDeferred
                && (candidate.item == Structure_IX || candidate.item == Structure_Palace))
            && !(pass.vanillaBoldMilitaryHeld && !pass.defendingEconomy
                && QuantBotBuildPolicy::militaryItem(candidate.item))
            && !(candidate.item == Unit_Harvester && pass.itemCount[Unit_Harvester] >= pass.spiceHarvesterTarget)
            && !(candidate.item == Structure_Refinery && pass.itemCount[Structure_Refinery] >= pass.vanillaBoomTargets.refineries)
            && !(candidate.item == Structure_HeavyFactory && pass.itemCount[Structure_HeavyFactory] >= pass.vanillaBoomTargets.factories);
        if (pass.vanillaBoomArmy) {
            const std::string_view kind(candidate.kind);
            const bool emergency = pass.defendingEconomy || pass.airEngaged;
            const bool recovery = (candidate.item == Unit_Harvester && pass.itemCount[Unit_Harvester] < pass.spiceHarvesterTarget)
                || (candidate.item == Unit_Carryall && pass.itemCount[Unit_Carryall] == 0)
                || (candidate.item == Unit_MCV && pass.itemCount[Structure_ConstructionYard] == 0);
            const bool power = candidate.item == Structure_WindTrap && pass.powerRules
                && getHouse()->getProducedPower() < getHouse()->getPowerRequirement() + pass.committedPowerDemand;
            const bool defence = emergency && (candidate.item == Structure_GunTurret
                || candidate.item == Structure_RocketTurret || kind == "defence_upgrade" || kind == "expansion_upgrade");
            admitted &= QuantBotBuildPolicy::militaryItem(candidate.item) || recovery || power || defence;
        }
        if (!admitted) candidate.score = 0;
    }
    if ((!pass.vanillaBoomArmy && pass.defendingEconomy)
        || (!pass.vanillaBoomArmy && getHouse()->isGroundUnitLimitReached())) return;
    const int house = getHouse()->getHouseID();
    const auto& data = currentGame->objectData.data;
    const int refineryGoal = pass.vanillaBoomTargets.refineries;
    const int factoryGoal = pass.vanillaBoomTargets.factories;
    const auto available = [&](Uint32 item) {
        return data[item][house].enabled && data[item][house].techLevel <= getHouse()->getTechLevel();
    };
    const Uint32 producer = available(Structure_HeavyFactory) ? Structure_HeavyFactory
        : available(Structure_LightFactory) ? Structure_LightFactory
        : available(Structure_WOR) ? Structure_WOR : Structure_Barracks;
    const bool portBoom = pass.vanillaPlan == Plan::GreedyEconomy || pass.vanillaPlan == Plan::ReactiveEconomy;
    Uint32 goal = NONE_ID;
    if (pass.vanillaBoomArmy) {
        goal = pass.itemCount[Structure_Refinery] == 0 ? Structure_Refinery
            : pass.itemCount[producer] == 0 ? producer
            : getHouse()->getTechLevel() > 4 && canAddRepairYard(0) && pass.itemCount[Structure_RepairYard] == 0
                ? Structure_RepairYard
            : needsFirstTransport(pass) && pass.itemCount[Structure_HighTechFactory] == 0
                && pass.itemCount[Structure_StarPort] == 0 && available(Structure_HighTechFactory)
                ? Structure_HighTechFactory
            : pass.itemCount[Structure_Refinery] < refineryGoal ? Structure_Refinery : NONE_ID;
    } else {
        goal = pass.itemCount[Structure_Refinery] < std::min(2, refineryGoal) ? Structure_Refinery
            : pass.itemCount[producer] == 0 ? producer
            : portBoom && pass.itemCount[Structure_Refinery] >= std::min(3, refineryGoal)
                && pass.itemCount[Structure_StarPort] == 0 && available(Structure_StarPort) ? Structure_StarPort
            : pass.itemCount[Structure_Refinery] < refineryGoal ? Structure_Refinery
            : pass.itemCount[Structure_HeavyFactory] < factoryGoal && available(Structure_HeavyFactory)
                ? Structure_HeavyFactory : NONE_ID;
    }
    for (int depth = 0; goal != NONE_ID && depth < Structure_LastID; ++depth) {
        const auto& spec = data[goal][house];
        if (!spec.enabled || spec.techLevel > getHouse()->getTechLevel()
            || pass.itemCount[goal] > getHouse()->getNumItems(goal)) return;
        for (const auto* builder : pass.capitalBuilders) {
            if (builder->isUpgrading() || builder->isOnHold() || builder->getProductionQueueSize() > 0) continue;
            CapitalCandidate candidate;
            candidate.builder = builder->getObjectID(); candidate.item = goal;
            candidate.kind = "vanilla_bold"; candidate.reason = "strategy_commitment"; candidate.score = 8000;
            if (campaignAvailableToBuild(builder, goal)) {
                if (builder->getItemID() == Structure_StarPort
                    && (getHouse()->getChoam().getNumAvailable(goal) <= 0
                        || !static_cast<const StarPort*>(builder)->okToOrder())) continue;
                candidate.price = candidate.cost = purchasePrice(builder, goal);
                if (isStructure(goal)) {
                    planningBuilder = builder->getObjectID(); clearPlacementCache(false, true);
                    candidate.site = findPlaceLocation(goal);
                    if (candidate.site.isInvalid()) continue;
                    const auto foundation = foundationPlan(builder, goal, candidate.site);
                    if (!foundation.complete) continue;
                    for (const auto& slab : foundation.orders) candidate.foundationCost += purchasePrice(builder, slab.item);
                    candidate.cost += candidate.foundationCost;
                }
            } else if (int(builder->getItemID()) == spec.builder
                && builder->getCurrentUpgradeLevel() < spec.upgradeLevel
                && builder->getMaxUpgradeLevel() >= spec.upgradeLevel && builder->isAllowedToUpgrade()
                && builder->getHealth() >= builder->getMaxHealth()) {
                bool prerequisites = true;
                for (Uint32 item = Structure_FirstID; item < spec.prerequisiteStructuresSet.size(); ++item)
                    if (isStructure(item) && spec.prerequisiteStructuresSet[item]
                        && getHouse()->getNumItems(item) == 0) prerequisites = false;
                if (!prerequisites) continue;
                candidate.item = builder->getItemID(); candidate.kind = "vanilla_bold_upgrade";
                candidate.price = candidate.cost = builder->getUpgradeCost();
                pass.vanillaBoomUpgradeBuilder = builder->getObjectID();
            } else continue;
            if (candidate.price <= 0) continue;
            pass.vanillaBoomRequiredItem = candidate.item;
            pass.vanillaBoldCapital = int(pass.capitalCandidates.size());
            pass.capitalCandidates.push_back(candidate);
            planningBuilder = NONE_ID; clearPlacementCache(false, true);
            return;
        }
        Uint32 missing = NONE_ID;
        for (Uint32 item = Structure_FirstID; item < spec.prerequisiteStructuresSet.size(); ++item)
            if (isStructure(item) && spec.prerequisiteStructuresSet[item]
                && getHouse()->getNumItems(item) == 0) {
                if (pass.itemCount[item] > 0) return;
                missing = item; break;
            }
        goal = missing;
    }
    planningBuilder = NONE_ID; clearPlacementCache(false, true);
}

void QuantBot::chooseCapitalPlan(BuildPass& pass) {
    planningBuilder=NONE_ID; clearPlacementCache(false,true);
    pass.capitalChoice = -1;
    // A fleet-ratio carryall plan is not active mining, repair or pickup work.
    // It must not indefinitely outrank a fully income-budgeted standing battery.
    // Keep actual transport work and every other safety/income priority intact.
    const bool mayPreferBattery=pass.earlyRocketLimit>2
        && pass.itemCount[Structure_RocketTurret]<pass.earlyRocketLimit
        && pass.spiceShare<=0 && pass.desiredWorkers==0 && pass.busyTransports==0
        && pass.pickupQueue.empty() && pass.waitingRepairVehicles==0 && pass.itemCount[Unit_MCV]==0;
    const auto idleTransport=[](const auto& candidate) {
        return std::string_view(candidate.kind)=="transport_production" || candidate.item==Unit_Carryall;
    };
    bool fundedBattery=false;
    int withoutIdleTransport=-1;
    for (size_t i=0;i<pass.capitalCandidates.size();++i) {
        const auto& candidate=pass.capitalCandidates[i];
        if (candidate.score<=0) continue;
        if (pass.capitalChoice<0 || candidate.score>pass.capitalCandidates[pass.capitalChoice].score)
            pass.capitalChoice=int(i);
        if (mayPreferBattery) {
            fundedBattery |= std::string_view(candidate.reason)=="early_rocket";
            if (!idleTransport(candidate) && (withoutIdleTransport<0
                || candidate.score>pass.capitalCandidates[withoutIdleTransport].score))
                withoutIdleTransport=int(i);
        }
    }
    const bool preferFundedBattery=mayPreferBattery && fundedBattery;
    if (preferFundedBattery) pass.capitalChoice=withoutIdleTransport;
    // A bargain's return score must not divert the bounded safety/supplier
    // savings into the military-to-zoning interleave just before they are paid.
    if (pass.capitalChoice>=0
        && std::string_view(pass.capitalCandidates[pass.capitalChoice].kind)=="military") {
        int protectedChoice=-1;
        bool boundedClaim=false;
        for (size_t i=0;i<pass.capitalCandidates.size();++i) {
            const auto& candidate=pass.capitalCandidates[i];
            if (candidate.score<=0 || std::string_view(candidate.kind)=="military") continue;
            if (preferFundedBattery && idleTransport(candidate)) continue;
            boundedClaim |= std::string_view(candidate.reason)=="ground_threat"
                || std::string_view(candidate.reason)=="early_rocket"
                || std::string_view(candidate.kind)=="city_supply"
                || std::string_view(candidate.kind)=="city_advanced_tech"
                || std::string_view(candidate.kind)=="city_construction";
            if (protectedChoice<0 || candidate.score>pass.capitalCandidates[protectedChoice].score)
                protectedChoice=int(i);
        }
        if (boundedClaim) pass.capitalChoice=protectedChoice;
    }
    // Keep a demanded tax-producing lot funded alongside the army. Otherwise
    // saving for the next launcher repeatedly withholds even the last100 credits
    // from idle yards, despite positive demand and legal space. Reserve one lot,
    // release it on acceptance, and let independent factories use the remainder.
    // A selected safety investment owns its cash and yard until accepted.
    // Cheap zoning must not repeatedly consume the savings for that service.
    // Peaceful core coverage is deliberately absent: it is a plan, not a loss
    // in progress, and its shortfall can persist indefinitely. Letting it own
    // the yard and its savings suppressed the dedicated growth allocation and
    // the zoning fallback on every pass, so the city never developed at all.
    // The DefencePrerequisites unlock steps are protected too: unlike the
    // overlap shortfall they are a bounded one-off price (yard upgrades and a
    // Radar) that ends once paid, and plots or bargains bought from their
    // savings are what left a no-spice city with no turret at all.
    pass.protectionCapital = pass.capitalChoice >= 0
        && (pass.capitalCandidates[pass.capitalChoice].reason == std::string("crime_prevention")
            || pass.capitalCandidates[pass.capitalChoice].reason == std::string("air_coverage")
            || pass.capitalCandidates[pass.capitalChoice].reason == std::string("secure_expansion")
            || pass.capitalCandidates[pass.capitalChoice].reason == std::string("ground_threat")
            || std::string_view(pass.capitalCandidates[pass.capitalChoice].reason)=="early_rocket"
            || pass.capitalCandidates[pass.capitalChoice].kind == std::string("city_supply")
            || std::string_view(pass.capitalCandidates[pass.capitalChoice].kind) == "city_construction"
            || std::string_view(pass.capitalCandidates[pass.capitalChoice].kind) == "city_advanced_tech"
            || pass.capitalCandidates[pass.capitalChoice].kind == std::string("defence_upgrade")
            || pass.capitalCandidates[pass.capitalChoice].kind == std::string("defence_prerequisite"));
    // Existing emergency/supplier savings keep priority. First cover for a
    // genuinely uncovered core is a present loss, not optional overlap. Once
    // those are safe, the one infantry lane owns cash before repeat growth.
    if (pass.trooperSupport && !pass.protectionCapital) {
        int infantry = -1, firstCover = -1;
        for (size_t i = 0; i < pass.capitalCandidates.size(); ++i) {
            const auto& candidate = pass.capitalCandidates[i];
            if (pass.techSavings && candidate.score <= 0) continue;
            if (std::string_view(candidate.kind) == "trooper_support"
                || std::string_view(candidate.kind) == "trooper_recruitment") infantry = int(i);
            if (candidate.score > 0 && pass.uncoveredCoreAssets > 0
                && (std::string_view(candidate.reason) == "core_coverage"
                    || std::string_view(candidate.reason) == "uncovered_base")
                && (firstCover < 0 || candidate.score > pass.capitalCandidates[firstCover].score))
                firstCover = int(i);
        }
        if (infantry >= 0) {
            pass.capitalChoice = firstCover >= 0 ? firstCover : infantry;
            pass.protectionCapital = true;
        }
    }
    // A fully priced first police station gets one protected turn after real
    // income, a producer and a seed town exist. Existing emergencies and
    // suppliers keep their claims; no worker/army/tech target is changed.
    if (pass.cityLandValueServices && !pass.protectionCapital
        && pass.cityLandValuePoliceCandidate>=0) {
        const auto& police=pass.capitalCandidates[pass.cityLandValuePoliceCandidate];
        const int working=std::max({2000,pass.cityWorkingReserve,cityBootstrapReserve(pass)});
        if (police.score>0 && int64_t(pass.money)>=int64_t(police.price)+police.foundationCost+working) {
            pass.capitalChoice=pass.cityLandValuePoliceCandidate;
            pass.protectionCapital=true;
        }
    }
    // The existing experimental project owns the shared capital turn.
    // Actual safety and first-Starport priorities remain unchanged here.
    if (pass.techSavingActive && pass.techCandidateIndex >= 0
        && (!pass.protectionCapital || (pass.capitalChoice >= 0
            && !(pass.airEngaged && pass.hostileAircraft > 0 && pass.trooperCount < pass.trooperTarget)
            && (std::string_view(pass.capitalCandidates[pass.capitalChoice].kind) == "trooper_support"
                || std::string_view(pass.capitalCandidates[pass.capitalChoice].kind) == "trooper_recruitment")))) {
        pass.capitalChoice = pass.techCandidateIndex;
        pass.protectionCapital = true;
    }
    pass.cityGrowthProtected = false;
    // With multiple yards, assign one to growth alongside routine investment.
    // Crime protection keeps the winning allocation; growth resumes after its
    // order is accepted. A lone opening yard retains its tech path.
    Uint32 dedicatedCityYard=NONE_ID;
    const bool threatBudget = pass.cityBoomArmy || (botConfig().force.cityThreatBudget && pass.citySimEnabled
        && gameMode == GameMode::Custom && !campaignPolicyApplies()
        && lastCalculatedSpice > 0 && pass.defendingEconomy);
    if (pass.sharedSpending && pass.citySimEnabled && pass.cityYards>1 && !pass.protectionCapital
        && !threatBudget) {
        for (const auto* builder:pass.capitalBuilders) {
            if (builder->getItemID()!=Structure_ConstructionYard || builder->isUpgrading()
                || builder->isOnHold()) continue;
            // Prefer the same oldest usable yard on every pass, including
            // while its existing job finishes. Other yards' incidental zoning
            // must not divert this dedicated queue back into service spending.
            if (builder->getProductionQueueSize()>0) {
                dedicatedCityYard=builder->getObjectID();
                break;
            }
            planningBuilder=builder->getObjectID();clearPlacementCache(false,true);
            const Uint32 zone=affordableCityZone(pass, builder,pass.money);
            if (zone==NONE_ID) continue;
            dedicatedCityYard=builder->getObjectID();
            CapitalCandidate growth;
            growth.builder=builder->getObjectID();growth.item=zone;
            growth.price=purchasePrice(builder,zone);growth.cost=buildingCapitalCost(pass, zone);
            growth.kind="city_growth";growth.reason="dedicated_city_growth";
            growth.score=1; // A construction allocation, not a fabricated return forecast.
            pass.capitalChoice=int(pass.capitalCandidates.size());pass.capitalCandidates.push_back(growth);
            pass.cityGrowthProtected=true;
            break;
        }
        planningBuilder=NONE_ID;clearPlacementCache(false,true);
    }
    // A single yard may spend a bounded live infrastructure allowance on the
    // same profitable zone candidates. Do not divert urgent service, committed
    // power, first combat capability or the opening import supplier.
    if (botConfig().force.citySingleYardGrowth && pass.sharedSpending && pass.citySimEnabled
        && gameMode == GameMode::Custom && !campaignPolicyApplies()
        && lastCalculatedSpice > 0 && pass.cityYards == 1 && !pass.protectionCapital
        && !pass.defendingEconomy && !pass.airEngaged && pass.dangerousCrimeProperties == 0
        && pass.civicStructuralNeed == 0 && !pass.openingSupplierDue
        && pass.cityZonesIncludingQueued >= CityEconomyInvestmentPolicy::kBootstrapZoneSeed
        && getHouse()->getNumItems(Structure_HeavyFactory) > 0
        && getHouse()->getProducedPower()-getHouse()->getPowerRequirement() >= pass.committedPowerDemand+24
        && nonServiceConstructionOrders >= 2) {
        int infrastructure = 0;
        for (const Uint32 item : {Structure_WindTrap,Structure_NuclearPlant,Structure_Refinery,Structure_Silo,
                Structure_Barracks,Structure_WOR,Structure_LightFactory,Structure_HeavyFactory,
                Structure_HighTechFactory,Structure_Radar,Structure_StarPort,Structure_RepairYard,
                Structure_IX,Structure_Palace,Structure_ResearchLab})
            infrastructure += getHouse()->getNumItems(item);
        if (CityEconomyInvestmentPolicy::singleYardGrowthDue(infrastructure,pass.cityZonesIncludingQueued))
            for (size_t i=0;i<pass.capitalCandidates.size();++i) {
                auto& growth = pass.capitalCandidates[i];
                if (std::string_view(growth.kind) != "economy" || !DuneCity::isCityZoneStructure(growth.item)
                    || growth.score <= 0 || growth.proceeds <= growth.cost
                    || pass.money < int64_t(growth.price)+growth.foundationCost) continue;
                growth.reason = "single_yard_growth";
                dedicatedCityYard = growth.builder;
                pass.capitalChoice = int(i);
                pass.cityGrowthProtected = true;
                break;
            }
    }
    if (!pass.cityGrowthProtected && pass.citySimEnabled && !threatBudget
        && (getHouse()->getProducedPower()-getHouse()->getPowerRequirement()>=24
            || botConfig().force.cityGrowthPower)
        && pass.capitalChoice>=0 && pass.capitalCandidates[pass.capitalChoice].kind==std::string("military")) {
        int growth=-1;
        for (size_t i=0;i<pass.capitalCandidates.size();++i)
            if ((DuneCity::isCityZoneStructure(pass.capitalCandidates[i].item)
                    ? getHouse()->getProducedPower()-getHouse()->getPowerRequirement()>=24
                    : std::string_view(pass.capitalCandidates[i].kind)=="city_growth_power")
                && pass.capitalCandidates[i].score>0
                && (growth<0 || pass.capitalCandidates[i].score>pass.capitalCandidates[growth].score)) growth=int(i);
        if (growth>=0) {pass.capitalChoice=growth;pass.cityGrowthProtected=true;}
    }
    if (pass.vanillaBoldCapital >= 0 && !pass.defendingEconomy
        && !(pass.protectionCapital && pass.airEngaged)) {
        pass.capitalChoice = pass.vanillaBoldCapital;
        pass.protectionCapital = true;
        pass.cityGrowthProtected = false;
    }
    if ((pass.cityBoomFunding || pass.cityBoomArmy) && !pass.protectionCapital) {
        int choice = -1, bestRank = std::numeric_limits<int>::max();
        const int workers = pass.itemCount[Unit_Harvester], bays = pass.itemCount[Structure_Refinery];
        const bool refineryFirst = bays < std::min(2, pass.cityBoomBays) || pass.cityBoom == CityBoomKind::Refineries
            || ((pass.cityBoom == CityBoomKind::Greedy || pass.cityBoom == CityBoomKind::Reactive) && workers >= 3 * bays);
        for (size_t i = 0; i < pass.capitalCandidates.size(); ++i) {
            const auto& candidate = pass.capitalCandidates[i];
            if (candidate.score <= 0) continue;
            int rank = std::numeric_limits<int>::max();
            if (pass.cityBoomArmy) {
                if (std::string_view(candidate.kind) == "military") rank = 0;
            } else {
                if (candidate.item == Unit_Harvester && workers < pass.cityBoomWorkers) rank = refineryFirst ? 1 : 0;
                else if (candidate.item == Structure_Refinery && bays < pass.cityBoomBays) rank = refineryFirst ? 0 : 1;
                else if (DuneCity::isCityZoneStructure(candidate.item) || std::string_view(candidate.kind) == "city_growth_power")
                    rank = 2;
            }
            if (rank < bestRank || (rank == bestRank && choice >= 0
                && candidate.score > pass.capitalCandidates[choice].score)) {
                choice = int(i); bestRank = rank;
            }
        }
        if (choice >= 0) {
            pass.capitalChoice = choice;
            pass.cityGrowthProtected = DuneCity::isCityZoneStructure(pass.capitalCandidates[choice].item);
        }
    }
    pass.capitalConsumed = false;
    pass.capitalOrderedCost = 0;
    pass.capitalInitialCount = pass.capitalChoice>=0 ? pass.itemCount[pass.capitalCandidates[pass.capitalChoice].item] : 0;
    pass.capitalDecision = 0;
    if (!builderOnly() && AITelemetry::log().enabled()) {
        AITelemetry::Record options;
        for (size_t i=0;i<pass.capitalCandidates.size();++i) {
            const auto& c=pass.capitalCandidates[i];
            options.set(std::to_string(i),AITelemetry::Record().set("builder",c.builder).set("item",c.item)
                .set("kind",c.kind).set("price",c.price).set("total_cost_with_power",c.cost)
                .set("proceeds_or_military_value",c.proceeds).set("additional_funded_capacity",c.capacity)
                .set("foundation_cost",c.foundationCost).set("score",c.score).set("delay_cycles",c.delay)
                .set("reason",c.reason).set("urgent",c.urgent)
                .set("affordable",pass.money>=c.price+c.foundationCost));
        }
        auto observeVanillaFfa = [&](AITelemetry::Record& details) -> AITelemetry::Record& {
            const auto& force = botConfig().force;
            const bool largeSwitch = pass.vanillaFfaLargeScope
                && (force.vanillaFfaThreatProduction || force.vanillaFfaFundedTech);
            if (!(force.vanillaFfaStarportAfterWave || force.vanillaFfaLightWorkingCash || largeSwitch)
                || !vanillaMultiEnemyGame()) return details;
            if (force.vanillaFfaStarportAfterWave)
                details.set("vanilla_ffa_port_deferred",pass.vanillaFfaStarportDeferred)
                    .set("vanilla_ffa_wave_capital",pass.vanillaFfaWaveCapitalReserve);
            if (force.vanillaFfaThreatProduction && pass.vanillaFfaLargeScope)
                details.set("vanilla_ffa_threat_production",pass.vanillaFfaThreatProduction);
            if (force.vanillaFfaLightWorkingCash)
                details.set("vanilla_ffa_light_working_cash",pass.vanillaFfaLightWorkingCash);
            if (force.vanillaFfaFundedTech && pass.vanillaFfaLargeScope)
                details.set("vanilla_ffa_tech_deferred",pass.vanillaFfaTechDeferred);
            return details;
        };
        pass.capitalDecision=traceDecision("capital_plan",observeVanillaFfa(AITelemetry::Record().set("policy_version",2)
            .set("mode",pass.citySimEnabled ? "dunecity" : "vanilla").set("campaign",campaignPolicyApplies())
            .set("campaign_helper_reserve",harvesterInvestmentReserve(pass)).set("defending",pass.defendingEconomy)
            .set("cash",getHouse()->getCredits()).set("committed_cost",pass.queuedProductionCost).set("spendable",pass.money)
            .set("horizon_cycles",CityEconomyInvestmentPolicy::horizonCycles).set("forecast_tax_per_minute",pass.forecastTax)
            .set("forecast_upkeep_per_minute",pass.forecastUpkeep).set("forecast_spice_receipts",pass.forecastSpice)
            .set("forecast_net_income",pass.forecastNetIncome).set("forecast_funding",pass.forecastFunding)
            .set("active_production_burn_per_minute",pass.activeProductionBurn)
            .set("construction_capacity_cost",pass.constructionCapacity).set("continued_construction_cost",pass.continuedConstructionCost)
            .set("continued_unit_cost",pass.continuedMilitaryCost).set("military_funding_after_construction",pass.militaryFunding)
            .set("projected_cash",pass.cashFlow.projectedCash).set("cash_buffer",pass.cashBuffer)
            .set("net_burn_per_minute",pass.cashFlow.netBurnPerMinute).set("cash_runway_seconds",pass.cashFlow.runwaySeconds)
            .set("funds_parallel_production",pass.cashFlow.fundsParallelProduction).set("shared_priority",pass.sharedSpending)
            .set("prefer_funded_battery",preferFundedBattery)
            .set("starport_market_available",pass.starportMarketAvailable).set("funded_factory_opening",pass.fundedFactoryOpening)
            .set("funded_city_production",pass.fundedCityProduction).set("next_heavy_runway",pass.nextHeavyRunway)
            .set("city_growth_protected",pass.cityGrowthProtected)
            .set("city_growth_yards_busy",pass.cityGrowthYardsBusy)
            .set("city_growth_dedicated_yard",dedicatedCityYard)
            .set("city_growth_builder",pass.cityGrowthProtected ? pass.capitalCandidates[pass.capitalChoice].builder : NONE_ID)
            .set("first_factory_capital",pass.firstFactoryCapital).set("first_factory_production_cost",pass.firstFactoryProduction)
            .set("existing_military_capacity",pass.existingMilitaryCapacity).set("military_value",pass.militaryValue)
            .set("military_target",militaryValueLimit).set("military_budget",pass.militaryBudget)
            .set("military_budget_source",pass.militaryBudgetOverridden ? "override_rolling_headroom" : "configured")
            .set("unit_count_override",getGameInitSettings().getGameOptions().maximumNumberOfUnitsOverride)
            .set("queued_military_value",pass.queuedMilitaryValue)
            .set("workers",pass.itemCount[Unit_Harvester]).set("worker_target",pass.desiredWorkers)
            .set("carryalls",getHouse()->getNumItems(Unit_Carryall)).set("carryalls_committed",pass.itemCount[Unit_Carryall])
            .set("carryall_target",pass.transportTarget).set("funded_army_target",pass.vehiclePlanValue)
            .set("carryall_baseline",pass.transportBaseline).set("carryalls_busy",pass.busyTransports)
            .set("pickup_waiting",int(pass.pickupQueue.size())).set("combat_vehicles",pass.combatVehicles)
            .set("repair_baseline",pass.repairBaseline).set("repair_target",pass.repairTarget)
            .set("repair_bays",getHouse()->getNumItems(Structure_RepairYard)).set("repair_bays_busy",pass.busyRepairBays)
            .set("repair_waiting",pass.waitingRepairVehicles).set("repair_bays_committed",pass.itemCount[Structure_RepairYard])
            .set("refinery_capacity_target",(pass.itemCount[Unit_Harvester]*workerAnnualIncome(pass)+pass.bayAnnualIncome-1)/std::max(1,pass.bayAnnualIncome))
            .set("refineries_committed",pass.itemCount[Structure_Refinery]).set("refinery_waiting",pass.waitingHarvesters)
            .set("refinery_unbooked_bays",pass.unbookedRefineries).set("refinery_field_returners_blocked",pass.fieldReturnersBlocked)
            .set("refinery_returns_waiting",pass.blockedRefineryReturns)
            .set("refinery_queue_persistent",pass.unloadingBacklog)
            .set("moderate_crime_properties",pass.moderateCrimeProperties).set("dangerous_crime_properties",pass.dangerousCrimeProperties)
            .set("spice_share",pass.spiceShare).set("worker_income",workerAnnualIncome(pass)).set("bay_income",pass.bayAnnualIncome)
            .set("trip_cycles",pass.refineryTripCycles).set("walking_trip_cycles",pass.walkingTripCycles).set("field_risk",pass.refineryFieldRisk)
            .set("ready_yards",pass.readyYards).set("power_required",getHouse()->getPowerRequirement())
            .set("power_produced",getHouse()->getProducedPower()).set("producers",pass.capitalProducers)
            .set("ground_threat_value",pass.groundThreatValue).set("ground_turret_goal",pass.groundTurretGoal)
            .set("ground_turret_cover",pass.groundTurretCover)
            .set("ground_threat_x",pass.groundThreatAsset.x).set("ground_threat_y",pass.groundThreatAsset.y)
            .set("unit_options",pass.capitalUnitOptions).set("candidates",options).set("selected",pass.capitalChoice)
            .set("reason",pass.cityGrowthProtected ? "protect_demanded_city_growth" : !pass.sharedSpending ? "funded_parallel_production" : pass.capitalChoice<0
                ? "no_useful_available_purchase" : "highest_marginal_priority")));
    }
}

bool QuantBot::capitalPending(BuildPass& pass) {
    return pass.capitalChoice>=0 && !pass.capitalConsumed
        && pass.itemCount[pass.capitalCandidates[pass.capitalChoice].item]==pass.capitalInitialCount;
}

int QuantBot::capitalReserve(BuildPass& pass, Uint32 builder) {
    if (!capitalPending(pass)) return 0;
    const auto& candidate = pass.capitalCandidates[pass.capitalChoice];
    int64_t cost = int64_t(candidate.price) + candidate.foundationCost;
    if (pass.techSavings && pass.capitalChoice == pass.techCandidateIndex) return 0;
    const bool infrastructure = std::string_view(candidate.kind) == "trooper_support";
    if (infrastructure || std::string_view(candidate.kind) == "trooper_recruitment") {
        if (infrastructure) cost = std::max<int64_t>(cost, candidate.cost);
        cost += pass.cityWorCashReserve;
    }
    return QuantBotSpendingPolicy::reserveForOther(INT_MAX, int(std::min<int64_t>(INT_MAX, cost)),
        candidate.builder == builder, false,
        !getHouse()->hasPower() || pass.itemCount[Structure_ConstructionYard] == 0);
}

void QuantBot::planBuildOrder(BuildPass& pass) {
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse()) {
            int priority=QuantBotBuildPolicy::productionPlanningPriority(pass.citySimEnabled,
                structure->getItemID(), structure->getItemID() == Structure_ConstructionYard
                    && static_cast<const ConstructionYard*>(structure)->isWaitingToPlace(), needsFirstTransport(pass));
            if (pass.trooperSupport && structure->getItemID() == Structure_WOR
                && pass.trooperCount < QuantBotBuildPolicy::trooperSupportFloor(pass.hostileAircraft))
                priority = 1; // After city/first transport, before light/heavy spending.
            // Compound the helper's income before optional construction/army
            // spending. The port still checks funds, stock and existing orders.
            if (pass.campaignEconomyPush && pass.itemCount[Unit_Harvester]<pass.spiceHarvesterTarget) {
                if (structure->getItemID()==Structure_StarPort) priority=6;
                else if (structure->getItemID()==Structure_HeavyFactory) priority=5;
            }
            if (pass.capitalChoice>=0 && pass.capitalCandidates[pass.capitalChoice].item==Unit_Carryall
                && pass.capitalCandidates[pass.capitalChoice].builder==structure->getObjectID()
                && getHouse()->hasPower()) priority=7;
            // A visible attacker firing on the base is a current replacement
            // need, not a reason to finish the helper's optional worker fleet.
            const Uint32 item = structure->getItemID();
            if ((pass.campaignThreatProduction || pass.vanillaFfaThreatProduction)
                && (item == Structure_LightFactory || item == Structure_HeavyFactory
                    || item == Structure_Barracks || item == Structure_WOR)) priority = 8;
            pass.planningOrder.emplace_back(priority,structure->getObjectID());
        }
    std::stable_sort(pass.planningOrder.begin(), pass.planningOrder.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    cityReadyYardCount = std::max(1,int(std::count_if(pass.planningOrder.begin(),pass.planningOrder.end(),
        [](const auto& row){return row.first == 3;})));
    if (pass.citySimEnabled) CityPlanningPolicy::rotateYards(pass.planningOrder,getGameCycleCount());
}

void QuantBot::traceCapitalOutcome(BuildPass& pass) {
    if (!builderOnly() && AITelemetry::log().enabled()) {
        traceDecision("capital_outcome",AITelemetry::Record().set("plan",pass.capitalDecision)
            .set("priority_ordered",pass.capitalConsumed).set("priority_pending",capitalPending(pass))
            .set("orders",pass.capitalOrders).set("ordered_cost",pass.capitalOrderedCost).set("remaining_planning_cash",pass.money));
    }
}

double QuantBot::getProductionBuildingMultiplier(int itemID) const {
	switch (itemID) {
		case Structure_ConstructionYard:
			return 2.0;
		case Structure_RepairYard:
			return 2.0;
		case Structure_HeavyFactory:
			return 1.5;
		case Structure_Refinery:
			return 1.3;
		case Structure_StarPort:
			return 1.3;
		default:
			return 1.0;
	}
}

Coord QuantBot::findFinishedRoadSite(const BuilderBase* yard) {
    const Uint32 id = yard->getObjectID(), cycle = getGameCycleCount();
    const auto retry = roadRedirectRetryCycle.find(id);
    if (retry != roadRedirectRetryCycle.end() && cycle < retry->second) return Coord::Invalid();
    const auto sites = cityRoadRepairSites();
    if (sites.empty()) {
        roadRedirectRetryCycle[id] = cycle + MILLI2CYCLES(5000);
        return Coord::Invalid();
    }
    roadRedirectRetryCycle.erase(id);
    return Coord(sites.front().first,sites.front().second);
}

std::vector<std::pair<int,int>> QuantBot::cityRoadRepairSites() {
    std::set<std::pair<int,int>> planned;
    for (const auto& entry : builderPlaceLocations)
        for (const Coord p : entry.second) planned.emplace(p.x,p.y);
    std::vector<CityRoadRepairPolicy::Footprint> buildings;
    for (const StructureBase* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || !structure->isActive()) continue;
        const Coord p=structure->getLocation(), size=getStructureSize(structure->getItemID());
        buildings.push_back({p.x,p.y,size.x,size.y});
    }
    return CityRoadRepairPolicy::candidates(buildings,[&](int x,int y) {
        if (!getMap().tileExists(x,y) || planned.count({x,y})) return false;
        const Tile* tile=getMap().getTile(x,y);
        return !tile->isRoadConnection() && !tile->hasCityZone() && !tile->hasAGroundObject()
            && tile->isRock() && !tile->isMountain()
            && !overlapsReservedStructure(x,y,1,1)
            && getMap().okayToPlaceStructure(x,y,1,1,false,getHouse(),false,Structure_Road);
    },[&](int x,int y) {
        return getMap().tileExists(x,y) && getMap().getTile(x,y)->isRoadConnection();
    });
}

int QuantBot::queueCityRoadRepairs(const BuilderBase* yard, int limit) {
    if (limit <= 0) return 0;
    const auto sites = cityRoadRepairSites();
    int queued=0;
    AITelemetry::Record locations;
    for (const auto& site:sites) {
        if (queued>=limit) break;
        const int before = yard->getProductionQueueSize();
        doProduceItem(yard,Structure_Road);
        if (yard->getProductionQueueSize() <= before) break;
        builderPlaceLocations[yard->getObjectID()].emplace_back(site.first,site.second);
        locations.set(std::to_string(queued),AITelemetry::Record().set("x",site.first).set("y",site.second));
        ++queued;
    }
    if (queued) traceDecision("city_road_repair",AITelemetry::Record().set("builder",yard->getObjectID())
        .set("rule","idle_yard_road_gaps").set("segments_queued",queued).set("locations",locations));
    return queued;
}

void QuantBot::manageCityBuilding() {
    AITelemetry::PerformanceScope perfScope("ai.manageCityBuilding", getGameCycleCount(), getHouse()->getHouseID());
    if (!currentGame) return;
    auto* citySim = currentGame->getCitySimulation();
    if (!citySim || !citySim->isInitialized()) return;
    if (!currentGameMap) return;

    Coord baseCenter = findBaseCentre(getHouse()->getHouseID());
    if (!baseCenter.isValid()) return;

    // Zone structures are now built through the Construction Yard build
    // order (see the Structure_ConstructionYard case in build()).  The old
    // tile-flag approach (CMD_CITY_PLACE_ZONE without a backing structure)
    // created phantom zones that runZoneGrowth() ignored (it requires an
    // actual structure object) and that blocked real zone placement.
    //
    // Road placement remains here: roads are tile-level and don't need the
    // Construction Yard pipeline.

    // Place roads in the gaps between zones/structures. A tile gets a road
    // when it is adjacent to a zone or structure on at least one side AND
    // adjacent to an existing road or zone on at least one side (keeps the
    // network continuous). Scan outward from base center.
    int roadsPlaced = 0;
    constexpr int MAX_ROADS_PER_ROUND = 8;
    constexpr int CITY_RADIUS = 20;

    static constexpr int dx4[] = { 0, 1, 0, -1 };
    static constexpr int dy4[] = { -1, 0, 1, 0 };

    // First repair a missing transport link. City growth checks real road
    // reachability (R -> C, C -> I, I -> R), so spreading local road stubs
    // cannot help two otherwise healthy districts that are disconnected.
    struct RoadLink { Coord from; Coord to; DuneCity::CityRole sourceRole; DuneCity::CityRole targetRole; };
    auto targetRoleFor = [](DuneCity::CityRole role) {
        switch (role) {
            case DuneCity::CityRole::Residential: return DuneCity::CityRole::Commercial;
            case DuneCity::CityRole::Commercial: return DuneCity::CityRole::Industrial;
            case DuneCity::CityRole::Industrial: return DuneCity::CityRole::Residential;
            default: return DuneCity::CityRole::None;
        }
    };
    auto zoneTypeFor = [](DuneCity::CityRole role) {
        switch (role) {
            case DuneCity::CityRole::Residential: return DuneCity::ZoneType::Residential;
            case DuneCity::CityRole::Commercial: return DuneCity::ZoneType::Commercial;
            case DuneCity::CityRole::Industrial: return DuneCity::ZoneType::Industrial;
            default: return DuneCity::ZoneType::None;
        }
    };
    auto perimeterRoads = [&](Coord zone) {
        static constexpr int pdx[] = {-1,0,1,2, -1,0,1,2, -1,2, -1,2};
        static constexpr int pdy[] = {-1,-1,-1,-1, 2,2,2,2, 0,0, 1,1};
        std::vector<Coord> roads;
        for (int i = 0; i < 12; ++i) {
            const int x = zone.x + pdx[i], y = zone.y + pdy[i];
            if (currentGameMap->tileExists(x, y) && currentGameMap->getTile(x, y)->isRoadConnection())
                roads.emplace_back(x, y);
        }
        return roads;
    };

    struct ZoneRoad { Coord zone; DuneCity::CityRole role; std::vector<Coord> roads; };
    std::vector<ZoneRoad> zoneRoads;
    for (const StructureBase* structure : getStructureList()) {
        if (structure->getOwner() != getHouse()) continue;
        const auto role = DuneCity::getStructureCityRole(structure->getItemID());
        if (role == DuneCity::CityRole::None) continue;
        auto roads = perimeterRoads(structure->getLocation());
        if (!roads.empty()) zoneRoads.push_back({structure->getLocation(), role, std::move(roads)});
    }

    DuneCity::TrafficSimulation traffic;
    traffic.init(citySim);
    RoadLink bestLink{Coord::Invalid(), Coord::Invalid(), DuneCity::CityRole::None, DuneCity::CityRole::None};
    int bestLinkDistance = std::numeric_limits<int>::max();
    for (const auto& source : zoneRoads) {
        const auto targetRole = targetRoleFor(source.role);
        if (targetRole == DuneCity::CityRole::None
            || traffic.makeTraffic(source.zone.x, source.zone.y, zoneTypeFor(targetRole)) == 1) continue;
        for (const auto& target : zoneRoads) {
            if (target.role != targetRole) continue;
            for (const auto& from : source.roads) for (const auto& to : target.roads) {
                const int distance = std::abs(from.x - to.x) + std::abs(from.y - to.y);
                if (distance > 1 && distance <= DuneCity::kMaxTrafficDistance && distance < bestLinkDistance) {
                    bestLink = {from, to, source.role, targetRole};
                    bestLinkDistance = distance;
                }
            }
        }
    }

    std::set<std::pair<int, int>> scheduledRoads;
    if (bestLink.from.isValid()) {
        std::vector<Coord> route;
        auto appendLeg = [&](Coord& cursor, int target, bool horizontal) {
            const int step = ((horizontal ? target - cursor.x : target - cursor.y) >= 0) ? 1 : -1;
            while ((horizontal ? cursor.x : cursor.y) != target) {
                if (horizontal) cursor.x += step; else cursor.y += step;
                if (cursor != bestLink.to) route.push_back(cursor);
            }
        };
        Coord cursor = bestLink.from;
        appendLeg(cursor, bestLink.to.x, true);
        appendLeg(cursor, bestLink.to.y, false);

        for (const auto& road : route) {
            if (roadsPlaced >= MAX_ROADS_PER_ROUND) break;
            if (!currentGameMap->tileExists(road.x, road.y)) break;
            Tile* tile = currentGameMap->getTile(road.x, road.y);
            if (tile->isRoadConnection()) continue;
            if (tile->hasCityZone() || tile->hasAStructure() || !tile->isRock() || tile->isMountain()) break;
            if (tile->hasAGroundObject() && tile->getDestroyedStructureTile() == DestroyedStructure_None) break;
            if (!scheduledRoads.emplace(road.x, road.y).second) continue;
            currentGame->getCommandManager().addCommand(Command(getPlayerID(), CMD_CITY_TOOL,
                static_cast<Uint32>(road.x), static_cast<Uint32>(road.y),
                static_cast<Uint32>(DuneCity::CityTool_Road)));
            ++roadsPlaced;
        }
        if (roadsPlaced > 0) {
            traceDecision("city_road_link", AITelemetry::Record()
                .set("source_role", static_cast<int>(bestLink.sourceRole))
                .set("target_role", static_cast<int>(bestLink.targetRole))
                .set("from_x", bestLink.from.x).set("from_y", bestLink.from.y)
                .set("to_x", bestLink.to.x).set("to_y", bestLink.to.y)
                .set("distance", bestLinkDistance).set("segments_queued", roadsPlaced));
        }
    }

    for (int r = 1; r <= CITY_RADIUS && roadsPlaced < MAX_ROADS_PER_ROUND; r++) {
        for (int angle = 0; angle < r * 8 && roadsPlaced < MAX_ROADS_PER_ROUND; angle++) {
            int ox, oy;
            int side = angle / (r * 2);
            int pos = angle % (r * 2);
            switch (side) {
                case 0: ox = -r + pos; oy = -r; break;
                case 1: ox = r; oy = -r + pos; break;
                case 2: ox = r - pos; oy = r; break;
                default: ox = -r; oy = r - pos; break;
            }

            int tx = baseCenter.x + ox;
            int ty = baseCenter.y + oy;

            if (tx < 0 || tx >= currentGameMap->getSizeX() || ty < 0 || ty >= currentGameMap->getSizeY()) continue;
            if (!currentGameMap->tileExists(tx, ty)) continue;

            Tile* tile = currentGameMap->getTile(tx, ty);
            // Skip tiles that already have road, zone, structure, or aren't buildable
            if (tile->isRoad() || tile->hasCityZone() || tile->hasAStructure()) continue;
            if (scheduledRoads.count({tx, ty}) != 0) continue;
            if (!tile->isRock() || tile->isMountain()) continue;
            // Allow rubble tiles (destroyed structures) — road clears the rubble
            if (tile->hasAGroundObject() && tile->getDestroyedStructureTile() == DestroyedStructure_None) continue;

            bool nearStructure = false;
            bool nearRoadOrStructure = false;
            for (int d = 0; d < 4; d++) {
                int nx = tx + dx4[d];
                int ny = ty + dy4[d];
                if (nx < 0 || nx >= currentGameMap->getSizeX() || ny < 0 || ny >= currentGameMap->getSizeY()) continue;
                if (!currentGameMap->tileExists(nx, ny)) continue;
                const Tile* nb = currentGameMap->getTile(nx, ny);
                if (nb->hasCityZone() || nb->hasAStructure()) nearStructure = true;
                if (nb->isRoad() || nb->hasCityZone() || nb->hasAStructure()) nearRoadOrStructure = true;
            }

            // Place road if tile is next to a structure AND connects to
            // existing road network or another structure
            if (nearStructure && nearRoadOrStructure) {
                currentGame->getCommandManager().addCommand(
                    Command(getPlayerID(), CMD_CITY_TOOL,
                            static_cast<Uint32>(tx), static_cast<Uint32>(ty),
                            static_cast<Uint32>(1)));
                scheduledRoads.emplace(tx, ty);
                roadsPlaced++;
            }
        }
    }
}

// === CityRefineryLogistics: balance established, safety-approved return trips ===
// The build source list is explicit, so this routine lives in an existing unit.
bool QuantBot::balanceRefineryReturn(const Harvester* harvester) {
    if (!botConfig().force.cityRefineryLogistics || !citySpiceFfaScope(4)
        || !harvester || harvester->getOwner() != getHouse() || !harvester->isActive()
        || !harvester->isRespondable() || harvester->getHealth() <= 0 || harvester->wasDeviated()
        || !harvester->isReturning() || harvester->getAmountOfSpice() <= 0
        || harvester->isAwaitingPickup() || harvester->hasBookedCarrier()) return false;
    // The engine's ordinary refinery returns are forced moves too. Human/role
    // authority, rather than wasForced(), distinguishes orders we must leave alone.
    if (unitRole(harvester, {UnitRole::Human, UnitRole::Repair, UnitRole::Defence,
            UnitRole::Wave, UnitRole::Escort, UnitRole::ColonyGuard}) != UnitRole::Free) return false;
    const auto* current = dynamic_cast<const StructureBase*>(harvester->getTarget());
    if (!current || current->getOwner() != getHouse() || !current->isActive()
        || current->getHealth() <= 0 || !current->acceptsHarvesterDropoff()) return false;
    const Coord origin = harvester->getLocation(), currentEntry = current->getClosestPoint(origin);
    if (!getMap().tileExists(origin) || !getMap().tileExists(currentEntry)) return false;
    auto distance = [](Coord a, Coord b) { return std::max(std::abs(a.x-b.x), std::abs(a.y-b.y)); };
    if (distance(origin, currentEntry) <= 3) return false;

    // Same deterministic fixed-point walking-speed allowance as forecastSpiceTrip,
    // using this worker variant's speed rather than assuming a normal harvester.
    const FixPoint speed = currentGame->objectData.data[harvester->getItemID()][getHouse()->getHouseID()].maxspeed * 0.75_fix;
    if (speed <= 0) return false;
    const int cyclesPerTile = std::max(1, (FixPoint(TILESIZE) / speed).lround());
    constexpr int unloadCycles = HARVESTERMAXSPICE * 8 / 5;
    auto expectedCycles = [&](const StructureBase* dropoff, Coord entry) -> int64_t {
        // Our current target's booking count includes us; other bookings are ahead
        // of us. Occupied unloading capacity contributes one more full unload.
        const int ahead = std::max(0, dropoff->getHarvesterDropoffBookings() - (dropoff == current ? 1 : 0))
            + (dropoff->isHarvesterDropoffFree() ? 0 : 1);
        return int64_t(distance(origin, entry)) * cyclesPerTile + int64_t(ahead) * unloadCycles;
    };
    const int64_t currentCycles = expectedCycles(current, currentEntry);
    if (currentCycles < unloadCycles) return false;

    const int width = getMap().getSizeX(), height = getMap().getSizeY();
    // Use exactly the safety pass's field and escape-corridor rule. No new
    // visibility decisions or path searches, and never redirect without its field.
    if (harvesterDanger.size() != static_cast<size_t>(width) * height) return false;
    auto danger = [&](int x, int y) { return harvesterDanger[static_cast<size_t>(y) * width + x]; };
    const int originRegion = getMap().vehicleTerrainRegion(origin);
    if (originRegion == 0) return false;

    const Uint32 now = getGameCycleCount();
    if (refineryReturnDropoffsCycle != now) {
        refineryReturnDropoffsCycle = now;
        refineryReturnDropoffs.clear();
        for (const auto* structure : getStructureList())
            if (structure->getOwner() == getHouse() && structure->isActive()
                && structure->getHealth() > 0 && structure->acceptsHarvesterDropoff())
                refineryReturnDropoffs.push_back(structure->getObjectID());
    }
    const StructureBase* best = current;
    int64_t bestCycles = currentCycles;
    for (Uint32 id : refineryReturnDropoffs) {
        const auto* dropoff = dynamic_cast<const StructureBase*>(getObject(id));
        if (!dropoff || dropoff == current || dropoff->getOwner() != getHouse()
            || !dropoff->isActive() || dropoff->getHealth() <= 0 || !dropoff->acceptsHarvesterDropoff()) continue;
        const Coord entry = dropoff->getClosestPoint(origin);
        if (!getMap().tileExists(entry)) continue;
        const int64_t cycles = expectedCycles(dropoff, entry);
        if (currentCycles - cycles < unloadCycles || cycles > bestCycles
            || (cycles == bestCycles && dropoff->getObjectID() >= best->getObjectID())) continue;
        if (getMap().vehicleTerrainRegion(entry) != originRegion || danger(entry.x, entry.y) > 0
            || !TacticalSafetyPolicy::escapeCorridor(origin.x, origin.y, entry.x, entry.y, danger)) continue;
        best = dropoff;
        bestCycles = cycles;
    }
    if (best == current) return false;

    // Player::doMove2Object applies immediately; Harvester::setTarget unbooks the
    // old bay and books the new one before the next worker is considered. Removing
    // our own booking above, plus a full-unload improvement and the near-bay guard,
    // prevents a retarget from reversing merely because it changed the bookings.
    doMove2Object(harvester, best);
    traceDecision("refinery_return_balance", AITelemetry::Record()
        .set("object", harvester->getObjectID()).set("from", current->getObjectID())
        .set("to", best->getObjectID()).set("saved_cycles", currentCycles - bestCycles));
    return true;
}
