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

#ifndef QUANTBOT_BUILD_PASS_H
#define QUANTBOT_BUILD_PASS_H

// The state of one QuantBot::build() pass, split out of the former single build()
// function. BuildPass holds what the pass plans before the producers run and what
// they share across one pass (cash still unspent, queue-inclusive counts, capital
// plan); BuilderTurn holds one producer's turn in that pass; YardTurn holds one
// construction yard's structure selection. Members keep the names and initial
// values of the locals they replaced and are written in the original order.

#include <players/QuantBot.h>
#include <players/CityEconomyInvestmentPolicy.h>
#include <players/QuantBotCityPolicy.h>
#include <players/QuantBotSpendingPolicy.h>
#include <players/QuantBotStrategy.h>
#include <players/UnitMixPolicy.h>

#include <array>
#include <list>
#include <map>
#include <set>
#include <utility>
#include <vector>

// A required civic the yard cannot build yet is not an absent demand: the
// missing link in the mod's own prerequisite chain - including the legal
// yard upgrade - is what has to be bought first. Houses whose Radar was
// destroyed or never built stayed capped for a whole match because
// availability erased the demand instead of producing this plan. Nothing
// about the chain is hard-coded; it is walked out of ObjectData exactly as
// the core-defence rule walks it.
struct QuantBot::CivicPlan {
    Uint32 civic = NONE_ID;   ///< the needed civic itself
    Uint32 order = NONE_ID;   ///< the civic, or the prerequisite to buy before it
    bool upgrade = false;     ///< the yard needs its own legal upgrade first
    bool pending = false;     ///< already ordered; nothing more to buy this pass
    const char* reason = "no_structural_need";
};

struct QuantBot::CapitalCandidate {
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

struct QuantBot::BuildPass {
    explicit BuildPass(int militaryValue) : militaryValue(militaryValue) {}

    // build(): the queue-inclusive army value; producers add what they order.
    int militaryValue;

    // planBuildInventory()
    // Family economy projects the two original slots as worker/unload-bay
    // totals. Use rawPlannedItemCount() when pricing actual objects or queues.
    int itemCount[Num_ItemID]{};
    int activeHeavyFactoryCount{};
    std::vector<const BuilderBase*> harvesterFactories{};
    bool carryallBuildAvailable{};
    bool mcvBuildAvailable{};
    bool nuclearBuildAvailable{};
    int activeLightFactoryCount{};
    int activeHighTechFactoryCount{};
    int ornithopterFactoryCount{};
    int ornithopterCapableFactoryCount{};
    int activeRepairYardCount{};
    int queuedProductionCost{};
    int queuedMilitaryValue{};
    int mcvUpgradesInProgress{};

    // planBuildBudget()
    int money{};
    bool militaryBudgetOverridden{};
    int militaryBudget{};
    bool surplusArmy{};
    int lightBudget{};
    bool citySimEnabled{};
    int spiceCompetitors{};
    int citySpiceWorkerTarget{};
    bool starportMarketAvailable{};
    int engineHarvesterLimit{};
    int actualHarvesters{};
    int combatVehicles{};
    int busyTransports{};
    int busyRepairBays{};
    std::set<Uint32> pickupQueue{};
    int waitingRepairVehicles{};
    int repairBaseline{};
    int repairTarget{};
    bool powerRules{};
    bool turretPowerRequired{};
    bool vanillaEconomy{};
    bool campaignEconomyPush{};
    int clearanceCommitmentShortfall{};
    int economyReserve{};

    // planCityState()
    int ownResPop{};
    int ownComPop{};
    int ownIndPop{};
    int ownTotalPop{};
    int ownTaxBaseEighths{};
    int ownAvgLandValue{};
    int16_t ownResValve{};
    int16_t ownComValve{};
    int16_t ownIndValve{};
    Uint32 currentBuildCycle{};

    // planStrategicReserve()
    bool anyConstructionYardCanBuildIX{};
    bool anyConstructionYardCanBuildRefinery{};
    bool customStrategicPlanning{};
    bool stablePower{};
    int palaceTarget{};
    bool ixOverdue{};
    bool palaceOverdue{};
    Uint32 strategicReserveItem{};
    int strategicReserveCost{};

    // planSpiceEconomy()
    int spiceShare{};
    int mapSpiceHarvesterTarget{};
    int spiceHarvesterTarget{};
    int fundedHarvesterTarget{};
    int busyRefineries{};
    int freeRefineries{};
    int unbookedRefineries{};
    int waitingHarvesters{};
    int waitingCargo{};
    int blockedRefineryReturns{};
    int fieldReturnersBlocked{};
    AITelemetry::Record refineryQueues{};
    bool unloadingBacklog{};
    bool rockExpansionNeeded{};
    int cityWorkingReserve{};
    bool orderedSpiceHarvester{};
    bool transportTechAvailable{};
    bool brutalCityEconomy{};
    bool brutalEconomy{};
    bool openingWorkerFloor{};

    // planPowerAndYards()
    int generationCostPerThousand{};
    int largestGenerator{};
    int currentZonePower{};
    int matureZonePower{};
    int committedPowerDemand{};
    std::array<int,3> developingZones{};
    int zoneGrowthHeadroom{};
    int cityPowerReserve{};
    bool nuclearPlan{};
    int cityYardTarget{};
    int yardLimit{};
    int cityConstructionCapacity{};
    bool colonisationNeeded{};
    bool postponeCapacityMcv{};

    // planCityPopulation()
    int populationCeiling{};
    QuantBotCityPolicy::Limits cityGrowthLimits{};
    int cityZonesIncludingQueued{};
    int cityDisplayPopulation{};
    int cityPendingDisplayPopulation{};

    // planCivicNeedAndTrips()
    bool campaignPowerBudget{};
    uint8_t civicDemandClipped{};
    uint8_t civicStructuralNeed{};
    int refineryTripCycles{};
    int walkingTripCycles{};
    int refineryFieldRisk{};
    int fillCycles{};
    int unloadCycles{};
    int bayAnnualIncome{};
    CityEconomyInvestmentPolicy::Investment lastEconomyInvestment{};
    bool cityRefineryCatchup{};
    bool cityRefineryOpening{};

    // traceBuildState()
    bool emitStatsLog{};

    // planUnitMix()
    int infantryPercent{};
    const QuantBotStrategy::MixProfile* strategyMixProfile = nullptr;
    bool learningUnitMix{};
    UnitMixPolicy::Mix unitMix{};
    bool cityAntiAirInfantryDemand{};
    FixPoint tankPercent{};
    FixPoint siegePercent{};
    FixPoint launcherPercent{};
    FixPoint specialPercent{};
    FixPoint ornithopterPercent{};
    int lightVehicleBps{};
    int lightVehicleValue{};
    int infantryValue{};
    int lightVehicleCount{};
    int infantryCount{};
    int64_t cityWorCashReserve{};
    int vehiclePlanValue{};

    // planCapitalForecast()
    // Track unique structures ordered this tick to prevent multiple CYs
    // from building the same thing. Zones and turrets are excluded (want multiples).
    std::set<Uint32> orderedThisTick{};
    bool roadMaintenanceAttempted{};
    bool defendingEconomy{};
    std::vector<CapitalCandidate> capitalCandidates{};
    AITelemetry::Record capitalProducers{};
    AITelemetry::Record capitalUnitOptions{};
    int forecastTax{};
    int forecastUpkeep{};
    int forecastSpice{};
    int forecastNetIncome{};
    int forecastFunding{};
    int existingMilitaryCapacity{};
    int constructionCapacity{};
    int readyYards{};
    int continuedMilitaryCost{};
    int continuedConstructionCost{};
    int activeProductionBurn{};
    std::map<Uint32,int> laneCapacity{};
    std::vector<const BuilderBase*> capitalBuilders{};
    int cashBuffer{};
    QuantBotSpendingPolicy::CashFlow cashFlow{};
    bool capacityMcvRunway{};
    int capacityMcvOrderedCost{};
    bool sharedSpending{};
    int militaryFunding{};

    // planCityBootstrap()
    int cityYards{};
    int cityGrowthYardsBusy{};
    int civicReserveCost{};
    bool spiceEconomyViable{};
    int cityBootstrapLotCost{};
    int cityStorageCapacityIncludingQueued{};
    Uint32 cityStorageItem{};
    int cityStorageCost{};
    bool cityParallelBootstrapFunding{};

    // planOpeningProduction()
    int firstFactoryCapital{};
    int firstFactoryProduction{};
    bool fundedFactoryOpening{};
    int nextHeavyRunway{};
    bool fundedCityProduction{};
    int desiredWorkers{};
    int transportBaseline{};
    int transportTarget{};

    // planDefenceCoverage()
    int coverageTier{};
    bool airEngaged{};
    int uncoveredCoreAssets{};
    int coverageDemand{};
    Uint32 missingCoreInfrastructure{};
    bool coreInfrastructureReady{};
    bool cityCoreInfrastructureReady{};
    bool routineRocketsAllowed{};
    int cityPlotPrice{};
    int rocketPowerBuffer{};
    bool openingSupplierDue{};
    int moderateCrimeProperties{};
    int dangerousCrimeProperties{};

    // chooseCapitalPlan()
    int capitalChoice{};
    bool protectionCapital{};
    bool cityGrowthProtected{};
    bool capitalConsumed{};
    int capitalOrderedCost{};
    AITelemetry::Record capitalOrders{};
    int capitalInitialCount{};
    uint64_t capitalDecision{};

    // planBuildOrder()
    // Give city construction first access to this pass's planning budget.
    // Air gets its allocation before ground production, then light precedes heavy overflow.
    // Stable ordering keeps peers deterministic.
    // Keep IDs, not pointers: an earlier yard may demolish a later zone.
    std::vector<std::pair<int, Uint32>> planningOrder{};
};

struct QuantBot::BuilderTurn {
    // Restore the reserved portion after this builder's decisions, keeping
    // charges made by the acceptance wrapper for subsequent builders.
    struct RestoreReservedCredits {
        int& money;
        int reserved;
        ~RestoreReservedCredits() { money += reserved; }
    };

    explicit BuilderTurn(int& money) : reserve{money, 0} {}

    // buildProducer()
    const StructureBase* pStructure = nullptr;
    const BuilderBase* pBuilder = nullptr;

    // beginBuilderTurn()
    Uint32 installmentProject{};
    int installmentProjectStartBudget{};
    bool emergencyFoundationOrder{};

    // reserveBuilderCash()
    bool expansionProducer{};
    bool workerProducer{};
    bool transportProducer{};
    bool capitalSupplier{};

    // Set by reserveBuilderCash(); restored when the turn ends.
    RestoreReservedCredits reserve;
};

struct QuantBot::YardTurn {
    // buildConstructionYard()
    const ConstructionYard* pConstYard = nullptr;
    std::list<Coord>* placeLocations = nullptr;

    // planYardLanes()
    int heavyDeficit{};
    int lightDeficit{};
    bool lightBacklog{};
    int airDeficit{};
    bool heavyBacklog{};
    bool airBacklog{};

    // startYardSelection()
    Uint32 itemID{};
    const char* structureRule = nullptr;
    Coord crimeServiceSite{};
    bool serviceSavingHold{};
    bool skipRemainingStructureLogic{};
    CivicPlan civicPlan{};
    bool civicClaimsYard{};
    bool urgentProtection{};

    // chooseYardAirDefence()
    int maxEnemyOrnithopters{};
    int counterAirTurrets{};
    int batteryTurrets{};
    int activeRocketTurretGoal{};

    // chooseYardCityFoundations()
    bool higherPriorityCapital{};

    // chooseYardCityOpening()
    bool lowSpiceEconomy{};
    bool isCitySim{};
    int coreShortfall{};
    bool holdExtraHeavy{};

    // chooseYardServicesAndRefineries()
    int developedZones{};
    int dangerousZones{};
    int moderateZones{};
    unsigned serviceInterval{};
    bool serviceEvaluated{};
    int servicePriceFloor{};

    // chooseYardProductionLanes()
    bool cityIncomeReady{};
};

#endif // QUANTBOT_BUILD_PASS_H
