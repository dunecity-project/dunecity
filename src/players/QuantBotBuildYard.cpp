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

// QuantBot build pass: a construction yard's turn. The legacy campaign rebuild list or the
// rule-ordered custom selection, then ordering, foundations and placement.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>

using QuantBotInternal::cityRoadImpact;
using QuantBotInternal::wouldLandlockNeighbouringZone;

namespace {
// Check the real build-list gates while allowing only this missing prerequisite.
bool prerequisiteBlocksBuild(const BuilderBase* builder, Uint32 goal, Uint32 prerequisite) {
    if (!builder || !builder->getOwner() || !currentGame || goal >= Num_ItemID) return false;
    const auto& data = currentGame->objectData.data[goal][builder->getOriginalHouseID()];
    if (prerequisite >= data.prerequisiteStructuresSet.size()
        || !data.enabled || data.builder != static_cast<int>(builder->getItemID())
        || data.techLevel > currentGame->techLevel
        || data.upgradeLevel > builder->getCurrentUpgradeLevel()
        || !data.prerequisiteStructuresSet[prerequisite]
        || builder->getOwner()->getNumItems(prerequisite) > 0) return false;
    for (int item = ItemID_FirstID; item < std::min<int>(Num_ItemID, data.prerequisiteStructuresSet.size()); ++item) {
        if (item != static_cast<int>(prerequisite) && isStructure(item)
            && data.prerequisiteStructuresSet[item] && builder->getOwner()->getNumItems(item) <= 0) return false;
    }
    return true;
}
} // namespace

void QuantBot::buildConstructionYard(BuildPass& pass, BuilderTurn& turn) {
    YardTurn yardTurn;
    yardTurn.pConstYard = static_cast<const ConstructionYard*>(turn.pBuilder);

    planYardLanes(pass, turn, yardTurn);

    // Each yard owns its concrete/structure placement sequence. Sharing
    // a single FIFO lets the faster yard consume the other's locations.
    planningBuilder = turn.pBuilder->getObjectID();
    clearPlacementCache(false,true);
    yardTurn.placeLocations = &builderPlaceLocations[turn.pBuilder->getObjectID()];
    if (turn.pBuilder->getProductionQueueSize() == 0) yardTurn.placeLocations->clear();
    traceYardStatus(pass, turn, yardTurn);

    if (!turn.pBuilder->isUpgrading() && getHouse()->getCredits() >= 100 && (turn.pBuilder->getProductionQueueSize() < 1) && turn.pBuilder->getBuildListSize()) {
        // Campaign Build order, iterate through the buildings, if the number that exist
        // is less than the number that should exist, then build the one that is missing

        // City campaigns need the same economy/civic planner as
        // city custom games. The classic rebuild list puts zones
        // behind upgrades and defences and can starve them forever.
        if (!pass.citySimEnabled && gameMode == GameMode::Campaign && difficulty != Difficulty::Brutal) {
            if (buildCampaignYard(pass, turn, yardTurn)) return;
        }
        else {
            if (buildCustomYard(pass, turn, yardTurn)) return;
        }
    }

    if (placeYardStructure(pass, turn, yardTurn)) return;

    // Pre-plan queue=0 often means a yard is about to receive
    // an order, not that it stayed idle. Record the outcome too.
    if (pass.emitStatsLog && AITelemetry::log().enabled()) {
        const char* result = turn.pBuilder->isUpgrading() ? "upgrading"
            : turn.pBuilder->getProductionQueueSize() > 0 ? "queued"
            : getHouse()->getCredits() <= 100 ? "insufficient_cash"
            : turn.pBuilder->getBuildListSize() == 0 ? "no_build_options"
            : "empty_after_planning";
        traceDecision("yard_planning_result", AITelemetry::Record()
            .set("builder", turn.pBuilder->getObjectID()).set("result", result)
            .set("queue", turn.pBuilder->getProductionQueueSize())
            .set("upgrading", turn.pBuilder->isUpgrading()).set("hold", turn.pBuilder->isOnHold())
            .set("state", decisionState(pass)));
    }
}

void QuantBot::planYardLanes(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Compare the funded next-wave mix with fielded AND queued units.
    // Cash targets must not hide a heavy-unit production bottleneck.
    const int fundedArmy = pass.vehiclePlanValue;
    int heavyValue = 0;
    for (const auto unit : {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
            Unit_Devastator, Unit_SonicTank, Unit_Deviator})
        heavyValue += pass.itemCount[unit] * data[unit][houseID].price;
    yardTurn.heavyDeficit = std::max(0,
        (fundedArmy * (pass.tankPercent + pass.siegePercent + pass.launcherPercent + pass.specialPercent)).lround() - heavyValue);
    yardTurn.lightDeficit = std::max<int64_t>(0,int64_t(fundedArmy)*pass.lightVehicleBps/10000-pass.lightVehicleValue);
    yardTurn.lightBacklog = !getHouse()->isGroundUnitLimitReached()
        && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_LightFactory),
            pass.itemCount[Structure_LightFactory],pass.activeLightFactoryCount,yardTurn.lightDeficit,
            pass.money,pass.economyReserve,data[Structure_LightFactory][houseID].price);
    yardTurn.airDeficit = std::max(0, (fundedArmy * pass.ornithopterPercent).lround()
        - pass.itemCount[Unit_Ornithopter] * data[Unit_Ornithopter][houseID].price);
    yardTurn.heavyBacklog = !getHouse()->isGroundUnitLimitReached()
        && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_HeavyFactory),
            pass.itemCount[Structure_HeavyFactory], pass.activeHeavyFactoryCount, yardTurn.heavyDeficit,
            pass.money, pass.economyReserve, data[Structure_HeavyFactory][houseID].price);
    yardTurn.airBacklog = QuantBotBuildPolicy::needsAirProductionLane(
        getHouse()->getNumItems(Structure_HighTechFactory), pass.itemCount[Structure_HighTechFactory],
        pass.activeHighTechFactoryCount, pass.ornithopterCapableFactoryCount, yardTurn.airDeficit,
        pass.money, pass.economyReserve, data[Structure_HighTechFactory][houseID].price,
        data[Unit_Ornithopter][houseID].price, pass.militaryBudget-pass.militaryValue,
        getHouse()->isAirUnitLimitReached());
}

void QuantBot::traceYardStatus(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (pass.emitStatsLog) {
        logDebug("PRODUCTION: CY=%u upgrading=%d queue=%d hold=%d credits=%d buildList=%d R/C/I=%d/%d/%d reserve=%u/%d",
            turn.pBuilder->getObjectID(), turn.pBuilder->isUpgrading(), turn.pBuilder->getProductionQueueSize(),
            turn.pBuilder->isOnHold(), pass.money, turn.pBuilder->getBuildListSize(),
            pass.itemCount[Structure_ZoneResidential], pass.itemCount[Structure_ZoneCommercial],
            pass.itemCount[Structure_ZoneIndustrial], pass.strategicReserveItem, pass.strategicReserveCost);
        auto* balanceCity = currentGame ? currentGame->getCitySimulation() : nullptr;
        const int taxIncome = pass.citySimEnabled
            ? DuneCity::computeAnnualTaxRevenue(pass.ownTaxBaseEighths, balanceCity ? balanceCity->getCityTax() : 7, pass.ownAvgLandValue) / 60 : 0;
        int factoryTarget = QuantBotBuildPolicy::desiredHeavyFactories(pass.citySimEnabled, taxIncome, pass.money, getHouse()->getNumItems(Structure_HeavyFactory), pass.activeHeavyFactoryCount, recentFactoryLossCount());
        if (pass.vanillaEconomy) factoryTarget = DuneCity::vanillaFactoryTarget(factoryTarget, getHouse()->getNumItems(Unit_Harvester), pass.money);
        const int tech = currentGame ? currentGame->techLevel : 8;
        const bool policyPrerequisites = pass.citySimEnabled || pass.vanillaEconomy || tech <= 4
            || (pass.itemCount[Structure_RepairYard] > 0 && (tech <= 6 || pass.itemCount[Structure_IX] > 0));
        const char* factoryReason = !campaignAvailableToBuild(turn.pBuilder,Structure_HeavyFactory) ? "unavailable"
            : pass.money <= std::max(2000, pass.economyReserve + data[Structure_HeavyFactory][houseID].price) ? "cash-reserve"
            : pass.militaryValue >= pass.militaryBudget ? "military-limit"
            : getHouse()->isGroundUnitLimitReached() ? "unit-limit"
            : yardTurn.heavyBacklog && pass.itemCount[Structure_HeavyFactory] < 24 ? "funded-unit-backlog"
            : pass.itemCount[Structure_HeavyFactory] >= factoryTarget ? "target-met"
            : !policyPrerequisites ? "tech-policy" : "expansion-due";
        traceDecision("builder_status", AITelemetry::Record().set("builder", turn.pBuilder->getObjectID())
            .set("queue", turn.pBuilder->getProductionQueueSize()).set("hold", turn.pBuilder->isOnHold())
            .set("upgrading", turn.pBuilder->isUpgrading()).set("heavy_target", factoryTarget)
            .set("heavy_reason", factoryReason).set("heavy_busy", pass.activeHeavyFactoryCount)
            .set("heavy_deficit", yardTurn.heavyDeficit).set("air_deficit", yardTurn.airDeficit)
            .set("heavy_backlog", yardTurn.heavyBacklog).set("air_backlog", yardTurn.airBacklog)
            .set("light_backlog",yardTurn.lightBacklog).set("light_busy",pass.activeLightFactoryCount).set("light_deficit",yardTurn.lightDeficit)
            .set("high_tech_busy", pass.activeHighTechFactoryCount)
            .set("high_tech_air_capable", pass.ornithopterCapableFactoryCount)
            .set("air_army_room", pass.militaryBudget-pass.militaryValue)
            .set("high_tech_building_ornithopters", pass.ornithopterFactoryCount)
            .set("heavy_economy_target", pass.vanillaEconomy ? std::max(1, getHouse()->getNumItems(Unit_Harvester) / 3) : 0)
            .set("heavy_cash_target", pass.vanillaEconomy ? 1 + std::max(0, pass.money - 10000) / 4000 : 0)
            .set("heavy_opening_target", pass.vanillaEconomy ? std::min(factoryTarget, 2 * std::clamp(pass.itemCount[Structure_ConstructionYard], 1, 8)) : 0)
            .set("heavy_losses_2min", recentFactoryLossCount())
            .set("repair_busy", pass.activeRepairYardCount).set("repair_target",pass.repairTarget)
            .set("state", decisionState(pass)));
        logDebug("BUILD-BALANCE: CY=%u HF=%d queued=%d busy=%d target=%d reason=%s RY=%d queued=%d busy=%d cap=%d taxPerSec=%d power=%d/%d",
            turn.pBuilder->getObjectID(), getHouse()->getNumItems(Structure_HeavyFactory),
            pass.itemCount[Structure_HeavyFactory] - getHouse()->getNumItems(Structure_HeavyFactory),
            pass.activeHeavyFactoryCount, factoryTarget, factoryReason,
            getHouse()->getNumItems(Structure_RepairYard),
            pass.itemCount[Structure_RepairYard] - getHouse()->getNumItems(Structure_RepairYard),
            pass.activeRepairYardCount, pass.repairTarget,
            taxIncome, getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
    }
}

bool QuantBot::buildCampaignYard(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    //logDebug("GameMode Campaign.. ");

    if (storageExpansionNeeded(pass) && getHouse()->hasPower()
        && campaignAvailableToBuild(turn.pBuilder,Structure_Silo)
        && findPlaceLocation(Structure_Silo).isValid()
        && queueFoundedStructure(pass, turn, Structure_Silo,__LINE__,"spice_storage_priority")) {
        ++pass.itemCount[Structure_Silo];
        return true;
    }

    for (int i = Structure_FirstID; i <= Structure_LastID; i++) {
        if (pass.itemCount[i] < initialItemCount[i]
            && campaignAvailableToBuild(turn.pBuilder,i)
            && findPlaceLocation(i).isValid()
            && !turn.pBuilder->isUpgrading()
            && turn.pBuilder->getProductionQueueSize() < 1) {

            if (queueCampaignWindtrap(pass, turn, std::max(0,data[i][houseID].power))) break;
            logDebug("***CampAI Build itemID: %o structure count: %o, initial count: %o", i, pass.itemCount[i], initialItemCount[i]);
            if (queueFoundedStructure(pass, turn, i, __LINE__)) pass.itemCount[i]++;  // Increment immediately to prevent multiple CYs from building same item
        }
    }

    // If Campaign AI can't build military, let it build up its cash reserves and defenses

    if (turn.pStructure->getHealth() < turn.pStructure->getMaxHealth()) {
        doRepair(turn.pBuilder);
        int health = turn.pStructure->getHealth().lround();
        int maxHealth = turn.pStructure->getMaxHealth();
        logDebug("PRODUCTION: Repairing CY, health: %d/%d", health, maxHealth);
    }
    else if (turn.pBuilder->getCurrentUpgradeLevel() < turn.pBuilder->getMaxUpgradeLevel()
        && !turn.pBuilder->isUpgrading()
        && pass.itemCount[Unit_Harvester] >= harvesterLimit
        && pass.money > 1500) {  // Don't upgrade if low on money (need money for structures/units)

        upgradeWithLogging(pass, turn, __LINE__);
        logDebug("PRODUCTION: Upgrading CY to level %d, credits: %d", turn.pBuilder->getCurrentUpgradeLevel() + 1, pass.money);
    }
    else if ((!getHouse()->hasPower())
        && turn.pBuilder->getProductionQueueSize() == 0) {
        // Prefer nuclear plant over windtrap
        if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant))
            && findPlaceLocation(Structure_NuclearPlant).isValid()) {
            if (queueFoundedStructure(pass, turn, Structure_NuclearPlant, __LINE__))
                pass.itemCount[Structure_NuclearPlant]++;
            logDebug("***CampAI Build Nuclear Plant: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
        } else if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap))
            && findPlaceLocation(Structure_WindTrap).isValid()) {
            if (queueFoundedStructure(pass, turn, Structure_WindTrap, __LINE__)) pass.itemCount[Structure_WindTrap]++;
            logDebug("***CampAI Build windtrap: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
        }
    }
    // Legacy campaign reconstruction shares the economic
    // generation gate: Vanilla's hasPower() above never
    // reports the raw shortage that degrades the base.
    else if (const Uint32 generator = economicGenerator(pass, turn); generator != NONE_ID) {
        if (queueFoundedStructure(pass, turn, generator, __LINE__, "power_investment"))
            ++pass.itemCount[generator];
    }
    else if (pass.money > 3000
        && campaignAvailableToBuild(turn.pBuilder,Structure_RocketTurret)
        && turn.pBuilder->getProductionQueueSize() == 0
        && (pass.itemCount[Structure_RocketTurret] <
            (pass.itemCount[Structure_Silo] + pass.itemCount[Structure_Refinery]) * 2)
        && findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {

        if (queueFoundedStructure(pass, turn, Structure_RocketTurret, __LINE__)) pass.itemCount[Structure_RocketTurret]++;

        logDebug("***CampAI Build A new Rocket turret increasing count to: %d", pass.itemCount[Structure_RocketTurret]);
    }
    // City zone structures for campaign AI — pick by live demand AND ratio.
    else if (currentGame && currentGame->isCitySimEnabled()
        && pass.money > 200
        && turn.pBuilder->getProductionQueueSize() == 0
        && pass.itemCount[Structure_WindTrap] > 0) {
        const int resCount = pass.itemCount[Structure_ZoneResidential];
        const int comCount = pass.itemCount[Structure_ZoneCommercial];
        const int indCount = pass.itemCount[Structure_ZoneIndustrial];
        const Uint32 zoneID = chooseCityZone(pass, turn.pBuilder, false);

        if (zoneID != NONE_ID && campaignAvailableToBuild(turn.pBuilder,zoneID)
            && findPlaceLocation(zoneID).isValid()) {
            if (queueFoundedStructure(pass, turn, zoneID, __LINE__)) pass.itemCount[zoneID]++;
            logDebug("***CampAI CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d)",
                getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
                pass.ownResValve, pass.ownComValve, pass.ownIndValve);
        }
    }

    // MULTIPLAYER FIX: Use deterministic timer instead of random
    buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
    return false;
}

bool QuantBot::buildCustomYard(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    startYardSelection(pass, turn, yardTurn);
    chooseYardAirDefence(pass, turn, yardTurn);
    chooseYardEssentials(pass, turn, yardTurn);
    chooseYardCityFoundations(pass, turn, yardTurn);
    chooseYardCoreEconomy(pass, turn, yardTurn);
    chooseYardCityOpening(pass, turn, yardTurn);
    chooseYardServicesAndRefineries(pass, turn, yardTurn);
    chooseYardProductionLanes(pass, turn, yardTurn);
    chooseYardMilitaryInfrastructure(pass, turn, yardTurn);
    chooseYardTechAndExpansion(pass, turn, yardTurn);
    chooseYardCivicAndDefence(pass, turn, yardTurn);
    vetYardChoice(pass, turn, yardTurn);
    if (placeYardChoice(pass, turn, yardTurn)) return true;
    useSpareYardCapacity(pass, turn, yardTurn);
    return false;
}

void QuantBot::startYardSelection(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    // custom AI starts here:

    yardTurn.itemID = NONE_ID;
    yardTurn.structureRule = "no_eligible_structure";
    yardTurn.crimeServiceSite = Coord::Invalid();
    // Set when this yard is short of a demanded service's price and
    // the forecast can still reach it. Cheap zoning must not spend
    // that budget, or the service is never ordered at all.
    yardTurn.serviceSavingHold = false;
    yardTurn.skipRemainingStructureLogic = false;
    // Evaluated once for this yard and this pass, then used by both
    // the protected-plot guard below and the civic rule itself, so
    // the bounded site search runs once rather than per rule.
    yardTurn.civicPlan = planCivicForYard(pass, turn.pBuilder);
    yardTurn.civicClaimsYard = yardTurn.civicPlan.order != NONE_ID || yardTurn.civicPlan.upgrade;
    // Protection that the service evaluation actually found to be an
    // emergency, plus enemy aircraft already over our own buildings,
    // keep their place above required civics. Routine coverage, its
    // saving and a merely high capital score do not: that is what
    // took the yard on pass after pass while the city stayed capped.
    yardTurn.urgentProtection = pass.airEngaged
        || (capitalPending(pass) && pass.capitalCandidates[pass.capitalChoice].urgent);

    if (storageExpansionNeeded(pass) && getHouse()->hasPower()
        && campaignAvailableToBuild(turn.pBuilder,Structure_Silo)
        && pass.money >= buildingCapitalCost(pass, Structure_Silo)
        && findPlaceLocation(Structure_Silo).isValid()) {
        yardTurn.itemID=Structure_Silo;
        yardTurn.structureRule="spice_storage_priority";
        yardTurn.skipRemainingStructureLogic=true;
    }

    // Honour the growth allocation in the actual yard decision,
    // not just the shared cash reserve. Previously this yard could
    // immediately spend the protected plot's budget on a service.
    // An announced growth cap outranks the protected plot: while the
    // civic is missing the valve for that plot's own demand is pinned
    // at 0, so this allocation would buy a lot that cannot develop and
    // spend the civic's down payment doing it. The civic rule below
    // owns the decision in that case.
    if (yardTurn.itemID==NONE_ID && pass.cityGrowthProtected && capitalPending(pass)
        && pass.capitalCandidates[pass.capitalChoice].builder==turn.pBuilder->getObjectID()
        && !yardTurn.civicClaimsYard) {
        yardTurn.itemID=affordableCityZone(pass, turn.pBuilder,pass.money);
        if (yardTurn.itemID!=NONE_ID) yardTurn.structureRule="dedicated_city_growth";
    }

    // Skip build order if something is already queued
    if (turn.pBuilder->getProductionQueueSize() > 0) {
        yardTurn.skipRemainingStructureLogic = true;
    }
}

// True once a growth-cap rule below has selected its project on a
// production start budget rather than its full price.
bool QuantBot::onInstallments(BuilderTurn& turn, Uint32 item) {
    return item != Uint32(NONE_ID) && item == turn.installmentProject;
}

void QuantBot::chooseYardAirDefence(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Count enemy ornithopters - use MAXIMUM from a single enemy house, not sum
    yardTurn.maxEnemyOrnithopters = 0;
    int totalEnemyOrnithopters = 0;
    if (currentGame) {
        for (int i = 0; i < NUM_HOUSES; i++) {
            const House* pHouse = currentGame->getHouse(i);
            if (pHouse && pHouse->getTeamID() != getHouse()->getTeamID()) {
                int houseOrnis = pHouse->getNumItems(Unit_Ornithopter);
                totalEnemyOrnithopters += houseOrnis;
                if (houseOrnis > yardTurn.maxEnemyOrnithopters) {
                    yardTurn.maxEnemyOrnithopters = houseOrnis;
                }
            }
        }
    }
    // Counter-air demand, as before. It is no longer the only
    // driver: a base facing a purely ground opponent used to cap
    // itself at the two baseline emplacements for the whole match,
    // because this figure is zero without enemy aircraft.
    yardTurn.counterAirTurrets = std::max(yardTurn.maxEnemyOrnithopters * 2, totalEnemyOrnithopters);
    // Demand-scaled enemy-facing battery for Custom Hard/Brutal,
    // gated on the economy that pays for it and bounded to one
    // order per pass below. Zero outside that mode.
    if (recoveryActive() && lastSurveyCycle != getGameCycleCount()) {
        lastSurvey = surveyArmy();
        lastSurveyCycle = getGameCycleCount();
    }
    yardTurn.batteryTurrets = frontBatteryGoal(
        RocketTurretPolicy::coverageTurretCap(pass.coverageDemand),
        pass.itemCount[Structure_Refinery], pass.itemCount[Structure_HeavyFactory],
        pass.itemCount[Structure_RepairYard]);
    const int requiredTurrets = std::max(yardTurn.counterAirTurrets, yardTurn.batteryTurrets);
    const bool ixExpected = data[Structure_IX][houseID].enabled
        && data[Structure_IX][houseID].techLevel <= currentGame->techLevel;
    const bool palaceExpected = data[Structure_Palace][houseID].enabled
        && data[Structure_Palace][houseID].techLevel <= currentGame->techLevel;
    const bool strategicInfrastructureIncomplete = pass.customStrategicPlanning
        && ((ixExpected && pass.itemCount[Structure_IX] == 0)
            || (palaceExpected && pass.itemCount[Structure_Palace] == 0));
    // The interim cap left a large base with two emplacements
    // while optional tech finished, which cannot span it.
    // Scale it with the cover the base actually demands —
    // which now grows with difficulty — while the enemy
    // wing still caps the goal itself.
    yardTurn.activeRocketTurretGoal = strategicInfrastructureIncomplete
        ? std::min(requiredTurrets, RocketTurretPolicy::coverageTurretCap(pass.coverageDemand))
        : requiredTurrets;


    // CRITICAL: Counter enemy ornithopters ASAP (prep prerequisites if needed)
    // An enemy wing seen elsewhere on the map is a plan, not a
    // present loss: it buys the baseline emplacements and its own
    // prerequisites, then waits behind the production core.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && yardTurn.counterAirTurrets > 0
        && pass.routineRocketsAllowed
        && pass.itemCount[Structure_RocketTurret] < yardTurn.activeRocketTurretGoal) {
        bool hasWindtrap = pass.itemCount[Structure_WindTrap] > 0;
        bool hasRadar = pass.itemCount[Structure_Radar] > 0;

        if (turn.pBuilder->getCurrentUpgradeLevel() < 2) {
            if (turn.pBuilder->getHealth() < turn.pBuilder->getMaxHealth() && !turn.pBuilder->isRepairing()) {
                doRepair(turn.pBuilder);
                logDebug("COUNTER-ORNITHOPTER: Repairing CY before upgrade (level %d)", turn.pBuilder->getCurrentUpgradeLevel());
            } else if (!turn.pBuilder->isUpgrading() && turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth()) {
                upgradeWithLogging(pass, turn, __LINE__);
                logDebug("COUNTER-ORNITHOPTER: Upgrading CY (level %d -> %d)", turn.pBuilder->getCurrentUpgradeLevel(), turn.pBuilder->getCurrentUpgradeLevel() + 1);
            }
        } else if (!hasWindtrap && (!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap))) {
            yardTurn.itemID = Structure_WindTrap; yardTurn.structureRule = "power";
            logDebug("COUNTER-ORNITHOPTER: Building windtrap prerequisite (enemy ornis: %d)", yardTurn.maxEnemyOrnithopters);
        } else if (!hasRadar && campaignAvailableToBuild(turn.pBuilder,Structure_Radar) && getHouse()->hasPower()) {
            yardTurn.itemID = Structure_Radar; yardTurn.structureRule = "radar_prerequisite";
            logDebug("COUNTER-ORNITHOPTER: Building radar prerequisite (enemy ornis: %d)", yardTurn.maxEnemyOrnithopters);
        } else if (!hasPowerBufferForTurret(pass)) {
            int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
            if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant))
                && findPlaceLocation(Structure_NuclearPlant).isValid()) {
                yardTurn.itemID = Structure_NuclearPlant; yardTurn.structureRule = "power";
                logDebug("COUNTER-ORNITHOPTER: Nuclear Plant for turret power (excess: %d)", powerExcess);
            } else if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap))
                && findPlaceLocation(Structure_WindTrap).isValid()) {
                yardTurn.itemID = Structure_WindTrap; yardTurn.structureRule = "power";
                logDebug("COUNTER-ORNITHOPTER: Windtrap for turret power (excess: %d)", powerExcess);
            }
        } else if (campaignAvailableToBuild(turn.pBuilder,Structure_RocketTurret)
            && hasPowerBufferForTurret(pass)
            && findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
            yardTurn.itemID = Structure_RocketTurret; yardTurn.structureRule = "rocket_defense";
            logDebug("COUNTER-ORNITHOPTER: Building rocket turret (enemy ornis: %d, target turrets: %d)", yardTurn.maxEnemyOrnithopters, yardTurn.activeRocketTurretGoal);
        }
    }
}

// Power buffer check for the next turret and city growth.
// Only applies if rocketTurretsNeedPower is enabled
bool QuantBot::hasPowerBufferForTurret(BuildPass& pass) {
    if (!pass.turretPowerRequired) {
        return true; // No power requirement, always allow
    }
    int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
    // Keep the city-specific headroom after powering the turret.
    return powerExcess >= pass.rocketPowerBuffer;
}

void QuantBot::chooseYardEssentials(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // The bulk-concrete upgrade already ran ahead of this
    // whole selection, for every mode and difficulty.

    // Essential infrastructure - Build Order:
    // 1. WindTrap (if 0)
    // 2. Refinery (if 0)
    // 3. Refinery (ratio with harvesters)
    // 4. Refinery (< 4, money < 2000)
    // 5. Starport when the map offers useful imports
    // 6. Radar
    // 7. Light Factory
    // 8. Repair Yard (if starport or heavy factory exists)
    // 8b. 2 Rocket Turrets (if starport or heavy factory exists)
    // 8c. Counter ornithopters (turrets < 2x max enemy ornis)
    // 9. Heavy Factory (money > 500)
    // 10. High Tech Factory (if no carryalls in CHOAM or no starport)

    // 1. WindTrap
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.itemCount[Structure_WindTrap] == 0
        && (!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap))) {
        yardTurn.itemID = Structure_WindTrap; yardTurn.structureRule = "power";
    }
    // 1b. Power Deficit Recovery - prefer nuclear plant, fall back to windtrap
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && (!getHouse()->hasPower() || (pass.turretPowerRequired
            && pass.itemCount[Structure_RocketTurret] > 0
            && getHouse()->getProducedPower() < getHouse()->getPowerRequirement()))) {
        int powerDeficit = getHouse()->getPowerRequirement() - getHouse()->getProducedPower();
        if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant))
            && pass.money >= data[Structure_NuclearPlant][houseID].price
            && findPlaceLocation(Structure_NuclearPlant).isValid()) {
            yardTurn.itemID = Structure_NuclearPlant; yardTurn.structureRule = "power";
            logDebug("POWER-RECOVERY: Building Nuclear Plant for power deficit (%d)", powerDeficit);
        } else if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap))
            && findPlaceLocation(Structure_WindTrap).isValid()) {
            yardTurn.itemID = Structure_WindTrap; yardTurn.structureRule = "power";
            logDebug("POWER-RECOVERY: Building windtrap for power deficit (%d)", powerDeficit);
        }
    }
    // 1c. Economic generation outside the city simulation. Ahead of
    // every optional purchase below, behind the first windtrap and
    // the functional recovery above, and only when the documented
    // gate accepts the whole package.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic) {
        const Uint32 generator = economicGenerator(pass, turn);
        if (generator != NONE_ID) {
            yardTurn.itemID = generator; yardTurn.structureRule = "power_investment";
            yardTurn.skipRemainingStructureLogic = true;
        }
    }
}

void QuantBot::chooseYardCityFoundations(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // No usable spice: the demanded lots ARE the opening. They do
    // not wait for a harvesting economy, but they do need somewhere
    // to bank what they earn. This sits directly below essential
    // power and a live air attack, and above crime service, opening
    // tech, repair capacity, peaceful coverage and production: a
    // 500-credit police station cannot pay for itself while the
    // house owns no taxable lot, and buying it first is how the
    // grant ran out. The zone is demand-ranked by the ordinary
    // chooser, so a negative-demand lot is never forced; when no
    // lot is available this rule yields the pass rather than
    // holding the cash.
    const bool liveAirThreat = pass.protectionCapital && capitalPending(pass)
        && pass.capitalCandidates[pass.capitalChoice].reason == std::string("air_coverage");
    // Reserve the vault footprint before housing can occupy it.
    // Keeping the zone seed protected after this purchase prevents
    // the first tiny tax receipt from unlocking optional technology.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.citySimEnabled
        && !liveAirThreat && cityBootstrapNeeded(pass)
        && CityEconomyInvestmentPolicy::cityStorageMissing(pass.cityStorageCapacityIncludingQueued)
        && pass.cityStorageItem != NONE_ID) {
        yardTurn.skipRemainingStructureLogic = true;
        yardTurn.structureRule = "city_saving_for_income_storage";
        if (pass.money >= buildingCapitalCost(pass, pass.cityStorageItem)) {
            yardTurn.itemID = pass.cityStorageItem;
            yardTurn.structureRule = "city_income_storage";
        }
        if (pass.emitStatsLog) traceDecision("city_income_storage", AITelemetry::Record()
            .set("item",pass.cityStorageItem).set("cost",pass.cityStorageCost)
            .set("capacity",data[pass.cityStorageItem][houseID].capacity)
            .set("capacity_including_queued",pass.cityStorageCapacityIncludingQueued)
            .set("zones_including_queued",pass.cityZonesIncludingQueued)
            .set("credits",pass.money).set("spice_remaining",lastCalculatedSpice)
            .set("funded",yardTurn.itemID != NONE_ID));
    }
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.citySimEnabled
        && !liveAirThreat && cityBootstrapNeeded(pass)
        && pass.cityZonesIncludingQueued < CityEconomyInvestmentPolicy::kBootstrapZoneSeed) {
        const Uint32 seed = chooseCityZone(pass, turn.pBuilder,true);
        if (seed != NONE_ID) {
            yardTurn.skipRemainingStructureLogic = true;
            yardTurn.structureRule = "city_saving_for_no_spice_bootstrap";
            const int storageSaving=CityEconomyInvestmentPolicy::cityStorageMissing(
                pass.cityStorageCapacityIncludingQueued) ? pass.cityStorageCost : 0;
            if (pass.money >= buildingCapitalCost(pass, seed)+storageSaving) {
                yardTurn.itemID = seed;
                yardTurn.structureRule = "city_no_spice_bootstrap";
            } else if (storageSaving>0 && pass.money>=storageSaving) {
                // Bank income from the lots already committed before
                // another lot consumes the vault's remaining cash.
                yardTurn.skipRemainingStructureLogic=false;
            }
            if (pass.emitStatsLog) traceDecision("city_no_spice_bootstrap", AITelemetry::Record()
                .set("item",seed).set("zones_including_queued",pass.cityZonesIncludingQueued)
                .set("seed_target",CityEconomyInvestmentPolicy::kBootstrapZoneSeed)
                .set("tax_base_eighths",pass.ownTaxBaseEighths).set("credits",pass.money)
                .set("price",purchasePrice(turn.pBuilder,seed))
                .set("lot_cost",pass.cityBootstrapLotCost).set("reserved",cityBootstrapReserve(pass))
                .set("spice_remaining",lastCalculatedSpice).set("funded",yardTurn.itemID != NONE_ID));
        }
    }
    // A structural civic cap is required infrastructure, not optional
    // growth. While the civic is missing, the valve for the zones this
    // house already owns is pinned at 0 (DuneCity::computeDemandValves),
    // so every optional purchase below buys capacity that cannot
    // develop and spends the civic's down payment doing it. It
    // therefore outranks routine protection coverage and its saving,
    // profitable-spice and opening tech, planned reactor capacity and
    // ordinary zoning. Live blackout recovery, storage overflow,
    // observed enemy aircraft and an actual dangerous-crime emergency
    // all stay above it.
    //
    // When the civic itself is not buildable yet, this orders or saves
    // for the missing link in the mod's own prerequisite chain - the
    // reason houses whose Radar was destroyed stayed capped for the
    // rest of the match. The project is charged in installments by the
    // engine out of arriving income, exactly as the reactor above is;
    // nothing here demands the full price inside a forecast window.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.citySimEnabled
        && !yardTurn.urgentProtection && yardTurn.civicClaimsYard) {
        if (yardTurn.civicPlan.upgrade) {
            const int cost = turn.pBuilder->getUpgradeCost();
            if (cost > 0 && (pass.money >= cost || pass.cashFlow.projectedCash >= cost)) {
                yardTurn.serviceSavingHold = true;
                yardTurn.skipRemainingStructureLogic = true;
                yardTurn.structureRule = "unlock_demanded_civic";
                if (pass.money >= cost && !turn.pBuilder->isUpgrading()
                    && turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth())
                    upgradeWithLogging(pass, turn, __LINE__);
            }
        } else {
            const Uint32 project = yardTurn.civicPlan.order;
            const bool isCivic = project == yardTurn.civicPlan.civic;
            const int price = data[project][houseID].price;
            const int foundationBudget = std::max(0, buildingCapitalCost(pass, project) - price);
            const auto funding = CityEconomyInvestmentPolicy::installmentOrder(
                pass.money, price, foundationBudget, cityBuildTicks(pass, project), pass.forecastNetIncome);
            if (funding.funded()) {
                yardTurn.skipRemainingStructureLogic = true;
                yardTurn.serviceSavingHold = funding.reserve;
                if (funding.order) {
                    turn.installmentProject = project;
                    // Execution asks only for the installment: the foundation
                    // orders are queued and charged separately before it, so
                    // demanding their cost again here would double count them.
                    turn.installmentProjectStartBudget = CityEconomyInvestmentPolicy::firstInstallment(
                        price,cityBuildTicks(pass, project));
                }
                yardTurn.itemID = funding.order ? project : NONE_ID;
                yardTurn.structureRule = yardTurn.itemID == NONE_ID
                    ? (isCivic ? "save_demanded_civic" : "save_civic_prerequisite")
                    : isCivic ? (project == Structure_Stadium ? "residential_civic" : "commercial_civic")
                    : "civic_prerequisite";
            }
        }
    }
    // Record the derived need, the clip mask it is deliberately not
    // taken from, and why this yard did or did not act on it. Emitted
    // at normal planning frequency only while a structural need
    // exists, so the next starvation report is falsifiable without
    // re-deriving the condition from demand valves.
    if (pass.civicStructuralNeed != 0) traceDecision("city_civic_demand",
        AITelemetry::Record().set("builder",turn.pBuilder->getObjectID())
            .set("structural_need",int(pass.civicStructuralNeed))
            .set("demand_clipped_now",int(pass.civicDemandClipped))
            .set("res_pop",pass.ownResPop).set("com_pop",pass.ownComPop)
            .set("res_demand",pass.ownResValve).set("com_demand",pass.ownComValve)
            .set("civic",yardTurn.civicPlan.civic).set("planned_order",yardTurn.civicPlan.order)
            .set("plan_reason",yardTurn.civicPlan.reason).set("yard_upgrade",yardTurn.civicPlan.upgrade)
            .set("already_ordered",yardTurn.civicPlan.pending)
            .set("stadium_committed",pass.itemCount[Structure_Stadium])
            .set("airport_committed",pass.itemCount[Structure_Airport])
            .set("urgent_protection",yardTurn.urgentProtection)
            .set("power_headroom",getHouse()->getProducedPower()-getHouse()->getPowerRequirement())
            .set("spendable",pass.money).set("continuing_income",pass.forecastNetIncome)
            .set("reserved",pass.civicReserveCost).set("rule",yardTurn.structureRule));
    // Honour the winning service before optional power headroom,
    // opening tech and production expansion. Required civics above and
    // actual blackout recovery stay ahead of it.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.protectionCapital && capitalPending(pass)
        && pass.capitalCandidates[pass.capitalChoice].builder == turn.pBuilder->getObjectID()) {
        const auto& service = pass.capitalCandidates[pass.capitalChoice];
        if (service.kind==std::string("expansion_upgrade")) {
            yardTurn.skipRemainingStructureLogic=true;yardTurn.serviceSavingHold=true;
            yardTurn.structureRule="save_expansion_upgrade";
            if (pass.money>=service.price && turn.pBuilder->getHealth()>=turn.pBuilder->getMaxHealth()
                && upgradeWithLogging(pass, turn, __LINE__)) {
                pass.capitalConsumed=true;yardTurn.structureRule="expansion_turret_upgrade";
            }
        } else if (campaignAvailableToBuild(turn.pBuilder, service.item) && service.site.isValid()) {
            yardTurn.itemID = pass.money >= service.price + service.foundationCost ? service.item : NONE_ID;
            yardTurn.crimeServiceSite = service.site;
            yardTurn.structureRule = yardTurn.itemID == NONE_ID ? "save_city_protection" : "city_protection";
            yardTurn.skipRemainingStructureLogic = true;
        }
    }
    yardTurn.higherPriorityCapital = capitalPending(pass)
        && pass.capitalCandidates[pass.capitalChoice].builder == turn.pBuilder->getObjectID()
        && pass.capitalCandidates[pass.capitalChoice].score > 2600;
    // Seed each demanded part of the tax economy once the spice
    // opening is running. Optional upgrades must not monopolize a
    // single yard before residents and jobs can start developing.
    // Active air defence, dangerous crime and blackouts stay above.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.citySimEnabled
        && !yardTurn.higherPriorityCapital && pass.cityYards == 1 && pass.itemCount[Structure_Refinery] >= 2) {
        const Uint32 seed = chooseCityZone(pass, turn.pBuilder, true, true);
        if (seed != NONE_ID) {
            if (getHouse()->getProducedPower()-getHouse()->getPowerRequirement() >= 24
                && pass.money >= buildingCapitalCost(pass, seed)) {
                yardTurn.itemID = seed;
                yardTurn.structureRule = "opening_city_seed";
            } else if (getHouse()->getProducedPower()-getHouse()->getPowerRequirement() < 24
                && !powerGenerationPending(pass)
                && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap)
                && pass.money >= buildingCapitalCost(pass, Structure_WindTrap)
                && findPlaceLocation(Structure_WindTrap).isValid()) {
                yardTurn.itemID = Structure_WindTrap;
                yardTurn.structureRule = "opening_city_power";
            }
        }
    }
    // On a rich field, keep compounding a profitable refinery's
    // included worker beyond four bays before optional construction.
    // One housing seed and the first two rockets retain their slots.
    if (yardTurn.itemID==NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.citySimEnabled
        && !yardTurn.higherPriorityCapital && pass.itemCount[Structure_RocketTurret]>=2
        && openingFleetIncomplete(pass) && pass.itemCount[Structure_ZoneResidential]>0) {
        const Uint32 investment=chooseCityEconomy(pass, turn.pBuilder,false);
        if (investment==Structure_Refinery) {
            yardTurn.itemID=pass.money>=buildingCapitalCost(pass, investment) ? investment : NONE_ID;
            yardTurn.structureRule=yardTurn.itemID==NONE_ID ? "save_profitable_spice" : "profitable_spice_expansion";
            yardTurn.skipRemainingStructureLogic=true;
            yardTurn.serviceSavingHold=yardTurn.itemID==NONE_ID;
        }
    }
}

void QuantBot::chooseYardCoreEconomy(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Protect the income/rebuild core before optional tech. Walk the
    // actual mod prerequisites and save their cost, rather than
    // waiting for enemy aircraft to reveal that the yard is unready.
    const bool completingCore=pass.itemCount[Structure_RocketTurret]>=2 && pass.missingCoreInfrastructure!=NONE_ID;
    const bool openingWorkerSupplier = pass.openingSupplierDue || completingCore;
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.citySimEnabled
        && !yardTurn.higherPriorityCapital
        && ((openingWorkerSupplier && pass.itemCount[Structure_RocketTurret] >= 2)
            || ((pass.itemCount[Structure_Refinery] >= 3 || pass.airEngaged)
                && proactiveCoverageShortfall(pass) > 0
                && data[Structure_RocketTurret][houseID].enabled
                && data[Structure_RocketTurret][houseID].techLevel <= currentGame->techLevel))) {
        // Once basic defence exists, unlock the producer that can
        // fill the opening fleet. Starport upgrades otherwise wait
        // behind cheap lots even on a rich spice field.
        const bool supplyingWorkers = openingWorkerSupplier && pass.itemCount[Structure_RocketTurret] >= 2;
        Uint32 step = completingCore ? pass.missingCoreInfrastructure : supplyingWorkers
            ? (pass.itemCount[Structure_Refinery]<4 ? Structure_Refinery
                : pass.starportMarketAvailable ? Structure_StarPort : Structure_HeavyFactory)
            : Structure_RocketTurret;
        if (!supplyingWorkers && !hasPowerBufferForTurret(pass)) step = Structure_WindTrap;
        if (completingCore && data[step][houseID].power>
            getHouse()->getProducedPower()-getHouse()->getPowerRequirement()) step=Structure_WindTrap;
        for (int depth=0; depth<Structure_LastID && step!=NONE_ID; ++depth) {
            if (!data[step][houseID].enabled || data[step][houseID].techLevel>currentGame->techLevel) break;
            if (step!=Structure_RocketTurret && pass.itemCount[step]>getHouse()->getNumItems(step)) break;
            if (campaignAvailableToBuild(turn.pBuilder,step)) {
                int coverage=0;
                const Coord site=step==Structure_RocketTurret
                    ? findCityTurretPlaceLocation(step,&coverage) : findPlaceLocation(step);
                if (site.isValid() && (step!=Structure_RocketTurret || coverage>0)) {
                    const int cost=buildingCapitalCost(pass, step);
                    if (pass.money>=cost || pass.cashFlow.projectedCash>=cost) {
                        yardTurn.itemID=pass.money>=cost ? step : NONE_ID;
                        if (yardTurn.itemID!=NONE_ID) yardTurn.crimeServiceSite=site;
                        yardTurn.serviceSavingHold=yardTurn.itemID==NONE_ID && proactiveSavingHold(pass, cost);
                        yardTurn.skipRemainingStructureLogic=true;
                        yardTurn.structureRule=supplyingWorkers ? (yardTurn.itemID==NONE_ID ? "save_opening_supplier" : "opening_supplier")
                            : yardTurn.itemID==NONE_ID ? "save_core_defence" : "core_defence";
                    }
                }
                break;
            }
            if (turn.pBuilder->getCurrentUpgradeLevel()<data[step][houseID].upgradeLevel
                && turn.pBuilder->getMaxUpgradeLevel()>=data[step][houseID].upgradeLevel) {
                const int cost=turn.pBuilder->getUpgradeCost();
                if (cost>0 && (pass.money>=cost || pass.cashFlow.projectedCash>=cost)) {
                    yardTurn.serviceSavingHold=proactiveSavingHold(pass, cost);yardTurn.skipRemainingStructureLogic=true;
                    yardTurn.structureRule=supplyingWorkers ? "unlock_opening_supplier" : "unlock_core_defence";
                    if (pass.money>=cost && !turn.pBuilder->isUpgrading()
                        && turn.pBuilder->getHealth()>=turn.pBuilder->getMaxHealth()) upgradeWithLogging(pass, turn, __LINE__);
                }
                break;
            }
            Uint32 missing=NONE_ID;
            for (int prerequisite=Structure_FirstID;prerequisite<=Structure_LastID;++prerequisite)
                if (data[step][houseID].prerequisiteStructuresSet[prerequisite]
                    && getHouse()->getNumItems(prerequisite)==0) {missing=prerequisite;break;}
            step=missing;
        }
    }
    // 1c. Cover zone maturation/recovery and queued consumers as
    // well as the normal reserve. Keep one generator in flight.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && currentGame && currentGame->isCitySimEnabled()
        && pass.itemCount[Structure_Refinery] >= QuantBotBuildPolicy::openingSpiceRefineries(pass.spiceHarvesterTarget)) {
        const int produced  = getHouse()->getProducedPower();
        const int required  = getHouse()->getPowerRequirement();
        const int buffer    = produced - required;
        const int targetBuffer = pass.cityPowerReserve;
        if (required > 0 && buffer < targetBuffer) {
            // Prefer an affordable nuclear plant for capacity; the shared
            // investment policy below retains cheap wind for small starts.
            if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant))
                && pass.money >= data[Structure_NuclearPlant][houseID].price
                && findPlaceLocation(Structure_NuclearPlant).isValid()) {
                yardTurn.itemID = Structure_NuclearPlant; yardTurn.structureRule = "power";
                logDebug("CITY-POWER: Building Nuclear Plant (buffer=%d, target=%d, required=%d)",
                         buffer, targetBuffer, required);
            } else if (((!powerGenerationPending(pass) || !getHouse()->hasPower())
                    && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap))
                && findPlaceLocation(Structure_WindTrap).isValid()) {
                // A reactor already in flight normally satisfies the one
                // generator this rule keeps pending. An actual blackout is
                // different: the reactor is charged in installments out of
                // income the blackout itself is destroying, so waiting for
                // it can starve its own funding indefinitely. Cheap wind is
                // buildable now and produceItemWithLogging's emergency
                // generator path pays for it even with the reactor's
                // obligation booked, so recovery never waits for capacity.
                yardTurn.itemID = Structure_WindTrap; yardTurn.structureRule = "power";
                logDebug("CITY-POWER: Building Windtrap (no Nuclear available, buffer=%d, target=%d, blackout=%d)",
                         buffer, targetBuffer, getHouse()->hasPower() ? 0 : 1);
            }
        }
    }
    // Accumulate reactor funds before optional orders consume them.
    // An actual blackout can still buy an immediately affordable windtrap.
    if (pass.nuclearPlan && !(pass.protectionCapital && capitalPending(pass))
        && getHouse()->hasPower() && !powerGenerationPending(pass)
        && campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant)
        && (yardTurn.itemID == NONE_ID || yardTurn.itemID == Structure_WindTrap || yardTurn.itemID == Structure_NuclearPlant)
        && findPlaceLocation(Structure_NuclearPlant).isValid()) {
        // The house still has power here - this is planned capacity, not
        // a blackout - so the reactor can be ordered on a down payment and
        // charged in installments like any other order. An actual shortfall
        // is answered above, where an immediately affordable windtrap still
        // wins, so emergency power never waits for a reactor's instalments.
        const int price = data[Structure_NuclearPlant][houseID].price;
        const int foundationBudget = std::max(0, buildingCapitalCost(pass, Structure_NuclearPlant) - price);
        const auto funding = CityEconomyInvestmentPolicy::installmentOrder(
            pass.money, price, foundationBudget, cityBuildTicks(pass, Structure_NuclearPlant),
            pass.forecastNetIncome);
        if (funding.funded()) {
            yardTurn.skipRemainingStructureLogic = true;
            yardTurn.serviceSavingHold = funding.reserve;
            if (funding.order) {
                turn.installmentProject = Structure_NuclearPlant;
                // Execution asks only for the installment: the foundation
                // orders are queued and charged separately before it, so
                // demanding their cost again here would double count them.
                turn.installmentProjectStartBudget = CityEconomyInvestmentPolicy::firstInstallment(
                    price,cityBuildTicks(pass, Structure_NuclearPlant));
            }
            yardTurn.itemID = funding.order ? Structure_NuclearPlant : NONE_ID;
            yardTurn.structureRule = yardTurn.itemID == NONE_ID ? "save_nuclear_growth" : "nuclear_growth_investment";
        }
        if (pass.emitStatsLog) traceDecision("nuclear_investment",AITelemetry::Record()
            .set("funded",yardTurn.itemID != NONE_ID).set("cash",pass.money)
            .set("price",price).set("foundation_budget",foundationBudget)
            .set("start_budget",CityEconomyInvestmentPolicy::productionStartBudget(
                price,foundationBudget,cityBuildTicks(pass, Structure_NuclearPlant)))
            .set("continuing_income",pass.forecastNetIncome).set("saving",funding.reserve)
            .set("reserve",pass.cityPowerReserve));
    }
    // Restore a lost heavy production line before optional growth.
    // Walk missing prerequisites (e.g. the light factory lost in the
    // same attack); pending buildings prevent duplicate orders.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.itemCount[Structure_HeavyFactory] == 0
        && getHouse()->getNumLostItems(Structure_HeavyFactory) > 0
        && data[Structure_HeavyFactory][houseID].enabled
        && data[Structure_HeavyFactory][houseID].techLevel <= currentGame->techLevel) {
        Uint32 step = Structure_HeavyFactory;
        for (int depth = 0; step != NONE_ID && depth < Structure_LastID; ++depth) {
            if (pass.itemCount[step] > 0) {
                yardTurn.skipRemainingStructureLogic = true;
                yardTurn.structureRule = "heavy_recovery_pending";
                break;
            }
            if (campaignAvailableToBuild(turn.pBuilder,step)) {
                if (findPlaceLocation(step).isValid()) {
                    yardTurn.skipRemainingStructureLogic = true;
                    yardTurn.structureRule = "save_heavy_recovery";
                    if (pass.money >= data[step][houseID].price) {
                        yardTurn.itemID = step;
                        yardTurn.structureRule = "heavy_production_recovery";
                    }
                }
                break;
            }
            Uint32 prerequisite = NONE_ID;
            for (int i = Structure_FirstID; i <= Structure_LastID; ++i)
                if (data[step][houseID].prerequisiteStructuresSet[i]
                    && getHouse()->getNumItems(i) == 0) { prerequisite = i; break; }
            step = prerequisite;
        }
    }
    if (yardTurn.itemID==NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.fundedFactoryOpening) {
        Uint32 step=Structure_HeavyFactory;
        for (int depth=0;step!=NONE_ID && depth<Structure_LastID;++depth) {
            if (pass.itemCount[step]>0) {
                // A pending prerequisite will complete normally; let
                // other yards use their own slots for useful growth.
                break;
            }
            if (campaignAvailableToBuild(turn.pBuilder,step)) {
                if (pass.money>=data[step][houseID].price && findPlaceLocation(step).isValid()) {
                    yardTurn.itemID=step;yardTurn.structureRule="funded_factory_opening";
                    yardTurn.skipRemainingStructureLogic=true;
                }
                break;
            }
            Uint32 prerequisite=NONE_ID;
            for (int i=Structure_FirstID;i<=Structure_LastID;++i)
                if (data[step][houseID].prerequisiteStructuresSet[i]
                    && getHouse()->getNumItems(i)==0) {prerequisite=i;break;}
            step=prerequisite;
        }
    }
    // Starport opening: four income-producing refineries, the port,
    // then its repair support before optional factories. Small/spice-poor
    // maps use fewer workers; missions that lock the port keep their
    // normal early-tech progression. Authored campaign rebuild lists
    // are handled above and do not acquire extra opening structures.
    // This is a spice-income ladder: income-producing refineries,
    // the port, then its repair support. On a city map whose spice
    // cannot support a worker that premise is false - the refinery
    // earns nothing and the port's prerequisite walk charges the
    // whole grant to it. Wait for the city's own tax base to fund
    // the port; the ladder then proceeds unchanged, prerequisites
    // included, so military imports stay reachable.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && !cityBootstrapNeeded(pass)
        && pass.starportMarketAvailable && !pass.fundedFactoryOpening
        && data[Structure_StarPort][houseID].enabled
        && currentGame->techLevel >= data[Structure_StarPort][houseID].techLevel) {
        Uint32 step = NONE_ID;
        if (getHouse()->getNumItems(Structure_StarPort) == 0) {
            const int refineryGoal = std::min(4, pass.mapSpiceHarvesterTarget);
            step = pass.itemCount[Structure_Refinery] < refineryGoal
                ? Structure_Refinery : Structure_StarPort;
        } else if (getHouse()->getNumItems(Structure_RepairYard) == 0
            && canAddRepairYard(pass.itemCount[Structure_RepairYard])
            && data[Structure_RepairYard][houseID].enabled
            && currentGame->techLevel >= data[Structure_RepairYard][houseID].techLevel) {
            step = Structure_RepairYard;
        } else if (!canAddRepairYard(pass.itemCount[Structure_RepairYard])
            && pass.itemCount[Structure_HeavyFactory] == 0
            && data[Structure_HeavyFactory][houseID].enabled
            && currentGame->techLevel >= data[Structure_HeavyFactory][houseID].techLevel) {
            // Medium builds no repair yard of its own, but the yard was also this
            // ladder's only way to the light factory and so to vehicle production:
            // walk to the heavy factory instead (7P FFA: Medium idle with 17
            // harvesters and no factory, docs/ai-scenario-matrix.md).
            step = Structure_HeavyFactory;
        }
        // Walk missing prerequisites, stopping at a pending build.
        // This uses each mod's actual tech tree, without adding heavy
        // or high-tech factories unless that tree requires them.
        for (int depth = 0; step != NONE_ID && depth < Structure_LastID; ++depth) {
            if (pass.itemCount[step] > 0 && step != Structure_Refinery) {
                yardTurn.skipRemainingStructureLogic = true;
                yardTurn.structureRule = "starport_opening_pending";
                break;
            }
            if (campaignAvailableToBuild(turn.pBuilder,step)) {
                if (findPlaceLocation(step).isValid()) {
                    yardTurn.skipRemainingStructureLogic = true;
                    yardTurn.structureRule = "starport_opening";
                    if (pass.money >= data[step][houseID].price) yardTurn.itemID = step;
                    // Keep the next worker supplier's savings intact.
                    // Cheap fallback zoning otherwise spends them
                    // every pass and postpones the opening fleet.
                    yardTurn.serviceSavingHold = yardTurn.itemID == NONE_ID && openingFleetIncomplete(pass)
                        && getHouse()->getNumItems(Structure_StarPort) == 0
                        && proactiveSavingHold(pass, data[step][houseID].price);
                }
                break;
            }
            Uint32 prerequisite = NONE_ID;
            for (int i = Structure_FirstID; i <= Structure_LastID; ++i) {
                if (data[step][houseID].prerequisiteStructuresSet[i]
                    && getHouse()->getNumItems(i) == 0) { prerequisite = i; break; }
            }
            step = prerequisite;
        }
    }
}

void QuantBot::chooseYardCityOpening(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Low-spice economy: skip additional refineries, pivot to R/I/C zones
    yardTurn.lowSpiceEconomy = (lastCalculatedSpice < 500);
    yardTurn.isCitySim = (currentGame && currentGame->isCitySimEnabled());

    // The first refinery supplies income and unlocks the tech tree.
    // Further processing capacity competes with demanded tax growth.
    const int openingRefineries = QuantBotBuildPolicy::openingSpiceRefineries(pass.spiceHarvesterTarget);
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && yardTurn.isCitySim
        && pass.spiceEconomyViable
        && pass.itemCount[Structure_Refinery] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_Refinery)
        && findPlaceLocation(Structure_Refinery).isValid()) {
        yardTurn.skipRemainingStructureLogic = true;
        yardTurn.structureRule = "city_saving_for_spice_opening";
        if (pass.money >= data[Structure_Refinery][houseID].price) {
            yardTurn.itemID = Structure_Refinery;
            yardTurn.structureRule = "city_spice_opening";
        }
        if (pass.emitStatsLog) traceDecision("city_spice_opening", AITelemetry::Record()
            .set("target",openingRefineries).set("refineries",pass.itemCount[Structure_Refinery])
            .set("spice_share",pass.spiceShare).set("credits",pass.money)
            .set("price",data[Structure_Refinery][houseID].price).set("funded",yardTurn.itemID != NONE_ID));
    }

    // One demanded residential plot hedges spice income immediately.
    // After that, buy the better return until the small opening
    // economy exists; don't force a commercial/industrial seed.
    const int openingZones = pass.itemCount[Structure_ZoneResidential]
        + pass.itemCount[Structure_ZoneCommercial] + pass.itemCount[Structure_ZoneIndustrial];
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && yardTurn.isCitySim
        && pass.itemCount[Structure_Refinery] > 0
        && pass.itemCount[Structure_HeavyFactory] == 0
        && ((pass.itemCount[Structure_ZoneResidential] == 0 && pass.ownResValve>0)
            || ((pass.brutalCityEconomy || openingZones<6) && pass.itemCount[Structure_Refinery]<openingRefineries))) {
        const Uint32 investment = chooseCityEconomy(pass, turn.pBuilder,true);
        if (investment != NONE_ID && (!pass.brutalCityEconomy || investment == Structure_Refinery
            || pass.itemCount[Structure_ZoneResidential] == 0)) {
            yardTurn.skipRemainingStructureLogic = true;
            yardTurn.structureRule = "city_saving_for_opening_investment";
            if (pass.money>=data[investment][houseID].price) {
                yardTurn.itemID=investment;
                yardTurn.structureRule="city_opening_investment";
            }
        }
    }

    // After the seed economy, save for the first vehicle factory and
    // its available prerequisite. Cheap zoning must not spend that
    // money afresh on every tick. Unavailable/unplaceable tech does
    // not reserve cash, and essential power above still takes priority.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && yardTurn.isCitySim
        && pass.itemCount[Structure_Refinery] > 0
        && pass.itemCount[Structure_HeavyFactory] == 0
        && (pass.itemCount[Structure_ZoneResidential] > 0 || pass.ownResValve<=0)) {
        for (Uint32 candidate : {Structure_HeavyFactory, Structure_Radar, Structure_LightFactory}) {
            if (pass.itemCount[candidate] > 0 || !campaignAvailableToBuild(turn.pBuilder,candidate)
                || !findPlaceLocation(candidate).isValid()) continue;
            yardTurn.skipRemainingStructureLogic = true;
            yardTurn.structureRule = "city_saving_for_vehicle_production";
            if (pass.money >= data[candidate][houseID].price) {
                yardTurn.itemID = candidate;
                yardTurn.structureRule = "city_vehicle_production_bootstrap";
            }
            if (pass.emitStatsLog) traceDecision("city_bootstrap_reserve", AITelemetry::Record()
                .set("item",candidate).set("price",data[candidate][houseID].price)
                .set("credits",pass.money).set("funded",yardTurn.itemID != NONE_ID));
            break;
        }
    }

    // Cash already harvested but trapped at unloading bays takes
    // priority over optional civic/defence/zoning investments.
    if(yardTurn.itemID==NONE_ID&&!yardTurn.skipRemainingStructureLogic&&pass.unloadingBacklog) {
        const Uint32 investment=yardTurn.isCitySim ? chooseCityEconomy(pass, turn.pBuilder,false) : Uint32(Structure_Refinery);
        if(investment==Structure_Refinery && campaignAvailableToBuild(turn.pBuilder,investment)
            && findPlaceLocation(investment).isValid()) {
            yardTurn.skipRemainingStructureLogic=true;
            yardTurn.itemID=pass.money>=data[investment][houseID].price?investment:NONE_ID;
            yardTurn.structureRule="city_refinery_capacity";
        }
    }

    // The demanded-civic rule used to sit here, below routine
    // protection saving, profitable-spice/opening tech and planned
    // reactor capacity. In a contested city those branches win every
    // pass, so it was never reached and the city stayed capped. It now
    // runs above them; see "A structural civic cap is required
    // infrastructure" earlier in this chain.

    // Proactively cover the whole city, not just the first two
    // factories. Planned turrets count, so parallel yards fill gaps.
    // Preserve the opening worker investment and interleave peaceful
    // coverage with growth; observed enemy aircraft make it urgent.
    // Observed aircraft are a present loss, not a growth trade-off:
    // do not make that coverage wait for the opening worker fleet,
    // a heavy factory or a spare zone's price. Peaceful coverage
    // keeps interleaving with growth on the established terms.
    // A wing counted from the enemy's item list is a plan; aircraft
    // that have reached our buildings are the present loss.
    const bool airThreatSeen = pass.airEngaged;
    // An uncovered yard, refinery or factory is the same kind of
    // present loss as an enemy wing: it does not wait for the
    // opening worker fleet, a heavy factory or a spare zone's price.
    // Deepening the overlap on assets already covered does.
    yardTurn.coreShortfall = yardTurn.isCitySim ? proactiveCoverageShortfall(pass) : 0;
    const bool coreDefenceDue = airThreatSeen
        || (yardTurn.coreShortfall > 0 && pass.uncoveredCoreAssets > 0);
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && yardTurn.isCitySim
        && !yardTurn.higherPriorityCapital
        && (coreDefenceDue
            || (!openingWorkersNeeded(pass) && pass.itemCount[Structure_HeavyFactory] > 0
                && nonServiceConstructionOrders >= 3))
        && campaignAvailableToBuild(turn.pBuilder,Structure_RocketTurret)
        && hasPowerBufferForTurret(pass)
        && pass.money >= data[Structure_RocketTurret][houseID].price
            + (coreDefenceDue ? 0 : data[Structure_ZoneResidential][houseID].price)) {
        int uncoveredWeight=0;
        const Coord site=findCityTurretPlaceLocation(Structure_RocketTurret,&uncoveredWeight);
        if(site.isValid() && uncoveredWeight>0) {
            yardTurn.itemID=Structure_RocketTurret;
            yardTurn.crimeServiceSite=site;
            yardTurn.structureRule="base_air_coverage";
            yardTurn.skipRemainingStructureLogic=true; // Do not replace this funded coverage slot with optional tech.
            traceDecision("base_air_coverage",AITelemetry::Record()
                .set("x",site.x).set("y",site.y).set("uncovered_building_weight",uncoveredWeight)
                .set("core_coverage_shortfall",yardTurn.coreShortfall).set("coverage_tier",pass.coverageTier)
                .set("coverage_turret_cap",RocketTurretPolicy::coverageTurretCap(pass.coverageDemand))
                .set("uncovered_core_assets",pass.uncoveredCoreAssets).set("air_engaged",pass.airEngaged)
                .set("enemy_aircraft",yardTurn.maxEnemyOrnithopters));
        }
    }

    // Unlock the first transport before extra ground production.
    // Save the actual factory price; high cash thresholds delayed this
    // until 3-5 heavy factories in 638. Never reserve an impossible site.
    const bool firstTransport = needsFirstTransport(pass);
    const bool firstHighTechSite = firstTransport && pass.itemCount[Structure_HighTechFactory] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_HighTechFactory)
        && findPlaceLocation(Structure_HighTechFactory).isValid();
    yardTurn.holdExtraHeavy = !pass.fundedCityProduction && (openingWorkersNeeded(pass) || (firstTransport
        && (firstHighTechSite || pass.carryallBuildAvailable || pass.itemCount[Structure_HighTechFactory]
            > getHouse()->getNumItems(Structure_HighTechFactory))));
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && firstHighTechSite) {
        yardTurn.skipRemainingStructureLogic = true;
        yardTurn.structureRule = "save_first_transport_factory";
        if (pass.money >= data[Structure_HighTechFactory][houseID].price) {
            yardTurn.itemID = Structure_HighTechFactory;
            yardTurn.structureRule = "first_transport_factory";
        }
    }
}

void QuantBot::chooseYardServicesAndRefineries(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Ongoing economic expansion uses the same comparison after
    // essential services and military infrastructure below.
    // Occupied zones are exactly the buildings that supply rebels to
    // an outbreak, so they are the right denominator. Count the
    // moderate band as well: that is the population still climbing
    // towards the dangerous band and the last chance to prevent it.
    yardTurn.developedZones = 0; yardTurn.dangerousZones = 0; yardTurn.moderateZones = 0;
    if (yardTurn.isCitySim) {
        const auto* sim = currentGame->getCitySimulation();
        if (sim && sim->isInitialized()) {
            for (const auto* structure : getStructureList()) {
                const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
                if (!zone || zone->getOwner() != getHouse() || zone->getHealth() <= 0) continue;
                const Coord p = zone->getLocation();
                if (!getMap().tileExists(p.x, p.y)
                    || DuneCity::getStructurePopulation(zone,getMap().getTile(p.x,p.y)->getCityZoneDensity()) == 0) continue;
                ++yardTurn.developedZones;
                const int zoneCrime = sim->getCrimeRateMap().worldGet(p.x, p.y);
                if (zoneCrime >= DuneCity::kCrimeDangerousThreshold) ++yardTurn.dangerousZones;
                else if (zoneCrime >= 128) ++yardTurn.moderateZones;
            }
        }
    }
    yardTurn.serviceInterval = QuantBotBuildPolicy::crimeServiceOrderInterval(
        yardTurn.developedZones, yardTurn.dangerousZones, yardTurn.moderateZones);
    yardTurn.serviceEvaluated = false;
    // The scorer used to refuse to even look while the till held
    // less than a station, so a demanded service disappeared from
    // the pass and the yard spent the same cash on a cheap plot —
    // the city then never reached the price and crime ran to an
    // outbreak. Evaluate at the service price, then keep the budget
    // when the forecast can actually reach it inside the horizon.
    yardTurn.servicePriceFloor = std::max(data[Structure_PoliceStation][houseID].price,
        data[Structure_RocketTurret][houseID].price);
    // A turret that repays its entire cost through land-value tax
    // alone earns an early investment slot when it also reduces
    // existing crime and improves residential/commercial value.
    // Keep cash for harvesters/refineries and a combat unit; cap the
    // frequency so it cannot monopolise the construction yards.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && yardTurn.isCitySim
        && pass.itemCount[Structure_HeavyFactory] > 0
        && yardTurn.serviceInterval != 2 // Widespread dangerous crime still gets the strongest service comparison first.
        && QuantBotBuildPolicy::crimeServiceOrderDue(3, nonServiceConstructionOrders)
        && selectCityServiceInvestment(turn.pBuilder,
            std::max(0,pass.money-pass.cityWorkingReserve), false,
            yardTurn.itemID, yardTurn.crimeServiceSite, true)) {
        yardTurn.structureRule = "turret_land_value_investment";
    }
    if (QuantBotBuildPolicy::crimeServiceOrderDue(yardTurn.serviceInterval, nonServiceConstructionOrders))
        selectCrimeService(pass, turn, yardTurn, "city_crime_reserved_order");
    // 2. Refinery (if 0) — non-city-sim path; city sim handles refineries above.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && !yardTurn.isCitySim
        && pass.itemCount[Structure_Refinery] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_Refinery)) {
        yardTurn.itemID = Structure_Refinery; yardTurn.structureRule = "refinery_economy";
    }

    // Light-first opening (Rush tempo, or the test rush style; docs/ai-play-styles.md):
    // light production right after the first refinery, ahead of repairs, radar and the
    // heavy factory.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && !yardTurn.isCitySim
        && lightFirstOpening()
        && pass.itemCount[Structure_LightFactory] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_LightFactory)
        && pass.money > 500) {
        yardTurn.itemID = Structure_LightFactory; yardTurn.structureRule = "style_light_first";
    }
    // Establish repairs before more factories/tech consume the opening
    // grant. Count queued yards to avoid duplicate orders from parallel CYs.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && gameMode == GameMode::Custom && canAddRepairYard(pass.itemCount[Structure_RepairYard])
        && !openingWorkersNeeded(pass)
        && pass.itemCount[Structure_RepairYard] < pass.repairBaseline
        && getHouse()->getNumItems(Structure_Refinery) > 0
        && pass.money >= data[Structure_RepairYard][houseID].price + 1000
        && campaignAvailableToBuild(turn.pBuilder,Structure_RepairYard)
        && findPlaceLocation(Structure_RepairYard).isValid()) {
        yardTurn.itemID = Structure_RepairYard;
        yardTurn.structureRule = "early_repair_capacity";
    }


    // 3. Refinery (ratio: 1 refinery per 3 harvesters) — non-city-sim only.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && !yardTurn.isCitySim
        && !yardTurn.lowSpiceEconomy
        && pass.itemCount[Structure_Refinery] < (pass.vanillaEconomy
            ? QuantBotBuildPolicy::desiredSpiceRefineries(pass.spiceHarvesterTarget, pass.itemCount[Unit_Harvester])
                + style().extraRefineries
            : pass.itemCount[Unit_Harvester] / 3)
        && campaignAvailableToBuild(turn.pBuilder,Structure_Refinery)
        && !(gameMode == GameMode::Campaign && (difficulty!=Difficulty::Medium || initialItemCount[Structure_RepairYard]>0) && pass.itemCount[Structure_Refinery] >= 2 && pass.itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
        yardTurn.itemID = Structure_Refinery; yardTurn.structureRule = "refinery_economy";
    }
    // 4. Refinery (< 4, money < 2000) — non-city-sim only.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && !yardTurn.isCitySim
        && !yardTurn.lowSpiceEconomy
        && gameMode != GameMode::Campaign
        && pass.itemCount[Structure_Refinery] < 4
        && campaignAvailableToBuild(turn.pBuilder,Structure_Refinery)
        && pass.money < 2000) {
        yardTurn.itemID = Structure_Refinery; yardTurn.structureRule = "refinery_economy";
    }
}

void QuantBot::selectCrimeService(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn, const char* rule) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (yardTurn.itemID != NONE_ID || yardTurn.skipRemainingStructureLogic || !yardTurn.isCitySim || yardTurn.serviceEvaluated) return;
    yardTurn.serviceEvaluated = true;
    const bool selected = selectCityServiceInvestment(turn.pBuilder,
        std::max(pass.money, yardTurn.servicePriceFloor),
        yardTurn.serviceInterval > 0, yardTurn.itemID, yardTurn.crimeServiceSite);
    const int price = selected ? data[yardTurn.itemID][houseID].price : 0;
    const bool shortOfPrice = selected && pass.money < price;
    if (shortOfPrice) {
        yardTurn.serviceSavingHold = yardTurn.dangerousZones>0 && pass.cashFlow.projectedCash >= price;
        yardTurn.itemID = NONE_ID;
        yardTurn.crimeServiceSite = Coord::Invalid();
        yardTurn.structureRule = yardTurn.serviceSavingHold ? "save_city_service" : yardTurn.structureRule;
        yardTurn.skipRemainingStructureLogic = yardTurn.serviceSavingHold;
    } else if (selected) yardTurn.structureRule = rule;
    traceDecision("crime_service_reservation", AITelemetry::Record()
        .set("rule",rule).set("interval",int(yardTurn.serviceInterval))
        .set("developed_zones",yardTurn.developedZones).set("dangerous_zones",yardTurn.dangerousZones)
        .set("moderate_zones",yardTurn.moderateZones)
        .set("non_service_orders",int(nonServiceConstructionOrders))
        .set("selected",selected).set("item",int(yardTurn.itemID)).set("price",price)
        .set("spendable",pass.money).set("projected_cash",pass.cashFlow.projectedCash)
        .set("saving",yardTurn.serviceSavingHold));
}

void QuantBot::chooseYardProductionLanes(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // City income gate: in city sim mode, defer military infrastructure
    // (StarPort/Radar/LightFactory) until the city has at least the
    // seed economy in place. Uses zone *count*, not population —
    // zones stay at density 0 (and contribute 0 pop) until growth
    // conditions kick in, so a pop-based gate locks military out
    // indefinitely if the AI hasn't laid roads/supply yet.
    constexpr int kCityIncomeReadyZones = 3;
    const int kCityZoneCount = pass.itemCount[Structure_ZoneResidential]
        + pass.itemCount[Structure_ZoneCommercial]
        + pass.itemCount[Structure_ZoneIndustrial];
    yardTurn.cityIncomeReady = !yardTurn.isCitySim || kCityZoneCount >= kCityIncomeReadyZones || pass.money > 3000;

    // One light line is enough until the first heavy line exists.
    // Do not mistake an opening's temporary all-light mix for a need
    // to build several light factories before unlocking tanks.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.itemCount[Structure_LightFactory] > 0
        && pass.itemCount[Structure_HeavyFactory] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_HeavyFactory)
        && findPlaceLocation(Structure_HeavyFactory).isValid()) {
        yardTurn.skipRemainingStructureLogic = true;
        yardTurn.structureRule = "save_first_heavy";
        if (pass.money >= data[Structure_HeavyFactory][houseID].price) {
            yardTurn.itemID = Structure_HeavyFactory;
            yardTurn.structureRule = "first_heavy_before_expansion";
        }
    }
    // The high-tech factory and IX unlock the units the extra lanes
    // would build, so a wealthy base finishes them before repeating
    // optional factory expansion. Only tech this yard (or another)
    // can actually order counts, so a locked tree never stalls
    // production, and the first factory is never delayed.
    const bool techProgressionPending = pass.customStrategicPlanning
        && pass.itemCount[Structure_HeavyFactory] > 0
        && ((pass.itemCount[Structure_HighTechFactory] == 0
                && campaignAvailableToBuild(turn.pBuilder,Structure_HighTechFactory))
            || (pass.itemCount[Structure_IX] == 0 && pass.anyConstructionYardCanBuildIX));
    // Wealthy vanilla openings need parallel MCV/troop production
    // before optional infrastructure. First/expanding refineries above
    // keep their priority; queued factories count towards this target.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.vanillaEconomy && gameMode == GameMode::Custom && !yardTurn.holdExtraHeavy
        && !techProgressionPending
        && pass.itemCount[Structure_Refinery] > 0
        && pass.militaryValue < pass.militaryBudget && !getHouse()->isGroundUnitLimitReached()
        && campaignAvailableToBuild(turn.pBuilder,Structure_HeavyFactory)) {
        const int target = DuneCity::vanillaFactoryTarget(
            QuantBotBuildPolicy::desiredHeavyFactories(false, 0, pass.money,
                getHouse()->getNumItems(Structure_HeavyFactory), pass.activeHeavyFactoryCount, recentFactoryLossCount()),
            getHouse()->getNumItems(Unit_Harvester), pass.money);
        if (DuneCity::prioritizeVanillaFactory(pass.money, pass.itemCount[Structure_HeavyFactory],
                pass.itemCount[Structure_ConstructionYard], target)
            && findPlaceLocation(Structure_HeavyFactory).isValid()) {
            yardTurn.itemID = Structure_HeavyFactory; yardTurn.structureRule = "cash_factory_expansion";
        }
    }
    // Unlock one defensive yard before repeat zoning can occupy every
    // construction slot. Other yards keep growing the city in parallel.
    // The upgrade is the prerequisite for any emplacement at all, so
    // demanded core coverage buys it once the first refinery pays the
    // bills rather than after the whole opening worker fleet.
    const int rocketUpgrade=data[Structure_RocketTurret][houseID].upgradeLevel;
    if (yardTurn.itemID==NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.citySimEnabled
        && !yardTurn.higherPriorityCapital
        && (yardTurn.coreShortfall>0
            || (!openingWorkersNeeded(pass) && pass.itemCount[Structure_HeavyFactory]>0
                && pass.itemCount[Structure_RocketTurret]<2))
        && data[Structure_RocketTurret][houseID].enabled
        && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel
        && turn.pBuilder->getCurrentUpgradeLevel()<rocketUpgrade
        && turn.pBuilder->getMaxUpgradeLevel()>=rocketUpgrade
        && pass.money>=turn.pBuilder->getUpgradeCost()+data[Structure_ZoneResidential][houseID].price
        && std::none_of(pass.capitalBuilders.begin(),pass.capitalBuilders.end(),[&](const auto* other) {
            return other!=turn.pBuilder && other->getItemID()==Structure_ConstructionYard
                && (other->isUpgrading() || other->getCurrentUpgradeLevel()>=rocketUpgrade);
        })) {
        if (turn.pBuilder->getHealth()<turn.pBuilder->getMaxHealth()) doRepair(turn.pBuilder);
        else if (upgradeWithLogging(pass, turn, __LINE__)) {
            yardTurn.skipRemainingStructureLogic=true;
            traceDecision("city_yard_upgrade",AITelemetry::Record()
                .set("builder",turn.pBuilder->getObjectID()).set("accepted",true)
                .set("reason","unlock_base_defence"));
        }
    }
    // Demanded core coverage also saves for its own prerequisite:
    // the emplacement, or the yard upgrade that unlocks it. Bounded
    // by the forecast, so a city that cannot reach the price keeps
    // growing instead of idling.
    if (yardTurn.isCitySim && yardTurn.coreShortfall>0 && !yardTurn.serviceSavingHold) {
        const int upgradeStep=turn.pBuilder->getCurrentUpgradeLevel()<rocketUpgrade
            && turn.pBuilder->getMaxUpgradeLevel()>=rocketUpgrade ? turn.pBuilder->getUpgradeCost() : 0;
        const int wanted=upgradeStep>0 ? upgradeStep : data[Structure_RocketTurret][houseID].price;
        if (pass.money<wanted && pass.cashFlow.projectedCash>=wanted
            && proactiveSavingHold(pass, wanted)
            && data[Structure_RocketTurret][houseID].enabled
            && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel)
            yardTurn.serviceSavingHold=true;
    }
    if (yardTurn.itemID==NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.sharedSpending && !pass.fundedCityProduction) {
        const CapitalCandidate* choice=nullptr;
        for (const auto& candidate:pass.capitalCandidates)
            if (candidate.builder==turn.pBuilder->getObjectID() && candidate.score>0
                && (!choice || candidate.score>choice->score)) choice=&candidate;
        if (choice) {
            Uint32 candidate=choice->item;
            // Re-evaluate after earlier yards have committed lots/bays.
            if (pass.citySimEnabled && std::string(choice->kind)=="economy") candidate=chooseCityEconomy(pass, turn.pBuilder,false);
            if (candidate!=NONE_ID && (!pass.orderedThisTick.count(candidate)
                || DuneCity::isCityZoneStructure(candidate))) {
                yardTurn.itemID=pass.money>=data[candidate][houseID].price ? candidate : NONE_ID;
                if (yardTurn.itemID!=NONE_ID && choice->site.isValid()) yardTurn.crimeServiceSite=choice->site;
                // A civic allocation that the till cannot cover yet
                // keeps its budget; the fallback below used to spend
                // it on a plot every pass, so the price never came.
                if (yardTurn.itemID==NONE_ID && std::string(choice->kind)=="civic"
                    && pass.cashFlow.projectedCash>=data[candidate][houseID].price
                    && (choice->reason==std::string("crime_prevention")
                        || proactiveSavingHold(pass, data[candidate][houseID].price)))
                    yardTurn.serviceSavingHold=true;
                yardTurn.structureRule=yardTurn.itemID==NONE_ID ? "save_shared_capital" : choice->reason==std::string("crime_prevention")
                    ? "city_crime_prevention" : (choice->reason==std::string("uncovered_base")
                        || choice->reason==std::string("core_coverage")
                        || choice->reason==std::string("air_coverage"))
                    ? "base_air_coverage" : "shared_capital_investment";
                yardTurn.skipRemainingStructureLogic=true;
            }
        }
    }
    if (yardTurn.itemID==NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.fundedCityProduction
        && pass.itemCount[Structure_RepairYard]<pass.repairTarget
        && canAddRepairYard(pass.itemCount[Structure_RepairYard])
        && campaignAvailableToBuild(turn.pBuilder,Structure_RepairYard)
        && pass.money>=buildingCapitalCost(pass, Structure_RepairYard)+pass.cashBuffer
        && findPlaceLocation(Structure_RepairYard).isValid()) {
        yardTurn.itemID=Structure_RepairYard;yardTurn.structureRule="funded_repair_capacity";
    }
    // Fund combat-air capacity before optional ground-factory expansion.
    // Pending factories prevent duplicate lanes across parallel yards.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && yardTurn.airBacklog
        && campaignAvailableToBuild(turn.pBuilder,Structure_HighTechFactory)
        && findPlaceLocation(Structure_HighTechFactory).isValid()) {
        yardTurn.itemID = Structure_HighTechFactory; yardTurn.structureRule = "air_unit_backlog";
    }
    // Expand saturated light production before optional heavy capacity.
    // Pending factories count, so parallel yards add one lane at a time.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && yardTurn.lightBacklog
        && (pass.itemCount[Structure_HeavyFactory] > 0
            || !data[Structure_HeavyFactory][houseID].enabled
            || data[Structure_HeavyFactory][houseID].techLevel > currentGame->techLevel)
        && campaignAvailableToBuild(turn.pBuilder,Structure_LightFactory)
        && findPlaceLocation(Structure_LightFactory).isValid()) {
        yardTurn.itemID = Structure_LightFactory; yardTurn.structureRule = "light_unit_backlog";
    }
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && !yardTurn.holdExtraHeavy && yardTurn.heavyBacklog
        && pass.itemCount[Structure_HeavyFactory] < 24
        && campaignAvailableToBuild(turn.pBuilder,Structure_HeavyFactory)
        && findPlaceLocation(Structure_HeavyFactory).isValid()) {
        yardTurn.itemID = Structure_HeavyFactory; yardTurn.structureRule = "heavy_unit_backlog";
    }
    // 5. Starport only if this map offers enabled imports (sold-out entries restock).
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && yardTurn.cityIncomeReady
        && pass.itemCount[Structure_StarPort] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_StarPort)
        && findPlaceLocation(Structure_StarPort).isValid()
        && (gameMode != GameMode::Campaign || pass.money > 1000)
        && pass.starportMarketAvailable) {
        yardTurn.itemID = Structure_StarPort; yardTurn.structureRule = "starport_supply";
    }
    // 6. Radar
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && yardTurn.cityIncomeReady
        && pass.itemCount[Structure_Radar] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_Radar)
        && pass.money > 500) {
        yardTurn.itemID = Structure_Radar; yardTurn.structureRule = "radar_prerequisite";
    }
    // 7. Light Factory
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && yardTurn.cityIncomeReady
        && pass.itemCount[Structure_LightFactory] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_LightFactory)
        && pass.money > 500) {
        yardTurn.itemID = Structure_LightFactory; yardTurn.structureRule = "light_production";
    }
}

void QuantBot::chooseYardMilitaryInfrastructure(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Custom vanilla invests in vehicle production; retain infantry
    // infrastructure for campaign and other-mod build policies.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && !yardTurn.isCitySim
        && (!(pass.vanillaEconomy && gameMode == GameMode::Custom) || infantryStrategy())
        && pass.itemCount[Structure_Barracks] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_Barracks)
        && pass.money > 400) {
        yardTurn.itemID = Structure_Barracks; yardTurn.structureRule = "infantry_production";
    }
    // A city builds no infantry barracks for its own sake, but the producer
    // the close-range part of its anti-air answer needs has to exist before
    // anything can be made. An observed hostile wing - and nothing else -
    // unblocks it, behind every economy and progression rule above, behind
    // the opening worker fleet, and only out of cash the city's working
    // reserve and its next zone do not need. A WOR with no enabled, unlocked
    // troop content, or with no room left in the infantry quota, is not worth
    // founding at all.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && (!(pass.vanillaEconomy && gameMode == GameMode::Custom) || infantryStrategy())
        && (!yardTurn.isCitySim || (pass.cityAntiAirInfantryDemand && cityTroopContentExists(pass)
            && !openingWorkersNeeded(pass)
            && int64_t(pass.money) >= data[Structure_WOR][houseID].price + pass.cityWorCashReserve))
        && pass.itemCount[Structure_WOR] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_WOR)
        && pass.money > 600) {
        yardTurn.itemID = Structure_WOR;
        yardTurn.structureRule = yardTurn.isCitySim ? "city_anti_air_infantry" : "infantry_production";
    }
    // 8. Repair Yard (only if starport or heavy factory exists)
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && canAddRepairYard(pass.itemCount[Structure_RepairYard]) && pass.itemCount[Structure_RepairYard] == 0
        && (pass.itemCount[Structure_StarPort] > 0 || pass.itemCount[Structure_HeavyFactory] > 0)
        && campaignAvailableToBuild(turn.pBuilder,Structure_RepairYard)
        && (!yardTurn.isCitySim || (!openingWorkersNeeded(pass)
            && pass.money >= data[Structure_RepairYard][houseID].price
                + data[Structure_ZoneResidential][houseID].price))) {
        yardTurn.itemID = Structure_RepairYard; yardTurn.structureRule = "repair_capacity";
        logDebug("Build Repair Yard... money: %d", pass.money);
    }
    // 8a. Upgrade CY to level 2 for rocket turrets
    //     City sim: upgrade early (no repair yard needed) so turrets protect the colony
    //     Non-city: requires repair yard + starport/heavy factory
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && turn.pBuilder->getCurrentUpgradeLevel() < 2
        && (!yardTurn.isCitySim || (!openingWorkersNeeded(pass)
            && pass.money >= turn.pBuilder->getUpgradeCost() + data[Structure_ZoneResidential][houseID].price))
        && !turn.pBuilder->isUpgrading()
        && ((currentGame && currentGame->isCitySimEnabled() && pass.money > 500)
            || (pass.itemCount[Structure_RepairYard] > 0
                && (pass.itemCount[Structure_StarPort] > 0 || pass.itemCount[Structure_HeavyFactory] > 0)
                && pass.itemCount[Structure_RocketTurret] < style().rocketTurrets
        && (!pass.vanillaEconomy || pass.money > pass.economyReserve + data[Structure_RocketTurret][houseID].price)))) {
        if (turn.pBuilder->getHealth() < turn.pBuilder->getMaxHealth() && !turn.pBuilder->isRepairing()) {
            doRepair(turn.pBuilder);
            logDebug("TURRET-PREP: Repairing CY before upgrade (level %d)", turn.pBuilder->getCurrentUpgradeLevel());
        } else if (turn.pBuilder->getHealth() >= turn.pBuilder->getMaxHealth()) {
            const int upgradeCost = turn.pBuilder->getUpgradeCost();
            const bool accepted = upgradeWithLogging(pass, turn, __LINE__);
            if (accepted) {  yardTurn.skipRemainingStructureLogic = true; }
            traceDecision("city_yard_upgrade", AITelemetry::Record().set("builder",turn.pBuilder->getObjectID())
                .set("accepted",accepted).set("cost",upgradeCost).set("spendable",pass.money));
            logDebug("TURRET-PREP: Upgrading CY to level %d for rocket turrets", turn.pBuilder->getCurrentUpgradeLevel() + 1);
        }
    }
    // Windtrap damage does not reduce output; repair for survival
    // uses the ordinary building-repair policy, not power recovery.
    // 8b-pre. Build power for turret buffer — prefer nuclear, fall back to windtrap
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && getGameInitSettings().getGameOptions().rocketTurretsNeedPower
        && pass.itemCount[Structure_RepairYard] > 0
        && (pass.itemCount[Structure_StarPort] > 0 || pass.itemCount[Structure_HeavyFactory] > 0)
        && turn.pBuilder->getCurrentUpgradeLevel() >= 2
        && campaignAvailableToBuild(turn.pBuilder,Structure_RocketTurret)
        && pass.money >= data[Structure_RocketTurret][houseID].price
        && !hasPowerBufferForTurret(pass)) {
        int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
        if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant))
            && findPlaceLocation(Structure_NuclearPlant).isValid()) {
            yardTurn.itemID = Structure_NuclearPlant; yardTurn.structureRule = "power";
            logDebug("TURRET-POWER: Nuclear Plant for turret buffer (excess: %d, need: %d)", powerExcess, pass.rocketPowerBuffer);
        } else if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap))
            && findPlaceLocation(Structure_WindTrap).isValid()) {
            yardTurn.itemID = Structure_WindTrap; yardTurn.structureRule = "power";
            logDebug("TURRET-POWER: Windtrap for turret buffer (excess: %d, need: %d)", powerExcess, pass.rocketPowerBuffer);
        }
    }
    // 8b. Two baseline rocket turrets after repair yard (requires CY level 2)
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.itemCount[Structure_RepairYard] > 0
        && pass.itemCount[Structure_RocketTurret] < style().rocketTurrets
        && (pass.itemCount[Structure_StarPort] > 0 || pass.itemCount[Structure_HeavyFactory] > 0)
        && hasPowerBufferForTurret(pass)
        && turn.pBuilder->getCurrentUpgradeLevel() >= 2
        && campaignAvailableToBuild(turn.pBuilder,Structure_RocketTurret)
        && pass.money >= data[Structure_RocketTurret][houseID].price
        && findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
        yardTurn.itemID = Structure_RocketTurret; yardTurn.structureRule = "rocket_defense";
        logDebug("INSURANCE: Building baseline rocket turret (%d/2) after repair yard", pass.itemCount[Structure_RocketTurret] + 1);
    }
    // 8c. Counter enemy ornithopters (requires CY level 2).
    //     The enemy-facing battery is deliberately NOT here: this
    //     slot sits ahead of the refinery, factory, tech and city
    //     growth rules below, and a demand-scaled battery placed
    //     here took those slots away from the economy that pays for
    //     it. It has its own rule after the whole economy chain.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.itemCount[Structure_RepairYard] > 0
        && (pass.itemCount[Structure_StarPort] > 0 || pass.itemCount[Structure_HeavyFactory] > 0)
        && hasPowerBufferForTurret(pass)
        && turn.pBuilder->getCurrentUpgradeLevel() >= 2
        && campaignAvailableToBuild(turn.pBuilder,Structure_RocketTurret)
        && pass.money >= data[Structure_RocketTurret][houseID].price
        && yardTurn.counterAirTurrets > 0
                        && pass.routineRocketsAllowed
        && pass.itemCount[Structure_RocketTurret] < yardTurn.activeRocketTurretGoal
        && findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
        yardTurn.itemID = Structure_RocketTurret; yardTurn.structureRule = "rocket_defense";
        logDebug("COUNTER-ORNITHOPTER: Building rocket turret to counter enemy ornithopters");
    }
    // 8d. Threat turrets on the base perimeter (U, on by default, docs/formations.md
    //     "Round 5"): while the remembered enemy army outnumbers ours, one rocket
    //     turret per pass on the enemy-facing perimeter, one per 1,000 credits of the
    //     enemy's lead and at most eight there, never below the economy reserve plus a
    //     unit reserve; power first when turrets need it. Nothing once we are not outnumbered.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && forceVariant(kForceThreatTurrets)
        && turn.pBuilder->getCurrentUpgradeLevel() >= 2
        && campaignAvailableToBuild(turn.pBuilder,Structure_RocketTurret)
        && !pass.orderedThisTick.count(Structure_RocketTurret)
        && pass.money >= pass.economyReserve + data[Structure_RocketTurret][houseID].price
            + ForceConcentrationPolicy::kPerimeterUnitReserve
        && perimeterTurrets() < perimeterTurretGoal()) {
        if (!hasPowerBufferForTurret(pass)) {
            if (!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap)
                && findPlaceLocation(Structure_WindTrap).isValid()) {
                yardTurn.itemID = Structure_WindTrap; yardTurn.structureRule = "perimeter_battery_power";
            }
        } else if (findPerimeterTurretLocation().isValid()) {
            yardTurn.itemID = Structure_RocketTurret; yardTurn.structureRule = "perimeter_battery";
            ++forceStats.perimeterTurretOrders;
        }
    }
    // 9. Heavy Factory
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && yardTurn.cityIncomeReady
        && pass.itemCount[Structure_HeavyFactory] == 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_HeavyFactory)
        && pass.money > 500) {
        yardTurn.itemID = Structure_HeavyFactory; yardTurn.structureRule = "heavy_production";
        logDebug("Build first Heavy Factory... money: %d", pass.money);
    }
    // 10. High Tech Factory (first one - after heavy factory)
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && yardTurn.cityIncomeReady
        && pass.itemCount[Structure_HighTechFactory] == 0
        && !openingWorkersNeeded(pass)
        && pass.itemCount[Structure_HeavyFactory] > 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_HighTechFactory)
        && pass.money > 1000) {
        yardTurn.itemID = Structure_HighTechFactory; yardTurn.structureRule = "air_production";
        logDebug("Build first High Tech Factory... money: %d", pass.money);
    }
}

void QuantBot::chooseYardTechAndExpansion(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // 11. House IX (after essential production buildings)
    //     Neither IX rule may choose an IX that has no site: the overdue rule replaces the
    //     choice of every rule above it, and an unplaceable IX was then rejected each pass
    //     (no_site) and nothing at all was built. The Palace below likewise.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.itemCount[Structure_IX] == 0
        && pass.itemCount[Structure_HeavyFactory] > 0
        && pass.itemCount[Structure_HighTechFactory] > 0
        && pass.itemCount[Structure_RepairYard] > 0
        && campaignAvailableToBuild(turn.pBuilder,Structure_IX)
        && pass.money > 1000
        && (!botConfig().strategicSiteGuards || findPlaceLocation(Structure_IX).isValid())) {
        yardTurn.itemID = Structure_IX; yardTurn.structureRule = "advanced_tech";
        logDebug("Build IX... money: %d", pass.money);
    }
    if (pass.ixOverdue && !yardTurn.skipRemainingStructureLogic
        && pass.itemCount[Structure_IX] == 0
        && pass.stablePower
        && pass.money > 1000
        && campaignAvailableToBuild(turn.pBuilder,Structure_IX)
        && yardTurn.itemID != Structure_IX
        && yardTurn.itemID != Structure_WindTrap
        && yardTurn.itemID != Structure_NuclearPlant
        && (!botConfig().strategicSiteGuards || findPlaceLocation(Structure_IX).isValid())) {
        yardTurn.itemID = Structure_IX; yardTurn.structureRule = "advanced_tech";
    }
    // Missing prerequisites never start the IX overdue timer.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && pass.itemCount[Structure_IX] == 0
        && pass.itemCount[Structure_HeavyFactory] > 0
        && pass.itemCount[Structure_HighTechFactory] > 0
        && pass.itemCount[Structure_RepairYard] > 0
        && pass.money > 1000
        && starportUnlocks(pass, turn, Structure_IX)) {
        yardTurn.itemID = Structure_StarPort; yardTurn.structureRule = "starport_prerequisite";
        logDebug("Build Starport as House IX prerequisite... money: %d", pass.money);
    }
    // 12. Additional Heavy Factories (expansion).
    //     Income supports steady expansion; large cash surpluses fund
    //     extra tank capacity, bounded while military demand remains.
    //     Non-city uses the existing money/4000 target, also bounded.
    //     City mode uses actual build availability; classic prerequisites remain.
    //     Requirements outside city mode are progressive based on tech level:
    //     Tech 4: No prerequisites (just money and need)
    //     Tech 5-6: Require Repair Yard
    //     Tech 7+: Require Repair Yard + IX
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
                    && !yardTurn.holdExtraHeavy && pass.money > std::max(2000, pass.economyReserve + data[Structure_HeavyFactory][houseID].price) && campaignAvailableToBuild(turn.pBuilder,Structure_HeavyFactory)) {

        int creditsPerSec = 0;
        if (yardTurn.isCitySim) {
            auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
            int tax = citySim ? citySim->getCityTax() : 7;
            int32_t annual = DuneCity::computeAnnualTaxRevenue(pass.ownTaxBaseEighths, tax, pass.ownAvgLandValue);
            creditsPerSec = annual / 60;
        }
        int desiredHFs = QuantBotBuildPolicy::desiredHeavyFactories(yardTurn.isCitySim, creditsPerSec, pass.money, getHouse()->getNumItems(Structure_HeavyFactory), pass.activeHeavyFactoryCount, recentFactoryLossCount());

        if (pass.vanillaEconomy) desiredHFs = DuneCity::vanillaFactoryTarget(desiredHFs, getHouse()->getNumItems(Unit_Harvester), pass.money);
        const bool needMore = pass.itemCount[Structure_HeavyFactory] < desiredHFs
            && pass.militaryValue < pass.militaryBudget && !getHouse()->isGroundUnitLimitReached();

        if (needMore) {
            int techLevel = currentGame ? currentGame->techLevel : 8;
            bool prerequisitesMet = false;

            if (yardTurn.isCitySim || pass.vanillaEconomy || techLevel <= 4) {
                // City and vanilla production use the actual tech tree, not an extra IX policy gate.
                prerequisitesMet = true;
            }
            else if (techLevel <= 6) {
                // Tech 5-6: Require Repair Yard
                prerequisitesMet = (pass.itemCount[Structure_RepairYard] >= 1);
            }
            else {
                // Tech 7+: Require both Repair Yard and IX
                prerequisitesMet = (pass.itemCount[Structure_RepairYard] >= 1 && pass.itemCount[Structure_IX] >= 1);
            }

            if (prerequisitesMet) {
                yardTurn.itemID = Structure_HeavyFactory; yardTurn.structureRule = "heavy_production";
                logDebug("PRIORITY Heavy Factory - active: %d  total: %d  money: %d  desired: %d  tech: %d",
                    pass.activeHeavyFactoryCount, getHouse()->getNumItems(Structure_HeavyFactory), pass.money, desiredHFs, techLevel);
            }
        }
    }
    // 13. Vanilla refinery ratio. City mode compares capacity and tax returns.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && !yardTurn.isCitySim
            && !yardTurn.lowSpiceEconomy
            && ((pass.itemCount[Structure_Refinery] * 3.5_fix < pass.itemCount[Unit_Harvester])
        || (currentGame && currentGame->techLevel < 4))
            && campaignAvailableToBuild(turn.pBuilder,Structure_Refinery)
            && !(gameMode == GameMode::Campaign && (difficulty!=Difficulty::Medium || initialItemCount[Structure_RepairYard]>0) && pass.itemCount[Structure_Refinery] >= 2 && pass.itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
        yardTurn.itemID = Structure_Refinery; yardTurn.structureRule = "refinery_economy";
    }
    // 14. Expand repair only when existing capacity is busy and production supports it.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
                && canAddRepairYard(pass.itemCount[Structure_RepairYard]) && campaignAvailableToBuild(turn.pBuilder,Structure_RepairYard) && pass.money > std::max(2000, pass.economyReserve + data[Structure_RepairYard][houseID].price)
                && pass.itemCount[Structure_RepairYard]<pass.repairTarget) {
        yardTurn.itemID = Structure_RepairYard; yardTurn.structureRule = "repair_capacity";
        logDebug("Build Repair Yard: have=%d busy=%d cap=%d military=%d", pass.itemCount[Structure_RepairYard], pass.activeRepairYardCount,
            pass.repairTarget, pass.militaryValue);
    }
}

// Buy a prerequisite port at the tech goal's priority, even
// without imports. Count queued ports across all yards.
bool QuantBot::starportUnlocks(BuildPass& pass, BuilderTurn& turn, Uint32 goal) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    return pass.itemCount[Structure_StarPort] == 0
        && !pass.orderedThisTick.count(Structure_StarPort)
        && campaignPermitsStructure(goal)
        && prerequisiteBlocksBuild(turn.pBuilder,goal,Structure_StarPort)
        && campaignAvailableToBuild(turn.pBuilder,Structure_StarPort)
        && pass.money >= data[Structure_StarPort][houseID].price
        && findPlaceLocation(Structure_StarPort).isValid();
}

void QuantBot::chooseYardCivicAndDefence(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    selectCrimeService(pass, turn, yardTurn, "city_service_investment");
    // Civic turrets use the shared investment comparison above.
    // 17b. Palace (after military infrastructure)
    //       City sim: 1 palace per 30000 population
    {
        const bool palaceAllowed = pass.itemCount[Structure_Palace] < pass.palaceTarget
            && !pass.orderedThisTick.count(Structure_Palace);
        if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
                            && pass.money > 5000
                            && campaignAvailableToBuild(turn.pBuilder,Structure_Palace)
                            && palaceAllowed
                            && pass.itemCount[Structure_HeavyFactory] > 0
                            && pass.itemCount[Structure_LightFactory] > 0
                            && (!botConfig().strategicSiteGuards || findPlaceLocation(Structure_Palace).isValid())) {
            yardTurn.itemID = Structure_Palace; yardTurn.structureRule = "palace_strategy";
        }
        if (pass.palaceOverdue && !yardTurn.skipRemainingStructureLogic
            && pass.stablePower
            && pass.money > 5000
            && campaignAvailableToBuild(turn.pBuilder,Structure_Palace)
            && palaceAllowed
            && pass.itemCount[Structure_HeavyFactory] > 0
            && pass.itemCount[Structure_LightFactory] > 0
            && yardTurn.itemID != Structure_Palace
            && yardTurn.itemID != Structure_IX
            && yardTurn.itemID != Structure_WindTrap
            && yardTurn.itemID != Structure_NuclearPlant
            && (!botConfig().strategicSiteGuards || findPlaceLocation(Structure_Palace).isValid())) {
            yardTurn.itemID = Structure_Palace; yardTurn.structureRule = "palace_strategy";
        }
        // Same demand as the palace rules above, for a map whose Palace
        // lists a Starport this base does not own.
        if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
            && pass.money > 5000
            && palaceAllowed
            && pass.itemCount[Structure_HeavyFactory] > 0
            && pass.itemCount[Structure_LightFactory] > 0
            && starportUnlocks(pass, turn, Structure_Palace)) {
            yardTurn.itemID = Structure_StarPort; yardTurn.structureRule = "starport_prerequisite";
            logDebug("Build Starport as Palace prerequisite... money: %d", pass.money);
        }
    }
    // Round out vanilla bases with regular turrets and short wall lines.
    // Fixed count targets keep this deterministic and prevent defence spam.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && !yardTurn.isCitySim
        && pass.itemCount[Structure_HeavyFactory] > 0 && pass.money > std::max(1000, pass.economyReserve + 1000)
        && (!pass.vanillaEconomy || pass.itemCount[Unit_Harvester] >= pass.spiceHarvesterTarget)) {
        const int productionBuildings = pass.itemCount[Structure_LightFactory]
            + pass.itemCount[Structure_HeavyFactory] + pass.itemCount[Structure_Barracks]
            + pass.itemCount[Structure_WOR];
        const bool rocketTech = data[Structure_RocketTurret][houseID].enabled
            && data[Structure_RocketTurret][houseID].techLevel <= currentGame->techLevel;
        const Uint32 defenceTurret = rocketTech ? Structure_RocketTurret : Structure_GunTurret;
        const int desiredDefenceTurrets = 1 + productionBuildings / 3;
        // Short wall lines for the turrets this rule builds, not one pair per
        // emplacement of a counter-air or front battery: that grew with every
        // battery turret into a hundred walls on a rich base.
        const int desiredWalls = 2 + std::min(pass.itemCount[Structure_GunTurret]
            + pass.itemCount[Structure_RocketTurret], desiredDefenceTurrets) * 2;
        if (pass.itemCount[defenceTurret] < desiredDefenceTurrets
            && campaignAvailableToBuild(turn.pBuilder,defenceTurret)
            && (!rocketTech || hasPowerBufferForTurret(pass))
            && findEffectiveTurretPlaceLocation(defenceTurret).isValid()) {
            yardTurn.itemID = defenceTurret;
            yardTurn.structureRule = rocketTech ? "rocket_defense" : "ground_defense";
        } else if (pass.itemCount[Structure_Wall] < desiredWalls
            && campaignAvailableToBuild(turn.pBuilder,Structure_Wall)
            && findPlaceLocation(Structure_Wall).isValid()) {
            yardTurn.itemID = Structure_Wall; yardTurn.structureRule = "ground_defense";
        }
    }
    // 17b. Enemy-facing rocket battery, Custom Hard/Brutal.
    //
    // Deliberately the last defensive rule, after the refineries,
    // factories, repair yards, tech, Palace, Starport and the
    // established fixed-count defence above: the goal scales with
    // base size, so placed any earlier it starves the economy that
    // funds it. It also keeps every existing restriction - the
    // production-core gate (routineRocketsAllowed), the actual mod
    // power setting, the CY upgrade level, the tech check - and adds
    // two of its own: the economy gate inside frontBatteryGoal(),
    // and one emplacement per construction pass across all yards, so
    // a belt is built over minutes rather than in one cash dump.
    // The growth interleave is the established one, so city and
    // economy orders keep their share of the yards.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && yardTurn.batteryTurrets > 0
        && pass.routineRocketsAllowed
        && pass.itemCount[Structure_RocketTurret] < yardTurn.batteryTurrets
        && FrontBatteryPolicy::mayOrderThisPass(
            pass.orderedThisTick.count(Structure_RocketTurret) ? 1 : 0)
        && RocketTurretPolicy::proactiveCoverageTurn(pass.airEngaged, pass.uncoveredCoreAssets,
            nonServiceConstructionOrders)
        && turn.pBuilder->getCurrentUpgradeLevel() >= 2
        && hasPowerBufferForTurret(pass)
        && campaignAvailableToBuild(turn.pBuilder,Structure_RocketTurret)
        && pass.money >= pass.economyReserve + data[Structure_RocketTurret][houseID].price
        // An established city runs out of free legal ground on the side
        // the enemy comes from long before the goal is met, and the
        // belt then stops growing exactly where it is needed. The
        // fallback prefers one of this house's own cheaper eligible
        // R/C/I lots - and only after the free-ground search has
        // already answered that there is none. Every gate above still
        // has to pass first, so a lot is never displaced for an
        // emplacement this base does not want, cannot power, cannot
        // reach or cannot pay for. Choosing the site commits nothing:
        // the lot is still standing when this order is accepted and
        // goes only when finished material is placed on it.
        && (findFrontBatteryPlaceLocation().isValid()
            || findBatteryClearanceSite().isValid())) {
        yardTurn.itemID = Structure_RocketTurret; yardTurn.structureRule = "front_battery";
        logDebug("FRONT-BATTERY: rocket turret %d/%d (counter-air goal %d)",
            pass.itemCount[Structure_RocketTurret] + 1, yardTurn.batteryTurrets, yardTurn.counterAirTurrets);
    }
    // 18. City zone structures (when city sim is active)
    // Zones are 2x2 structures built via the CY; runZoneGrowth()
    // requires an actual structure object, so tile-flag placement
    // (CMD_CITY_PLACE_ZONE without a structure) does not work.

    // Rank zones by live demand and the R/I/C balance. Try the next
    // candidate if the preferred zone has no available building site.
    // In city sim, zones are the economic base — only windtrap is
    // required so the AI doesn't gate growth behind military
    // infrastructure that itself requires population (e.g. Starport
    // now needs 10000 pop). Outside city sim there's no zone path
    // here at all.
    if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && currentGame && currentGame->isCitySimEnabled()
        && pass.money > 200
        && pass.itemCount[Structure_WindTrap] > 0) {
        // Zones consume power as they grow. Before placing one,
        // ensure we have surplus power. If not, build a nuclear
        // plant (or windtrap fallback) first.
        constexpr int kZonePowerHeadroom = 24;  // worst case: industrial L3
        const int powerSurplus = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
        if (powerSurplus < kZonePowerHeadroom) {
            if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant))
                && findPlaceLocation(Structure_NuclearPlant).isValid()) {
                yardTurn.itemID = Structure_NuclearPlant; yardTurn.structureRule = "power";
                logDebug("CITY-ZONE-POWER: Building Nuclear Plant before zoning (surplus=%d, need=%d)",
                    powerSurplus, kZonePowerHeadroom);
            } else if ((!powerGenerationPending(pass) && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap))
                && findPlaceLocation(Structure_WindTrap).isValid()) {
                yardTurn.itemID = Structure_WindTrap; yardTurn.structureRule = "power";
                logDebug("CITY-ZONE-POWER: Building Windtrap before zoning (surplus=%d, need=%d)",
                    powerSurplus, kZonePowerHeadroom);
            }
        } else {
            const int resCount = pass.itemCount[Structure_ZoneResidential];
            const int comCount = pass.itemCount[Structure_ZoneCommercial];
            const int indCount = pass.itemCount[Structure_ZoneIndustrial];
            const Uint32 zoneID = chooseCityEconomy(pass, turn.pBuilder, false);

            if (zoneID != NONE_ID && campaignAvailableToBuild(turn.pBuilder,zoneID)
                && findPlaceLocation(zoneID).isValid()) {
                yardTurn.itemID = zoneID; yardTurn.structureRule = "city_economy";
                logDebug("CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d surplus=%d)",
                    getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
                    pass.ownResValve, pass.ownComValve, pass.ownIndValve, powerSurplus);
            }
        }
    }
}

void QuantBot::vetYardChoice(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // All city power branches share the same investment decision.
    // Include committed consumers, but never change vanilla policy.
    if (yardTurn.isCitySim && (yardTurn.itemID == Structure_NuclearPlant || yardTurn.itemID == Structure_WindTrap)
        && pass.itemCount[Structure_WindTrap] > 0) {
        const int need = std::max(1,getHouse()->getPowerRequirement()
            + pass.cityPowerReserve - getHouse()->getProducedPower());
        const Coord windSite = campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap)
            ? findPlaceLocation(Structure_WindTrap) : Coord::Invalid();
        const Coord nuclearSite = campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant)
            ? findPlaceLocation(Structure_NuclearPlant) : Coord::Invalid();
        const int windOutput = std::max(1,-data[Structure_WindTrap][houseID].power);
        const int windNeeded = (need+windOutput-1)/windOutput;
        const int cash = std::max(0,pass.money);
        // Once the first windtrap establishes power, prefer a reactor
        // whenever its real footprint and price are feasible. Extra
        // windtrap room is no longer a reason to choose less capacity.
        const bool nuclear = nuclearSite.isValid()
            && (cash >= data[Structure_NuclearPlant][houseID].price
                || onInstallments(turn, Structure_NuclearPlant));
        yardTurn.itemID = nuclear ? Structure_NuclearPlant : windSite.isValid() ? Structure_WindTrap : NONE_ID;
        if (yardTurn.itemID == NONE_ID) yardTurn.skipRemainingStructureLogic = true;
        traceDecision("city_generator_choice", AITelemetry::Record().set("item",yardTurn.itemID)
            .set("need",need).set("committed_demand",pass.committedPowerDemand).set("cash",cash)
            .set("forecast_growth",projectedPowerDemandGrowth).set("forecast_seconds",120)
            .set("zone_power_current",pass.currentZonePower).set("zone_power_mature",pass.matureZonePower)
            .set("zone_growth_headroom",pass.zoneGrowthHeadroom).set("power_shortage",!getHouse()->hasPower())
            .set("nuclear_available",campaignAvailableToBuild(turn.pBuilder,Structure_NuclearPlant))
            .set("nuclear_placement",placementScoreDetails[Structure_NuclearPlant])
            .set("nuclear_site",nuclearSite.isValid()).set("nuclear_price",data[Structure_NuclearPlant][houseID].price)
            .set("wind_needed",windNeeded).set("wind_site",windSite.isValid()).set("nuclear",nuclear)
            .set("installments",nuclear && onInstallments(turn, Structure_NuclearPlant))
            .set("industrial_demand",pass.ownIndValve));
    }
    if (yardTurn.itemID!=NONE_ID && yardTurn.itemID!=Structure_WindTrap
        && campaignPowerNeeded(pass, std::max(0,data[yardTurn.itemID][houseID].power))
        && campaignAvailableToBuild(turn.pBuilder,Structure_WindTrap)) {
        yardTurn.itemID=Structure_WindTrap; yardTurn.structureRule="campaign_planned_power";
        yardTurn.crimeServiceSite=Coord::Invalid();
    }
    if (pass.sharedSpending && !pass.fundedCityProduction
        && (yardTurn.itemID==Structure_LightFactory || yardTurn.itemID==Structure_HeavyFactory || yardTurn.itemID==Structure_HighTechFactory)
        && getHouse()->getNumItems(yardTurn.itemID)>0) {
        const bool fundedLane=std::any_of(pass.capitalCandidates.begin(),pass.capitalCandidates.end(),[&](const auto& c) {
            return c.builder==turn.pBuilder->getObjectID() && c.item==yardTurn.itemID && c.score>0;
        });
        if (!fundedLane) {
            traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",pass.capitalDecision)
                .set("builder",turn.pBuilder->getObjectID()).set("item",yardTurn.itemID).set("rule",yardTurn.structureRule)
                .set("reason","no_funded_production_bottleneck"));
            yardTurn.itemID=NONE_ID;
        }
    }
    // Dedup: skip if another CY already ordered this unique structure
    // this tick. Zones and turrets are allowed in multiples.
    if (yardTurn.itemID != NONE_ID) {
        bool isMultiBuild = (yardTurn.itemID == Structure_ZoneResidential
            || yardTurn.itemID == Structure_ZoneCommercial
            || yardTurn.itemID == Structure_ZoneIndustrial
            || yardTurn.itemID == Structure_RocketTurret
            || yardTurn.itemID == Structure_GunTurret
            || yardTurn.itemID == Structure_PoliceStation
            || yardTurn.itemID == Structure_Wall
            || yardTurn.itemID == Structure_Slab1);
        if (!isMultiBuild && pass.orderedThisTick.count(yardTurn.itemID)) {
            logDebug("DEDUP: Skipping %s — already ordered by another CY this tick",
                getItemNameByID(yardTurn.itemID).c_str());
            if (pass.emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                .set("builder", turn.pBuilder->getObjectID()).set("item", yardTurn.itemID)
                .set("rule", yardTurn.structureRule).set("reason", "deduplicated"));
            yardTurn.itemID = NONE_ID;
            yardTurn.crimeServiceSite = Coord::Invalid();
        }
    }

    // Retry useful economic work when another yard claimed the strategic
    // order, or a chosen structure has no feasible footprint. Never
    // default to residential regardless of demand or power.
    if (yardTurn.itemID != NONE_ID && yardTurn.itemID != Structure_RocketTurret
        && yardTurn.itemID != Structure_GunTurret && yardTurn.itemID != Structure_PoliceStation
        && !findPlaceLocation(yardTurn.itemID).isValid()) {
        if (pass.emitStatsLog) logDebug("PRODUCTION: CY=%u no site for item=%u", turn.pBuilder->getObjectID(), yardTurn.itemID);
        if (pass.emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
            .set("builder", turn.pBuilder->getObjectID()).set("item", yardTurn.itemID)
            .set("rule", yardTurn.structureRule).set("reason", "no_site"));
        yardTurn.itemID = NONE_ID;
    }
    if (yardTurn.isCitySim && yardTurn.itemID != NONE_ID
        && (yardTurn.structureRule == std::string("city_economy") || yardTurn.structureRule == std::string("city_economy_fallback"))
        && pass.money < data[yardTurn.itemID][houseID].price) {
        yardTurn.itemID=NONE_ID;
        yardTurn.skipRemainingStructureLogic=true;
    }
    // A validated growth-cap project is the one order that may stand below
    // its price here: the engine charges it in installments out of arriving
    // income, and queuedProductionCost already withholds its whole unpaid
    // remainder from every later pass, so nothing else spends that income.
    if ((pass.vanillaEconomy || yardTurn.isCitySim) && yardTurn.itemID != NONE_ID && pass.money < data[yardTurn.itemID][houseID].price
        && !onInstallments(turn, yardTurn.itemID)
        && !(yardTurn.isCitySim && !getHouse()->hasPower()
            && (yardTurn.itemID == Structure_WindTrap || yardTurn.itemID == Structure_NuclearPlant))) {
        if (pass.emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
            .set("builder", turn.pBuilder->getObjectID()).set("item", yardTurn.itemID)
            .set("rule", yardTurn.structureRule).set("reason", "committed_cash")
            .set("spendable", pass.money).set("queued_production_cost", pass.queuedProductionCost));
        yardTurn.itemID = NONE_ID;
    }
    // Paid imports must still be delivered. If they fill the last slots,
    // wait before adding a refinery that could spawn another worker first.
    // Once the fleet has arrived, capacity-only refineries are safe again.
    if (yardTurn.itemID==Structure_Refinery && pass.engineHarvesterLimit>0
        && pass.actualHarvesters<pass.engineHarvesterLimit
        && pass.itemCount[Unit_Harvester]+pass.itemCount[Unit_RebelHarvester]>=pass.engineHarvesterLimit) {
        traceDecision("construction_rejected", AITelemetry::Record()
            .set("builder",turn.pBuilder->getObjectID()).set("item",yardTurn.itemID)
            .set("rule",yardTurn.structureRule).set("reason","pending_worker_capacity"));
        yardTurn.itemID=NONE_ID;
    }
    if (yardTurn.isCitySim && DuneCity::isCityZoneStructure(yardTurn.itemID)
        && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<24) {
        traceDecision("construction_rejected",AITelemetry::Record()
            .set("builder",turn.pBuilder->getObjectID()).set("item",yardTurn.itemID)
            .set("rule",yardTurn.structureRule).set("reason","city_growth_power_headroom"));
        yardTurn.itemID=NONE_ID;
        yardTurn.skipRemainingStructureLogic=true;
    }
    if (yardTurn.itemID==Structure_RocketTurret && !pass.routineRocketsAllowed && yardTurn.structureRule!=std::string("perimeter_battery")) {
        yardTurn.itemID=NONE_ID;
        yardTurn.crimeServiceSite=Coord::Invalid();
        yardTurn.structureRule="core_infrastructure_before_more_rockets";
    }
    if (pass.emitStatsLog) logDebug("BUILD-CHOICE: CY=%u item=%u credits=%d skip=%d",
        turn.pBuilder->getObjectID(), yardTurn.itemID, pass.money, yardTurn.skipRemainingStructureLogic);
    // A later power/tech override cannot reuse a service's 1x1 site.
    if (yardTurn.itemID != Structure_RocketTurret && yardTurn.itemID != Structure_PoliceStation)
        yardTurn.crimeServiceSite = Coord::Invalid();
}

bool QuantBot::placeYardChoice(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    Coord selectedPlaceLocation = Coord::Invalid();
    if (yardTurn.itemID != NONE_ID && campaignAvailableToBuild(turn.pBuilder,yardTurn.itemID)) {
        if (yardTurn.crimeServiceSite.isValid()) selectedPlaceLocation = yardTurn.crimeServiceSite;
        else if (yardTurn.itemID == Structure_PoliceStation) {
            // Legacy demand/emergency rules use the same bounded service
            // scorer; no alternate full-map search bypasses its overlap costs.
            Uint32 service = NONE_ID;
            selectCityServiceInvestment(turn.pBuilder,pass.money,true,service,selectedPlaceLocation,false,yardTurn.itemID);
        } else if (yardTurn.itemID == Structure_RocketTurret
            && yardTurn.structureRule == std::string("perimeter_battery")) {
            selectedPlaceLocation = findPerimeterTurretLocation();
        } else if (yardTurn.itemID == Structure_RocketTurret
            && yardTurn.structureRule == std::string("front_battery")) {
            // The battery rule chose this order, so it also chooses the
            // site: the city coverage planner has no front. Free ground
            // first; the clearance fallback answers only when there is
            // none, and it answers with the tile of the single own lot
            // the emplacement will displace.
            selectedPlaceLocation = findFrontBatteryPlaceLocation();
            if (selectedPlaceLocation.isInvalid())
                selectedPlaceLocation = findBatteryClearanceSite();
        } else selectedPlaceLocation = (yardTurn.itemID == Structure_RocketTurret || yardTurn.itemID == Structure_GunTurret)
            ? findEffectiveTurretPlaceLocation(yardTurn.itemID) : findPlaceLocation(yardTurn.itemID);
    }

    // An idle yard does not wait for a dearer project while it can
    // afford a demanded plot. This also covers deduplication, failed
    // placement and saving rules that set skipRemainingStructureLogic.
    int selectedCost=yardTurn.itemID!=NONE_ID ? purchasePrice(turn.pBuilder,yardTurn.itemID) : 0;
    if (selectedPlaceLocation.isValid())
        for (const auto& foundation:foundationOrders(turn.pBuilder,yardTurn.itemID,selectedPlaceLocation))
            selectedCost+=purchasePrice(turn.pBuilder,foundation.item);
    if (yardTurn.isCitySim && !turn.pBuilder->isUpgrading() && !turn.pBuilder->isOnHold()
        && turn.pBuilder->getProductionQueueSize()==0
        && !(pass.protectionCapital && capitalPending(pass))
        && !yardTurn.serviceSavingHold
        // An installment-funded growth-cap project is deliberately short
        // of its full price. That is not "cannot afford it": replacing it
        // with a plot here is what prevented it from ever being ordered.
        && !onInstallments(turn, yardTurn.itemID)
        && (!selectedPlaceLocation.isValid() || pass.money<selectedCost)) {
        const Uint32 zone=affordableCityZone(pass, turn.pBuilder,pass.money);
        if (zone!=NONE_ID) {
            yardTurn.itemID=zone;selectedPlaceLocation=findPlaceLocation(zone);
            yardTurn.crimeServiceSite=Coord::Invalid();yardTurn.structureRule="idle_city_growth";
            yardTurn.skipRemainingStructureLogic=false;
        }
    }
    if (yardTurn.serviceSavingHold && yardTurn.itemID==NONE_ID)
        traceDecision("city_service_saving",AITelemetry::Record()
            .set("builder",turn.pBuilder->getObjectID()).set("rule",yardTurn.structureRule)
            .set("spendable",pass.money).set("projected_cash",pass.cashFlow.projectedCash));

    if (AITelemetry::log().enabled() && (yardTurn.itemID != NONE_ID || pass.emitStatsLog)) {
        AITelemetry::Record site;
        site.set("valid", selectedPlaceLocation.isValid()).set("x", selectedPlaceLocation.x).set("y", selectedPlaceLocation.y);
        if (selectedPlaceLocation.isValid() && currentGame->getCitySimulation()) {
            auto* sim = currentGame->getCitySimulation();
            const int x = selectedPlaceLocation.x, y = selectedPlaceLocation.y;
            const auto size = getStructureSize(yardTurn.itemID);
            const auto roads = cityRoadImpact(getMap(), x, y, size.x, size.y, yardTurn.itemID);
            if (placementCache.count(yardTurn.itemID) && placementCache[yardTurn.itemID] == selectedPlaceLocation)
                site.set("placement_quality", placementScoreDetails[yardTurn.itemID]);
            site.set("road_connections_preserved", roads.preservesConnections)
                .set("roads_covered", roads.roadsCovered).set("redundant_roads_reused",roads.redundantRoadsCovered).set("junction_bonus", roads.junctionBonus);
            site.set("pollution", sim->getPollutionDensityMap().worldGet(x, y))
                .set("crime", sim->getCrimeRateMap().worldGet(x, y))
                .set("traffic", sim->getTrafficDensityMap().worldGet(x, y))
                .set("land_value", sim->getLandValueMap().worldGet(x, y));
        }
        traceDecision("construction_selection", AITelemetry::Record().set("capital_plan",pass.capitalDecision).set("builder", turn.pBuilder->getObjectID())
            .set("item", yardTurn.itemID).set("rule", yardTurn.structureRule).set("skip", yardTurn.skipRemainingStructureLogic)
            .set("state", decisionState(pass)).set("site", site));
    }

    if (selectedPlaceLocation.isValid()) {
        // Plan redevelopment against the ground after its zones are
        // removed. Buildings consumed their old foundations at placement,
        // so replacement buildings need newly queued slabs as well.
        std::vector<Uint32> zonesToRemove;
        const bool redevelop = redevelopmentZones(yardTurn.itemID, selectedPlaceLocation, zonesToRemove);
        // The battery clearance fallback deliberately does NOT join the
        // redevelopment route here. Accepting an order into a yard's
        // queue is not payment and does not place anything: the order
        // can still be cancelled, outbid, re-sited or lost with the
        // yard, and a lot demolished now would be a pure loss. The lot
        // stays standing; it is displaced by commitBatteryClearance()
        // at the moment finished material is actually placed on it.
        // Plan foundations against the ground after clearance, without
        // changing the live map. The former building consumed its old
        // foundation, so a new slab is queued where needed.
        std::vector<Uint32> clearanceLots;
        const bool batteryClearance = !redevelop && yardTurn.itemID == Structure_RocketTurret
            && yardTurn.structureRule == std::string("front_battery")
            && batteryClearanceZones(selectedPlaceLocation, clearanceLots, batteryAnchor());
        const auto plan = foundationPlan(turn.pBuilder,yardTurn.itemID,selectedPlaceLocation,
            batteryClearance ? clearanceLots : zonesToRemove);
        if (!plan.complete) {
            traceDecision("construction_rejected",AITelemetry::Record()
                .set("builder",planningBuilder).set("item",yardTurn.itemID).set("rule",yardTurn.structureRule)
                .set("reason","foundation_incomplete")
                .set("x",selectedPlaceLocation.x).set("y",selectedPlaceLocation.y));
            return true;
        }
        const auto& foundations=plan.orders;
        int foundationCost=0;
        for (const auto& foundation:foundations) foundationCost+=purchasePrice(turn.pBuilder,foundation.item);
        // Foundations are queued ahead of the building, so this is where the
        // whole production start budget has to be payable: the foundations
        // plus the building's first installment. The execution check then
        // asks for the installment alone, because queueing the foundations
        // has already charged their cost against this pass's money.
        //
        // A blackout exemption matches produceItemWithLogging(): the cheap
        // generator that restores income must not be refused here because an
        // unpaid queue has withheld the planner's virtual money, or a city
        // with a reactor in flight could never recover under concrete rules.
        const bool emergencyGenerator = !getHouse()->hasPower()
            && (yardTurn.itemID == Structure_WindTrap || yardTurn.itemID == Structure_NuclearPlant);
        const int acceptanceFloor = onInstallments(turn, yardTurn.itemID)
            ? CityEconomyInvestmentPolicy::productionStartBudget(
                  purchasePrice(turn.pBuilder,yardTurn.itemID),foundationCost,cityBuildTicks(pass, yardTurn.itemID))
            : purchasePrice(turn.pBuilder,yardTurn.itemID)+foundationCost;
        const bool cityWorOrder = pass.citySimEnabled && yardTurn.itemID == Structure_WOR;
        const int64_t fundingFloor = int64_t(acceptanceFloor)
            + (cityWorOrder ? pass.cityWorCashReserve : 0);
        if ((!foundations.empty() || cityWorOrder) && !emergencyGenerator && pass.money < fundingFloor) {
            traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",pass.capitalDecision)
                .set("builder",planningBuilder).set("item",yardTurn.itemID)
                .set("reason",cityWorOrder ? "city_anti_air_reserve" : "foundation_budget")
                .set("price",purchasePrice(turn.pBuilder,yardTurn.itemID)).set("foundation_cost",foundationCost)
                .set("acceptance_floor",fundingFloor).set("installments",onInstallments(turn, yardTurn.itemID))
                .set("spendable",pass.money));
            return true;
        }
        size_t foundationsQueued=0;
        auto cancelFoundations=[&]() {
            for (size_t i=0;i<foundationsQueued;++i)
                doCancelItem(turn.pBuilder,foundations[i].item);
            yardTurn.placeLocations->clear();
        };
        for (const auto& foundation : foundations) {
            // These slabs belong to the validated blackout recovery site.
            // Its unpaid reactor reservation must not block the slabs while
            // allowing only the generator itself through the same guard.
            turn.emergencyFoundationOrder = emergencyGenerator;
            if (!produceItemWithLogging(pass, turn, foundation.item,__LINE__,"building_foundation")) break;
            yardTurn.placeLocations->push_back(Coord(foundation.x,foundation.y));
            ++foundationsQueued;
        }
        turn.emergencyFoundationOrder = false;
        if (foundationsQueued!=foundations.size()) {
            cancelFoundations();
            return true;
        }
        if (!foundations.empty()) yardTurn.placeLocations->push_back(selectedPlaceLocation);

        if (produceItemWithLogging(pass, turn, yardTurn.itemID, __LINE__, yardTurn.structureRule)) {
            if (yardTurn.isCitySim && yardTurn.itemID != Structure_Road && yardTurn.itemID != Structure_Slab1
                && yardTurn.itemID != Structure_Slab4) {
                if (yardTurn.crimeServiceSite.isValid()) nonServiceConstructionOrders = 0;
                else nonServiceConstructionOrders = std::min<Uint32>(6, nonServiceConstructionOrders + 1);
            }
            if (redevelop) {
                AITelemetry::Record removed;
                for (Uint32 id : zonesToRemove) {
                    auto* zone=dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(id));
                    if (!zone || zone->getOwner()!=getHouse()) continue;
                    const Coord z=zone->getLocation();
                    const auto* sim=currentGame->getCitySimulation();
                    const auto& state=sim->getHouseState(getHouse()->getHouseID());
                    const int demand=zone->getItemID()==Structure_ZoneResidential ? state.resValve
                        : zone->getItemID()==Structure_ZoneCommercial ? state.comValve : state.indValve;
                    removed.set(std::to_string(id),AITelemetry::Record().set("item",zone->getItemID())
                        .set("demand",demand).set("land_value",sim->getLandValueMap().worldGet(z.x,z.y))
                        .set("density",getMap().getTile(z.x,z.y)->getCityZoneDensity()));
                    pass.itemCount[zone->getItemID()]--;
                    zone->demolish();
                }
                traceDecision("redevelopment_committed", AITelemetry::Record().set("builder",planningBuilder)
                    .set("item",yardTurn.itemID).set("x",selectedPlaceLocation.x).set("y",selectedPlaceLocation.y)
                    .set("removed_zones",removed));
            }
            // A clearance reservation records an intention only. The
            // lot named here is still standing and stays standing
            // unless and until commitBatteryClearance() displaces it.
            if (batteryClearance) traceDecision("front_battery_clearance_reserved",
                AITelemetry::Record().set("builder",planningBuilder)
                    .set("x",selectedPlaceLocation.x).set("y",selectedPlaceLocation.y)
                    .set("lot",clearanceLots.empty() ? Uint32(NONE_ID) : clearanceLots.front())
                    .set("foundation_orders",int(foundations.size())));
            if (yardTurn.itemID==Structure_Refinery && (pass.engineHarvesterLimit==0
                || pass.itemCount[Unit_Harvester]+pass.itemCount[Unit_RebelHarvester]<pass.engineHarvesterLimit))
                ++pass.itemCount[Unit_Harvester];
            reservedStructures[planningBuilder] = {yardTurn.itemID, selectedPlaceLocation};
            if (yardTurn.placeLocations->empty()) yardTurn.placeLocations->push_back(selectedPlaceLocation);
            traceDecision("site_reserved", AITelemetry::Record().set("builder", planningBuilder)
                .set("item", yardTurn.itemID).set("x", selectedPlaceLocation.x).set("y", selectedPlaceLocation.y));
            pass.orderedThisTick.insert(yardTurn.itemID);
            pass.itemCount[yardTurn.itemID]++;

            if (yardTurn.itemID == pass.strategicReserveItem) pass.strategicReserveCost = 0;
            clearPlacementCache();
        } else {
            cancelFoundations();
        }
    }
    else if (yardTurn.itemID != NONE_ID && campaignAvailableToBuild(turn.pBuilder,yardTurn.itemID)) {
        // Extend the buildable area with concrete for anything QuantBot
        // would have founded anyway; the same policy decides both, so a
        // building never waits for ground it would not have prepared.
        bool needsConcreteExpansion = getGameInitSettings().getGameOptions().concreteRequired
            && QuantBotBuildPolicy::foundationRequiredForItem(yardTurn.itemID);

        if (needsConcreteExpansion && campaignAvailableToBuild(turn.pBuilder,Structure_Slab1)) {
            Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
            if (slabLocation.isValid()) {
                produceItemWithLogging(pass, turn, Structure_Slab1,__LINE__,"concrete_expansion");
                logDebug("Building concrete slab to expand buildable area for itemID %d at (%d,%d)", yardTurn.itemID, slabLocation.x, slabLocation.y);
            } else {
                // Cannot place slab - silenced (too spammy)
            }
        } else {
            logDebug("Cannot build itemID %d: no place to build and slabs not available", yardTurn.itemID);
        }
    }
    else if (yardTurn.itemID != NONE_ID && !campaignAvailableToBuild(turn.pBuilder,yardTurn.itemID)) {
        logDebug("Cannot build itemID %d: not available (prerequisites not met)", yardTurn.itemID);
    }
    else if (yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic && pass.emitStatsLog) {
        logDebug("No structure selected to build (money: %d, skipRemaining: %d)", pass.money, yardTurn.skipRemainingStructureLogic);
    }
    return false;
}

void QuantBot::useSpareYardCapacity(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    // Only use spare yard capacity, after all strategic/city choices.
    if (yardTurn.isCitySim && yardTurn.itemID == NONE_ID && !yardTurn.skipRemainingStructureLogic
        && !pass.roadMaintenanceAttempted && !turn.pBuilder->isUpgrading()
        && turn.pBuilder->getProductionQueueSize() == 0 && campaignAvailableToBuild(turn.pBuilder,Structure_Road)) {
        pass.roadMaintenanceAttempted = true;
        const int roadPrice = std::max(1,data[Structure_Road][houseID].price);
        const int repaired = queueCityRoadRepairs(turn.pBuilder,std::min(8,std::max(0,pass.money-pass.economyReserve)/roadPrice));
        pass.money -= repaired*roadPrice;
        pass.capitalOrderedCost += repaired*roadPrice;
        if (repaired) traceDecision("capital_road_batch",AITelemetry::Record().set("capital_plan",pass.capitalDecision)
            .set("builder",planningBuilder).set("count",repaired).set("price",roadPrice).set("cost",repaired*roadPrice));
    }

    // City yards use the demand-ranked fallback before placement above.
    // Outside city mode an otherwise idle yard can extend concrete, but
    // only where concrete means something.
    if (!yardTurn.isCitySim && getGameInitSettings().getGameOptions().concreteRequired
        && !yardTurn.skipRemainingStructureLogic && pass.money > 500 && turn.pBuilder->getProductionQueueSize() < 1
        && yardTurn.itemID == NONE_ID && campaignAvailableToBuild(turn.pBuilder,Structure_Slab1)) {
        Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
        if (slabLocation.isValid()) produceItemWithLogging(pass, turn, Structure_Slab1,__LINE__,"concrete_expansion");
    }
}

bool QuantBot::placeYardStructure(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    int houseID = getHouse()->getHouseID();
    if (turn.pBuilder->isWaitingToPlace()) {
        Uint32 itemToBePlaced = turn.pBuilder->getCurrentProducedItem();
        logDebug("PRODUCTION: CY waiting to place itemID: %d, credits: %d, queued locations: %zu", itemToBePlaced, pass.money, yardTurn.placeLocations->size());
        Coord location;
        bool placementIssueHandled = false;
        auto tracePlacementIssue = [&](const char* event, const char* reason, const Coord& site) {
            if (!AITelemetry::log().enabled()) return;
            AITelemetry::Record tiles;
            const Coord size = getStructureSize(itemToBePlaced);
            if (site.isValid()) for (int dx = 0; dx < size.x; ++dx) for (int dy = 0; dy < size.y; ++dy) {
                const int x = site.x + dx, y = site.y + dy;
                const Tile* tile = getMap().tileExists(x,y) ? getMap().getTile(x, y) : nullptr;
                AITelemetry::Record state;
                state.set("x", x).set("y", y).set("exists", tile != nullptr);
                if (tile) {
                    state.set("terrain", tile->getType()).set("blocked", tile->isBlocked())
                        .set("zone", static_cast<int>(tile->getCityZoneType())).set("road", tile->isRoad())
                        .set("in_build_range", getMap().isWithinBuildRange(x, y, getHouse()));
                    if (const auto* object = tile->getGroundObject())
                        state.set("occupant", object->getObjectID()).set("occupant_item", object->getItemID())
                            .set("occupant_structure", object->isAStructure());
                }
                tiles.set(std::to_string(dx) + "," + std::to_string(dy), state);
            }
            traceDecision(event, AITelemetry::Record().set("builder", yardTurn.pConstYard->getObjectID())
                .set("item", itemToBePlaced).set("reason", reason).set("x", site.x).set("y", site.y)
                .set("reserved_overlap", site.isValid() && overlapsReservedStructure(site.x, site.y, size.x, size.y))
                .set("recent_loss_nearby", site.isValid() && nearRecentStructureLoss(site.x, site.y, size.x, size.y))
                .set("enemy_fire_risk", dangerAt(site,size)).set("recent_loss_risk", dangerAt(site,size,true))
                .set("reactor_clearance", site.isValid() && reactorClearance(itemToBePlaced,site))
                .set("placement_quality",placementScoreDetails[itemToBePlaced]).set("planned_locations_remaining", yardTurn.placeLocations->size()).set("tiles", tiles));
        };


        // House::placeStructure paves whatever tile it is given and
        // clears its road flag, so the only guard a slab has is this
        // one. Rock only, nothing standing on it, no road erased.
        const bool placingSlab = itemToBePlaced == Structure_Slab1
            || itemToBePlaced == Structure_Slab4;

        // Finished material in hand is the first honest moment to
        // displace a lot for an enemy-facing battery, so the
        // clearance is committed here and the freed ground is used
        // by the very next statements. If it declines - free ground
        // opened, the emplacement is no longer queued or funded, a
        // guard has changed, or this pass has already displaced its
        // one lot - the lot stays standing and the ordinary
        // validity checks below simply re-plan or defer.
        bool deferClearance = false;
        commitBatteryClearance(turn.pBuilder, itemToBePlaced, (*yardTurn.placeLocations), pass.money,
            pass.economyReserve, pass.clearanceCommitmentShortfall, &deferClearance);
        // A refused clearance is still occupied by its lot. Never
        // feed that finished slab through ordinary foundation
        // replanning, which would cancel it as blocked and leave
        // the turret behind it without concrete. Temporary cash or
        // pass-budget pressure retains the material; a lost site,
        // prerequisite or scope releases the project and its cash.
        const auto pendingBattery = reservedStructures.find(planningBuilder);
        if (pendingBattery != reservedStructures.end()
            && pendingBattery->second.item == Structure_RocketTurret
            && getMap().tileExists(pendingBattery->second.location.x,
                                   pendingBattery->second.location.y)
            && getMap().getTile(pendingBattery->second.location)->hasCityZone()) {
            if (!deferClearance) {
                if (placingSlab) doCancelItem(turn.pBuilder,itemToBePlaced);
                doCancelItem(turn.pBuilder,Structure_RocketTurret);
                reservedStructures.erase(planningBuilder);
                yardTurn.placeLocations->clear();
                clearPlacementCache();
            }
            return true;
        }

        auto plannedSlabLegal = [&](Uint32 slab, const Coord& site) {
            const Coord span = getStructureSize(slab);
            if (!site.isValid()) return false;
            for (int dx = 0; dx < span.x; ++dx) for (int dy = 0; dy < span.y; ++dy) {
                const int x = site.x + dx, y = site.y + dy;
                if (!getMap().tileExists(x, y)) return false;
                const Tile* tile = getMap().getTile(x, y);
                if (tile->isRoad() || !tile->isRock() || tile->isMountain()
                    || tile->hasCityZone()) return false;
            }
            // Nothing standing on it but our own traffic (the footprint rule).
            return placeableAmongOwnUnits(site.x, site.y, span.x, span.y, slab);
        };

        // Check if we have a pre-stored location (from concrete pre-placement)
        if (!yardTurn.placeLocations->empty()) {
            location = yardTurn.placeLocations->front();
            Coord itemsize = getStructureSize(itemToBePlaced);

            // Verify the location is still valid
            if ((!placingSlab || plannedSlabLegal(itemToBePlaced, location))
                && placeableAmongOwnUnits(location.x, location.y, itemsize.x, itemsize.y, itemToBePlaced)
                && (itemToBePlaced != Structure_Road || !getMap().getTile(location.x,location.y)->isRoadConnection())
                && cityRoadImpact(getMap(), location.x, location.y, itemsize.x, itemsize.y, itemToBePlaced).preservesConnections
                && !overlapsReservedStructure(location.x, location.y, itemsize.x, itemsize.y)
                && preservesGroundAccess(itemToBePlaced,location)
                && (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret
                    || itemToBePlaced == Structure_Wall || itemToBePlaced == Structure_Road
                    || itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4
                    || (!nearRecentStructureLoss(location.x, location.y, itemsize.x, itemsize.y)
                        && dangerAt(location,itemsize) == 0 && TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,reactorClearance(itemToBePlaced,location))))) {
                yardTurn.placeLocations->pop_front();
                logDebug("PRODUCTION: Using pre-stored location (%d,%d) for itemID: %d", location.x, location.y, itemToBePlaced);
            } else if (itemToBePlaced == Structure_Road) {
                const Coord oldSite = location;
                location = Coord::Invalid();
                // Rate-limit unsuccessful searches by yard; a finished
                // road remains ready and can be used when a gap opens.
                if (!pass.roadMaintenanceAttempted) {
                    pass.roadMaintenanceAttempted = true;
                    location = findFinishedRoadSite(turn.pBuilder);
                }
                if (location.isValid()) {
                    yardTurn.placeLocations->pop_front();
                    tracePlacementIssue("placement_replan","road_redirected_to_gap",location);
                } else if (pass.emitStatsLog) {
                    tracePlacementIssue("placement_deferred","road_waiting_for_useful_gap",oldSite);
                }
                placementIssueHandled = true;
            } else if (placingSlab) {
                // A foundation slab is never dropped on its own: that
                // is exactly how a building ends up on bare ground.
                // Re-plan the reserved building's foundation against
                // the map as it is now and use the first order of this
                // kind. Concrete the ground no longer needs is
                // refunded while the reserved building keeps its
                // queue position and can use incomplete foundations.
                const Coord oldSite = location;
                location = Coord::Invalid();
                const auto reserved = reservedStructures.find(planningBuilder);
                QuantBotFoundationPolicy::Plan replan;
                replan.complete = true;
                if (reserved != reservedStructures.end())
                    replan = foundationPlan(turn.pBuilder, reserved->second.item, reserved->second.location);
                for (const auto& order : replan.orders)
                    if (order.item == itemToBePlaced
                        && plannedSlabLegal(order.item, Coord(order.x, order.y))) {
                        location = Coord(order.x, order.y);
                        break;
                    }
                if (location.isValid()) {
                    yardTurn.placeLocations->pop_front();
                    tracePlacementIssue("placement_replan", "foundation_replanned", location);
                } else if (replan.complete && replan.orders.empty()) {
                    tracePlacementIssue("placement_cancel", "foundation_already_prepared", oldSite);
                    doCancelItem(yardTurn.pConstYard, itemToBePlaced);
                    yardTurn.placeLocations->pop_front();
                } else {
                    // The ground can no longer take this slab. Refund
                    // the concrete only: the building behind it is
                    // finished or paid for and keeps both its queue
                    // place and its planned site. Placing it on a
                    // partly bare footprint incurs normal foundation
                    // damage instead of wasting its construction time.
                    tracePlacementIssue("placement_cancel", "foundation_unavailable", oldSite);
                    doCancelItem(yardTurn.pConstYard, itemToBePlaced);
                    yardTurn.placeLocations->pop_front();
                    clearPlacementCache();
                }
                placementIssueHandled = true;
            } else {
                // Try another legal site if this one was blocked. The
                // final foundation check below still applies to that site.
                const Coord oldSite = location;
                yardTurn.placeLocations->pop_front();
                placementCache.erase(itemToBePlaced);
                location = (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret)
                    ? findEffectiveTurretPlaceLocation(itemToBePlaced) : findPlaceLocation(itemToBePlaced);
                if (location.isInvalid()) {
                    yardTurn.placeLocations->push_front(oldSite);
                    if (pass.emitStatsLog) tracePlacementIssue("placement_deferred", "planned_site_blocked", oldSite);
                } else {
                    tracePlacementIssue("placement_replan", "planned_site_blocked", oldSite);
                }
                placementIssueHandled = true; // Suppress the no-dynamic-site branch below.
            }
        } else {
            // No pre-stored location, find one dynamically
            if (itemToBePlaced == Structure_Road) {
                location = Coord::Invalid();
                if (!pass.roadMaintenanceAttempted) {
                    pass.roadMaintenanceAttempted = true;
                    location = findFinishedRoadSite(turn.pBuilder);
                }
            } else if (placingSlab) {
                // For concrete slabs, use specialized slab placement method.
                // It is still held to the same ground rule as a planned one.
                location = findSlabPlaceLocation(itemToBePlaced);
                if (!plannedSlabLegal(itemToBePlaced, location)) location = Coord::Invalid();
            } else if (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret) {
                // For turrets, try city placement first (near crime hotspots),
                // falling back to normal perimeter placement
                location = findEffectiveTurretPlaceLocation(itemToBePlaced);
            } else {
                // For other structures, use normal method that favors adjacency
                location = findPlaceLocation(itemToBePlaced);
            }
        }

        // Only windtraps may use an exposed emergency power site.
        // A reactor under known fire risks another blackout and
        // collateral damage; normal placement must find it safety.
        if (location.isInvalid() && itemToBePlaced == Structure_WindTrap) {
            const Coord size = getStructureSize(itemToBePlaced);
            int bestRisk = std::numeric_limits<int>::max();
            int bestDistance = std::numeric_limits<int>::max();
            for (int x=0; x<=getMap().getSizeX()-size.x; ++x) {
                for (int y=0; y<=getMap().getSizeY()-size.y; ++y) {
                    const Coord site(x,y);
                    if (!getMap().okayToPlaceStructure(x,y,size.x,size.y,false,getHouse(),false,itemToBePlaced)
                        || overlapsReservedStructure(x,y,size.x,size.y)
                        || !preservesGroundAccess(itemToBePlaced,site)
                        || !TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,reactorClearance(itemToBePlaced,site))
                        || !cityRoadImpact(getMap(),x,y,size.x,size.y,itemToBePlaced).preservesConnections
                        || (currentGame->isCitySimEnabled()
                            && wouldLandlockNeighbouringZone(getMap(),houseID,x,y,size.x,size.y))) continue;
                    const int risk = dangerAt(site,size)
                        + (nearRecentStructureLoss(x,y,size.x,size.y) ? 1000 : 0);
                    const Coord yard = yardTurn.pConstYard->getLocation();
                    const int distance = std::abs(x-yard.x)+std::abs(y-yard.y);
                    if (risk < bestRisk || (risk == bestRisk && distance < bestDistance)) {
                        bestRisk = risk; bestDistance = distance; location = site;
                    }
                }
            }
            if (location.isValid()) {
                yardTurn.placeLocations->clear();
                traceDecision("placement_power_recovery", AITelemetry::Record()
                    .set("builder",planningBuilder).set("item",itemToBePlaced)
                    .set("x",location.x).set("y",location.y).set("risk",bestRisk));
            }
        }
        if (location.isInvalid() && itemToBePlaced==Structure_NuclearPlant) {
            const Coord size=getStructureSize(itemToBePlaced);
            bool potentialSite=false;
            for (int y=0;y<=getMap().getSizeY()-size.y && !potentialSite;++y)
                for (int x=0;x<=getMap().getSizeX()-size.x && !potentialSite;++x) {
                    if (!getMap().okayToPlaceStructure(x,y,size.x,size.y,false,getHouse(),true,itemToBePlaced)
                        || overlapsReservedStructure(x,y,size.x,size.y)
                        || dangerAt(Coord(x,y),size)>0) continue;
                    bool structure=false;
                    for(int dy=0;dy<size.y;++dy) for(int dx=0;dx<size.x;++dx) {
                        const auto* object=getMap().getTile(x+dx,y+dy)->getGroundObject();
                        structure |= object && object->isAStructure();
                    }
                    potentialSite=!structure;
                }
            if (!potentialSite) {
                // Refund through the ordinary production API. The
                // next planning pass can buy a smaller windtrap.
                tracePlacementIssue("placement_cancel","no_safe_nuclear_footprint",Coord::Invalid());
                doCancelItem(yardTurn.pConstYard,itemToBePlaced);
                yardTurn.placeLocations->clear();
                reservedStructures.erase(planningBuilder);
                --pass.itemCount[itemToBePlaced];
                clearPlacementCache();
                placementIssueHandled=true;
            }
        }
        if (location.isValid() && !preservesGroundAccess(itemToBePlaced,location)) {
            tracePlacementIssue("placement_deferred", "ground_exit_blocked", location);
            location=Coord::Invalid();
            placementIssueHandled=true;
        }
        // Last gate before the ground is committed. Foundations
        // are still planned and ordered before every building,
        // but once a building is finished, cancelling it because
        // its slabs were lost throws away the whole building and
        // the yard time that made it: forty-eight were refunded
        // this way in one match, three of them inside the first
        // ten minutes. Prefer an equally good prepared footprint
        // nearby, and otherwise place it where it was planned and
        // let the engine apply its own foundation damage.
        if (location.isValid() && !placingSlab
            && getGameInitSettings().getGameOptions().concreteRequired
            && QuantBotBuildPolicy::foundationRequiredForItem(itemToBePlaced)) {
            const Coord span=getStructureSize(itemToBePlaced);
            auto footprintPrepared=[&](const Coord& site) {
                for (int dx=0;dx<span.x;++dx)
                    for (int dy=0;dy<span.y;++dy) {
                        const int x=site.x+dx, y=site.y+dy;
                        if (!getMap().tileExists(x,y)
                            || !getMap().getTile(x,y)->hasPreparedFoundation()) return false;
                    }
                return true;
            };
            if (!footprintPrepared(location)) {
                // Same legality the planned site had to satisfy:
                // real engine placement, no reservation overlap,
                // ground access, roads and neighbouring lots. A
                // substitute is only taken if it is free now: the
                // planned site is the one that waits for our units.
                auto usableSite=[&](const Coord& site) {
                    return footprintPrepared(site)
                        && getMap().okayToPlaceStructure(site.x,site.y,span.x,span.y,
                            false,getHouse(),false,itemToBePlaced)
                        && !overlapsReservedStructure(site.x,site.y,span.x,span.y)
                        && preservesGroundAccess(itemToBePlaced,site)
                        && (itemToBePlaced==Structure_RocketTurret || itemToBePlaced==Structure_GunTurret
                            || (!nearRecentStructureLoss(site.x,site.y,span.x,span.y)
                                && dangerAt(site,span)==0
                                && TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,
                                    reactorClearance(itemToBePlaced,site))))
                        && cityRoadImpact(getMap(),site.x,site.y,span.x,span.y,itemToBePlaced).preservesConnections
                        && !(currentGame->isCitySimEnabled()
                            && wouldLandlockNeighbouringZone(getMap(),houseID,site.x,site.y,span.x,span.y));
                };
                constexpr int preparedSearchRadius=8;
                Coord prepared=Coord::Invalid();
                int bestDistance=std::numeric_limits<int>::max();
                for (int y=location.y-preparedSearchRadius;y<=location.y+preparedSearchRadius;++y)
                    for (int x=location.x-preparedSearchRadius;x<=location.x+preparedSearchRadius;++x) {
                        const Coord site(x,y);
                        const int distance=std::abs(x-location.x)+std::abs(y-location.y);
                        if (distance>=bestDistance || !usableSite(site)) continue;
                        bestDistance=distance; prepared=site;
                    }
                if (prepared.isValid()) {
                    tracePlacementIssue("placement_replan","prepared_footprint_preferred",prepared);
                    location=prepared;
                } else {
                    // Never a cancellation, and never a perpetual
                    // block: the building goes up on the ground
                    // it was planned for.
                    tracePlacementIssue("placement_unprepared","footprint_not_prepared",location);
                }
            }
        }
        // The footprint rule: our own units on the site are asked off it, as the
        // pre-placement queue asks them off a planned plot, and the building waits
        // for them instead of moving elsewhere. A unit that was ordered off and
        // cannot leave makes the site illegal (placeableAmongOwnUnits), so the
        // next pass plans another one.
        if (location.isValid()) {
            const Coord span = getStructureSize(itemToBePlaced);
            if (ownUnitsOnFootprint(location.x, location.y, span.x, span.y)) {
                doMoveOwnUnitsOffPlot(yardTurn.pConstYard, itemToBePlaced, location.x, location.y);
                if (yardTurn.placeLocations->empty() || yardTurn.placeLocations->front() != location)
                    yardTurn.placeLocations->push_front(location);
                if (!placingSlab && itemToBePlaced != Structure_Road)
                    reservedStructures[planningBuilder] = {itemToBePlaced, location};
                if (pass.emitStatsLog) tracePlacementIssue("placement_deferred", "own_units_leaving_site", location);
                location = Coord::Invalid();
                placementIssueHandled = true;
            }
        }
        if (location.isValid()) {
            traceDecision("placement_request", AITelemetry::Record().set("builder", yardTurn.pConstYard->getObjectID())
                .set("item", itemToBePlaced).set("x", location.x).set("y", location.y));
            const bool placed = doPlaceStructure(yardTurn.pConstYard, location.x, location.y);
            // A road retry keeps its place ahead of the remaining
            // queue; do not accidentally consume the next plan.
            if (!placed && itemToBePlaced == Structure_Road) yardTurn.placeLocations->push_front(location);
            traceDecision("placement_result", AITelemetry::Record().set("builder", planningBuilder)
                .set("item", itemToBePlaced).set("x", location.x).set("y", location.y).set("success", placed));
            if (itemToBePlaced != Structure_Slab1 && itemToBePlaced != Structure_Slab4) {
                if (placed) reservedStructures.erase(planningBuilder);
                else { reservedStructures[planningBuilder] = {itemToBePlaced, location};
                    if (yardTurn.placeLocations->empty()) yardTurn.placeLocations->push_front(location); }
            }
            clearPlacementCache();
            logDebug("PRODUCTION: Placed structure itemID: %d at (%d,%d)", itemToBePlaced, location.x, location.y);
        }
        else if (!placementIssueHandled) {
            if (pass.emitStatsLog) logDebug("PRODUCTION: Holding finished item %d until a legal site is available; search=%s",
                itemToBePlaced, placementScoreDetails[itemToBePlaced].json().c_str());
            if (pass.emitStatsLog) tracePlacementIssue("placement_deferred", "no_dynamic_site", location);
        }
    }
    return false;
}
