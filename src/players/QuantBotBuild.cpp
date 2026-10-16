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

// QuantBot build and economy: the per-yard build pass, repair-yard and factory-loss gates,
// production multipliers and city building management.

#include "QuantBotInternal.h"

using QuantBotInternal::rocketCoverageTier;
using QuantBotInternal::cityRoadImpact;
using QuantBotInternal::wouldLandlockNeighbouringZone;

int QuantBot::recentFactoryLossCount() const {
    return std::count_if(recentStructureLosses.begin(), recentStructureLosses.end(), [&](const auto& loss) {
        return loss.item == Structure_HeavyFactory && getGameCycleCount()-loss.cycle < MILLI2CYCLES(120000);
    });
}

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


bool QuantBot::canAddRepairYard(int includingQueued) const {
    return (currentGame && currentGame->isCitySimEnabled())
        || difficulty != Difficulty::Medium
        || includingQueued < initialItemCount[Structure_RepairYard];
}

void QuantBot::build(int militaryValue) {
    AITelemetry::PerformanceScope perfScope("ai.build", getGameCycleCount(), getHouse()->getHouseID());
    // Shares one owned-tile field across this call's placement searches. The
    // guard clears it on every exit, so it never outlives this invocation.
    BuildAnchorScope anchorScope(*this);
    AITelemetry::PerformanceScope phaseScope("ai.build.evaluate",getGameCycleCount(),getHouse()->getHouseID());
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

	int houseID = getHouse()->getHouseID();
	auto& data = currentGame->objectData.data;

	int itemCount[Num_ItemID];
	for (int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
		itemCount[i] = getHouse()->getNumItems(i);
	}

	int activeHeavyFactoryCount = 0;
    std::vector<const BuilderBase*> harvesterFactories;
    bool carryallBuildAvailable = false;
    bool mcvBuildAvailable = false;
    bool nuclearBuildAvailable = false;
    int activeLightFactoryCount = 0;
	int activeHighTechFactoryCount = 0;
    int ornithopterFactoryCount = 0;
    int ornithopterCapableFactoryCount = 0;
	int activeRepairYardCount = 0;
    int queuedProductionCost = 0;
    int queuedMilitaryValue = 0;
    int mcvUpgradesInProgress = 0;

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
                    && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant)) nuclearBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HighTechFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_Carryall)) carryallBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HighTechFactory && pBuilder->getHealth() > 0
                    && !pBuilder->isUpgrading() && !pBuilder->isOnHold()
                    && campaignAvailableToBuild(pBuilder,Unit_Ornithopter)) ++ornithopterCapableFactoryCount;
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_Harvester)) harvesterFactories.push_back(pBuilder);
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->getHealth() > 0
                    && campaignAvailableToBuild(pBuilder,Unit_MCV)) mcvBuildAvailable = true;
                if (pBuilder->getItemID() == Structure_HeavyFactory && pBuilder->isUpgrading()
                    && !campaignAvailableToBuild(pBuilder,Unit_MCV)) ++mcvUpgradesInProgress;
                if (pBuilder->isUpgrading())
                    queuedProductionCost += std::max(0,pBuilder->getUpgradeCost() - pBuilder->getUpgradeProgress().lround());
				if (pBuilder->getProductionQueueSize() > 0) {
                    int builderQueuedCost = 0;
					for (const auto& queued : pBuilder->getBuildList()) {
						itemCount[queued.itemID] += queued.num;
                        builderQueuedCost += queued.num * queued.price;
                        if (QuantBotBuildPolicy::militaryItem(queued.itemID))
                            queuedMilitaryValue += queued.num * data[queued.itemID][houseID].price;
					}
                    // Starport cargo is paid for when ordered. It still counts
                    // toward the fleet, but must not reserve the same cash again.
                    if (pBuilder->getItemID()!=Structure_StarPort)
                        queuedProductionCost += std::max(0, builderQueuedCost - pBuilder->getProductionProgress().lround());
                    if (pBuilder->getItemID() == Structure_HeavyFactory) {
						activeHeavyFactoryCount++;
					}
					else if (pBuilder->getItemID() == Structure_LightFactory) {
                        ++activeLightFactoryCount;
                    }
                    else if (pBuilder->getItemID() == Structure_HighTechFactory) {
						activeHighTechFactoryCount++;
                        if (pBuilder->getCurrentProducedItem() == Unit_Ornithopter
                            && !pBuilder->isOnHold() && !pBuilder->isUpgrading()) ++ornithopterFactoryCount;
					}
				}
			}
			else if (pStructure->getItemID() == Structure_RepairYard) {
				const RepairYard* pRepairYard = static_cast<const RepairYard*>(pStructure);
				if (!pRepairYard->isFree()) {
					activeRepairYardCount++;
				}

			}

			// Unit deployment position - disabled, just deploy units normally
			// Production buildings will deploy units at their default position
		}


	}

	int money = getHouse()->getCredits();
    militaryValue += queuedMilitaryValue;
    // Effective military planning budget for this pass, computed once now that the
    // queue-inclusive army value and the cash are known. It equals the configured
    // militaryValueLimit unless this is Brutal with an explicit unit-count override,
    // in which case it is rolling headroom above the committed value so production
    // only stops at the engine's unit-count limits, reserves, costs and prerequisites.
    // The serialized militaryValueLimit is never modified.
    const bool militaryBudgetOverridden = overridesMilitaryValueCap();
    int largestMilitaryUnitValue = 0;
    if (militaryBudgetOverridden)
        for (Uint32 item = Unit_FirstID; item <= Unit_LastID; ++item)
            if (QuantBotBuildPolicy::militaryItem(item) && data[item][houseID].enabled)
                largestMilitaryUnitValue = std::max(largestMilitaryUnitValue, data[item][houseID].price);
    const int militaryBudget = planningMilitaryBudget(militaryValue, money, largestMilitaryUnitValue);
	const bool citySimEnabled = currentGame && currentGame->isCitySimEnabled();
    // One reading of the remaining field, shared by the space decision here and
    // by the cash decisions further down. A tiny field that supports no worker
    // must not count as viable for one and not the other: "any spice at all"
    // would reserve every corner tile for production the house can never fund.
    // Under city sim this is exactly mapSpiceHarvesterTarget below, because
    // vanillaEconomy is false there; the city divisor is also the stricter one.
    int spiceCompetitors = 0;
    for (int h = 0; h < NUM_HOUSES; ++h)
        if (getHouse(h) && getHouse(h)->getNumStructures() > 0) ++spiceCompetitors;
    const int citySpiceWorkerTarget = QuantBotBuildPolicy::desiredSpiceHarvesters(
        lastCalculatedSpice, spiceCompetitors, harvesterLimit);
    // Space is reserved on the same terms as cash. A house whose field supports
    // no worker and which has no tax base yet cannot pay for either footprint,
    // and on a cramped slot the two reserved areas plus their access margin are
    // the whole buildable surface: the lots that would earn that money get
    // "no_site" instead. On the captured 192x192 start that cost two houses
    // every zone site for 43,000 cycles. Reserve production space once the city
    // funds it.
    const bool cityFundsProduction = !citySimEnabled || citySpiceWorkerTarget > 0
        || (getHouse()->getCapacity()>0 && currentGame->getCitySimulation()
            && currentGame->getCitySimulation()->getHouseState(houseID).taxBaseEighths > 0);
    if (citySimEnabled && cityFundsProduction && itemCount[Structure_ConstructionYard]>0) {
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
    const bool starportMarketAvailable = [&] {
        for (int item=Unit_FirstID;item<=Unit_LastID;++item)
            if (data[item][houseID].enabled && getHouse()->getChoam().getNumAvailable(item)>=0) return true;
        return false;
    }();
    // A queued refinery supplies a free worker when placed. Reserve that
    // worker alongside factory production and paid imports in every game mode.
    const int engineHarvesterLimit = getHouse()->getMaxHarvesters();
    const int actualHarvesters = getHouse()->getNumItems(Unit_Harvester)
        + getHouse()->getNumItems(Unit_RebelHarvester);
    const int pendingRefineries = std::max(0,itemCount[Structure_Refinery]
        - getHouse()->getNumItems(Structure_Refinery));
    itemCount[Unit_Harvester] += engineHarvesterLimit > 0
        ? std::min(pendingRefineries, std::max(0,engineHarvesterLimit
            - itemCount[Unit_Harvester] - itemCount[Unit_RebelHarvester]))
        : pendingRefineries;
    int combatVehicles=0, busyTransports=0, busyRepairBays=0;
    std::set<Uint32> pickupQueue, repairQueue;
    for (const auto* unit:getUnitList()) {
        if (unit->getOwner()!=getHouse() || unit->getHealth()<=0) continue;
        if (unit->getItemID()==Unit_Carryall && static_cast<const Carryall*>(unit)->isBooked()) ++busyTransports;
        if (!unit->isAGroundUnit() || unit->isInfantry() || unit->getItemID()==Unit_Saboteur) continue;
        const auto* ground=static_cast<const GroundUnit*>(unit);
        if (unit->canAttack() && unit->getItemID()!=Unit_Sandworm) ++combatVehicles;
        if (unit->getAttackMode()==CARRYALLREQUESTED && !ground->hasBookedCarrier()) pickupQueue.insert(unit->getObjectID());
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
                ++busyRepairBays;repairQueue.insert(unit->getObjectID());
            } else returnUnit=dynamic_cast<const GroundUnit*>(unit);
        } else if (structure->getItemID()==Structure_Refinery) {
            const auto* unit=dynamic_cast<const Harvester*>(static_cast<const Refinery*>(structure)->getContainedHarvester());
            if (unit && unit->getAmountOfSpice()<=0) returnUnit=unit;
        }
        // A finished job waiting for its return flight is transport pressure,
        // not a reason to buy another repair/unloading bay.
        if (returnUnit && returnUnit->getGuardPoint().isValid() && !returnUnit->hasBookedCarrier()
            && blockDistance(structure->getLocation(),returnUnit->getGuardPoint())>=MIN_CARRYALL_LIFT_DISTANCE)
            pickupQueue.insert(returnUnit->getObjectID());
    }
    const int waitingRepairVehicles=std::max(0,int(repairQueue.size())-busyRepairBays);
    const int repairBaseline=QuantBotBuildPolicy::baselineRepairYards(combatVehicles,actualHarvesters);
    const int repairTarget=QuantBotBuildPolicy::supportQueueTarget(repairBaseline,
        getHouse()->getNumItems(Structure_RepairYard),itemCount[Structure_RepairYard],busyRepairBays,waitingRepairVehicles);
    const bool powerRules = getHouse()->isPowerRequired();
    const bool turretPowerRequired = getGameInitSettings().getGameOptions().rocketTurretsNeedPower;
    const bool vanillaEconomy = !citySimEnabled && !powerRules;
    const bool campaignEconomyPush = vanillaEconomy && isCampaignGameType(currentGame->gameType)
        && !supportMode && !isCampaignEnemy() && difficulty>=Difficulty::Hard;
    // Buildings pay gradually. Previously the same unspent cash funded more
    // orders every pass even though it was already committed to existing queues.
    const int clearanceCommitmentShortfall = std::max(0, queuedProductionCost - money);
    money = std::max(0, money - queuedProductionCost);
    const int economyReserve = vanillaEconomy ? std::max(2000,
        data[Structure_Refinery][houseID].price + 2 * data[Unit_Harvester][houseID].price) : 0;

	// Per-house city stats — CitySimulation now tracks these per player.
	int ownResPop = 0, ownComPop = 0, ownIndPop = 0, ownTotalPop = 0, ownTaxBaseEighths = 0;
	int ownAvgLandValue = 0;
	int16_t ownResValve = 0, ownComValve = 0, ownIndValve = 0;

	if (citySimEnabled) {
		auto* citySim = currentGame->getCitySimulation();
		if (citySim) {
			const auto& hs = citySim->getHouseState(getHouse()->getHouseID());
			ownResPop = hs.resPop;
			ownComPop = hs.comPop;
			ownIndPop = hs.indPop;
			ownTotalPop = hs.getTotalPop();
            ownTaxBaseEighths = hs.taxBaseEighths;
			ownAvgLandValue = hs.avgLandValue;
			ownResValve = hs.resValve;
			ownComValve = hs.comValve;
			ownIndValve = hs.indValve;
		}
	}

    if (citySimEnabled && !supportMode && gameMode == GameMode::Custom) {
        auto* sim = currentGame->getCitySimulation();
        const auto& hs = sim->getHouseState(houseID);
        const int recentLosses = std::count_if(recentStructureLosses.begin(),recentStructureLosses.end(),[&](const auto& loss) {
            return getGameCycleCount()-loss.cycle < MILLI2CYCLES(180000);
        });
        const int taxIncome = DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths,sim->getCityTax(),ownAvgLandValue);
        const int powerCost = getHouse()->isPowerRequired() ? getHouse()->getPowerRequirement()/8 : 0;
        // Include queued services, so their completion cannot silently push
        // recurring costs above the limit between periodic city scans.
        int committedPoliceMilli=0;
        for (Uint32 item:{Structure_PoliceStation,Structure_GunTurret,Structure_RocketTurret})
            committedPoliceMilli+=itemCount[item]*(DuneCity::getPoliceAnnualCost(item)*1000).lround();
        const int committedPoliceCost=(committedPoliceMilli+999)/1000;
        const int budgetPercent=CityServiceInvestmentPolicy::policingBudgetPercent(money,taxIncome,powerCost,committedPoliceCost);
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
                .set("nominal_cost",hs.nominalPoliceCost).set("cash",money).set("recent_losses",recentLosses)
                .set("budget_percent",budgetPercent).set("committed_police_cost",committedPoliceCost)
                .set("reason",funding < hs.policeFundingPercent ? "police_budget_limit" : "police_budget_headroom"));
        }
    }

	// Strategic structures must eventually outrank repeatable choices such as
	// factories, zones, and reactive turrets. These timers stay runtime-only so
	// loading an older save starts a fresh bounded wait without changing save data.
	const Uint32 currentBuildCycle = getGameCycleCount();
    // Forecast two minutes from the last thirty seconds of actual demand.
    // Sample on simulation cycles and save it so peers/reloads agree. Falling
    // or flat demand removes the measured trend; latent zone load is reserved below.
    if (citySimEnabled) {
        const int required = getHouse()->getPowerRequirement();
        if (powerDemandSampleCycle == 0) {
            powerDemandSampleCycle = currentBuildCycle;
            powerDemandSample = required;
        } else if (currentBuildCycle-powerDemandSampleCycle >= MILLI2CYCLES(30000)) {
            projectedPowerDemandGrowth = QuantBotBuildPolicy::projectedPowerGrowth(
                powerDemandSample,required,currentBuildCycle-powerDemandSampleCycle,MILLI2CYCLES(120000));
            powerDemandSampleCycle = currentBuildCycle;
            powerDemandSample = required;
        }
    }
	const Uint32 noEligibilityCycle = std::numeric_limits<Uint32>::max();
	bool anyConstructionYardCanBuildIX = false;
    bool anyConstructionYardCanBuildRefinery = false;
	bool anyConstructionYardCanBuildPalace = false;
	for (const StructureBase* pStructure : getStructureList()) {
		if (pStructure->getOwner() != getHouse()
			|| pStructure->getItemID() != Structure_ConstructionYard
			|| !pStructure->isABuilder()) {
			continue;
		}

		const BuilderBase* pConstructionYard = static_cast<const BuilderBase*>(pStructure);
		anyConstructionYardCanBuildIX |= pConstructionYard->isAvailableToBuild(Structure_IX);
        anyConstructionYardCanBuildRefinery |= pConstructionYard->isAvailableToBuild(Structure_Refinery);
		anyConstructionYardCanBuildPalace |= pConstructionYard->isAvailableToBuild(Structure_Palace);
	}

	const bool customStrategicPlanning = gameMode == GameMode::Custom && !supportMode;
	const bool stablePower = getHouse()->hasPower();
	// itemCount already includes every queued and unplaced palace from all
	// builders, so parallel construction yards cannot exceed the target.
	const int palaceTarget = QuantBotBuildPolicy::palaceTarget(
		getGameInitSettings().getGameOptions().onlyOnePalace, citySimEnabled,
		ownTotalPop * DuneCity::CitySimulation::kPopDisplayMultiplier,
		static_cast<int>(difficulty));
	const bool palaceAllowedNow = itemCount[Structure_Palace] < palaceTarget;
	const bool ixEligible = customStrategicPlanning
		&& itemCount[Structure_IX] == 0
		&& itemCount[Structure_HeavyFactory] > 0
		&& itemCount[Structure_HighTechFactory] > 0
		&& itemCount[Structure_RepairYard] > 0
		&& anyConstructionYardCanBuildIX;
	const bool palaceEligible = customStrategicPlanning
		&& palaceAllowedNow
		&& itemCount[Structure_HeavyFactory] > 0
		&& itemCount[Structure_LightFactory] > 0
		&& anyConstructionYardCanBuildPalace;

	auto updateEligibilityTimer = [&](bool eligible, Uint32& eligibleSinceCycle) {
		if (!eligible) {
			eligibleSinceCycle = noEligibilityCycle;
		} else if (eligibleSinceCycle == noEligibilityCycle) {
			eligibleSinceCycle = currentBuildCycle;
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

	const bool ixOverdue = ixEligible
		&& currentBuildCycle - ixEligibleSinceCycle >= MILLI2CYCLES(ixWaitMs);
	const bool palaceOverdue = palaceEligible
		&& currentBuildCycle - palaceEligibleSinceCycle >= MILLI2CYCLES(palaceWaitMs);
	Uint32 strategicReserveItem = NONE_ID;
	if (stablePower && ixOverdue) {
		strategicReserveItem = Structure_IX;
	} else if (stablePower && palaceOverdue) {
		strategicReserveItem = Structure_Palace;
	}
	// Reserve only the cost of a feasible order. An unplaceable structure must
	// not freeze production indefinitely, and excess funds remain spendable.
	if (strategicReserveItem != NONE_ID && !findPlaceLocation(strategicReserveItem).isValid()) {
		strategicReserveItem = NONE_ID;
	}
	int strategicReserveCost = strategicReserveItem == NONE_ID ? 0
		: data[strategicReserveItem][houseID].price;

    const int spiceShare = lastCalculatedSpice / std::max(1, spiceCompetitors);
    // Same competitor count and, under city sim, the same target the space
    // reservation above already used.
    const int mapSpiceHarvesterTarget = vanillaEconomy
        ? DuneCity::vanillaHarvesterTarget(lastCalculatedSpice, campaignEconomyPush ? 1 : spiceCompetitors, harvesterLimit)
        : citySpiceWorkerTarget;
    const int spiceHarvesterTarget = citySimEnabled ? mapSpiceHarvesterTarget
        : QuantBotBuildPolicy::refineryThroughputHarvesterTarget(
            spiceShare,mapSpiceHarvesterTarget,getHouse()->getNumItems(Structure_Refinery),harvesterLimit);
    const int fundedHarvesterTarget = citySimEnabled
        ? CityEconomyInvestmentPolicy::factoryHarvesterTarget(spiceHarvesterTarget,harvesterLimit)
        : QuantBotBuildPolicy::fundedSpiceHarvesters(mapSpiceHarvesterTarget,getHouse()->getNumItems(Structure_Refinery));
    int busyRefineries=0,freeRefineries=0,unbookedRefineries=0,waitingHarvesters=0,waitingCargo=0,blockedRefineryReturns=0;
    int fieldReturnersBlocked=0;
    auto refineryWaitingForTransport=[](const StructureBase* structure) {
        const auto* refinery=dynamic_cast<const Refinery*>(structure);
        const auto* worker=refinery ? dynamic_cast<const Harvester*>(refinery->getContainedHarvester()) : nullptr;
        return worker && worker->getAmountOfSpice()<=0;
    };
    AITelemetry::Record refineryQueues;
    {
        for(const auto* structure:getStructureList()) if(structure->getOwner()==getHouse()&&structure->acceptsHarvesterDropoff()) {
            if(structure->isHarvesterDropoffFree())++freeRefineries;else ++busyRefineries;
            if(structure->isHarvesterDropoffFree() && structure->getHarvesterDropoffBookings()==0)++unbookedRefineries;
            blockedRefineryReturns+=refineryWaitingForTransport(structure);
            refineryQueues.set(std::to_string(structure->getObjectID()),AITelemetry::Record()
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
                ++waitingHarvesters;
                fieldReturnersBlocked+=blockDistance(harvester->getLocation(),target->getClosestPoint(harvester->getLocation()))
                    >=MIN_CARRYALL_LIFT_DISTANCE;
                waitingCargo+=std::min(int(HARVESTERMAXSPICE),harvester->getAmountOfSpice().lround());
            }
        }
    }
    if(waitingHarvesters<unbookedRefineries+2)refineryQueueSince=std::numeric_limits<Uint32>::max();
    else if(refineryQueueSince==std::numeric_limits<Uint32>::max())refineryQueueSince=getGameCycleCount();
    const bool unloadingBacklog=CityEconomyInvestmentPolicy::unloadingQueueNeedsBay(waitingHarvesters,unbookedRefineries,
        itemCount[Structure_Refinery]-getHouse()->getNumItems(Structure_Refinery),
        refineryQueueSince!=std::numeric_limits<Uint32>::max()&&getGameCycleCount()-refineryQueueSince>=MILLI2CYCLES(10000));
    if(citySimEnabled&&(rockSurveyCycle==std::numeric_limits<Uint32>::max()
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
    const bool rockExpansionNeeded=citySimEnabled&&colonySiteOpen()
        && (availableBaseRock<48 || (unloadingBacklog&&!findPlaceLocation(Structure_Refinery).isValid()));
    const int cityWorkingReserve = data[Unit_Tank][houseID].price
        + (itemCount[Unit_Harvester] < fundedHarvesterTarget ? data[Unit_Harvester][houseID].price : 0)
        + (!citySimEnabled && itemCount[Structure_Refinery] < QuantBotBuildPolicy::desiredSpiceRefineries(
            mapSpiceHarvesterTarget,itemCount[Unit_Harvester]) ? data[Structure_Refinery][houseID].price : 0);
    bool orderedSpiceHarvester = false;
    const bool transportTechAvailable = gameMode == GameMode::Custom && lastCalculatedSpice > 0
        && data[Unit_Carryall][houseID].enabled && data[Structure_HighTechFactory][houseID].enabled
        && currentGame->techLevel >= data[Unit_Carryall][houseID].techLevel
        && currentGame->techLevel >= data[Structure_HighTechFactory][houseID].techLevel
        && !getHouse()->isAirUnitLimitReached();
    const bool brutalCityEconomy = citySimEnabled && gameMode == GameMode::Custom
        && difficulty == Difficulty::Brutal;
    auto openingFleetIncomplete = [&]() {
        return citySimEnabled && gameMode == GameMode::Custom
            && !getHouse()->isGroundUnitLimitReached()
            && CityEconomyInvestmentPolicy::openingWorkersNeeded(itemCount[Unit_Harvester],fundedHarvesterTarget,brutalCityEconomy);
    };
    auto openingWorkersNeeded = [&]() {
        return openingFleetIncomplete() && !harvesterFactories.empty();
    };
    // Keep money for the missing workers when market stock or a delivery is
    // temporarily unavailable. Count queued workers, so the reserve releases
    // as soon as the fleet is funded rather than waiting for it to arrive.
    auto harvesterInvestmentReserve = [&]() {
        if (!isCampaignGameType(currentGame->gameType) || !isAlliedWithHuman()
            || gameMode != GameMode::Custom || supportMode || !(vanillaEconomy || citySimEnabled)
            || lastCalculatedSpice <= 0 || getHouse()->isGroundUnitLimitReached()
            || getHouse()->getNumItems(Structure_Refinery) == 0
            || (harvesterFactories.empty() && getHouse()->getNumItems(Structure_StarPort) == 0)) return 0;
        int target = vanillaEconomy ? spiceHarvesterTarget : fundedHarvesterTarget;
        const auto& market = getHouse()->getChoam();
        if (getHouse()->getNumItems(Structure_StarPort) > 0 && isAlliedWithHuman()
            && market.getPrice(Unit_Harvester) > 0
            && market.getPrice(Unit_Harvester) < data[Unit_Harvester][houseID].price
            && getHouse()->getMaxHarvesters() > 0) target = getHouse()->getMaxHarvesters();
        if (getHouse()->getMaxHarvesters() > 0) target = std::min(target, getHouse()->getMaxHarvesters());
        return std::max(0, target-itemCount[Unit_Harvester]) * data[Unit_Harvester][houseID].price;
    };
    auto needsFirstTransport = [&]() {
        // Preserve early carryalls while Brutal continues growing its fleet.
        if (brutalCityEconomy ? CityEconomyInvestmentPolicy::openingWorkersNeeded(itemCount[Unit_Harvester],fundedHarvesterTarget)
                              : openingWorkersNeeded()) return false;
        return QuantBotBuildPolicy::firstTransportNeeded(transportTechAvailable,
            itemCount[Structure_HeavyFactory],getHouse()->getNumItems(Unit_Harvester),
            itemCount[Unit_Carryall]);
    };

    const auto windSize = getStructureSize(Structure_WindTrap);
    int generationCostPerThousand = 1000 * (data[Structure_WindTrap][houseID].price
        + windSize.x*windSize.y*data[Structure_Slab1][houseID].price)
        / std::max(1,-data[Structure_WindTrap][houseID].power);
    int largestGenerator = 0;
    int currentZonePower = 0, matureZonePower = 0, committedPowerDemand = 0;
    std::array<int,3> developingZones{};
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse()) continue;
        const int power = data[structure->getItemID()][structure->getOriginalHouseID()].power;
        if (power < 0) {
            largestGenerator = std::max(largestGenerator, -power);
            const auto size = getStructureSize(structure->getItemID());
            const int generationCost = data[structure->getItemID()][structure->getOriginalHouseID()].price
                + size.x*size.y*data[Structure_Slab1][houseID].price;
            generationCostPerThousand = std::min(generationCostPerThousand,1000*generationCost/-power);
        }
        if (citySimEnabled && DuneCity::isCityZoneStructure(structure->getItemID())) {
            const auto* zone = static_cast<const ZoneStructure*>(structure);
            currentZonePower += zone->getZonePowerDraw();
            const Coord pos = zone->getLocation();
            if (getMap().tileExists(pos.x,pos.y)
                && DuneCity::getStructurePopulation(zone,getMap().getTile(pos.x,pos.y)->getCityZoneDensity())
                    < DuneCity::getZonePopulation(zone->getItemID(),1))
                ++developingZones[zone->getItemID()-Structure_ZoneResidential];
            matureZonePower += DuneCity::getZonePower(structure->getItemID(), 3);
        }
    }
    if (citySimEnabled) for (Uint32 item=Structure_FirstID; item<=Structure_LastID; ++item)
        committedPowerDemand += std::max(0,itemCount[item]-getHouse()->getNumItems(item))
            * (DuneCity::isCityZoneStructure(item) ? DuneCity::getZonePower(item,3) : std::max(0,data[item][houseID].power));
    const int zoneGrowthHeadroom = QuantBotBuildPolicy::cityGrowthPowerHeadroom(
        currentZonePower,matureZonePower,projectedPowerDemandGrowth,committedPowerDemand);
    const int cityPowerReserve = citySimEnabled ? QuantBotBuildPolicy::cityPowerReserve(
        getHouse()->getPowerRequirement(), largestGenerator) + zoneGrowthHeadroom : 0;
    const bool nuclearPlan = citySimEnabled && !needsFirstTransport()
        && itemCount[Structure_HeavyFactory] > 0
        && itemCount[Structure_NuclearPlant] == getHouse()->getNumItems(Structure_NuclearPlant)
        && itemCount[Structure_WindTrap] == getHouse()->getNumItems(Structure_WindTrap)
        && QuantBotBuildPolicy::planNuclearInvestment(getHouse()->getPowerRequirement(),
            getHouse()->getProducedPower(),cityPowerReserve,std::max(1,-data[Structure_WindTrap][houseID].power))
        && nuclearBuildAvailable && findPlaceLocation(Structure_NuclearPlant).isValid();
    int cityYardTarget = citySimEnabled ? QuantBotBuildPolicy::cityConstructionYardTarget(
        money, ownResValve, ownComValve, ownIndValve) : 0;
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
        && cityYardTarget>itemCount[Structure_ConstructionYard]+itemCount[Unit_MCV]
        && QuantBotColonisationPolicy::colonisationAllowed(colonisationDemand(0,0)))
        cityYardTarget=std::min(cityYardTarget,QuantBotColonisationPolicy::productionYardCeiling(
            yardFormations(),getHouse()->getNumItems(Structure_ConstructionYard),colonySiteOpen(),builtOut));
    if(rockExpansionNeeded)cityYardTarget=std::max(cityYardTarget,getHouse()->getNumItems(Structure_ConstructionYard)+1);
    const int yardLimit = getGameInitSettings().getGameOptions().maximumNumberOfConstructionYardsOverride;
    if(yardLimit > 0) cityYardTarget = std::min(cityYardTarget, yardLimit);
    const int cityConstructionCapacity = itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV];
    // Settling free rock is a space decision, not a capacity one: it is due
    // when the city has nowhere left to build and somewhere safe to go, and it
    // is the only thing that keeps a built-out city growing. It runs beside
    // the yard target rather than through it, so production capacity is still
    // governed by that target alone.
    const bool colonisationNeeded = colonisationMcvDue(itemCount[Unit_MCV],yardLimit);

    // Custom-game city size ceiling for this bot, including shared-house
    // helpers. Campaign games keep their own separate gates.
    const int populationCeiling = getCityPopulationLimit(getMap().getSizeX()*getMap().getSizeY());
    const auto cityGrowthLimits = populationCeiling > 0
        ? QuantBotCityPolicy::limits(static_cast<int>(difficulty), getMap().getSizeX()*getMap().getSizeY())
        : QuantBotCityPolicy::Limits{};
    int cityZonesIncludingQueued = itemCount[Structure_ZoneResidential]
        + itemCount[Structure_ZoneCommercial] + itemCount[Structure_ZoneIndustrial];
    auto initialCityPopulation = [](Uint32 item) {
        return isStructure(item) ? DuneCity::getZonePopulation(item,1)
            + (item==Structure_Palace ? DuneCity::getPalaceCommercialPopulation(1) : 0) : 0;
    };
    int cityPendingPopulation = 0;
    int currentCityPopulation = 0;
    if(populationCeiling > 0) {
        for(Uint32 item=Structure_FirstID; item<=Structure_LastID; ++item)
            cityPendingPopulation += std::max(0,itemCount[item]-getHouse()->getNumItems(item))*initialCityPopulation(item);
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
    const int cityDisplayPopulation = QuantBotCityPolicy::displayPopulation(currentCityPopulation);
    int cityPendingDisplayPopulation = QuantBotCityPolicy::displayPopulation(cityPendingPopulation);
    auto cityAdmitsStructure = [&](Uint32 item) {
        if(populationCeiling <= 0 || !isStructure(item)) return true;
        if(DuneCity::isCityZoneStructure(item)
           && !QuantBotCityPolicy::allowsZone(cityZonesIncludingQueued,cityGrowthLimits.sharedZoneCap)) return false;
        const int added=QuantBotCityPolicy::displayPopulation(initialCityPopulation(item));
        // Zero-population services, power and roads remain available even in an oversized save.
        return added==0 || QuantBotCityPolicy::allowsPopulation(cityDisplayPopulation,
            cityPendingDisplayPopulation,added,populationCeiling);
    };

    auto decisionState = [&]() {
        return AITelemetry::Record().set("credits", getHouse()->getCredits()).set("spendable", money)
            .set("military", militaryValue).set("military_limit", militaryValueLimit)
            .set("military_budget", militaryBudget)
            .set("military_budget_source", militaryBudgetOverridden ? "override_rolling_headroom" : "configured")
            .set("unit_count_override", getGameInitSettings().getGameOptions().maximumNumberOfUnitsOverride)
            .set("unit_limit_reached", getHouse()->isGroundUnitLimitReached()).set("max_units", getHouse()->getMaxUnits())
            .set("power_rules_enabled", powerRules).set("rocket_turrets_need_power", turretPowerRequired).set("city_effects_enabled", citySimEnabled)
            .set("waiting_to_unload",waitingHarvesters).set("unloading_backlog",unloadingBacklog)
            .set("free_refineries",freeRefineries).set("busy_refineries",busyRefineries)
            .set("free_base_rock",availableBaseRock).set("rock_expansion_needed",rockExpansionNeeded)
            .set("base_free_footprints",availableBaseFootprints)
            .set("base_production_room_blocked",baseProductionRoomBlocked)
            .set("base_built_out",baseBuiltOut())
            .set("colonisation_due",colonisationNeeded).set("colonists_lost",colonistsLost())
            .set("economy_reserve", economyReserve).set("queued_production_cost", queuedProductionCost).set("queued_military_value", queuedMilitaryValue)
            .set("power_produced", getHouse()->getProducedPower()).set("power_required", getHouse()->getPowerRequirement())
            .set("zone_power_current",currentZonePower).set("zone_power_mature",matureZonePower)
            .set("zone_growth_headroom",zoneGrowthHeadroom).set("committed_power_demand",committedPowerDemand)
            .set("nuclear_investment_due",nuclearPlan)
            .set("city_power_reserve_target", cityPowerReserve).set("largest_generator_nominal", largestGenerator)
            .set("res_count", itemCount[Structure_ZoneResidential]).set("com_count", itemCount[Structure_ZoneCommercial])
            .set("ind_count", itemCount[Structure_ZoneIndustrial])
            .set("construction_yards", itemCount[Structure_ConstructionYard])
            .set("mcvs_including_queued", itemCount[Unit_MCV])
            .set("city_yard_target", cityYardTarget)
            .set("city_mcv_cash", citySimEnabled ? money : 0)
            .set("city_mcv_working_reserve", citySimEnabled ? std::max(1000, cityWorkingReserve) : 0)
            .set("city_mcv_shortfall", citySimEnabled ? std::max(0, cityYardTarget - cityConstructionCapacity) : 0)
            .set("vanilla_yard_target", vanillaEconomy ? DuneCity::vanillaYardTarget(
                money, getHouse()->getNumItems(Unit_Harvester)) : 0)
            .set("mcv_upgrade_in_progress", mcvUpgradesInProgress > 0)
            .set("mcv_upgrades_in_progress", mcvUpgradesInProgress)
            .set("mcv_shortfall", vanillaEconomy ? DuneCity::vanillaMcvShortfall(
                money, getHouse()->getNumItems(Unit_Harvester), itemCount[Structure_ConstructionYard], itemCount[Unit_MCV]) : 0)
            .set("construction_plans_first", citySimEnabled)
            .set("res_demand", ownResValve).set("com_demand", ownComValve).set("ind_demand", ownIndValve)
            .set("res_pop", ownResPop).set("com_pop", ownComPop).set("ind_pop", ownIndPop)
            .set("land_value", ownAvgLandValue).set("spice_remaining", lastCalculatedSpice)
            .set("spice_share", spiceShare).set("map_harvester_target", mapSpiceHarvesterTarget)
            .set("harvester_target", spiceHarvesterTarget).set("refinery_harvester_target",
                QuantBotBuildPolicy::refineryThroughputHarvesterTarget(spiceShare, 0,
                    getHouse()->getNumItems(Structure_Refinery), harvesterLimit))
            .set("repair_baseline",repairBaseline).set("repair_target",repairTarget)
            .set("combat_vehicles",combatVehicles).set("repair_waiting",waitingRepairVehicles)
            .set("harvester_investment_reserve", harvesterInvestmentReserve())
            .set("harvester_ai_limit", harvesterLimit).set("campaign_economy_push",campaignEconomyPush)
            .set("campaign_city_policy",campaignCityEconomy()).set("campaign_city_baseline",campaignBaseline.allowance)
            .set("campaign_city_zone_cap",campaignCityLimits().sharedZoneCap).set("campaign_city_post_spice",campaignPostSpice())
            .set("campaign_city_income_goal",QuantBotCityCampaignPolicy::totalIncomeGoalPerMinute(campaignBaseline))
            .set("campaign_city_income_forecast",campaignIncomeForecastPerMinute())
            .set("city_population_limit",cityGrowthLimits.displayPopulationLimit)
            .set("city_population_display",cityDisplayPopulation)
            .set("city_population_pending",cityPendingDisplayPopulation)
            .set("city_zone_cap",cityGrowthLimits.sharedZoneCap)
            .set("city_zones_including_queued",cityZonesIncludingQueued)
            .set("harvester_engine_limit", getHouse()->getMaxHarvesters())
            .set("funded_harvester_target", vanillaEconomy ? spiceHarvesterTarget : fundedHarvesterTarget)
            .set("storage_capacity", getHouse()->getCapacity())
            .set("nuclear_pending", itemCount[Structure_NuclearPlant] - getHouse()->getNumItems(Structure_NuclearPlant))
            .set("windtrap_pending", itemCount[Structure_WindTrap] - getHouse()->getNumItems(Structure_WindTrap))
            .set("stadium_committed", itemCount[Structure_Stadium]).set("airport_committed", itemCount[Structure_Airport])
            .set("strategic_item", strategicReserveItem).set("strategic_reserve", strategicReserveCost);
    };

    const bool campaignPowerBudget = isCampaignGameType(currentGame->gameType)
        && (difficulty==Difficulty::Easy || difficulty==Difficulty::Medium)
        && !citySimEnabled && !vanillaEconomy;
    auto campaignPowerNeeded = [&](int nextDemand) {
        int demand=0;
        for (Uint32 item=Structure_FirstID;item<=Structure_LastID;++item)
            demand+=std::max(0,itemCount[item]-getHouse()->getNumItems(item))*std::max(0,data[item][houseID].power);
        return campaignPowerBudget && CampaignDifficultyPolicy::needsWindtrap(
            getHouse()->getProducedPower(),getHouse()->getPowerRequirement(),demand,nextDemand,
            itemCount[Structure_WindTrap]>getHouse()->getNumItems(Structure_WindTrap)
            || itemCount[Structure_NuclearPlant]>getHouse()->getNumItems(Structure_NuclearPlant));
    };

    auto powerGenerationPending = [&]() {
        return itemCount[Structure_NuclearPlant] > getHouse()->getNumItems(Structure_NuclearPlant)
            || itemCount[Structure_WindTrap] > getHouse()->getNumItems(Structure_WindTrap);
    };

	auto chooseCityZone = [&](const BuilderBase* builder, bool bootstrap, bool missingOnly = false) {
        if (!campaignAllowsZone(itemCount[Structure_ZoneResidential]+itemCount[Structure_ZoneCommercial]+itemCount[Structure_ZoneIndustrial])) return Uint32(NONE_ID);

		auto ranked = QuantBotBuildPolicy::rankZones(
			itemCount[Structure_ZoneResidential], itemCount[Structure_ZoneCommercial],
			itemCount[Structure_ZoneIndustrial], ownResValve, ownComValve, ownIndValve, bootstrap);
        // The site scorer still rewards residential infill. It must not
        // override the chosen zone type and suppress stronger jobs demand.
        Uint32 selected = NONE_ID;
        AITelemetry::Record candidates;
        for (Uint32 candidate : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
            const int demand = candidate == Structure_ZoneResidential ? ownResValve
                : candidate == Structure_ZoneCommercial ? ownComValve : ownIndValve;
            int rank = -1;
            for (int i = 0; i < 3; ++i) if (ranked[i] == candidate) rank = i;
            candidates.set(std::to_string(candidate), AITelemetry::Record().set("rank", rank)
                .set("demand", demand).set("normalized_demand", QuantBotBuildPolicy::normalizedZoneDemand(candidate, demand))
                .set("count_including_queued", itemCount[candidate]));
        }
        AITelemetry::Record evaluated;
        for (Uint32 candidate : ranked) {
            if (candidate == NONE_ID || (missingOnly && itemCount[candidate] > 0)) continue;
            // Demand and site suitability select the tax candidate; the economic
            // comparison below decides whether more spice capacity is better.
            const char* reason = "lower_rank_not_evaluated";
            if (selected == NONE_ID) {
                if (!cityAdmitsStructure(candidate)) reason = "city_limit";
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
                .set("state", decisionState()).set("candidates", candidates).set("evaluated", evaluated));
        }
        return selected;
	};

    auto canBuildMilitaryVehicle = [&](const BuilderBase* factory) {
        if (getHouse()->isGroundUnitLimitReached()) return false;
        for (Uint32 item : {Unit_Tank,Unit_SiegeTank,Unit_Launcher,Unit_Devastator,Unit_SonicTank,Unit_Deviator})
            if (factory->isAvailableToBuild(item) && data[item][houseID].price <= money
                && int64_t(militaryValue) + data[item][houseID].price <= militaryBudget) return true;
        return false;
    };

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
    const uint8_t civicDemandClipped = citySimEnabled
        ? currentGame->getCitySimulation()->getHouseState(houseID).civicDemandBlocked : uint8_t(0);
    const uint8_t civicStructuralNeed = [&]() -> uint8_t {
        if (!citySimEnabled) return 0;
        DuneCity::ValveInputs thresholds;
        thresholds.resPop = ownResPop;
        thresholds.comPop = ownComPop;
        thresholds.indPop = ownIndPop;
        thresholds.hasStadium = itemCount[Structure_Stadium] > 0;
        thresholds.hasAirport = itemCount[Structure_Airport] > 0;
        // Industry's seaport requirement is not a civic this rule buys; the
        // starport rules below own it, so it is never reported missing here.
        thresholds.hasStarport = true;
        return DuneCity::missingDemandCivics(thresholds);
    }();

    // A required civic the yard cannot build yet is not an absent demand: the
    // missing link in the mod's own prerequisite chain - including the legal
    // yard upgrade - is what has to be bought first. Houses whose Radar was
    // destroyed or never built stayed capped for a whole match because
    // availability erased the demand instead of producing this plan. Nothing
    // about the chain is hard-coded; it is walked out of ObjectData exactly as
    // the core-defence rule walks it.
    struct CivicPlan {
        Uint32 civic = NONE_ID;   ///< the needed civic itself
        Uint32 order = NONE_ID;   ///< the civic, or the prerequisite to buy before it
        bool upgrade = false;     ///< the yard needs its own legal upgrade first
        bool pending = false;     ///< already ordered; nothing more to buy this pass
        const char* reason = "no_structural_need";
    };
    auto planCivicForYard = [&](const BuilderBase* yard) {
        CivicPlan plan;
        if (!citySimEnabled || yard == nullptr || civicStructuralNeed == 0) return plan;
        for (const Uint32 candidate : {Uint32(Structure_Stadium), Uint32(Structure_Airport)}) {
            const uint8_t flag = candidate == Uint32(Structure_Stadium)
                ? uint8_t(DuneCity::NeedStadium) : uint8_t(DuneCity::NeedAirport);
            if (!(civicStructuralNeed & flag)) continue;
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
            if (itemCount[step] > getHouse()->getNumItems(step)) {
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
                if (itemCount[prerequisite] > 0) { ordered = true; continue; }
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
    };

    // Estimate travel from existing refineries, with a bounded local spice scan.
    // Missing nearby fields retain a conservative long trip instead of inventing
    // local resources. Future receipts are always capped by the remaining share.
    int refineryTripCycles = -1, walkingTripCycles = -1, refineryFieldRisk = 0;
    auto forecastSpiceTrip = [&](Coord site) {
        if (refineryTripCycles >= 0 || site.isInvalid()) return;
        int distance = 65;
        Coord field = Coord::Invalid();
        for (int dy=-32;dy<=32;dy+=2) for (int dx=-32;dx<=32;dx+=2) {
            const Coord p(site.x+dx,site.y+dy);
            if (!getMap().tileExists(p.x,p.y) || !getMap().getTile(p.x,p.y)->hasSpice()) continue;
            const int d=std::abs(dx)+std::abs(dy);
            if (d<distance) {distance=d;field=p;}
        }
        refineryFieldRisk=dangerAt(site,getStructureSize(Structure_Refinery))
            + (field.isValid() ? dangerAt(field,Coord(1,1)) : 0);
        const FixPoint speed=data[Unit_Harvester][houseID].maxspeed*0.75_fix;
        walkingTripCycles=speed>0 ? (FixPoint(2*distance*TILESIZE)/speed).lround()
            : CityEconomyInvestmentPolicy::horizonCycles;
        refineryTripCycles=walkingTripCycles;
        const int workers=std::max(1,actualHarvesters);
        const int transported=std::min(workers,getHouse()->getNumItems(Unit_Carryall)*5);
        const FixPoint airSpeed=data[Unit_Carryall][houseID].maxspeed*0.75_fix;
        if (transported>0 && airSpeed>0 && distance>=MIN_CARRYALL_LIFT_DISTANCE) {
            // Two flights plus a conservative pickup/landing allowance. Credit
            // only the share supported by existing aircraft, never queued ones.
            const int flown=(FixPoint(2*distance*TILESIZE)/airSpeed).lround()+MILLI2CYCLES(12000);
            refineryTripCycles=int((int64_t(walkingTripCycles)*(workers-transported)
                + int64_t(std::min(walkingTripCycles,flown))*transported)/workers);
        }
    };
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->acceptsHarvesterDropoff()) {
            forecastSpiceTrip(structure->getLocation()); break;
        }
    const int fillCycles=(FixPoint(HARVESTERMAXSPICE)/HARVESTSPEED).lround();
    const int unloadCycles=HARVESTERMAXSPICE*8/5;
    auto workerAnnualIncome = [&]() {
        return HARVESTERMAXSPICE*int(DuneCity::kCyclesPerCityYear)
            / std::max(1,fillCycles+unloadCycles+std::max(0,refineryTripCycles));
    };
    const int bayAnnualIncome=int(DuneCity::kCyclesPerCityYear)*15/32;
    auto factoryPrefersHarvester = [&](const BuilderBase* factory) {
        if (harvesterInvestmentReserve() > 0) return true;
        const int workers=itemCount[Unit_Harvester],refs=itemCount[Structure_Refinery];
        if (QuantBotSpendingPolicy::marginalSpice(spiceShare,
            std::min(workers*workerAnnualIncome(),refs*bayAnnualIncome),
            std::min((workers+1)*workerAnnualIncome(),refs*bayAnnualIncome),
            data[Unit_Harvester][houseID].buildtime*15,CityEconomyInvestmentPolicy::horizonCycles,
            DuneCity::kCyclesPerCityYear)<=data[Unit_Harvester][houseID].price) return false;
        return CityEconomyInvestmentPolicy::preferFactoryHarvester(itemCount[Unit_Harvester],
            citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget,militaryValue,militaryBudget,
            data[Unit_Harvester][houseID].price,canBuildMilitaryVehicle(factory),
            citySimEnabled && gameMode == GameMode::Custom,brutalCityEconomy);
    };

    CityEconomyInvestmentPolicy::Investment lastEconomyInvestment;
    bool cityRefineryCatchup = false, cityRefineryOpening = false;
    auto chooseCityEconomy = [&](const BuilderBase* builder, bool opening) {
        using namespace CityEconomyInvestmentPolicy;
        lastEconomyInvestment = {};
        cityRefineryCatchup = false;
        cityRefineryOpening = false;
        const Uint32 zone = chooseCityZone(builder, opening);
        const auto* sim = currentGame->getCitySimulation();
        if (!sim) return zone;
        const int tax = sim->getCityTax();
        const int land = ownAvgLandValue>0 ? ownAvgLandValue : 128;
        const int workers = itemCount[Unit_Harvester], refs = itemCount[Structure_Refinery];
        const bool expandingOpening = CityEconomyInvestmentPolicy::openingWorkersNeeded(workers,fundedHarvesterTarget,brutalCityEconomy)
            && brutalCityEconomy;
        const bool openingRefinery = openingRefineryInvestment(brutalCityEconomy,workers,fundedHarvesterTarget,refs);
        const bool wantedWorker = workers < std::min(spiceHarvesterTarget,harvesterLimit);
        bool capacityNeeded = unloadingBacklog, refineryUseful = false;
        int workerIncome = 0, bayIncome = 0;
        bool hedge = zone == Structure_ZoneResidential && itemCount[zone] == 0;
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
                + (power*generationCostPerThousand+999)/1000;
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
            const int unfinished = developingZones[zone-Structure_ZoneResidential]
                + std::max(0,itemCount[zone]-getHouse()->getNumItems(zone));
            const int demand = zone == Structure_ZoneResidential ? ownResValve : industry ? ownIndValve : ownComValve;
            residential = setupInvestment(zone,site,power);
            residential.annualIncome = DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(zone,population,level),tax,land);
            if (zone != Structure_ZoneResidential) {
                // One C/I job supports eight residents. Credit only half the
                // tax of existing/pending housing currently short of jobs.
                const int waitingR = developingZones[0] + std::max(0,itemCount[Structure_ZoneResidential]
                    - getHouse()->getNumItems(Structure_ZoneResidential));
                const int shortage = std::max(0,ownResPop+waitingR*16-(ownComPop+ownIndPop)*8);
                residential.annualIncome += DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(
                    Structure_ZoneResidential,std::min(shortage,population*8),0),tax,land)/2;
            }
            residential.delayCycles = DuneCity::getCityBuildTime(zone,data[zone][houseID].buildtime)*15
                + DuneCity::kCyclesPerCityYear; // construction plus ~60 s growth/foundation allowance
            residential.confidence = zoneConfidence(demand,zone==Structure_ZoneResidential ? 2000 : 1500,
                industry ? 0 : pollution,crime,unfinished);
        }
        Coord refinerySite = Coord::Invalid();
        if ((spiceShare>0 || unloadingBacklog) && campaignAvailableToBuild(builder,Structure_Refinery))
            refinerySite = findPlaceLocation(Structure_Refinery);
        Coord forecastSite=refinerySite;
        if(forecastSite.isInvalid()) for(const auto* structure:getStructureList())
            if(structure->getOwner()==getHouse()&&structure->getItemID()==Structure_Refinery){forecastSite=structure->getLocation();break;}
        if (forecastSite.isValid()) {
            forecastSpiceTrip(forecastSite);
            const int roundTrip=fillCycles+unloadCycles+std::max(0,refineryTripCycles);
            workerIncome=workerAnnualIncome();
            bayIncome=bayAnnualIncome;
            capacityNeeded = unloadingBacklog || processingCapacityNeeded(refs,workers,workerIncome,bayIncome);
            if(refinerySite.isValid()) {
            refineryUseful = considerRefinery(capacityNeeded,wantedWorker,!harvesterFactories.empty(),workers < 2 || openingRefinery);
            const bool freeWorker = workers < harvesterLimit;
            const int power = std::max(0,data[Structure_Refinery][houseID].power);
            refinery = setupInvestment(Structure_Refinery,refinerySite,power);
            refinery.annualIncome = marginalSpiceIncome(workers,refs,freeWorker,workerIncome,bayIncome);
            const int refineryBuildCycles = data[Structure_Refinery][houseID].buildtime*15;
            refinery.delayCycles = refineryBuildCycles + roundTrip;
            refinery.projectedProceeds = refineryProceeds(workers,refs,freeWorker,workerIncome,bayIncome,
                refineryBuildCycles,roundTrip,unloadCycles,HARVESTERMAXSPICE,refinery.annualUpkeep);
            const int before=std::min(workers*workerIncome,refs*bayIncome);
            const int after=std::min((workers+int(freeWorker))*workerIncome,(refs+1)*bayIncome);
            const int finiteReturn=QuantBotSpendingPolicy::marginalSpice(spiceShare,before,after,
                refineryBuildCycles,horizonCycles,DuneCity::kCyclesPerCityYear);
            refinery.projectedProceeds=std::min(refinery.projectedProceeds,
                std::max(0,finiteReturn-refinery.annualUpkeep*4));
            if (refineryFieldRisk>0) refinery.confidence/=2;
            if(unloadingBacklog) {
                refinery.projectedProceeds=std::max(refinery.projectedProceeds,
                    std::min(waitingCargo,2*int(HARVESTERMAXSPICE))-refinery.annualUpkeep*4);
                refinery.confidence=1000; // Already harvested cargo, no remaining-spice risk.
            }
            }
        }
        const int taxIncome = DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths,tax,land);
        int developingIncome = 0;
        for (Uint32 kind : {Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial}) {
            const int pending = developingZones[kind-Structure_ZoneResidential]
                + std::max(0,itemCount[kind]-getHouse()->getNumItems(kind));
            const int demand = kind==Structure_ZoneResidential ? ownResValve
                : kind==Structure_ZoneCommercial ? ownComValve : ownIndValve;
            if (demand > 0) developingIncome += pending * DuneCity::computeAnnualTaxRevenue(
                DuneCity::taxablePopulationEighths(kind,DuneCity::getZonePopulation(kind,1),1),tax,land)/2;
        }
        const int fleetIncome = workers * workerIncome;
        // Positive demand and marginal return decide growth, independently of
        // the current tax/spice ratio. Only the very first housing seed is special.
        Uint32 selected = preferRefinery(refinery,residential,refineryUseful,hedge,capacityNeeded) ? Structure_Refinery : zone;
        cityRefineryCatchup = selected == Structure_Refinery && capacityNeeded;
        cityRefineryOpening = selected == Structure_Refinery && openingRefinery;
        if (selected == zone && zone!=NONE_ID && residential.confidence==0) selected=NONE_ID;
        lastEconomyInvestment=selected==Structure_Refinery ? refinery : selected==zone ? residential : Investment{};
        // Save for a winning refinery instead of spending its money on another
        // cheap lot every pass. The caller preserves urgent non-economic work.
        const bool funded = selected!=NONE_ID && money>=data[selected][houseID].price;
        if (AITelemetry::log().enabled()) {
            lastEconomyTraceCycle[builder->getObjectID()]=getGameCycleCount();
            auto describe = [](const Investment& i) { return AITelemetry::Record().set("cost",i.cost)
                .set("annual_income",i.annualIncome).set("annual_upkeep",i.annualUpkeep)
                .set("delay_cycles",i.delayCycles).set("confidence_per_mille",i.confidence).set("proceeds",i.proceeds()); };
            traceDecision("city_economy_comparison",AITelemetry::Record().set("builder",builder->getObjectID())
                .set("selected",selected).set("zone",zone).set("hedge",hedge).set("funded",funded)
                .set("capacity_needed",capacityNeeded).set("refinery_useful",refineryUseful)
                .set("unloading_backlog",unloadingBacklog).set("waiting_to_unload",waitingHarvesters).set("waiting_cargo",waitingCargo)
                .set("refinery_site_valid",refinerySite.isValid())
                .set("refinery_available",campaignAvailableToBuild(builder,Structure_Refinery))
                .set("refinery_placement",placementScoreDetails[Structure_Refinery])
                .set("brutal_opening",expandingOpening).set("opening_refinery",openingRefinery)
                .set("wanted_included_worker",wantedWorker)
                .set("worker_income",workerIncome).set("bay_capacity",bayIncome)
                .set("generation_cost_per_thousand",generationCostPerThousand)
                .set("workers",workers).set("refineries",refs)
                .set("factory_can_supply",!harvesterFactories.empty())
                .set("tax_income",taxIncome).set("developing_tax_income",developingIncome)
                .set("forecast_fleet_income",fleetIncome)
                .set("sustainable_workers",spiceHarvesterTarget).set("horizon_cycles",horizonCycles)
                .set("spice_share",spiceShare).set("trip_cycles",refineryTripCycles).set("field_risk",refineryFieldRisk)
                .set("tax_candidate",describe(residential)).set("refinery_candidate",describe(refinery)));
        }
        return selected;
    };

	bool emitStatsLog = false;

    if (!supportMode && (militaryValue > 0 || getHouse()->getNumStructures() > 0)) {
        const Uint32 currentCycle = getGameCycleCount();
        if(currentCycle - lastStatsLogCycle >= MILLI2CYCLES(30000)) {
			emitStatsLog = true;
            if (gameMode == GameMode::Custom) {
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d  built: %d  kill: %d  loss: %d remaining spice: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), militaryValue, militaryValueLimit, getHouse()->getUnitBuiltValue(),
                    getHouse()->getKillValue(), getHouse()->getLossValue(), lastCalculatedSpice, getHouse()->getNumItems(Unit_Harvester), harvesterLimit);
            } else {
                // Campaign mode - include initial military value and multiplier
                const QuantBotConfig& config = getQuantBotConfig();
                const QuantBotConfig::DifficultySettings& diffSettings = config.getSettings(static_cast<int>(difficulty));
                logDebug("Stats: %d  crdt: %d  mVal: %d/%d (init: %d, mult: %.1fx)  built: %d  kill: %d  loss: %d hvstr: %d/%d",
                    attackTimer, getHouse()->getCredits(), militaryValue, militaryValueLimit, initialMilitaryValue, diffSettings.militaryValueMultiplier,
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
            if (itemCount[i] != count) queued.set(std::to_string(i), itemCount[i] - count);
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
        if (citySimEnabled && currentGame->getCitySimulation()) {
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
                    .set("police_funding",hs.policeFundingPercent).set("state",decisionState()));
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
            if (!house) continue;
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
                .set("difficulty", static_cast<int>(difficulty)).set("support", supportMode)
                .set("house_name", getHouseNameByNumber(static_cast<HOUSETYPE>(getHouse()->getHouseID())))
                .set("team", getHouse()->getTeamID())
                .set("state", decisionState()).set("city_health", cityHealth).set("actual", actual).set("queued", queued)
                .set("harvesters", harvesters).set("refinery_queues",refineryQueues).set("house_comparison", opponents)
                .set("power_accounting", AITelemetry::Record().set("generators", actualGeneratorPower)
                    .set("reported", getHouse()->getProducedPower())
                    .set("difference", getHouse()->getProducedPower() - actualGeneratorPower))
                .set("economy_totals", AITelemetry::log().economyTotals(houseID))
                .set("economy", AITelemetry::Record()
                    .set("harvested_spice_total", getHouse()->getHarvestedSpice().lround())
                    .set("stored_spice_credits", getHouse()->getStoredCredits().lround())
                    .set("city_credit_balance", getHouse()->getCityCredits().lround())
                    .set("starting_credit_balance", getHouse()->getStartingCredits().lround()))
                .set("heavy_busy", activeHeavyFactoryCount).set("light_busy",activeLightFactoryCount).set("repair_busy", activeRepairYardCount)
                .set("built_value", getHouse()->getUnitBuiltValue()).set("kill_value", getHouse()->getKillValue())
                .set("loss_value", getHouse()->getLossValue()));
    }

    // Infantry retains its separate difficulty quota. Vehicle openings use
    // the current technology and actual production availability below.
	int infantryPercent = 10;
	switch (difficulty) {
		case Difficulty::Defend:
		case Difficulty::Easy:
			infantryPercent = 18;
			break;
		case Difficulty::Medium:
			infantryPercent = 15;
			break;
		case Difficulty::Hard:
			infantryPercent = 12;
			break;
		case Difficulty::Brutal:
			break;
	}

    constexpr std::array<Uint32,8> mixItems = {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
        Unit_SonicTank, Unit_Ornithopter, Unit_Trike, Unit_RaiderTrike, Unit_Quad};
    UnitMixPolicy::Weights damage{}, rewardMilli{}, killBonusMilli{}, lostValue{}, scores{}, defaults{}, prices{}, lossMilli{};
    const auto& ratios = getQuantBotConfig().getRatios(houseID);
    const std::array<int,5> configured = {static_cast<int>(ratios.tank*10000),
        static_cast<int>(ratios.siegeTank*10000), static_cast<int>(ratios.launcher*10000),
        static_cast<int>(ratios.special*10000), static_cast<int>(ratios.ornithopter*10000)};
    std::array<bool,8> available{};
    bool launcherProductionAvailable = false;
    for (const auto* structure : getStructureList()) {
        const auto* builder = dynamic_cast<const BuilderBase*>(structure);
        if (!builder || builder->getOwner() != getHouse()
            || (builder->getItemID() != Structure_LightFactory && builder->getItemID() != Structure_HeavyFactory
                && builder->getItemID() != Structure_HighTechFactory)) continue;
        for (size_t i=0; i<8; ++i) {
            if (i == 3) {
                for (Uint32 special : {Unit_Devastator, Unit_SonicTank, Unit_Deviator})
                    available[i] |= campaignAvailableToBuild(builder,special);
            } else {
                const bool producible = campaignAvailableToBuild(builder,mixItems[i]);
                available[i] |= producible;
                if (i == 2 && builder->getHealth() > 0 && builder->isActive())
                    launcherProductionAvailable |= producible;
            }
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
        for (size_t i = 0; i < 4; ++i) {
            if (i == 3) {
                for (Uint32 special : {Unit_Devastator, Unit_SonicTank, Unit_Deviator})
                    available[i] |= recoverableHeavyUnit(special);
            } else available[i] |= recoverableHeavyUnit(mixItems[i]);
        }
    }
    if (getHouse()->isAirUnitLimitReached()) available[4]=false;
    UnitMixPolicy::Weights configuredWeights{};
    for (size_t i=0; i<5; ++i) configuredWeights[i] = configured[i];
    const auto openingMix = UnitMixPolicy::openingMix(currentGame->techLevel, configuredWeights, available);
    for (size_t i=0; i<8; ++i) defaults[i] = openingMix[i];
    int64_t totalDamage = 0, totalRewardMilli = 0, totalLostValue = 0;
    for (size_t i=0; i<8; ++i) {
        const Uint32 item = mixItems[i];
        int priorPrice = std::max(1, data[item][houseID].price);
        damage[i] = std::max(0, getHouse()->getNumItemDamageInflicted(item));
        rewardMilli[i] = getHouse()->getCombatReward(item).total();
        killBonusMilli[i] = getHouse()->getCombatReward(item).killBonusMilli;
        lostValue[i] = int64_t(getHouse()->getNumLostItems(item)) * data[item][houseID].price;
        bool enabled = data[item][houseID].enabled && data[item][houseID].price > 0
            && data[item][houseID].techLevel <= currentGame->techLevel;
        if (i == 3) {
            damage[i] = rewardMilli[i] = killBonusMilli[i] = lostValue[i] = 0;
            enabled = false;
            priorPrice = 700; // Preserve the special-unit group's existing prior.
            for (Uint32 special : {Unit_Devastator, Unit_SonicTank, Unit_Deviator}) {
                damage[i] += getHouse()->getNumItemDamageInflicted(special);
                rewardMilli[i] += getHouse()->getCombatReward(special).total();
                killBonusMilli[i] += getHouse()->getCombatReward(special).killBonusMilli;
                lostValue[i] += int64_t(getHouse()->getNumLostItems(special)) * data[special][houseID].price;
                enabled |= data[special][houseID].enabled && data[special][houseID].price > 0
                    && data[special][houseID].techLevel <= currentGame->techLevel;
            }
            damage[i] = std::max<int64_t>(0, damage[i]);
        }
        totalDamage += damage[i];
        totalRewardMilli += rewardMilli[i];
        totalLostValue += lostValue[i];
        prices[i] = priorPrice * 1000;
        lossMilli[i] = lostValue[i] * 1000;
        if (!enabled || !available[i]) { available[i] = false; continue; }
        scores[i] = UnitMixPolicy::performanceScore(rewardMilli[i], lostValue[i]*1000, priorPrice*1000);
    }
    const bool learningUnitMix = totalRewardMilli >= 3000000;
    performanceHistory.update(getGameCycleCount(),rewardMilli,lossMilli);
    if (learningUnitMix) {
        for (size_t i=0;i<8;++i) scores[i]=available[i]
            ? UnitMixPolicy::performanceScore(performanceHistory.reward[i],performanceHistory.loss[i],prices[i]) : 0;
        scores=UnitMixPolicy::exploredScores(scores,performanceHistory.reward,performanceHistory.loss,prices,available);
    }
    const auto allocationWeights = UnitMixPolicy::sharpenScores(scores);
    const auto rawMix = UnitMixPolicy::normalize(allocationWeights);
    const int performanceConfidenceBps = UnitMixPolicy::evidenceConfidenceBps(totalLostValue, militaryValue);
    auto unitMix = UnitMixPolicy::allocate(scores, defaults, learningUnitMix, vanillaEconomy,
                                                  totalLostValue, militaryValue);
    // Damage-based learning does not value air deterrence, so protect a minimum
    // mobile anti-air share while a visible hostile wing is active.
    // The floor only exists while hostile aircraft are genuinely visible and
    // launchers are genuinely producible, so it cannot become a standing tax.
    const int observedHostileAircraft = autonomousCustomHardBrutal()
        ? visibleHostileAircraft() : 0;
    const bool launcherFloorAuthorised = observedHostileAircraft > 0 && available[2]
        && launcherProductionAvailable;
    const int launcherBpsBeforeFloor = unitMix[2];
    const int launcherFloorBps = launcherFloorAuthorised
        ? UnitMixPolicy::kMobileAntiAirFloorBps : 0;
    if (launcherFloorAuthorised)
        unitMix = UnitMixPolicy::applyFloor(unitMix, 2, launcherFloorBps);
    const bool launcherFloorApplied = unitMix[2] > launcherBpsBeforeFloor;
    // Troopers supplement the launcher share and the rocket turrets a city already
    // has; they are the close-range part of that answer, not the whole of it. The
    // demand is the same observation that protects the launcher share - reused here,
    // so no factory pass performs another army or map scan - and it adds no budget of
    // its own: the infantry quota, the city's working reserve and every build
    // prerequisite below still decide whether anything is actually ordered.
    const bool cityAntiAirInfantryDemand = citySimEnabled && observedHostileAircraft > 0;
    lastUnitMixBps = unitMix;
    // Observational record of exactly this pass: the same scores and the same
    // intended shares the orders below are placed from. Reading it cannot move a
    // unit, and nothing here recomputes anything - it only copies.
    for (size_t i = 0; i < 8; ++i) {
        allocationSnapshot.slots[i] = {mixItems[i], scores[i], unitMix[i], available[i]};
    }
    allocationSnapshot.ready = true;
    allocationSnapshot.cycle = getGameCycleCount();
    allocationSnapshot.learning = learningUnitMix;
    allocationSnapshot.militaryValue = militaryValue;
    allocationSnapshot.antiAirFloorBps = launcherFloorAuthorised ? launcherFloorBps : 0;
    allocationSnapshot.infantryQuotaBps = infantryPercent * 100;
    const int rawOrnithopterBps = rawMix[4];
    const FixPoint tankPercent = FixPoint(unitMix[0])/10000;
    const FixPoint siegePercent = FixPoint(unitMix[1])/10000;
    const FixPoint launcherPercent = FixPoint(unitMix[2])/10000;
    const FixPoint specialPercent = FixPoint(unitMix[3])/10000;
    const FixPoint ornithopterPercent = FixPoint(unitMix[4])/10000;
    const int lightVehicleBps = unitMix[5] + unitMix[6] + unitMix[7];
    const int lightVehiclePercent = lightVehicleBps / 100;

	int lightVehicleValue = data[Unit_Trike][houseID].price * itemCount[Unit_Trike]
		+ data[Unit_RaiderTrike][houseID].price * itemCount[Unit_RaiderTrike]
		+ data[Unit_Quad][houseID].price * itemCount[Unit_Quad];
	int infantryValue = data[Unit_Soldier][houseID].price * itemCount[Unit_Soldier]
		+ data[Unit_Infantry][houseID].price * itemCount[Unit_Infantry]
		+ data[Unit_Trooper][houseID].price * itemCount[Unit_Trooper]
		+ data[Unit_Troopers][houseID].price * itemCount[Unit_Troopers];
	int lightVehicleCount = itemCount[Unit_Trike] + itemCount[Unit_RaiderTrike] + itemCount[Unit_Quad];
	int infantryCount = itemCount[Unit_Soldier] + itemCount[Unit_Infantry]
		+ itemCount[Unit_Trooper] + itemCount[Unit_Troopers];

    // Would ordering one more of \a value still leave infantry inside their share of
    // the army? The candidate's own value counts on both sides, so an order that would
    // overshoot the share is refused rather than placed and then regretted. Both totals
    // already include every queued order and this pass updates them as it accepts more,
    // so several producers share one quota instead of each claiming a full share.
    auto infantryQuotaAdmits = [&](int value) {
        return value > 0 && (int64_t(infantryValue) + value) * 100
            <= (int64_t(militaryValue) + value) * infantryPercent;
    };
    // A city raises infantry only as the close-range part of its anti-air answer, so
    // nothing may rank, reserve cash for or order them once the observed wing is gone.
    auto cityInfantryItem = [&](Uint32 item) {
        return citySimEnabled && (item == Unit_Soldier || item == Unit_Infantry
            || item == Unit_Trooper || item == Unit_Troopers);
    };
    // Check troop content and its existing prerequisites; the new WOR supplies itself.
    // Its real build list still decides which offers can be queued once it exists.
    auto cityTroopContentExists = [&]() {
        for (const Uint32 troop : {Unit_Trooper, Unit_Troopers}) {
            const auto& spec = data[troop][houseID];
            if (!spec.enabled || spec.price <= 0 || spec.techLevel > currentGame->techLevel
                || spec.builder != Structure_WOR || !infantryQuotaAdmits(spec.price)) continue;
            bool prerequisites = true;
            for (int item = Structure_FirstID; item <= Structure_LastID; ++item)
                if (item != Structure_WOR && spec.prerequisiteStructuresSet[item]
                    && getHouse()->getNumItems(item) == 0) prerequisites = false;
            if (prerequisites) return true;
        }
        return false;
    };

    const int productionCash = std::max(0, money
        - std::max(strategicReserveCost, economyReserve) - (citySimEnabled ? cityWorkingReserve : 0));
    const int64_t cityWorCashReserve = int64_t(std::max(strategicReserveCost, economyReserve))
        + cityWorkingReserve + std::max(0, data[Structure_ZoneResidential][houseID].price);
    const int fundedArmyValue = QuantBotBuildPolicy::fundedArmyTarget(militaryValue, militaryBudget, productionCash);
    const int vehiclePlanValue = std::max(0, fundedArmyValue - infantryValue);
    if (emitStatsLog && AITelemetry::log().enabled()) {
        traceDecision("unit_mix", AITelemetry::Record().set("basis", !learningUnitMix ? "configured" : vanillaEconomy ? "lifetime_evidence_weighted_value_per_loss" : "lifetime_value_per_loss")
            .set("tank_bps", (tankPercent * 10000).lround()).set("siege_bps", (siegePercent * 10000).lround())
            .set("special_bps", (specialPercent * 10000).lround()).set("launcher_bps", (launcherPercent * 10000).lround())
            .set("ornithopter_bps", (ornithopterPercent * 10000).lround()).set("raw_ornithopter_bps", rawOrnithopterBps)
            .set("light_vehicle_percent", lightVehiclePercent).set("light_vehicle_bps", lightVehicleBps)
            .set("trike_bps", unitMix[5]).set("raider_trike_bps", unitMix[6]).set("quad_bps", unitMix[7])
            .set("allocation_types", 8).set("tech_level", currentGame->techLevel)
            .set("opening_light_bps", openingMix[5]+openingMix[6]+openingMix[7]).set("total_damage", totalDamage)
            .set("total_reward_milli", totalRewardMilli).set("total_lost_value", totalLostValue)
            .set("launcher_floor_bps", launcherFloorBps)
            .set("launcher_floor_applied", launcherFloorApplied)
            .set("launcher_production_available", launcherProductionAvailable)
            .set("launcher_bps_before_floor", launcherBpsBeforeFloor)
            .set("visible_hostile_aircraft", observedHostileAircraft)
            .set("performance_exponent_milli",1500).set("performance_confidence_bps", performanceConfidenceBps).set("funded_army_value",fundedArmyValue)
            .set("vehicle_plan_value",vehiclePlanValue).set("queued_military_value",queuedMilitaryValue).set("mix_inputs", [&]() {
                AITelemetry::Record inputs;
                for (size_t i=0; i<8; ++i) inputs.set(i == 3 ? "special" : std::to_string(mixItems[i]),
                    AITelemetry::Record().set("damage", damage[i]).set("reward_milli", rewardMilli[i]).set("kill_bonus_milli", killBonusMilli[i])
                        .set("lost_value", lostValue[i]).set("score", scores[i]).set("allocation_weight",allocationWeights[i])
                        .set("lifetime_reward_milli",performanceHistory.reward[i]).set("lifetime_loss_milli",performanceHistory.loss[i])
                        .set("available", available[i]).set("opening_bps", openingMix[i]).set("target_bps", unitMix[i]));
                return inputs;
            }()).set("infantry_percent", infantryPercent));
    }

	// lets analyse damage inflicted

	if (emitStatsLog) {
		logDebug("Adaptive unit mix: damage=%lld light=%d bps", static_cast<long long>(totalDamage), lightVehicleBps);

		logDebug("  Tank: %d/%d %f Siege: %d/%d %f Special: %d/%d %f Launch: %d/%d %f Orni: %d/%d %f",
			getHouse()->getNumItemDamageInflicted(Unit_Tank), getHouse()->getNumLostItems(Unit_Tank) * 300, tankPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_SiegeTank), getHouse()->getNumLostItems(Unit_SiegeTank) * 600, siegePercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_SonicTank) + getHouse()->getNumItemDamageInflicted(Unit_Devastator) + getHouse()->getNumItemDamageInflicted(Unit_Deviator),
			getHouse()->getNumLostItems(Unit_SonicTank) * 600 + getHouse()->getNumLostItems(Unit_Devastator) * 800 + getHouse()->getNumLostItems(Unit_Deviator) * 750,
			specialPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_Launcher), getHouse()->getNumLostItems(Unit_Launcher) * 450, launcherPercent.toDouble(),
			getHouse()->getNumItemDamageInflicted(Unit_Ornithopter), getHouse()->getNumLostItems(Unit_Ornithopter) * data[Unit_Ornithopter][houseID].price, ornithopterPercent.toDouble()
		);
	}

	// End of adaptive unit prioritisation algorithm

	// Track unique structures ordered this tick to prevent multiple CYs
	// from building the same thing. Zones and turrets are excluded (want multiples).
	std::set<Uint32> orderedThisTick;
    bool roadMaintenanceAttempted = false;

    // One cross-queue capital comparison. Protect only its next purchase; all
    // independent builders may use the remainder. Recovery/prerequisite rules
    // still run, but optional growth cannot consume another queue's commitment.
    auto purchasePrice = [](const BuilderBase* builder, Uint32 item) {
        for (const auto& offer : builder->getBuildList())
            if (offer.itemID == item) return int(offer.price);
        return 0;
    };
    bool defendingEconomy=false;
    // Concrete is no longer an investment decision. Every ordinary building is
    // prepared in full before it is ordered, in every mode and at every
    // difficulty, because half health is a permanent loss of production,
    // generation, land value or armour. Walls, roads and the slabs themselves
    // are tile states with no placement penalty and stay exempt, and the yard
    // an MCV deploys is never queued, so it keeps its own exception.
    // An incomplete plan defers the building; it never places it bare.
    auto foundationPlan = [&](const BuilderBase* builder, Uint32 item, Coord site,
                              const std::vector<Uint32>& clearedZones = std::vector<Uint32>{}) {
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
                state.paveable=tile->isRock() && !tile->isMountain()
                    && (!tile->isBlocked() || cleared) && (!tile->hasCityZone() || cleared);
                state.inBuildRange=getMap().isWithinBuildRange(x,y,getHouse());
                return state;
            });
    };
    auto foundationOrders = [&](const BuilderBase* builder, Uint32 item, Coord site) {
        return foundationPlan(builder,item,site).orders;
    };
    // The buildings this bot keeps at full health regardless of wealth. Used
    // both by the repair pass below and by the generation investment, which
    // prices exactly the repairs a closed power deficit would stop paying for.
    // StructureBase charges repairs from the original owner's price table, so
    // captured buildings and mod overrides are classified the way the engine
    // actually bills them.
    auto repairPrice = [&](const StructureBase* structure) {
        return data[structure->getItemID()][structure->getOriginalHouseID()].price;
    };
    auto repairPolicyMaintains = [&](const StructureBase* structure) {
        return QuantBotBuildPolicy::repairMaintainsValue(structure->getItemID(),citySimEnabled,
            structure->getMaxHealth(),repairPrice(structure));
    };
    struct CapitalCandidate {
        Uint32 builder=NONE_ID, item=NONE_ID;
        int price=0, cost=0, proceeds=0, score=0, delay=0, capacity=0, foundationCost=0;
        const char* kind="economy";
        const char* reason="unavailable";
        /// The service evaluation actually found a present emergency behind this
        /// candidate: dangerous crime in occupied buildings, or enemy aircraft
        /// already over our own structures. Deliberately not "this candidate
        /// scored highly" and not "this is a protection candidate" - treating
        /// routine coverage as urgent is what starved required civics.
        bool urgent=false;
        Coord site=Coord::Invalid();
    };
    std::vector<CapitalCandidate> capitalCandidates;
    AITelemetry::Record capitalProducers, capitalUnitOptions;
    for (const auto* unit:getUnitList()) {
        if (!unit->isActive() || !unit->isVisible(getHouse()->getTeamID())
            || unit->getOwner()->getTeamID()==getHouse()->getTeamID()) continue;
        const auto* victim=unit->getTarget();
        if (victim && victim->getOwner()==getHouse() && unit->isInWeaponRange(victim)
            && (victim->isAStructure() || victim->getItemID()==Unit_Harvester
                || victim->getItemID()==Unit_RebelHarvester)) defendingEconomy=true;
    }
    const int forecastTax=citySimEnabled ? DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths,
        currentGame->getCitySimulation()->getCityTax(),ownAvgLandValue) : 0;
    const int forecastUpkeep=citySimEnabled ? getHouse()->getPowerRequirement()/8
        + currentGame->getCitySimulation()->getHouseState(houseID).lastPoliceExpense : 0;
    // Future workers/bays count toward targets and commitments, but their
    // production is not income until delivered. Reforecast every planning pass.
    const int fleetRate=std::min(actualHarvesters*workerAnnualIncome(),
        getHouse()->getNumItems(Structure_Refinery)*bayAnnualIncome);
    const int forecastSpice=std::min(spiceShare,fleetRate*QuantBotSpendingPolicy::horizonMinutes);
    const int forecastNetIncome=forecastSpice+(forecastTax-forecastUpkeep)*QuantBotSpendingPolicy::horizonMinutes;
    const int forecastFunding=std::max(0,money+forecastNetIncome);
    int existingMilitaryCapacity=0, constructionCapacity=0, readyYards=0;
    int continuedMilitaryCost=0, continuedConstructionCost=0;
    int activeProductionBurn=0;
    std::map<Uint32,int> laneCapacity;
    std::vector<const BuilderBase*> capitalBuilders;
    for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || !structure->isABuilder()) continue;
        const auto* builder=static_cast<const BuilderBase*>(structure);
        capitalBuilders.push_back(builder);
        if (builder->getItemID()==Structure_ConstructionYard && !builder->isUpgrading()
            && !builder->isOnHold() && builder->getProductionQueueSize()==0) ++readyYards;
        int capacity=0, cityCapacity=0, unpaid=0, currentBurn=0;
        const bool port=builder->getItemID()==Structure_StarPort;
        const bool yard=builder->getItemID()==Structure_ConstructionYard;
        const FixPoint speed=std::min(builder->getHealth()/builder->getMaxHealth(),builder->getBuildSpeedLimit());
        for (const auto& offer:builder->getBuildList()) {
            const Uint32 item=offer.itemID;
            if (!port) unpaid+=offer.num*int(offer.price);
            int buildtime=data[item][builder->getOriginalHouseID()].buildtime;
            if (citySimEnabled) buildtime=DuneCity::getCityBuildTime(item,buildtime);
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
                    && itemCount[item]<(citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget)))
                capacity=std::max(capacity,cost);
            if (yard && (citySimEnabled
                    ? (item==Structure_ZoneResidential && ownResValve>0)
                        || (item==Structure_ZoneCommercial && ownComValve>0)
                        || (item==Structure_ZoneIndustrial && ownIndValve>0)
                        || (item==Structure_Refinery && unloadingBacklog)
                    : item==Structure_HeavyFactory || (item==Structure_Refinery && lastCalculatedSpice>0)))
                cityCapacity=std::max(cityCapacity,cost);
        }
        unpaid=port ? 0 : std::max(0,unpaid-builder->getProductionProgress().lround());
        activeProductionBurn+=currentBurn;
        laneCapacity[builder->getItemID()]=std::max(laneCapacity[builder->getItemID()],capacity);
        // Include idle and held lines: more factories cannot fix cash starvation.
        existingMilitaryCapacity+=capacity;
        constructionCapacity+=cityCapacity;
        continuedMilitaryCost+=std::max(0,capacity-unpaid);
        continuedConstructionCost+=std::max(0,cityCapacity-unpaid);
        if (AITelemetry::log().enabled()) capitalProducers.set(std::to_string(builder->getObjectID()),
            AITelemetry::Record().set("item",builder->getItemID()).set("queue",builder->getProductionQueueSize())
                .set("upgrading",builder->isUpgrading()).set("hold",builder->isOnHold())
                .set("current_item",builder->getCurrentProducedItem()).set("unpaid",unpaid)
                .set("active_burn_per_minute",currentBurn).set("unit_capacity_cost",capacity)
                .set("construction_capacity_cost",cityCapacity));
    }
    const int cashBuffer=std::max(1000,citySimEnabled ? cityWorkingReserve : economyReserve);
    const auto cashFlow=QuantBotSpendingPolicy::cashFlow(getHouse()->getCredits(),money,forecastNetIncome,
        continuedMilitaryCost+continuedConstructionCost,existingMilitaryCapacity+constructionCapacity,cashBuffer);
    // Cash-funded vanilla openings use the established parallel MCV/factory
    // policy. Use actual runway, not a fixed wealth cutoff: as the base grows,
    // the same cash must cover more production as well as its economy pipeline.
    const bool sharedSpending=!supportMode && (!vanillaEconomy || !cashFlow.fundsParallelProduction);
    const int militaryFunding=std::max(0,forecastFunding-continuedConstructionCost
        + existingMilitaryCapacity-continuedMilitaryCost);
    std::sort(capitalBuilders.begin(),capitalBuilders.end(),[](const auto* a,const auto* b) {
        return a->getObjectID()<b->getObjectID();
    });
    // One construction slot keeps growing the tax base. Recompute it from real
    // queues, so losing a yard or loading a save cannot strand the assignment.
    int cityYards=0, cityGrowthYardsBusy=0;
    if (citySimEnabled) for (const auto* builder:capitalBuilders) {
        if (builder->getItemID()!=Structure_ConstructionYard) continue;
        ++cityYards;
        const auto reserved=reservedStructures.find(builder->getObjectID());
        if (!builder->isOnHold() && !builder->isUpgrading() && builder->getProductionQueueSize()>0
            && (DuneCity::isCityZoneStructure(builder->getCurrentProducedItem())
                || (reserved!=reservedStructures.end() && DuneCity::isCityZoneStructure(reserved->second.item))))
            ++cityGrowthYardsBusy;
    }
    auto affordableCityZone = [&](const BuilderBase* builder, int budget) {
        if (!citySimEnabled || getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<24)
            return Uint32(NONE_ID);
        const Uint32 zone=chooseCityZone(builder,false);
        return zone!=NONE_ID && purchasePrice(builder,zone)<=budget ? zone : Uint32(NONE_ID);
    };
    auto buildingCapitalCost = [&](Uint32 item) {
        const Coord size=getStructureSize(item);
        const int power=citySimEnabled || powerRules ? std::max(0,data[item][houseID].power) : 0;
        // Without concrete there is no foundation to budget for.
        const int foundation=getGameInitSettings().getGameOptions().concreteRequired
            ? size.x*size.y*data[Structure_Slab1][houseID].price : 0;
        return data[item][houseID].price + foundation
            + (power*generationCostPerThousand+999)/1000;
    };
    // The engine's own installment divisor: BuilderBase::updateProductionProgress()
    // charges price/(buildTime*15) per tick, taking the city build time in city
    // games. Used to price one installment, never to predict a completion date.
    auto cityBuildTicks = [&](Uint32 item) {
        const int configured = data[item][houseID].buildtime;
        return 15 * (citySimEnabled ? DuneCity::getCityBuildTime(item,configured) : configured);
    };
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
    int civicReserveCost = 0;
    if (citySimEnabled && civicStructuralNeed != 0) for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getItemID() != Structure_ConstructionYard) continue;
        const auto plan = planCivicForYard(static_cast<const BuilderBase*>(structure));
        if (plan.order == NONE_ID) continue;
        const int price = data[plan.order][houseID].price;
        civicReserveCost = std::max(civicReserveCost,
            CityEconomyInvestmentPolicy::productionStartBudget(price,
                std::max(0, buildingCapitalCost(plan.order) - price), cityBuildTicks(plan.order)));
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
    const bool spiceEconomyViable =
        CityEconomyInvestmentPolicy::spiceEconomyViable(mapSpiceHarvesterTarget);
    // A lot pays for its own foundation and for the roads that connect it;
    // an unconnected 2x2 never develops, so it is part of the lot's price.
    const int cityBootstrapLotCost = buildingCapitalCost(Structure_ZoneResidential)
        + 2 * std::max(0, data[Structure_Road][houseID].price);
    // Somewhere to put the money. Capacity comes from the mod's own tables,
    // not from an assumption about which building stores credits: take the
    // cheapest item this house can actually build and place that reports any
    // capacity at all. In the shipped tree that is the Refinery (1,005) and
    // then the Silo (10,000) behind it - so on a spiceless map a refinery is
    // bought for its vault and its tech, never to feed a harvester, and the
    // distinction is in the rule that buys it, not in the building.
    int cityStorageCapacityIncludingQueued = getHouse()->getCapacity();
    for (Uint32 item = Structure_FirstID; item <= Structure_LastID; ++item)
        cityStorageCapacityIncludingQueued += std::max(0,
            itemCount[item] - getHouse()->getNumItems(item)) * std::max(0, data[item][houseID].capacity);
    Uint32 cityStorageItem = NONE_ID;
    int cityStorageCost = 0;
    if (citySimEnabled
        && CityEconomyInvestmentPolicy::cityStorageMissing(cityStorageCapacityIncludingQueued))
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || !structure->isABuilder()) continue;
            const auto* builder = static_cast<const BuilderBase*>(structure);
            if (builder->getItemID() != Structure_ConstructionYard) continue;
            for (Uint32 item = Structure_FirstID; item <= Structure_LastID; ++item) {
                if (data[item][houseID].capacity <= 0) continue;
                if (!campaignAvailableToBuild(builder,item)) continue;
                const int cost = buildingCapitalCost(item);
                if (cityStorageItem != NONE_ID && cost >= cityStorageCost) continue;
                if (!findPlaceLocation(item).isValid()) continue;
                cityStorageItem = item; cityStorageCost = cost;
            }
        }
    // Live, not latched: cityZonesIncludingQueued rises as this pass places
    // orders, so the reserve shrinks lot by lot and several yards in the same
    // pass cannot each withhold - or each buy - the whole seed. Only a city
    // the simulation actually demands can be seeded; with no positive R/C/I
    // demand there is nothing to protect and nothing to wait for, so the
    // ordinary ladder must not be held up either.
    bool cityParallelBootstrapFunding = false;
    auto cityBootstrapNeeded = [&] {
        // Nothing to hold cash for without a yard that can place a lot.
        return cityYards > 0
            && CityEconomyInvestmentPolicy::cityBootstrapIncomplete(citySimEnabled,
                spiceEconomyViable, cityZonesIncludingQueued,
                cityStorageCapacityIncludingQueued)
            && (!cityParallelBootstrapFunding
                || CityEconomyInvestmentPolicy::cityStorageMissing(cityStorageCapacityIncludingQueued))
            // A house that owns no lot at all holds the seed whatever the
            // current valves say. On the first pass the simulation has not
            // published any demand yet, and a map-placed Starport spent the
            // whole grant on bargains in that one-tick window - the original
            // bankruptcy, on a different map. Once some lots exist, continued
            // protection does need demand, so a finished city releases it.
            // Unbankable income needs no demand reading at all: the vault is
            // worth buying the moment the city can earn anything.
            && (cityZonesIncludingQueued == 0
                || CityEconomyInvestmentPolicy::cityStorageMissing(cityStorageCapacityIncludingQueued)
                || ownResValve > 0 || ownComValve > 0 || ownIndValve > 0);
    };
    auto cityBootstrapReserve = [&] {
        return CityEconomyInvestmentPolicy::cityBootstrapReserve(cityBootstrapNeeded(),
            cityZonesIncludingQueued, cityBootstrapLotCost,
            cityStorageItem == NONE_ID
                || !CityEconomyInvestmentPolicy::cityStorageMissing(cityStorageCapacityIncludingQueued)
                ? 0 : cityStorageCost);
    };
    // A large starting grant can pay for the first heavy line AND keep it
    // operating. In that case construction throughput, not income, is the
    // opening constraint. Budget its missing prerequisites and four minutes of
    // production before allowing it ahead of the ordinary refinery/port path.
    int firstFactoryCapital=0, firstFactoryProduction=0;
    bool firstFactoryLegal=true;
    std::set<Uint32> firstFactoryPrerequisites;
    std::vector<Uint32> factorySteps{Structure_HeavyFactory};
    while (!factorySteps.empty()) {
        const auto item=factorySteps.back();factorySteps.pop_back();
        if (!firstFactoryPrerequisites.insert(item).second || itemCount[item]>0) continue;
        firstFactoryLegal &= data[item][houseID].enabled && data[item][houseID].techLevel<=currentGame->techLevel;
        firstFactoryCapital+=buildingCapitalCost(item);
        for (int prerequisite=Structure_FirstID;prerequisite<=Structure_LastID;++prerequisite)
            if (data[item][houseID].prerequisiteStructuresSet[prerequisite]) factorySteps.push_back(prerequisite);
    }
    for (int item=Unit_FirstID;item<=Unit_LastID;++item) {
        const auto& info=data[item][houseID];
        if (!info.enabled || info.builder!=Structure_HeavyFactory || info.techLevel>currentGame->techLevel
            || info.upgradeLevel>0 || info.buildtime<=0 || item==Unit_MCV) continue;
        const int time=citySimEnabled ? DuneCity::getCityBuildTime(item,info.buildtime) : info.buildtime;
        firstFactoryProduction=std::max(firstFactoryProduction,
            int(int64_t(info.price)*CityEconomyInvestmentPolicy::horizonCycles/std::max(1,time*15)));
    }
    const bool fundedFactoryOpening=gameMode==GameMode::Custom && itemCount[Structure_HeavyFactory]==0
        && firstFactoryLegal && firstFactoryProduction>0 && militaryValue<militaryBudget
        && money>=firstFactoryCapital+cashBuffer
        && cashFlow.projectedCash>=firstFactoryCapital+firstFactoryProduction+cashBuffer;
    // A funded established city can grow and produce in parallel. Its cash
    // must cover both commitments, rather than treating one tiny tax receipt
    // as permission to spend a scarce opening grant on optional technology.
    const int bootstrapCommitment=CityEconomyInvestmentPolicy::cityBootstrapReserve(true,
        cityZonesIncludingQueued,cityBootstrapLotCost,cityStorageItem==NONE_ID ? 0 : cityStorageCost);
    const bool establishedCityIncome=getHouse()->getNumItems(Structure_HeavyFactory)>0
        && getHouse()->getCapacity()>0 && ownTaxBaseEighths>0 && forecastTax>forecastUpkeep;
    cityParallelBootstrapFunding=establishedCityIncome || (ownTaxBaseEighths>0
        && money>=firstFactoryCapital+cashBuffer+bootstrapCommitment
        && cashFlow.projectedCash>=firstFactoryCapital+firstFactoryProduction+cashBuffer+bootstrapCommitment);
    // When cash covers another line AND four minutes of its operation, use
    // the established parallel factory/repair build order. City demand is not
    // a prerequisite for production: factories also supply the city's MCVs.
    const int nextHeavyRunway=buildingCapitalCost(Structure_HeavyFactory)
        + std::max(firstFactoryProduction,laneCapacity[Structure_HeavyFactory])+cashBuffer;
    const bool fundedCityProduction=citySimEnabled && gameMode==GameMode::Custom
        && cashFlow.fundsParallelProduction && money>=nextHeavyRunway
        && cashFlow.projectedCash>=nextHeavyRunway && militaryValue<militaryBudget;
    const int desiredWorkers=citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget;
    const int transportBaseline=QuantBotBuildPolicy::carryallTarget(itemCount[Unit_Harvester],combatVehicles,
        getHouse()->getNumItems(Structure_RepairYard));
    const int transportTarget=QuantBotBuildPolicy::supportQueueTarget(transportBaseline,
        getHouse()->getNumItems(Unit_Carryall),itemCount[Unit_Carryall],busyTransports,int(pickupQueue.size()));
    // Coverage the core still lacks, counting existing and planned turrets.
    // This is a spatial demand, not a count goal: it stops by itself once the
    // yards, refineries and factories have the difficulty's overlap, so it can
    // drive defence proactively without turning into turret spam. Coverage
    // starts once the first refinery pays for it, not after the full fleet.
    const int coverageTier=rocketCoverageTier(difficulty);
    // Enemy aircraft that have actually reached our territory. A wing counted
    // from the enemy's item list is a plan the base can prepare for between
    // other construction; aircraft over our own buildings are a present loss.
    bool airEngaged=false;
    for (const auto* aircraft:getUnitList()) {
        if (airEngaged) break;
        if (!aircraft->isAFlyingUnit() || !aircraft->isActive() || aircraft->getHealth()<=0
            || !aircraft->canAttack() || !aircraft->getOwner()
            || aircraft->getOwner()->getTeamID()==getHouse()->getTeamID()
            || !aircraft->isVisible(getHouse()->getTeamID())) continue;
        const int reach=std::max(1,aircraft->getWeaponRange())+8;
        for (const auto* structure:getStructureList())
            if (structure->getOwner()==getHouse() && structure->getHealth()>0
                && blockDistance(aircraft->getLocation(),
                    structure->getClosestPoint(aircraft->getLocation()))<=reach) { airEngaged=true; break; }
    }
    // Core assets that no turret covers at all, as opposed to those short of
    // the difficulty's overlap. First cover is defence; further overlap is a
    // plan that has to share the yards with the city it protects.
    int uncoveredCoreAssets=0;
    auto coreCoverageShortfall=[&]() {
        if (!citySimEnabled || itemCount[Structure_Refinery]==0) return 0;
        const int radius=std::max(1,data[Structure_RocketTurret][houseID].weaponrange-1);
        std::vector<Coord> turrets;
        for (const auto* structure:getStructureList())
            if (structure->getOwner()==getHouse() && structure->getHealth()>0
                && structure->getItemID()==Structure_RocketTurret) turrets.push_back(structure->getLocation());
        for (const auto& entry:reservedStructures)
            if (entry.second.item==Structure_RocketTurret) turrets.push_back(entry.second.location);
        int shortfall=0;
        uncoveredCoreAssets=0;
        const Uint32 mainYard=mainConstructionYardID();
        auto account=[&](Uint32 item,Coord origin,Coord size,Uint32 objectID) {
            if (!RocketTurretPolicy::coreAsset(item)) return;
            const int demand=RocketTurretPolicy::desiredCoverage(item,coverageTier,
                isExpansionYard(item,objectID,mainYard));
            int covered=0;
            for (const Coord turret:turrets)
                if (RocketTurretPolicy::coversBuilding(turret,origin,size,radius)) ++covered;
            if (demand>0 && covered==0) ++uncoveredCoreAssets;
            shortfall+=std::max(0,demand-covered);
        };
        for (const auto* structure:getStructureList())
            if (structure->getOwner()==getHouse() && structure->getHealth()>0)
                account(structure->getItemID(),structure->getLocation(),structure->getStructureSize(),
                    structure->getObjectID());
        for (const auto& entry:reservedStructures)
            account(entry.second.item,entry.second.location,getStructureSize(entry.second.item),NONE_ID);
        return shortfall;
    };
    // Prime the first-cover count for the rules that consult it before any
    // shortfall is evaluated; later calls refresh it against new reservations.
    coreCoverageShortfall();
    // Cover the base actually demands, used both as the interim emplacement
    // ceiling and to bound proactive coverage below.
    int coverageDemand=0;
    const Uint32 anchorYard=mainConstructionYardID();
    for (const auto* structure:getStructureList())
        if (structure->getOwner()==getHouse() && structure->getHealth()>0)
            coverageDemand+=RocketTurretPolicy::desiredCoverage(structure->getItemID(),coverageTier,
                isExpansionYard(structure->getItemID(),structure->getObjectID(),anchorYard));
    // Spread core assets can demand an overlap that no legal site reaches, so
    // the shortfall alone never returns to zero. Peaceful pursuit of it then
    // owns every construction slot and its savings for the rest of the game,
    // which is what stopped city growth. Bound the proactive claim by the
    // emplacements the base's own demand justifies; observed aircraft keep the
    // unbounded shortfall, because that is a present loss rather than a plan.
    // Both custom-game economies finish the production core before repeatedly
    // adding cover against aircraft seen elsewhere on the map.
    Uint32 missingCoreInfrastructure=NONE_ID;
    bool coreInfrastructureReady=true;
    if (gameMode==GameMode::Custom && !supportMode)
    for (Uint32 item:{Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard}) {
        if (!data[item][houseID].enabled || data[item][houseID].techLevel>currentGame->techLevel) continue;
        if (getHouse()->getNumItems(item)==0) coreInfrastructureReady=false;
        if (missingCoreInfrastructure==NONE_ID && itemCount[item]==0) missingCoreInfrastructure=item;
    }
    // Storage relief keeps its established city-only core condition: an
    // overflowing bank is lost income now, whatever tech is still missing.
    const bool cityCoreInfrastructureReady=!citySimEnabled || coreInfrastructureReady;
    // Earned spice and city income share storage. Make room before optional
    // growth can monopolize the yards, and stop once storage reaches the global
    // credit ceiling. Count queued capacity so parallel yards do not spam silos.
    auto storageExpansionNeeded = [&]() {
        const int capacity = getHouse()->getCapacity();
        // A bay already ordered brings its own storage, so count that pending
        // capacity instead of cancelling every silo while refineries expand.
        const int pendingCapacity=std::max(0,itemCount[Structure_Refinery]
            -getHouse()->getNumItems(Structure_Refinery))*data[Structure_Refinery][houseID].capacity;
        const int projectedCapacity=capacity+pendingCapacity;
        return getHouse()->getNumItems(Structure_HeavyFactory)>0 && cityCoreInfrastructureReady
            && capacity > 0 && projectedCapacity < 999999
            && getHouse()->getEarnedCredits() >= projectedCapacity * 0.80_fix
            && itemCount[Structure_Silo] == getHouse()->getNumItems(Structure_Silo);
    };
    // IX unlocks the units and upgrades that make further emplacements worth
    // owning, so a non-city base that can already order it finishes that step
    // before repeating optional cover. City coverage keeps its own established
    // core rule; this adds no new condition there.
    const bool advancedTechPending=!citySimEnabled && gameMode==GameMode::Custom && !supportMode
        && itemCount[Structure_HeavyFactory]>0 && itemCount[Structure_IX]==0
        && anyConstructionYardCanBuildIX && findPlaceLocation(Structure_IX).isValid();
    // Keep unloading capacity ahead of optional additional cover as the worker
    // fleet grows. Existing/pending bays count, and an unplaceable refinery
    // cannot reserve the construction queue. The first two rockets still fit.
    const bool refiningCapacityPending=!citySimEnabled && customStrategicPlanning
        && lastCalculatedSpice>=500 && anyConstructionYardCanBuildRefinery
        && itemCount[Structure_Refinery]<(vanillaEconomy
            ? QuantBotBuildPolicy::desiredSpiceRefineries(spiceHarvesterTarget,itemCount[Unit_Harvester])
            : itemCount[Unit_Harvester]/3)
        && findPlaceLocation(Structure_Refinery).isValid();
    const bool routineRocketsAllowed=(coreInfrastructureReady && !advancedTechPending && !refiningCapacityPending)
        || itemCount[Structure_RocketTurret]<2;
    // Coverage worth buying this pass. The unbounded shortfall belongs to
    // aircraft that have actually reached the base — owning ornithopters
    // somewhere on the map kept a 1v4 chasing an overlap it could never
    // complete — and every claim is interleaved with construction unless a
    // core asset still has no cover at all.
    auto proactiveCoverageShortfall=[&]() {
        if (!routineRocketsAllowed) return 0;
        const int shortfall=coreCoverageShortfall();
        if (!RocketTurretPolicy::proactiveCoverageTurn(airEngaged,uncoveredCoreAssets,
            nonServiceConstructionOrders)) return 0;
        if (airEngaged) return shortfall;
        return itemCount[Structure_RocketTurret]>=RocketTurretPolicy::coverageTurretCap(coverageDemand)
            ? 0 : shortfall;
    };
    // A proactive goal keeps its savings only while the forecast cannot also
    // fund a city plot. Growth pays the bills that reach the saved price, so a
    // hold that outlives the forecast starves the economy it is defending.
    // Urgent crime keeps its unconditional reserve.
    const int cityPlotPrice=data[Structure_ZoneResidential][houseID].price;
    auto proactiveSavingHold=[&](int price) {
        // Establish a small R/C/I district, then interleave further plots with
        // services. Unlimited cheap fallback lots otherwise prevent even the
        // first turret upgrade or supplier from ever reaching its price.
        const int services=itemCount[Structure_RocketTurret]+itemCount[Structure_PoliceStation];
        const bool growthTurn=cityZonesIncludingQueued < 3+2*services;
        return !growthTurn || cashFlow.projectedCash < price+cityPlotPrice;
    };
    // Reserve the turret's draw and one small city plot, rather than two whole
    // spare windtraps. Normal power planning separately budgets maturation.
    // Outside the city the reserve is the emplacement's own draw: the flat 225
    // was more than two spare windtraps of headroom per turret, and a base
    // countering aircraft built thirteen generators it never needed.
    const int rocketPowerBuffer=citySimEnabled ? std::max(0,data[Structure_RocketTurret][houseID].power)
        + std::max({DuneCity::getZonePower(Structure_ZoneResidential,1),
                    DuneCity::getZonePower(Structure_ZoneCommercial,1),
                    DuneCity::getZonePower(Structure_ZoneIndustrial,1)})
        : std::max(0,data[Structure_RocketTurret][houseID].power);
    const bool openingSupplierDue = openingFleetIncomplete()
        && itemCount[Structure_Refinery]>=3 && itemCount[Structure_RocketTurret]>=2
        && itemCount[Structure_StarPort]==0 && itemCount[Structure_HeavyFactory]==0;
    int moderateCrimeProperties=0, dangerousCrimeProperties=0;
    if (citySimEnabled) for (const auto* structure:getStructureList()) {
        if (structure->getOwner()!=getHouse() || structure->getHealth()<=0
            || DuneCity::getStructureCityRole(structure->getItemID())==DuneCity::CityRole::None) continue;
        const auto p=structure->getLocation();
        const int crime=currentGame->getCitySimulation()->getCrimeRateMap().worldGet(p.x,p.y);
        moderateCrimeProperties+=crime>=128;
        dangerousCrimeProperties+=crime>=192;
    }
    if (sharedSpending) for (const auto* builder:capitalBuilders) {
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
            if (citySimEnabled && coreInfrastructureReady && expansionTurretsMissing(builder)>0
                && data[Structure_RocketTurret][houseID].enabled
                && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel) {
                CapitalCandidate defence;
                defence.builder=builder->getObjectID();defence.item=Structure_RocketTurret;
                defence.kind="expansion_defence";defence.reason="secure_expansion";defence.score=5500;
                if (builder->getCurrentUpgradeLevel()<data[Structure_RocketTurret][houseID].upgradeLevel
                    && builder->getMaxUpgradeLevel()>=data[Structure_RocketTurret][houseID].upgradeLevel) {
                    defence.kind="expansion_upgrade";
                    defence.price=defence.cost=builder->getUpgradeCost();
                    if (defence.price>0) capitalCandidates.push_back(defence);
                } else if (campaignAvailableToBuild(builder,Structure_RocketTurret)) {
                    if (turretPowerRequired && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<rocketPowerBuffer)
                        defence.item=Structure_WindTrap;
                    if (campaignAvailableToBuild(builder,defence.item)) {
                        defence.site=defence.item==Structure_RocketTurret ? findCityTurretPlaceLocation(defence.item)
                            : findPlaceLocation(defence.item);
                        defence.price=data[defence.item][houseID].price;defence.cost=buildingCapitalCost(defence.item);
                        if (defence.site.isValid()) capitalCandidates.push_back(defence);
                    }
                }
            }
            CapitalCandidate economy;
            economy.builder=builder->getObjectID();
            if (citySimEnabled) {
                economy.item=chooseCityEconomy(builder,false);
                economy.cost=lastEconomyInvestment.cost;
                economy.proceeds=lastEconomyInvestment.proceeds();
                economy.delay=lastEconomyInvestment.delayCycles;
            } else if (campaignAvailableToBuild(builder,Structure_Refinery)
                && findPlaceLocation(Structure_Refinery).isValid()) {
                forecastSpiceTrip(findPlaceLocation(Structure_Refinery));
                const int workers=itemCount[Unit_Harvester], refs=itemCount[Structure_Refinery];
                const bool freeWorker=workers<desiredWorkers;
                const int before=std::min(workers*workerAnnualIncome(),refs*bayAnnualIncome);
                const int after=std::min((workers+int(freeWorker))*workerAnnualIncome(),(refs+1)*bayAnnualIncome);
                economy.item=Structure_Refinery;
                economy.cost=buildingCapitalCost(economy.item);
                economy.delay=data[economy.item][houseID].buildtime*15;
                economy.proceeds=QuantBotSpendingPolicy::marginalSpice(spiceShare,before,after,
                    economy.delay,CityEconomyInvestmentPolicy::horizonCycles,DuneCity::kCyclesPerCityYear);
                if (refineryFieldRisk>0) economy.proceeds/=2;
                const bool useful=CityEconomyInvestmentPolicy::considerRefinery(unloadingBacklog
                    || CityEconomyInvestmentPolicy::processingCapacityNeeded(refs,workers,workerAnnualIncome(),bayAnnualIncome),
                    freeWorker,!harvesterFactories.empty(),workers<2);
                if (!useful) economy.proceeds=0;
                if (unloadingBacklog) economy.proceeds=std::max(economy.proceeds,std::min(waitingCargo,2*int(HARVESTERMAXSPICE)));
            }
            if (economy.item!=NONE_ID) {
                economy.price=data[economy.item][houseID].price;
                economy.score=QuantBotSpendingPolicy::economyScore(economy.proceeds,economy.cost);
                economy.reason=economy.score>0 ? "marginal_income" : "no_marginal_income";
                capitalCandidates.push_back(economy);
            }
            // Services must be allowed to save their price, not wait for money
            // left after every factory has placed another order. Use the existing
            // marginal coverage search; pending services are included in it.
            if (citySimEnabled && getHouse()->hasPower()) {
                CapitalCandidate service;
                const int coreShortfall=proactiveCoverageShortfall();
                service.builder=builder->getObjectID();service.kind="civic";
                if (moderateCrimeProperties>0 && selectCityServiceInvestment(builder,
                    std::max(data[Structure_PoliceStation][houseID].price,data[Structure_RocketTurret][houseID].price),
                    true,service.item,service.site)) {
                    service.score=dangerousCrimeProperties>0 ? 6000 : 2500;
                    service.reason=dangerousCrimeProperties>0 ? "crime_prevention" : "crime_maintenance";
                    service.urgent=dangerousCrimeProperties>0;
                } else if ((airEngaged || coreShortfall>0
                        || (!openingWorkersNeeded() && itemCount[Structure_Refinery]>0))
                    && (airEngaged || uncoveredCoreAssets>0
                        || nonServiceConstructionOrders>=3 || itemCount[Structure_RocketTurret]==0)
                    && (!openingSupplierDue || airEngaged)
                    && routineRocketsAllowed
                    && campaignAvailableToBuild(builder,Structure_RocketTurret)
                    && (!turretPowerRequired || getHouse()->getProducedPower()-getHouse()->getPowerRequirement()>=rocketPowerBuffer)) {
                    int uncovered=0;
                    service.site=findCityTurretPlaceLocation(Structure_RocketTurret,&uncovered);
                    if (service.site.isValid() && uncovered>0) {
                        // Aircraft already in play outrank peaceful coverage, and
                        // an uncovered yard/refinery/factory outranks covering an
                        // ordinary building, but both stay below crime prevention.
                        service.item=Structure_RocketTurret;
                        service.score=airEngaged ? 3000 : uncoveredCoreAssets>0 ? 2600 : 2000;
                        service.reason=airEngaged ? "air_coverage"
                            : uncoveredCoreAssets>0 ? "core_coverage" : "uncovered_base";
                        service.urgent=airEngaged;
                    }
                }
                if (service.item!=NONE_ID && service.site.isValid()) {
                    service.price=data[service.item][houseID].price;service.cost=buildingCapitalCost(service.item);
                    capitalCandidates.push_back(service);
                }
            }
            if (itemCount[Structure_RepairYard]>0 && itemCount[Structure_RepairYard]<repairTarget
                && canAddRepairYard(itemCount[Structure_RepairYard]) && !openingWorkersNeeded()
                && campaignAvailableToBuild(builder,Structure_RepairYard)) {
                CapitalCandidate repair;
                repair.builder=builder->getObjectID();repair.item=Structure_RepairYard;repair.kind="repair";
                repair.price=data[repair.item][houseID].price;repair.cost=buildingCapitalCost(repair.item);
                repair.site=findPlaceLocation(repair.item);
                if (repair.site.isValid()) {
                    repair.score=waitingRepairVehicles>=2 ? 3500 : 1800;
                    repair.reason=waitingRepairVehicles>=2 ? "repair_queue" : "repair_fleet_ratio";
                }
                capitalCandidates.push_back(repair);
            }
            // A sold-out Starport cannot meet a growing transport target.
            // Fund the first local supplier before optional factory expansion
            // or repeat zoning; pending High Tech factories already cover it.
            if (itemCount[Unit_Carryall]<transportTarget && itemCount[Structure_HighTechFactory]==0
                && itemCount[Structure_HeavyFactory]>0 && !openingWorkersNeeded()
                && (getHouse()->getChoam().getNumAvailable(Unit_Carryall)<=0 || itemCount[Structure_StarPort]==0)
                && ((actualHarvesters>0 && lastCalculatedSpice>0)
                    || (combatVehicles>0 && getHouse()->getNumItems(Structure_RepairYard)>0))
                && !getHouse()->isAirUnitLimitReached()
                && campaignAvailableToBuild(builder,Structure_HighTechFactory)) {
                CapitalCandidate supplier;
                supplier.builder=builder->getObjectID();supplier.item=Structure_HighTechFactory;
                supplier.kind="transport_production";supplier.price=data[supplier.item][houseID].price;
                supplier.cost=buildingCapitalCost(supplier.item);supplier.site=findPlaceLocation(supplier.item);
                if (supplier.site.isValid()) {
                    supplier.score=itemCount[Unit_Carryall]==0 ? 5500 : 4500;
                    supplier.reason="transport_imports_unavailable";
                    capitalCandidates.push_back(supplier);
                }
            }
            for (Uint32 factory:{Structure_LightFactory,Structure_HeavyFactory,Structure_HighTechFactory}) {
                CapitalCandidate capacity;
                capacity.builder=builder->getObjectID();capacity.item=factory;capacity.kind="production";
                capacity.price=data[factory][houseID].price;capacity.cost=buildingCapitalCost(factory);
                const int actual=getHouse()->getNumItems(factory);
                const int busy=factory==Structure_LightFactory ? activeLightFactoryCount
                    : factory==Structure_HeavyFactory ? activeHeavyFactoryCount : activeHighTechFactoryCount;
                capacity.delay=data[factory][houseID].buildtime*15;
                const int added=int(int64_t(laneCapacity[factory])
                    * std::max(0,CityEconomyInvestmentPolicy::horizonCycles-capacity.delay)
                    / CityEconomyInvestmentPolicy::horizonCycles);
                capacity.capacity=QuantBotSpendingPolicy::additionalProduction(militaryFunding,existingMilitaryCapacity,
                    added,std::max(0,militaryBudget-militaryValue),capacity.cost);
                if (actual==0) capacity.reason="bootstrap_managed_separately";
                else if (factory==Structure_LightFactory && int64_t(lightVehicleValue)*10000
                    >= int64_t(vehiclePlanValue)*lightVehicleBps) capacity.reason="light_mix_satisfied";
                else if (itemCount[factory]>actual) capacity.reason="factory_pending";
                else if (busy<actual) capacity.reason="existing_line_available";
                else if (!campaignAvailableToBuild(builder,factory)) capacity.reason="tech_unavailable";
                else if (getHouse()->isUnitLimitReached(factory==Structure_HighTechFactory ? Unit_Ornithopter : Unit_Tank)) capacity.reason="unit_limit";
                else if (capacity.capacity<=0) capacity.reason="income_or_army_bottleneck";
                else if (!findPlaceLocation(factory).isValid()) capacity.reason="no_site";
                else {
                    capacity.score=QuantBotSpendingPolicy::productionScore(capacity.capacity,capacity.cost,militaryValue,militaryBudget);
                    capacity.reason="funded_production_bottleneck";
                }
                capitalCandidates.push_back(capacity);
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
            const int before=std::min(itemCount[Unit_Harvester]*workerAnnualIncome(),itemCount[Structure_Refinery]*bayAnnualIncome);
            const int after=std::min((itemCount[Unit_Harvester]+1)*workerAnnualIncome(),itemCount[Structure_Refinery]*bayAnnualIncome);
            worker.proceeds=QuantBotSpendingPolicy::marginalSpice(spiceShare,before,after,
                worker.delay,CityEconomyInvestmentPolicy::horizonCycles,DuneCity::kCyclesPerCityYear);
            if (refineryFieldRisk>0) worker.proceeds/=2;
            if (!ready) worker.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_Harvester)) worker.reason="tech_unavailable";
            else if (port && getHouse()->getChoam().getNumAvailable(Unit_Harvester)<=0) worker.reason="sold_out";
            else if (getHouse()->isGroundUnitLimitReached() || (engineHarvesterLimit>0
                && itemCount[Unit_Harvester]>=engineHarvesterLimit)) worker.reason="unit_limit";
            else if (itemCount[Unit_Harvester]>=desiredWorkers && harvesterInvestmentReserve()==0) worker.reason="fleet_target";
            else if (worker.proceeds<=worker.cost && harvesterInvestmentReserve()==0) worker.reason="spice_or_bay_bottleneck";
            else if (!port && harvesterInvestmentReserve()==0 && !factoryPrefersHarvester(builder)) worker.reason="army_balance";
            else {
                worker.score=QuantBotSpendingPolicy::economyScore(worker.proceeds,worker.cost);
                if ((harvesterInvestmentReserve()>0 || openingFleetIncomplete()) && !defendingEconomy)
                    worker.score=std::max(worker.score,5000);
                worker.reason="marginal_income";
            }
            capitalCandidates.push_back(worker);
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
            if (itemCount[Unit_Carryall]>=transportTarget) transport.reason="transport_target_met";
            else if ((getHouse()->getNumItems(Unit_Harvester)==0 || getHouse()->getNumItems(Structure_Refinery)==0)
                && (militaryValue==0 || itemCount[Structure_RepairYard]==0))
                transport.reason="no_working_spice_fleet";
            else if (lastCalculatedSpice<=0 && (militaryValue==0 || itemCount[Structure_RepairYard]==0)) transport.reason="spice_depleted";
            else if (!ready) transport.reason="producer_busy";
            else if (!campaignAvailableToBuild(builder,Unit_Carryall) || transport.price<=0) transport.reason="tech_unavailable";
            else if (port && getHouse()->getChoam().getNumAvailable(Unit_Carryall)<=0) transport.reason="sold_out";
            else if (getHouse()->isAirUnitLimitReached()) transport.reason="unit_limit";
            else {
                transport.score=itemCount[Unit_Carryall]==0 ? 5500
                    : itemCount[Unit_Carryall]*2<transportTarget ? 4500 : 2000;
                transport.reason=itemCount[Unit_Carryall]==0 ? "first_transport_productivity" : "transport_capacity_shortfall";
            }
            capitalCandidates.push_back(transport);
        }
        // Give an imported colonist the same shared cash protection as a
        // factory-built one; otherwise repeated bargains consume its savings.
        if (port && colonisationNeeded && !mcvBuildAvailable && itemCount[Unit_MCV]==0) {
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
            capitalCandidates.push_back(colonist);
        }
        if (ready && building==Structure_HeavyFactory && campaignAvailableToBuild(builder,Unit_MCV)
            && !getHouse()->isGroundUnitLimitReached() && itemCount[Unit_MCV]==0
            && gameMode==GameMode::Custom && !openingWorkersNeeded()) {
            CapitalCandidate mcv;
            mcv.builder=builder->getObjectID();mcv.item=Unit_MCV;mcv.kind="construction";
            mcv.price=data[Unit_MCV][houseID].price;mcv.cost=mcv.price;
            mcv.delay=data[Unit_MCV][houseID].buildtime*15+MILLI2CYCLES(60000);
            const bool cityNeed=citySimEnabled && cityConstructionCapacity<cityYardTarget
                && (rockExpansionNeeded || availableBaseRock>=48)
                && (rockExpansionNeeded || ownResValve>0 || ownComValve>0 || ownIndValve>0);
            // Colonisation is not production capacity, so it does not wait for
            // the yard target. A city that has built out its own rock cannot
            // grow at all until someone settles the next formation, however
            // many yards it already runs.
            const bool colonyNeed=colonisationNeeded;
            const bool vanillaNeed=!citySimEnabled && DuneCity::prioritizeVanillaMcv(money,
                actualHarvesters,itemCount[Structure_ConstructionYard],itemCount[Unit_MCV],mcv.price);
            if (!(cityNeed || colonyNeed || vanillaNeed)) mcv.reason="construction_capacity_available";
            else if (forecastFunding<mcv.cost+std::max(1000,cityWorkingReserve)) mcv.reason="income_bottleneck";
            else {
                mcv.capacity=forecastFunding-mcv.cost-cityWorkingReserve;
                // Establish demanded city throughput before filling the entire
                // transport ratio. The first Carryall still ranks above this.
                mcv.score=(cityNeed || colonyNeed) ? 5000 : 4500;
                mcv.reason=colonyNeed&&!cityNeed ? "base_rock_exhausted"
                    : rockExpansionNeeded ? "available_rock_exhausted" : "funded_construction_bottleneck";
            }
            capitalCandidates.push_back(mcv);
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
            for (size_t i=0;i<mixItems.size();++i) if (mixItems[i]==item) bps=unitMix[i];
            if (item==Unit_SonicTank || item==Unit_Deviator || item==Unit_Devastator) bps=unitMix[3];
            if (item==Unit_Soldier || item==Unit_Infantry || item==Unit_Trooper || item==Unit_Troopers) bps=infantryPercent*100;
            const int64_t deficit=int64_t(vehiclePlanValue)*bps-int64_t(itemCount[item])*value*10000;
            int score=QuantBotSpendingPolicy::militaryScore(price,value,militaryValue,militaryBudget,defendingEconomy);
            const char* reason="eligible";
            if (!campaignAvailableToBuild(builder,item)) reason="unavailable";
            else if (getHouse()->isUnitLimitReached(item)) reason="unit_limit";
            else if (port && getHouse()->getChoam().getNumAvailable(item)<=0) reason="sold_out";
            else if (port && price >= value) reason="not_discounted";
            else if (!port && (bps<=0 || deficit<=0)) reason="mix_satisfied";
            // City infantry answer an observed wing and claim the aggregate infantry
            // share, not a full share each. Without this a finished air raid would go
            // on reserving cash for troops nobody is going to order.
            else if (cityInfantryItem(item) && !cityAntiAirInfantryDemand) reason="no_observed_air";
            else if (cityInfantryItem(item) && !infantryQuotaAdmits(value)) reason="infantry_quota_satisfied";
            else if (item==Unit_Ornithopter && int64_t(itemCount[item]+1)*value*10000
                > int64_t(militaryValue+value)*2000) reason="air_composition_cap";
            else if (score<=0) reason="military_limit";
            if (std::string(reason)!="eligible") score=0;
            if (AITelemetry::log().enabled()) unitOptions.set(std::to_string(item),AITelemetry::Record()
                .set("price",price).set("military_value",value).set("committed",itemCount[item])
                .set("target_bps",bps).set("deficit_scaled",deficit).set("score",score).set("reason",reason));
            if (score>soldier.score || (score>0 && score==soldier.score && deficit>bestDeficit)) {
                soldier.item=item;soldier.price=price;soldier.cost=price;soldier.score=score;
                soldier.proceeds=value;soldier.delay=port ? MILLI2CYCLES(30000) : data[item][houseID].buildtime*15;
                soldier.reason=defendingEconomy ? "active_defence" : "military_shortfall";bestDeficit=deficit;
            }
        }
        if (soldier.item!=NONE_ID) capitalCandidates.push_back(soldier);
        if (AITelemetry::log().enabled()) capitalUnitOptions.set(std::to_string(builder->getObjectID()),unitOptions);
    }
    for (auto& candidate:capitalCandidates) if (isStructure(candidate.item) && candidate.score>0
        && candidate.kind!=std::string("expansion_upgrade")) {
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
    planningBuilder=NONE_ID; clearPlacementCache(false,true);
    int capitalChoice=-1;
    for (size_t i=0;i<capitalCandidates.size();++i)
        if (capitalCandidates[i].score>0 && (capitalChoice<0
            || capitalCandidates[i].score>capitalCandidates[capitalChoice].score)) capitalChoice=int(i);
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
    const bool protectionCapital = capitalChoice >= 0
        && (capitalCandidates[capitalChoice].reason == std::string("crime_prevention")
            || capitalCandidates[capitalChoice].reason == std::string("air_coverage")
            || capitalCandidates[capitalChoice].reason == std::string("secure_expansion"));
    bool cityGrowthProtected=false;
    // With multiple yards, assign one to growth alongside routine investment.
    // Crime protection keeps the winning allocation; growth resumes after its
    // order is accepted. A lone opening yard retains its tech path.
    Uint32 dedicatedCityYard=NONE_ID;
    if (sharedSpending && citySimEnabled && cityYards>1 && !protectionCapital) {
        for (const auto* builder:capitalBuilders) {
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
            const Uint32 zone=affordableCityZone(builder,money);
            if (zone==NONE_ID) continue;
            dedicatedCityYard=builder->getObjectID();
            CapitalCandidate growth;
            growth.builder=builder->getObjectID();growth.item=zone;
            growth.price=purchasePrice(builder,zone);growth.cost=buildingCapitalCost(zone);
            growth.kind="city_growth";growth.reason="dedicated_city_growth";
            growth.score=1; // A construction allocation, not a fabricated return forecast.
            capitalChoice=int(capitalCandidates.size());capitalCandidates.push_back(growth);
            cityGrowthProtected=true;
            break;
        }
        planningBuilder=NONE_ID;clearPlacementCache(false,true);
    }
    if (!cityGrowthProtected && citySimEnabled
        && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()>=24
        && capitalChoice>=0 && capitalCandidates[capitalChoice].kind==std::string("military")) {
        int growth=-1;
        for (size_t i=0;i<capitalCandidates.size();++i)
            if (DuneCity::isCityZoneStructure(capitalCandidates[i].item) && capitalCandidates[i].score>0
                && (growth<0 || capitalCandidates[i].score>capitalCandidates[growth].score)) growth=int(i);
        if (growth>=0) {capitalChoice=growth;cityGrowthProtected=true;}
    }
    bool capitalConsumed=false;
    int capitalOrderedCost=0;
    AITelemetry::Record capitalOrders;
    const int capitalInitialCount=capitalChoice>=0 ? itemCount[capitalCandidates[capitalChoice].item] : 0;
    auto capitalPending = [&]() {
        return capitalChoice>=0 && !capitalConsumed
            && itemCount[capitalCandidates[capitalChoice].item]==capitalInitialCount;
    };
    auto capitalReserve = [&](Uint32 builder) {
        return capitalPending() ? QuantBotSpendingPolicy::reserveForOther(INT_MAX,
            capitalCandidates[capitalChoice].price+capitalCandidates[capitalChoice].foundationCost,capitalCandidates[capitalChoice].builder==builder,false,
            !getHouse()->hasPower() || itemCount[Structure_ConstructionYard]==0) : 0;
    };
    uint64_t capitalDecision=0;
    if (!supportMode && AITelemetry::log().enabled()) {
        AITelemetry::Record options;
        for (size_t i=0;i<capitalCandidates.size();++i) {
            const auto& c=capitalCandidates[i];
            options.set(std::to_string(i),AITelemetry::Record().set("builder",c.builder).set("item",c.item)
                .set("kind",c.kind).set("price",c.price).set("total_cost_with_power",c.cost)
                .set("proceeds_or_military_value",c.proceeds).set("additional_funded_capacity",c.capacity)
                .set("foundation_cost",c.foundationCost).set("score",c.score).set("delay_cycles",c.delay)
                .set("reason",c.reason).set("urgent",c.urgent)
                .set("affordable",money>=c.price+c.foundationCost));
        }
        capitalDecision=traceDecision("capital_plan",AITelemetry::Record().set("policy_version",2)
            .set("mode",citySimEnabled ? "dunecity" : "vanilla").set("campaign",isCampaignGameType(currentGame->gameType))
            .set("campaign_helper_reserve",harvesterInvestmentReserve()).set("defending",defendingEconomy)
            .set("cash",getHouse()->getCredits()).set("committed_cost",queuedProductionCost).set("spendable",money)
            .set("horizon_cycles",CityEconomyInvestmentPolicy::horizonCycles).set("forecast_tax_per_minute",forecastTax)
            .set("forecast_upkeep_per_minute",forecastUpkeep).set("forecast_spice_receipts",forecastSpice)
            .set("forecast_net_income",forecastNetIncome).set("forecast_funding",forecastFunding)
            .set("active_production_burn_per_minute",activeProductionBurn)
            .set("construction_capacity_cost",constructionCapacity).set("continued_construction_cost",continuedConstructionCost)
            .set("continued_unit_cost",continuedMilitaryCost).set("military_funding_after_construction",militaryFunding)
            .set("projected_cash",cashFlow.projectedCash).set("cash_buffer",cashBuffer)
            .set("net_burn_per_minute",cashFlow.netBurnPerMinute).set("cash_runway_seconds",cashFlow.runwaySeconds)
            .set("funds_parallel_production",cashFlow.fundsParallelProduction).set("shared_priority",sharedSpending)
            .set("starport_market_available",starportMarketAvailable).set("funded_factory_opening",fundedFactoryOpening)
            .set("funded_city_production",fundedCityProduction).set("next_heavy_runway",nextHeavyRunway)
            .set("city_growth_protected",cityGrowthProtected)
            .set("city_growth_yards_busy",cityGrowthYardsBusy)
            .set("city_growth_dedicated_yard",dedicatedCityYard)
            .set("city_growth_builder",cityGrowthProtected ? capitalCandidates[capitalChoice].builder : NONE_ID)
            .set("first_factory_capital",firstFactoryCapital).set("first_factory_production_cost",firstFactoryProduction)
            .set("existing_military_capacity",existingMilitaryCapacity).set("military_value",militaryValue)
            .set("military_target",militaryValueLimit).set("military_budget",militaryBudget)
            .set("military_budget_source",militaryBudgetOverridden ? "override_rolling_headroom" : "configured")
            .set("unit_count_override",getGameInitSettings().getGameOptions().maximumNumberOfUnitsOverride)
            .set("queued_military_value",queuedMilitaryValue)
            .set("workers",itemCount[Unit_Harvester]).set("worker_target",desiredWorkers)
            .set("carryalls",getHouse()->getNumItems(Unit_Carryall)).set("carryalls_committed",itemCount[Unit_Carryall])
            .set("carryall_target",transportTarget).set("funded_army_target",vehiclePlanValue)
            .set("carryall_baseline",transportBaseline).set("carryalls_busy",busyTransports)
            .set("pickup_waiting",int(pickupQueue.size())).set("combat_vehicles",combatVehicles)
            .set("repair_baseline",repairBaseline).set("repair_target",repairTarget)
            .set("repair_bays",getHouse()->getNumItems(Structure_RepairYard)).set("repair_bays_busy",busyRepairBays)
            .set("repair_waiting",waitingRepairVehicles).set("repair_bays_committed",itemCount[Structure_RepairYard])
            .set("refinery_capacity_target",(itemCount[Unit_Harvester]*workerAnnualIncome()+bayAnnualIncome-1)/std::max(1,bayAnnualIncome))
            .set("refineries_committed",itemCount[Structure_Refinery]).set("refinery_waiting",waitingHarvesters)
            .set("refinery_unbooked_bays",unbookedRefineries).set("refinery_field_returners_blocked",fieldReturnersBlocked)
            .set("refinery_returns_waiting",blockedRefineryReturns)
            .set("refinery_queue_persistent",unloadingBacklog)
            .set("moderate_crime_properties",moderateCrimeProperties).set("dangerous_crime_properties",dangerousCrimeProperties)
            .set("spice_share",spiceShare).set("worker_income",workerAnnualIncome()).set("bay_income",bayAnnualIncome)
            .set("trip_cycles",refineryTripCycles).set("walking_trip_cycles",walkingTripCycles).set("field_risk",refineryFieldRisk)
            .set("ready_yards",readyYards).set("power_required",getHouse()->getPowerRequirement())
            .set("power_produced",getHouse()->getProducedPower()).set("producers",capitalProducers)
            .set("unit_options",capitalUnitOptions).set("candidates",options).set("selected",capitalChoice)
            .set("reason",cityGrowthProtected ? "protect_demanded_city_growth" : !sharedSpending ? "funded_parallel_production" : capitalChoice<0
                ? "no_useful_available_purchase" : "highest_marginal_priority"));
    }

    phaseScope.next("ai.build.orders");
    // Give city construction first access to this pass's planning budget.
    // Air gets its allocation before ground production, then light precedes heavy overflow.
    // Stable ordering keeps peers deterministic.
    // Keep IDs, not pointers: an earlier yard may demolish a later zone.
    std::vector<std::pair<int, Uint32>> planningOrder;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse()) {
            int priority=QuantBotBuildPolicy::productionPlanningPriority(citySimEnabled,
                structure->getItemID(), structure->getItemID() == Structure_ConstructionYard
                    && static_cast<const ConstructionYard*>(structure)->isWaitingToPlace(), needsFirstTransport());
            // Compound the helper's income before optional construction/army
            // spending. The port still checks funds, stock and existing orders.
            if (campaignEconomyPush && itemCount[Unit_Harvester]<spiceHarvesterTarget) {
                if (structure->getItemID()==Structure_StarPort) priority=6;
                else if (structure->getItemID()==Structure_HeavyFactory) priority=5;
            }
            if (capitalChoice>=0 && capitalCandidates[capitalChoice].item==Unit_Carryall
                && capitalCandidates[capitalChoice].builder==structure->getObjectID()
                && getHouse()->hasPower()) priority=7;
            planningOrder.emplace_back(priority,structure->getObjectID());
        }
    std::stable_sort(planningOrder.begin(), planningOrder.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    cityReadyYardCount = std::max(1,int(std::count_if(planningOrder.begin(),planningOrder.end(),
        [](const auto& row){return row.first == 3;})));
    if (citySimEnabled) CityPlanningPolicy::rotateYards(planningOrder,getGameCycleCount());
	for (const auto& entry : planningOrder) {
        const auto* pStructure = dynamic_cast<const StructureBase*>(getObject(entry.second));
		if (pStructure && pStructure->getOwner() == getHouse()) {
            // Condition is production, generation, land value and armour, so
            // anything whose output follows its health — every factory, the
            // yard itself, refineries, the whole windtrap family and the city
            // reactor — is repaired as soon as the engine can be paid at all,
            // as is everything the engine's integer formula repairs for free.
            // City mode extends that to the entire colony, walls included,
            // because a damaged footprint lowers its own land value. The rest
            // waits for a comfortable treasury, and survival and engaged
            // emplacements keep their emergency repair on top of all of it.
            if (!pStructure->isRepairing() && pStructure->getHealth() > 0
                && pStructure->getHealth() < pStructure->getMaxHealth()
                && QuantBotBuildPolicy::canAffordRepairTick(getHouse()->getCredits())) {
                const Uint32 repairItem = pStructure->getItemID();
                const bool maintained = repairPolicyMaintains(pStructure);
                const bool survival = pStructure->getHealth() < pStructure->getMaxHealth() * 0.40_fix
                    && money > 1000;
                const bool engaged = QuantBotBuildPolicy::defensiveEmplacement(repairItem)
                    && pStructure->hasATarget();
                const bool wealthy = QuantBotBuildPolicy::repairWhenWealthy(money);
                if (maintained || survival || engaged || wealthy) {
                    doRepair(pStructure);
                    if (AITelemetry::log().enabled())
                        traceDecision("structure_repair", AITelemetry::Record()
                            .set("object", pStructure->getObjectID()).set("item", repairItem)
                            .set("health", pStructure->getHealth().lround())
                            .set("max_health", pStructure->getMaxHealth())
                            .set("credits", getHouse()->getCredits()).set("spendable", money)
                            .set("reason", maintained ? "condition_is_output"
                                : survival ? "survival" : engaged ? "engaged_defence" : "wealth"));
                }
            }

			// Special weapon launch logic (not for support AI)
			if (pStructure->getItemID() == Structure_Palace && !supportMode) {

				const Palace* pPalace = static_cast<const Palace*>(pStructure);
				if (pPalace->isSpecialWeaponReady()) {

					if (houseID != HOUSE_HARKONNEN && houseID != HOUSE_SARDAUKAR) {
						doSpecialWeapon(pPalace);
                        placementCache.clear(); // Palace summons can occupy previously free sites.
					}
					else {
						int enemyHouseID = -1;
						int enemyHouseBuildingCount = 0;

						for (int i = 0; i < NUM_HOUSES; i++) {
							if (getHouse(i) != nullptr) {
								if (getHouse(i)->getTeamID() != getHouse()->getTeamID() && getHouse(i)->getNumStructures() > enemyHouseBuildingCount) {
									enemyHouseBuildingCount = getHouse(i)->getNumStructures();
									enemyHouseID = i;
								}
							}
						}

					if ((enemyHouseID != -1) && (houseID == HOUSE_HARKONNEN || houseID == HOUSE_SARDAUKAR)) {
						Coord target = findBestDeathHandTarget(enemyHouseID);
						if (target.isValid()) {
							doLaunchDeathhand(pPalace, target.x, target.y);
						}
					}
					}
				}
			}

			if (pStructure->isABuilder()) {
				const BuilderBase* pBuilder = static_cast<const BuilderBase*>(pStructure);
                // Retire old-save orders that introduce an unauthored RTS type.
                // Copy the list because cancellation mutates its queue counts.
                if (campaignCityEconomy()) {
                    const auto queued = pBuilder->getBuildList();
                    for (const auto& order : queued)
                        if (!campaignPermitsStructure(order.itemID))
                            for (int n = 0; n < order.num; ++n) doCancelItem(pBuilder, order.itemID);
                }

                planningBuilder = pBuilder->getObjectID();
                clearPlacementCache(false,true);

				// Log all builder status for campaign AIs (not just CY)
				if (gameMode == GameMode::Campaign && !supportMode && pStructure->getItemID() != Structure_ConstructionYard) {
					logDebug("PRODUCTION: %s - Upgrading:%d Queue:%d Credits:%d", 
						getItemNameByID(pStructure->getItemID()).c_str(),
						pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(), money);
				}

                if (emitStatsLog && AITelemetry::log().enabled()) {
                    traceDecision("producer_status", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                        .set("item", pBuilder->getItemID()).set("queue", pBuilder->getProductionQueueSize())
                        .set("current_item", pBuilder->getCurrentProducedItem()).set("hold", pBuilder->isOnHold())
                        .set("upgrading", pBuilder->isUpgrading()).set("waiting_to_place", pBuilder->isWaitingToPlace())
                        .set("health", pBuilder->getHealth().lround()).set("max_health", pBuilder->getMaxHealth())
                        .set("progress_credits", pBuilder->getProductionProgress().lround())
                        .set("unit_limit_blocked", pBuilder->isUnitLimitReached(pBuilder->getCurrentProducedItem()))
                        .set("military_limit_blocked", militaryValue >= militaryBudget)
                        .set("military_budget", militaryBudget)
                        .set("power_deficit", !getHouse()->hasPower())
                        .set("x", pBuilder->getLocation().x).set("y", pBuilder->getLocation().y)
                        .set("credits", getHouse()->getCredits()));
                }
				// Correlate only decisions made for this builder in this planning pass.
                zoneDecisionIds.erase(pBuilder->getObjectID());

                // The one item this builder pass may queue on a production start
                // budget instead of its full price, and the budget it needs. Set
                // only by the growth-cap rules below, and only after live demand,
                // mode, tech, placement, prerequisites, the no-existing-or-pending
                // copy check and a positive income test have all passed. Any other
                // item keeps the shared-cash budget guard unchanged.
                Uint32 installmentProject = NONE_ID;
                int installmentProjectStartBudget = 0;
                bool emergencyFoundationOrder = false;

                // Record actual queue acceptance for unit and structure production.
				auto produceItemWithLogging = [&](Uint32 itemID, int sourceLine, const char* rule = "unit_mix_or_prerequisite") {
                    if(!cityAdmitsStructure(itemID)) return false;
                    if (itemID==Structure_RepairYard && !canAddRepairYard(itemCount[Structure_RepairYard])) return false;
                    // Also cover campaign rebuild orders, which bypass the strategic planner.
                    if (itemID==Structure_Palace && itemCount[Structure_Palace] >=
                        QuantBotBuildPolicy::difficultyPalaceCap(static_cast<int>(difficulty))) return false;
                    if(itemID == Unit_MCV && yardLimit > 0
                       && itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV] >= yardLimit) return false;
                    const int quotedPrice=purchasePrice(pBuilder,itemID);
                    // Enforce reactive infantry and its funded quota at queue acceptance,
                    // including callers outside the WOR selection branch.
                    if (cityInfantryItem(itemID) && (!cityAntiAirInfantryDemand
                        || !infantryQuotaAdmits(data[itemID][houseID].price)
                        || int64_t(quotedPrice) + cityWorkingReserve > money)) return false;
                    if (citySimEnabled && itemID == Structure_WOR
                        && (!cityAntiAirInfantryDemand || !cityTroopContentExists()
                            || openingWorkersNeeded()
                            || int64_t(quotedPrice) + cityWorCashReserve > money)) return false;
                    const bool emergencyGenerator=!getHouse()->hasPower()
                        && (itemID==Structure_WindTrap || itemID==Structure_NuclearPlant
                            || (emergencyFoundationOrder
                                && (itemID==Structure_Slab1 || itemID==Structure_Slab4)));
                    // A validated growth-cap project is charged by the engine in
                    // installments out of arriving income, so the shared cash
                    // budget asks it for its production start budget instead of
                    // its price. The obligation is still booked in full below, so
                    // nothing else spends the same credits.
                    const bool installment = itemID!=NONE_ID && itemID==installmentProject;
                    const int budgetFloor = installment ? installmentProjectStartBudget : quotedPrice;
                    if (!emergencyGenerator && money<budgetFloor) {
                        traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",capitalDecision)
                            .set("builder",pBuilder->getObjectID()).set("item",itemID).set("rule",rule)
                            .set("price",quotedPrice).set("spendable",money)
                            .set("budget_floor",budgetFloor).set("installments",installment)
                            .set("reason",installment ? "installment_start_budget" : "shared_cash_budget"));
                        return false;
                    }

					if (gameMode == GameMode::Campaign && !supportMode && currentGame) {
						std::string itemName = getItemNameByID(itemID);
						logDebug("Queuing %s (ID:%d)", itemName.c_str(), itemID);
					}
                    Coord nuclearSite=Coord::Invalid();
                    if (itemID==Structure_NuclearPlant) {
                        // Campaign rebuild/power orders bypass the general site
                        // reservation path. Recheck and reserve at queue acceptance.
                        placementCache.erase(itemID);
                        nuclearSite=findPlaceLocation(itemID);
                        if (!nuclearSite.isValid()) {
                            traceDecision("construction_rejected",AITelemetry::Record()
                                .set("builder",planningBuilder).set("item",itemID).set("reason","no_nuclear_site"));
                            return false;
                        }
                    }
					const int before = pBuilder->getProductionQueueSize();
                    const auto preOrder = AITelemetry::log().enabled() ? decisionState() : AITelemetry::Record();
					doProduceItem(pBuilder, itemID);
					const bool accepted = pBuilder->getProductionQueueSize() > before;
                    if (accepted) {
                        cityPendingDisplayPopulation += QuantBotCityPolicy::displayPopulation(initialCityPopulation(itemID));
                        if(DuneCity::isCityZoneStructure(itemID)) ++cityZonesIncludingQueued;
                        if (itemID>=Structure_FirstID && itemID<=Structure_LastID)
                            cityStorageCapacityIncludingQueued+=std::max(0,data[itemID][houseID].capacity);
                        money-=quotedPrice;
                        capitalOrderedCost+=quotedPrice;
                        if (AITelemetry::log().enabled()) capitalOrders.set(std::to_string(pBuilder->getObjectID())+":"+std::to_string(before),
                            AITelemetry::Record().set("item",itemID).set("price",quotedPrice).set("rule",rule));
                    }
                    if (accepted && capitalPending() && (itemID==capitalCandidates[capitalChoice].item
                        || (cityGrowthProtected && capitalCandidates[capitalChoice].builder==pBuilder->getObjectID()
                            && DuneCity::isCityZoneStructure(itemID))))
                        capitalConsumed=true;
                    if (accepted && nuclearSite.isValid()) {
                        reservedStructures[planningBuilder]={itemID,nuclearSite};
                        auto& sites=builderPlaceLocations[planningBuilder];
                        if (sites.empty()) sites.push_back(nuclearSite);
                        clearPlacementCache();
                        traceDecision("site_reserved",AITelemetry::Record().set("builder",planningBuilder)
                            .set("item",itemID).set("x",nuclearSite.x).set("y",nuclearSite.y));
                    }
                    if (AITelemetry::log().enabled()) traceDecision("production_order", AITelemetry::Record().set("capital_plan",capitalDecision)
                        .set("builder", pBuilder->getObjectID()).set("builder_item", pBuilder->getItemID())
                        .set("item", itemID).set("item_name", getItemNameByID(itemID)).set("accepted", accepted)
                        .set("source_line", sourceLine).set("rule", rule).set("queue_before", before).set("queue_after", pBuilder->getProductionQueueSize())
                        .set("quoted_price", quotedPrice)
                        .set("zone_decision", itemID >= Structure_ZoneResidential && itemID <= Structure_ZoneIndustrial ? zoneDecisionIds[pBuilder->getObjectID()] : 0)
                        .set("state", preOrder));
					if (!accepted && emitStatsLog) {
						logDebug("PRODUCTION: builder=%u rejected item=%u credits=%d", pBuilder->getObjectID(), itemID, money);
					}
					return accepted;
				};

                // Recover construction on every difficulty before withholding
                // optional economy/civic/capital reserves. Count paid cargo and
                // factory queues as MCVs so another port cannot duplicate it.
                // A sold-out catalogue entry can restock; save rather than spend
                // the recovery cash on workers or military in the meantime.
                const bool starportRecovery = itemCount[Structure_ConstructionYard] == 0
                    && itemCount[Unit_MCV] == 0 && itemCount[Structure_StarPort] > 0
                    && data[Unit_MCV][houseID].enabled
                    && getHouse()->getChoam().getNumAvailable(Unit_MCV) >= 0;
                if (starportRecovery) {
                    if (pBuilder->getItemID() == Structure_StarPort) {
                        const auto* port = static_cast<const StarPort*>(pBuilder);
                        if (port->okToOrder() && port->isAvailableToBuild(Unit_MCV)
                            && getHouse()->getChoam().getNumAvailable(Unit_MCV) > 0) {
                            // Existing factory queues pay gradually. Recovery may
                            // use that still-unspent cash, but never actual money
                            // already charged by another order in this pass.
                            const int heldCash = std::max(0, getHouse()->getCredits() - money);
                            money += heldCash;
                            const bool accepted = produceItemWithLogging(Unit_MCV, __LINE__, "starport_construction_recovery");
                            money -= heldCash;
                            if (accepted) {
                                ++itemCount[Unit_MCV];
                                doPlaceOrder(port);
                            }
                        }
                    }
                    continue;
                }

                auto upgradeWithLogging = [&](int sourceLine) {
                    const int price=pBuilder->getUpgradeCost();
                    const bool affordable=money>=price;
                    const bool accepted=affordable && doUpgrade(pBuilder);
                    if (accepted) { money-=price; capitalOrderedCost+=price; }
                    traceDecision("capital_upgrade",AITelemetry::Record().set("capital_plan",capitalDecision)
                        .set("builder",pBuilder->getObjectID()).set("price",price).set("accepted",accepted)
                        .set("reason",!affordable ? "shared_cash_budget" : accepted ? "technology_unlock" : "unavailable")
                        .set("source_line",sourceLine));
                    return accepted;
                };

                // Campaign reconstruction and power orders do not pass through
                // the standard construction selection below. Give them the same
                // founded queue and per-yard site reservation.
                auto queueFoundedStructure = [&](Uint32 item, int sourceLine,
                                                  const char* rule="campaign_rebuild") {
                    if (pBuilder->getItemID()!=Structure_ConstructionYard
                        || pBuilder->isUpgrading() || pBuilder->getProductionQueueSize()!=0
                        || !campaignAvailableToBuild(pBuilder,item) || !cityAdmitsStructure(item)) return false;
                    planningBuilder=pBuilder->getObjectID();
                    clearPlacementCache(false,true);
                    const Coord site=item==Structure_RocketTurret || item==Structure_GunTurret
                        ? findEffectiveTurretPlaceLocation(item) : findPlaceLocation(item);
                    if (!site.isValid()) return false;
                    // Legacy campaign reconstruction and power orders share the
                    // one foundation rule: no site it cannot prepare in full.
                    const auto plan=foundationPlan(pBuilder,item,site);
                    if(!plan.complete) {
                        traceDecision("construction_rejected",AITelemetry::Record()
                            .set("builder",planningBuilder).set("item",item).set("rule",rule)
                            .set("reason","foundation_incomplete"));
                        return false;
                    }
                    int total=purchasePrice(pBuilder,item);
                    for(const auto& foundation:plan.orders)total+=purchasePrice(pBuilder,foundation.item);
                    if(money<total) return false;
                    auto& places=builderPlaceLocations[planningBuilder];
                    places.clear();
                    // Concrete this building will not stand on is wasted cash.
                    // If any part of the sequence is refused, give it all back.
                    auto abandonFoundation=[&](size_t laid) {
                        for(size_t i=0;i<laid;++i) doCancelItem(pBuilder,plan.orders[i].item);
                        places.clear();
                        return false;
                    };
                    for(size_t i=0;i<plan.orders.size();++i) {
                        if(!produceItemWithLogging(plan.orders[i].item,sourceLine,"building_foundation"))
                            return abandonFoundation(i);
                        places.push_back(Coord(plan.orders[i].x,plan.orders[i].y));
                    }
                    const auto priorSites=places.size();
                    if(!produceItemWithLogging(item,sourceLine,rule))
                        return abandonFoundation(plan.orders.size());
                    if(places.size()==priorSites)places.push_back(site); // Nuclear may reserve in the acceptance wrapper.
                    reservedStructures[planningBuilder]={item,site};
                    clearPlacementCache();
                    return true;
                };

                // Bulk concrete comes before anything optional this yard could
                // buy, in every mode and at every difficulty, as soon as the
                // technology allows it. Every ordinary building now waits for a
                // full foundation, and the 2x2 slab is one order where Slab1
                // needs four — so income, reserve, power and defence
                // preconditions were all gating the unlock behind the buildings
                // it makes cheaper. Only the cost of the upgrade itself remains,
                // and a damaged yard repairs first so it can accept it. A yard
                // that cannot reach the level, or a tree without Slab4, simply
                // keeps founding with single slabs.
                const bool bulkSlabReachable =
                    data[Structure_Slab4][houseID].enabled
                    && data[Structure_Slab4][houseID].techLevel <= currentGame->techLevel
                    && data[Structure_Slab4][houseID].builder == int(Structure_ConstructionYard)
                    && campaignPermitsStructure(Structure_Slab4);
                auto upgradeForConcrete = [&]() {
                    if (pBuilder->getItemID()!=Structure_ConstructionYard
                        || pBuilder->isUpgrading() || pBuilder->getProductionQueueSize()!=0
                        || !QuantBotBuildPolicy::bulkSlabUpgradePending(
                            getGameInitSettings().getGameOptions().concreteRequired, bulkSlabReachable,
                            pBuilder->getCurrentUpgradeLevel(), data[Structure_Slab4][houseID].upgradeLevel,
                            pBuilder->getMaxUpgradeLevel())) return false;
                    // The upgrade is owed and reachable, so this yard spends the
                    // pass on it and on nothing else — waiting for the credits or
                    // for another yard's turn is still the upgrade's pass, not a
                    // licence to buy something cheaper first.
                    const bool anotherUpgrading = std::any_of(capitalBuilders.begin(), capitalBuilders.end(),
                        [&](const auto* other) {
                            return other != pBuilder && other->getItemID() == Structure_ConstructionYard
                                && other->isUpgrading();
                        });
                    const int upgradeCost = pBuilder->getUpgradeCost();
                    auto hold = [&](const char* reason) {
                        traceDecision("yard_upgrade_for_concrete", AITelemetry::Record()
                            .set("builder", pBuilder->getObjectID()).set("accepted", false)
                            .set("cost", upgradeCost).set("spendable", money)
                            .set("level", pBuilder->getCurrentUpgradeLevel())
                            .set("reason", reason));
                    };
                    if (!QuantBotBuildPolicy::upgradeYardForBulkSlab(
                            getGameInitSettings().getGameOptions().concreteRequired, bulkSlabReachable,
                            pBuilder->getCurrentUpgradeLevel(), data[Structure_Slab4][houseID].upgradeLevel,
                            pBuilder->getMaxUpgradeLevel(), money, upgradeCost, anotherUpgrading)) {
                        if (anotherUpgrading) { hold("another_yard_unlocking_bulk_slab"); return true; }
                        hold("saving_for_bulk_slab");
                        return true;
                    }
					if (pBuilder->getHealth() < pBuilder->getMaxHealth()) {
						if (!pBuilder->isRepairing()) doRepair(pBuilder);
						hold("repair_before_bulk_slab");
						return true; // The affordable upgrade owns this pass.
					}
					const bool accepted = upgradeWithLogging(__LINE__);
					traceDecision("yard_upgrade_for_concrete", AITelemetry::Record()
						.set("builder", pBuilder->getObjectID()).set("accepted", accepted)
						.set("cost", upgradeCost).set("spendable", money)
						.set("level", pBuilder->getCurrentUpgradeLevel())
						.set("reason", "unlock_bulk_slab"));
                    return accepted;
                };
                if (upgradeForConcrete()) continue;

                auto queueCampaignWindtrap = [&](int nextDemand) {
                    if (!campaignPowerNeeded(nextDemand) || pBuilder->getItemID()!=Structure_ConstructionYard
                        || pBuilder->isUpgrading() || pBuilder->getProductionQueueSize()!=0
                        || !campaignAvailableToBuild(pBuilder,Structure_WindTrap)) return false;
                    const Coord site=findPlaceLocation(Structure_WindTrap);
                    if (!site.isValid() || money < data[Structure_WindTrap][houseID].price) return false;
                    if (!queueFoundedStructure(Structure_WindTrap,__LINE__,"campaign_required_power")) return false;
                    ++itemCount[Structure_WindTrap];
                    traceDecision("campaign_power",AITelemetry::Record().set("produced",getHouse()->getProducedPower())
                        .set("required",getHouse()->getPowerRequirement()).set("next_demand",nextDemand));
                    return true;
                };
                if (queueCampaignWindtrap(0)) continue;

                // Generation outside the city simulation is an investment, not
                // a function. Vanilla's House::hasPower() answers true whatever
                // the meters say, so this never asks it: it compares the raw
                // requirement, plus the demand of everything already on order,
                // with raw production plus the output that repairs in progress
                // and queued generators will deliver, keeps a reserve on top of
                // that, and puts the remaining package through the documented
                // economics in QuantBotPowerInvestmentPolicy. A base running at
                // 1800 against 1840 is one order away from another shortage, so
                // being exactly powered is not the target. City power rules, the
                // first prerequisite windtrap and the turret-power option are all
                // untouched and still run ahead of this. Custom and legacy
                // campaign games at every difficulty share the one gate.
                auto economicGenerator = [&]() -> Uint32 {
                    if (!vanillaEconomy || supportMode || defendingEconomy
                        || pBuilder->getItemID()!=Structure_ConstructionYard
                        || pBuilder->isUpgrading() || pBuilder->getProductionQueueSize()!=0) return NONE_ID;
                    // Income and whatever production core this tree actually
                    // offers come first; absent technology is not a blocker.
                    auto coreEstablished=[&](Uint32 item) {
                        return !data[item][houseID].enabled
                            || data[item][houseID].techLevel>currentGame->techLevel
                            || getHouse()->getNumItems(item)>0;
                    };
                    if (getHouse()->getNumItems(Structure_Refinery)==0 || getHouse()->getNumItems(Unit_Harvester)==0
                        || !coreEstablished(Structure_LightFactory)
                        || !coreEstablished(Structure_HeavyFactory)) return NONE_ID;
                    // Capacity already paid for: the health a damaged generator
                    // regains under the repair policy, and every queued
                    // generator anywhere in the base. Demand already ordered
                    // counts on the other side of the ledger.
                    QuantBotPowerInvestmentPolicy::PowerTarget target;
                    target.demand=getHouse()->getPowerRequirement();
                    target.produced=getHouse()->getProducedPower();
                    for (const auto* structure:getStructureList()) {
                        if (structure->getOwner()!=getHouse() || structure->getHealth()<=0) continue;
                        const int nominal=-data[structure->getItemID()][structure->getOriginalHouseID()].power;
                        if (nominal<=0 || structure->getMaxHealth()<=0) continue;
                        const int current=(structure->getHealth()*nominal/structure->getMaxHealth()).floor();
                        target.restorable+=std::max(0,nominal-std::min(nominal,current));
                    }
                    for (int item=Structure_FirstID;item<=Structure_LastID;++item) {
                        const int ordered=std::max(0,itemCount[item]-getHouse()->getNumItems(item));
                        if (ordered<=0) continue;
                        const int nominal=-data[item][houseID].power;
                        if (nominal>0) target.pendingOutput+=ordered*nominal;
                        else target.queuedDemand+=ordered*std::max(0,data[item][houseID].power);
                    }
                    // Every generator this yard could actually place, so the
                    // reserve is sized on a real building rather than a guess.
                    // The plain windtrap is the standard unit where the tree has
                    // one; otherwise the smallest available generator is, which
                    // keeps the figure the same for every peer.
                    struct Candidate { Uint32 item; int output; int unitCapital; };
                    std::vector<Candidate> available;
                    for (Uint32 generator:{Uint32(Structure_WindTrap),Uint32(Structure_AdvancedWindTrap),
                            Uint32(Structure_AdvancedWindTrapMK2),Uint32(Structure_AdvancedWindTrapMK3),
                            Uint32(Structure_NuclearPlant)}) {
                        const int output=-data[generator][houseID].power;
                        if (output<=0 || !campaignAvailableToBuild(pBuilder,generator)
                            || !findPlaceLocation(generator).isValid()) continue;
                        available.push_back({generator,output,buildingCapitalCost(generator)});
                    }
                    if (available.empty()) return NONE_ID;
                    for (const auto& candidate:available) {
                        if (candidate.item==Structure_WindTrap) { target.standardOutput=candidate.output; break; }
                        target.standardOutput=target.standardOutput==0
                            ? candidate.output : std::min(target.standardOutput,candidate.output);
                    }
                    // One package at a time, but a pending generator that cannot
                    // reach the target no longer blocks the rest of it: its
                    // output is already counted, so the next pass simply orders
                    // what is still missing.
                    const int deficit=QuantBotPowerInvestmentPolicy::bufferedShortfall(target);
                    const int shortage=QuantBotPowerInvestmentPolicy::operatingShortfall(target);
                    if (deficit<=0) return NONE_ID;
                    // Exactly the repairs a closed deficit stops paying for,
                    // at the engine's own integer per-hitpoint charge.
                    int repairMilli=0;
                    for (const auto* structure:getStructureList())
                        if (structure->getOwner()==getHouse() && structure->getHealth()>0
                            && structure->getItemID()!=Structure_Wall
                            && (repairPolicyMaintains(structure)
                                || QuantBotBuildPolicy::repairWhenWealthy(money)))
                            repairMilli+=QuantBotBuildPolicy::repairCreditsPerHitpointMilli(
                                structure->getMaxHealth(),repairPrice(structure));
                    // Cheapest complete package for a given size, ties broken by
                    // the lowest item id so every peer picks the same building.
                    auto cheapestFor=[&](int size) {
                        QuantBotPowerInvestmentPolicy::GeneratorChoice best;
                        for (const auto& generator:available) {
                            QuantBotPowerInvestmentPolicy::GeneratorChoice candidate;
                            candidate.item=generator.item;
                            candidate.output=generator.output;
                            candidate.unitCapital=generator.unitCapital;
                            candidate.packageCapital=QuantBotPowerInvestmentPolicy::generatorsNeeded(
                                size,generator.output)*candidate.unitCapital;
                            if (QuantBotPowerInvestmentPolicy::preferGenerator(candidate,best)) best=candidate;
                        }
                        return best;
                    };
                    // Forecast recurring income from the current fleet and field;
                    // starting cash is not income, and core queues keep their reserve.
                    auto priced=[&](const QuantBotPowerInvestmentPolicy::GeneratorChoice& choice,int size) {
                        QuantBotPowerInvestmentPolicy::Investment investment;
                        investment.deficit=size;
                        investment.generatorOutput=choice.output;
                        investment.unitCapital=choice.unitCapital;
                        investment.spendable=QuantBotBuildPolicy::spendableCredits(money,economyReserve);
                        investment.netIncome=forecastNetIncome;
                        investment.incomeMinutes=QuantBotSpendingPolicy::horizonMinutes;
                        investment.repairMilliPerHitpoint=repairMilli;
                        return investment;
                    };
                    const auto best=cheapestFor(deficit);
                    if (best.item==NONE_ID) return NONE_ID;
                    const auto investment=priced(best,deficit);
                    // The buffered package is the offer; the bare shortage, with
                    // its own cheapest generator, is the fallback the same gates
                    // judge if the buffered one is refused.
                    const auto relief=cheapestFor(shortage);
                    const auto decision=QuantBotPowerInvestmentPolicy::decide(
                        investment,priced(relief,shortage));
                    const auto verdict=decision.verdict;
                    const bool buy=QuantBotPowerInvestmentPolicy::buys(verdict);
                    const auto& chosen=decision.trimmedToShortage ? relief : best;
                    traceDecision("power_investment",AITelemetry::Record()
                        .set("builder",pBuilder->getObjectID()).set("item",chosen.item)
                        .set("raw_required",getHouse()->getPowerRequirement())
                        .set("raw_produced",getHouse()->getProducedPower())
                        .set("queued_demand",target.queuedDemand)
                        .set("anticipated_demand",QuantBotPowerInvestmentPolicy::anticipatedDemand(target))
                        .set("repair_restored_output",target.restorable)
                        .set("pending_output",target.pendingOutput)
                        .set("effective_capacity",QuantBotPowerInvestmentPolicy::effectiveCapacity(target))
                        .set("standard_generator_output",target.standardOutput)
                        .set("buffer_reserve",QuantBotPowerInvestmentPolicy::bufferReserve(target))
                        .set("buffered_target",QuantBotPowerInvestmentPolicy::bufferedTarget(target))
                        .set("operating_shortage",shortage)
                        .set("buffered_shortfall",deficit)
                        .set("deficit",decision.deficit).set("generator_output",chosen.output)
                        .set("unit_capital",chosen.unitCapital)
                        .set("generators_needed",QuantBotPowerInvestmentPolicy::generatorsNeeded(decision.deficit,chosen.output))
                        .set("package_capital",decision.capital)
                        .set("buffered_package_capital",QuantBotPowerInvestmentPolicy::packageCapital(investment))
                        .set("trimmed_to_shortage",decision.trimmedToShortage)
                        .set("spendable_after_reserve",investment.spendable)
                        .set("economy_reserve",economyReserve)
                        .set("forecast_net_income",forecastNetIncome)
                        .set("forecast_minutes",QuantBotSpendingPolicy::horizonMinutes)
                        .set("maintained_repair_milli_per_hitpoint",repairMilli)
                        .set("avoided_repairs_per_minute",QuantBotPowerInvestmentPolicy::avoidedRepairPerMinute(investment))
                        .set("accepted",buy)
                        .set("reason",QuantBotPowerInvestmentPolicy::describe(verdict)));
                    return buy ? chosen.item : Uint32(NONE_ID);
                };

                // Hard/Brutal establish repair capacity; Medium only replaces an authored
                // yard before optional expansion, including its missing prerequisites.
                // Count queued structures and preserve normal costs/placement rules.
                if (isCampaignGameType(currentGame->gameType)
                    && !(cityGrowthProtected && capitalCandidates[capitalChoice].builder==pBuilder->getObjectID())
                    && !(capitalPending() && capitalCandidates[capitalChoice].item==Unit_Carryall)
                    && (difficulty==Difficulty::Hard || difficulty==Difficulty::Brutal
                        || (difficulty==Difficulty::Medium && initialItemCount[Structure_RepairYard]>0))
                    && data[Structure_RepairYard][houseID].enabled
                    && currentGame->techLevel>=data[Structure_RepairYard][houseID].techLevel
                    && pBuilder->getItemID()==Structure_ConstructionYard
                    && !pBuilder->isUpgrading() && pBuilder->getProductionQueueSize()==0
                    && itemCount[Structure_RepairYard]==0
                    && getHouse()->getNumItems(Structure_Refinery)>0
                    && getHouse()->getNumItems(Unit_Harvester)>0
                    && ((gameMode == GameMode::Campaign && !citySimEnabled && difficulty != Difficulty::Brutal)
                        || !starportMarketAvailable || !data[Structure_StarPort][houseID].enabled
                        || currentGame->techLevel < data[Structure_StarPort][houseID].techLevel
                        || getHouse()->getNumItems(Structure_StarPort)>0)
                    && (getHouse()->hasHeavyFactory() || getHouse()->getNumItems(Structure_StarPort)>0)) {
                    Uint32 repairStep=Structure_RepairYard;
                    if (!campaignAvailableToBuild(pBuilder,repairStep)) {
                        repairStep=NONE_ID;
                        for (int i=Structure_FirstID;i<=Structure_LastID;++i)
                            if (data[Structure_RepairYard][houseID].prerequisiteStructuresSet[i]
                                && itemCount[i]==0 && campaignAvailableToBuild(pBuilder,i)) {repairStep=i;break;}
                    }
                    if (repairStep!=NONE_ID && money>=data[repairStep][houseID].price+300) {
                        const Coord site=findPlaceLocation(repairStep);
                        if (site.isValid() && queueFoundedStructure(repairStep,__LINE__,"campaign_repair_capacity")) {
                            ++itemCount[repairStep];
                            continue;
                        }
                    }
                }

				// Restore the reserved portion after this builder's decisions, keeping
				// charges made by the acceptance wrapper for subsequent builders.
				struct RestoreReservedCredits {
					int& money;
					int reserved;
					~RestoreReservedCredits() { money += reserved; }
                } reserve{money, 0};
                // The factory that has to supply the next yard: either because
                // the rock ran out under the old exhaustion rule, or because
                // the city is built out and has somewhere to settle. Both save
                // for that MCV instead of spending the same cash on units.
                const bool expansionProducer=(rockExpansionNeeded||colonisationNeeded)&&itemCount[Unit_MCV]==0
                    && pBuilder->getItemID()==Structure_HeavyFactory;
                const bool openingWorker = openingWorkersNeeded() && getHouse()->hasPower();
                const bool workerProducer = pBuilder->getItemID() == Structure_HeavyFactory
                    && campaignAvailableToBuild(pBuilder,Unit_Harvester)
                    && (openingWorker || (brutalCityEconomy && factoryPrefersHarvester(pBuilder)));
                const bool firstCarryall = needsFirstTransport() && carryallBuildAvailable && getHouse()->hasPower();
                const bool transportProducer = firstCarryall && pBuilder->getItemID() == Structure_HighTechFactory
                    && campaignAvailableToBuild(pBuilder,Unit_Carryall);
                int protectedCash = pBuilder->getItemID() == Structure_ConstructionYard || transportProducer || workerProducer || expansionProducer
                    ? 0 : std::max({strategicReserveCost,economyReserve,civicReserveCost});
                // A cramped start still needs power, income and a factory before
                // it can expand. Don't protect cash for an MCV we cannot build.
                if ((rockExpansionNeeded||colonisationNeeded) && mcvBuildAvailable && itemCount[Unit_MCV]==0 && !expansionProducer)
                    protectedCash=std::max(protectedCash,int(data[Unit_MCV][houseID].price));
                if (!openingWorker && !expansionProducer && pBuilder->getItemID() != Structure_ConstructionYard)
                    protectedCash = std::max(protectedCash,civicReserveCost);
                if (openingWorker && !workerProducer)
                    protectedCash = std::max(protectedCash,data[Unit_Harvester][houseID].price);
                if (firstCarryall && !transportProducer)
                    protectedCash = std::max(protectedCash,data[Unit_Carryall][houseID].price);
                if (nuclearPlan && getHouse()->hasPower() && !powerGenerationPending()
                    && pBuilder->getItemID() != Structure_ConstructionYard && !transportProducer && !workerProducer && !expansionProducer)
                    protectedCash = std::max(protectedCash,data[Structure_NuclearPlant][houseID].price);
                if (pBuilder->getItemID() != Structure_ConstructionYard && !expansionProducer && !transportProducer)
                    protectedCash = std::max(protectedCash,harvesterInvestmentReserve());
                const bool capitalSupplier=capitalPending() && capitalCandidates[capitalChoice].builder==pBuilder->getObjectID();
                if (capitalSupplier && (cityGrowthProtected || protectionCapital || defendingEconomy || harvesterInvestmentReserve()==0
                    || capitalCandidates[capitalChoice].item==Unit_Harvester
                    || capitalCandidates[capitalChoice].item==Unit_Carryall
                    || capitalCandidates[capitalChoice].item==Unit_MCV)) protectedCash=0;
                protectedCash=std::max(protectedCash,capitalReserve(pBuilder->getObjectID()));
                // A no-spice city's seed cash belongs to the yard that can
                // zone. No factory, port or market bargain may spend it on
                // the way there; only construction yards build R/C/I lots, so
                // nothing legitimate is withheld from its owner.
                if (pBuilder->getItemID() != Structure_ConstructionYard)
                    protectedCash = std::max(protectedCash,cityBootstrapReserve());
                // Saving for optional capital must not stop the yard from
                // relieving the storage ceiling that prevents further saving.
                if (pBuilder->getItemID()==Structure_ConstructionYard && storageExpansionNeeded()
                    && campaignAvailableToBuild(pBuilder,Structure_Silo)) protectedCash=0;
                reserve.reserved = money - QuantBotBuildPolicy::spendableCredits(money,protectedCash);
				money -= reserve.reserved;

                if (capitalSupplier && capitalPending() && capitalCandidates[capitalChoice].item==Unit_Carryall
                    && money<capitalCandidates[capitalChoice].price) {
                    traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",capitalDecision)
                        .set("builder",pBuilder->getObjectID()).set("item",Unit_Carryall)
                        .set("price",capitalCandidates[capitalChoice].price).set("spendable",money)
                        .set("reason",itemCount[Unit_Carryall]==0 ? "saving_first_transport" : "saving_transport_capacity"));
                    continue;
                }
                const CapitalCandidate* localUnitChoice=nullptr;
                // A rich opening can fund its MCV now. Do not repeatedly buy
                // workers before the factory gets to unlock that construction.
                const bool unlockingVanillaMcv=vanillaEconomy && gameMode==GameMode::Custom
                    && pBuilder->getItemID()==Structure_HeavyFactory
                    && !campaignAvailableToBuild(pBuilder,Unit_MCV)
                    && pBuilder->getCurrentUpgradeLevel()<pBuilder->getMaxUpgradeLevel()
                    && DuneCity::prioritizeVanillaMcv(money,getHouse()->getNumItems(Unit_Harvester),
                        itemCount[Structure_ConstructionYard],itemCount[Unit_MCV],data[Unit_MCV][houseID].price);
                const bool parallelHeavyProduction=fundedCityProduction
                    && pBuilder->getItemID()==Structure_HeavyFactory;
                if (!transportProducer && !expansionProducer && !unlockingVanillaMcv && !parallelHeavyProduction)
                    for (const auto& candidate:capitalCandidates)
                        if (candidate.builder==pBuilder->getObjectID() && isUnit(candidate.item)
                            && (candidate.item!=Unit_Carryall || itemCount[Unit_Carryall]<transportTarget)
                            && !QuantBotBuildPolicy::militaryItem(candidate.item)
                            && (!capitalSupplier || candidate.item==capitalCandidates[capitalChoice].item)
                            && candidate.score>0 && candidate.price<=money
                            && (!localUnitChoice || candidate.score>localUnitChoice->score)) localUnitChoice=&candidate;
                if (localUnitChoice && pBuilder->getProductionQueueSize()==0
                    && !pBuilder->isUpgrading() && !pBuilder->isOnHold()) {
                    const auto choice=*localUnitChoice;
                    if (produceItemWithLogging(choice.item,__LINE__,capitalSupplier ? "shared_capital_priority" : "parallel_capital_purchase")) {
                        ++itemCount[choice.item];
                        if (QuantBotBuildPolicy::militaryItem(choice.item)) militaryValue+=data[choice.item][houseID].price;
                        if (choice.item==Unit_Harvester) orderedSpiceHarvester=true;
                        if (pBuilder->getItemID()!=Structure_StarPort) continue;
                        // Imports can buy further workers and bargains in the same shipment.
                    }
                } else if (capitalSupplier && isUnit(capitalCandidates[capitalChoice].item)
                    && !QuantBotBuildPolicy::militaryItem(capitalCandidates[capitalChoice].item)
                    && pBuilder->getItemID()!=Structure_StarPort && !transportProducer && !expansionProducer
                    && !unlockingVanillaMcv && !parallelHeavyProduction) continue;

				if (pBuilder->getItemID() != Structure_StarPort && !transportProducer && !workerProducer && !expansionProducer && !pBuilder->isUpgrading() && pBuilder->getProductionQueueSize() < 1
					&& money > 1500) {
					const int customItem = chooseLowPriorityCustomUnit(pBuilder);
					if (customItem != ItemID_Invalid) {
						if (produceItemWithLogging(customItem, __LINE__)) itemCount[customItem]++;

						militaryValue += data[customItem][houseID].price;
						continue;
					}
				}

				switch (pStructure->getItemID()) {

			case Structure_LightFactory: {
				if (!pBuilder->isUpgrading()
					&& money > (citySimEnabled ? 500 : 700)
					&& pBuilder->getProductionQueueSize() < 1
					&& pBuilder->getBuildListSize() > 0
					&& militaryValue < militaryBudget
					&& ((citySimEnabled && itemCount[Structure_HeavyFactory] == 0)
						|| ((!learningUnitMix && lightVehicleCount < 2)
                            || int64_t(lightVehicleValue) * 10000 < int64_t(vehiclePlanValue) * lightVehicleBps))) {

					if (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel() && getHouse()->getCredits() > 1500) {
						upgradeWithLogging(__LINE__);
					}
					else if (!getHouse()->isGroundUnitLimitReached()) {
                        Uint32 itemID = NONE_ID;
                        int64_t bestDeficit = 0;
                        for (size_t i=5; i<8; ++i) {
                            const Uint32 candidate = mixItems[i];
                            if (!campaignAvailableToBuild(pBuilder,candidate)) continue;
                            const auto deficit = UnitMixPolicy::deficit(unitMix[i], vehiclePlanValue,
                                itemCount[candidate], data[candidate][houseID].price);
                            if (deficit > bestDeficit) { bestDeficit = deficit; itemID = candidate; }
                        }
                        // Bootstrap a small sample before combat learning begins.
                        if (itemID == NONE_ID && !learningUnitMix && lightVehicleCount < 2)
                            for (size_t i=5; i<8; ++i)
                                if (campaignAvailableToBuild(pBuilder,mixItems[i])
                                    && (itemID == NONE_ID || itemCount[mixItems[i]] < itemCount[itemID])) itemID = mixItems[i];
                        if (itemID != NONE_ID && int64_t(militaryValue) + data[itemID][houseID].price <= militaryBudget
                            && produceItemWithLogging(itemID, __LINE__, "adaptive_light_mix")) {
                            ++itemCount[itemID];
                            ++lightVehicleCount;
                            lightVehicleValue += data[itemID][houseID].price;
                            militaryValue += data[itemID][houseID].price;

                        }
					}
				}
			} break;

		case Structure_WOR: {
			// A city raises rocket infantry only while a hostile wing is actually
			// observed, and then only inside the ordinary infantry quota, because they
			// supplement the launcher share and the turrets rather than replacing them.
			// The opening bootstrap of three stays a non-city rule: a city has no
			// standing infantry role to bootstrap.
			if ((!citySimEnabled || cityAntiAirInfantryDemand)
				&& !pBuilder->isUpgrading()
				&& pBuilder->getProductionQueueSize() < 1
				&& pBuilder->getBuildListSize() > 0
				&& money > 450
				&& militaryValue < militaryBudget
				&& !getHouse()->isGroundUnitLimitReached()
				&& ((!citySimEnabled && infantryCount < 3)
					|| infantryValue * 100 < std::max(militaryValue, 1) * infantryPercent)) {
				Uint32 itemID = NONE_ID;
				// Budget, cash and quota decide which offers are candidates at all, so a
				// 300-credit squad cannot shadow an affordable single Trooper. `money` is
				// already net of the strategic, economy and capital reserves this builder
				// protects, and the acceptance wrapper charges it for every order placed,
				// so one pass cannot spend the same credits at several WORs; the city's
				// own working reserve is not part of that protection and is withheld
				// here. A finished Unit_Troopers order deploys three Unit_Trooper, so its
				// queued squad price is what both sides of the quota account for.
				for (Uint32 candidate : {Unit_Trooper, Unit_Troopers}) {
					if (!campaignAvailableToBuild(pBuilder,candidate)) continue;
					const int value = data[candidate][houseID].price;
					if (int64_t(militaryValue)+value > militaryBudget) continue;
					if (citySimEnabled
						&& (purchasePrice(pBuilder,candidate) > money - cityWorkingReserve
							|| !infantryQuotaAdmits(value))) continue;
					if (itemID == NONE_ID || itemCount[candidate] < itemCount[itemID]) itemID = candidate;
				}
				if (itemID != NONE_ID && produceItemWithLogging(itemID, __LINE__)) {
					itemCount[itemID]++;
					infantryCount++;
					infantryValue += data[itemID][houseID].price;
					militaryValue += data[itemID][houseID].price;

				}
			}
		} break;

		case Structure_Barracks: {
			if (!citySimEnabled
				&& !pBuilder->isUpgrading()
				&& pBuilder->getProductionQueueSize() < 1
				&& pBuilder->getBuildListSize() > 0
				&& money > 300
				&& militaryValue < militaryBudget
				&& !getHouse()->isGroundUnitLimitReached()
				&& (infantryCount < 3
					|| infantryValue * 100 < std::max(militaryValue, 1) * infantryPercent)) {
				Uint32 itemID = NONE_ID;
				for (Uint32 candidate : {Unit_Soldier, Unit_Infantry}) {
					if (campaignAvailableToBuild(pBuilder,candidate)
						&& (itemID == NONE_ID || itemCount[candidate] < itemCount[itemID])) {
						itemID = candidate;
					}
				}
				if (itemID != NONE_ID && int64_t(militaryValue)+data[itemID][houseID].price<=militaryBudget
                    && produceItemWithLogging(itemID, __LINE__)) {
					itemCount[itemID]++;
					infantryCount++;
					infantryValue += data[itemID][houseID].price;
					militaryValue += data[itemID][houseID].price;

				}
			}
		} break;

                case Structure_HighTechFactory: {
                    const int ornithopterPrice = data[Unit_Ornithopter][houseID].price;
                    const int carryallPrice = data[Unit_Carryall][houseID].price;
                    const int ornithopterValue = ornithopterPrice * itemCount[Unit_Ornithopter];
                    QuantBotBuildPolicy::AirProductionState air;
                    air.busy = pBuilder->getProductionQueueSize() > 0;
                    air.upgrading = pBuilder->isUpgrading();
                    air.airLimit = getHouse()->isAirUnitLimitReached();
                    air.ornithopterAvailable = campaignAvailableToBuild(pBuilder,Unit_Ornithopter);
                    air.carryallAvailable = campaignAvailableToBuild(pBuilder,Unit_Carryall);
                    air.canUpgrade = pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel();
                    air.spendable = money; // Economy/strategic reserves were removed above.
                    air.ornithopterPrice = ornithopterPrice;
                    air.carryallPrice = carryallPrice;
                    air.carryalls = itemCount[Unit_Carryall];
                    air.carryallTarget = transportTarget;
                    air.armyValue = militaryValue;
                    air.armyLimit = militaryBudget;
                    air.vehiclePlanValue = vehiclePlanValue;
                    air.airCommittedValue = ornithopterValue;
                    air.airTargetBps = unitMix[4];
                    const auto decision = QuantBotBuildPolicy::chooseAirProduction(air);
                    if (emitStatsLog && AITelemetry::log().enabled()) {
                        traceDecision("air_production_decision", AITelemetry::Record()
                            .set("builder",pBuilder->getObjectID()).set("reason",decision.reason)
                            .set("credits",getHouse()->getCredits()).set("planning_spendable",money)
                            .set("economy_reserve",economyReserve).set("strategic_reserve",strategicReserveCost)
                            .set("cash_threshold",ornithopterPrice).set("ornithopter_price",ornithopterPrice)
                            .set("air_target_value",(vehiclePlanValue*ornithopterPercent).lround())
                            .set("air_committed_value",ornithopterValue).set("air_target_bps",unitMix[4])
                            .set("military_value",militaryValue).set("military_limit",militaryValueLimit)
                            .set("military_budget",militaryBudget)
                            .set("ornithopter_available",air.ornithopterAvailable)
                            .set("carryall_target",transportTarget).set("carryalls_committed",itemCount[Unit_Carryall])
                            .set("queue",pBuilder->getProductionQueueSize()).set("current_item",pBuilder->getCurrentProducedItem())
                            .set("upgrading",pBuilder->isUpgrading()).set("hold",pBuilder->isOnHold()));
                    }
                    using AirOrder = QuantBotBuildPolicy::AirOrder;
                    if (decision.order == AirOrder::Upgrade) {
                        if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) upgradeWithLogging(__LINE__);
                        else doRepair(pBuilder);
                    } else if (decision.order == AirOrder::Ornithopter || decision.order == AirOrder::Carryall) {
                        const Uint32 item = decision.order == AirOrder::Ornithopter ? Unit_Ornithopter : Unit_Carryall;
                        if (produceItemWithLogging(item, __LINE__, decision.reason)) {
                            ++itemCount[item];

                            if (item == Unit_Ornithopter) militaryValue += ornithopterPrice;
                        }
                    }
                } break;

				case Structure_HeavyFactory: {
					// Log HF status when idle with money (Custom mode diagnostics)
					if (gameMode == GameMode::Custom && emitStatsLog) {
						logDebug("HF=%u: upgrading=%d queue=%d buildList=%d upgLv=%d/%d unitLimit=%d spendable=%d hold=%d military=%d/%d reserve=%d",
							pBuilder->getObjectID(), pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(),
							pBuilder->getBuildListSize(),
							pBuilder->getCurrentUpgradeLevel(), pBuilder->getMaxUpgradeLevel(),
							getHouse()->isGroundUnitLimitReached(), money, pBuilder->isOnHold(),
							militaryValue, militaryValueLimit, strategicReserveCost);
					}
                    const int cityMcvCash = money;
                    if(expansionProducer && !pBuilder->isUpgrading() && pBuilder->getProductionQueueSize()==0
                        && money < (campaignAvailableToBuild(pBuilder,Unit_MCV)
                            ? data[Unit_MCV][houseID].price : pBuilder->getUpgradeCost())) break;
                    const bool prioritizeCityMcv = citySimEnabled && gameMode == GameMode::Custom
                        && (fundedCityProduction || !openingWorkersNeeded() || rockExpansionNeeded)
                        && !getHouse()->isGroundUnitLimitReached()
                        && (expansionProducer || QuantBotBuildPolicy::canFundCityYard(cityMcvCash, data[Unit_MCV][houseID].price,
                            itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV], cityYardTarget, cityWorkingReserve));
                    const bool prioritizeMcv = prioritizeCityMcv || (vanillaEconomy && gameMode == GameMode::Custom
                        && !getHouse()->isGroundUnitLimitReached()
                        && DuneCity::prioritizeVanillaMcv(money, getHouse()->getNumItems(Unit_Harvester),
                            itemCount[Structure_ConstructionYard], itemCount[Unit_MCV], data[Unit_MCV][houseID].price));
                    // A built-out city needs one yard it cannot express as a
                    // capacity shortfall, so the factory may still unlock MCV
                    // production for it.
                    const int mcvShortfall = citySimEnabled
                        ? std::max(colonisationNeeded ? 1 : 0,
                            cityYardTarget - itemCount[Structure_ConstructionYard] - itemCount[Unit_MCV])
                        : DuneCity::vanillaMcvShortfall(money, getHouse()->getNumItems(Unit_Harvester),
                            itemCount[Structure_ConstructionYard], itemCount[Unit_MCV]);
                    if (emitStatsLog && AITelemetry::log().enabled())
                        traceDecision("factory_economy_priority",AITelemetry::Record()
                            .set("builder",pBuilder->getObjectID()).set("harvesters_committed",itemCount[Unit_Harvester])
                            .set("spice_target",citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget)
                            .set("army_value",militaryValue).set("army_target",militaryValueLimit)
                            .set("army_budget",militaryBudget)
                            .set("military_available",canBuildMilitaryVehicle(pBuilder))
                            .set("prefer_harvester",factoryPrefersHarvester(pBuilder))
                            .set("opening_workers_needed",openingWorkersNeeded()).set("spendable",money)
                            .set("protected_cash",reserve.reserved).set("queued_cost",queuedProductionCost));
					// only if the factory isn't busy
					if ((pBuilder->isUpgrading() == false) && (pBuilder->getProductionQueueSize() < 1) && (pBuilder->getBuildListSize() > 0)) {
						// we need a construction yard. Build an MCV if we don't have a starport
						if ((difficulty == Difficulty::Hard || difficulty == Difficulty::Brutal)
							&& itemCount[Unit_MCV] + itemCount[Structure_ConstructionYard] + itemCount[Structure_StarPort] < 1
							&& campaignAvailableToBuild(pBuilder,Unit_MCV)
							&& !getHouse()->isGroundUnitLimitReached()) {
							if (produceItemWithLogging(Unit_MCV, __LINE__)) itemCount[Unit_MCV]++;
						}
                        else if (prioritizeMcv && campaignAvailableToBuild(pBuilder,Unit_MCV)) {
                            if (produceItemWithLogging(Unit_MCV, __LINE__,
                                    citySimEnabled ? "city_cash_construction_capacity" : "cash_construction_capacity")) {
                                ++itemCount[Unit_MCV];
                            }
                        }
                        else if (prioritizeMcv && mcvUpgradesInProgress < mcvShortfall
                            && !campaignAvailableToBuild(pBuilder,Unit_MCV)
                            && pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel()
                            && (citySimEnabled ? cityMcvCash : money) >= pBuilder->getUpgradeCost()
                                + (expansionProducer ? 0 : citySimEnabled ? std::max(1000, cityWorkingReserve) : 1000)) {
                            if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
                                const bool accepted = upgradeWithLogging(__LINE__);
                                if (accepted) {
                                    ++mcvUpgradesInProgress;
                                }
                                traceDecision("mcv_unlock", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                                    .set("accepted", accepted).set("rule", citySimEnabled
                                        ? "city_cash_construction_capacity" : "cash_construction_capacity"));
                            } else if (!pBuilder->isRepairing()) {
                                doRepair(pBuilder);
                            }
                        }
						else if ((citySimEnabled || vanillaEconomy) && !orderedSpiceHarvester
                            && factoryPrefersHarvester(pBuilder)
                            && itemCount[Unit_Harvester] < (vanillaEconomy ? spiceHarvesterTarget : fundedHarvesterTarget)
                            && campaignAvailableToBuild(pBuilder,Unit_Harvester) && !getHouse()->isGroundUnitLimitReached()
                            && money + ((vanillaEconomy || harvesterInvestmentReserve() > 0) ? reserve.reserved : 0) >= data[Unit_Harvester][houseID].price
                                + ((campaignEconomyPush || harvesterInvestmentReserve() > 0) ? 0 : vanillaEconomy ? 1000 : (openingWorkersNeeded() || brutalCityEconomy) ? 0 : data[Unit_Tank][houseID].price)) {
                            const int workerCash=(vanillaEconomy || harvesterInvestmentReserve()>0)
                                ? std::max(0,reserve.reserved-capitalReserve(pBuilder->getObjectID())) : 0;
                            money+=workerCash;
                            const bool accepted=produceItemWithLogging(Unit_Harvester,__LINE__,"spice_economy");
                            money-=workerCash;
                            if (accepted) {
                                ++itemCount[Unit_Harvester];
                                orderedSpiceHarvester = true;
                            }
                        }
						else if ((money > 10000) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
							if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								upgradeWithLogging(__LINE__);
							}
							else {
								doRepair(pBuilder);
							}
						}
						else if (gameMode == GameMode::Custom && !vanillaEconomy && !citySimEnabled
							&& campaignAvailableToBuild(pBuilder,Unit_MCV)
							&& !getHouse()->isGroundUnitLimitReached()
							&& itemCount[Structure_ConstructionYard] + itemCount[Unit_MCV] < std::min(8, money / 4000)) {
                            if (produceItemWithLogging(Unit_MCV, __LINE__, "construction_capacity")) {
                                itemCount[Unit_MCV]++;
                            }
						}
						else if (gameMode == GameMode::Custom
							&& !vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
							&& campaignAvailableToBuild(pBuilder,Unit_Harvester)
							&& !getHouse()->isGroundUnitLimitReached()
							&& itemCount[Unit_Harvester] < militaryValue / 1000
							&& itemCount[Unit_Harvester] < harvesterLimit && lastCalculatedSpice > 0) {
							// In case we get given lots of money, it will eventually run out so we need to be prepared
							// Classic-mode harvester expansion; city spice investment is handled above.
							if (produceItemWithLogging(Unit_Harvester, __LINE__)) itemCount[Unit_Harvester]++;
						}
						else if (!vanillaEconomy && !(currentGame && currentGame->isCitySimEnabled())
                            && lastCalculatedSpice > 0
							&& itemCount[Unit_Harvester] < harvesterLimit
							&& campaignAvailableToBuild(pBuilder,Unit_Harvester)
							&& !getHouse()->isGroundUnitLimitReached()
							&& (money < 2000 || gameMode == GameMode::Campaign)) {
							// Classic-mode recovery; city spice investment is handled above.
							if (produceItemWithLogging(Unit_Harvester, __LINE__)) itemCount[Unit_Harvester]++;
						}
						else if ((money > 500) && (pBuilder->isUpgrading() == false) && (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel())) {
							// Upgrade before military — unlocks MCV(1), Launcher(2), SiegeTank(3)
							if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								upgradeWithLogging(__LINE__);
							}
							else {
								doRepair(pBuilder);
							}
						}
						else if (money > (citySimEnabled || vanillaEconomy ? 500 : 2000)
							&& militaryValue < militaryBudget && !getHouse()->isGroundUnitLimitReached()) {
                            const int specialValue = data[Unit_Devastator][houseID].price * itemCount[Unit_Devastator]
                                + data[Unit_SonicTank][houseID].price * itemCount[Unit_SonicTank]
                                + data[Unit_Deviator][houseID].price * itemCount[Unit_Deviator];
                            // The special class competes for one share, so it
                            // occupies one allocation slot. Which special that
                            // slot buys is decided on each type's own measured
                            // return per credit lost, with a bounded prior so an
                            // untried type is tried rather than starved.
                            //
                            // Previously all three specials were separate
                            // candidates carrying the same committed value and
                            // the same target share, so the outer
                            // largest-deficit comparison saw three identical
                            // deficits and its strict greater-than kept the
                            // first one in array order. The outer share, budget,
                            // affordability and availability rules are unchanged.
                            SpecialUnitPolicy::Candidates specials{};
                            {
                                const std::array<Uint32,SpecialUnitPolicy::kSpecials> specialItems =
                                    {Unit_Devastator, Unit_SonicTank, Unit_Deviator};
                                for (size_t s=0; s<specialItems.size(); ++s) {
                                    const auto item = specialItems[s];
                                    const int price = data[item][houseID].price;
                                    specials[s].item = item;
                                    specials[s].price = price;
                                    specials[s].rewardMilli = getHouse()->getCombatReward(item).total();
                                    specials[s].lossMilli = int64_t(getHouse()->getNumLostItems(item)) * price * 1000;
                                    specials[s].committedValue = int64_t(price) * itemCount[item];
                                    specials[s].available = campaignAvailableToBuild(pBuilder,item);
                                    specials[s].affordable = price > 0 && price <= money
                                        && int64_t(militaryValue) + price <= militaryBudget;
                                }
                            }
                            const int specialChoice = SpecialUnitPolicy::select(specials);
                            const Uint32 specialItem = specialChoice >= 0
                                ? specials[specialChoice].item : Unit_Devastator;
                            const std::array<Uint32,4> types = {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
                                specialItem};
                            const std::array<FixPoint,4> targets = {tankPercent,siegePercent,launcherPercent,
                                specialPercent};
                            std::array<QuantBotBuildPolicy::AllocationCandidate,4> candidates;
                            AITelemetry::Record choices;
                            for (size_t i=0; i<types.size(); ++i) {
                                const auto item = types[i];
                                candidates[i] = {data[item][houseID].price,
                                    i>=3 ? specialValue : data[item][houseID].price * itemCount[item],
                                    (targets[i]*10000).lround(),
                                    i>=3 ? specialChoice>=0 : campaignAvailableToBuild(pBuilder,item)};
                            }
                            const int normalHorizon = vehiclePlanValue;
                            const int expansionHorizon = vehiclePlanValue;
                            for (size_t i=0; i<types.size(); ++i) {
                                const auto item=types[i];
                                const auto& c = candidates[i];
                                choices.set(std::to_string(item), AITelemetry::Record().set("price",c.price)
                                    .set("committed_value",c.committedValue).set("target_bps",c.targetBps)
                                    .set("available",c.available).set("affordable",c.price<=money)
                                    .set("deficit_scaled",int64_t(normalHorizon)*c.targetBps-int64_t(c.committedValue)*10000)
                                    .set("expansion_deficit_scaled",int64_t(expansionHorizon)*c.targetBps-int64_t(c.committedValue)*10000));
                            }
                            int selected = QuantBotBuildPolicy::fundedDeficit(
                                candidates,militaryValue,money,militaryBudget,vehiclePlanValue);
                            const bool expansionFallback = selected < 0;
                            if (expansionFallback) selected = QuantBotBuildPolicy::capacityFill(
                                candidates,militaryValue,money,militaryBudget);
                            // Candidate tables are useful when the choice changes, but logging the
                            // identical no-deficit decision for every idle factory rapidly exhausts
                            // the match capture. Keep a periodic heartbeat for diagnosis.
                            const Uint32 allocationCycle = getGameCycleCount();
                            // The within-special choice is part of the decision,
                            // so a change of special re-emits the heartbeat.
                            const uint64_t allocationSignature = (uint64_t(selected + 1) << 40)
                                | (uint64_t(specialChoice + 1) << 8)
                                | static_cast<uint64_t>(expansionFallback);
                            const auto traceIt = lastHeavyAllocationTrace.find(pBuilder->getObjectID());
                            const bool allocationChanged = traceIt == lastHeavyAllocationTrace.end()
                                || traceIt->second.first != allocationSignature
                                || allocationCycle - traceIt->second.second >= MILLI2CYCLES(30000);
                            if (allocationChanged) {
                                traceDecision("heavy_allocation_decision",AITelemetry::Record()
                                    .set("builder",pBuilder->getObjectID()).set("army_value",militaryValue)
                                    .set("army_limit",militaryBudget).set("configured_army_target",militaryValueLimit)
                                    .set("army_budget_source",militaryBudgetOverridden ? "override_rolling_headroom" : "configured")
                                    .set("horizon_value",normalHorizon)
                                    .set("expansion_horizon",expansionHorizon).set("expansion_fallback",expansionFallback)
                                    .set("spendable",money).set("candidates",choices)
                                    .set("selected",selected<0 ? NONE_ID : types[selected])
                                    .set("special_selected",specialChoice<0 ? NONE_ID : specialItem)
                                    .set("special_reason",SpecialUnitPolicy::selectionReason(specials,specialChoice))
                                    .set("specials",[&]() {
                                        AITelemetry::Record record;
                                        const auto explored=SpecialUnitPolicy::exploredScores(specials);
                                        for (size_t s=0;s<specials.size();++s)
                                            record.set(std::to_string(specials[s].item),AITelemetry::Record()
                                                .set("price",specials[s].price)
                                                .set("reward_milli",specials[s].rewardMilli)
                                                .set("loss_milli",specials[s].lossMilli)
                                                .set("owned_value",specials[s].committedValue)
                                                .set("available",specials[s].available)
                                                .set("affordable",specials[s].affordable)
                                                .set("raw_score",SpecialUnitPolicy::rawScore(specials[s]))
                                                .set("explored_score",explored[s]));
                                        return record;
                                    }())
                                    .set("reason",selected<0 ? "no_affordable_capacity"
                                        : expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit"));
                                lastHeavyAllocationTrace[pBuilder->getObjectID()] = {allocationSignature, allocationCycle};
                            }
                            if (selected>=0 && produceItemWithLogging(types[selected],__LINE__,
                                expansionFallback ? "available_factory_capacity" : "largest_affordable_deficit")) {
                                ++itemCount[types[selected]];

                                militaryValue += candidates[selected].price;
                            }
						}
					}

				} break;

				case Structure_StarPort: {
					const StarPort* pStarPort = static_cast<const StarPort*>(pBuilder);
					if (pStarPort->okToOrder()) {
						const Choam& choam = getHouse()->getChoam();

                        // Economic imports may use the cash held for the economy,
                        // just as factory-built harvesters do. Market discounts are
                        // irrelevant to needed workers and the first transport.
                        int workerTarget = citySimEnabled ? fundedHarvesterTarget : spiceHarvesterTarget;
                        // A bargain worker is a cheap economy upgrade for a human
                        // ally. Fill the permitted fleet instead of stopping at the
                        // normal-price, remaining-spice planning target.
                        const bool bargainWorkers = isCampaignGameType(currentGame->gameType)
                            && isAlliedWithHuman() && lastCalculatedSpice > 0
                            && purchasePrice(pBuilder,Unit_Harvester) > 0
                            && purchasePrice(pBuilder,Unit_Harvester) < data[Unit_Harvester][houseID].price;
                        if (bargainWorkers) {
                            if (getHouse()->getMaxHarvesters() > 0)
                                workerTarget = getHouse()->getMaxHarvesters();
                        }
                        // The shared reservation protects the next construction,
                        // transport or combat order. Spend the remainder on useful
                        // workers without applying a second arbitrary half-cash cap.
                        auto buyEconomicImport = [&](Uint32 item, const char* rule) {
                            const int price = purchasePrice(pBuilder,item);
                            const int portReserve = std::max(capitalReserve(pBuilder->getObjectID()),
                                cityBootstrapReserve());
                            const int cash = money + reserve.reserved - portReserve;
                            if (item==Unit_Harvester && !bargainWorkers && harvesterInvestmentReserve()==0
                                && QuantBotSpendingPolicy::marginalSpice(spiceShare,
                                    std::min(itemCount[item]*workerAnnualIncome(),itemCount[Structure_Refinery]*bayAnnualIncome),
                                    std::min((itemCount[item]+1)*workerAnnualIncome(),itemCount[Structure_Refinery]*bayAnnualIncome),
                                    MILLI2CYCLES(30000),CityEconomyInvestmentPolicy::horizonCycles,
                                    DuneCity::kCyclesPerCityYear)<=price) return false;
                            if (item == Unit_Harvester && getHouse()->getMaxHarvesters() > 0
                                && itemCount[item] >= getHouse()->getMaxHarvesters()) return false;
                            if (price <= 0 || cash < price || choam.getNumAvailable(item) <= 0
                                || !pStarPort->isAvailableToBuild(item)) return false;
                            traceDecision("starport_economy_purchase", AITelemetry::Record()
                                .set("item", item).set("market_price", price)
                                .set("normal_price", data[item][houseID].price)
                                .set("spendable", money).set("economic_cash", cash)
                                .set("reserved_cash", reserve.reserved).set("worker_target", workerTarget));
                            const int availableReserve=std::max(0,reserve.reserved-portReserve);
                            money+=availableReserve;
                            const bool accepted=produceItemWithLogging(item,__LINE__,rule);
                            money-=availableReserve;
                            if (!accepted) return false;
                            ++itemCount[item];
                            // The scoped reserve is restored after this builder;
                            // deducting here charges this order exactly once.

                            return true;
                        };

                        // The priority purchase above imports the colonist. If
                        // it is still unaffordable, preserve this port's savings.
                        if (capitalSupplier && capitalPending()
                            && capitalCandidates[capitalChoice].item==Unit_MCV) break;
                        // A market transport is still only worth its price when
                        // there is something to carry. The capital/factory lane
                        // already refuses one without a working harvesting
                        // fleet or vehicle repair traffic; a discounted import
                        // is the same purchase, so it answers to the same gate.
                        const bool transportUseful = CityEconomyInvestmentPolicy::economicTransportUseful(
                            lastCalculatedSpice,getHouse()->getNumItems(Unit_Harvester),
                            getHouse()->getNumItems(Structure_Refinery),combatVehicles,
                            getHouse()->getNumItems(Structure_RepairYard));
                        if (itemCount[Unit_Carryall] == 0 && transportUseful)
                            buyEconomicImport(Unit_Carryall, "first_economic_transport");
                        while (itemCount[Unit_Harvester] < workerTarget) {
                            if (!buyEconomicImport(Unit_Harvester, "starport_spice_economy")) break;
                        }
                        while (transportUseful && harvesterInvestmentReserve() == 0
                            && itemCount[Unit_Carryall] < transportTarget)
                            if (!buyEconomicImport(Unit_Carryall, "starport_transport_capacity")) break;

                        // Imports are opportunistic: every discounted combat type
                        // is eligible, regardless of the factory composition targets.
                        // Buy the best percentage discount first, with stable item-ID
                        // tie breaking. Keep the military-value budget and normal caps.
                        std::vector<Uint32> bargains;
                        for (const auto& offer : pStarPort->getBuildList()) {
                            const Uint32 unit = offer.itemID;
                            if (!isUnit(unit) || unit == Unit_Harvester || unit == Unit_Carryall
                                || unit == Unit_MCV || data[unit][houseID].price <= 0
                                || purchasePrice(pBuilder,unit) <= 0 || purchasePrice(pBuilder,unit) >= data[unit][houseID].price) continue;
                            bargains.push_back(unit);
                        }
                        std::sort(bargains.begin(), bargains.end(), [&](Uint32 a, Uint32 b) {
                            const int64_t left = int64_t(purchasePrice(pBuilder,a)) * data[b][houseID].price;
                            const int64_t right = int64_t(purchasePrice(pBuilder,b)) * data[a][houseID].price;
                            return left != right ? left < right : a < b;
                        });
                        // A bargain is still optional spending: it may not come
                        // out of the cash a no-spice city needs to start earning.
                        const int bargainReserve = std::max({harvesterInvestmentReserve(),
                            capitalReserve(pBuilder->getObjectID()),cityBootstrapReserve()});
                        for (Uint32 unit : bargains) {
                            const int price = purchasePrice(pBuilder,unit);
                            const int value = data[unit][houseID].price;
                            while (money + reserve.reserved - bargainReserve >= price && choam.getNumAvailable(unit) > 0
                                && int64_t(militaryValue) + value <= militaryBudget
                                && !getHouse()->isUnitLimitReached(unit)) {
                                const int availableReserve=std::max(0,reserve.reserved - bargainReserve);
                                money+=availableReserve;
                                const bool accepted=produceItemWithLogging(unit,__LINE__,"starport_bargain");
                                money-=availableReserve;
                                if (!accepted) break;
                                ++itemCount[unit];

                                militaryValue += value;
                            }
                        }

						doPlaceOrder(pStarPort);
					}

				} break;

				case Structure_ConstructionYard: {

				const ConstructionYard* pConstYard = static_cast<const ConstructionYard*>(pBuilder);

                // Compare the funded next-wave mix with fielded AND queued units.
                // Cash targets must not hide a heavy-unit production bottleneck.
                const int fundedArmy = vehiclePlanValue;
                int heavyValue = 0;
                for (const auto unit : {Unit_Tank, Unit_SiegeTank, Unit_Launcher,
                        Unit_Devastator, Unit_SonicTank, Unit_Deviator})
                    heavyValue += itemCount[unit] * data[unit][houseID].price;
                const int heavyDeficit = std::max(0,
                    (fundedArmy * (tankPercent + siegePercent + launcherPercent + specialPercent)).lround() - heavyValue);
                const int lightDeficit = std::max<int64_t>(0,int64_t(fundedArmy)*lightVehicleBps/10000-lightVehicleValue);
                const bool lightBacklog = !getHouse()->isGroundUnitLimitReached()
                    && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_LightFactory),
                        itemCount[Structure_LightFactory],activeLightFactoryCount,lightDeficit,
                        money,economyReserve,data[Structure_LightFactory][houseID].price);
                const int airDeficit = std::max(0, (fundedArmy * ornithopterPercent).lround()
                    - itemCount[Unit_Ornithopter] * data[Unit_Ornithopter][houseID].price);
                const bool heavyBacklog = !getHouse()->isGroundUnitLimitReached()
                    && QuantBotBuildPolicy::needsProductionLane(getHouse()->getNumItems(Structure_HeavyFactory),
                        itemCount[Structure_HeavyFactory], activeHeavyFactoryCount, heavyDeficit,
                        money, economyReserve, data[Structure_HeavyFactory][houseID].price);
                const bool airBacklog = QuantBotBuildPolicy::needsAirProductionLane(
                    getHouse()->getNumItems(Structure_HighTechFactory), itemCount[Structure_HighTechFactory],
                    activeHighTechFactoryCount, ornithopterCapableFactoryCount, airDeficit,
                    money, economyReserve, data[Structure_HighTechFactory][houseID].price,
                    data[Unit_Ornithopter][houseID].price, militaryBudget-militaryValue,
                    getHouse()->isAirUnitLimitReached());

				// Each yard owns its concrete/structure placement sequence. Sharing
				// a single FIFO lets the faster yard consume the other's locations.
				planningBuilder = pBuilder->getObjectID();
                clearPlacementCache(false,true);
                auto& placeLocations = builderPlaceLocations[pBuilder->getObjectID()];
				if (pBuilder->getProductionQueueSize() == 0) placeLocations.clear();
				if (emitStatsLog) {
					logDebug("PRODUCTION: CY=%u upgrading=%d queue=%d hold=%d credits=%d buildList=%d R/C/I=%d/%d/%d reserve=%u/%d",
						pBuilder->getObjectID(), pBuilder->isUpgrading(), pBuilder->getProductionQueueSize(),
						pBuilder->isOnHold(), money, pBuilder->getBuildListSize(),
						itemCount[Structure_ZoneResidential], itemCount[Structure_ZoneCommercial],
						itemCount[Structure_ZoneIndustrial], strategicReserveItem, strategicReserveCost);
					auto* balanceCity = currentGame ? currentGame->getCitySimulation() : nullptr;
					const int taxIncome = citySimEnabled
						? DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths, balanceCity ? balanceCity->getCityTax() : 7, ownAvgLandValue) / 60 : 0;
					int factoryTarget = QuantBotBuildPolicy::desiredHeavyFactories(citySimEnabled, taxIncome, money, getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount());
                    if (vanillaEconomy) factoryTarget = DuneCity::vanillaFactoryTarget(factoryTarget, getHouse()->getNumItems(Unit_Harvester), money);
					const int tech = currentGame ? currentGame->techLevel : 8;
					const bool policyPrerequisites = citySimEnabled || vanillaEconomy || tech <= 4
						|| (itemCount[Structure_RepairYard] > 0 && (tech <= 6 || itemCount[Structure_IX] > 0));
					const char* factoryReason = !campaignAvailableToBuild(pBuilder,Structure_HeavyFactory) ? "unavailable"
						: money <= std::max(2000, economyReserve + data[Structure_HeavyFactory][houseID].price) ? "cash-reserve"
						: militaryValue >= militaryBudget ? "military-limit"
						: getHouse()->isGroundUnitLimitReached() ? "unit-limit"
						: heavyBacklog && itemCount[Structure_HeavyFactory] < 24 ? "funded-unit-backlog"
						: itemCount[Structure_HeavyFactory] >= factoryTarget ? "target-met"
						: !policyPrerequisites ? "tech-policy" : "expansion-due";
                    traceDecision("builder_status", AITelemetry::Record().set("builder", pBuilder->getObjectID())
                        .set("queue", pBuilder->getProductionQueueSize()).set("hold", pBuilder->isOnHold())
                        .set("upgrading", pBuilder->isUpgrading()).set("heavy_target", factoryTarget)
                        .set("heavy_reason", factoryReason).set("heavy_busy", activeHeavyFactoryCount)
                        .set("heavy_deficit", heavyDeficit).set("air_deficit", airDeficit)
                        .set("heavy_backlog", heavyBacklog).set("air_backlog", airBacklog)
                        .set("light_backlog",lightBacklog).set("light_busy",activeLightFactoryCount).set("light_deficit",lightDeficit)
                        .set("high_tech_busy", activeHighTechFactoryCount)
                        .set("high_tech_air_capable", ornithopterCapableFactoryCount)
                        .set("air_army_room", militaryBudget-militaryValue)
                        .set("high_tech_building_ornithopters", ornithopterFactoryCount)
                        .set("heavy_economy_target", vanillaEconomy ? std::max(1, getHouse()->getNumItems(Unit_Harvester) / 3) : 0)
                        .set("heavy_cash_target", vanillaEconomy ? 1 + std::max(0, money - 10000) / 4000 : 0)
                        .set("heavy_opening_target", vanillaEconomy ? std::min(factoryTarget, 2 * std::clamp(itemCount[Structure_ConstructionYard], 1, 8)) : 0)
                        .set("heavy_losses_2min", recentFactoryLossCount())
                        .set("repair_busy", activeRepairYardCount).set("repair_target",repairTarget)
                        .set("state", decisionState()));
					logDebug("BUILD-BALANCE: CY=%u HF=%d queued=%d busy=%d target=%d reason=%s RY=%d queued=%d busy=%d cap=%d taxPerSec=%d power=%d/%d",
						pBuilder->getObjectID(), getHouse()->getNumItems(Structure_HeavyFactory),
						itemCount[Structure_HeavyFactory] - getHouse()->getNumItems(Structure_HeavyFactory),
						activeHeavyFactoryCount, factoryTarget, factoryReason,
						getHouse()->getNumItems(Structure_RepairYard),
						itemCount[Structure_RepairYard] - getHouse()->getNumItems(Structure_RepairYard),
						activeRepairYardCount, repairTarget,
						taxIncome, getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
				}

					if (!pBuilder->isUpgrading() && getHouse()->getCredits() >= 100 && (pBuilder->getProductionQueueSize() < 1) && pBuilder->getBuildListSize()) {

						// Campaign Build order, iterate through the buildings, if the number that exist
						// is less than the number that should exist, then build the one that is missing

                        // City campaigns need the same economy/civic planner as
                        // city custom games. The classic rebuild list puts zones
                        // behind upgrades and defences and can starve them forever.
						if (!citySimEnabled && gameMode == GameMode::Campaign && difficulty != Difficulty::Brutal) {
							//logDebug("GameMode Campaign.. ");

                            if (storageExpansionNeeded() && getHouse()->hasPower()
                                && campaignAvailableToBuild(pBuilder,Structure_Silo)
                                && findPlaceLocation(Structure_Silo).isValid()
                                && queueFoundedStructure(Structure_Silo,__LINE__,"spice_storage_priority")) {
                                ++itemCount[Structure_Silo];
                                break;
                            }

						for (int i = Structure_FirstID; i <= Structure_LastID; i++) {
							if (itemCount[i] < initialItemCount[i]
								&& campaignAvailableToBuild(pBuilder,i)
								&& findPlaceLocation(i).isValid()
								&& !pBuilder->isUpgrading()
								&& pBuilder->getProductionQueueSize() < 1) {

                                if (queueCampaignWindtrap(std::max(0,data[i][houseID].power))) break;
								logDebug("***CampAI Build itemID: %o structure count: %o, initial count: %o", i, itemCount[i], initialItemCount[i]);
								if (queueFoundedStructure(i, __LINE__)) itemCount[i]++;  // Increment immediately to prevent multiple CYs from building same item
							}
						}

							// If Campaign AI can't build military, let it build up its cash reserves and defenses

							if (pStructure->getHealth() < pStructure->getMaxHealth()) {
								doRepair(pBuilder);
								int health = pStructure->getHealth().lround();
								int maxHealth = pStructure->getMaxHealth();
								logDebug("PRODUCTION: Repairing CY, health: %d/%d", health, maxHealth);
							}
							else if (pBuilder->getCurrentUpgradeLevel() < pBuilder->getMaxUpgradeLevel()
								&& !pBuilder->isUpgrading()
								&& itemCount[Unit_Harvester] >= harvesterLimit
								&& money > 1500) {  // Don't upgrade if low on money (need money for structures/units)

								upgradeWithLogging(__LINE__);
								logDebug("PRODUCTION: Upgrading CY to level %d, credits: %d", pBuilder->getCurrentUpgradeLevel() + 1, money);
							}
							else if ((!getHouse()->hasPower())
								&& pBuilder->getProductionQueueSize() == 0) {
								// Prefer nuclear plant over windtrap
								if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
									&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
                                    if (queueFoundedStructure(Structure_NuclearPlant, __LINE__))
                                        itemCount[Structure_NuclearPlant]++;
									logDebug("***CampAI Build Nuclear Plant: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
								} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
									&& findPlaceLocation(Structure_WindTrap).isValid()) {
									if (queueFoundedStructure(Structure_WindTrap, __LINE__)) itemCount[Structure_WindTrap]++;
									logDebug("***CampAI Build windtrap: power %d/%d", getHouse()->getProducedPower(), getHouse()->getPowerRequirement());
								}
							}
							// Legacy campaign reconstruction shares the economic
							// generation gate: Vanilla's hasPower() above never
							// reports the raw shortage that degrades the base.
							else if (const Uint32 generator = economicGenerator(); generator != NONE_ID) {
								if (queueFoundedStructure(generator, __LINE__, "power_investment"))
									++itemCount[generator];
							}
							else if (money > 3000
								&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
								&& pBuilder->getProductionQueueSize() == 0
								&& (itemCount[Structure_RocketTurret] <
									(itemCount[Structure_Silo] + itemCount[Structure_Refinery]) * 2)
								&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {

								if (queueFoundedStructure(Structure_RocketTurret, __LINE__)) itemCount[Structure_RocketTurret]++;

								logDebug("***CampAI Build A new Rocket turret increasing count to: %d", itemCount[Structure_RocketTurret]);
							}
							// City zone structures for campaign AI — pick by live demand AND ratio.
							else if (currentGame && currentGame->isCitySimEnabled()
								&& money > 200
								&& pBuilder->getProductionQueueSize() == 0
								&& itemCount[Structure_WindTrap] > 0) {
								const int resCount = itemCount[Structure_ZoneResidential];
								const int comCount = itemCount[Structure_ZoneCommercial];
								const int indCount = itemCount[Structure_ZoneIndustrial];
								const Uint32 zoneID = chooseCityZone(pBuilder, false);

								if (zoneID != NONE_ID && campaignAvailableToBuild(pBuilder,zoneID)
									&& findPlaceLocation(zoneID).isValid()) {
									if (queueFoundedStructure(zoneID, __LINE__)) itemCount[zoneID]++;
									logDebug("***CampAI CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d)",
										getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
										ownResValve, ownComValve, ownIndValve);
								}
							}

							// MULTIPLAYER FIX: Use deterministic timer instead of random
							buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
						}
						else {
								// custom AI starts here:

								Uint32 itemID = NONE_ID;
                const char* structureRule = "no_eligible_structure";
                Coord crimeServiceSite = Coord::Invalid();
                // Set when this yard is short of a demanded service's price and
                // the forecast can still reach it. Cheap zoning must not spend
                // that budget, or the service is never ordered at all.
                bool serviceSavingHold = false;
								bool skipRemainingStructureLogic = false;
                // True once a growth-cap rule below has selected its project on a
                // production start budget rather than its full price.
                auto onInstallments = [&](Uint32 item) {
                    return item != Uint32(NONE_ID) && item == installmentProject;
                };
                // Evaluated once for this yard and this pass, then used by both
                // the protected-plot guard below and the civic rule itself, so
                // the bounded site search runs once rather than per rule.
                const CivicPlan civicPlan = planCivicForYard(pBuilder);
                const bool civicClaimsYard = civicPlan.order != NONE_ID || civicPlan.upgrade;
                // Protection that the service evaluation actually found to be an
                // emergency, plus enemy aircraft already over our own buildings,
                // keep their place above required civics. Routine coverage, its
                // saving and a merely high capital score do not: that is what
                // took the yard on pass after pass while the city stayed capped.
                const bool urgentProtection = airEngaged
                    || (capitalPending() && capitalCandidates[capitalChoice].urgent);

                if (storageExpansionNeeded() && getHouse()->hasPower()
                    && campaignAvailableToBuild(pBuilder,Structure_Silo)
                    && money >= buildingCapitalCost(Structure_Silo)
                    && findPlaceLocation(Structure_Silo).isValid()) {
                    itemID=Structure_Silo;
                    structureRule="spice_storage_priority";
                    skipRemainingStructureLogic=true;
                }

                // Honour the growth allocation in the actual yard decision,
                // not just the shared cash reserve. Previously this yard could
                // immediately spend the protected plot's budget on a service.
                // An announced growth cap outranks the protected plot: while the
                // civic is missing the valve for that plot's own demand is pinned
                // at 0, so this allocation would buy a lot that cannot develop and
                // spend the civic's down payment doing it. The civic rule below
                // owns the decision in that case.
                if (itemID==NONE_ID && cityGrowthProtected && capitalPending()
                    && capitalCandidates[capitalChoice].builder==pBuilder->getObjectID()
                    && !civicClaimsYard) {
                    itemID=affordableCityZone(pBuilder,money);
                    if (itemID!=NONE_ID) structureRule="dedicated_city_growth";
                }

				// Skip build order if something is already queued
								if (pBuilder->getProductionQueueSize() > 0) {
									skipRemainingStructureLogic = true;
								}

							// Count enemy ornithopters - use MAXIMUM from a single enemy house, not sum
								int maxEnemyOrnithopters = 0;
								int totalEnemyOrnithopters = 0;
								if (currentGame) {
								for (int i = 0; i < NUM_HOUSES; i++) {
									const House* pHouse = currentGame->getHouse(i);
									if (pHouse && pHouse->getTeamID() != getHouse()->getTeamID()) {
										int houseOrnis = pHouse->getNumItems(Unit_Ornithopter);
										totalEnemyOrnithopters += houseOrnis;
										if (houseOrnis > maxEnemyOrnithopters) {
											maxEnemyOrnithopters = houseOrnis;
										}
									}
									}
								}
								// Counter-air demand, as before. It is no longer the only
									// driver: a base facing a purely ground opponent used to cap
									// itself at the two baseline emplacements for the whole match,
									// because this figure is zero without enemy aircraft.
									const int counterAirTurrets = std::max(maxEnemyOrnithopters * 2, totalEnemyOrnithopters);
									// Demand-scaled enemy-facing battery for Custom Hard/Brutal,
									// gated on the economy that pays for it and bounded to one
									// order per pass below. Zero outside that mode.
									if (recoveryActive() && lastSurveyCycle != getGameCycleCount()) {
										lastSurvey = surveyArmy();
										lastSurveyCycle = getGameCycleCount();
									}
									const int batteryTurrets = frontBatteryGoal(
										RocketTurretPolicy::coverageTurretCap(coverageDemand),
										itemCount[Structure_Refinery], itemCount[Structure_HeavyFactory],
										itemCount[Structure_RepairYard]);
									const int requiredTurrets = std::max(counterAirTurrets, batteryTurrets);
								const bool ixExpected = data[Structure_IX][houseID].enabled
									&& data[Structure_IX][houseID].techLevel <= currentGame->techLevel;
								const bool palaceExpected = data[Structure_Palace][houseID].enabled
									&& data[Structure_Palace][houseID].techLevel <= currentGame->techLevel;
								const bool strategicInfrastructureIncomplete = customStrategicPlanning
									&& ((ixExpected && itemCount[Structure_IX] == 0)
										|| (palaceExpected && itemCount[Structure_Palace] == 0));
								// The interim cap left a large base with two emplacements
								// while optional tech finished, which cannot span it.
								// Scale it with the cover the base actually demands —
								// which now grows with difficulty — while the enemy
								// wing still caps the goal itself.
								const int activeRocketTurretGoal = strategicInfrastructureIncomplete
									? std::min(requiredTurrets, RocketTurretPolicy::coverageTurretCap(coverageDemand))
									: requiredTurrets;

								// Power buffer check for the next turret and city growth.
								// Only applies if rocketTurretsNeedPower is enabled
								auto hasPowerBufferForTurret = [&]() {
									if (!turretPowerRequired) {
										return true; // No power requirement, always allow
									}
									int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
									// Keep the city-specific headroom after powering the turret.
									return powerExcess >= rocketPowerBuffer;
								};

								// CRITICAL: Counter enemy ornithopters ASAP (prep prerequisites if needed)
								// An enemy wing seen elsewhere on the map is a plan, not a
								// present loss: it buys the baseline emplacements and its own
								// prerequisites, then waits behind the production core.
								if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& counterAirTurrets > 0
                                    && routineRocketsAllowed
									&& itemCount[Structure_RocketTurret] < activeRocketTurretGoal) {
								bool hasWindtrap = itemCount[Structure_WindTrap] > 0;
								bool hasRadar = itemCount[Structure_Radar] > 0;

							if (pBuilder->getCurrentUpgradeLevel() < 2) {
							if (pBuilder->getHealth() < pBuilder->getMaxHealth() && !pBuilder->isRepairing()) {
								doRepair(pBuilder);
											logDebug("COUNTER-ORNITHOPTER: Repairing CY before upgrade (level %d)", pBuilder->getCurrentUpgradeLevel());
										} else if (!pBuilder->isUpgrading() && pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
								upgradeWithLogging(__LINE__);
											logDebug("COUNTER-ORNITHOPTER: Upgrading CY (level %d -> %d)", pBuilder->getCurrentUpgradeLevel(), pBuilder->getCurrentUpgradeLevel() + 1);
							}
									} else if (!hasWindtrap && (!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))) {
						itemID = Structure_WindTrap; structureRule = "power";
										logDebug("COUNTER-ORNITHOPTER: Building windtrap prerequisite (enemy ornis: %d)", maxEnemyOrnithopters);
									} else if (!hasRadar && campaignAvailableToBuild(pBuilder,Structure_Radar) && getHouse()->hasPower()) {
						itemID = Structure_Radar; structureRule = "radar_prerequisite";
										logDebug("COUNTER-ORNITHOPTER: Building radar prerequisite (enemy ornis: %d)", maxEnemyOrnithopters);
									} else if (!hasPowerBufferForTurret()) {
										int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
										if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
											&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
											itemID = Structure_NuclearPlant; structureRule = "power";
											logDebug("COUNTER-ORNITHOPTER: Nuclear Plant for turret power (excess: %d)", powerExcess);
										} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
											&& findPlaceLocation(Structure_WindTrap).isValid()) {
											itemID = Structure_WindTrap; structureRule = "power";
											logDebug("COUNTER-ORNITHOPTER: Windtrap for turret power (excess: %d)", powerExcess);
										}
									} else if (campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
							&& hasPowerBufferForTurret()
							            && findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
							itemID = Structure_RocketTurret; structureRule = "rocket_defense";
										logDebug("COUNTER-ORNITHOPTER: Building rocket turret (enemy ornis: %d, target turrets: %d)", maxEnemyOrnithopters, activeRocketTurretGoal);
									}
								}

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
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_WindTrap] == 0 
					&& (!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))) {
						itemID = Structure_WindTrap; structureRule = "power";
					}
				// 1b. Power Deficit Recovery - prefer nuclear plant, fall back to windtrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& (!getHouse()->hasPower() || (turretPowerRequired
                        && itemCount[Structure_RocketTurret] > 0
                        && getHouse()->getProducedPower() < getHouse()->getPowerRequirement()))) {
					int powerDeficit = getHouse()->getPowerRequirement() - getHouse()->getProducedPower();
					if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
						&& money >= data[Structure_NuclearPlant][houseID].price
						&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
						itemID = Structure_NuclearPlant; structureRule = "power";
						logDebug("POWER-RECOVERY: Building Nuclear Plant for power deficit (%d)", powerDeficit);
					} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
						&& findPlaceLocation(Structure_WindTrap).isValid()) {
						itemID = Structure_WindTrap; structureRule = "power";
						logDebug("POWER-RECOVERY: Building windtrap for power deficit (%d)", powerDeficit);
					}
				}
                // 1c. Economic generation outside the city simulation. Ahead of
                // every optional purchase below, behind the first windtrap and
                // the functional recovery above, and only when the documented
                // gate accepts the whole package.
                if (itemID == NONE_ID && !skipRemainingStructureLogic) {
                    const Uint32 generator = economicGenerator();
                    if (generator != NONE_ID) {
                        itemID = generator; structureRule = "power_investment";
                        skipRemainingStructureLogic = true;
                    }
                }
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
                const bool liveAirThreat = protectionCapital && capitalPending()
                    && capitalCandidates[capitalChoice].reason == std::string("air_coverage");
                // Reserve the vault footprint before housing can occupy it.
                // Keeping the zone seed protected after this purchase prevents
                // the first tiny tax receipt from unlocking optional technology.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !liveAirThreat && cityBootstrapNeeded()
                    && CityEconomyInvestmentPolicy::cityStorageMissing(cityStorageCapacityIncludingQueued)
                    && cityStorageItem != NONE_ID) {
                    skipRemainingStructureLogic = true;
                    structureRule = "city_saving_for_income_storage";
                    if (money >= buildingCapitalCost(cityStorageItem)) {
                        itemID = cityStorageItem;
                        structureRule = "city_income_storage";
                    }
                    if (emitStatsLog) traceDecision("city_income_storage", AITelemetry::Record()
                        .set("item",cityStorageItem).set("cost",cityStorageCost)
                        .set("capacity",data[cityStorageItem][houseID].capacity)
                        .set("capacity_including_queued",cityStorageCapacityIncludingQueued)
                        .set("zones_including_queued",cityZonesIncludingQueued)
                        .set("credits",money).set("spice_remaining",lastCalculatedSpice)
                        .set("funded",itemID != NONE_ID));
                }
                if (itemID == NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !liveAirThreat && cityBootstrapNeeded()
                    && cityZonesIncludingQueued < CityEconomyInvestmentPolicy::kBootstrapZoneSeed) {
                    const Uint32 seed = chooseCityZone(pBuilder,true);
                    if (seed != NONE_ID) {
                        skipRemainingStructureLogic = true;
                        structureRule = "city_saving_for_no_spice_bootstrap";
                        const int storageSaving=CityEconomyInvestmentPolicy::cityStorageMissing(
                            cityStorageCapacityIncludingQueued) ? cityStorageCost : 0;
                        if (money >= buildingCapitalCost(seed)+storageSaving) {
                            itemID = seed;
                            structureRule = "city_no_spice_bootstrap";
                        } else if (storageSaving>0 && money>=storageSaving) {
                            // Bank income from the lots already committed before
                            // another lot consumes the vault's remaining cash.
                            skipRemainingStructureLogic=false;
                        }
                        if (emitStatsLog) traceDecision("city_no_spice_bootstrap", AITelemetry::Record()
                            .set("item",seed).set("zones_including_queued",cityZonesIncludingQueued)
                            .set("seed_target",CityEconomyInvestmentPolicy::kBootstrapZoneSeed)
                            .set("tax_base_eighths",ownTaxBaseEighths).set("credits",money)
                            .set("price",purchasePrice(pBuilder,seed))
                            .set("lot_cost",cityBootstrapLotCost).set("reserved",cityBootstrapReserve())
                            .set("spice_remaining",lastCalculatedSpice).set("funded",itemID != NONE_ID));
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
                if (itemID == NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !urgentProtection && civicClaimsYard) {
                    if (civicPlan.upgrade) {
                        const int cost = pBuilder->getUpgradeCost();
                        if (cost > 0 && (money >= cost || cashFlow.projectedCash >= cost)) {
                            serviceSavingHold = true;
                            skipRemainingStructureLogic = true;
                            structureRule = "unlock_demanded_civic";
                            if (money >= cost && !pBuilder->isUpgrading()
                                && pBuilder->getHealth() >= pBuilder->getMaxHealth())
                                upgradeWithLogging(__LINE__);
                        }
                    } else {
                        const Uint32 project = civicPlan.order;
                        const bool isCivic = project == civicPlan.civic;
                        const int price = data[project][houseID].price;
                        const int foundationBudget = std::max(0, buildingCapitalCost(project) - price);
                        const auto funding = CityEconomyInvestmentPolicy::installmentOrder(
                            money, price, foundationBudget, cityBuildTicks(project), forecastNetIncome);
                        if (funding.funded()) {
                            skipRemainingStructureLogic = true;
                            serviceSavingHold = funding.reserve;
                            if (funding.order) {
                                installmentProject = project;
                                // Execution asks only for the installment: the foundation
                                // orders are queued and charged separately before it, so
                                // demanding their cost again here would double count them.
                                installmentProjectStartBudget = CityEconomyInvestmentPolicy::firstInstallment(
                                    price,cityBuildTicks(project));
                            }
                            itemID = funding.order ? project : NONE_ID;
                            structureRule = itemID == NONE_ID
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
                if (civicStructuralNeed != 0) traceDecision("city_civic_demand",
                    AITelemetry::Record().set("builder",pBuilder->getObjectID())
                        .set("structural_need",int(civicStructuralNeed))
                        .set("demand_clipped_now",int(civicDemandClipped))
                        .set("res_pop",ownResPop).set("com_pop",ownComPop)
                        .set("res_demand",ownResValve).set("com_demand",ownComValve)
                        .set("civic",civicPlan.civic).set("planned_order",civicPlan.order)
                        .set("plan_reason",civicPlan.reason).set("yard_upgrade",civicPlan.upgrade)
                        .set("already_ordered",civicPlan.pending)
                        .set("stadium_committed",itemCount[Structure_Stadium])
                        .set("airport_committed",itemCount[Structure_Airport])
                        .set("urgent_protection",urgentProtection)
                        .set("power_headroom",getHouse()->getProducedPower()-getHouse()->getPowerRequirement())
                        .set("spendable",money).set("continuing_income",forecastNetIncome)
                        .set("reserved",civicReserveCost).set("rule",structureRule));
                // Honour the winning service before optional power headroom,
                // opening tech and production expansion. Required civics above and
                // actual blackout recovery stay ahead of it.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && protectionCapital && capitalPending()
                    && capitalCandidates[capitalChoice].builder == pBuilder->getObjectID()) {
                    const auto& service = capitalCandidates[capitalChoice];
                    if (service.kind==std::string("expansion_upgrade")) {
                        skipRemainingStructureLogic=true;serviceSavingHold=true;
                        structureRule="save_expansion_upgrade";
                        if (money>=service.price && pBuilder->getHealth()>=pBuilder->getMaxHealth()
                            && upgradeWithLogging(__LINE__)) {
                            capitalConsumed=true;structureRule="expansion_turret_upgrade";
                        }
                    } else if (campaignAvailableToBuild(pBuilder, service.item) && service.site.isValid()) {
                        itemID = money >= service.price + service.foundationCost ? service.item : NONE_ID;
                        crimeServiceSite = service.site;
                        structureRule = itemID == NONE_ID ? "save_city_protection" : "city_protection";
                        skipRemainingStructureLogic = true;
                    }
                }
                const bool higherPriorityCapital = capitalPending()
                    && capitalCandidates[capitalChoice].builder == pBuilder->getObjectID()
                    && capitalCandidates[capitalChoice].score > 2600;
                // Seed each demanded part of the tax economy once the spice
                // opening is running. Optional upgrades must not monopolize a
                // single yard before residents and jobs can start developing.
                // Active air defence, dangerous crime and blackouts stay above.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !higherPriorityCapital && cityYards == 1 && itemCount[Structure_Refinery] >= 2) {
                    const Uint32 seed = chooseCityZone(pBuilder, true, true);
                    if (seed != NONE_ID) {
                        if (getHouse()->getProducedPower()-getHouse()->getPowerRequirement() >= 24
                            && money >= buildingCapitalCost(seed)) {
                            itemID = seed;
                            structureRule = "opening_city_seed";
                        } else if (getHouse()->getProducedPower()-getHouse()->getPowerRequirement() < 24
                            && !powerGenerationPending()
                            && campaignAvailableToBuild(pBuilder,Structure_WindTrap)
                            && money >= buildingCapitalCost(Structure_WindTrap)
                            && findPlaceLocation(Structure_WindTrap).isValid()) {
                            itemID = Structure_WindTrap;
                            structureRule = "opening_city_power";
                        }
                    }
                }
                // On a rich field, keep compounding a profitable refinery's
                // included worker beyond four bays before optional construction.
                // One housing seed and the first two rockets retain their slots.
                if (itemID==NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !higherPriorityCapital && itemCount[Structure_RocketTurret]>=2
                    && openingFleetIncomplete() && itemCount[Structure_ZoneResidential]>0) {
                    const Uint32 investment=chooseCityEconomy(pBuilder,false);
                    if (investment==Structure_Refinery) {
                        itemID=money>=buildingCapitalCost(investment) ? investment : NONE_ID;
                        structureRule=itemID==NONE_ID ? "save_profitable_spice" : "profitable_spice_expansion";
                        skipRemainingStructureLogic=true;
                        serviceSavingHold=itemID==NONE_ID;
                    }
                }
                // Protect the income/rebuild core before optional tech. Walk the
                // actual mod prerequisites and save their cost, rather than
                // waiting for enemy aircraft to reveal that the yard is unready.
                const bool completingCore=itemCount[Structure_RocketTurret]>=2 && missingCoreInfrastructure!=NONE_ID;
                const bool openingWorkerSupplier = openingSupplierDue || completingCore;
                if (itemID == NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !higherPriorityCapital
                    && ((openingWorkerSupplier && itemCount[Structure_RocketTurret] >= 2)
                        || ((itemCount[Structure_Refinery] >= 3 || airEngaged)
                            && proactiveCoverageShortfall() > 0
                            && data[Structure_RocketTurret][houseID].enabled
                            && data[Structure_RocketTurret][houseID].techLevel <= currentGame->techLevel))) {
                    // Once basic defence exists, unlock the producer that can
                    // fill the opening fleet. Starport upgrades otherwise wait
                    // behind cheap lots even on a rich spice field.
                    const bool supplyingWorkers = openingWorkerSupplier && itemCount[Structure_RocketTurret] >= 2;
                    Uint32 step = completingCore ? missingCoreInfrastructure : supplyingWorkers
                        ? (itemCount[Structure_Refinery]<4 ? Structure_Refinery
                            : starportMarketAvailable ? Structure_StarPort : Structure_HeavyFactory)
                        : Structure_RocketTurret;
                    if (!supplyingWorkers && !hasPowerBufferForTurret()) step = Structure_WindTrap;
                    if (completingCore && data[step][houseID].power>
                        getHouse()->getProducedPower()-getHouse()->getPowerRequirement()) step=Structure_WindTrap;
                    for (int depth=0; depth<Structure_LastID && step!=NONE_ID; ++depth) {
                        if (!data[step][houseID].enabled || data[step][houseID].techLevel>currentGame->techLevel) break;
                        if (step!=Structure_RocketTurret && itemCount[step]>getHouse()->getNumItems(step)) break;
                        if (campaignAvailableToBuild(pBuilder,step)) {
                            int coverage=0;
                            const Coord site=step==Structure_RocketTurret
                                ? findCityTurretPlaceLocation(step,&coverage) : findPlaceLocation(step);
                            if (site.isValid() && (step!=Structure_RocketTurret || coverage>0)) {
                                const int cost=buildingCapitalCost(step);
                                if (money>=cost || cashFlow.projectedCash>=cost) {
                                    itemID=money>=cost ? step : NONE_ID;
                                    if (itemID!=NONE_ID) crimeServiceSite=site;
                                    serviceSavingHold=itemID==NONE_ID && proactiveSavingHold(cost);
                                    skipRemainingStructureLogic=true;
                                    structureRule=supplyingWorkers ? (itemID==NONE_ID ? "save_opening_supplier" : "opening_supplier")
                                        : itemID==NONE_ID ? "save_core_defence" : "core_defence";
                                }
                            }
                            break;
                        }
                        if (pBuilder->getCurrentUpgradeLevel()<data[step][houseID].upgradeLevel
                            && pBuilder->getMaxUpgradeLevel()>=data[step][houseID].upgradeLevel) {
                            const int cost=pBuilder->getUpgradeCost();
                            if (cost>0 && (money>=cost || cashFlow.projectedCash>=cost)) {
                                serviceSavingHold=proactiveSavingHold(cost);skipRemainingStructureLogic=true;
                                structureRule=supplyingWorkers ? "unlock_opening_supplier" : "unlock_core_defence";
                                if (money>=cost && !pBuilder->isUpgrading()
                                    && pBuilder->getHealth()>=pBuilder->getMaxHealth()) upgradeWithLogging(__LINE__);
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
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
                    && itemCount[Structure_Refinery] >= QuantBotBuildPolicy::openingSpiceRefineries(spiceHarvesterTarget)) {
					const int produced  = getHouse()->getProducedPower();
					const int required  = getHouse()->getPowerRequirement();
					const int buffer    = produced - required;
					const int targetBuffer = cityPowerReserve;
					if (required > 0 && buffer < targetBuffer) {
						// Prefer an affordable nuclear plant for capacity; the shared
						// investment policy below retains cheap wind for small starts.
						if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
							&& money >= data[Structure_NuclearPlant][houseID].price
							&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
							itemID = Structure_NuclearPlant; structureRule = "power";
							logDebug("CITY-POWER: Building Nuclear Plant (buffer=%d, target=%d, required=%d)",
									 buffer, targetBuffer, required);
						} else if (((!powerGenerationPending() || !getHouse()->hasPower())
								&& campaignAvailableToBuild(pBuilder,Structure_WindTrap))
							&& findPlaceLocation(Structure_WindTrap).isValid()) {
							// A reactor already in flight normally satisfies the one
							// generator this rule keeps pending. An actual blackout is
							// different: the reactor is charged in installments out of
							// income the blackout itself is destroying, so waiting for
							// it can starve its own funding indefinitely. Cheap wind is
							// buildable now and produceItemWithLogging's emergency
							// generator path pays for it even with the reactor's
							// obligation booked, so recovery never waits for capacity.
							itemID = Structure_WindTrap; structureRule = "power";
							logDebug("CITY-POWER: Building Windtrap (no Nuclear available, buffer=%d, target=%d, blackout=%d)",
									 buffer, targetBuffer, getHouse()->hasPower() ? 0 : 1);
						}
					}
				}
                // Accumulate reactor funds before optional orders consume them.
                // An actual blackout can still buy an immediately affordable windtrap.
                if (nuclearPlan && !(protectionCapital && capitalPending())
                    && getHouse()->hasPower() && !powerGenerationPending()
                    && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant)
                    && (itemID == NONE_ID || itemID == Structure_WindTrap || itemID == Structure_NuclearPlant)
                    && findPlaceLocation(Structure_NuclearPlant).isValid()) {
                    // The house still has power here - this is planned capacity, not
                    // a blackout - so the reactor can be ordered on a down payment and
                    // charged in installments like any other order. An actual shortfall
                    // is answered above, where an immediately affordable windtrap still
                    // wins, so emergency power never waits for a reactor's instalments.
                    const int price = data[Structure_NuclearPlant][houseID].price;
                    const int foundationBudget = std::max(0, buildingCapitalCost(Structure_NuclearPlant) - price);
                    const auto funding = CityEconomyInvestmentPolicy::installmentOrder(
                        money, price, foundationBudget, cityBuildTicks(Structure_NuclearPlant),
                        forecastNetIncome);
                    if (funding.funded()) {
                        skipRemainingStructureLogic = true;
                        serviceSavingHold = funding.reserve;
                        if (funding.order) {
                            installmentProject = Structure_NuclearPlant;
                            // Execution asks only for the installment: the foundation
                            // orders are queued and charged separately before it, so
                            // demanding their cost again here would double count them.
                            installmentProjectStartBudget = CityEconomyInvestmentPolicy::firstInstallment(
                                price,cityBuildTicks(Structure_NuclearPlant));
                        }
                        itemID = funding.order ? Structure_NuclearPlant : NONE_ID;
                        structureRule = itemID == NONE_ID ? "save_nuclear_growth" : "nuclear_growth_investment";
                    }
                    if (emitStatsLog) traceDecision("nuclear_investment",AITelemetry::Record()
                        .set("funded",itemID != NONE_ID).set("cash",money)
                        .set("price",price).set("foundation_budget",foundationBudget)
                        .set("start_budget",CityEconomyInvestmentPolicy::productionStartBudget(
                            price,foundationBudget,cityBuildTicks(Structure_NuclearPlant)))
                        .set("continuing_income",forecastNetIncome).set("saving",funding.reserve)
                        .set("reserve",cityPowerReserve));
                }
                // Restore a lost heavy production line before optional growth.
                // Walk missing prerequisites (e.g. the light factory lost in the
                // same attack); pending buildings prevent duplicate orders.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && itemCount[Structure_HeavyFactory] == 0
                    && getHouse()->getNumLostItems(Structure_HeavyFactory) > 0
                    && data[Structure_HeavyFactory][houseID].enabled
                    && data[Structure_HeavyFactory][houseID].techLevel <= currentGame->techLevel) {
                    Uint32 step = Structure_HeavyFactory;
                    for (int depth = 0; step != NONE_ID && depth < Structure_LastID; ++depth) {
                        if (itemCount[step] > 0) {
                            skipRemainingStructureLogic = true;
                            structureRule = "heavy_recovery_pending";
                            break;
                        }
                        if (campaignAvailableToBuild(pBuilder,step)) {
                            if (findPlaceLocation(step).isValid()) {
                                skipRemainingStructureLogic = true;
                                structureRule = "save_heavy_recovery";
                                if (money >= data[step][houseID].price) {
                                    itemID = step;
                                    structureRule = "heavy_production_recovery";
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
                if (itemID==NONE_ID && !skipRemainingStructureLogic && fundedFactoryOpening) {
                    Uint32 step=Structure_HeavyFactory;
                    for (int depth=0;step!=NONE_ID && depth<Structure_LastID;++depth) {
                        if (itemCount[step]>0) {
                            // A pending prerequisite will complete normally; let
                            // other yards use their own slots for useful growth.
                            break;
                        }
                        if (campaignAvailableToBuild(pBuilder,step)) {
                            if (money>=data[step][houseID].price && findPlaceLocation(step).isValid()) {
                                itemID=step;structureRule="funded_factory_opening";
                                skipRemainingStructureLogic=true;
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
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && !cityBootstrapNeeded()
                    && starportMarketAvailable && !fundedFactoryOpening
                    && data[Structure_StarPort][houseID].enabled
                    && currentGame->techLevel >= data[Structure_StarPort][houseID].techLevel) {
                    Uint32 step = NONE_ID;
                    if (getHouse()->getNumItems(Structure_StarPort) == 0) {
                        const int refineryGoal = std::min(4, mapSpiceHarvesterTarget);
                        step = itemCount[Structure_Refinery] < refineryGoal
                            ? Structure_Refinery : Structure_StarPort;
                    } else if (getHouse()->getNumItems(Structure_RepairYard) == 0
                        && canAddRepairYard(itemCount[Structure_RepairYard])
                        && data[Structure_RepairYard][houseID].enabled
                        && currentGame->techLevel >= data[Structure_RepairYard][houseID].techLevel) {
                        step = Structure_RepairYard;
                    }
                    // Walk missing prerequisites, stopping at a pending build.
                    // This uses each mod's actual tech tree, without adding heavy
                    // or high-tech factories unless that tree requires them.
                    for (int depth = 0; step != NONE_ID && depth < Structure_LastID; ++depth) {
                        if (itemCount[step] > 0 && step != Structure_Refinery) {
                            skipRemainingStructureLogic = true;
                            structureRule = "starport_opening_pending";
                            break;
                        }
                        if (campaignAvailableToBuild(pBuilder,step)) {
                            if (findPlaceLocation(step).isValid()) {
                                skipRemainingStructureLogic = true;
                                structureRule = "starport_opening";
                                if (money >= data[step][houseID].price) itemID = step;
                                // Keep the next worker supplier's savings intact.
                                // Cheap fallback zoning otherwise spends them
                                // every pass and postpones the opening fleet.
                                serviceSavingHold = itemID == NONE_ID && openingFleetIncomplete()
                                    && getHouse()->getNumItems(Structure_StarPort) == 0
                                    && proactiveSavingHold(data[step][houseID].price);
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
				// Low-spice economy: skip additional refineries, pivot to R/I/C zones
				const bool lowSpiceEconomy = (lastCalculatedSpice < 500);
				const bool isCitySim = (currentGame && currentGame->isCitySimEnabled());

                // The first refinery supplies income and unlocks the tech tree.
                // Further processing capacity competes with demanded tax growth.
                const int openingRefineries = QuantBotBuildPolicy::openingSpiceRefineries(spiceHarvesterTarget);
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && spiceEconomyViable
                    && itemCount[Structure_Refinery] == 0
                    && campaignAvailableToBuild(pBuilder,Structure_Refinery)
                    && findPlaceLocation(Structure_Refinery).isValid()) {
                    skipRemainingStructureLogic = true;
                    structureRule = "city_saving_for_spice_opening";
                    if (money >= data[Structure_Refinery][houseID].price) {
                        itemID = Structure_Refinery;
                        structureRule = "city_spice_opening";
                    }
                    if (emitStatsLog) traceDecision("city_spice_opening", AITelemetry::Record()
                        .set("target",openingRefineries).set("refineries",itemCount[Structure_Refinery])
                        .set("spice_share",spiceShare).set("credits",money)
                        .set("price",data[Structure_Refinery][houseID].price).set("funded",itemID != NONE_ID));
                }

                // One demanded residential plot hedges spice income immediately.
                // After that, buy the better return until the small opening
                // economy exists; don't force a commercial/industrial seed.
                const int openingZones = itemCount[Structure_ZoneResidential]
                    + itemCount[Structure_ZoneCommercial] + itemCount[Structure_ZoneIndustrial];
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_Refinery] > 0
                    && itemCount[Structure_HeavyFactory] == 0
                    && ((itemCount[Structure_ZoneResidential] == 0 && ownResValve>0)
                        || ((brutalCityEconomy || openingZones<6) && itemCount[Structure_Refinery]<openingRefineries))) {
                    const Uint32 investment = chooseCityEconomy(pBuilder,true);
                    if (investment != NONE_ID && (!brutalCityEconomy || investment == Structure_Refinery
                        || itemCount[Structure_ZoneResidential] == 0)) {
                        skipRemainingStructureLogic = true;
                        structureRule = "city_saving_for_opening_investment";
                        if (money>=data[investment][houseID].price) {
                            itemID=investment;
                            structureRule="city_opening_investment";
                        }
                    }
                }

                // After the seed economy, save for the first vehicle factory and
                // its available prerequisite. Cheap zoning must not spend that
                // money afresh on every tick. Unavailable/unplaceable tech does
                // not reserve cash, and essential power above still takes priority.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_Refinery] > 0
                    && itemCount[Structure_HeavyFactory] == 0
                    && (itemCount[Structure_ZoneResidential] > 0 || ownResValve<=0)) {
                    for (Uint32 candidate : {Structure_HeavyFactory, Structure_Radar, Structure_LightFactory}) {
                        if (itemCount[candidate] > 0 || !campaignAvailableToBuild(pBuilder,candidate)
                            || !findPlaceLocation(candidate).isValid()) continue;
                        skipRemainingStructureLogic = true;
                        structureRule = "city_saving_for_vehicle_production";
                        if (money >= data[candidate][houseID].price) {
                            itemID = candidate;
                            structureRule = "city_vehicle_production_bootstrap";
                        }
                        if (emitStatsLog) traceDecision("city_bootstrap_reserve", AITelemetry::Record()
                            .set("item",candidate).set("price",data[candidate][houseID].price)
                            .set("credits",money).set("funded",itemID != NONE_ID));
                        break;
                    }
                }

                // Cash already harvested but trapped at unloading bays takes
                // priority over optional civic/defence/zoning investments.
                if(itemID==NONE_ID&&!skipRemainingStructureLogic&&unloadingBacklog) {
                    const Uint32 investment=isCitySim ? chooseCityEconomy(pBuilder,false) : Uint32(Structure_Refinery);
                    if(investment==Structure_Refinery && campaignAvailableToBuild(pBuilder,investment)
                        && findPlaceLocation(investment).isValid()) {
                        skipRemainingStructureLogic=true;
                        itemID=money>=data[investment][houseID].price?investment:NONE_ID;
                        structureRule="city_refinery_capacity";
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
                const bool airThreatSeen = airEngaged;
                // An uncovered yard, refinery or factory is the same kind of
                // present loss as an enemy wing: it does not wait for the
                // opening worker fleet, a heavy factory or a spare zone's price.
                // Deepening the overlap on assets already covered does.
                const int coreShortfall = isCitySim ? proactiveCoverageShortfall() : 0;
                const bool coreDefenceDue = airThreatSeen
                    || (coreShortfall > 0 && uncoveredCoreAssets > 0);
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && !higherPriorityCapital
                    && (coreDefenceDue
                        || (!openingWorkersNeeded() && itemCount[Structure_HeavyFactory] > 0
                            && nonServiceConstructionOrders >= 3))
                    && campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
                    && hasPowerBufferForTurret()
                    && money >= data[Structure_RocketTurret][houseID].price
                        + (coreDefenceDue ? 0 : data[Structure_ZoneResidential][houseID].price)) {
                    int uncoveredWeight=0;
                    const Coord site=findCityTurretPlaceLocation(Structure_RocketTurret,&uncoveredWeight);
                    if(site.isValid() && uncoveredWeight>0) {
                        itemID=Structure_RocketTurret;
                        crimeServiceSite=site;
                        structureRule="base_air_coverage";
                        skipRemainingStructureLogic=true; // Do not replace this funded coverage slot with optional tech.
                        traceDecision("base_air_coverage",AITelemetry::Record()
                            .set("x",site.x).set("y",site.y).set("uncovered_building_weight",uncoveredWeight)
                            .set("core_coverage_shortfall",coreShortfall).set("coverage_tier",coverageTier)
                            .set("coverage_turret_cap",RocketTurretPolicy::coverageTurretCap(coverageDemand))
                            .set("uncovered_core_assets",uncoveredCoreAssets).set("air_engaged",airEngaged)
                            .set("enemy_aircraft",maxEnemyOrnithopters));
                    }
                }

                // Unlock the first transport before extra ground production.
                // Save the actual factory price; high cash thresholds delayed this
                // until 3-5 heavy factories in 638. Never reserve an impossible site.
                const bool firstTransport = needsFirstTransport();
                const bool firstHighTechSite = firstTransport && itemCount[Structure_HighTechFactory] == 0
                    && campaignAvailableToBuild(pBuilder,Structure_HighTechFactory)
                    && findPlaceLocation(Structure_HighTechFactory).isValid();
                const bool holdExtraHeavy = !fundedCityProduction && (openingWorkersNeeded() || (firstTransport
                    && (firstHighTechSite || carryallBuildAvailable || itemCount[Structure_HighTechFactory]
                        > getHouse()->getNumItems(Structure_HighTechFactory))));
                if (itemID == NONE_ID && !skipRemainingStructureLogic && firstHighTechSite) {
                    skipRemainingStructureLogic = true;
                    structureRule = "save_first_transport_factory";
                    if (money >= data[Structure_HighTechFactory][houseID].price) {
                        itemID = Structure_HighTechFactory;
                        structureRule = "first_transport_factory";
                    }
                }

                // Ongoing economic expansion uses the same comparison after
                // essential services and military infrastructure below.
                // Occupied zones are exactly the buildings that supply rebels to
                // an outbreak, so they are the right denominator. Count the
                // moderate band as well: that is the population still climbing
                // towards the dangerous band and the last chance to prevent it.
                int developedZones = 0, dangerousZones = 0, moderateZones = 0;
                if (isCitySim) {
                    const auto* sim = currentGame->getCitySimulation();
                    if (sim && sim->isInitialized()) {
                        for (const auto* structure : getStructureList()) {
                            const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
                            if (!zone || zone->getOwner() != getHouse() || zone->getHealth() <= 0) continue;
                            const Coord p = zone->getLocation();
                            if (!getMap().tileExists(p.x, p.y)
                                || DuneCity::getStructurePopulation(zone,getMap().getTile(p.x,p.y)->getCityZoneDensity()) == 0) continue;
                            ++developedZones;
                            const int zoneCrime = sim->getCrimeRateMap().worldGet(p.x, p.y);
                            if (zoneCrime >= DuneCity::kCrimeDangerousThreshold) ++dangerousZones;
                            else if (zoneCrime >= 128) ++moderateZones;
                        }
                    }
                }
                const unsigned serviceInterval = QuantBotBuildPolicy::crimeServiceOrderInterval(
                    developedZones, dangerousZones, moderateZones);
                bool serviceEvaluated = false;
                // The scorer used to refuse to even look while the till held
                // less than a station, so a demanded service disappeared from
                // the pass and the yard spent the same cash on a cheap plot —
                // the city then never reached the price and crime ran to an
                // outbreak. Evaluate at the service price, then keep the budget
                // when the forecast can actually reach it inside the horizon.
                const int servicePriceFloor = std::max(data[Structure_PoliceStation][houseID].price,
                    data[Structure_RocketTurret][houseID].price);
                auto selectCrimeService = [&](const char* rule) {
                    if (itemID != NONE_ID || skipRemainingStructureLogic || !isCitySim || serviceEvaluated) return;
                    serviceEvaluated = true;
                    const bool selected = selectCityServiceInvestment(pBuilder,
                        std::max(money, servicePriceFloor),
                        serviceInterval > 0, itemID, crimeServiceSite);
                    const int price = selected ? data[itemID][houseID].price : 0;
                    const bool shortOfPrice = selected && money < price;
                    if (shortOfPrice) {
                        serviceSavingHold = dangerousZones>0 && cashFlow.projectedCash >= price;
                        itemID = NONE_ID;
                        crimeServiceSite = Coord::Invalid();
                        structureRule = serviceSavingHold ? "save_city_service" : structureRule;
                        skipRemainingStructureLogic = serviceSavingHold;
                    } else if (selected) structureRule = rule;
                    traceDecision("crime_service_reservation", AITelemetry::Record()
                        .set("rule",rule).set("interval",int(serviceInterval))
                        .set("developed_zones",developedZones).set("dangerous_zones",dangerousZones)
                        .set("moderate_zones",moderateZones)
                        .set("non_service_orders",int(nonServiceConstructionOrders))
                        .set("selected",selected).set("item",int(itemID)).set("price",price)
                        .set("spendable",money).set("projected_cash",cashFlow.projectedCash)
                        .set("saving",serviceSavingHold));
                };
                // A turret that repays its entire cost through land-value tax
                // alone earns an early investment slot when it also reduces
                // existing crime and improves residential/commercial value.
                // Keep cash for harvesters/refineries and a combat unit; cap the
                // frequency so it cannot monopolise the construction yards.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && isCitySim
                    && itemCount[Structure_HeavyFactory] > 0
                    && serviceInterval != 2 // Widespread dangerous crime still gets the strongest service comparison first.
                    && QuantBotBuildPolicy::crimeServiceOrderDue(3, nonServiceConstructionOrders)
                    && selectCityServiceInvestment(pBuilder,
                        std::max(0,money-cityWorkingReserve), false,
                        itemID, crimeServiceSite, true)) {
                    structureRule = "turret_land_value_investment";
                }
                if (QuantBotBuildPolicy::crimeServiceOrderDue(serviceInterval, nonServiceConstructionOrders))
                    selectCrimeService("city_crime_reserved_order");
				// 2. Refinery (if 0) — non-city-sim path; city sim handles refineries above.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& !isCitySim
					&& itemCount[Structure_Refinery] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_Refinery)) {
					itemID = Structure_Refinery; structureRule = "refinery_economy";
				}

                // Establish repairs before more factories/tech consume the opening
                // grant. Count queued yards to avoid duplicate orders from parallel CYs.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && gameMode == GameMode::Custom && canAddRepairYard(itemCount[Structure_RepairYard])
                    && !openingWorkersNeeded()
                    && itemCount[Structure_RepairYard] < repairBaseline
                    && getHouse()->getNumItems(Structure_Refinery) > 0
                    && money >= data[Structure_RepairYard][houseID].price + 1000
                    && campaignAvailableToBuild(pBuilder,Structure_RepairYard)
                    && findPlaceLocation(Structure_RepairYard).isValid()) {
                    itemID = Structure_RepairYard;
                    structureRule = "early_repair_capacity";
                }


				// 3. Refinery (ratio: 1 refinery per 3 harvesters) — non-city-sim only.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& !isCitySim
					&& !lowSpiceEconomy
					&& itemCount[Structure_Refinery] < (vanillaEconomy
                        ? QuantBotBuildPolicy::desiredSpiceRefineries(spiceHarvesterTarget, itemCount[Unit_Harvester])
                        : itemCount[Unit_Harvester] / 3)
			&& campaignAvailableToBuild(pBuilder,Structure_Refinery)
			&& !(gameMode == GameMode::Campaign && (difficulty!=Difficulty::Medium || initialItemCount[Structure_RepairYard]>0) && itemCount[Structure_Refinery] >= 2 && itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
						itemID = Structure_Refinery; structureRule = "refinery_economy";
					}
				// 4. Refinery (< 4, money < 2000) — non-city-sim only.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
				&& !isCitySim
				&& !lowSpiceEconomy
				&& gameMode != GameMode::Campaign
				&& itemCount[Structure_Refinery] < 4
				&& campaignAvailableToBuild(pBuilder,Structure_Refinery)
					&& money < 2000) {
					itemID = Structure_Refinery; structureRule = "refinery_economy";
				}
				// City income gate: in city sim mode, defer military infrastructure
				// (StarPort/Radar/LightFactory) until the city has at least the
				// seed economy in place. Uses zone *count*, not population —
				// zones stay at density 0 (and contribute 0 pop) until growth
				// conditions kick in, so a pop-based gate locks military out
				// indefinitely if the AI hasn't laid roads/supply yet.
				constexpr int kCityIncomeReadyZones = 3;
				const int kCityZoneCount = itemCount[Structure_ZoneResidential]
					+ itemCount[Structure_ZoneCommercial]
					+ itemCount[Structure_ZoneIndustrial];
				const bool cityIncomeReady = !isCitySim || kCityZoneCount >= kCityIncomeReadyZones || money > 3000;

                // One light line is enough until the first heavy line exists.
                // Do not mistake an opening's temporary all-light mix for a need
                // to build several light factories before unlocking tanks.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && itemCount[Structure_LightFactory] > 0
                    && itemCount[Structure_HeavyFactory] == 0
                    && campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)
                    && findPlaceLocation(Structure_HeavyFactory).isValid()) {
                    skipRemainingStructureLogic = true;
                    structureRule = "save_first_heavy";
                    if (money >= data[Structure_HeavyFactory][houseID].price) {
                        itemID = Structure_HeavyFactory;
                        structureRule = "first_heavy_before_expansion";
                    }
                }
                // The high-tech factory and IX unlock the units the extra lanes
                // would build, so a wealthy base finishes them before repeating
                // optional factory expansion. Only tech this yard (or another)
                // can actually order counts, so a locked tree never stalls
                // production, and the first factory is never delayed.
                const bool techProgressionPending = customStrategicPlanning
                    && itemCount[Structure_HeavyFactory] > 0
                    && ((itemCount[Structure_HighTechFactory] == 0
                            && campaignAvailableToBuild(pBuilder,Structure_HighTechFactory))
                        || (itemCount[Structure_IX] == 0 && anyConstructionYardCanBuildIX));
                // Wealthy vanilla openings need parallel MCV/troop production
                // before optional infrastructure. First/expanding refineries above
                // keep their priority; queued factories count towards this target.
                if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && vanillaEconomy && gameMode == GameMode::Custom && !holdExtraHeavy
                    && !techProgressionPending
                    && itemCount[Structure_Refinery] > 0
                    && militaryValue < militaryBudget && !getHouse()->isGroundUnitLimitReached()
                    && campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)) {
                    const int target = DuneCity::vanillaFactoryTarget(
                        QuantBotBuildPolicy::desiredHeavyFactories(false, 0, money,
                            getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount()),
                        getHouse()->getNumItems(Unit_Harvester), money);
                    if (DuneCity::prioritizeVanillaFactory(money, itemCount[Structure_HeavyFactory],
                            itemCount[Structure_ConstructionYard], target)
                        && findPlaceLocation(Structure_HeavyFactory).isValid()) {
                        itemID = Structure_HeavyFactory; structureRule = "cash_factory_expansion";
                    }
                }
                // Unlock one defensive yard before repeat zoning can occupy every
                // construction slot. Other yards keep growing the city in parallel.
                // The upgrade is the prerequisite for any emplacement at all, so
                // demanded core coverage buys it once the first refinery pays the
                // bills rather than after the whole opening worker fleet.
                const int rocketUpgrade=data[Structure_RocketTurret][houseID].upgradeLevel;
                if (itemID==NONE_ID && !skipRemainingStructureLogic && citySimEnabled
                    && !higherPriorityCapital
                    && (coreShortfall>0
                        || (!openingWorkersNeeded() && itemCount[Structure_HeavyFactory]>0
                            && itemCount[Structure_RocketTurret]<2))
                    && data[Structure_RocketTurret][houseID].enabled
                    && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel
                    && pBuilder->getCurrentUpgradeLevel()<rocketUpgrade
                    && pBuilder->getMaxUpgradeLevel()>=rocketUpgrade
                    && money>=pBuilder->getUpgradeCost()+data[Structure_ZoneResidential][houseID].price
                    && std::none_of(capitalBuilders.begin(),capitalBuilders.end(),[&](const auto* other) {
                        return other!=pBuilder && other->getItemID()==Structure_ConstructionYard
                            && (other->isUpgrading() || other->getCurrentUpgradeLevel()>=rocketUpgrade);
                    })) {
                    if (pBuilder->getHealth()<pBuilder->getMaxHealth()) doRepair(pBuilder);
                    else if (upgradeWithLogging(__LINE__)) {
                        skipRemainingStructureLogic=true;
                        traceDecision("city_yard_upgrade",AITelemetry::Record()
                            .set("builder",pBuilder->getObjectID()).set("accepted",true)
                            .set("reason","unlock_base_defence"));
                    }
                }
                // Demanded core coverage also saves for its own prerequisite:
                // the emplacement, or the yard upgrade that unlocks it. Bounded
                // by the forecast, so a city that cannot reach the price keeps
                // growing instead of idling.
                if (isCitySim && coreShortfall>0 && !serviceSavingHold) {
                    const int upgradeStep=pBuilder->getCurrentUpgradeLevel()<rocketUpgrade
                        && pBuilder->getMaxUpgradeLevel()>=rocketUpgrade ? pBuilder->getUpgradeCost() : 0;
                    const int wanted=upgradeStep>0 ? upgradeStep : data[Structure_RocketTurret][houseID].price;
                    if (money<wanted && cashFlow.projectedCash>=wanted
                        && proactiveSavingHold(wanted)
                        && data[Structure_RocketTurret][houseID].enabled
                        && data[Structure_RocketTurret][houseID].techLevel<=currentGame->techLevel)
                        serviceSavingHold=true;
                }
                if (itemID==NONE_ID && !skipRemainingStructureLogic && sharedSpending && !fundedCityProduction) {
                    const CapitalCandidate* choice=nullptr;
                    for (const auto& candidate:capitalCandidates)
                        if (candidate.builder==pBuilder->getObjectID() && candidate.score>0
                            && (!choice || candidate.score>choice->score)) choice=&candidate;
                    if (choice) {
                        Uint32 candidate=choice->item;
                        // Re-evaluate after earlier yards have committed lots/bays.
                        if (citySimEnabled && std::string(choice->kind)=="economy") candidate=chooseCityEconomy(pBuilder,false);
                        if (candidate!=NONE_ID && (!orderedThisTick.count(candidate)
                            || DuneCity::isCityZoneStructure(candidate))) {
                            itemID=money>=data[candidate][houseID].price ? candidate : NONE_ID;
                            if (itemID!=NONE_ID && choice->site.isValid()) crimeServiceSite=choice->site;
                            // A civic allocation that the till cannot cover yet
                            // keeps its budget; the fallback below used to spend
                            // it on a plot every pass, so the price never came.
                            if (itemID==NONE_ID && std::string(choice->kind)=="civic"
                                && cashFlow.projectedCash>=data[candidate][houseID].price
                                && (choice->reason==std::string("crime_prevention")
                                    || proactiveSavingHold(data[candidate][houseID].price)))
                                serviceSavingHold=true;
                            structureRule=itemID==NONE_ID ? "save_shared_capital" : choice->reason==std::string("crime_prevention")
                                ? "city_crime_prevention" : (choice->reason==std::string("uncovered_base")
                                    || choice->reason==std::string("core_coverage")
                                    || choice->reason==std::string("air_coverage"))
                                ? "base_air_coverage" : "shared_capital_investment";
                            skipRemainingStructureLogic=true;
                        }
                    }
                }
                if (itemID==NONE_ID && !skipRemainingStructureLogic && fundedCityProduction
                    && itemCount[Structure_RepairYard]<repairTarget
                    && canAddRepairYard(itemCount[Structure_RepairYard])
                    && campaignAvailableToBuild(pBuilder,Structure_RepairYard)
                    && money>=buildingCapitalCost(Structure_RepairYard)+cashBuffer
                    && findPlaceLocation(Structure_RepairYard).isValid()) {
                    itemID=Structure_RepairYard;structureRule="funded_repair_capacity";
                }
                // Fund combat-air capacity before optional ground-factory expansion.
                // Pending factories prevent duplicate lanes across parallel yards.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && airBacklog
                    && campaignAvailableToBuild(pBuilder,Structure_HighTechFactory)
                    && findPlaceLocation(Structure_HighTechFactory).isValid()) {
                    itemID = Structure_HighTechFactory; structureRule = "air_unit_backlog";
                }
                // Expand saturated light production before optional heavy capacity.
                // Pending factories count, so parallel yards add one lane at a time.
                if (itemID == NONE_ID && !skipRemainingStructureLogic && lightBacklog
                    && (itemCount[Structure_HeavyFactory] > 0
                        || !data[Structure_HeavyFactory][houseID].enabled
                        || data[Structure_HeavyFactory][houseID].techLevel > currentGame->techLevel)
                    && campaignAvailableToBuild(pBuilder,Structure_LightFactory)
                    && findPlaceLocation(Structure_LightFactory).isValid()) {
                    itemID = Structure_LightFactory; structureRule = "light_unit_backlog";
                }
                if (itemID == NONE_ID && !skipRemainingStructureLogic && !holdExtraHeavy && heavyBacklog
                    && itemCount[Structure_HeavyFactory] < 24
                    && campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)
                    && findPlaceLocation(Structure_HeavyFactory).isValid()) {
                    itemID = Structure_HeavyFactory; structureRule = "heavy_unit_backlog";
                }
				// 5. Starport only if this map offers enabled imports (sold-out entries restock).
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_StarPort] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_StarPort)
					&& findPlaceLocation(Structure_StarPort).isValid()
					&& (gameMode != GameMode::Campaign || money > 1000)
                    && starportMarketAvailable) {
					itemID = Structure_StarPort; structureRule = "starport_supply";
				}
				// 6. Radar
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_Radar] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_Radar)
					&& money > 500) {
					itemID = Structure_Radar; structureRule = "radar_prerequisite";
				}
				// 7. Light Factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_LightFactory] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_LightFactory)
					&& money > 500) {
					itemID = Structure_LightFactory; structureRule = "light_production";
				}
				// Custom vanilla invests in vehicle production; retain infantry
				// infrastructure for campaign and other-mod build policies.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
                    && !(vanillaEconomy && gameMode == GameMode::Custom)
					&& itemCount[Structure_Barracks] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_Barracks)
					&& money > 400) {
					itemID = Structure_Barracks; structureRule = "infantry_production";
				}
				// A city builds no infantry barracks for its own sake, but the producer
				// the close-range part of its anti-air answer needs has to exist before
				// anything can be made. An observed hostile wing - and nothing else -
				// unblocks it, behind every economy and progression rule above, behind
				// the opening worker fleet, and only out of cash the city's working
				// reserve and its next zone do not need. A WOR with no enabled, unlocked
				// troop content, or with no room left in the infantry quota, is not worth
				// founding at all.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
                    && !(vanillaEconomy && gameMode == GameMode::Custom)
					&& (!isCitySim || (cityAntiAirInfantryDemand && cityTroopContentExists()
						&& !openingWorkersNeeded()
                        && int64_t(money) >= data[Structure_WOR][houseID].price + cityWorCashReserve))
					&& itemCount[Structure_WOR] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_WOR)
					&& money > 600) {
					itemID = Structure_WOR;
					structureRule = isCitySim ? "city_anti_air_infantry" : "infantry_production";
				}
				// 8. Repair Yard (only if starport or heavy factory exists)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& canAddRepairYard(itemCount[Structure_RepairYard]) && itemCount[Structure_RepairYard] == 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& campaignAvailableToBuild(pBuilder,Structure_RepairYard)
                    && (!isCitySim || (!openingWorkersNeeded()
                        && money >= data[Structure_RepairYard][houseID].price
                            + data[Structure_ZoneResidential][houseID].price))) {
					itemID = Structure_RepairYard; structureRule = "repair_capacity";
					logDebug("Build Repair Yard... money: %d", money);
				}
				// 8a. Upgrade CY to level 2 for rocket turrets
				//     City sim: upgrade early (no repair yard needed) so turrets protect the colony
				//     Non-city: requires repair yard + starport/heavy factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& pBuilder->getCurrentUpgradeLevel() < 2
                    && (!isCitySim || (!openingWorkersNeeded()
                        && money >= pBuilder->getUpgradeCost() + data[Structure_ZoneResidential][houseID].price))
					&& !pBuilder->isUpgrading()
					&& ((currentGame && currentGame->isCitySimEnabled() && money > 500)
						|| (itemCount[Structure_RepairYard] > 0
							&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
							&& itemCount[Structure_RocketTurret] < 2
                    && (!vanillaEconomy || money > economyReserve + data[Structure_RocketTurret][houseID].price)))) {
					if (pBuilder->getHealth() < pBuilder->getMaxHealth() && !pBuilder->isRepairing()) {
						doRepair(pBuilder);
						logDebug("TURRET-PREP: Repairing CY before upgrade (level %d)", pBuilder->getCurrentUpgradeLevel());
					} else if (pBuilder->getHealth() >= pBuilder->getMaxHealth()) {
                        const int upgradeCost = pBuilder->getUpgradeCost();
                        const bool accepted = upgradeWithLogging(__LINE__);
                        if (accepted) {  skipRemainingStructureLogic = true; }
                        traceDecision("city_yard_upgrade", AITelemetry::Record().set("builder",pBuilder->getObjectID())
                            .set("accepted",accepted).set("cost",upgradeCost).set("spendable",money));
						logDebug("TURRET-PREP: Upgrading CY to level %d for rocket turrets", pBuilder->getCurrentUpgradeLevel() + 1);
					}
				}
                // Windtrap damage does not reduce output; repair for survival
                // uses the ordinary building-repair policy, not power recovery.
				// 8b-pre. Build power for turret buffer — prefer nuclear, fall back to windtrap
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& getGameInitSettings().getGameOptions().rocketTurretsNeedPower
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& !hasPowerBufferForTurret()) {
					int powerExcess = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
					if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
						&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
						itemID = Structure_NuclearPlant; structureRule = "power";
						logDebug("TURRET-POWER: Nuclear Plant for turret buffer (excess: %d, need: %d)", powerExcess, rocketPowerBuffer);
					} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
						&& findPlaceLocation(Structure_WindTrap).isValid()) {
						itemID = Structure_WindTrap; structureRule = "power";
						logDebug("TURRET-POWER: Windtrap for turret buffer (excess: %d, need: %d)", powerExcess, rocketPowerBuffer);
					}
				}
				// 8b. Two baseline rocket turrets after repair yard (requires CY level 2)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] > 0
					&& itemCount[Structure_RocketTurret] < 2
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& hasPowerBufferForTurret()
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
					itemID = Structure_RocketTurret; structureRule = "rocket_defense";
					logDebug("INSURANCE: Building baseline rocket turret (%d/2) after repair yard", itemCount[Structure_RocketTurret] + 1);
				}
				// 8c. Counter enemy ornithopters (requires CY level 2).
				//     The enemy-facing battery is deliberately NOT here: this
				//     slot sits ahead of the refinery, factory, tech and city
				//     growth rules below, and a demand-scaled battery placed
				//     here took those slots away from the economy that pays for
				//     it. It has its own rule after the whole economy chain.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_RepairYard] > 0
					&& (itemCount[Structure_StarPort] > 0 || itemCount[Structure_HeavyFactory] > 0)
					&& hasPowerBufferForTurret()
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
					&& money >= data[Structure_RocketTurret][houseID].price
					&& counterAirTurrets > 0
                                    && routineRocketsAllowed
					&& itemCount[Structure_RocketTurret] < activeRocketTurretGoal
					&& findEffectiveTurretPlaceLocation(Structure_RocketTurret).isValid()) {
					itemID = Structure_RocketTurret; structureRule = "rocket_defense";
					logDebug("COUNTER-ORNITHOPTER: Building rocket turret to counter enemy ornithopters");
				}
				// 9. Heavy Factory
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_HeavyFactory] == 0
					&& campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)
					&& money > 500) {
					itemID = Structure_HeavyFactory; structureRule = "heavy_production";
					logDebug("Build first Heavy Factory... money: %d", money);
				}
				// 10. High Tech Factory (first one - after heavy factory)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& cityIncomeReady
					&& itemCount[Structure_HighTechFactory] == 0
                    && !openingWorkersNeeded()
					&& itemCount[Structure_HeavyFactory] > 0
					&& campaignAvailableToBuild(pBuilder,Structure_HighTechFactory)
					&& money > 1000) {
					itemID = Structure_HighTechFactory; structureRule = "air_production";
					logDebug("Build first High Tech Factory... money: %d", money);
				}
                // Buy a prerequisite port at the tech goal's priority, even
                // without imports. Count queued ports across all yards.
				auto starportUnlocks = [&](Uint32 goal) {
					return itemCount[Structure_StarPort] == 0
						&& !orderedThisTick.count(Structure_StarPort)
						&& campaignPermitsStructure(goal)
						&& prerequisiteBlocksBuild(pBuilder,goal,Structure_StarPort)
						&& campaignAvailableToBuild(pBuilder,Structure_StarPort)
						&& money >= data[Structure_StarPort][houseID].price
						&& findPlaceLocation(Structure_StarPort).isValid();
				};
				// 11. House IX (after essential production buildings)
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_IX] == 0
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_HighTechFactory] > 0
					&& itemCount[Structure_RepairYard] > 0
					&& campaignAvailableToBuild(pBuilder,Structure_IX)
					&& money > 1000) {
					itemID = Structure_IX; structureRule = "advanced_tech";
					logDebug("Build IX... money: %d", money);
				}
				if (ixOverdue && !skipRemainingStructureLogic
					&& itemCount[Structure_IX] == 0
					&& stablePower
					&& money > 1000
					&& campaignAvailableToBuild(pBuilder,Structure_IX)
					&& itemID != Structure_IX
					&& itemID != Structure_WindTrap
					&& itemID != Structure_NuclearPlant) {
					itemID = Structure_IX; structureRule = "advanced_tech";
				}
                // Missing prerequisites never start the IX overdue timer.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& itemCount[Structure_IX] == 0
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_HighTechFactory] > 0
					&& itemCount[Structure_RepairYard] > 0
					&& money > 1000
					&& starportUnlocks(Structure_IX)) {
					itemID = Structure_StarPort; structureRule = "starport_prerequisite";
					logDebug("Build Starport as House IX prerequisite... money: %d", money);
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
				if (itemID == NONE_ID && !skipRemainingStructureLogic
								&& !holdExtraHeavy && money > std::max(2000, economyReserve + data[Structure_HeavyFactory][houseID].price) && campaignAvailableToBuild(pBuilder,Structure_HeavyFactory)) {

								int creditsPerSec = 0;
								if (isCitySim) {
									auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
									int tax = citySim ? citySim->getCityTax() : 7;
									int32_t annual = DuneCity::computeAnnualTaxRevenue(ownTaxBaseEighths, tax, ownAvgLandValue);
									creditsPerSec = annual / 60;
								}
								int desiredHFs = QuantBotBuildPolicy::desiredHeavyFactories(isCitySim, creditsPerSec, money, getHouse()->getNumItems(Structure_HeavyFactory), activeHeavyFactoryCount, recentFactoryLossCount());

                                if (vanillaEconomy) desiredHFs = DuneCity::vanillaFactoryTarget(desiredHFs, getHouse()->getNumItems(Unit_Harvester), money);
								const bool needMore = itemCount[Structure_HeavyFactory] < desiredHFs
									&& militaryValue < militaryBudget && !getHouse()->isGroundUnitLimitReached();

								if (needMore) {
									int techLevel = currentGame ? currentGame->techLevel : 8;
									bool prerequisitesMet = false;

									if (isCitySim || vanillaEconomy || techLevel <= 4) {
										// City and vanilla production use the actual tech tree, not an extra IX policy gate.
										prerequisitesMet = true;
									}
									else if (techLevel <= 6) {
										// Tech 5-6: Require Repair Yard
										prerequisitesMet = (itemCount[Structure_RepairYard] >= 1);
									}
									else {
										// Tech 7+: Require both Repair Yard and IX
										prerequisitesMet = (itemCount[Structure_RepairYard] >= 1 && itemCount[Structure_IX] >= 1);
									}

									if (prerequisitesMet) {
										itemID = Structure_HeavyFactory; structureRule = "heavy_production";
										logDebug("PRIORITY Heavy Factory - active: %d  total: %d  money: %d  desired: %d  tech: %d",
											activeHeavyFactoryCount, getHouse()->getNumItems(Structure_HeavyFactory), money, desiredHFs, techLevel);
									}
								}
							}
				// 13. Vanilla refinery ratio. City mode compares capacity and tax returns.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
						&& !lowSpiceEconomy
						&& ((itemCount[Structure_Refinery] * 3.5_fix < itemCount[Unit_Harvester])
					|| (currentGame && currentGame->techLevel < 4))
						&& campaignAvailableToBuild(pBuilder,Structure_Refinery)
						&& !(gameMode == GameMode::Campaign && (difficulty!=Difficulty::Medium || initialItemCount[Structure_RepairYard]>0) && itemCount[Structure_Refinery] >= 2 && itemCount[Structure_RepairYard] == 0 && currentGame && currentGame->techLevel >= 5)) {
						itemID = Structure_Refinery; structureRule = "refinery_economy";
					}
				// 14. Expand repair only when existing capacity is busy and production supports it.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
							&& canAddRepairYard(itemCount[Structure_RepairYard]) && campaignAvailableToBuild(pBuilder,Structure_RepairYard) && money > std::max(2000, economyReserve + data[Structure_RepairYard][houseID].price)
							&& itemCount[Structure_RepairYard]<repairTarget) {
							itemID = Structure_RepairYard; structureRule = "repair_capacity";
							logDebug("Build Repair Yard: have=%d busy=%d cap=%d military=%d", itemCount[Structure_RepairYard], activeRepairYardCount,
								repairTarget, militaryValue);
						}
                selectCrimeService("city_service_investment");
                // Civic turrets use the shared investment comparison above.
				// 17b. Palace (after military infrastructure)
				//       City sim: 1 palace per 30000 population
				{
				const bool palaceAllowed = itemCount[Structure_Palace] < palaceTarget
					&& !orderedThisTick.count(Structure_Palace);
				if (itemID == NONE_ID && !skipRemainingStructureLogic
									&& money > 5000
									&& campaignAvailableToBuild(pBuilder,Structure_Palace)
									&& palaceAllowed
									&& itemCount[Structure_HeavyFactory] > 0
									&& itemCount[Structure_LightFactory] > 0) {
								itemID = Structure_Palace; structureRule = "palace_strategy";
							}
				if (palaceOverdue && !skipRemainingStructureLogic
					&& stablePower
					&& money > 5000
					&& campaignAvailableToBuild(pBuilder,Structure_Palace)
					&& palaceAllowed
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_LightFactory] > 0
					&& itemID != Structure_Palace
					&& itemID != Structure_IX
					&& itemID != Structure_WindTrap
					&& itemID != Structure_NuclearPlant) {
					itemID = Structure_Palace; structureRule = "palace_strategy";
				}
				// Same demand as the palace rules above, for a map whose Palace
				// lists a Starport this base does not own.
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& money > 5000
					&& palaceAllowed
					&& itemCount[Structure_HeavyFactory] > 0
					&& itemCount[Structure_LightFactory] > 0
					&& starportUnlocks(Structure_Palace)) {
					itemID = Structure_StarPort; structureRule = "starport_prerequisite";
					logDebug("Build Starport as Palace prerequisite... money: %d", money);
				}
				}
				// Round out vanilla bases with regular turrets and short wall lines.
				// Fixed count targets keep this deterministic and prevent defence spam.
				if (itemID == NONE_ID && !skipRemainingStructureLogic && !isCitySim
					&& itemCount[Structure_HeavyFactory] > 0 && money > std::max(1000, economyReserve + 1000)
                    && (!vanillaEconomy || itemCount[Unit_Harvester] >= spiceHarvesterTarget)) {
					const int productionBuildings = itemCount[Structure_LightFactory]
						+ itemCount[Structure_HeavyFactory] + itemCount[Structure_Barracks]
						+ itemCount[Structure_WOR];
                    const bool rocketTech = data[Structure_RocketTurret][houseID].enabled
                        && data[Structure_RocketTurret][houseID].techLevel <= currentGame->techLevel;
                    const Uint32 defenceTurret = rocketTech ? Structure_RocketTurret : Structure_GunTurret;
                    const int desiredDefenceTurrets = 1 + productionBuildings / 3;
                    // Short wall lines for the turrets this rule builds, not one pair per
                    // emplacement of a counter-air or front battery: that grew with every
                    // battery turret into a hundred walls on a rich base.
                    const int desiredWalls = 2 + std::min(itemCount[Structure_GunTurret]
                        + itemCount[Structure_RocketTurret], desiredDefenceTurrets) * 2;
                    if (itemCount[defenceTurret] < desiredDefenceTurrets
                        && campaignAvailableToBuild(pBuilder,defenceTurret)
                        && (!rocketTech || hasPowerBufferForTurret())
                        && findEffectiveTurretPlaceLocation(defenceTurret).isValid()) {
                        itemID = defenceTurret;
                        structureRule = rocketTech ? "rocket_defense" : "ground_defense";
					} else if (itemCount[Structure_Wall] < desiredWalls
						&& campaignAvailableToBuild(pBuilder,Structure_Wall)
						&& findPlaceLocation(Structure_Wall).isValid()) {
						itemID = Structure_Wall; structureRule = "ground_defense";
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
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& batteryTurrets > 0
					&& routineRocketsAllowed
					&& itemCount[Structure_RocketTurret] < batteryTurrets
					&& FrontBatteryPolicy::mayOrderThisPass(
						orderedThisTick.count(Structure_RocketTurret) ? 1 : 0)
					&& RocketTurretPolicy::proactiveCoverageTurn(airEngaged, uncoveredCoreAssets,
						nonServiceConstructionOrders)
					&& pBuilder->getCurrentUpgradeLevel() >= 2
					&& hasPowerBufferForTurret()
					&& campaignAvailableToBuild(pBuilder,Structure_RocketTurret)
					&& money >= economyReserve + data[Structure_RocketTurret][houseID].price
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
					itemID = Structure_RocketTurret; structureRule = "front_battery";
					logDebug("FRONT-BATTERY: rocket turret %d/%d (counter-air goal %d)",
						itemCount[Structure_RocketTurret] + 1, batteryTurrets, counterAirTurrets);
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
				if (itemID == NONE_ID && !skipRemainingStructureLogic
					&& currentGame && currentGame->isCitySimEnabled()
					&& money > 200
					&& itemCount[Structure_WindTrap] > 0) {
					// Zones consume power as they grow. Before placing one,
					// ensure we have surplus power. If not, build a nuclear
					// plant (or windtrap fallback) first.
					constexpr int kZonePowerHeadroom = 24;  // worst case: industrial L3
					const int powerSurplus = getHouse()->getProducedPower() - getHouse()->getPowerRequirement();
					if (powerSurplus < kZonePowerHeadroom) {
						if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
							&& findPlaceLocation(Structure_NuclearPlant).isValid()) {
							itemID = Structure_NuclearPlant; structureRule = "power";
							logDebug("CITY-ZONE-POWER: Building Nuclear Plant before zoning (surplus=%d, need=%d)",
								powerSurplus, kZonePowerHeadroom);
						} else if ((!powerGenerationPending() && campaignAvailableToBuild(pBuilder,Structure_WindTrap))
							&& findPlaceLocation(Structure_WindTrap).isValid()) {
							itemID = Structure_WindTrap; structureRule = "power";
							logDebug("CITY-ZONE-POWER: Building Windtrap before zoning (surplus=%d, need=%d)",
								powerSurplus, kZonePowerHeadroom);
						}
					} else {
						const int resCount = itemCount[Structure_ZoneResidential];
						const int comCount = itemCount[Structure_ZoneCommercial];
						const int indCount = itemCount[Structure_ZoneIndustrial];
						const Uint32 zoneID = chooseCityEconomy(pBuilder, false);

						if (zoneID != NONE_ID && campaignAvailableToBuild(pBuilder,zoneID)
							&& findPlaceLocation(zoneID).isValid()) {
							itemID = zoneID; structureRule = "city_economy";
							logDebug("CITY-ZONE: Building %s (R:%d C:%d I:%d valves=R%+d C%+d I%+d surplus=%d)",
								getItemNameByID(zoneID).c_str(), resCount, comCount, indCount,
								ownResValve, ownComValve, ownIndValve, powerSurplus);
						}
					}
				}

                // All city power branches share the same investment decision.
                // Include committed consumers, but never change vanilla policy.
                if (isCitySim && (itemID == Structure_NuclearPlant || itemID == Structure_WindTrap)
                    && itemCount[Structure_WindTrap] > 0) {
                    const int need = std::max(1,getHouse()->getPowerRequirement()
                        + cityPowerReserve - getHouse()->getProducedPower());
                    const Coord windSite = campaignAvailableToBuild(pBuilder,Structure_WindTrap)
                        ? findPlaceLocation(Structure_WindTrap) : Coord::Invalid();
                    const Coord nuclearSite = campaignAvailableToBuild(pBuilder,Structure_NuclearPlant)
                        ? findPlaceLocation(Structure_NuclearPlant) : Coord::Invalid();
                    const int windOutput = std::max(1,-data[Structure_WindTrap][houseID].power);
                    const int windNeeded = (need+windOutput-1)/windOutput;
                    const int cash = std::max(0,money);
                    // Once the first windtrap establishes power, prefer a reactor
                    // whenever its real footprint and price are feasible. Extra
                    // windtrap room is no longer a reason to choose less capacity.
                    const bool nuclear = nuclearSite.isValid()
                        && (cash >= data[Structure_NuclearPlant][houseID].price
                            || onInstallments(Structure_NuclearPlant));
                    itemID = nuclear ? Structure_NuclearPlant : windSite.isValid() ? Structure_WindTrap : NONE_ID;
                    if (itemID == NONE_ID) skipRemainingStructureLogic = true;
                    traceDecision("city_generator_choice", AITelemetry::Record().set("item",itemID)
                        .set("need",need).set("committed_demand",committedPowerDemand).set("cash",cash)
                        .set("forecast_growth",projectedPowerDemandGrowth).set("forecast_seconds",120)
                        .set("zone_power_current",currentZonePower).set("zone_power_mature",matureZonePower)
                        .set("zone_growth_headroom",zoneGrowthHeadroom).set("power_shortage",!getHouse()->hasPower())
                        .set("nuclear_available",campaignAvailableToBuild(pBuilder,Structure_NuclearPlant))
                        .set("nuclear_placement",placementScoreDetails[Structure_NuclearPlant])
                        .set("nuclear_site",nuclearSite.isValid()).set("nuclear_price",data[Structure_NuclearPlant][houseID].price)
                        .set("wind_needed",windNeeded).set("wind_site",windSite.isValid()).set("nuclear",nuclear)
                        .set("installments",nuclear && onInstallments(Structure_NuclearPlant))
                        .set("industrial_demand",ownIndValve));
                }
            if (itemID!=NONE_ID && itemID!=Structure_WindTrap
                && campaignPowerNeeded(std::max(0,data[itemID][houseID].power))
                && campaignAvailableToBuild(pBuilder,Structure_WindTrap)) {
                itemID=Structure_WindTrap; structureRule="campaign_planned_power";
                crimeServiceSite=Coord::Invalid();
            }
            if (sharedSpending && !fundedCityProduction
                && (itemID==Structure_LightFactory || itemID==Structure_HeavyFactory || itemID==Structure_HighTechFactory)
                && getHouse()->getNumItems(itemID)>0) {
                const bool fundedLane=std::any_of(capitalCandidates.begin(),capitalCandidates.end(),[&](const auto& c) {
                    return c.builder==pBuilder->getObjectID() && c.item==itemID && c.score>0;
                });
                if (!fundedLane) {
                    traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",capitalDecision)
                        .set("builder",pBuilder->getObjectID()).set("item",itemID).set("rule",structureRule)
                        .set("reason","no_funded_production_bottleneck"));
                    itemID=NONE_ID;
                }
            }
			// Dedup: skip if another CY already ordered this unique structure
			// this tick. Zones and turrets are allowed in multiples.
			if (itemID != NONE_ID) {
				bool isMultiBuild = (itemID == Structure_ZoneResidential
					|| itemID == Structure_ZoneCommercial
					|| itemID == Structure_ZoneIndustrial
					|| itemID == Structure_RocketTurret
					|| itemID == Structure_GunTurret
					|| itemID == Structure_PoliceStation
					|| itemID == Structure_Wall
					|| itemID == Structure_Slab1);
				if (!isMultiBuild && orderedThisTick.count(itemID)) {
					logDebug("DEDUP: Skipping %s — already ordered by another CY this tick",
						getItemNameByID(itemID).c_str());
					if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "deduplicated"));
				itemID = NONE_ID;
				crimeServiceSite = Coord::Invalid();
				}
			}

			// Retry useful economic work when another yard claimed the strategic
			// order, or a chosen structure has no feasible footprint. Never
			// default to residential regardless of demand or power.
			if (itemID != NONE_ID && itemID != Structure_RocketTurret
				&& itemID != Structure_GunTurret && itemID != Structure_PoliceStation
                && !findPlaceLocation(itemID).isValid()) {
				if (emitStatsLog) logDebug("PRODUCTION: CY=%u no site for item=%u", pBuilder->getObjectID(), itemID);
                if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "no_site"));
				itemID = NONE_ID;
			}
            if (isCitySim && itemID != NONE_ID
                && (structureRule == std::string("city_economy") || structureRule == std::string("city_economy_fallback"))
                && money < data[itemID][houseID].price) {
                itemID=NONE_ID;
                skipRemainingStructureLogic=true;
            }
            // A validated growth-cap project is the one order that may stand below
            // its price here: the engine charges it in installments out of arriving
            // income, and queuedProductionCost already withholds its whole unpaid
            // remainder from every later pass, so nothing else spends that income.
            if ((vanillaEconomy || isCitySim) && itemID != NONE_ID && money < data[itemID][houseID].price
                && !onInstallments(itemID)
                && !(isCitySim && !getHouse()->hasPower()
                    && (itemID == Structure_WindTrap || itemID == Structure_NuclearPlant))) {
                if (emitStatsLog) traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder", pBuilder->getObjectID()).set("item", itemID)
                    .set("rule", structureRule).set("reason", "committed_cash")
                    .set("spendable", money).set("queued_production_cost", queuedProductionCost));
                itemID = NONE_ID;
            }
            // Paid imports must still be delivered. If they fill the last slots,
            // wait before adding a refinery that could spawn another worker first.
            // Once the fleet has arrived, capacity-only refineries are safe again.
            if (itemID==Structure_Refinery && engineHarvesterLimit>0
                && actualHarvesters<engineHarvesterLimit
                && itemCount[Unit_Harvester]+itemCount[Unit_RebelHarvester]>=engineHarvesterLimit) {
                traceDecision("construction_rejected", AITelemetry::Record()
                    .set("builder",pBuilder->getObjectID()).set("item",itemID)
                    .set("rule",structureRule).set("reason","pending_worker_capacity"));
                itemID=NONE_ID;
            }
            if (isCitySim && DuneCity::isCityZoneStructure(itemID)
                && getHouse()->getProducedPower()-getHouse()->getPowerRequirement()<24) {
                traceDecision("construction_rejected",AITelemetry::Record()
                    .set("builder",pBuilder->getObjectID()).set("item",itemID)
                    .set("rule",structureRule).set("reason","city_growth_power_headroom"));
                itemID=NONE_ID;
                skipRemainingStructureLogic=true;
            }
            if (itemID==Structure_RocketTurret && !routineRocketsAllowed) {
                itemID=NONE_ID;
                crimeServiceSite=Coord::Invalid();
                structureRule="core_infrastructure_before_more_rockets";
            }
			if (emitStatsLog) logDebug("BUILD-CHOICE: CY=%u item=%u credits=%d skip=%d",
				pBuilder->getObjectID(), itemID, money, skipRemainingStructureLogic);
            // A later power/tech override cannot reuse a service's 1x1 site.
            if (itemID != Structure_RocketTurret && itemID != Structure_PoliceStation)
                crimeServiceSite = Coord::Invalid();
			Coord selectedPlaceLocation = Coord::Invalid();
			if (itemID != NONE_ID && campaignAvailableToBuild(pBuilder,itemID)) {
                if (crimeServiceSite.isValid()) selectedPlaceLocation = crimeServiceSite;
                else if (itemID == Structure_PoliceStation) {
                    // Legacy demand/emergency rules use the same bounded service
                    // scorer; no alternate full-map search bypasses its overlap costs.
                    Uint32 service = NONE_ID;
                    selectCityServiceInvestment(pBuilder,money,true,service,selectedPlaceLocation,false,itemID);
                } else if (itemID == Structure_RocketTurret
                    && structureRule == std::string("front_battery")) {
                    // The battery rule chose this order, so it also chooses the
                    // site: the city coverage planner has no front. Free ground
                    // first; the clearance fallback answers only when there is
                    // none, and it answers with the tile of the single own lot
                    // the emplacement will displace.
                    selectedPlaceLocation = findFrontBatteryPlaceLocation();
                    if (selectedPlaceLocation.isInvalid())
                        selectedPlaceLocation = findBatteryClearanceSite();
                } else selectedPlaceLocation = (itemID == Structure_RocketTurret || itemID == Structure_GunTurret)
                    ? findEffectiveTurretPlaceLocation(itemID) : findPlaceLocation(itemID);
			}

            // An idle yard does not wait for a dearer project while it can
            // afford a demanded plot. This also covers deduplication, failed
            // placement and saving rules that set skipRemainingStructureLogic.
            int selectedCost=itemID!=NONE_ID ? purchasePrice(pBuilder,itemID) : 0;
            if (selectedPlaceLocation.isValid())
                for (const auto& foundation:foundationOrders(pBuilder,itemID,selectedPlaceLocation))
                    selectedCost+=purchasePrice(pBuilder,foundation.item);
            if (isCitySim && !pBuilder->isUpgrading() && !pBuilder->isOnHold()
                && pBuilder->getProductionQueueSize()==0
                && !(protectionCapital && capitalPending())
                && !serviceSavingHold
                // An installment-funded growth-cap project is deliberately short
                // of its full price. That is not "cannot afford it": replacing it
                // with a plot here is what prevented it from ever being ordered.
                && !onInstallments(itemID)
                && (!selectedPlaceLocation.isValid() || money<selectedCost)) {
                const Uint32 zone=affordableCityZone(pBuilder,money);
                if (zone!=NONE_ID) {
                    itemID=zone;selectedPlaceLocation=findPlaceLocation(zone);
                    crimeServiceSite=Coord::Invalid();structureRule="idle_city_growth";
                    skipRemainingStructureLogic=false;
                }
            }
            if (serviceSavingHold && itemID==NONE_ID)
                traceDecision("city_service_saving",AITelemetry::Record()
                    .set("builder",pBuilder->getObjectID()).set("rule",structureRule)
                    .set("spendable",money).set("projected_cash",cashFlow.projectedCash));

            if (AITelemetry::log().enabled() && (itemID != NONE_ID || emitStatsLog)) {
                AITelemetry::Record site;
                site.set("valid", selectedPlaceLocation.isValid()).set("x", selectedPlaceLocation.x).set("y", selectedPlaceLocation.y);
                if (selectedPlaceLocation.isValid() && currentGame->getCitySimulation()) {
                    auto* sim = currentGame->getCitySimulation();
                    const int x = selectedPlaceLocation.x, y = selectedPlaceLocation.y;
                    const auto size = getStructureSize(itemID);
                    const auto roads = cityRoadImpact(getMap(), x, y, size.x, size.y, itemID);
                    if (placementCache.count(itemID) && placementCache[itemID] == selectedPlaceLocation)
                        site.set("placement_quality", placementScoreDetails[itemID]);
                    site.set("road_connections_preserved", roads.preservesConnections)
                        .set("roads_covered", roads.roadsCovered).set("redundant_roads_reused",roads.redundantRoadsCovered).set("junction_bonus", roads.junctionBonus);
                    site.set("pollution", sim->getPollutionDensityMap().worldGet(x, y))
                        .set("crime", sim->getCrimeRateMap().worldGet(x, y))
                        .set("traffic", sim->getTrafficDensityMap().worldGet(x, y))
                        .set("land_value", sim->getLandValueMap().worldGet(x, y));
                }
                traceDecision("construction_selection", AITelemetry::Record().set("capital_plan",capitalDecision).set("builder", pBuilder->getObjectID())
                    .set("item", itemID).set("rule", structureRule).set("skip", skipRemainingStructureLogic)
                    .set("state", decisionState()).set("site", site));
            }

			if (selectedPlaceLocation.isValid()) {
                // Plan redevelopment against the ground after its zones are
                // removed. Buildings consumed their old foundations at placement,
                // so replacement buildings need newly queued slabs as well.
				std::vector<Uint32> zonesToRemove;
                const bool redevelop = redevelopmentZones(itemID, selectedPlaceLocation, zonesToRemove);
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
                const bool batteryClearance = !redevelop && itemID == Structure_RocketTurret
                    && structureRule == std::string("front_battery")
                    && batteryClearanceZones(selectedPlaceLocation, clearanceLots, batteryAnchor());
                const auto plan = foundationPlan(pBuilder,itemID,selectedPlaceLocation,
                    batteryClearance ? clearanceLots : zonesToRemove);
                if (!plan.complete) {
                    traceDecision("construction_rejected",AITelemetry::Record()
                        .set("builder",planningBuilder).set("item",itemID).set("rule",structureRule)
                        .set("reason","foundation_incomplete")
                        .set("x",selectedPlaceLocation.x).set("y",selectedPlaceLocation.y));
                    continue;
                }
                const auto& foundations=plan.orders;
                int foundationCost=0;
                for (const auto& foundation:foundations) foundationCost+=purchasePrice(pBuilder,foundation.item);
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
                    && (itemID == Structure_WindTrap || itemID == Structure_NuclearPlant);
                const int acceptanceFloor = onInstallments(itemID)
                    ? CityEconomyInvestmentPolicy::productionStartBudget(
                          purchasePrice(pBuilder,itemID),foundationCost,cityBuildTicks(itemID))
                    : purchasePrice(pBuilder,itemID)+foundationCost;
                const bool cityWorOrder = citySimEnabled && itemID == Structure_WOR;
                const int64_t fundingFloor = int64_t(acceptanceFloor)
                    + (cityWorOrder ? cityWorCashReserve : 0);
                if ((!foundations.empty() || cityWorOrder) && !emergencyGenerator && money < fundingFloor) {
                    traceDecision("capital_order_blocked",AITelemetry::Record().set("plan",capitalDecision)
                        .set("builder",planningBuilder).set("item",itemID)
                        .set("reason",cityWorOrder ? "city_anti_air_reserve" : "foundation_budget")
                        .set("price",purchasePrice(pBuilder,itemID)).set("foundation_cost",foundationCost)
                        .set("acceptance_floor",fundingFloor).set("installments",onInstallments(itemID))
                        .set("spendable",money));
                    continue;
                }
                size_t foundationsQueued=0;
                auto cancelFoundations=[&]() {
                    for (size_t i=0;i<foundationsQueued;++i)
                        doCancelItem(pBuilder,foundations[i].item);
                    placeLocations.clear();
                };
                for (const auto& foundation : foundations) {
                    // These slabs belong to the validated blackout recovery site.
                    // Its unpaid reactor reservation must not block the slabs while
                    // allowing only the generator itself through the same guard.
                    emergencyFoundationOrder = emergencyGenerator;
                    if (!produceItemWithLogging(foundation.item,__LINE__,"building_foundation")) break;
                    placeLocations.push_back(Coord(foundation.x,foundation.y));
                    ++foundationsQueued;
                }
                emergencyFoundationOrder = false;
                if (foundationsQueued!=foundations.size()) {
                    cancelFoundations();
                    continue;
                }
                if (!foundations.empty()) placeLocations.push_back(selectedPlaceLocation);

				if (produceItemWithLogging(itemID, __LINE__, structureRule)) {
                    if (isCitySim && itemID != Structure_Road && itemID != Structure_Slab1
                        && itemID != Structure_Slab4) {
                        if (crimeServiceSite.isValid()) nonServiceConstructionOrders = 0;
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
                            itemCount[zone->getItemID()]--;
                            // Invalidate before mutation, including its callbacks.
                            invalidateAnchorField();
                            zone->demolish();
                        }
                        traceDecision("redevelopment_committed", AITelemetry::Record().set("builder",planningBuilder)
                            .set("item",itemID).set("x",selectedPlaceLocation.x).set("y",selectedPlaceLocation.y)
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
                    if (itemID==Structure_Refinery && (engineHarvesterLimit==0
                        || itemCount[Unit_Harvester]+itemCount[Unit_RebelHarvester]<engineHarvesterLimit))
                        ++itemCount[Unit_Harvester];
					reservedStructures[planningBuilder] = {itemID, selectedPlaceLocation};
                    if (placeLocations.empty()) placeLocations.push_back(selectedPlaceLocation);
                    traceDecision("site_reserved", AITelemetry::Record().set("builder", planningBuilder)
                        .set("item", itemID).set("x", selectedPlaceLocation.x).set("y", selectedPlaceLocation.y));
                    orderedThisTick.insert(itemID);
                    itemCount[itemID]++;

					if (itemID == strategicReserveItem) strategicReserveCost = 0;
					clearPlacementCache();
				} else {
					cancelFoundations();
				}
			}
			else if (itemID != NONE_ID && campaignAvailableToBuild(pBuilder,itemID)) {
				// Extend the buildable area with concrete for anything QuantBot
				// would have founded anyway; the same policy decides both, so a
				// building never waits for ground it would not have prepared.
				bool needsConcreteExpansion = getGameInitSettings().getGameOptions().concreteRequired
					&& QuantBotBuildPolicy::foundationRequiredForItem(itemID);

				if (needsConcreteExpansion && campaignAvailableToBuild(pBuilder,Structure_Slab1)) {
					Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
					if (slabLocation.isValid()) {
						produceItemWithLogging(Structure_Slab1,__LINE__,"concrete_expansion");
						logDebug("Building concrete slab to expand buildable area for itemID %d at (%d,%d)", itemID, slabLocation.x, slabLocation.y);
					} else {
					// Cannot place slab - silenced (too spammy)
					}
				} else {
					logDebug("Cannot build itemID %d: no place to build and slabs not available", itemID);
				}
			}
		else if (itemID != NONE_ID && !campaignAvailableToBuild(pBuilder,itemID)) {
			logDebug("Cannot build itemID %d: not available (prerequisites not met)", itemID);
		}
		else if (itemID == NONE_ID && !skipRemainingStructureLogic && emitStatsLog) {
			logDebug("No structure selected to build (money: %d, skipRemaining: %d)", money, skipRemainingStructureLogic);
		}

        // Only use spare yard capacity, after all strategic/city choices.
        if (isCitySim && itemID == NONE_ID && !skipRemainingStructureLogic
            && !roadMaintenanceAttempted && !pBuilder->isUpgrading()
            && pBuilder->getProductionQueueSize() == 0 && campaignAvailableToBuild(pBuilder,Structure_Road)) {
            roadMaintenanceAttempted = true;
            const int roadPrice = std::max(1,data[Structure_Road][houseID].price);
            const int repaired = queueCityRoadRepairs(pBuilder,std::min(8,std::max(0,money-economyReserve)/roadPrice));
            money -= repaired*roadPrice;
            capitalOrderedCost += repaired*roadPrice;
            if (repaired) traceDecision("capital_road_batch",AITelemetry::Record().set("capital_plan",capitalDecision)
                .set("builder",planningBuilder).set("count",repaired).set("price",roadPrice).set("cost",repaired*roadPrice));
        }

		// City yards use the demand-ranked fallback before placement above.
		// Outside city mode an otherwise idle yard can extend concrete, but
		// only where concrete means something.
		if (!isCitySim && getGameInitSettings().getGameOptions().concreteRequired
			&& !skipRemainingStructureLogic && money > 500 && pBuilder->getProductionQueueSize() < 1
			&& itemID == NONE_ID && campaignAvailableToBuild(pBuilder,Structure_Slab1)) {
			Coord slabLocation = findSlabPlaceLocation(Structure_Slab1);
			if (slabLocation.isValid()) produceItemWithLogging(Structure_Slab1,__LINE__,"concrete_expansion");
		}

						}
					}

				if (pBuilder->isWaitingToPlace()) {
					Uint32 itemToBePlaced = pBuilder->getCurrentProducedItem();
					logDebug("PRODUCTION: CY waiting to place itemID: %d, credits: %d, queued locations: %zu", itemToBePlaced, money, placeLocations.size());
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
                        traceDecision(event, AITelemetry::Record().set("builder", pConstYard->getObjectID())
                            .set("item", itemToBePlaced).set("reason", reason).set("x", site.x).set("y", site.y)
                            .set("reserved_overlap", site.isValid() && overlapsReservedStructure(site.x, site.y, size.x, size.y))
                            .set("recent_loss_nearby", site.isValid() && nearRecentStructureLoss(site.x, site.y, size.x, size.y))
                            .set("enemy_fire_risk", dangerAt(site,size)).set("recent_loss_risk", dangerAt(site,size,true))
                            .set("reactor_clearance", site.isValid() && reactorClearance(itemToBePlaced,site))
                            .set("placement_quality",placementScoreDetails[itemToBePlaced]).set("planned_locations_remaining", placeLocations.size()).set("tiles", tiles));
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
                    commitBatteryClearance(pBuilder, itemToBePlaced, placeLocations, money,
                        economyReserve, clearanceCommitmentShortfall, &deferClearance);
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
                            if (placingSlab) doCancelItem(pBuilder,itemToBePlaced);
                            doCancelItem(pBuilder,Structure_RocketTurret);
                            reservedStructures.erase(planningBuilder);
                            placeLocations.clear();
                            clearPlacementCache();
                        }
                        continue;
                    }

                    auto plannedSlabLegal = [&](Uint32 slab, const Coord& site) {
                        const Coord span = getStructureSize(slab);
                        if (!site.isValid()) return false;
                        for (int dx = 0; dx < span.x; ++dx) for (int dy = 0; dy < span.y; ++dy) {
                            const int x = site.x + dx, y = site.y + dy;
                            if (!getMap().tileExists(x, y)) return false;
                            const Tile* tile = getMap().getTile(x, y);
                            if (tile->isRoad() || !tile->isRock() || tile->isMountain()
                                || tile->isBlocked() || tile->hasCityZone()) return false;
                        }
                        return getMap().okayToPlaceStructure(site.x, site.y, span.x, span.y,
                            false, getHouse(), false, slab);
                    };

					// Check if we have a pre-stored location (from concrete pre-placement)
					if (!placeLocations.empty()) {
						location = placeLocations.front();
						Coord itemsize = getStructureSize(itemToBePlaced);

						// Verify the location is still valid
						if ((!placingSlab || plannedSlabLegal(itemToBePlaced, location))
                            && getMap().okayToPlaceStructure(location.x, location.y, itemsize.x, itemsize.y, false, getHouse(), false, itemToBePlaced)
                            && (itemToBePlaced != Structure_Road || !getMap().getTile(location.x,location.y)->isRoadConnection())
                            && cityRoadImpact(getMap(), location.x, location.y, itemsize.x, itemsize.y, itemToBePlaced).preservesConnections
                            && !overlapsReservedStructure(location.x, location.y, itemsize.x, itemsize.y)
                            && preservesGroundAccess(itemToBePlaced,location)
                            && (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret
                                || itemToBePlaced == Structure_Wall || itemToBePlaced == Structure_Road
                                || itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4
                                || (!nearRecentStructureLoss(location.x, location.y, itemsize.x, itemsize.y)
                                    && dangerAt(location,itemsize) == 0 && TacticalSafetyPolicy::reactorPlacementAllowed(itemToBePlaced,reactorClearance(itemToBePlaced,location))))) {
							placeLocations.pop_front();
							logDebug("PRODUCTION: Using pre-stored location (%d,%d) for itemID: %d", location.x, location.y, itemToBePlaced);
						} else if (itemToBePlaced == Structure_Road) {
                            const Coord oldSite = location;
                            location = Coord::Invalid();
                            // Rate-limit unsuccessful searches by yard; a finished
                            // road remains ready and can be used when a gap opens.
                            if (!roadMaintenanceAttempted) {
                                roadMaintenanceAttempted = true;
                                location = findFinishedRoadSite(pBuilder);
                            }
                            if (location.isValid()) {
                                placeLocations.pop_front();
                                tracePlacementIssue("placement_replan","road_redirected_to_gap",location);
                            } else if (emitStatsLog) {
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
                                replan = foundationPlan(pBuilder, reserved->second.item, reserved->second.location);
                            for (const auto& order : replan.orders)
                                if (order.item == itemToBePlaced
                                    && plannedSlabLegal(order.item, Coord(order.x, order.y))) {
                                    location = Coord(order.x, order.y);
                                    break;
                                }
                            if (location.isValid()) {
                                placeLocations.pop_front();
                                tracePlacementIssue("placement_replan", "foundation_replanned", location);
                            } else if (replan.complete && replan.orders.empty()) {
                                tracePlacementIssue("placement_cancel", "foundation_already_prepared", oldSite);
                                doCancelItem(pConstYard, itemToBePlaced);
                                placeLocations.pop_front();
                            } else {
                                // The ground can no longer take this slab. Refund
                                // the concrete only: the building behind it is
                                // finished or paid for and keeps both its queue
                                // place and its planned site. Placing it on a
                                // partly bare footprint incurs normal foundation
                                // damage instead of wasting its construction time.
                                tracePlacementIssue("placement_cancel", "foundation_unavailable", oldSite);
                                doCancelItem(pConstYard, itemToBePlaced);
                                placeLocations.pop_front();
                                clearPlacementCache();
                            }
							placementIssueHandled = true;
						} else {
                            // Try another legal site if this one was blocked. The
                            // final foundation check below still applies to that site.
                            const Coord oldSite = location;
                            placeLocations.pop_front();
                            placementCache.erase(itemToBePlaced);
                            location = (itemToBePlaced == Structure_RocketTurret || itemToBePlaced == Structure_GunTurret)
                                ? findEffectiveTurretPlaceLocation(itemToBePlaced) : findPlaceLocation(itemToBePlaced);
                            if (location.isInvalid()) {
                                placeLocations.push_front(oldSite);
                                if (emitStatsLog) tracePlacementIssue("placement_deferred", "planned_site_blocked", oldSite);
                            } else {
                                tracePlacementIssue("placement_replan", "planned_site_blocked", oldSite);
                            }
                            placementIssueHandled = true; // Suppress the no-dynamic-site branch below.
                        }
					} else {
						// No pre-stored location, find one dynamically
					if (itemToBePlaced == Structure_Road) {
                            location = Coord::Invalid();
                            if (!roadMaintenanceAttempted) {
                                roadMaintenanceAttempted = true;
                                location = findFinishedRoadSite(pBuilder);
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
                                    const Coord yard = pConstYard->getLocation();
                                    const int distance = std::abs(x-yard.x)+std::abs(y-yard.y);
                                    if (risk < bestRisk || (risk == bestRisk && distance < bestDistance)) {
                                        bestRisk = risk; bestDistance = distance; location = site;
                                    }
                                }
                            }
                            if (location.isValid()) {
                                placeLocations.clear();
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
                                doCancelItem(pConstYard,itemToBePlaced);
                                placeLocations.clear();
                                reservedStructures.erase(planningBuilder);
                                --itemCount[itemToBePlaced];
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
                                // ground access, roads and neighbouring lots.
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
						if (location.isValid()) {
							traceDecision("placement_request", AITelemetry::Record().set("builder", pConstYard->getObjectID())
                                .set("item", itemToBePlaced).set("x", location.x).set("y", location.y));
                            // Invalidate before placement and its callbacks; a
                            // failed attempt can conservatively rebuild too.
                            invalidateAnchorField();
                            const bool placed = doPlaceStructure(pConstYard, location.x, location.y);
                            // A road retry keeps its place ahead of the remaining
                            // queue; do not accidentally consume the next plan.
                            if (!placed && itemToBePlaced == Structure_Road) placeLocations.push_front(location);
                            traceDecision("placement_result", AITelemetry::Record().set("builder", planningBuilder)
                                .set("item", itemToBePlaced).set("x", location.x).set("y", location.y).set("success", placed));
                            if (itemToBePlaced != Structure_Slab1 && itemToBePlaced != Structure_Slab4) {
                                if (placed) reservedStructures.erase(planningBuilder);
                                else { reservedStructures[planningBuilder] = {itemToBePlaced, location};
                                    if (placeLocations.empty()) placeLocations.push_front(location); }
                            }
                            clearPlacementCache();
							logDebug("PRODUCTION: Placed structure itemID: %d at (%d,%d)", itemToBePlaced, location.x, location.y);
						}
						else if (!placementIssueHandled) {
							if (emitStatsLog) logDebug("PRODUCTION: Holding finished item %d until a legal site is available; search=%s",
                                itemToBePlaced, placementScoreDetails[itemToBePlaced].json().c_str());
							if (emitStatsLog) tracePlacementIssue("placement_deferred", "no_dynamic_site", location);
						}
					}
                    // Pre-plan queue=0 often means a yard is about to receive
                    // an order, not that it stayed idle. Record the outcome too.
                    if (emitStatsLog && AITelemetry::log().enabled()) {
                        const char* result = pBuilder->isUpgrading() ? "upgrading"
                            : pBuilder->getProductionQueueSize() > 0 ? "queued"
                            : getHouse()->getCredits() <= 100 ? "insufficient_cash"
                            : pBuilder->getBuildListSize() == 0 ? "no_build_options"
                            : "empty_after_planning";
                        traceDecision("yard_planning_result", AITelemetry::Record()
                            .set("builder", pBuilder->getObjectID()).set("result", result)
                            .set("queue", pBuilder->getProductionQueueSize())
                            .set("upgrading", pBuilder->isUpgrading()).set("hold", pBuilder->isOnHold())
                            .set("state", decisionState()));
                    }
				} break;
				}
			}
		}
	}

    if (!supportMode && AITelemetry::log().enabled()) {
        traceDecision("capital_outcome",AITelemetry::Record().set("plan",capitalDecision)
            .set("priority_ordered",capitalConsumed).set("priority_pending",capitalPending())
            .set("orders",capitalOrders).set("ordered_cost",capitalOrderedCost).set("remaining_planning_cash",money));
    }
	// MULTIPLAYER FIX: Use deterministic timer instead of random
	buildTimer = 5 + (getHouse()->getHouseID() % 10);  // 5-14 cycles
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
