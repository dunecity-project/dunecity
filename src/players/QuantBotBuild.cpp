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

using QuantBotInternal::rocketCoverageTier;

int QuantBot::recentFactoryLossCount() const {
    return std::count_if(recentStructureLosses.begin(), recentStructureLosses.end(), [&](const auto& loss) {
        return loss.item == Structure_HeavyFactory && getGameCycleCount()-loss.cycle < MILLI2CYCLES(120000);
    });
}

bool QuantBot::canAddRepairYard(int includingQueued) const {
    return (currentGame && currentGame->isCitySimEnabled())
        || difficulty != Difficulty::Medium
        || includingQueued < initialItemCount[Structure_RepairYard];
}

void QuantBot::firePalaceWeapon(const Palace* pPalace) {
    if (!pPalace->isSpecialWeaponReady()) return;
    const int houseID = getHouse()->getHouseID();
    if (houseID != HOUSE_HARKONNEN && houseID != HOUSE_SARDAUKAR) {
        doSpecialWeapon(pPalace);
        placementCache.clear(); // Palace summons can occupy previously free sites.
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
    planCapitalCandidates(pass);
    chooseCapitalPlan(pass);

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

    traceCapitalOutcome(pass);
    // MULTIPLAYER FIX: Use deterministic timer instead of random
    buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
}

void QuantBot::resetBuildPass() {
    refreshTacticalDanger();
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

    for (int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
        pass.itemCount[i] = getHouse()->getNumItems(i);
    }

    pass.activeHeavyFactoryCount = 0;
    pass.carryallBuildAvailable = false;
    pass.mcvBuildAvailable = false;
    pass.nuclearBuildAvailable = false;
    pass.activeLightFactoryCount = 0;
    pass.activeHighTechFactoryCount = 0;
    pass.ornithopterFactoryCount = 0;
    pass.ornithopterCapableFactoryCount = 0;
    pass.activeRepairYardCount = 0;
    pass.queuedProductionCost = 0;
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
                if (pBuilder->getItemID() == Structure_ConstructionYard && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant)) pass.nuclearBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HighTechFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_Carryall)) pass.carryallBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HighTechFactory && pBuilder->getHealth() > 0
                    && !pBuilder->isUpgrading() && !pBuilder->isOnHold()
                    && campaignAvailableToBuild(pBuilder,Unit_Ornithopter)) ++pass.ornithopterCapableFactoryCount;
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_Harvester)) pass.harvesterFactories.push_back(pBuilder);
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_MCV)) pass.mcvBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->isUpgrading()
                    && !campaignAvailableToBuild(pBuilder,Unit_MCV)) ++pass.mcvUpgradesInProgress;
                if (pBuilder->isUpgrading())
                    pass.queuedProductionCost += std::max(0,pBuilder->getUpgradeCost() - pBuilder->getUpgradeProgress().lround());
                if (pBuilder->getProductionQueueSize() > 0) {
                    int builderQueuedCost = 0;
                    for (const auto& queued : pBuilder->getBuildList()) {
                        pass.itemCount[queued.itemID] += queued.num;
                        builderQueuedCost += queued.num * queued.price;
                        if (QuantBotBuildPolicy::militaryItem(queued.itemID))
                            pass.queuedMilitaryValue += queued.num * data[queued.itemID][houseID].price;
                    }
                    // Starport cargo is paid for when ordered. It still counts
                    // toward the fleet, but must not reserve the same cash again.
                    if (pBuilder->getItemID()!=Structure_StarPort)
                        pass.queuedProductionCost += std::max(0, builderQueuedCost - pBuilder->getProductionProgress().lround());
                    if (pBuilder->getItemID() == Structure_HeavyFactory) {
                        pass.activeHeavyFactoryCount++;
                    }
                    else if (pBuilder->getItemID() == Structure_LightFactory) {
                        ++pass.activeLightFactoryCount;
                    }
                    else if (pBuilder->getItemID() == Structure_HighTechFactory) {
                        pass.activeHighTechFactoryCount++;
                        if (pBuilder->getCurrentProducedItem() == Unit_Ornithopter
                            && !pBuilder->isOnHold() && !pBuilder->isUpgrading()) ++pass.ornithopterFactoryCount;
                    }
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
}

void QuantBot::planBuildBudget(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    pass.money = getHouse()->getCredits();
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
        for (Uint32 item = Unit_FirstID; item <= Unit_LastID; ++item)
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
    // One reading of the remaining field, shared by the space decision here and
    // by the cash decisions further down. A tiny field that supports no worker
    // must not count as viable for one and not the other: "any spice at all"
    // would reserve every corner tile for production the house can never fund.
    // Under city sim this is exactly mapSpiceHarvesterTarget below, because
    // vanillaEconomy is false there; the city divisor is also the stricter one.
    pass.spiceCompetitors = 0;
    for (int h = 0; h < NUM_HOUSES; ++h)  // the Natives harvest nothing
        if (getHouse(h) && !getHouse(h)->isNatives() && getHouse(h)->getNumStructures() > 0) ++pass.spiceCompetitors;
    pass.citySpiceWorkerTarget = QuantBotBuildPolicy::desiredSpiceHarvesters(
        lastCalculatedSpice, pass.spiceCompetitors, harvesterLimit);
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
            if (!data[item][houseID].enabled || data[item][houseID].techLevel>currentGame->techLevel) continue;
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
        for (int item=Unit_FirstID;item<=Unit_LastID;++item)
            if (data[item][houseID].enabled && getHouse()->getChoam().getNumAvailable(item)>=0) return true;
        return false;
    }();
    // A queued refinery supplies a free worker when placed. Reserve that
    // worker alongside factory production and paid imports in every game mode.
    pass.engineHarvesterLimit = getHouse()->getMaxHarvesters();
    pass.actualHarvesters = getHouse()->getNumItems(Unit_Harvester)
        + getHouse()->getNumItems(Unit_RebelHarvester);
    const int pendingRefineries = std::max(0,pass.itemCount[Structure_Refinery]
        - getHouse()->getNumItems(Structure_Refinery));
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
    for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse()) continue;
        const GroundUnit* returnUnit=nullptr;
        if (structure->getItemID()==Structure_RepairYard) {
            const auto* unit=static_cast<const RepairYard*>(structure)->getRepairUnit();
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
    pass.powerRules = getHouse()->isPowerRequired();
    pass.turretPowerRequired = getGameInitSettings().getGameOptions().rocketTurretsNeedPower;
    pass.vanillaEconomy = !pass.citySimEnabled && !pass.powerRules;
    pass.campaignEconomyPush = pass.vanillaEconomy && isCampaignGameType(currentGame->gameType)
        && !builderOnly() && !isCampaignEnemy() && difficulty>=Difficulty::Hard;
    // Buildings pay gradually. Previously the same unspent cash funded more
    // orders every pass even though it was already committed to existing queues.
    pass.clearanceCommitmentShortfall = std::max(0, pass.queuedProductionCost - pass.money);
    pass.money = std::max(0, pass.money - pass.queuedProductionCost);
    pass.economyReserve = pass.vanillaEconomy ? std::max(2000,
        data[Structure_Refinery][houseID].price + 2 * data[Unit_Harvester][houseID].price) : 0;
}

void QuantBot::planCityState(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    // Per-house city stats — CitySimulation now tracks these per player.
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

    if (pass.citySimEnabled && !builderOnly() && gameMode == GameMode::Custom) {
        auto* sim = currentGame->getCitySimulation();
        const auto& hs = sim->getHouseState(houseID);
        const int recentLosses = std::count_if(recentStructureLosses.begin(),recentStructureLosses.end(),[&](const auto& loss) {
            return getGameCycleCount()-loss.cycle < MILLI2CYCLES(180000);
        });
        const int taxIncome = DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(pass.ownTaxBaseEighths,sim->getCityTax(),pass.ownAvgLandValue));
        const int powerCost = getHouse()->isPowerRequired() ? getHouse()->getPowerRequirement()/8 : 0;
        // Include queued services, so their completion cannot silently push
        // recurring costs above the limit between periodic city scans.
        int committedPoliceMilli=0;
        for (Uint32 item:{Structure_PoliceStation,Structure_GunTurret,Structure_RocketTurret})
            committedPoliceMilli+=pass.itemCount[item]*DuneCity::annualPerMinute((DuneCity::getPoliceAnnualCost(item)*1000).lround());
        const int committedPoliceCost=(committedPoliceMilli+999)/1000;
        const int budgetPercent=CityServiceInvestmentPolicy::policingBudgetPercent(pass.money,taxIncome,powerCost,committedPoliceCost);
        const int affordable=CityServiceInvestmentPolicy::affordablePoliceFunding(taxIncome,powerCost,committedPoliceCost,budgetPercent);
        // Necessary cuts keep the strict 33%/50% recurring limit and apply
        // immediately; increases wait out the review cadence and deadband, so
        // the service stops oscillating every build pass.
        const int funding=CityServiceInvestmentPolicy::smoothedPoliceFunding(hs.policeFundingPercent,
            affordable,getGameCycleCount(),lastPoliceBudgetReviewCycle);
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
                .set("reason",funding < hs.policeFundingPercent ? "police_budget_limit" : "police_budget_headroom"));
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
    pass.spiceShare = lastCalculatedSpice / std::max(1, pass.spiceCompetitors);
    // Same competitor count and, under city sim, the same target the space
    // reservation above already used.
    // The campaign economy push no longer plans as the only house on the map: a target of 43-56
    // harvesters left the Hard partner without an army for 12 minutes in missions 20-22.
    pass.mapSpiceHarvesterTarget = pass.vanillaEconomy
        ? DuneCity::vanillaHarvesterTarget(lastCalculatedSpice, style().greedyHarvest ? 1 : pass.spiceCompetitors, harvesterLimit)
        : pass.citySpiceWorkerTarget;
    pass.spiceHarvesterTarget = pass.citySimEnabled ? pass.mapSpiceHarvesterTarget
        : QuantBotBuildPolicy::refineryThroughputHarvesterTarget(
            pass.spiceShare,pass.mapSpiceHarvesterTarget,getHouse()->getNumItems(Structure_Refinery),harvesterLimit);
    pass.fundedHarvesterTarget = pass.citySimEnabled
        ? CityEconomyInvestmentPolicy::factoryHarvesterTarget(pass.spiceHarvesterTarget,harvesterLimit)
        : QuantBotBuildPolicy::fundedSpiceHarvesters(pass.mapSpiceHarvesterTarget,getHouse()->getNumItems(Structure_Refinery));
    pass.busyRefineries = 0; pass.freeRefineries = 0; pass.unbookedRefineries = 0; pass.waitingHarvesters = 0; pass.waitingCargo = 0; pass.blockedRefineryReturns = 0;
    pass.fieldReturnersBlocked = 0;
    auto refineryWaitingForTransport=[](const StructureBase* structure) {
        const auto* refinery=dynamic_cast<const Refinery*>(structure);
        const auto* worker=refinery ? dynamic_cast<const Harvester*>(refinery->getContainedHarvester()) : nullptr;
        return worker && worker->getAmountOfSpice()<=0;
    };
    {
        for(const auto* structure:getStructureList()) if(structure->getOwner()==getHouse()&&structure->acceptsHarvesterDropoff()) {
            if(structure->isHarvesterDropoffFree())++pass.freeRefineries;else ++pass.busyRefineries;
            if(structure->isHarvesterDropoffFree() && structure->getHarvesterDropoffBookings()==0)++pass.unbookedRefineries;
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
        pass.itemCount[Structure_Refinery]-getHouse()->getNumItems(Structure_Refinery),
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
        && currentGame->techLevel >= data[Unit_Carryall][houseID].techLevel
        && currentGame->techLevel >= data[Structure_HighTechFactory][houseID].techLevel
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
        && !builderOnly() && !isCampaignGameType(currentGame->gameType)
        && enemyHouses <= ownTeamHouses && !outnumberedByEstimate());
    pass.openingWorkerFloor = (pass.citySimEnabled && gameMode == GameMode::Custom) || pass.brutalEconomy;
}

bool QuantBot::openingFleetIncomplete(BuildPass& pass) {
    return pass.openingWorkerFloor
        && !getHouse()->isGroundUnitLimitReached()
        && CityEconomyInvestmentPolicy::openingWorkersNeeded(pass.itemCount[Unit_Harvester],pass.fundedHarvesterTarget,pass.brutalEconomy);
}

bool QuantBot::openingWorkersNeeded(BuildPass& pass) {
    return openingFleetIncomplete(pass) && !pass.harvesterFactories.empty();
}

// Keep money for the missing workers when market stock or a delivery is
// temporarily unavailable. Count queued workers, so the reserve releases
// as soon as the fleet is funded rather than waiting for it to arrive.
int QuantBot::harvesterInvestmentReserve(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    if (!isCampaignGameType(currentGame->gameType) || !isAlliedWithHuman()
        || gameMode != GameMode::Custom || builderOnly() || !(pass.vanillaEconomy || pass.citySimEnabled)
        || lastCalculatedSpice <= 0 || getHouse()->isGroundUnitLimitReached()
        || getHouse()->getNumItems(Structure_Refinery) == 0
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
    // Preserve early carryalls while Brutal continues growing its fleet.
    if (pass.brutalEconomy ? CityEconomyInvestmentPolicy::openingWorkersNeeded(pass.itemCount[Unit_Harvester],pass.fundedHarvesterTarget)
                          : openingWorkersNeeded(pass)) return false;
    return QuantBotBuildPolicy::firstTransportNeeded(pass.transportTechAvailable,
        pass.itemCount[Structure_HeavyFactory],getHouse()->getNumItems(Unit_Harvester),
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
        const int power = data[structure->getItemID()][structure->getOriginalHouseID()].power;
        if (power < 0) {
            pass.largestGenerator = std::max(pass.largestGenerator, -power);
            const auto size = getStructureSize(structure->getItemID());
            const int generationCost = data[structure->getItemID()][structure->getOriginalHouseID()].price
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
    if (pass.citySimEnabled) for (Uint32 item=Structure_FirstID; item<=Structure_LastID; ++item)
        pass.committedPowerDemand += std::max(0,pass.itemCount[item]-getHouse()->getNumItems(item))
            * (DuneCity::isCityZoneStructure(item) ? DuneCity::getZonePower(item,3) : std::max(0,data[item][houseID].power));
    pass.zoneGrowthHeadroom = QuantBotBuildPolicy::cityGrowthPowerHeadroom(
        pass.currentZonePower,pass.matureZonePower,projectedPowerDemandGrowth,pass.committedPowerDemand);
    pass.cityPowerReserve = pass.citySimEnabled ? QuantBotBuildPolicy::cityPowerReserve(
        getHouse()->getPowerRequirement(), pass.largestGenerator) + pass.zoneGrowthHeadroom : 0;
    pass.nuclearPlan = pass.citySimEnabled && !needsFirstTransport(pass)
        && pass.itemCount[Structure_HeavyFactory] > 0
        && pass.itemCount[Structure_NuclearPlant] == getHouse()->getNumItems(Structure_NuclearPlant)
        && pass.itemCount[Structure_WindTrap] == getHouse()->getNumItems(Structure_WindTrap)
        && QuantBotBuildPolicy::planNuclearInvestment(getHouse()->getPowerRequirement(),
            getHouse()->getProducedPower(),pass.cityPowerReserve,std::max(1,-data[Structure_WindTrap][houseID].power))
        && pass.nuclearBuildAvailable && findPlaceLocation(Structure_NuclearPlant).isValid();
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
        && !builderOnly() && !isCampaignGameType(currentGame->gameType) && customWave.launched==0
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
    pass.colonisationNeeded = colonisationMcvDue(pass.itemCount[Unit_MCV],pass.yardLimit);
    // Optional construction capacity must not occupy the first heavy factory while a small start
    // is funding its first combat wave. A lost yard or exhausted home rock can still buy an MCV.
    pass.postponeCapacityMcv = (openingCombatBudgetActive() || campaignCombatBudgetActive())
        && pass.itemCount[Structure_ConstructionYard] > 0 && !pass.rockExpansionNeeded && !pass.colonisationNeeded;
    if (pass.postponeCapacityMcv) pass.cityYardTarget = std::min(pass.cityYardTarget, pass.cityConstructionCapacity);
}

void QuantBot::planCityPopulation(BuildPass& pass) {
    // Custom-game city size ceiling for this bot, including shared-house
    // helpers. Campaign games keep their own separate gates.
    pass.populationCeiling = getCityPopulationLimit(getMap().getSizeX()*getMap().getSizeY());
    pass.cityGrowthLimits = pass.populationCeiling > 0
        ? QuantBotCityPolicy::limits(static_cast<int>(difficulty), getMap().getSizeX()*getMap().getSizeY())
        : QuantBotCityPolicy::Limits{};
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
    if(pass.populationCeiling <= 0 || !isStructure(item)) return true;
    if(DuneCity::isCityZoneStructure(item)
        && !QuantBotCityPolicy::allowsZone(pass.cityZonesIncludingQueued,pass.cityGrowthLimits.sharedZoneCap)) return false;
    const int added=QuantBotCityPolicy::displayPopulation(initialCityPopulation(item));
    // Zero-population services, power and roads remain available even in an oversized save.
    return added==0 || QuantBotCityPolicy::allowsPopulation(pass.cityDisplayPopulation,
        pass.cityPendingDisplayPopulation,added,pass.populationCeiling);
}

void QuantBot::planCivicNeedAndTrips(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    pass.campaignPowerBudget = isCampaignGameType(currentGame->gameType)
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

    pass.cityRefineryCatchup = false; pass.cityRefineryOpening = false;
}

AITelemetry::Record QuantBot::decisionState(BuildPass& pass) {
    return AITelemetry::Record().set("credits", getHouse()->getCredits()).set("spendable", pass.money)
        .set("military", pass.militaryValue).set("military_limit", militaryValueLimit)
        .set("military_budget", pass.militaryBudget)
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
        .set("city_mcv_shortfall", pass.citySimEnabled ? std::max(0, pass.cityYardTarget - pass.cityConstructionCapacity) : 0)
        .set("vanilla_yard_target", pass.vanillaEconomy ? DuneCity::vanillaYardTarget(
            pass.money, getHouse()->getNumItems(Unit_Harvester)) : 0)
        .set("mcv_upgrade_in_progress", pass.mcvUpgradesInProgress > 0)
        .set("mcv_upgrades_in_progress", pass.mcvUpgradesInProgress)
        .set("mcv_shortfall", pass.vanillaEconomy ? DuneCity::vanillaMcvShortfall(
            pass.money, getHouse()->getNumItems(Unit_Harvester), pass.itemCount[Structure_ConstructionYard], pass.itemCount[Unit_MCV]) : 0)
        .set("construction_plans_first", pass.citySimEnabled)
        .set("res_demand", pass.ownResValve).set("com_demand", pass.ownComValve).set("ind_demand", pass.ownIndValve)
        .set("res_pop", pass.ownResPop).set("com_pop", pass.ownComPop).set("ind_pop", pass.ownIndPop)
        .set("land_value", pass.ownAvgLandValue).set("spice_remaining", lastCalculatedSpice)
        .set("spice_share", pass.spiceShare).set("map_harvester_target", pass.mapSpiceHarvesterTarget)
        .set("harvester_target", pass.spiceHarvesterTarget).set("refinery_harvester_target",
            QuantBotBuildPolicy::refineryThroughputHarvesterTarget(pass.spiceShare, 0,
                getHouse()->getNumItems(Structure_Refinery), harvesterLimit))
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
    for (Uint32 item=Structure_FirstID;item<=Structure_LastID;++item)
        demand+=std::max(0,pass.itemCount[item]-getHouse()->getNumItems(item))*std::max(0,data[item][houseID].power);
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
            || data[candidate][houseID].techLevel > currentGame->techLevel) {
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
        if (!data[step][houseID].enabled || data[step][houseID].techLevel > currentGame->techLevel) {
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

bool QuantBot::factoryPrefersHarvester(BuildPass& pass, const BuilderBase* factory) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
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
    const int tax = sim->getCityTax();
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
    if ((pass.spiceShare>0 || pass.unloadingBacklog) && campaignAvailableToBuild(builder,Structure_Refinery))
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
    if (selected == zone && zone!=NONE_ID && residential.confidence==0) selected=NONE_ID;
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
                    getHouse()->getKillValue(), getHouse()->getLossValue(), lastCalculatedSpice, getHouse()->getNumItems(Unit_Harvester), harvesterLimit);
            } else {
                // Campaign mode - include initial military value and multiplier
                const QuantBotConfig& config = botConfig();
                const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d (init: %d, mult: %.1fx)  built: %d  kill: %d  loss: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), pass.militaryValue, militaryValueLimit, initialMilitaryValue, diffSettings.militaryValueMultiplier,
                    getHouse()->getUnitBuiltValue(), getHouse()->getKillValue(), getHouse()->getLossValue(), getHouse()->getNumItems(Unit_Harvester), harvesterLimit);
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
            cityHealth.set("unemployment", hs.unemploymentRate).set("tax_rate", sim->getCityTax())
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
                        .set("power_nominal",currentGame->objectData.data[item][structure->getOriginalHouseID()].power)
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
                .set("heavy_busy", pass.activeHeavyFactoryCount).set("light_busy",pass.activeLightFactoryCount).set("repair_busy", pass.activeRepairYardCount)
                .set("built_value", getHouse()->getUnitBuiltValue()).set("kill_value", getHouse()->getKillValue())
                .set("loss_value", getHouse()->getLossValue()));
    }
}

void QuantBot::planUnitMix(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
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
                && builder->getItemID() != Structure_HighTechFactory)) continue;
        for (size_t i=0; i<kSlots; ++i) {
            const bool producible = campaignAvailableToBuild(builder,mixItems[i]);
            available[i] |= producible;
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
            if (!spec.enabled || spec.price <= 0 || spec.techLevel > currentGame->techLevel
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
            && data[item][houseID].techLevel <= currentGame->techLevel;
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
    pass.unitMix = UnitMixPolicy::allocate(scores, defaults, pass.learningUnitMix, pass.vanillaEconomy,
                                                  totalLostValue, priorArmyValue);
    // Test-only play styles (docs/ai-play-styles.md): inert in every real game.
    pass.unitMix = styledUnitMix(pass.unitMix, available);
    // Damage-based learning does not value air deterrence, so protect a minimum
    // mobile anti-air share while a visible hostile wing is active.
    // The floor only exists while hostile aircraft are genuinely visible and
    // launchers are genuinely producible, so it cannot become a standing tax.
    const int observedHostileAircraft = autonomousCustomArmy()
        ? visibleHostileAircraft() : 0;
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


    const int productionCash = std::max(0, pass.money
        - std::max(pass.strategicReserveCost, pass.economyReserve) - (pass.citySimEnabled ? pass.cityWorkingReserve : 0));
    pass.cityWorCashReserve = int64_t(std::max(pass.strategicReserveCost, pass.economyReserve))
        + pass.cityWorkingReserve + std::max(0, data[Structure_ZoneResidential][houseID].price);
    const int fundedArmyValue = QuantBotBuildPolicy::fundedArmyTarget(pass.militaryValue, pass.militaryBudget, productionCash);
    pass.vehiclePlanValue = std::max(0, fundedArmyValue - pass.infantryValue);
    if (pass.emitStatsLog && AITelemetry::log().enabled()) {
        traceDecision("unit_mix", AITelemetry::Record().set("basis", !pass.learningUnitMix ? "configured" : pass.vanillaEconomy ? "lifetime_evidence_weighted_value_per_loss" : "lifetime_value_per_loss")
            .set("tank_bps", (pass.tankPercent * 10000).lround()).set("siege_bps", (pass.siegePercent * 10000).lround())
            .set("special_bps", specialBps).set("devastator_bps", pass.unitMix[3]).set("sonic_tank_bps", pass.unitMix[4])
            .set("deviator_bps", pass.unitMix[5]).set("launcher_bps", (pass.launcherPercent * 10000).lround())
            .set("ornithopter_bps", (pass.ornithopterPercent * 10000).lround()).set("raw_ornithopter_bps", rawOrnithopterBps)
            .set("light_vehicle_percent", lightVehiclePercent).set("light_vehicle_bps", pass.lightVehicleBps)
            .set("trike_bps", pass.unitMix[7]).set("raider_trike_bps", pass.unitMix[8]).set("quad_bps", pass.unitMix[9])
            .set("allocation_types", int(kSlots)).set("tech_level", currentGame->techLevel)
            .set("opening_light_bps", defaults[7]+defaults[8]+defaults[9]).set("total_damage", totalDamage)
            .set("total_reward_milli", totalRewardMilli).set("total_lost_value", totalLostValue)
            .set("launcher_floor_bps", launcherFloorBps)
            .set("launcher_floor_applied", launcherFloorApplied)
            .set("launcher_production_available", launcherProductionAvailable)
            .set("launcher_bps_before_floor", launcherBpsBeforeFloor)
            .set("visible_hostile_aircraft", observedHostileAircraft)
            .set("performance_exponent_milli",1500).set("performance_confidence_bps", performanceConfidenceBps).set("funded_army_value",fundedArmyValue)
            .set("vehicle_plan_value",pass.vehiclePlanValue).set("queued_military_value",pass.queuedMilitaryValue).set("mix_inputs", [&]() {
                AITelemetry::Record inputs;
                for (size_t i=0; i<kSlots; ++i) inputs.set(std::to_string(mixItems[i]),
                    AITelemetry::Record().set("damage", damage[i]).set("reward_milli", rewardMilli[i]).set("kill_bonus_milli", killBonusMilli[i])
                        .set("lost_value", lostValue[i]).set("score", scores[i]).set("allocation_weight",allocationWeights[i])
                        .set("lifetime_reward_milli",performanceHistory.reward[i]).set("lifetime_loss_milli",performanceHistory.loss[i])
                        .set("available", available[i]).set("opening_bps", defaults[i]).set("target_bps", pass.unitMix[i]));
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

// Would ordering one more of \a value still leave infantry inside their share of
// the army? The candidate's own value counts on both sides, so an order that would
// overshoot the share is refused rather than placed and then regretted. Both totals
// already include every queued order and this pass updates them as it accepts more,
// so several producers share one quota instead of each claiming a full share.
bool QuantBot::infantryQuotaAdmits(BuildPass& pass, int value) {
    return value > 0 && (int64_t(pass.infantryValue) + value) * 100
        <= (int64_t(pass.militaryValue) + value) * pass.infantryPercent;
}

// A city raises infantry only as the close-range part of its anti-air answer, so
// nothing may rank, reserve cash for or order them once the observed wing is gone.
bool QuantBot::cityInfantryItem(BuildPass& pass, Uint32 item) {
    return pass.citySimEnabled && (item == Unit_Soldier || item == Unit_Infantry
        || item == Unit_Trooper || item == Unit_Troopers);
}

// Check troop content and its existing prerequisites; the new WOR supplies itself.
// Its real build list still decides which offers can be queued once it exists.
bool QuantBot::cityTroopContentExists(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    for (const Uint32 troop : {Unit_Trooper, Unit_Troopers}) {
        const auto& spec = data[troop][houseID];
        if (!spec.enabled || spec.price <= 0 || spec.techLevel > currentGame->techLevel
            || spec.builder != Structure_WOR || !infantryQuotaAdmits(pass, spec.price)) continue;
        bool prerequisites = true;
        for (int item = Structure_FirstID; item <= Structure_LastID; ++item)
            if (item != Structure_WOR && spec.prerequisiteStructuresSet[item]
                && getHouse()->getNumItems(item) == 0) prerequisites = false;
        if (prerequisites) return true;
    }
    return false;
}

void QuantBot::planCapitalForecast(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    pass.roadMaintenanceAttempted = false;

    pass.defendingEconomy = false;
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->isVisible(getHouse()->getTeamID())
            || unit->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
        const auto* victim=unit->getTarget();
        if (victim && victim->getOwner()==getHouse() && unit->isInWeaponRange(victim)
            && (victim->isAStructure() || victim->getItemID()==Unit_Harvester
                || victim->getItemID()==Unit_RebelHarvester)) pass.defendingEconomy=true;
    }
    pass.forecastTax = pass.citySimEnabled ? DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(pass.ownTaxBaseEighths,
        currentGame->getCitySimulation()->getCityTax(),pass.ownAvgLandValue)) : 0;
    pass.forecastUpkeep = pass.citySimEnabled ? getHouse()->getPowerRequirement()/8
        + DuneCity::annualPerMinute(currentGame->getCitySimulation()->getHouseState(houseID).lastPoliceExpense) : 0;
    // Future workers/bays count toward targets and commitments, but their
    // production is not income until delivered. Reforecast every planning pass.
    const int fleetRate=std::min(pass.actualHarvesters*workerAnnualIncome(pass),
        getHouse()->getNumItems(Structure_Refinery)*pass.bayAnnualIncome);
    pass.forecastSpice = std::min(pass.spiceShare,fleetRate*QuantBotSpendingPolicy::horizonMinutes);
    pass.forecastNetIncome = pass.forecastSpice+(pass.forecastTax-pass.forecastUpkeep)*QuantBotSpendingPolicy::horizonMinutes;
    pass.forecastFunding = std::max(0,pass.money+pass.forecastNetIncome);
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
            int buildtime=data[item][builder->getOriginalHouseID()].buildtime;
            if (pass.citySimEnabled) buildtime=DuneCity::getCityBuildTime(item,buildtime);
            if (port || buildtime<=0) continue;
            const int cost=(FixPoint(offer.price)*speed*CityEconomyInvestmentPolicy::horizonCycles/(buildtime*15)).lround();
            if (item==builder->getCurrentProducedItem() && !builder->isOnHold()
                && !builder->isUpgrading() && !builder->isWaitingToPlace()
                && !getHouse()->isUnitLimitReached(item)) currentBurn=cost/QuantBotSpendingPolicy::horizonMinutes;
            if (!campaignAvailableToBuild(builder,item)) continue;
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
    pass.sharedSpending = !builderOnly() && (!pass.vanillaEconomy || !pass.cashFlow.fundsParallelProduction);
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
    return data[structure->getItemID()][structure->getOriginalHouseID()].price;
}

bool QuantBot::repairPolicyMaintains(BuildPass& pass, const StructureBase* structure) {
    return QuantBotBuildPolicy::repairMaintainsValue(structure->getItemID(),pass.citySimEnabled,
        structure->getMaxHealth(),repairPrice(structure));
}

bool QuantBot::canFundCapacityMcv(BuildPass& pass, int price) {
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
    for (Uint32 item = Structure_FirstID; item <= Structure_LastID; ++item)
        pass.cityStorageCapacityIncludingQueued += std::max(0,
            pass.itemCount[item] - getHouse()->getNumItems(item)) * std::max(0, data[item][houseID].capacity);
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
    return zone!=NONE_ID && purchasePrice(builder,zone)<=budget ? zone : Uint32(NONE_ID);
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
        firstFactoryLegal &= data[item][houseID].enabled && data[item][houseID].techLevel<=currentGame->techLevel;
        pass.firstFactoryCapital+=buildingCapitalCost(pass, item);
        for (int prerequisite=Structure_FirstID;prerequisite<=Structure_LastID;++prerequisite)
            if (data[item][houseID].prerequisiteStructuresSet[prerequisite]) factorySteps.push_back(prerequisite);
    }
    for (int item=Unit_FirstID;item<=Unit_LastID;++item) {
        const auto& info=data[item][houseID];
        if (!info.enabled || info.builder!=Structure_HeavyFactory || info.techLevel>currentGame->techLevel
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
    for (const auto* aircraft:getUnitList()) {
        if (pass.airEngaged) break;
        if (!aircraft->isAFlyingUnit() || !aircraft->isActive() || aircraft->getHealth()<=0
            || !aircraft->canAttack() || !aircraft->getOwner()
            || aircraft->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !aircraft->isVisible(getHouse()->getTeamID())) continue;
        const int reach=std::max(1,aircraft->getWeaponRange())+8;
        for (const auto* structure:getStructureList())
            if (structure->getOwner()==getHouse() && structure->getHealth()>0
                && blockDistance(aircraft->getLocation(),
                    structure->getClosestPoint(aircraft->getLocation()))<=reach) { pass.airEngaged=true; break; }
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
    // Spread core assets can demand an overlap that no legal site reaches, so
    // the shortfall alone never returns to zero. Peaceful pursuit of it then
    // owns every construction slot and its savings for the rest of the game,
    // which is what stopped city growth. Bound the proactive claim by the
    // emplacements the base's own demand justifies; observed aircraft keep the
    // unbounded shortfall, because that is a present loss rather than a plan.
    // Both custom-game economies finish the production core before repeatedly
    // adding cover against aircraft seen elsewhere on the map.
    pass.missingCoreInfrastructure = NONE_ID;
    pass.coreInfrastructureReady = true;
    if (gameMode==GameMode::Custom && !builderOnly())
        for (Uint32 item:{Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard}) {
            if (!data[item][houseID].enabled || data[item][houseID].techLevel>currentGame->techLevel) continue;
            if (getHouse()->getNumItems(item)==0) pass.coreInfrastructureReady=false;
            if (pass.missingCoreInfrastructure==NONE_ID && pass.itemCount[item]==0) pass.missingCoreInfrastructure=item;
        }
    // Storage relief keeps its established city-only core condition: an
    // overflowing bank is lost income now, whatever tech is still missing.
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
    pass.routineRocketsAllowed = (pass.coreInfrastructureReady && !advancedTechPending && !refiningCapacityPending)
        || pass.itemCount[Structure_RocketTurret]<2;
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
    pass.openingSupplierDue = openingFleetIncomplete(pass)
        && pass.itemCount[Structure_Refinery]>=3 && pass.itemCount[Structure_RocketTurret]>=2
        && pass.itemCount[Structure_StarPort]==0 && pass.itemCount[Structure_HeavyFactory]==0;
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

// Earned spice and city income share storage. Make room before optional
// growth can monopolize the yards, and stop once storage reaches the global
// credit ceiling. Count queued capacity so parallel yards do not spam silos.
bool QuantBot::storageExpansionNeeded(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    const int capacity = getHouse()->getCapacity();
    // A bay already ordered brings its own storage, so count that pending
    // capacity instead of cancelling every silo while refineries expand.
    const int pendingCapacity=std::max(0,pass.itemCount[Structure_Refinery]
        -getHouse()->getNumItems(Structure_Refinery))*data[Structure_Refinery][houseID].capacity;
    const int projectedCapacity=capacity+pendingCapacity;
    // Where the tech level has no heavy factory yet (DuneCity campaign levels 1-3), storage
    // is not deferred for one: taxes fill the store and the spice quota waits on it.
    return (getHouse()->getNumItems(Structure_HeavyFactory)>0
            || data[Structure_HeavyFactory][houseID].techLevel>currentGame->techLevel)
        && pass.cityCoreInfrastructureReady
        && capacity > 0 && projectedCapacity < 999999
        && getHouse()->getEarnedCredits() >= projectedCapacity * 0.80_fix
        && pass.itemCount[Structure_Silo] == getHouse()->getNumItems(Structure_Silo);
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

void QuantBot::planCapitalCandidates(BuildPass& pass) {
    int houseID = getHouse()->getHouseID();
    auto& data = currentGame->objectData.data;
    constexpr auto& mixItems = kMixItems;
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
                && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel) {
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
            CapitalCandidate economy;
            economy.builder=builder->getObjectID();
            if (pass.citySimEnabled) {
                economy.item=chooseCityEconomy(pass, builder,false);
                economy.cost=pass.lastEconomyInvestment.cost;
                economy.proceeds=pass.lastEconomyInvestment.proceeds();
                economy.delay=pass.lastEconomyInvestment.delayCycles;
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
                economy.reason=economy.score>0 ? "marginal_income" : "no_marginal_income";
                pass.capitalCandidates.push_back(economy);
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
            const int cityZones=pass.itemCount[Structure_ZoneResidential]+pass.itemCount[Structure_ZoneCommercial]
                +pass.itemCount[Structure_ZoneIndustrial];
            if (botConfig().force.defencePrerequisites && pass.citySimEnabled && getHouse()->hasPower()
                && (pass.airEngaged || (pass.uncoveredCoreAssets>0 && cityZones>=4)) && pass.routineRocketsAllowed
                && data[Structure_RocketTurret][houseID].enabled
                && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel
                && !campaignAvailableToBuild(builder,Structure_RocketTurret)) {
                int uncovered=0;
                if (findCityTurretPlaceLocation(Structure_RocketTurret,&uncovered).isValid() && uncovered>0) {
                    CapitalCandidate unlock;
                    unlock.builder=builder->getObjectID();
                    unlock.score=pass.airEngaged ? 3000 : 2600;
                    unlock.reason=pass.airEngaged ? "air_coverage" : "core_coverage";
                    unlock.urgent=pass.airEngaged;
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
                                unlock.site=findPlaceLocation(missing);
                                unlock.price=data[missing][houseID].price;unlock.cost=buildingCapitalCost(pass, missing);
                                break;
                            }
                            step=missing;
                        }
                    }
                    if (unlock.price>0 && (unlock.kind==std::string("defence_upgrade") || unlock.site.isValid()))
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
                if (actual==0) capacity.reason="bootstrap_managed_separately";
                else if (factory==Structure_LightFactory && int64_t(pass.lightVehicleValue)*10000
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
            if (!ready) worker.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_Harvester)) worker.reason="tech_unavailable";
            else if (port && getHouse()->getChoam().getNumAvailable(Unit_Harvester)<=0) worker.reason="sold_out";
            else if (getHouse()->isGroundUnitLimitReached() || (pass.engineHarvesterLimit>0
                && pass.itemCount[Unit_Harvester]>=pass.engineHarvesterLimit)) worker.reason="unit_limit";
            else if (pass.itemCount[Unit_Harvester]>=pass.desiredWorkers && harvesterInvestmentReserve(pass)==0) worker.reason="fleet_target";
            else if (worker.proceeds<=worker.cost && harvesterInvestmentReserve(pass)==0) worker.reason="spice_or_bay_bottleneck";
            else if (!port && harvesterInvestmentReserve(pass)==0 && !factoryPrefersHarvester(pass, builder)) worker.reason="army_balance";
            else {
                worker.score=QuantBotSpendingPolicy::economyScore(worker.proceeds,worker.cost);
                if ((harvesterInvestmentReserve(pass)>0 || openingFleetIncomplete(pass)) && !pass.defendingEconomy)
                    worker.score=std::max(worker.score,5000);
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
            else if ((getHouse()->getNumItems(Unit_Harvester)==0 || getHouse()->getNumItems(Structure_Refinery)==0)
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
        // Give an imported colonist the same shared cash protection as a
        // factory-built one; otherwise repeated bargains consume its savings.
        if (port && pass.colonisationNeeded && !pass.mcvBuildAvailable && pass.itemCount[Unit_MCV]==0) {
            CapitalCandidate colonist;
            colonist.builder=builder->getObjectID();colonist.item=Unit_MCV;
            colonist.kind="construction";colonist.price=purchasePrice(builder,Unit_MCV);
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
            else if (!canFundCapacityMcv(pass, mcv.price)) mcv.reason="existing_production_runway";
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
            else if (!port && (bps<=0 || deficit<=0)) reason="mix_satisfied";
            // City infantry answer an observed wing and claim the aggregate infantry
            // share, not a full share each. Without this a finished air raid would go
            // on reserving cash for troops nobody is going to order.
            else if (cityInfantryItem(pass, item) && !pass.cityAntiAirInfantryDemand) reason="no_observed_air";
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
        && candidate.kind!=std::string("expansion_upgrade") && candidate.kind!=std::string("defence_upgrade")) {
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
    }
}

void QuantBot::chooseCapitalPlan(BuildPass& pass) {
    planningBuilder=NONE_ID; clearPlacementCache(false,true);
    pass.capitalChoice = -1;
    for (size_t i=0;i<pass.capitalCandidates.size();++i)
        if (pass.capitalCandidates[i].score>0 && (pass.capitalChoice<0
            || pass.capitalCandidates[i].score>pass.capitalCandidates[pass.capitalChoice].score)) pass.capitalChoice=int(i);
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
            || pass.capitalCandidates[pass.capitalChoice].kind == std::string("defence_upgrade")
            || pass.capitalCandidates[pass.capitalChoice].kind == std::string("defence_prerequisite"));
    pass.cityGrowthProtected = false;
    // With multiple yards, assign one to growth alongside routine investment.
    // Crime protection keeps the winning allocation; growth resumes after its
    // order is accepted. A lone opening yard retains its tech path.
    Uint32 dedicatedCityYard=NONE_ID;
    if (pass.sharedSpending && pass.citySimEnabled && pass.cityYards>1 && !pass.protectionCapital) {
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
    if (!pass.cityGrowthProtected && pass.citySimEnabled
        && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()>=24
        && pass.capitalChoice>=0 && pass.capitalCandidates[pass.capitalChoice].kind==std::string("military")) {
        int growth=-1;
        for (size_t i=0;i<pass.capitalCandidates.size();++i)
            if (DuneCity::isCityZoneStructure(pass.capitalCandidates[i].item) && pass.capitalCandidates[i].score>0
                && (growth<0 || pass.capitalCandidates[i].score>pass.capitalCandidates[growth].score)) growth=int(i);
        if (growth>=0) {pass.capitalChoice=growth;pass.cityGrowthProtected=true;}
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
        pass.capitalDecision=traceDecision("capital_plan",AITelemetry::Record().set("policy_version",2)
            .set("mode",pass.citySimEnabled ? "dunecity" : "vanilla").set("campaign",isCampaignGameType(currentGame->gameType))
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
            .set("unit_options",pass.capitalUnitOptions).set("candidates",options).set("selected",pass.capitalChoice)
            .set("reason",pass.cityGrowthProtected ? "protect_demanded_city_growth" : !pass.sharedSpending ? "funded_parallel_production" : pass.capitalChoice<0
                ? "no_useful_available_purchase" : "highest_marginal_priority"));
    }
}

bool QuantBot::capitalPending(BuildPass& pass) {
    return pass.capitalChoice>=0 && !pass.capitalConsumed
        && pass.itemCount[pass.capitalCandidates[pass.capitalChoice].item]==pass.capitalInitialCount;
}

int QuantBot::capitalReserve(BuildPass& pass, Uint32 builder) {
    return capitalPending(pass) ? QuantBotSpendingPolicy::reserveForOther(INT_MAX,
        pass.capitalCandidates[pass.capitalChoice].price+pass.capitalCandidates[pass.capitalChoice].foundationCost,pass.capitalCandidates[pass.capitalChoice].builder==builder,false,
        !getHouse()->hasPower() || pass.itemCount[Structure_ConstructionYard]==0) : 0;
}

void QuantBot::planBuildOrder(BuildPass& pass) {
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse()) {
            int priority=QuantBotBuildPolicy::productionPlanningPriority(pass.citySimEnabled,
                structure->getItemID(), structure->getItemID() == Structure_ConstructionYard
                    && static_cast<const ConstructionYard*>(structure)->isWaitingToPlace(), needsFirstTransport(pass));
            // Compound the helper's income before optional construction/army
            // spending. The port still checks funds, stock and existing orders.
            if (pass.campaignEconomyPush && pass.itemCount[Unit_Harvester]<pass.spiceHarvesterTarget) {
                if (structure->getItemID()==Structure_StarPort) priority=6;
                else if (structure->getItemID()==Structure_HeavyFactory) priority=5;
            }
            if (pass.capitalChoice>=0 && pass.capitalCandidates[pass.capitalChoice].item==Unit_Carryall
                && pass.capitalCandidates[pass.capitalChoice].builder==structure->getObjectID()
                && getHouse()->hasPower()) priority=7;
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
        builderPlaceLocations[yard->getObjectID()].emplace_back(site.first,site.second);
        doProduceItem(yard,Structure_Road);
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
