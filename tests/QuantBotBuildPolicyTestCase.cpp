#include <players/CityServiceInvestmentPolicy.h>
#include <players/RocketTurretPolicy.h>
#include <players/CombatReward.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <players/UnitMixPolicy.h>
#include <numeric>
#include <dunecity/PowerRules.h>
#include <dunecity/VanillaEconomy.h>
#include <catch2/catch_test_macros.hpp>
#include <players/AirStrikePolicy.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/QuantBotFoundationPolicy.h>
#include <players/QuantBotPowerInvestmentPolicy.h>
#include <players/CityEconomyInvestmentPolicy.h>
#include <players/QuantBotSpendingPolicy.h>
#include <players/QuantBotColonisationPolicy.h>
#include <limits>
#include <set>
#include <utility>

using namespace QuantBotBuildPolicy;

TEST_CASE("QuantBot respects the single-palace option even in a large city", "[quantbot][production]") {
    REQUIRE(palaceTarget(true, true, 60000) == 1);
    REQUIRE(palaceTarget(false, false, 60000) == 1);
    REQUIRE(palaceTarget(false, true, 29999) == 1);
    REQUIRE(palaceTarget(false, true, 30000) == 2);
    REQUIRE(palaceTarget(false, true, 60000) == 3);
}

TEST_CASE("QuantBot caps palaces per difficulty without loosening stricter rules", "[quantbot][production]") {
    // Difficulty values match QuantBot::Difficulty: Easy 0, Medium 1, Hard 2,
    // Brutal 3, Defend 4.
    REQUIRE(difficultyPalaceCap(0) == 1);
    REQUIRE(difficultyPalaceCap(1) == 3);
    REQUIRE(difficultyPalaceCap(2) > 3);
    REQUIRE(difficultyPalaceCap(3) > 3);

    // Easy never exceeds one palace, Medium never exceeds three, however large
    // the city grows.
    REQUIRE(palaceTarget(false, true, 300000, 0) == 1);
    REQUIRE(palaceTarget(false, true, 60000, 1) == 3);
    REQUIRE(palaceTarget(false, true, 300000, 1) == 3);

    // Below the cap the population target still governs.
    REQUIRE(palaceTarget(false, true, 30000, 1) == 2);

    // Hard and Brutal keep the unchanged target.
    REQUIRE(palaceTarget(false, true, 300000, 2) == palaceTarget(false, true, 300000));
    REQUIRE(palaceTarget(false, true, 300000, 3) == palaceTarget(false, true, 300000));

    // Stricter existing rules win at every difficulty.
    REQUIRE(palaceTarget(true, true, 300000, 2) == 1);
    REQUIRE(palaceTarget(true, true, 300000, 3) == 1);
    REQUIRE(palaceTarget(false, false, 300000, 1) == 1);
}

TEST_CASE("QuantBot strategic savings leave excess funds available for tanks", "[quantbot][production]") {
    REQUIRE(spendableCredits(59044, 2000) == 57044);
    REQUIRE(spendableCredits(1000, 2000) == 0);
    REQUIRE(spendableCredits(1000, 0) == 1000);
    REQUIRE(spendableCredits(-100, 2000) == 0);
}

TEST_CASE("QuantBot repairs an over-residential economy even at saturated demand", "[quantbot][city]") {
    const auto zones = rankZones(60, 4, 9, 2000, 1500, 1500, false);
    REQUIRE(zones[0] == Structure_ZoneCommercial);
    REQUIRE(zones[1] == Structure_ZoneIndustrial);
    REQUIRE(zones[2] == Structure_ZoneResidential);
    // The second yard sees the first yard's accepted order in its counts.
    const auto first = rankZones(30, 10, 10, 2000, 1500, 1500, false);
    REQUIRE(first[0] == Structure_ZoneResidential);
    const auto second = rankZones(33, 10, 10, 2000, 1500, 1500, false);
    REQUIRE(second[0] == Structure_ZoneIndustrial);
}

TEST_CASE("QuantBot does not build residential as a fallback against demand", "[quantbot][city]") {
    const auto zones = rankZones(60, 4, 9, -286, 1500, 1500, false);
    REQUIRE(zones[0] == Structure_ZoneCommercial);
    REQUIRE(zones[1] == Structure_ZoneIndustrial);
    REQUIRE(zones[2] == NONE_ID);
    for(auto item : rankZones(3, 1, 1, 0, -100, -100, false)) REQUIRE(item == NONE_ID);
}

TEST_CASE("QuantBot opening hedges with demanded housing without forcing missing jobs", "[quantbot][city]") {
    REQUIRE(rankZones(0,0,0,2000,-1500,-1500,true)[0] == Structure_ZoneResidential);
    REQUIRE(rankZones(3,0,0,2000,-1500,-1500,true)[0] == Structure_ZoneResidential);
    REQUIRE(rankZones(3,0,0,2000,-1500,-1500,true)[1] == NONE_ID);
    REQUIRE(rankZones(0,0,0,-100,500,0,true)[0] == Structure_ZoneCommercial);
    REQUIRE(rankZones(0,0,0,0,0,0,true)[0] == NONE_ID);
}

TEST_CASE("City investment compares return per credit and protects the residential hedge", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    Investment refinery{400,400,4,7500,1000};
    Investment residential{100,100,2,3750,1000};
    REQUIRE_FALSE(preferRefinery(refinery,residential,true,false)); // Four plots earn more for the same cash.
    residential.annualIncome=30;
    REQUIRE(preferRefinery(refinery,residential,true,false));
    REQUIRE_FALSE(preferRefinery(refinery,residential,true,true)); // First demanded residential hedge.
    REQUIRE_FALSE(preferRefinery(refinery,residential,false,false)); // Bays already cover the fleet.
    refinery.delayCycles=horizonCycles;
    REQUIRE_FALSE(preferRefinery(refinery,residential,true,false)); // No returns within the horizon.
}

TEST_CASE("Refinery expansion follows near-term workers and marginal delivered spice", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    REQUIRE(factoryHarvesterTarget(40,120)==40);
    REQUIRE(factoryHarvesterTarget(140,120)==120);
    REQUIRE(factoryHarvesterTarget(0,120)==0);
    // Four is not a production cap: factories continue toward the spice target,
    // then the yard adds bays when predicted throughput requires them.
    REQUIRE(factoryHarvesterTarget(120,120)==120);
    REQUIRE_FALSE(processingCapacityNeeded(1,4,320,1757));
    REQUIRE(processingCapacityNeeded(1,6,320,1757));
    REQUIRE_FALSE(processingCapacityNeeded(2,6,320,1757)); // Queued bay already covers fleet.
    REQUIRE_FALSE(considerRefinery(false,true,true)); // Factory + zoning run together.
    REQUIRE(considerRefinery(false,true,false)); // Opening without any worker-capable factory.
    REQUIRE(considerRefinery(true,false,true)); // Existing fleet needs unloading capacity.
    REQUIRE_FALSE(considerRefinery(false,false,false)); // No need; keep yard for city growth.
    REQUIRE(marginalSpiceIncome(120,40,false,400,1200)==0);
    REQUIRE(marginalSpiceIncome(120,39,false,400,1200)==1200);
    REQUIRE(marginalSpiceIncome(3,1,true,400,1200)==400);
    REQUIRE(marginalSpiceIncome(3,1,false,400,1200)==0);
}

TEST_CASE("Factory economy priority balances workers with military without a refinery cap", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    CHECK(preferFactoryHarvester(1,120,0,10000,300,true)); // Recover collapsed economy.
    CHECK_FALSE(preferFactoryHarvester(4,120,600,10000,300,true)); // Army is too weak.
    CHECK(preferFactoryHarvester(4,120,2400,10000,300,true)); // Enough cover to expand economy.
    CHECK_FALSE(preferFactoryHarvester(5,120,2400,10000,300,true)); // Next worker yields to military.
    CHECK(preferFactoryHarvester(80,120,10000,10000,300,true)); // No refinery-based ceiling.
    CHECK(preferFactoryHarvester(4,120,600,10000,300,false)); // No available combat order to displace.
    CHECK_FALSE(preferFactoryHarvester(120,120,10000,10000,300,true));
}

TEST_CASE("City opening grows beyond two workers before optional tech", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    // 642 Moshpit: two workers, only 0-300 military value, 120 spice target.
    for (int army : {0,150,300}) {
        CHECK(preferFactoryHarvester(2,120,army,80000,300,true,true));
        CHECK(preferFactoryHarvester(3,120,army,80000,300,true,true));
    }
    // A spice-rich field funds a larger opening fleet before optional tech.
    CHECK(openingWorkersNeeded(4,120));
    CHECK(preferFactoryHarvester(4,120,300,80000,300,true,true));
    // Already queued workers count at the floor: do not duplicate from another factory.
    CHECK_FALSE(openingWorkersNeeded(8,120));
    CHECK_FALSE(preferFactoryHarvester(8,120,300,80000,300,true,true));
    CHECK(preferFactoryHarvester(8,120,4800,80000,300,true,true));
    CHECK(preferFactoryHarvester(60,120,80000,80000,300,true,true));
    // A modest field keeps the established four-worker opening.
    CHECK(openingWorkersNeeded(3,8));
    CHECK_FALSE(openingWorkersNeeded(4,8));
    CHECK_FALSE(preferFactoryHarvester(4,8,300,80000,300,true,true));
    // Lower map/spice targets remain authoritative, including exhausted fields.
    CHECK_FALSE(openingWorkersNeeded(2,2));
    CHECK_FALSE(openingWorkersNeeded(0,0));
    CHECK_FALSE(preferFactoryHarvester(2,2,0,80000,300,true,true));
    CHECK_FALSE(preferFactoryHarvester(0,0,0,80000,300,true,true));
    // Vanilla keeps its established army/worker balance.
    CHECK_FALSE(preferFactoryHarvester(2,120,300,80000,300,true));
}

TEST_CASE("Refinery forecasts count the first delivery and only marginal shared-bay income", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    // A new worker completes one full load within four minutes, not zero income
    // followed by a second full harvesting delay. Existing fleet income is common.
    CHECK(refineryProceeds(3,1,true,320,1757,1200,8200,1120,700,4)==686);
    CHECK(refineryProceeds(3,1,false,320,1757,1200,8200,1120,700,4)==0);
    CHECK(refineryProceeds(3,1,true,320,1757,1200,14000,1120,700,4)==0);
    CHECK(refineryProceeds(6,1,false,320,1757,1200,8200,1120,700,4)>0);
    CHECK(refineryProceeds(6,2,false,320,1757,1200,8200,1120,700,4)==0);
    Investment refinery{526,320,4,9400,1000,686};
    Investment mediumR{143,62,1,4350,1000};
    CHECK(preferRefinery(refinery,mediumR,true,false)); // Useful bay/worker before factory supply exists.
    CHECK_FALSE(preferRefinery(refinery,mediumR,considerRefinery(false,true,true),false));
    refinery.confidence=500;
    CHECK_FALSE(preferRefinery(refinery,mediumR,true,false)); // Dangerous/depleting field.
}

TEST_CASE("Tax investment forecasts reflect weak demand, pollution and the existing growth pipeline", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    REQUIRE(zoneConfidence(2000,2000,0,0,0)==1000);
    REQUIRE(zoneConfidence(0,2000,0,0,0)==0);
    REQUIRE(zoneConfidence(2000,2000,160,0,0)==0);
    REQUIRE(zoneConfidence(2000,2000,0,0,1)<zoneConfidence(2000,2000,0,0,0));
    REQUIRE(zoneConfidence(200,2000,100,192,0)<zoneConfidence(2000,2000,0,0,0));
}

TEST_CASE("QuantBot expands tank production with surplus cash without unbounded factory growth", "[quantbot][production]") {
    REQUIRE(desiredHeavyFactories(true, 50, 59044) == 23);
    REQUIRE(desiredHeavyFactories(true, 150, 3000) == 4);
    REQUIRE(desiredHeavyFactories(true, 0, 2000) == 1);
    REQUIRE(desiredHeavyFactories(true, 10000, 1000000) == 24);
    REQUIRE(desiredHeavyFactories(false, 0, 12000) == 4);
    REQUIRE(desiredHeavyFactories(false, 0, 1000000) == 24);
    REQUIRE(desiredHeavyFactories(true, 0, 59499) == 23);
    REQUIRE(desiredHeavyFactories(true, 0, 59500) == 24);
    // Live Harkonnen treasury must expand beyond the old eight-factory cap.
    REQUIRE(desiredHeavyFactories(true, 0, 149408) == 24);
}

TEST_CASE("QuantBot converts the observed city cash surplus into troop capacity", "[quantbot][production]") {
    REQUIRE(desiredHeavyFactories(true, 0, 4499) == 1);
    REQUIRE(desiredHeavyFactories(true, 0, 4500) == 2);
    REQUIRE(desiredHeavyFactories(true, 0, 11926) == 4);
    REQUIRE(desiredHeavyFactories(true, 0, 13873) == 5);
    REQUIRE(desiredHeavyFactories(true, 0, 17922) == 7);
    REQUIRE(desiredHeavyFactories(true, 0, -100) == 1);
}

TEST_CASE("QuantBot support queues expand occupied services without duplicating pending capacity", "[quantbot][production]") {
    CHECK(supportQueueTarget(2,2,2,2,3)==3); // Busy with three unserved jobs.
    CHECK(supportQueueTarget(2,2,3,2,3)==2); // Third supplier already coming.
    CHECK(supportQueueTarget(2,2,2,1,3)==2); // An existing supplier is idle.
    CHECK(supportQueueTarget(2,2,2,2,1)==2); // A single waiting job is normal.
    CHECK(supportQueueTarget(4,2,2,0,0)==4); // Ratio anticipates a growing fleet.
    CHECK(supportQueueTarget(1,4,4,0,8)==1); // Completed repairs awaiting transport are not busy repair bays.
    CHECK(supportQueueTarget(0,0,0,0,3)==0); // No useful fleet/service to bootstrap.
}

TEST_CASE("QuantBot prioritizes the live jobs demand seen in the current game", "[quantbot][city]") {
    const auto lowResidential = rankZones(191, 56, 65, 319, 1500, 1500, false);
    REQUIRE(lowResidential[0] == Structure_ZoneCommercial);
    REQUIRE(lowResidential[1] == Structure_ZoneIndustrial);
    REQUIRE(lowResidential[2] == Structure_ZoneResidential);
    REQUIRE(rankZones(179, 60, 61, 708, 1500, 1500, false)[0] == Structure_ZoneCommercial);
    REQUIRE(rankZones(219, 72, 73, 800, 1500, 1500, false)[0] == Structure_ZoneCommercial);
    // Normalize different valve ranges: R1000/2000 < C1000/1500.
    REQUIRE(rankZones(0, 10, 10, 1000, 1000, 0, false)[0] == Structure_ZoneCommercial);
    // When jobs demand falls, residential can win again.
    REQUIRE(rankZones(191, 56, 65, 1600, 500, 400, false)[0] == Structure_ZoneResidential);
}

TEST_CASE("QuantBot invests in spice without counting the whole map for every house", "[quantbot][economy]") {
    REQUIRE(desiredSpiceHarvesters(600000, 4, 40) == 40);
    REQUIRE(desiredSpiceHarvesters(60000, 4, 40) == 6);
    REQUIRE(desiredSpiceHarvesters(0, 4, 40) == 0);
    REQUIRE(desiredSpiceHarvesters(600000, 4, 10) == 10);
    REQUIRE(desiredSpiceRefineries(40, 21) == 8);
    REQUIRE(desiredSpiceRefineries(5, 21) == 2);
}

TEST_CASE("City power reserve covers growth and generator losses without doubling small bases", "[quantbot][power]") {
    REQUIRE(cityPowerReserve(0, 1000) == 0);
    REQUIRE(cityPowerReserve(100, 0) == 25);
    REQUIRE(cityPowerReserve(101, 0) == 26);
    REQUIRE(cityPowerReserve(1000, 1000) == 500);
    REQUIRE(cityPowerReserve(2000, 1000) == 1000);
    REQUIRE(cityPowerReserve(6000, 1000) == 1500);
    REQUIRE(cityPowerReserve(14000, 1000) == 3500);
    REQUIRE(cityPowerReserve(6000, 2000) == 2000);
    REQUIRE(cityPowerReserve(-100, 1000) == 0);
}

TEST_CASE("Funded Harkonnen develops its city while expanding heavy production", "[quantbot][city][production]") {
    // Live match: 26,298 credits, 15/5/9 zones, all valves saturated, idle CY.
    REQUIRE(desiredHeavyFactories(true, 0, 23000) == 9);
    REQUIRE(desiredHeavyFactories(true, 0, 26298) == 10);
    // Consecutive accepted orders balance city types under equal normalized demand.
    int r = 15, c = 5, i = 9;
    int builtR = 0, builtC = 0, builtI = 0;
    for (int n = 0; n < 50; ++n) {
        const auto zone = rankZones(r, c, i, 2000, 1500, 1500, false)[0];
        REQUIRE(zone != NONE_ID);
        if (zone == Structure_ZoneResidential) { ++r; ++builtR; }
        if (zone == Structure_ZoneCommercial) { ++c; ++builtC; }
        if (zone == Structure_ZoneIndustrial) { ++i; ++builtI; }
    }
    REQUIRE(builtR > 0);
    REQUIRE(builtC > 0);
    REQUIRE(builtI > 0);
    REQUIRE(std::abs(c - i) <= 1);
    REQUIRE(std::abs(r - 3 * c) <= 3);
}

TEST_CASE("Factory expansion reacts to busy production and recent losses without spending the reserve", "[quantbot][production]") {
    REQUIRE(desiredHeavyFactories(true,0,8000,8,6,0) == 10);
    REQUIRE(desiredHeavyFactories(true,0,8000,8,5,0) == 3);
    REQUIRE(desiredHeavyFactories(true,0,8000,8,0,2) == 12);
    REQUIRE(desiredHeavyFactories(true,0,7999,8,8,4) == 3);
    REQUIRE(desiredHeavyFactories(true,0,10000,23,23,4) == 24);
    REQUIRE(desiredHeavyFactories(false,0,8000,8,6,0) == 10);
}

TEST_CASE("Refinery throughput raises the worker target only for a viable spice field", "[quantbot][economy]") {
    REQUIRE(refineryThroughputHarvesterTarget(2000, 4, 10, 40) == 4);
    REQUIRE(refineryThroughputHarvesterTarget(20000, 6, 8, 40) == 12);
    REQUIRE(refineryThroughputHarvesterTarget(20000, 18, 8, 40) == 18);
    REQUIRE(refineryThroughputHarvesterTarget(20000, 6, 40, 20) == 20);
}

TEST_CASE("Attack commitment is reproducible without mutable random state", "[quantbot][attack]") {
    std::array<bool, 101> seen{};
    for (Uint32 cycle = 0; cycle < 10000; ++cycle) {
        const int firstPeer = attackCommitmentPercent(1859147109u, cycle, 2, 3);
        // Other decisions cannot perturb this peer's result or a replay/load.
        attackCommitmentPercent(5, cycle + 1, 6, 7);
        REQUIRE(firstPeer == attackCommitmentPercent(1859147109u, cycle, 2, 3));
        REQUIRE(firstPeer >= 20);
        REQUIRE(firstPeer <= 100);
        seen[firstPeer] = true;
    }
    REQUIRE(seen[20]);
    REQUIRE(seen[100]);
}

TEST_CASE("Main harvester strikes are deterministic and use the entire available force", "[quantbot][attack][multiplayer]") {
    bool sawStrikeWindow = false;
    bool sawHuntWindow = false;
    for (Uint32 cycle = 0; cycle < 10000; ++cycle) {
        const bool strike = shouldUseMainHarvesterStrike(1859147109u, cycle, 2, 3);
        REQUIRE(strike == shouldUseMainHarvesterStrike(1859147109u, cycle, 2, 3));
        sawStrikeWindow |= strike;
        sawHuntWindow |= !strike;
    }
    REQUIRE(sawStrikeWindow);
    REQUIRE(sawHuntWindow);
    REQUIRE(attackForceBudget(10000, 40, false) == 4000);
    REQUIRE(attackForceBudget(10000, 40, true) == 10000);
}

TEST_CASE("Opportunistic reactor strikes require a nearby wing and clear approach", "[quantbot][attack]") {
    REQUIRE(easyReactorStrike(3, 6, 0));
    REQUIRE_FALSE(easyReactorStrike(2, 6, 0));
    REQUIRE_FALSE(easyReactorStrike(3, 6, 1));
    REQUIRE_FALSE(easyReactorStrike(0, 0, 0));
}

TEST_CASE("Wealth funds city yards regardless of zone demand while preserving working cash", "[quantbot][city]") {
    REQUIRE(cityConstructionYardTarget(1500, 0, 0, 0) == 1);
    REQUIRE(cityConstructionYardTarget(1500, 2000, 1500, 1500) == 4);
    REQUIRE(cityConstructionYardTarget(20000, 2000, 1500, 0) == 5);
    REQUIRE(cityConstructionYardTarget(50000, 2000, 1500, 1500) == 6);
    REQUIRE(cityConstructionYardTarget(20000, 0, 0, 0) == 5);
    REQUIRE(cityConstructionYardTarget(50000, 0, 0, 1500) == 6);
    REQUIRE(cityConstructionYardTarget(100000, -1000, -500, -500) == 8);
    REQUIRE(cityConstructionYardTarget(1000000, 2000, 1500, 1500) == 8);
    REQUIRE(canFundCityYard(2000, 1000, 1, 4, 0));
    REQUIRE_FALSE(canFundCityYard(1999, 1000, 1, 4, 0));
    REQUIRE_FALSE(canFundCityYard(10000, 1000, 4, 4, 1000));
    REQUIRE_FALSE(canFundCityYard(4000, 1500, 2, 6, 3000));
    REQUIRE(canFundCityYard(4500, 1500, 2, 6, 3000));
    REQUIRE_FALSE(canFundCityYard(10000, 0, 1, 4, 1000));
    // Existing yards plus all queued/live MCVs satisfy capacity: no duplicate orders.
    REQUIRE_FALSE(canFundCityYard(100000, 1500, 2 + 6, 8, 3000));
}

TEST_CASE("Small armies retain a base defender while large armies reserve ten percent", "[quantbot][defence]") {
    REQUIRE(baseDefenderTarget(0) == 0);
    REQUIRE(baseDefenderTarget(1) == 1);
    REQUIRE(baseDefenderTarget(9) == 1);
    REQUIRE(baseDefenderTarget(20) == 2);
    REQUIRE(baseDefenderTarget(100) == 10);
}

TEST_CASE("Vanilla ignores power but city and other mods retain their rules", "[vanilla][power]") {
    REQUIRE_FALSE(DuneCity::powerRulesEnabled(false, "vanilla"));
    REQUIRE_FALSE(DuneCity::powerRulesEnabled(false, ""));
    REQUIRE(DuneCity::powerRulesEnabled(true, "vanilla"));
    REQUIRE(DuneCity::powerRulesEnabled(true, "dunecity"));
    REQUIRE(DuneCity::powerRulesEnabled(false, "Tornie"));
}
TEST_CASE("Spice-rich vanilla funds a larger fleet and preserves low-cash factory limits", "[vanilla][economy]") {
    REQUIRE(DuneCity::vanillaHarvesterCapacity(40) == 60);
    REQUIRE(DuneCity::vanillaHarvesterCapacity(0) == 0);
    REQUIRE(DuneCity::vanillaHarvesterTarget(640916, 5, 60) == 60);
    REQUIRE(DuneCity::vanillaHarvesterTarget(200000, 5, 60) == 26);
    REQUIRE(DuneCity::vanillaHarvesterTarget(640916, 5, 10) == 10);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 6, 8000) == 2);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 40, 8000) == 13);
    REQUIRE(desiredSpiceRefineries(60, 42) == 15); // build the next refinery before the fleet stalls at 42
    REQUIRE(desiredSpiceRefineries(60, 60) == 20);
}

TEST_CASE("Vanilla cash reserves unlock yard expansion before the harvester target", "[quantbot][vanilla]") {
    REQUIRE(DuneCity::vanillaYardTarget(100000, 1) == 8);
    REQUIRE(DuneCity::vanillaYardTarget(67782, 6) == 7);
    REQUIRE(DuneCity::vanillaYardTarget(50000, 16) == 6);
    REQUIRE(DuneCity::vanillaYardTarget(100000, 60) == 8);
    REQUIRE(DuneCity::vanillaYardTarget(1000, 60) == 1);
    REQUIRE(DuneCity::vanillaAttackThreshold(32000, 3) == 24000);
    REQUIRE(DuneCity::vanillaAttackThreshold(32000, 2) == 28000);
    REQUIRE(DuneCity::vanillaAttackThreshold(8000, 3) == 8000);
    REQUIRE(DuneCity::vanillaAttackThreshold(32000, 1) == 32000);
}

TEST_CASE("Vanilla Brutal attack commitment remains reproducible and favours larger waves", "[quantbot][multiplayer]") {
    int originalTotal=0, brutalTotal=0;
    for (Uint32 cycle=0; cycle<10000; cycle+=17) {
        const int original=attackCommitmentPercent(753675852u,cycle,1,18);
        const int brutal=difficultyAttackCommitment(753675852u,cycle,1,18,3);
        REQUIRE(brutal == difficultyAttackCommitment(753675852u,cycle,1,18,3));
        REQUIRE(brutal >= original);
        REQUIRE(brutal >= 20);
        REQUIRE(brutal <= 100);
        REQUIRE(difficultyAttackCommitment(753675852u,cycle,1,18,1) == original);
        originalTotal+=original; brutalTotal+=brutal;
    }
    REQUIRE(brutalTotal > originalTotal);
}

TEST_CASE("Vanilla prioritises parallel affordable MCVs while preserving recovery cash", "[quantbot][vanilla]") {
    REQUIRE(DuneCity::prioritizeVanillaMcv(93000, 2, 1, 0, 900));
    REQUIRE(DuneCity::prioritizeVanillaMcv(93000, 2, 1, 1, 900));
    REQUIRE(DuneCity::prioritizeVanillaMcv(93000, 2, 1, 6, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(93000, 2, 1, 7, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(93000, 2, 6, 2, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(93000, 2, 8, 0, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(1500, 40, 0, 0, 900));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(5000, 2, 1, 0, 900));
    REQUIRE(DuneCity::prioritizeVanillaMcv(10000, 2, 1, 0, 900));
}

TEST_CASE("Vanilla learning preserves ground production when aircraft dominate damage scores", "[quantbot][vanilla]") {
    const auto mix = DuneCity::balancedVanillaUnitMix({71,721,450,911,7847},{500,1000,3500,3500,1500});
    REQUIRE(mix[4] == 2500);
    REQUIRE(mix[2] >= 2500);
    REQUIRE(mix[3] >= 2500);
    int total=0;
    for (const int weight : mix) { REQUIRE(weight >= 0); total+=weight; }
    REQUIRE(total == 10000);
    const auto stable = DuneCity::balancedVanillaUnitMix({500,1000,3500,3500,1500},{500,1000,3500,3500,1500});
    REQUIRE(stable == std::array<int,5>{500,1000,3500,3500,1500});
    const auto empty = DuneCity::balancedVanillaUnitMix({0,0,0,0,0},{0,0,0,0,0});
    REQUIRE(empty == std::array<int,5>{2500,2500,2500,2500,0});
}

TEST_CASE("Wealthy vanilla expands factories without waiting for a full harvester fleet", "[quantbot][vanilla]") {
    // Recorded six-minute failure: 94k cash, sixteen harvesters, one factory.
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 16, 94000) == 22);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 2, 100000) == 23);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 6, 50000) == 11);
    REQUIRE(DuneCity::vanillaFactoryTarget(24, 6, 10000) == 2);
    REQUIRE(DuneCity::vanillaFactoryTarget(4, 60, 100000) == 4);
    REQUIRE(DuneCity::vanillaFactoryTarget(99, 99, 1000000) == 24);
    REQUIRE(DuneCity::prioritizeVanillaFactory(94000, 1, 1, 22));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaFactory(94000, 2, 1, 22)); // advance tech next
    REQUIRE(DuneCity::prioritizeVanillaFactory(94000, 2, 2, 22)); // a new yard adds production lanes
    REQUIRE_FALSE(DuneCity::prioritizeVanillaFactory(94000, 4, 2, 22)); // queued factories count
    REQUIRE_FALSE(DuneCity::prioritizeVanillaFactory(19000, 1, 2, 3));
    REQUIRE_FALSE(DuneCity::prioritizeVanillaFactory(94000, 2, 2, 2));
}

TEST_CASE("Parallel MCV orders account for earlier factories and subsequent deployment", "[quantbot][vanilla]") {
    int credits = 98000, pending = 0;
    for (int factory=0; factory<12; ++factory) {
        if (DuneCity::prioritizeVanillaMcv(credits, 2, 1, pending, 900)) {
            ++pending;
            credits -= 900;
        }
    }
    REQUIRE(pending == 7);
    REQUIRE(DuneCity::vanillaMcvShortfall(credits, 2, 1, pending) == 0);
    // Deployment converts one pending MCV to a yard, without creating extra demand.
    REQUIRE(DuneCity::vanillaMcvShortfall(credits, 2, 2, pending-1) == 0);
    REQUIRE(DuneCity::vanillaMcvShortfall(50000, 2, 1, 2) == 3);
    REQUIRE_FALSE(DuneCity::prioritizeVanillaMcv(10000, 2, 1, 1, 900));
}

TEST_CASE("Light vehicle combat returns compete with heavy units by replacement cost", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights baseline{440,880,3080,3080,1320,600,0,600};
    Weights scores{performanceScore(3000,600,300),performanceScore(3000,1200,600),
        performanceScore(3000,900,450),performanceScore(3000,1400,700),0,
        performanceScore(3000,300,150),0,performanceScore(3000,400,200)};
    // Provide combat evidence: without losses the opening mix is intentionally
    // retained, so performance must not steer production yet.
    const auto successful = allocate(scores,baseline,true,true,10000,10000);
    REQUIRE(successful[5] > successful[0]);
    REQUIRE(successful[5] > successful[7]);
    REQUIRE(successful[5]+successful[7] > 1200);
    scores[5] = performanceScore(3000,3000,150);
    const auto costly = allocate(scores,baseline,true,true,10000,10000);
    REQUIRE(costly[5] < successful[5]);
    REQUIRE(costly[7] > costly[5]);
    REQUIRE(costly[6] == 0);
}
TEST_CASE("Eight-type mix keeps defaults before combat and handles zero damage safely", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights baseline{440,880,3080,3080,1320,600,0,600};
    const auto expected = normalize(baseline);
    REQUIRE(allocate({},baseline,true,true) == expected);
    REQUIRE(allocate({0,0,0,0,0,100,0,0},baseline,false,true) == expected);
    REQUIRE(performanceScore(-100,0,0) == 0);
    REQUIRE(performanceScore(500,0,0) == 500000000);
    REQUIRE(performanceScore(2000000000,2000000000,150) > 0);
}
TEST_CASE("Adaptive eight-type mix preserves limits and deterministic peer results", "[quantbot][unitmix][multiplayer]") {
    using namespace UnitMixPolicy;
    const Weights baseline{440,880,3080,3080,1320,600,0,600};
    for (bool vanilla : {false,true}) {
        for (size_t strongest=0; strongest<8; ++strongest) {
            Weights scores{}; scores[strongest]=10000000;
            const auto first=allocate(scores,baseline,true,vanilla);
            allocate({4,3,2,1,0,4,3,2},baseline,true,vanilla);
            REQUIRE(allocate(scores,baseline,true,vanilla) == first);
            int total=0;
            for (int share : first) { REQUIRE(share>=0); REQUIRE(share<=8000); total+=share; }
            REQUIRE(total == 10000);
            if (vanilla) REQUIRE(first[4]<=2500);
        }
    }
}
TEST_CASE("Vanilla unit mix replaces its opening prior as combat evidence accumulates", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights baseline{5000,5000,0,0,0,0,0,0};
    const Weights scores{1000000,0,0,0,0,0,0,0};
    const auto early = allocate(scores, baseline, true, true, 0, 10000);
    const auto battleTested = allocate(scores, baseline, true, true, 90000, 10000);
    REQUIRE(evidenceConfidenceBps(0, 10000) == 0);
    REQUIRE(evidenceConfidenceBps(90000, 10000) == 9000);
    REQUIRE(early[0] == 5000);
    // The performance signal is 90%, then the universal 80% single-unit cap
    // keeps the mix from collapsing onto one type.
    REQUIRE(battleTested[0] == 8000);
    REQUIRE(battleTested[1] == 2000);
}
TEST_CASE("Light vehicle selection fills value deficits and counts queued units", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    REQUIRE(deficit(1000,10000,1,150) > deficit(500,10000,1,200));
    REQUIRE(deficit(1000,10000,7,150) < 0);
    REQUIRE(deficit(0,10000,0,150) == 0);
}

TEST_CASE("Opening light shares shrink with tech and unavailable units receive no allocation", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights configured{500,1000,3500,3500,1500,0,0,0};
    const std::array<bool,8> full{true,true,true,true,true,true,false,true};
    for (int tech=4; tech<=8; ++tech) {
        const auto mix = openingMix(tech,configured,full);
        REQUIRE(mix[5]+mix[6]+mix[7] == (tech>=7 ? 400 : tech>=5 ? 800 : 1500));
        REQUIRE(mix[6] == 0);
        REQUIRE(mix[7] > mix[5]);
        int total=0; for (int value : mix) total+=value;
        REQUIRE(total == 10000);
    }
    const auto tankOnly = openingMix(4,configured,{true,false,false,false,false,true,false,true});
    REQUIRE(tankOnly[0] == 8500);
    REQUIRE(tankOnly[3] == 0);
    REQUIRE(tankOnly[4] == 0);
    const auto lightsOnly = openingMix(3,configured,{false,false,false,false,false,true,false,true});
    REQUIRE(lightsOnly[5]+lightsOnly[7] == 10000);
    const auto noFactory = openingMix(8,configured,{});
    REQUIRE(noFactory == Mix{});
}
TEST_CASE("Opening allocation follows upgrades and missing producer recovery", "[quantbot][unitmix]") {
    using namespace UnitMixPolicy;
    const Weights configured{500,1000,3500,3500,1500,0,0,0};
    const auto before = openingMix(8,configured,{true,false,false,false,false,true,false,true});
    const auto after = openingMix(8,configured,{true,true,true,true,true,true,false,true});
    REQUIRE(before[0] == 9600);
    REQUIRE(after[0] < before[0]);
    REQUIRE(after[3] > 0);
    REQUIRE(after[4] > 0);
    REQUIRE(after[5]+after[7] == 400);
    REQUIRE(openingMix(8,{}, {true,false,false,false,false,false,false,false})[0] == 10000);
}

TEST_CASE("Damage reward values actual HP removed and gives the killer twenty percent", "[quantbot][reward]") {
    const auto partial = CombatReward::hit(600,300000,300000,200000,true,true);
    REQUIRE(partial.damageMilli == 200000);
    REQUIRE(partial.killBonusMilli == 0);
    const auto kill = CombatReward::hit(600,300000,20000,0,true,true);
    REQUIRE(kill.damageMilli == 40000);
    REQUIRE(kill.hpRemovedMilli == 20000);
    REQUIRE(kill.killBonusMilli == 120000);
    REQUIRE(kill.total() == 160000);
    REQUIRE(kill.kills == 1);
    REQUIRE(CombatReward::hit(600,300000,0,0,true,true).total() == 0);
    REQUIRE(CombatReward::hit(600,300000,20000,0,false,true).total() == 0);
    REQUIRE(CombatReward::hit(600,300000,20000,30000,true,true).total() == 0);
    REQUIRE(CombatReward::hit(600,300000,20000,0,true,false).killBonusMilli == 0);
    REQUIRE(CombatReward::hit(600,300000,1000,0,true,true).damageMilli == 2000);
}
TEST_CASE("Killing blow raises unit efficiency and reward counters survive save load", "[quantbot][reward][save-compat]") {
    const auto reward = CombatReward::hit(600,300000,300000,0,true,true);
    REQUIRE(reward.total() == 720000);
    REQUIRE(UnitMixPolicy::performanceScore(reward.total(),300000,300000)
        > UnitMixPolicy::performanceScore(reward.damageMilli,300000,300000));
    OMemoryStream out; reward.save(out); out.writeUint32(0x12345678);
    IMemoryStream in(out.getData(),static_cast<int>(out.getDataLength()));
    CombatReward::Totals loaded; loaded.load(in);
    REQUIRE(loaded.total() == reward.total());
    REQUIRE(loaded.damageMilli == reward.damageMilli);
    REQUIRE(loaded.killBonusMilli == reward.killBonusMilli);
    REQUIRE(loaded.kills == 1);
    REQUIRE(loaded.hits == 1);
    REQUIRE(loaded.hpRemovedMilli == 300000);
    REQUIRE(in.readUint32() == 0x12345678);
}

TEST_CASE("Factory priorities require funded demand and no spare lane", "[quantbot][production]") {
    REQUIRE(needsProductionLane(2,2,2,4000,6000,2000,600));
    REQUIRE_FALSE(needsProductionLane(2,3,2,4000,6000,2000,600));
    REQUIRE_FALSE(needsProductionLane(2,2,1,4000,6000,2000,600));
    REQUIRE_FALSE(needsProductionLane(2,2,2,0,6000,2000,600));
    REQUIRE_FALSE(needsProductionLane(2,2,2,4000,3000,2000,600));
    REQUIRE(needsProductionLane(4,4,4,4000,6000,2000,500)); // no arbitrary air cap
}
TEST_CASE("Launcher spacing triggers inside its safe range", "[quantbot][combat]") {
    REQUIRE(needsKiting(7, 9, false, true));
    REQUIRE(needsKiting(1, 9, false, true));
    REQUIRE_FALSE(needsKiting(8, 9, false, true));
    REQUIRE_FALSE(needsKiting(1, 9, true, true));
    REQUIRE_FALSE(needsKiting(1, 9, false, false));
}

TEST_CASE("Light raiders evade tanks and prefer vulnerable mobile prey", "[quantbot][combat]") {
    REQUIRE(isLightRaider(Unit_Trike));
    REQUIRE(isLightRaider(Unit_Quad));
    REQUIRE_FALSE(isLightRaider(Unit_Launcher));
    REQUIRE(isArmoredTank(Unit_Tank));
    REQUIRE(isArmoredTank(Unit_Devastator));
    REQUIRE(isArmoredTank(Unit_EliteSiegeTank));
    REQUIRE_FALSE(isArmoredTank(Unit_Launcher));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Launcher));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Harvester));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Trike));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Quad));
    REQUIRE(isLightRaiderPreferredTarget(Unit_Troopers));
    REQUIRE_FALSE(isLightRaiderPreferredTarget(Unit_Tank));
    REQUIRE_FALSE(isLightRaiderPreferredTarget(Structure_Refinery));
}

TEST_CASE("Funded ornithopter backlog expands mixed air production", "[quantbot][production]") {
    CHECK(needsAirProductionLane(1,1,1,1,1800,6000,1000,500,600,2000,false));
    CHECK(needsAirProductionLane(3,3,3,1,1800,6000,1000,500,600,2000,false)); // two carryall lanes
    CHECK(needsAirProductionLane(4,4,3,3,1800,6000,1000,500,600,2000,false));
    CHECK_FALSE(needsAirProductionLane(3,4,3,3,1800,6000,1000,500,600,2000,false)); // pending factory
    CHECK_FALSE(needsAirProductionLane(3,3,1,3,1800,6000,1000,500,600,2000,false)); // idle capacity
    CHECK_FALSE(needsAirProductionLane(1,1,1,0,1800,6000,1000,500,600,2000,false)); // tech locked
    CHECK_FALSE(needsAirProductionLane(1,1,1,1,569,6000,1000,500,600,2000,false)); // queued units meet most demand
    CHECK_FALSE(needsAirProductionLane(1,1,1,1,1800,3000,1000,500,600,2000,false)); // cannot fund lane + unit
    CHECK_FALSE(needsAirProductionLane(1,1,1,1,1800,6000,1000,500,600,50,false)); // current Sardaukar army cap
    CHECK_FALSE(needsAirProductionLane(1,1,1,1,1800,6000,1000,500,600,2000,true));
}

TEST_CASE("Rocket turret power setting is independent of vanilla power bypass", "[quantbot][power]") {
    REQUIRE_FALSE(DuneCity::powerRulesEnabled(false,"vanilla"));
    REQUIRE(DuneCity::rocketTurretPowered(false,100,1000));
    REQUIRE_FALSE(DuneCity::rocketTurretPowered(true,100,1000));
    REQUIRE(DuneCity::rocketTurretPowered(true,1000,1000));
    REQUIRE(DuneCity::rocketTurretPowered(true,1100,1000));
}

TEST_CASE("Unscaled generators keep output until destruction while city reactors scale", "[power]") {
    // The `false` helper is what scoutposts and the non-city reactor use: full
    // output at any surviving health, nothing once destroyed.
    using DuneCity::generatorOutput;
    REQUIRE(generatorOutput(100, 100, 100, false) == 100);
    REQUIRE(generatorOutput(100, 1, 100, false) == 100);
    REQUIRE(generatorOutput(300, 25, 100, false) == 300);
    REQUIRE(generatorOutput(100, 0, 100, false) == 0);
    REQUIRE(generatorOutput(1000, 25, 100, true) == 250);
    REQUIRE(generatorOutput(1000, 25, 100, false) == 1000);
    REQUIRE(generatorOutput(1000, 0, 100, true) == 0);
    const int before = generatorOutput(100,25,100,false);
    REQUIRE(100 - before + generatorOutput(100,0,100,false) == 0);
    REQUIRE(0 - generatorOutput(100,0,100,false) == 0); // destructor after lethal damage
}

TEST_CASE("Windtrap output follows health with no half-output floor", "[power]") {
    using DuneCity::windtrapOutput;
    REQUIRE(windtrapOutput(100, 100, 100) == 100);
    REQUIRE(windtrapOutput(100, 50, 100) == 50);    // half health, half power
    REQUIRE(windtrapOutput(300, 25, 100) == 75);
    REQUIRE(windtrapOutput(100, 200, 200) == 100);  // never above nominal
    REQUIRE(windtrapOutput(100, 100, 200) == 50);
    REQUIRE(windtrapOutput(100, 1, 100) == 1);      // no floor short of destruction
    REQUIRE(windtrapOutput(100, 0, 100) == 0);
    REQUIRE(windtrapOutput(100, 50, 0) == 0);
    // The bare-rock placement penalty alone halves a windtrap's contribution.
    REQUIRE(windtrapOutput(100, 200, 200) - windtrapOutput(100, 100, 200) == 50);
}

TEST_CASE("Main waves require both actual numbers and value", "[quantbot][attack]") {
    REQUIRE_FALSE(viableMainWave(1,600));
    REQUIRE_FALSE(viableMainWave(5,10000));
    REQUIRE_FALSE(viableMainWave(20,2999));
    REQUIRE(viableMainWave(6,3000));
}

TEST_CASE("Harvest anchors resist churn but leave danger and depleted fields", "[quantbot][rally]") {
    REQUIRE_FALSE(replaceHarvestAnchor(true,8,12,29999,30000));
    REQUIRE_FALSE(replaceHarvestAnchor(true,8,9,30000,30000));
    REQUIRE(replaceHarvestAnchor(true,8,10,30000,30000));
    REQUIRE(replaceHarvestAnchor(false,8,1,0,30000));
    REQUIRE(replaceHarvestAnchor(true,0,1,0,30000));
    REQUIRE_FALSE(replaceHarvestAnchor(true,9,11,90000,30000));
    REQUIRE(replaceHarvestAnchor(true,9,12,90000,30000));
}

TEST_CASE("Heavy allocation picks the largest funded deficit, not a fixed priority", "[quantbot][allocation]") {
    std::array<AllocationCandidate,3> c = {{{300,3000,1000,true},{600,600,5000,true},{450,450,4000,true}}};
    REQUIRE(largestAffordableDeficit(c,10000,1000,20000)==1);
    REQUIRE(largestAffordableDeficit(c,10000,500,20000)==2); // Largest deficit cannot be afforded.
    c[1].available=false;
    REQUIRE(largestAffordableDeficit(c,10000,1000,20000)==2);
    c[2].committedValue=10000; // Includes queued launchers; don't keep ordering them.
    REQUIRE(largestAffordableDeficit(c,10000,1000,20000)==-1); // No tank fallback.
}

TEST_CASE("Heavy allocation bootstraps and respects cash and army limits", "[quantbot][allocation]") {
    std::array<AllocationCandidate,2> c = {{{300,0,5000,true},{600,0,5000,true}}};
    REQUIRE(largestAffordableDeficit(c,0,600,1000)==0); // Stable tie, no RNG.
    REQUIRE(largestAffordableDeficit(c,0,299,1000)==-1);
    REQUIRE(largestAffordableDeficit(c,0,1000,299)==-1);
    c[0].targetBps=0;
    REQUIRE(largestAffordableDeficit(c,0,600,1000)==1);
    REQUIRE(largestAffordableDeficit(c,800,1000,1000)==-1);
}

TEST_CASE("Balanced small armies still fill idle heavy-factory lanes", "[quantbot][allocation]") {
    // A damaged/queued force can have more committed category value than its
    // current military total, which makes the normal one-unit horizon look full.
    std::array<AllocationCandidate,2> c = {{{300,4500,5000,true},{600,4500,5000,true}}};
    REQUIRE(largestAffordableDeficit(c,8000,1000,80000) == -1);
    const int growthHorizon = expansionAllocationHorizon(8000,80000);
    REQUIRE(growthHorizon == 16000);
    REQUIRE(largestAffordableDeficit(c,8000,1000,80000,growthHorizon) == 0);
    REQUIRE(expansionAllocationHorizon(50000,80000) == 80000);
}

TEST_CASE("All factory classes fill the same funded live plus queued army plan", "[quantbot][production]") {
    // Heavy shares are already filled. The remaining money must fund light/air shares
    // against 100k, not fractions of the existing 80k army.
    const int target=fundedArmyTarget(80000,100000,999999);
    REQUIRE(target==100000);
    const std::array<AllocationCandidate,2> lightAir={{{300,1500,400,true},{900,3600,1000,true}}};
    REQUIRE(fundedDeficit(lightAir,80000,999999,100000,target)==1);
    const std::array<AllocationCandidate,2> heavy={{{450,15000,1500,true},{450,44000,4400,true}}};
    REQUIRE(fundedDeficit(heavy,80000,999999,100000,target)==-1);
    REQUIRE(fundedArmyTarget(80000,100000,1200)==81200);
    REQUIRE(fundedDeficit(lightAir,99900,999999,100000,target)==-1); // queued military consumes the cap
    REQUIRE_FALSE(militaryItem(Unit_Carryall));
    REQUIRE_FALSE(militaryItem(Unit_Harvester));
    REQUIRE_FALSE(militaryItem(Unit_MCV));
    REQUIRE(militaryItem(Unit_Ornithopter));
}
TEST_CASE("An explicit unit-count override replaces the value cap with rolling headroom",
          "[quantbot][production][unitlimit]") {
    using QuantBotSpendingPolicy::militaryScore;
    using QuantBotSpendingPolicy::readiness;
    // Headroom is the largest of the configured target, the spendable cash, the dearest
    // military unit and one, added to what is already committed. It therefore always
    // leaves room for one more order and advances as the army grows.
    REQUIRE(rollingMilitaryBudget(0,20000,0,450)==20000);
    REQUIRE(rollingMilitaryBudget(80000,20000,0,450)==100000);
    REQUIRE(rollingMilitaryBudget(80000,20000,250000,450)==330000);
    // Cheap armies with little cash still clear the dearest unit, so a Starport bargain
    // is reachable when credits are below its nominal value.
    REQUIRE(rollingMilitaryBudget(1000,0,100,1500)==2500);
    REQUIRE(rollingMilitaryBudget(0,0,0,0)==1);          // never a zero budget
    REQUIRE(rollingMilitaryBudget(-5,0,0,0)==1);         // negative committed value is ignored
    // No artificial stop: every step of a growing army and every unit price keeps a
    // strictly positive remaining allowance.
    int committed=0;
    for (int order=0; order<64; ++order) {
        const int budget=rollingMilitaryBudget(committed,20000,5000,450);
        REQUIRE(budget-committed>=450);
        REQUIRE(militaryScore(450,450,committed,budget,false)>0);
        const std::array<AllocationCandidate,2> candidates={{{450,committed,5000,true},{300,0,5000,true}}};
        REQUIRE(capacityFill(candidates,committed,999999,budget)>=0);
        committed+=450;
    }
    // Budgets keep advancing beyond the former quarter-int boundary. Only the int
    // representation boundary saturates; helper sums must not wrap there.
    constexpr int largeArmy = std::numeric_limits<int>::max()/4 + 20000;
    REQUIRE(rollingMilitaryBudget(largeArmy,20000,5000,450)==largeArmy+20000);
    constexpr int maximum = std::numeric_limits<int>::max();
    const std::array<AllocationCandidate,2> edgeCandidates={{{450,maximum-500,5000,true},{600,0,5000,true}}};
    REQUIRE(fundedArmyTarget(maximum-500,maximum,2000)==maximum);
    REQUIRE(fundedDeficit(edgeCandidates,maximum-500,2000,maximum,maximum)==-1);
    REQUIRE(capacityFill(edgeCandidates,maximum-500,2000,maximum)==0);
    REQUIRE(allocationHorizon(edgeCandidates,maximum-500,2000,maximum)==maximum-50);
    REQUIRE(largestAffordableDeficit(edgeCandidates,maximum-500,2000,maximum)==-1);
    REQUIRE(expansionAllocationHorizon(maximum-500,maximum)==maximum);
    REQUIRE(militaryScore(600,600,maximum-500,maximum,true)==0);
    REQUIRE(militaryScore(450,450,maximum-500,maximum,true)>0);
    const int saturated=rollingMilitaryBudget(std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),std::numeric_limits<int>::max());
    REQUIRE(saturated==militaryBudgetSaturation());
    REQUIRE(saturated>0);
    REQUIRE(int64_t(saturated)*10000<std::numeric_limits<int64_t>::max()/4);
    REQUIRE(fundedArmyTarget(1000,saturated,2000)==3000); // finite target for the helpers
    REQUIRE(readiness(1000,saturated)>0);
}
TEST_CASE("Exploration fades independently for each unit's evidence", "[quantbot][allocation]") {
    using namespace UnitMixPolicy;
    Weights scores{},rewards{},losses{},prices{};
    std::array<bool,8> available{}; available[0]=available[1]=available[3]=true;
    scores[0]=2000000; rewards[0]=100000000; losses[0]=50000000;
    scores[1]=100000; rewards[1]=100000000; losses[1]=1000000000;
    prices.fill(700000);
    const auto first=exploredScores(scores,rewards,losses,prices,available);
    REQUIRE(first[3]>0); // never tried: eligible for a real combat sample
    REQUIRE(first[1]<first[3]); // many losses: no named-unit floor rescuing poor performance
    REQUIRE(first[4]==0); // unavailable aircraft get no speculative allocation
    scores[3]=100000; rewards[3]=10000000; losses[3]=100000000;
    const auto tested=exploredScores(scores,rewards,losses,prices,available);
    REQUIRE(tested[3]<first[3]);
}

#include <players/TacticalSafetyPolicy.h>
TEST_CASE("Reactor spacing protects production and expensive facilities while allowing RCI", "[quantbot][placement]") {
    using TacticalSafetyPolicy::protectedReactorNeighbour;
    for (const int item:{Structure_ConstructionYard,Structure_HeavyFactory,Structure_HighTechFactory,
            Structure_Refinery,Structure_IX,Structure_StarPort,Structure_Palace,Structure_NuclearPlant})
        REQUIRE(protectedReactorNeighbour(item));
    for (const int item:{Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial})
        REQUIRE_FALSE(protectedReactorNeighbour(item));
}

TEST_CASE("Unit mix remembers full-match evidence across idle time and saves", "[quantbot][allocation][save-compat]") {
    using namespace UnitMixPolicy;
    PerformanceHistory original;
    Weights reward{},loss{},prices{}; prices.fill(600000);
    const std::array<bool,8> available{true,false,false,false,true,false,false,false};
    reward[0]=4000000; loss[0]=2000000;
    reward[4]=800000; loss[4]=1600000;
    auto score = [&]() {
        Weights scores{};
        for (size_t i=0;i<8;++i) scores[i]=performanceScore(original.reward[i],original.loss[i],prices[i]);
        return normalize(exploredScores(scores,original.reward,original.loss,prices,available));
    };
    original.update(100,reward,loss);
    const auto initial=score();
    original.update(1000000,reward,loss);
    REQUIRE(original.reward==reward);
    REQUIRE(original.loss==loss);
    REQUIRE(score()==initial); // Time alone cannot restore a poorly performing type's share.
    OMemoryStream output; original.save(output);
    IMemoryStream input(output.getData(),output.getDataLength());
    PerformanceHistory restored; restored.load(input);
    reward[4]+=500000; loss[4]+=100000;
    original.update(1000001,reward,loss); restored.update(1000001,reward,loss);
    REQUIRE(restored.reward==original.reward);
    REQUIRE(restored.loss==original.loss);
    REQUIRE(restored.sampled==original.sampled);
    REQUIRE(original.reward[4]==1300000);
    REQUIRE(original.loss[4]==1700000);
}
TEST_CASE("Loaded legacy evidence is replaced with the house's complete combat totals", "[quantbot][allocation][save-compat]") {
    using namespace UnitMixPolicy;
    // The old format stores cycle, initialized, previous totals, and decayed totals.
    OMemoryStream output;
    output.writeUint32(100); output.writeBool(true);
    Weights totalReward{},totalLoss{},decayedReward{},decayedLoss{};
    totalReward[4]=800000; totalLoss[4]=1600000;
    decayedReward[4]=100000; decayedLoss[4]=200000;
    for (const auto& values:{totalReward,totalLoss,decayedReward,decayedLoss})
        for (auto value:values) output.writeSint64(value);
    IMemoryStream input(output.getData(),output.getDataLength());
    PerformanceHistory restored; restored.load(input);
    restored.update(101,totalReward,totalLoss);
    REQUIRE(restored.reward==totalReward);
    REQUIRE(restored.loss==totalLoss);
}
TEST_CASE("Factory threat clearance matches nearest danger including diagonals and map edges", "[quantbot][placement]") {
    using namespace TacticalSafetyPolicy;
    // Exhaust all 3x3 threat arrangements; the reference scans sources directly.
    for (unsigned mask=0;mask<512;++mask) {
        std::vector<int> danger(9);
        for (int i=0;i<9;++i) if (mask&(1u<<i)) danger[i]=100;
        const auto clearance=enemyClearance(danger,3,3);
        for (int y=0;y<3;++y) for (int x=0;x<3;++x) {
            int expected=12;
            for (int i=0;i<9;++i) if (danger[i])
                expected=std::min(expected,std::max(std::abs(x-i%3),std::abs(y-i/3)));
            REQUIRE(clearance[y*3+x]==expected);
        }
    }
    std::vector<int> danger(20*10); danger[0]=100;
    const auto clearance=enemyClearance(danger,20,10);
    REQUIRE(footprintClearance(clearance,20,10,3,2,3,2)==3);
    REQUIRE(footprintClearance(clearance,20,10,17,7,3,3)==12);
    REQUIRE(footprintClearance(clearance,20,10,18,7,3,3)==0);
}
TEST_CASE("Factory safety outranks frontage preferences without banning constrained sites", "[quantbot][placement]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE(factorySiteRank(0,8,0,-100)>factorySiteRank(0,2,10,1000));
    REQUIRE(factorySiteRank(0,2,0,0)>factorySiteRank(100,12,10,1000));
    REQUIRE(factorySiteRank(0,1,0,-1000)>factorySiteRank(1,-1,-1,0));
    // With no threats (12 everywhere), keep ordinary tier and score preferences.
    REQUIRE(factorySiteRank(0,12,10,0)>factorySiteRank(0,12,9,1000));
    for (int item:{Structure_HeavyFactory,Structure_LightFactory,Structure_HighTechFactory,
                  Structure_Barracks,Structure_WOR}) REQUIRE(productionFactory(item));
    REQUIRE_FALSE(productionFactory(Structure_Refinery));
    REQUIRE_FALSE(productionFactory(Structure_RocketTurret));
}

#include <players/SimpleArmyPolicy.h>
#include <players/CampaignDifficultyPolicy.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
TEST_CASE("Campaign alliance gates overlapping houses and recovery independently of individual timers", "[quantbot][campaign]") {
    using namespace CampaignDifficultyPolicy;
    const auto easy=profile(0,8), medium=profile(1,8), hard=profile(2,8), brutal=profile(3,8);
    Pressure one{1,2,600,1000};
    REQUIRE_FALSE(canLaunch(easy,one,10000,0,100));
    REQUIRE_FALSE(canLaunch(medium,one,10000,0,100));
    REQUIRE(canLaunch(hard,one,10000,0,100));
    Pressure two{2,4,1200,1000};
    REQUIRE_FALSE(canLaunch(hard,two,10000,0,100));
    REQUIRE(canLaunch(brutal,two,10000,0,100));
    Pressure ended{0,0,0,1000};
    REQUIRE_FALSE(canLaunch(easy,ended,1099,0,100));
    REQUIRE(canLaunch(easy,ended,1100,0,100));
    REQUIRE_FALSE(canLaunch(easy,{},1100,1200,100));
    REQUIRE(fits(easy,{1,4,1400,0},300)); // The late Easy wave may reach 1,700.
    REQUIRE_FALSE(fits(easy,{1,4,1400,0},301)); // Value cap, even with a troop slot.
    REQUIRE_FALSE(fits(easy,{1,5,500,0},50)); // Count cap, even with cheap infantry.
    // Hard has a fixed credit budget; Brutal retains unlimited wave size.
    const Pressure largeArmy{1,40,30000,1000};
    REQUIRE_FALSE(canLaunch(hard,largeArmy,10000,0,100));
    REQUIRE(canLaunch(brutal,largeArmy,10000,0,100));
    REQUIRE_FALSE(fits(hard,largeArmy,1000));
    REQUIRE(fits(brutal,largeArmy,1000));
    REQUIRE_FALSE(canLaunch(hard,{},1100,1200,100));
    REQUIRE_FALSE(canLaunch(brutal,ended,1099,0,100));
}
TEST_CASE("Campaign wave membership and deadlines survive stream round trips", "[quantbot][campaign][save]") {
    using namespace CampaignDifficultyPolicy;
    Wave original; original.initialized=true; original.opening=1234;
    original.launched=5678; original.lastActive=9000; original.front=71; original.members={7,19,55};
    OMemoryStream out; out.open(); original.save(out);
    IMemoryStream in(out.getData(),out.getDataLength()); Wave restored; restored.load(in);
    REQUIRE(restored.initialized==original.initialized);
    REQUIRE(restored.opening==original.opening);
    REQUIRE(restored.launched==original.launched);
    REQUIRE(restored.lastActive==original.lastActive);
    REQUIRE(restored.members==original.members);
    REQUIRE(restored.front==original.front);
}
TEST_CASE("Easy campaign pressure stays small with one-to-three-minute repeat waves", "[quantbot][campaign]") {
    using namespace CampaignDifficultyPolicy;
    // Opening missions keep their old budgets; late waves can add a tank
    // behind three 450-credit launchers, without admitting a fourth launcher.
    REQUIRE(profile(0,3).value==900);
    REQUIRE(profile(0,6).value==1200);
    const auto easy=profile(0,8);
    REQUIRE(fits(easy,{1,3,1350,0},300));
    REQUIRE_FALSE(fits(easy,{1,3,1350,0},450));
    REQUIRE(easy.enemyCommitPercent==50);
    for(uint32_t seed:{0u,99480356u,540497013u,UINT32_MAX})
        for(uint32_t house=0;house<6;++house)
            for(uint32_t cycle:{45000u,90000u,120000u}) {
                const auto easyDelay=repeatDelayMs(0,seed,cycle,house);
                const auto mediumDelay=repeatDelayMs(1,seed,cycle,house);
                REQUIRE(easyDelay>=60000);
                REQUIRE(easyDelay<=180000);
                REQUIRE(mediumDelay>=60000);
                REQUIRE(mediumDelay<=180000);
                REQUIRE(repeatDelayMs(0,seed,cycle,house)==easyDelay);
            }
}
TEST_CASE("Campaign windtrap accounting covers commitments without duplicate generators", "[quantbot][campaign][power]") {
    using namespace CampaignDifficultyPolicy;
    REQUIRE(needsWindtrap(100,140,0,0,false));
    REQUIRE(needsWindtrap(150,140,20,0,false));
    REQUIRE(needsWindtrap(150,140,0,20,false));
    REQUIRE_FALSE(needsWindtrap(150,140,0,10,false));
    REQUIRE_FALSE(needsWindtrap(100,140,0,0,true));
}
TEST_CASE("Campaign attack sizes scale with difficulty and retain a reserve", "[quantbot][combat]") {
    std::vector<SimpleArmyPolicy::Responder> army;
    for (unsigned i=0; i<20; ++i) army.push_back({i+1,100,0});
    REQUIRE(SimpleArmyPolicy::limitedAttack(2000,0,25,army).size()==5);
    REQUIRE(SimpleArmyPolicy::limitedAttack(2000,0,40,army).size()==8);
    REQUIRE(SimpleArmyPolicy::limitedAttack(2000,0,50,army).size()==10);
    REQUIRE(SimpleArmyPolicy::limitedAttack(2000,0,60,army).size()==12);
    REQUIRE(SimpleArmyPolicy::limitedAttack(2000,0,0,army).empty());
    REQUIRE(SimpleArmyPolicy::limitedAttack(2000,400,25,army).size()==1);
    REQUIRE(SimpleArmyPolicy::limitedAttack(2000,500,25,army).empty());
    REQUIRE(SimpleArmyPolicy::limitedAttack(2000,800,25,army).empty());
}
TEST_CASE("Campaign waves handle mixed costs and small armies without stacking", "[quantbot][combat]") {
    using namespace SimpleArmyPolicy;
    const std::vector<Responder> army{{9,300,0},{3,120,0},{1,600,0},{4,60,0}};
    // A large first candidate cannot prevent affordable troops being chosen.
    REQUIRE(limitedAttack(1080,0,25,army)==std::vector<uint32_t>{3,4});
    auto reversed=army; std::reverse(reversed.begin(),reversed.end());
    REQUIRE(limitedAttack(1080,0,25,reversed)==limitedAttack(1080,0,25,army));
    REQUIRE(limitedAttack(300,0,25,{{7,300,0}})==std::vector<uint32_t>{7});
    REQUIRE(limitedAttack(600,300,25,{{8,300,0}}).empty());
    REQUIRE(limitedAttack(600,0,0,{{8,300,0}}).empty());
    REQUIRE(limitedAttack(600,0,25,{{8,0,0}}).empty());
    REQUIRE(attackBudget(INT32_MAX,100)==INT32_MAX);
    REQUIRE(attackBudget(1000,150)==1000);
    REQUIRE(attackBudget(-1000,25)==0);
}
TEST_CASE("Local defence scales to the enemy instead of a fixed reserve", "[quantbot][combat]") {
    using namespace SimpleArmyPolicy;
    std::vector<Responder> army;
    for (unsigned i=0;i<100;++i) army.push_back({i+1,300,int(i+1)});
    // A lone raider needs two tanks, a 6k enemy force needs 25, not all 100.
    REQUIRE(reinforcements(300,0,army).size()==2);
    REQUIRE(reinforcements(6000,0,army).size()==25);
    // Troops already committed prevent each incoming hit recruiting another team.
    REQUIRE(reinforcements(300,600,army).empty());
    REQUIRE(reinforcements(6000,6000,army).size()==5);
    REQUIRE(reinforcements(100000,0,army).size()==100);
    REQUIRE(reinforcements(0,0,army).empty());
    REQUIRE(reinforcements(600,0,{}).empty());
}
TEST_CASE("Local defence is nearest first with deterministic ties", "[quantbot][combat]") {
    using namespace SimpleArmyPolicy;
    const std::vector<Responder> army{{9,300,10},{4,300,2},{3,300,2},{1,300,20}};
    const auto expected=std::vector<uint32_t>{3,4};
    REQUIRE(reinforcements(300,0,army)==expected);
    auto reversed=army; std::reverse(reversed.begin(),reversed.end());
    REQUIRE(reinforcements(300,0,reversed)==expected);
}

TEST_CASE("Loose rally search is bounded and never collapses blocked slots onto centre", "[quantbot][combat]") {
    int probes=0;
    const auto blocked=SimpleArmyPolicy::rallyOffset(42,12,[&](int,int) { ++probes; return false; });
    REQUIRE_FALSE(blocked);
    REQUIRE(probes==8);
    auto terrain=[](int x,int y) { return x>=0 && y>=0; };
    for (uint32_t id=0;id<200;++id) {
        const auto a=SimpleArmyPolicy::rallyOffset(id,12,terrain);
        REQUIRE(a==SimpleArmyPolicy::rallyOffset(id,12,terrain));
        if (a) { REQUIRE(terrain(a->first,a->second)); REQUIRE(a->first<=12); REQUIRE(a->second<=12); }
    }
}

TEST_CASE("Idle heavy factories fill capacity beyond mix quotas without breaking the army cap", "[quantbot][production]") {
    // Similar to the last match: all heavy shares met, but light factories lag.
    std::array<AllocationCandidate,3> heavy={{{300,6300,600,true},{600,7800,700,true},{450,33000,3300,true}}};
    REQUIRE(fundedDeficit(heavy,70000,500000,100000,100000)==-1);
    REQUIRE(capacityFill(heavy,70000,500000,100000)==2);
    REQUIRE(capacityFill(heavy,99900,500000,100000)==-1);
    REQUIRE(capacityFill(heavy,99700,500000,100000)==0); // Only a tank fits.
    REQUIRE(capacityFill(heavy,70000,299,100000)==-1);
    heavy[2].available=false;
    REQUIRE(capacityFill(heavy,70000,500000,100000)==0);
    heavy[0].targetBps=heavy[1].targetBps=0;
    REQUIRE(capacityFill(heavy,70000,500000,100000)==-1);
}
TEST_CASE("Parallel heavy overflow counts each queued unit and preserves the learned balance", "[quantbot][production]") {
    std::array<AllocationCandidate,2> heavy={{{300,6000,600,true},{450,40000,4000,true}}};
    int committed=99000,cash=1000,orders=0;
    while (true) {
        const int selected=capacityFill(heavy,committed,cash,100000);
        if (selected<0) break;
        committed+=heavy[selected].price; cash-=heavy[selected].price;
        heavy[selected].committedValue+=heavy[selected].price; ++orders;
    }
    REQUIRE(orders==2);
    REQUIRE(committed==99900);
    REQUIRE(cash==100);
}
TEST_CASE("Light factories expand for a funded backlog with queued capacity accounted for", "[quantbot][production]") {
    REQUIRE(needsProductionLane(1,1,1,30000,500000,1000,400));
    REQUIRE(needsProductionLane(8,8,6,12000,500000,1000,400));
    REQUIRE_FALSE(needsProductionLane(1,2,1,30000,500000,1000,400));
    REQUIRE_FALSE(needsProductionLane(4,4,1,30000,500000,1000,400));
    REQUIRE_FALSE(needsProductionLane(4,4,4,300,500000,1000,400));
    REQUIRE_FALSE(needsProductionLane(1,1,1,30000,2000,1000,400));
}

TEST_CASE("Spice clearing recruits a complete nearby force without distant reinforcements", "[quantbot][harvester]") {
    using namespace SimpleArmyPolicy;
    const std::vector<Responder> army{{1,300,3},{2,300,8},{3,600,25}};
    REQUIRE(clearingForce(450,0,army,18)==std::vector<uint32_t>{1,2});
    REQUIRE(clearingForce(1000,0,army,18).empty()); // Nearby force insufficient; keep harvesters safe.
    REQUIRE(clearingForce(1000,1000,army,18)==std::vector<uint32_t>{1});
    REQUIRE(clearingForce(450,600,army,18).empty()); // Already covered; no repeated recruitment.
}

TEST_CASE("Performance weighting favours proven returns without type-specific bonuses", "[quantbot][mix]") {
    using namespace UnitMixPolicy;
    const auto mixed=normalize(sharpenScores(Weights{4,1,0,0,0,0,0,0}));
    REQUIRE(mixed[0]==8889);
    REQUIRE(mixed[1]==1111);
    REQUIRE(normalize(sharpenScores(Weights{4000,1000,0,0,0,0,0,0}))==mixed);
    REQUIRE(sharpenScores(Weights{})==Weights{});
    REQUIRE(integerSqrt(UINT64_MAX)==UINT32_MAX);
    REQUIRE(integerSqrt(9999)==99);
    auto result=allocate(Weights{4,1},Weights{1,1},true,false);
    REQUIRE(result[0]==8000); // Existing anti-monoculture cap remains.
    REQUIRE(result[1]==2000);
}

#include <players/SimpleArmyPolicy.h>
#include <players/LocalPointIndex.h>
TEST_CASE("Attack intervals vary reproducibly without a permanent house advantage", "[quantbot][army]") {
    for(uint32_t house=0;house<8;++house) {
        int64_t sum=0;
        for(uint32_t n=0;n<10000;++n) {
            const int delay=SimpleArmyPolicy::attackDelay(10000,n,n*3750,house);
            REQUIRE(delay>=7500);
            REQUIRE(delay<=12500);
            REQUIRE(delay==SimpleArmyPolicy::attackDelay(10000,n,n*3750,house));
            sum+=delay;
        }
        REQUIRE(sum/10000>9900);
        REQUIRE(sum/10000<10100);
    }
    REQUIRE(SimpleArmyPolicy::attackDelay(0,0,0,0)>=1);
}
TEST_CASE("Local service property lookup matches full scans at map and bucket edges", "[quantbot][placement]") {
    LocalPointIndex index(43,37);
    std::vector<std::pair<int,int>> points;
    for(int y=0;y<37;y+=3) for(int x=0;x<43;x+=2) {
        index.add(x,y,points.size()); points.emplace_back(x,y);
    }
    for(int radius:{0,1,8,23,99}) for(int y=0;y<37;y+=4) for(int x=0;x<43;x+=5) {
        std::set<size_t> expected,actual;
        for(size_t i=0;i<points.size();++i)
            if(std::max(std::abs(x-points[i].first),std::abs(y-points[i].second))<=radius) expected.insert(i);
        index.visit(x,y,radius,[&](size_t i){ REQUIRE(actual.insert(i).second); });
        REQUIRE(actual==expected);
    }
}

TEST_CASE("Housing gaps do not suppress stronger jobs demand", "[quantbot][city]") {
    CHECK(rankZones(60,4,9,500,1500,1500,false)[0] == Structure_ZoneCommercial);
    // Logged house 2: previously forced housing despite greater industry demand.
    CHECK(rankZones(18,10,5,1685,224,1500,false)[0] == Structure_ZoneIndustrial);
    CHECK(rankZones(40,20,2,500,1500,1500,false)[0] == Structure_ZoneIndustrial);
    CHECK(rankZones(60,4,9,2000,0,0,false)[0] == Structure_ZoneResidential);
}

#include <players/CityPlanningPolicy.h>

TEST_CASE("City planning batches visit the whole map without gaps or oversized work", "[quantbot][city][performance]") {
    using CityPlanningPolicy::ScanWindow;
    for (const auto size : {std::pair<int,int>{1,1}, {64,64}, {193,191}}) {
        const int cells = size.first*size.second;
        const int batches = (cells+ScanWindow::tilesPerPass-1)/ScanWindow::tilesPerPass;
        for (unsigned house : {0u,2u,7u}) {
            std::vector<int> visits(cells);
            for (int pass=0;pass<batches;++pass) {
                const ScanWindow scan(size.first,size.second,pass*100+48,house);
                REQUIRE(scan.end-scan.begin <= ScanWindow::tilesPerPass);
                REQUIRE(scan.begin >= 0);
                REQUIRE(scan.end <= cells);
                for (int i=scan.begin;i<scan.end;++i) ++visits[i];
                // A reloaded planner derives the identical batch from the saved cycle.
                const ScanWindow reloaded(size.first,size.second,pass*100+48,house);
                REQUIRE(reloaded.begin == scan.begin);
            }
            REQUIRE(std::all_of(visits.begin(),visits.end(),[](int n){return n==1;}));
        }
    }
    REQUIRE(ScanWindow(0,0,100,2).end == 0);
}

TEST_CASE("City planning shares failed searches and cannot exceed its pass allowance", "[quantbot][city][performance]") {
    CityPlanningPolicy::PassSearch<int,int> search;
    REQUIRE(search.start(1));
    search.result() = -1; // No useful site is a cached result, not a cache miss.
    for (int yard=0;yard<20;++yard) {
        REQUIRE(search.get(1));
        REQUIRE(*search.get(1) == -1);
        REQUIRE_FALSE(search.start(1));
    }
    REQUIRE_FALSE(search.get(2)); // Different excluded reservation cannot reuse it.
    REQUIRE_FALSE(search.start(2));
    search.invalidate(); // Another yard placed/reserved/demolished something.
    REQUIRE_FALSE(search.get(1));
    REQUIRE_FALSE(search.start(1)); // Invalidation does not refill the work budget.
    search.reset();
    REQUIRE(search.start(2));
    search.result() = 42;
    REQUIRE(*search.get(2) == 42);
    search.invalidate();
    REQUIRE_FALSE(search.get(2)); // Never return a stale now-blocked positive site.
}

TEST_CASE("Blocked completed yards share search turns without delaying ready placement behind new orders", "[quantbot][city][performance]") {
    std::set<uint32_t> firstReady, firstIdle;
    for (unsigned pass=0;pass<8;++pass) {
        std::vector<std::pair<int,uint32_t>> yards{{3,10},{3,11},{3,12},{3,13},
            {2,20},{2,21},{2,22},{2,23},{1,30},{0,40}};
        CityPlanningPolicy::rotateYards(yards,pass*100+48);
        firstReady.insert(yards[0].second);
        firstIdle.insert(yards[4].second);
        REQUIRE(yards[0].first == 3);
        REQUIRE(yards[4].first == 2);
        REQUIRE(yards[8].second == 30); // Factory ordering stays unchanged.
        REQUIRE(yards[9].second == 40);
    }
    REQUIRE(firstReady.size() == 4);
    REQUIRE(firstIdle.size() == 4);
}

TEST_CASE("Each blocked yard sweeps all map batches even when yard count equals batch count", "[quantbot][city][performance]") {
    using CityPlanningPolicy::ScanWindow;
    constexpr int cells = 192*192;
    constexpr int batches = cells/ScanWindow::tilesPerPass;
    for (unsigned owners : {3u,8u,9u,12u}) {
        std::vector<std::set<int>> visited(owners);
        for (unsigned pass=0;pass<owners*batches;++pass) {
            std::vector<std::pair<int,uint32_t>> order;
            for (unsigned yard=0;yard<owners;++yard) order.emplace_back(3,yard);
            CityPlanningPolicy::rotateYards(order,pass*100+48);
            const ScanWindow scan(192,192,pass*100+48,2,owners);
            visited[order.front().second].insert(scan.begin);
        }
        for (const auto& starts:visited) REQUIRE(starts.size() == batches);
    }
}

TEST_CASE("Zone demand balancing replaces the industry-starving 500 threshold", "[quantbot][city]") {
    CHECK(rankZones(40,20,2,499,500,1500,false)[0] == Structure_ZoneIndustrial);
    CHECK(rankZones(40,20,2,499,499,1,false)[0] == Structure_ZoneCommercial);
    CHECK(rankZones(40,20,2,499,499,0,false)[0] == Structure_ZoneCommercial);
    CHECK(rankZones(40,20,2,499,0,0,false)[0] == Structure_ZoneResidential);
    CHECK(rankZones(40,20,2,0,0,0,false)[0] == NONE_ID);
    // Screenshot: negative R and both job demands high. Existing I shortage wins.
    CHECK(rankZones(20,10,1,-1110,1360,1500,false)[0] == Structure_ZoneIndustrial);
    // Committed counts include orders in other construction yards.
    CHECK(rankZones(20,4,3,-1110,1500,1360,false)[0] == Structure_ZoneIndustrial);
    CHECK(rankZones(20,4,4,-1110,1500,1360,false)[0] == Structure_ZoneCommercial);
}

TEST_CASE("Persistent slightly unequal demands fund both jobs sectors", "[quantbot][city]") {
    int c=10, i=1, builtC=0, builtI=0;
    for (int n=0;n<40;++n) {
        const auto selected=rankZones(20,c,i,-1110,1500,1360,false)[0];
        if (selected==Structure_ZoneCommercial) { ++c; ++builtC; }
        else if (selected==Structure_ZoneIndustrial) { ++i; ++builtI; }
        else FAIL("Negative-demand housing was selected");
    }
    CHECK(builtC>0);
    CHECK(builtI>0);
    CHECK(std::abs(c-i)<=1);
    // Near-zero demand does not get a plot merely to fill the ratio.
    CHECK(rankZones(30,30,0,-100,1500,1,false)[0] == Structure_ZoneCommercial);
}

TEST_CASE("Air gets funds before ground factories without changing city yard precedence", "[quantbot][production][air]") {
    for (bool city : {false,true}) {
        REQUIRE(productionPlanningPriority(city,Structure_HighTechFactory,false)
            > productionPlanningPriority(city,Structure_LightFactory,false));
        REQUIRE(productionPlanningPriority(city,Structure_LightFactory,false)
            > productionPlanningPriority(city,Structure_HeavyFactory,false));
    }
    REQUIRE(productionPlanningPriority(true,Structure_ConstructionYard,true)==3);
    REQUIRE(productionPlanningPriority(true,Structure_ConstructionYard,false)==2);
    REQUIRE(productionPlanningPriority(true,Structure_HighTechFactory,false)==1);
}

TEST_CASE("Unmet combat air wins over expanding carryall targets", "[quantbot][production][air]") {
    AirProductionState s;
    s.ornithopterAvailable=s.carryallAvailable=true;
    s.ornithopterPrice=600; s.carryallPrice=800;
    s.spendable=7976; s.carryalls=13; s.carryallTarget=20;
    s.armyValue=20000; s.armyLimit=80000;
    s.vehiclePlanValue=27000; s.airTargetBps=1440;
    REQUIRE(chooseAirProduction(s).order==AirOrder::Ornithopter);
    s.carryalls=0;
    REQUIRE(chooseAirProduction(s).order==AirOrder::Carryall); // First transport still bootstraps.
    s.carryalls=13; s.airCommittedValue=4200;
    REQUIRE(chooseAirProduction(s).order==AirOrder::Carryall); // Air share already covered.
    s.carryalls=20;
    REQUIRE(chooseAirProduction(s).order==AirOrder::None);
}

TEST_CASE("Aircraft use actual price after reserves and respect queues and caps", "[quantbot][production][air]") {
    AirProductionState s;
    s.ornithopterAvailable=true; s.ornithopterPrice=600;
    s.spendable=spendableCredits(2600,2000); // Old >1200 gate rejected this funded aircraft.
    s.armyValue=7400; s.armyLimit=8000;
    s.vehiclePlanValue=8000; s.airTargetBps=1440;
    REQUIRE(chooseAirProduction(s).order==AirOrder::Ornithopter);
    SECTION("insufficient funds") { s.spendable=599; REQUIRE(chooseAirProduction(s).order==AirOrder::None); }
    SECTION("military cap including queues") { s.armyValue=7401; REQUIRE(chooseAirProduction(s).order==AirOrder::None); }
    SECTION("air cap") { s.airLimit=true; REQUIRE(chooseAirProduction(s).order==AirOrder::None); }
    SECTION("busy") { s.busy=true; REQUIRE(chooseAirProduction(s).order==AirOrder::None); }
    SECTION("upgrading") { s.upgrading=true; REQUIRE(chooseAirProduction(s).order==AirOrder::None); }
    SECTION("unavailable") { s.ornithopterAvailable=false; REQUIRE(chooseAirProduction(s).order==AirOrder::None); }
    SECTION("queued air fills target") { s.airCommittedValue=1200; REQUIRE(chooseAirProduction(s).order==AirOrder::None); }
    SECTION("no air target") { s.airTargetBps=0; REQUIRE(chooseAirProduction(s).order==AirOrder::None); }
}

TEST_CASE("Air prerequisite upgrades proceed without displacing available combat aircraft", "[quantbot][production][air]") {
    AirProductionState s;
    s.canUpgrade=true; s.spendable=800; s.ornithopterPrice=600;
    s.armyValue=0; s.armyLimit=80000; s.vehiclePlanValue=8000; s.airTargetBps=1440;
    REQUIRE(chooseAirProduction(s).order==AirOrder::Upgrade);
    s.ornithopterAvailable=true;
    REQUIRE(chooseAirProduction(s).order==AirOrder::Ornithopter);
}

TEST_CASE("Spice fleet targets retain runway without retiring workers too early", "[quantbot][economy]") {
    // Actual 1.0.626 match: five houses, a 120 lobby cap, 75 Atreides workers.
    // The previous target was already 71 with 9.4 minutes of measured supply left.
    REQUIRE(DuneCity::vanillaHarvesterTarget(711263, 5, 120) == 94);
    REQUIRE(DuneCity::vanillaHarvesterTarget(638690, 5, 120) == 85);
    REQUIRE(DuneCity::vanillaHarvesterTarget(215370, 5, 120) == 28);
    REQUIRE(DuneCity::vanillaHarvesterTarget(1294346, 5, 120) == 120);
    REQUIRE(DuneCity::vanillaHarvesterTarget(711263, 5, 40) == 40);
    REQUIRE(DuneCity::vanillaHarvesterTarget(0, 5, 120) == 0);
    REQUIRE(DuneCity::vanillaHarvesterTarget(-1, 0, 120) == 0);
    REQUIRE(DuneCity::vanillaHarvesterTarget(711263, 5, 0) == 0);
    // City tax income still warrants the more cautious worker investment.
    REQUIRE(desiredSpiceHarvesters(711263, 5, 120) == 63);
    REQUIRE(desiredSpiceHarvesters(215370, 5, 120) == 19);
    REQUIRE(desiredSpiceHarvesters(711263, 5, 40) == 40);
    REQUIRE(desiredSpiceHarvesters(-1, 0, 120) == 0);
    REQUIRE(desiredSpiceHarvesters(711263, 5, 0) == 0);
}

TEST_CASE("QuantBot establishes repair support before expanding the opening vehicle fleet", "[quantbot][repair]") {
    CHECK(baselineRepairYards(0,0)==0);
    CHECK(baselineRepairYards(0,10)==1); // Working spice fleet needs a first repair bay.
    CHECK(baselineRepairYards(25,10)==1);
    CHECK(baselineRepairYards(26,10)==2);
    CHECK(baselineRepairYards(100,10)==4);
    CHECK(baselineRepairYards(175,10)==7); // Imported armies are not capped by heavy-factory count.
}

TEST_CASE("Opening transport precedes repeated heavy factories and saves for the first carryall", "[quantbot][production][air]") {
    CHECK(firstTransportNeeded(true,1,3,0));
    CHECK_FALSE(firstTransportNeeded(false,1,3,0)); // Low tech, disabled transport or air cap.
    CHECK_FALSE(firstTransportNeeded(true,0,3,0));
    CHECK_FALSE(firstTransportNeeded(true,1,0,0));
    CHECK_FALSE(firstTransportNeeded(true,1,3,1)); // Existing or queued carryall releases expansion.
    CHECK(carryallTarget(1,0,0)==1);
    CHECK(carryallTarget(0,0,0)==0);
    CHECK(carryallTarget(30,60,0)==6); // No repair yards: only harvest transport.
    CHECK(carryallTarget(30,60,1)==8); // Two repair flights per bay bound the repair allowance.
    CHECK(carryallTarget(30,60,3)==9);
    for (bool city : {false,true}) {
        CHECK(productionPlanningPriority(city,Structure_HighTechFactory,false,true)
            > productionPlanningPriority(city,Structure_ConstructionYard,true,true));
        CHECK(productionPlanningPriority(city,Structure_ConstructionYard,false,true)
            > productionPlanningPriority(city,Structure_HeavyFactory,false,true));
    }
    AirProductionState s;
    s.carryallAvailable=true; s.carryallPrice=800; s.carryallTarget=1;
    s.ornithopterAvailable=true; s.ornithopterPrice=600; s.canUpgrade=true;
    s.armyLimit=10000; s.vehiclePlanValue=10000; s.airTargetBps=1000;
    s.spendable=799;
    CHECK(chooseAirProduction(s).order==AirOrder::None);
    CHECK(std::string(chooseAirProduction(s).reason)=="save_first_carryall");
    s.spendable=800;
    CHECK(chooseAirProduction(s).order==AirOrder::Carryall);
    s.carryalls=1;
    CHECK(chooseAirProduction(s).order==AirOrder::Ornithopter);
}

TEST_CASE("Busy military factories must not create one refinery per harvester", "[quantbot][city][economy]") {
    using namespace CityEconomyInvestmentPolicy;
    // 638 Ordos at 20 minutes: 32 refineries/workers, one R and 47 tax/min.
    CHECK_FALSE(processingCapacityNeeded(32,32,246,1757));
    CHECK_FALSE(considerRefinery(false,true,true)); // Busy still means capable of supply.
    CHECK_FALSE(preferRefinery(Investment{521,246,3,11861,1000,689},
        Investment{130,23,0,4350,1000},considerRefinery(false,true,true),false));
    CHECK(considerRefinery(false,true,true,true)); // Recover a workforce below two.
    CHECK(considerRefinery(false,true,false)); // No heavy factory: opening remains possible.
    CHECK(considerRefinery(true,false,true)); // Genuine bay backlog still catches up.
    // Worker target stays independent of refinery count and tax hedge.
    CHECK(factoryHarvesterTarget(87,120)==87);
    CHECK(factoryHarvesterTarget(0,120)==0);
}

TEST_CASE("Finite spice investment only credits income beyond the existing fleet", "[quantbot][economy]") {
    using namespace QuantBotSpendingPolicy;
    // Four minutes, one-minute delivery delay; ample, partly exhausted, exhausted.
    CHECK(marginalSpice(20000,600,900,3750,15000,3750)==900);
    CHECK(marginalSpice(3000,600,900,3750,15000,3750)==600);
    CHECK(marginalSpice(2000,600,900,3750,15000,3750)==0);
    CHECK(marginalSpice(0,0,900,0,15000,3750)==0);
    CHECK(marginalSpice(20000,900,900,3750,15000,3750)==0); // Existing bays full.
    CHECK(marginalSpice(20000,600,900,15000,15000,3750)==0); // Delivery too late.
}

TEST_CASE("Shared capital compares readiness with return and protects only the next purchase", "[quantbot][economy]") {
    using namespace QuantBotSpendingPolicy;
    CHECK(economyScore(900,300)>militaryScore(300,300,2000,10000,false));
    CHECK(militaryScore(300,300,2000,10000,true)>economyScore(900,300));
    CHECK(militaryScore(150,300,2000,10000,false)>militaryScore(300,300,2000,10000,false));
    CHECK(militaryScore(300,300,10000,10000,true)==0);
    CHECK(economyScore(100,130)>0); // Permanent zoning can grow below four-minute payback.
    CHECK(reserveForOther(400,300,false,false,false)==300); // Remaining100 buys a plot.
    CHECK(reserveForOther(200,300,false,false,false)==200); // Save; don't repeatedly spend it.
    CHECK(reserveForOther(400,300,true,false,false)==0);
    CHECK(reserveForOther(400,300,false,true,false)==0);
    CHECK(reserveForOther(400,300,false,false,true)==0); // Emergency recovery may spend it.
}

TEST_CASE("Additional factories require funded work beyond existing capacity", "[quantbot][economy]") {
    using namespace QuantBotSpendingPolicy;
    CHECK(additionalProduction(3000,4000,2000,10000,600)==0); // Money is the bottleneck.
    CHECK(additionalProduction(10000,4000,2000,3000,600)==0); // Enough production already.
    CHECK(additionalProduction(6000,4000,2000,10000,600)==1400);
    CHECK(additionalProduction(10000,4000,2000,10000,600)==2000);
    CHECK(productionScore(0,600,2000,10000)==0);
    CHECK(productionScore(2000,600,2000,10000)>productionScore(2000,600,9000,10000));
}

TEST_CASE("Cash runway covers both unit lines and the economy pipeline", "[quantbot][economy]") {
    using namespace QuantBotSpendingPolicy;
    const auto opening=cashFlow(100000,95000,0,15000,20000,2000);
    CHECK(opening.fundsParallelProduction);
    CHECK(opening.projectedCash==80000); // Existing 5,000 queue is not charged twice.
    const auto crowded=cashFlow(20000,18000,4000,22000,24000,2000);
    CHECK_FALSE(crowded.fundsParallelProduction);
    CHECK(crowded.projectedCash==0);
    CHECK(crowded.netBurnPerMinute==5000);
    CHECK(crowded.runwaySeconds==216);
    const auto growing=cashFlow(20000,18000,28000,22000,24000,2000);
    CHECK(growing.fundsParallelProduction);
    CHECK(growing.runwaySeconds==-1);
    CHECK(growing.netBurnPerMinute==-1000);
    CHECK_FALSE(cashFlow(300,100,28000,22000,24000,2000).fundsParallelProduction);
}

TEST_CASE("Refineries catch up to profitable fleet queues even while tax hedge is short", "[quantbot][city][economy]") {
    using namespace CityEconomyInvestmentPolicy;
    Investment bay{526,640,4,2000,1000,1500};
    Investment zone{130,100,0,4350,1000};
    CHECK(preferRefinery(bay,zone,true,true,true));
    // One bay processes 1757/min: four 320/min workers fit, six do not.
    CHECK_FALSE(processingCapacityNeeded(1,4,320,1757));
    CHECK(processingCapacityNeeded(1,6,320,1757));
    CHECK_FALSE(processingCapacityNeeded(2,6,320,1757)); // Queued second bay prevents duplicates.
    bay.confidence=100;
    CHECK_FALSE(preferRefinery(bay,zone,true,true,true)); // Almost exhausted field cannot repay it.
}

TEST_CASE("Growing cities save for nuclear before another run of windtraps", "[quantbot][power]") {
    CHECK_FALSE(planNuclearInvestment(100,200,100,100)); // Opening still uses cheap wind.
    CHECK(planNuclearInvestment(300,500,125,100)); // Forecast reserve close: begin saving while powered.
    CHECK(planNuclearInvestment(600,700,250,100));
    CHECK_FALSE(planNuclearInvestment(600,2500,250,100)); // New reactor covers growth; do not duplicate.
    CHECK_FALSE(planNuclearInvestment(600,700,250,0));
    CHECK(spendableCredits(1700,2000)==0); // Existing factories allow the saving to accumulate.
    CHECK(spendableCredits(2600,2000)==600); // Excess funds can still buy military units.
}

TEST_CASE("Police budget cuts respond to loss and financial pressure then recover gradually", "[quantbot][city][budget]") {
    // Ordos at ~50min in638: gross695, police575, plus246 power/min.
    CHECK(recoveryPoliceFunding(100,695,246,575,500,true)==75);
    CHECK(recoveryPoliceFunding(75,695,246,575,500,true)==50);
    CHECK(recoveryPoliceFunding(50,695,246,575,500,true)==50); // Margin has recovered.
    CHECK(recoveryPoliceFunding(100,695,246,575,500,false)==100); // No major losses.
    CHECK(recoveryPoliceFunding(100,695,246,575,3000,true)==100); // Healthy cash buffer.
    CHECK(recoveryPoliceFunding(50,0,250,575,0,true)==25);
    CHECK(recoveryPoliceFunding(25,0,250,575,0,true)==25); // Keep some crime protection.
    CHECK(recoveryPoliceFunding(50,1600,250,575,500,true)==75);
    CHECK(recoveryPoliceFunding(75,1600,250,575,500,false)==100);
    CHECK(recoveryPoliceFunding(25,695,246,575,5000,false)==50);
}

TEST_CASE("Air strikes reject protected targets and covered approaches", "[quantbot][air]") {
    AirStrikePolicy::Coverage coverage(40,40);
    coverage.add(Coord(20,20),8);
    CHECK_FALSE(coverage.clearFootprint(Coord(25,20),Coord(2,2)));
    CHECK_FALSE(coverage.clearFootprint(Coord(28,20),Coord(2,2))); // range boundary
    CHECK(coverage.clearFootprint(Coord(30,20),Coord(2,2))); // exposed district
    CHECK(coverage.clearApproach(Coord(35,20),Coord(30,20)));
    CHECK_FALSE(coverage.clearApproach(Coord(5,20),Coord(30,20))); // target safe, route unsafe
    CHECK(coverage.clearApproach(Coord(5,5),Coord(30,5)));
    CHECK_FALSE(coverage.clearFootprint(Coord(-1,5),Coord(2,2)));
    CHECK_FALSE(coverage.clearApproach(Coord(20,20),Coord(30,20))); // aircraft already under AA
    // A launcher moving up invalidates a previously safe attack next pass.
    coverage.add(Coord(33,20),9);
    CHECK_FALSE(coverage.clearFootprint(Coord(30,20),Coord(2,2)));
    CHECK_FALSE(coverage.clearApproach(Coord(35,20),Coord(30,20)));
}

TEST_CASE("Air coverage uses combat diagonal distance and clears with removed defenders", "[quantbot][air]") {
    AirStrikePolicy::Coverage guarded(40,40);
    guarded.add(Coord(10,10),8);
    CHECK_FALSE(guarded.safe(Coord(18,10)));
    CHECK(guarded.safe(Coord(18,18))); // outside octile range, inside a square approximation
    AirStrikePolicy::Coverage rebuilt(40,40);
    CHECK(rebuilt.clearApproach(Coord(0,10),Coord(30,10)));
    for(int item : {Structure_RocketTurret,Unit_Launcher,Unit_EliteLauncher,Unit_Deviator})
        CHECK(AirStrikePolicy::antiAir(item));
    for(int item : {Structure_GunTurret,Structure_Refinery,Unit_Harvester,Unit_SonicTank})
        CHECK_FALSE(AirStrikePolicy::antiAir(item));
}

TEST_CASE("Opening fleet follows the field size and keeps the Brutal premium", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    // A rich field (target 120) doubles both openings: twelve Brutal, eight otherwise.
    CHECK(openingWorkerFloor(120,true) == 12);
    CHECK(openingWorkerFloor(120,false) == 8);
    for (int workers = 4; workers < 8; ++workers) {
        CHECK(openingWorkersNeeded(workers,120,true));
        CHECK(preferFactoryHarvester(workers,120,300,80000,300,true,true,true));
        CHECK(openingWorkersNeeded(workers,120));
        CHECK(preferFactoryHarvester(workers,120,300,80000,300,true,true));
    }
    CHECK(openingWorkersNeeded(8,120,true));
    CHECK_FALSE(openingWorkersNeeded(8,120));
    CHECK_FALSE(openingWorkersNeeded(12,120,true));
    CHECK(preferFactoryHarvester(12,120,3600,80000,300,true,true,true));
    CHECK_FALSE(preferFactoryHarvester(13,120,2400,80000,300,true,true,true));
    CHECK(preferFactoryHarvester(40,120,12000,80000,300,true,true,true));
    // A modest field keeps the established four/eight openings, and an
    // exhausted one never demands workers it cannot sustain.
    CHECK(openingWorkerFloor(8,false) == 4);
    CHECK(openingWorkerFloor(16,true) == 8);
    for (int target : {0,1,2,5}) {
        CHECK(openingWorkerFloor(target,true) == target);
        CHECK_FALSE(openingWorkersNeeded(target,target,true));
        CHECK_FALSE(preferFactoryHarvester(target,target,80000,80000,300,true,true,true));
    }
    CHECK_FALSE(preferFactoryHarvester(4,120,1200,80000,300,true,false,true)); // Vanilla unchanged.
}

TEST_CASE("Brutal can choose a profitable third refinery with a worker-capable factory", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    // Observed 645 forecast: refinery rejected solely because a factory exists.
    Investment refinery{461,375,3,8195,1000,689};
    Investment zone{109,19,0,4350,1000,53};
    CHECK_FALSE(considerRefinery(false,true,true));
    CHECK(openingRefineryInvestment(true,4,120,2));
    CHECK(preferRefinery(refinery,zone,considerRefinery(false,true,true,
        openingRefineryInvestment(true,4,120,2)),false));
    CHECK_FALSE(preferRefinery(refinery,zone,true,true)); // Keep first residential hedge.
    CHECK(openingRefineryInvestment(false,4,120,2));
    CHECK(openingRefineryInvestment(true,8,120,2));
    CHECK_FALSE(openingRefineryInvestment(true,12,120,2));
    CHECK(openingRefineryInvestment(false,3,120,3)); // Fourth rich-field bay on Medium too.
    CHECK(openingRefineryInvestment(true,4,120,4)); // Profitable fifth bay can supply the opening fleet.
    CHECK(openingRefineryInvestment(false,6,120,6));
    CHECK_FALSE(openingRefineryInvestment(false,8,120,8)); // Worker floor ends opening priority.
    CHECK_FALSE(openingRefineryInvestment(true,2,2,2));
    refinery.projectedProceeds=300;
    CHECK_FALSE(preferRefinery(refinery,zone,true,false)); // Bad/risky trips still lose.
    CHECK(considerRefinery(true,false,true)); // Mature unloading catchup remains available.
}

TEST_CASE("Announced civic requirements select a single feasible investment", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    using namespace DuneCity;
    CHECK(demandedCivic(NeedStadium,0,true,0,true)==Structure_Stadium);
    CHECK(demandedCivic(NeedStadium|NeedAirport,0,true,0,true)==Structure_Stadium);
    CHECK(demandedCivic(NeedStadium|NeedAirport,1,true,0,true)==Structure_Airport);
    CHECK(demandedCivic(NeedStadium|NeedAirport,0,false,0,true)==Structure_Airport);
    CHECK(demandedCivic(NeedStadium,0,false,0,true)==NONE_ID);
    CHECK(demandedCivic(NeedStadium,1,true,0,true)==NONE_ID);
    CHECK(demandedCivic(0,0,true,0,true)==NONE_ID); // No premature airports/stadiums.
    // A 3000-credit stadium can accumulate cash instead of losing it to optional units.
    CHECK(QuantBotBuildPolicy::spendableCredits(2900,3000)==0);
    CHECK(QuantBotBuildPolicy::spendableCredits(3400,3000)==400);
}

TEST_CASE("Persistent unloading queues trigger one extra bay without a fleet forecast", "[quantbot][city]") {
    using namespace CityEconomyInvestmentPolicy;
    CHECK(unloadingQueueNeedsBay(3,0,0,true));
    CHECK(unloadingQueueNeedsBay(4,1,0,true));
    CHECK_FALSE(unloadingQueueNeedsBay(3,0,0,false)); // Transient arrivals.
    CHECK_FALSE(unloadingQueueNeedsBay(1,0,0,true));
    CHECK_FALSE(unloadingQueueNeedsBay(3,3,0,true)); // Free bays can absorb arrivals.
    CHECK_FALSE(unloadingQueueNeedsBay(8,0,1,true)); // Wait for committed capacity.
}

#include <players/RockExpansionPolicy.h>
TEST_CASE("Expansion chooses safe reachable new rock rather than adjacent yards", "[quantbot][expansion]") {
    using namespace RockExpansionPolicy;
    constexpr int w=80,h=50;
    std::vector<Tile> tiles(w*h);
    for(auto& tile:tiles){tile.walkable=true;tile.free=true;}
    auto rock=[&](int x0,int y0){for(int y=y0;y<y0+10;++y)for(int x=x0;x<x0+10;++x)tiles[y*w+x].rock=true;};
    rock(2,20); rock(25,20); rock(60,20);
    tiles[23*w+5].owned=true; // Current base; plenty of rock but not a new formation.
    const auto safe=choose(w,h,tiles,{23*w+12},{23*w+0},{});
    REQUIRE(safe.valid());CHECK(safe.x>=25);CHECK(safe.x<35);CHECK(safe.room>=48);
    const auto other=choose(w,h,tiles,{23*w+12},{23*w+0},{safe.y*w+safe.x});
    REQUIRE(other.valid());CHECK(other.x>=60);
    // No safe ground route across a mountain barrier: do not order an unreachable MCV.
    for(int y=0;y<h;++y)tiles[y*w+45].walkable=false;
    const auto reachable=choose(w,h,tiles,{23*w+12},{23*w+0},{});
    REQUIRE(reachable.valid());CHECK(reachable.x<45);
    // The remaining rock is in enemy fire; preserve the MCV instead of deploying at home.
    for(int y=20;y<30;++y)for(int x=25;x<35;++x)tiles[y*w+x].unsafe=true;
    CHECK_FALSE(choose(w,h,tiles,{23*w+12},{23*w+0},{}).valid());
}

TEST_CASE("Air defence preempts building raids and ordinary ground units are raidable", "[quantbot][air]") {
    // Something standing over one of our assets still preempts an exposed
    // building. An ordinary enemy unit out in the open is a raid candidate now,
    // at the same rank as that building: what decides whether a wing may fly at
    // one is the launcher ratio below, not this rank. The old reading - a
    // roaming unit is worth nothing - is what left aircraft holding at base
    // while 551 of 3636 strikes in the reported match found a building to hit.
    CHECK(AirStrikePolicy::targetRank(false,true,true)>AirStrikePolicy::targetRank(true,false,true));
    CHECK(AirStrikePolicy::targetRank(false,false,true)==AirStrikePolicy::RaidRank);
    CHECK(AirStrikePolicy::targetRank(true,false,true)==AirStrikePolicy::RaidRank);
    CHECK(AirStrikePolicy::targetRank(false,true,false)==AirStrikePolicy::DefenseRank);
    CHECK(AirStrikePolicy::targetRank(false,false,false)==0);
    CHECK(AirStrikePolicy::safetyRange(7)==12);
    // The engagement radius is a launcher's own reach, so the wing is measured
    // over exactly the ground that can shoot it.
    CHECK(AirStrikePolicy::safetyRange(9)==AirStrikePolicy::kLocalEngagementRadius);
}

TEST_CASE("A wing pays for mobile launcher cover once per covering launcher", "[quantbot][air]") {
    using namespace AirStrikePolicy;
    // Only the launchers are outnumberable. A rocket turret is static with
    // twice the health, and a Deviator converts what it hits, so neither has a
    // price in aircraft.
    CHECK(mobileLauncher(Unit_Launcher));
    CHECK(mobileLauncher(Unit_EliteLauncher));
    CHECK_FALSE(mobileLauncher(Unit_Deviator));
    CHECK_FALSE(mobileLauncher(Structure_RocketTurret));
    for(int item : {Unit_Launcher,Unit_EliteLauncher}) CHECK(antiAir(item));

    CHECK(localWingPermits(0,0));          // uncovered ground is free
    CHECK_FALSE(localWingPermits(3,1));
    CHECK(localWingPermits(4,1));
    CHECK_FALSE(localWingPermits(7,2));
    CHECK(localWingPermits(8,2));
    CHECK(localWingPermits(4,1)==(kAircraftPerMobileLauncher<=4));

    Coverage field(60,60);
    field.addMobileLauncher(Coord(20,20),safetyRange(9));
    field.addMobileLauncher(Coord(46,20),safetyRange(9));
    CHECK(field.mobileLaunchersAt(Coord(20,20))==1);
    CHECK(field.mobileLaunchersAt(Coord(33,20))==2);   // the two reaches overlap
    CHECK(field.mobileLaunchersAt(Coord(0,50))==0);
    // Four may enter the reach of one launcher, but not the overlap of two.
    CHECK(field.clearFootprint(Coord(20,20),Coord(1,1),4));
    CHECK_FALSE(field.clearFootprint(Coord(33,20),Coord(1,1),4));
    CHECK(field.clearFootprint(Coord(33,20),Coord(1,1),8));
    // Every point of the route is priced, not just the endpoint: a wing of four
    // cannot cross the overlap to reach ground only one launcher covers.
    CHECK_FALSE(field.clearApproach(Coord(45,20),Coord(20,20),4));
    CHECK(field.clearApproach(Coord(45,20),Coord(20,20),8));
    // Withdrawal and escape stay on the conservative combined reading: any
    // known cover at all is unsafe to retreat through, at any strength.
    CHECK_FALSE(field.safe(Coord(20,20)));
    CHECK_FALSE(field.clearWithdrawal(Coord(45,20),Coord(20,20)));

    Coverage hard(60,60);
    hard.add(Coord(20,20),safetyRange(8));
    CHECK_FALSE(hard.clearFootprint(Coord(20,20),Coord(1,1),64));
    CHECK_FALSE(hard.clearApproach(Coord(40,20),Coord(20,20),64));
    CHECK(hard.mobileLaunchersAt(Coord(20,20))==0);

    // Unlimited unit settings can put more than 255 launchers in one area.
    Coverage crowded(3,3);
    for(int i=0;i<300;++i) crowded.addMobileLauncher(Coord(1,1),1);
    CHECK(crowded.mobileLaunchersAt(Coord(1,1))==300);
    CHECK_FALSE(crowded.safe(Coord(1,1),1199));
    CHECK(crowded.safe(Coord(1,1),1200));
    CHECK_FALSE(localWingPermits(std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max()));
}

TEST_CASE("An attack on the base outranks a remote worker rescue", "[quantbot][air]") {
    const int base=AirStrikePolicy::underAttackRank(true);
    const int worker=AirStrikePolicy::underAttackRank(false);
    CHECK(base>worker);
    CHECK(worker>AirStrikePolicy::targetRank(false,true,true));
    CHECK(AirStrikePolicy::emergencyRank(base));
    CHECK(AirStrikePolicy::emergencyRank(worker));
    CHECK_FALSE(AirStrikePolicy::emergencyRank(AirStrikePolicy::DefenseRank));
    CHECK_FALSE(AirStrikePolicy::emergencyRank(AirStrikePolicy::RaidRank));
}

TEST_CASE("A forced interception stands until something more urgent appears", "[quantbot][air][regression]") {
    using namespace AirStrikePolicy;
    // Nothing held: the best candidate always wins.
    CHECK_FALSE(holdsInterception(0,RaidRank));
    CHECK_FALSE(holdsInterception(0,BaseUnderAttackRank));
    // An equal or lower ranked alternative never restarts the attack run,
    // whatever its score: 144 of 149 observed switches abandoned a live target.
    CHECK(holdsInterception(UnderAttackRank,UnderAttackRank));
    CHECK(holdsInterception(UnderAttackRank,DefenseRank));
    CHECK(holdsInterception(RaidRank,RaidRank));
    CHECK(holdsInterception(BaseUnderAttackRank,UnderAttackRank));
    // A strictly more urgent class does replace it.
    CHECK_FALSE(holdsInterception(UnderAttackRank,BaseUnderAttackRank));
    CHECK_FALSE(holdsInterception(RaidRank,DefenseRank));
}

TEST_CASE("Proactive turret overlap is interleaved with city growth", "[quantbot][city][defence][regression]") {
    using RocketTurretPolicy::proactiveCoverageTurn;
    using RocketTurretPolicy::growthOrdersPerProactiveTurret;
    // Aircraft over our own buildings and a core asset nothing covers are
    // present losses: they take the slot immediately.
    CHECK(proactiveCoverageTurn(true,0,0));
    CHECK(proactiveCoverageTurn(false,1,0));
    // Deeper overlap on covered assets waits its turn instead of taking every
    // order, which is how one match ordered twenty-seven emplacements in ten
    // minutes with five heavy factories.
    for(unsigned orders=0;orders<growthOrdersPerProactiveTurret;++orders)
        CHECK_FALSE(proactiveCoverageTurn(false,0,orders));
    CHECK(proactiveCoverageTurn(false,0,growthOrdersPerProactiveTurret));
    CHECK(proactiveCoverageTurn(false,0,growthOrdersPerProactiveTurret+3));
    // The interim ceiling still bounds peaceful coverage on a large base.
    CHECK(RocketTurretPolicy::coverageTurretCap(0)==2);
    CHECK(RocketTurretPolicy::coverageTurretCap(40)==12);
}
TEST_CASE("Air withdrawal exits new coverage without crossing a second defended area", "[quantbot][air]") {
    AirStrikePolicy::Coverage map(40,40);
    map.add(Coord(10,20),5);
    map.add(Coord(25,20),4);
    CHECK(map.clearWithdrawal(Coord(10,20),Coord(17,20)));
    CHECK_FALSE(map.clearWithdrawal(Coord(10,20),Coord(35,20)));
    CHECK_FALSE(map.clearWithdrawal(Coord(5,5),Coord(10,20)));
    const Coord exit=map.escape(Coord(10,20));
    CHECK(exit.isValid());
    CHECK(map.safe(exit));
    CHECK(map.clearWithdrawal(Coord(10,20),exit));
    AirStrikePolicy::Coverage trapped(2,2); trapped.add(Coord(0,0),10);
    CHECK(trapped.escape(Coord(0,0)).isInvalid());
}

TEST_CASE("MCVs choose nearby usable rock without chasing distant space or clearance", "[quantbot][expansion]") {
    using namespace RockExpansionPolicy;
    constexpr int w=100,h=50;
    std::vector<Tile> tiles(w*h);
    for(auto& tile:tiles){tile.walkable=true;tile.free=true;}
    auto rock=[&](int x0,int y0,int size){for(int y=y0;y<y0+size;++y)for(int x=x0;x<x0+size;++x)tiles[y*w+x].rock=true;};
    rock(2,20,8); tiles[22*w+4].owned=true;
    rock(20,20,8); rock(65,15,20);
    // With no enemies, a much larger island must not outrank nearby usable rock.
    auto result=choose(w,h,tiles,{23*w+10},{},{});
    REQUIRE(result.valid());CHECK(result.x>=20);CHECK(result.x<28);
    // Same result when the far island offers needless extra enemy clearance.
    result=choose(w,h,tiles,{23*w+10},{23*w},{});
    REQUIRE(result.valid());CHECK(result.x>=20);CHECK(result.x<28);
    // Nearby rock under direct threat is still rejected.
    result=choose(w,h,tiles,{23*w+10},{23*w+22},{});
    REQUIRE(result.valid());CHECK(result.x>=65);
    // Main-base proximity beats safety/space rewards and the MCV's own position.
    const Site near{20,20,64,12,70,20}, safer{25,20,64,24,10,25}, distant{70,20,196,70,5,65};
    CHECK(betterSite(near,safer));CHECK(betterSite(near,distant));
    CHECK(betterSite(Site{20,20,64,24,70,20},near)); // safety only breaks a distance tie
    result=choose(w,h,tiles,{23*w+90},{},{},22*w+4);
    REQUIRE(result.valid());CHECK(result.x>=20);CHECK(result.x<28); // MCV beside far island

}

TEST_CASE("Campaign opening follows authored trigger with independent bounded delays", "[quantbot][campaign]") {
    using namespace CampaignDifficultyPolicy;
    std::set<uint32_t> offsets;
    for(uint32_t seed=0;seed<32;++seed) for(uint32_t house=0;house<6;++house) {
        for(int tier=0;tier<3;++tier) {
            const auto delay=openingDelayMs(tier,seed,45000,house);
            CHECK(delay<=120000);
            CHECK(delay==openingDelayMs(tier,seed,45000,house));
            offsets.insert(delay);
        }
        CHECK(openingDelayMs(3,seed,45000,house)==0);
    }
    CHECK(offsets.size()>100);
}

TEST_CASE("Medium and Hard campaign waves use fixed credits and independent opening delays", "[quantbot][campaign]") {
    using namespace CampaignDifficultyPolicy;
    for (int tech : {1, 4, 8}) {
        for (int difficulty : {1, 2}) {
            const auto p=profile(difficulty,tech);
            const int budget=difficulty==1 ? 2500 : 3500;
            CHECK(p.enemyCommitPercent==100);
            CHECK(fits(p,{1,100,budget-100,0},100));
            CHECK_FALSE(fits(p,{1,1,budget-100,0},101));
            for (uint32_t house=0;house<6;++house) {
                CHECK(openingDelayMs(difficulty,1599783965,45000,house)<=120000);
                const auto delay=repeatDelayMs(difficulty,1599783965,45000,house);
                CHECK(delay>=60000);CHECK(delay<=180000);
            }
        }
    }
    CHECK(openingDelayMs(3,1599783965,45000,0)==0);
    CHECK(openingDelayMs(2,1599783965,45000,0)!=openingDelayMs(2,1599783965,45000,1));
}

TEST_CASE("Custom attacks commit a strict share of the owned ground army", "[quantbot][army][regression]") {
    using namespace SimpleArmyPolicy;
    std::vector<Responder> infantry;
    for(uint32_t id=1;id<=76;++id) infantry.push_back({id,100,0});
    // The configured share is the only limit: a large army sends far more than
    // the retired eight/fourteen units and the retired 2400/4200 value caps.
    REQUIRE(customAttack(18850,0,25,infantry).size()==47);
    REQUIRE(customAttack(18850,0,40,infantry).size()==75);
    // Survivors of earlier waves keep their place inside that same share.
    REQUIRE(customAttack(18850,4600,25,infantry).size()==1);
    REQUIRE(customAttack(18850,4712,25,infantry).empty());
    REQUIRE(customAttack(2000,0,25,infantry).size()==5);
    REQUIRE(customAttack(2000,300,25,infantry).size()==2);
    REQUIRE(customAttack(2000,500,25,infantry).empty());
    REQUIRE(customAttack(2000,0,0,infantry).empty());
    // Strict budget, no single-unit fallback: too small an army simply waits.
    REQUIRE(customAttack(300,0,25,{{7,300,0}}).empty());
    std::vector<Responder> heavy;
    for(uint32_t id=1;id<=20;++id) heavy.push_back({id,600,0});
    REQUIRE(customAttack(18850,0,25,heavy).size()==7);
    REQUIRE(customAttack(18850,0,40,heavy).size()==12);
    REQUIRE(customAttack(2000,0,50,infantry).size()==10);
    REQUIRE(customAttack(2000,0,60,infantry).size()==12);
    auto reversed=infantry;std::reverse(reversed.begin(),reversed.end());
    REQUIRE(customAttack(18850,0,25,reversed)==customAttack(18850,0,25,infantry));
    // The deliberately lenient campaign helper remains separate.
    REQUIRE(limitedAttack(300,0,25,{{7,300,0}})==std::vector<uint32_t>{7});
}

TEST_CASE("City policing fits early and established recurring budgets", "[ai][city][budget]") {
    using namespace CityServiceInvestmentPolicy;
    CHECK(policingBudgetPercent(1000,600,50,300)==33);
    CHECK(policingBudgetPercent(5000,600,50,300)==50);
    CHECK(policingBudgetPercent(1000,1000,50,300)==50);
    CHECK(policingAllowance(600,50,33)==181);
    CHECK(affordablePoliceFunding(600,50,300,33)==60);
    CHECK(affordablePoliceFunding(600,50,300,50)==91);
    CHECK(affordablePoliceFunding(0,50,300,33)==0);
    CHECK(affordablePoliceFunding(600,50,0,33)==100);
}

TEST_CASE("Police funding cuts apply at once and increases wait for review", "[ai][city][budget]") {
    using namespace CityServiceInvestmentPolicy;
    // Enforcing the 33%/50% recurring limit is never delayed.
    CHECK(smoothedPoliceFunding(100,60,1000,900)==60);
    CHECK(smoothedPoliceFunding(60,0,1000,999)==0);
    // Unchanged funding stays unchanged whatever the clock says.
    CHECK(smoothedPoliceFunding(60,60,1000,999)==60);
    // An increase inside the review interval is held, whatever its size.
    CHECK(smoothedPoliceFunding(60,100,1000,900)==60);
    CHECK(smoothedPoliceFunding(60,63,1000+kPoliceIncreaseReviewCycles,1000)==60);
    // Sustained headroom past the interval, beyond the deadband, is applied.
    CHECK(smoothedPoliceFunding(60,100,1000+kPoliceIncreaseReviewCycles,1000)==100);
    CHECK(smoothedPoliceFunding(60,100,1000+kPoliceIncreaseReviewCycles-1,1000)==60);
    // Demand that flips on every build pass used to move funding every pass:
    // 1,028 changes on Medium and 1,275 on Hard in the reviewed run. Cuts stay
    // immediate, so the level still follows the limit down, but the reversals
    // are now bounded by the review cadence.
    int funding=100, changes=0;
    Uint32 lastChange=5000, cycle=5000;
    for(int pass=0;pass<60;++pass) {
        const int target=pass%2 ? 100 : 40;   // alternating demand, every build pass
        const int applied=smoothedPoliceFunding(funding,target,cycle,lastChange);
        if(applied!=funding) { funding=applied; lastChange=cycle; ++changes; }
        cycle+=100;                            // 1.6 game-seconds per build pass
    }
    CHECK(changes<=8);
    CHECK(changes>0);
    // A rewound or reloaded clock must not unlock an early increase.
    CHECK(smoothedPoliceFunding(40,100,900,5000)==40);
}

TEST_CASE("New expansion yards demand three turrets and are covered first", "[ai][city][defense]") {
    using namespace RocketTurretPolicy;
    // Tier 0 Easy, 1 Medium, 2 Hard, 3 Brutal.
    for(int tier : {0,1,2,3}) {
        CHECK(desiredCoverage(Structure_ConstructionYard,tier,true)==3);
        CHECK(desiredCoverage(Structure_ConstructionYard,tier,true)
              >= desiredCoverage(Structure_ConstructionYard,tier,false));
    }
    // Other buildings retain their difficulty-scaled goals.
    CHECK(desiredCoverage(Structure_Refinery,1,true)==desiredCoverage(Structure_Refinery,1,false));
    CHECK(desiredCoverage(Structure_ZoneResidential,1,true)==desiredCoverage(Structure_ZoneResidential,1,false));
    CHECK(desiredCoverage(Structure_RocketTurret,2,true)==0);

    // The exposed expansion outranks the rest of the core, which outranks
    // ordinary buildings, which never claim first-cover priority at all.
    CHECK(firstCoverPriority(Structure_ConstructionYard,true)
          > firstCoverPriority(Structure_Refinery,false));
    CHECK(firstCoverPriority(Structure_Refinery,false) > 0);
    CHECK(firstCoverPriority(Structure_ZoneResidential,true)==0);
    CHECK(firstCoverPriority(Structure_ConstructionYard,true)
          > 2*firstCoverPriority(Structure_HeavyFactory,false));

    // A site giving an uncovered expansion yard its first turret beats a
    // central site that only adds more cover to an already defended district.
    Score exposedYard; exposedYard.critical=firstCoverPriority(Structure_ConstructionYard,true);
    exposedYard.defense=3;
    Score crowdedCentre; crowdedCentre.defense=40; crowdedCentre.junction=8; crowdedCentre.amenity=30;
    CHECK(exposedYard.betterThan(crowdedCentre));
    CHECK_FALSE(crowdedCentre.betterThan(exposedYard));
    CHECK(exposedYard.useful());
    Score localExpansion;localExpansion.expansion=1;localExpansion.defense=3;
    Score remoteCore;remoteCore.critical=100;remoteCore.defense=100;
    CHECK(localExpansion.betterThan(remoteCore));
    // The expansion keeps a lower-weight priority for its second and third
    // turret, and a turret that covers nothing is still not worth buying.
    Score secondYardTurret; secondYardTurret.critical=1; secondYardTurret.defense=3;
    CHECK(secondYardTurret.betterThan(crowdedCentre));
    CHECK(exposedYard.betterThan(secondYardTurret));
    CHECK_FALSE(Score().useful());
    // The interim ceiling still scales with demand rather than jumping.
    CHECK(coverageTurretCap(12)==5);
    CHECK(coverageTurretCap(15)>coverageTurretCap(12)-1);
}

// --- Concrete foundation policy --------------------------------------------
// QuantBot founds a building because its health does work, not because concrete
// looks tidy. These fix which buildings that covers and what the slabs must
// never be allowed to cost.

TEST_CASE("Foundation priority follows the engine's health-dependent functions", "[quantbot][concrete]") {
    // BuilderBase multiplies build speed by the builder's health fraction.
    REQUIRE(healthScaledProduction(Structure_ConstructionYard));
    REQUIRE(healthScaledProduction(Structure_HeavyFactory));
    REQUIRE(healthScaledProduction(Structure_HighTechFactory));
    REQUIRE(healthScaledProduction(Structure_LightFactory));
    REQUIRE(healthScaledProduction(Structure_Barracks));
    REQUIRE(healthScaledProduction(Structure_WOR));
    // The Starport delivers imports on its own timer; its condition is not a
    // production rate, so it is not founded for that reason.
    REQUIRE_FALSE(healthScaledProduction(Structure_StarPort));
    REQUIRE_FALSE(healthScaledProduction(Structure_Radar));

    // Output that is literally a health fraction.
    REQUIRE(healthScaledOutput(Structure_WindTrap, false));
    REQUIRE(healthScaledOutput(Structure_AdvancedWindTrap, false));
    REQUIRE(healthScaledOutput(Structure_Refinery, false));
    REQUIRE(healthScaledOutput(Structure_Worfinery, false));
    // generatorOutput() only scales the reactor inside the city simulation.
    REQUIRE(healthScaledOutput(Structure_NuclearPlant, true));
    REQUIRE_FALSE(healthScaledOutput(Structure_NuclearPlant, false));
    REQUIRE_FALSE(healthScaledOutput(Structure_Scoutpost, true));

    REQUIRE(defensiveEmplacement(Structure_GunTurret));
    REQUIRE(defensiveEmplacement(Structure_RocketTurret));

    // Tile states and the wall are excluded from degradation entirely, so they
    // are never given a foundation of their own — including in city mode.
    for (Uint32 exempt : {Uint32(Structure_Wall), Uint32(Structure_Slab1), Uint32(Structure_Slab4),
                          Uint32(Structure_Road), Uint32(Structure_PowerLine), Uint32(NONE_ID)}) {
        REQUIRE(foundationExemptItem(exempt));
        REQUIRE_FALSE(foundationRequiredForItem(exempt));
    }
}

TEST_CASE("Every ordinary building is founded, in every mode", "[quantbot][concrete]") {
    // No rich, urgent or otherwise selective exception survives: if it is not a
    // tile state, it waits for its whole footprint.
    for (Uint32 item : {Uint32(Structure_WindTrap), Uint32(Structure_Refinery),
                        Uint32(Structure_HeavyFactory), Uint32(Structure_GunTurret),
                        Uint32(Structure_RocketTurret), Uint32(Structure_Palace),
                        Uint32(Structure_Radar), Uint32(Structure_Silo),
                        Uint32(Structure_StarPort), Uint32(Structure_ZoneResidential),
                        Uint32(Structure_ZoneCommercial), Uint32(Structure_ZoneIndustrial),
                        Uint32(Structure_PoliceStation), Uint32(Structure_Stadium),
                        Uint32(Structure_Airport), Uint32(Structure_NuclearPlant)})
        REQUIRE(foundationRequiredForItem(item));
}

TEST_CASE("The engine's own repair price drives repair and generation economics", "[quantbot][concrete][economy]") {
    // Radar: 500 max health, 400 credits. The engine charges
    // ((2*256)/500)*400/1280 credits per hitpoint and placement on bare rock
    // costs 250 of them.
    REQUIRE(repairCreditsPerHitpointMilli(500, 400) == 312);
    REQUIRE(placementRepairCost(500, 400) == 78);

    // Above 512 maximum health the engine's integer fraction is zero and
    // repairs really are free.
    REQUIRE(repairCreditsPerHitpointMilli(1000, 2000) == 0);
    REQUIRE(placementRepairCost(1000, 2000) == 0);
    REQUIRE(repairsForFree(1000, 2000));
    REQUIRE(repairsForFree(513, 2000));
    REQUIRE_FALSE(repairsForFree(512, 2000));

    // Exact fraction, not the rounded thousandths: one credit at 512 maximum
    // health still charges 1/1280 per hitpoint, which rounds to zero milli.
    REQUIRE(repairCreditsPerHitpointMilli(512, 1) == 0);
    REQUIRE_FALSE(repairsForFree(512, 1));
    // A priceless or health-less entry has nothing to charge.
    REQUIRE(repairsForFree(200, 0));
    REQUIRE(repairsForFree(0, 300));
}

TEST_CASE("Repairs follow condition-driven output, wealth and emergencies", "[quantbot][repair]") {
    const int windHealth = 200, windPrice = 300;   // output is a health fraction
    const int factoryHealth = 200, factoryPrice = 600;
    const int palaceHealth = 1000, palacePrice = 2000; // free in the engine formula
    const int siloHealth = 150, siloPrice = 300;

    // Productive condition is repaired outside the city simulation as well,
    // with no wealth gate at all.
    REQUIRE(repairMaintainsValue(Structure_WindTrap, false, windHealth, windPrice));
    REQUIRE(repairMaintainsValue(Structure_HeavyFactory, false, factoryHealth, factoryPrice));
    REQUIRE(repairMaintainsValue(Structure_ConstructionYard, false, 400, 400));
    REQUIRE(repairMaintainsValue(Structure_Refinery, false, 450, 400));
    // Free repairs cost nothing, so they are never withheld.
    REQUIRE(repairMaintainsValue(Structure_Palace, false, palaceHealth, palacePrice));
    // A passive building that does cost credits waits for a wealthy treasury.
    REQUIRE_FALSE(repairMaintainsValue(Structure_Silo, false, siloHealth, siloPrice));
    REQUIRE_FALSE(repairMaintainsValue(Structure_Wall, false, 50, 50));

    // City mode repairs the entire colony, walls included: a damaged footprint
    // scales down its own land value.
    REQUIRE(repairMaintainsValue(Structure_Silo, true, siloHealth, siloPrice));
    REQUIRE(repairMaintainsValue(Structure_Wall, true, 50, 50));
    REQUIRE(repairMaintainsValue(Structure_ZoneResidential, true, 200, 100));
    REQUIRE(repairMaintainsValue(Structure_NuclearPlant, true, 750, 1500));

    // The engine charges per tick, so it needs something to charge.
    REQUIRE_FALSE(canAffordRepairTick(4));
    REQUIRE(canAffordRepairTick(5));
    // Discretionary repairs use the available planning money.
    REQUIRE_FALSE(repairWhenWealthy(5000));
    REQUIRE(repairWhenWealthy(5001));
}

TEST_CASE("The yard unlocks bulk concrete as soon as the technology allows", "[quantbot][concrete][production]") {
    const int cost = 200;     // Construction Yard price 400, upgrade costs half
    const int slab4Level = 1;

    // Purely a concrete unlock: no concrete, or no reachable Slab4, no upgrade.
    REQUIRE_FALSE(upgradeYardForBulkSlab(false, true, 0, slab4Level, 2, 5000, cost, false));
    REQUIRE_FALSE(upgradeYardForBulkSlab(true, false, 0, slab4Level, 2, 5000, cost, false));
    // A mod that does not gate Slab4 behind an upgrade needs no upgrade either.
    REQUIRE_FALSE(upgradeYardForBulkSlab(true, true, 0, 0, 2, 5000, cost, false));

    // Only the price of the upgrade itself: no income, reserve, power or
    // defence precondition survives.
    REQUIRE_FALSE(upgradeYardForBulkSlab(true, true, 0, slab4Level, 2, cost - 1, cost, false));
    REQUIRE(upgradeYardForBulkSlab(true, true, 0, slab4Level, 2, cost, cost, false));

    // Already unlocked, or a yard that cannot reach the level at all.
    REQUIRE_FALSE(upgradeYardForBulkSlab(true, true, 1, slab4Level, 2, 5000, cost, false));
    REQUIRE_FALSE(upgradeYardForBulkSlab(true, true, 0, slab4Level, 0, 5000, cost, false));
    // One yard upgrades at a time so the others keep building.
    REQUIRE_FALSE(upgradeYardForBulkSlab(true, true, 0, slab4Level, 2, 5000, cost, true));

    // The pending test is independent of cash and of the other yards: it is
    // what makes a yard hold its pass instead of buying something cheaper.
    REQUIRE(bulkSlabUpgradePending(true, true, 0, slab4Level, 2));
    REQUIRE_FALSE(bulkSlabUpgradePending(true, true, 1, slab4Level, 2));
    REQUIRE_FALSE(bulkSlabUpgradePending(true, false, 0, slab4Level, 2));
    REQUIRE_FALSE(bulkSlabUpgradePending(false, true, 0, slab4Level, 2));
    // A tree that locks bulk concrete falls back to single slabs and never
    // holds the yard: nothing is pending because nothing is reachable.
    REQUIRE_FALSE(bulkSlabUpgradePending(true, true, 0, slab4Level, 0));
    REQUIRE_FALSE(bulkSlabUpgradePending(true, true, 0, 0, 2));
}

// ---------------------------------------------------------------------------
// Foundation planning
// ---------------------------------------------------------------------------

namespace {
using namespace QuantBotFoundationPolicy;

struct Ground {
    int width = 16, height = 16;
    std::set<std::pair<int,int>> prepared, roads, blocked;
    bool reachEverywhere = true;
    std::set<std::pair<int,int>> reach;
    TileState operator()(int x, int y) const {
        TileState state;
        if (x < 0 || y < 0 || x >= width || y >= height) return state;
        state.exists = true;
        state.road = roads.count({x,y}) > 0;
        state.prepared = state.road || prepared.count({x,y}) > 0;
        state.paveable = blocked.count({x,y}) == 0;
        state.inBuildRange = reachEverywhere || reach.count({x,y}) > 0;
        return state;
    }
};

int slabTiles(const Plan& plan) {
    int tiles = 0;
    for (const auto& order : plan.orders) tiles += order.item == Structure_Slab4 ? 4 : 1;
    return tiles;
}
bool paves(const Plan& plan, int x, int y) {
    for (const auto& order : plan.orders) {
        const int span = order.item == Structure_Slab4 ? 2 : 1;
        if (x >= order.x && x < order.x + span && y >= order.y && y < order.y + span) return true;
    }
    return false;
}
} // namespace

TEST_CASE("A bulk footprint is prepared by whole 2x2 slabs", "[quantbot][concrete][planner]") {
    Ground ground;
    const auto plan = planFoundation(4, 4, 2, 2, true, true, ground);
    REQUIRE(plan.complete);
    REQUIRE(plan.orders.size() == 1);
    REQUIRE(plan.orders[0].item == Structure_Slab4);
    REQUIRE(plan.orders[0].x == 4);
    REQUIRE(plan.orders[0].y == 4);
}

TEST_CASE("A 3x2 residual strip uses a second bulk slab outside the footprint", "[quantbot][concrete][planner]") {
    Ground ground;
    const auto plan = planFoundation(4, 4, 3, 2, true, true, ground);
    REQUIRE(plan.complete);
    // Two bulk slabs, not one bulk slab and two singles.
    REQUIRE(plan.orders.size() == 2);
    for (const auto& order : plan.orders) REQUIRE(order.item == Structure_Slab4);
    // The whole footprint is covered ...
    for (int x = 4; x < 7; ++x) for (int y = 4; y < 6; ++y) REQUIRE(paves(plan, x, y));
    // ... and the strip is covered by new ground beside the building rather
    // than by a slab that re-paves the one before it.
    REQUIRE(paves(plan, 7, 4));
    REQUIRE(paves(plan, 7, 5));
    REQUIRE(slabTiles(plan) == 8);
}

TEST_CASE("A 2x3 residual strip overhangs the other way round", "[quantbot][concrete][planner]") {
    Ground ground;
    const auto plan = planFoundation(4, 4, 2, 3, true, true, ground);
    REQUIRE(plan.complete);
    REQUIRE(plan.orders.size() == 2);
    for (const auto& order : plan.orders) REQUIRE(order.item == Structure_Slab4);
    for (int x = 4; x < 6; ++x) for (int y = 4; y < 7; ++y) REQUIRE(paves(plan, x, y));
    REQUIRE(paves(plan, 4, 7));
    REQUIRE(paves(plan, 5, 7));
}

TEST_CASE("A 3x3 footprint finishes residual strips with bulk concrete", "[quantbot][concrete][planner]") {
    Ground ground;
    const auto plan = planFoundation(4, 4, 3, 3, true, true, ground);
    REQUIRE(plan.complete);
    for (int x = 4; x < 7; ++x) for (int y = 4; y < 7; ++y) REQUIRE(paves(plan, x, y));
    int bulk = 0, singles = 0;
    for (const auto& order : plan.orders) (order.item == Structure_Slab4 ? bulk : singles)++;
    REQUIRE(bulk == 3);
    REQUIRE(singles == 1);
    // Both residual strips use bulk concrete; only the isolated corner uses
    // a single slab. Four extra tiles remain beside the building.
    REQUIRE(slabTiles(plan) == 13);
}

TEST_CASE("Existing roads and concrete are counted and never paved over", "[quantbot][concrete][planner]") {
    Ground ground;
    // Along the top edge, so the only row a 2x2 could use for the bare row is
    // the road row itself. Roads are prepared ground and must survive.
    for (int x = 4; x < 7; ++x) ground.roads.insert({x, 1});
    const auto plan = planFoundation(4, 0, 3, 2, true, true, ground);
    REQUIRE(plan.complete);
    REQUIRE(plan.orders.size() == 3);
    for (const auto& order : plan.orders) {
        REQUIRE(order.item == Structure_Slab1);
        REQUIRE(order.y == 0);
    }
    for (int x = 4; x < 7; ++x) REQUIRE_FALSE(paves(plan, x, 1));

    // A footprint that roads and concrete already prepare needs no order at
    // all, even for a builder that has no slab in its list.
    Ground ready;
    ready.roads.insert({4,4}); ready.roads.insert({5,4});
    ready.prepared.insert({4,5}); ready.prepared.insert({5,5});
    const auto none = planFoundation(4, 4, 2, 2, false, false, ready);
    REQUIRE(none.complete);
    REQUIRE(none.orders.empty());
}

TEST_CASE("A map edge and a blocked overhang fall back to single slabs", "[quantbot][concrete][planner]") {
    Ground edge;
    // Bottom-right corner: a 2x2 for the last column would leave the map.
    const auto corner = planFoundation(edge.width - 3, edge.height - 2, 3, 2, true, true, edge);
    REQUIRE(corner.complete);
    for (int x = edge.width - 3; x < edge.width; ++x)
        for (int y = edge.height - 2; y < edge.height; ++y) REQUIRE(paves(corner, x, y));
    for (const auto& order : corner.orders) {
        const int span = order.item == Structure_Slab4 ? 2 : 1;
        REQUIRE(order.x + span <= edge.width);
        REQUIRE(order.y + span <= edge.height);
    }

    Ground blocked;
    // Every tile around the footprint already carries a building, so no slab
    // may hang outside it. The footprint is still prepared in full.
    for (int y = 3; y < 7; ++y) { blocked.blocked.insert({7,y}); blocked.blocked.insert({3,y}); }
    for (int x = 3; x < 8; ++x) { blocked.blocked.insert({x,3}); blocked.blocked.insert({x,6}); }
    const auto plan = planFoundation(4, 4, 3, 2, true, true, blocked);
    REQUIRE(plan.complete);
    for (int x = 4; x < 7; ++x) for (int y = 4; y < 6; ++y) REQUIRE(paves(plan, x, y));
    for (const auto& blockedTile : blocked.blocked)
        REQUIRE_FALSE(paves(plan, blockedTile.first, blockedTile.second));

    // With the last overhang gone and a single tile left over, the plan falls
    // back to one 1x1 rather than paying for a 2x2 it cannot place.
    Ground strip = blocked;
    strip.prepared.insert({6,5});
    const auto fallback = planFoundation(4, 4, 3, 2, true, true, strip);
    REQUIRE(fallback.complete);
    int singles = 0;
    for (const auto& order : fallback.orders) singles += order.item == Structure_Slab1;
    REQUIRE(singles == 1);
    REQUIRE(paves(fallback, 6, 4));
}

TEST_CASE("Without the bulk slab the plan still prepares every tile", "[quantbot][concrete][planner]") {
    Ground ground;
    const auto plan = planFoundation(4, 4, 3, 2, false, true, ground);
    REQUIRE(plan.complete);
    REQUIRE(plan.orders.size() == 6);
    for (const auto& order : plan.orders) REQUIRE(order.item == Structure_Slab1);

    // An isolated one-tile emplacement takes one single slab, never a 2x2.
    const auto turret = planFoundation(4, 4, 1, 1, true, true, ground);
    REQUIRE(turret.complete);
    REQUIRE(turret.orders.size() == 1);
    REQUIRE(turret.orders[0].item == Structure_Slab1);
}

TEST_CASE("A footprint that cannot be prepared defers instead of building bare", "[quantbot][concrete][planner]") {
    Ground blocked;
    blocked.blocked.insert({5,4}); // a unit or building on the footprint itself
    const auto plan = planFoundation(4, 4, 3, 2, true, true, blocked);
    REQUIRE_FALSE(plan.complete);
    REQUIRE(plan.orders.empty());

    Ground noSlabs;
    const auto unavailable = planFoundation(4, 4, 2, 2, false, false, noSlabs);
    REQUIRE_FALSE(unavailable.complete);
    REQUIRE(unavailable.orders.empty());

    // A road on the footprint is prepared ground, so it is not a blocker.
    Ground mixed;
    mixed.roads.insert({5,4});
    const auto withRoad = planFoundation(4, 4, 3, 2, true, true, mixed);
    REQUIRE(withRoad.complete);
    REQUIRE_FALSE(paves(withRoad, 5, 4));
}

TEST_CASE("Planned slabs extend the reach the next slab needs", "[quantbot][concrete][planner]") {
    Ground ground;
    ground.reachEverywhere = false;
    // Only the left column of the footprint is inside the existing base.
    for (int y = 0; y < ground.height; ++y) ground.reach.insert({4,y});
    const auto plan = planFoundation(4, 4, 3, 2, true, true, ground);
    REQUIRE(plan.complete);
    for (int x = 4; x < 7; ++x) for (int y = 4; y < 6; ++y) REQUIRE(paves(plan, x, y));

    // Nothing in reach at all: defer rather than order concrete that the
    // engine would reject.
    Ground unreachable;
    unreachable.reachEverywhere = false;
    const auto none = planFoundation(4, 4, 2, 2, true, true, unreachable);
    REQUIRE_FALSE(none.complete);
    REQUIRE(none.orders.empty());
}

// ---------------------------------------------------------------------------
// Generation outside the city simulation
// ---------------------------------------------------------------------------

namespace {
QuantBotPowerInvestmentPolicy::Investment matureBase() {
    QuantBotPowerInvestmentPolicy::Investment state;
    state.deficit = 100;          // one windtrap short
    state.generatorOutput = 100;  // Vanilla windtrap
    state.unitCapital = 320;      // 300 credits plus a 2x2 of five-credit slab
    state.spendable = 4000;
    state.netIncome = 8000;       // measured over four game minutes
    state.incomeMinutes = QuantBotSpendingPolicy::horizonMinutes;
    state.repairMilliPerHitpoint = 9000; // ~36 credits of repair a game minute
    return state;
}

// The mature Vanilla base measured on Air: 1800 produced against 1840 demanded,
// short again every time it finishes a building.
QuantBotPowerInvestmentPolicy::PowerTarget airBase() {
    QuantBotPowerInvestmentPolicy::PowerTarget target;
    target.demand = 1840;
    target.produced = 1800;
    target.standardOutput = 100; // the Vanilla windtrap it can order
    return target;
}
} // namespace

TEST_CASE("Generation sizes the whole package before buying one generator", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    REQUIRE(generatorsNeeded(0, 100) == 0);
    REQUIRE(generatorsNeeded(-50, 100) == 0);
    REQUIRE(generatorsNeeded(100, 100) == 1);
    REQUIRE(generatorsNeeded(101, 100) == 2);
    REQUIRE(generatorsNeeded(250, 100) == 3);
    // A mod that publishes no usable output falls back to the Vanilla windtrap.
    REQUIRE(generatorsNeeded(250, 0) == generatorsNeeded(250, defaultGeneratorOutput));

    auto state = matureBase();
    REQUIRE(packageCapital(state) == 320);
    state.deficit = 250;
    REQUIRE(packageCapital(state) == 960); // half a package closes nothing
}

TEST_CASE("Generation needs income, cash and a benefit it can name", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    auto state = matureBase();
    REQUIRE(evaluate(state) == Verdict::BuyForRepairs);

    // No deficit at all once repairs and queued generators are counted.
    auto covered = state; covered.deficit = 0;
    REQUIRE(evaluate(covered) == Verdict::NoDeficit);

    // The cash has to be there after the core queues and the reserve.
    auto poor = state; poor.spendable = 319;
    REQUIRE(evaluate(poor) == Verdict::Unaffordable);
    poor.spendable = 320;
    REQUIRE(buys(evaluate(poor)));

    // Thirty game seconds of measured net income must replace the capital.
    // Four minutes of income at 8x the capital is exactly the boundary.
    auto boundary = state; boundary.netIncome = 320 * 8;
    REQUIRE(buys(evaluate(boundary)));
    boundary.netIncome = 320 * 8 - 1;
    REQUIRE(evaluate(boundary) == Verdict::IncomeTooLow);
    // A starting balance is not income: the forecast, not the bank, decides.
    auto banked = state; banked.netIncome = 0; banked.spendable = 100000;
    REQUIRE(evaluate(banked) == Verdict::IncomeTooLow);
}

TEST_CASE("Avoided repairs or plain prosperity justify the generator", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    auto state = matureBase();
    // 9000 milli per hitpoint, four hitpoints a minute, is 36 credits a minute
    // and 360 over ten game minutes: short of the 320-credit package? No.
    REQUIRE(avoidedRepairPerMinute(state) == 36);
    REQUIRE(repaysThroughAvoidedRepairs(state, 320));
    REQUIRE(evaluate(state) == Verdict::BuyForRepairs);

    // A base made of large buildings repairs for free, so the repair case
    // alone can never justify generation. Modest cash keeps it unbought.
    auto free = state; free.repairMilliPerHitpoint = 0;
    REQUIRE(avoidedRepairPerMinute(free) == 0);
    REQUIRE(evaluate(free) == Verdict::RepairsTooCheap);

    // Once the spare cash after the reserve is comfortable, a prosperous base
    // stops arguing about repair credits and closes the deficit.
    auto rich = free; rich.spendable = 5000;
    REQUIRE(evaluate(rich) == Verdict::RepairsTooCheap);
    rich.spendable = 5001;
    REQUIRE(evaluate(rich) == Verdict::BuyWhileProsperous);
    REQUIRE(buys(evaluate(rich)));

    // Prosperity never excuses the income or affordability gates.
    auto idle = rich; idle.netIncome = 0;
    REQUIRE(evaluate(idle) == Verdict::IncomeTooLow);
    auto huge = rich; huge.deficit = 10000; // 100 windtraps, 32000 credits
    REQUIRE(evaluate(huge) == Verdict::Unaffordable);
}

TEST_CASE("The cheapest complete generation package wins deterministically", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    GeneratorChoice best;
    GeneratorChoice wind{Structure_WindTrap, 100, 320, 320 * 3};        // 250 deficit
    GeneratorChoice reactor{Structure_NuclearPlant, 1000, 1545, 1545};
    REQUIRE(preferGenerator(wind, best));
    best = wind;
    REQUIRE(preferGenerator(reactor, best) == false); // 1545 > 960
    best = GeneratorChoice{};
    REQUIRE(preferGenerator(reactor, best));
    // Equal packages keep the lowest item id, so every peer agrees. The
    // windtrap is id 18 and the advanced windtrap id 56.
    GeneratorChoice cheapWind{Structure_WindTrap, 100, 500, 500};
    GeneratorChoice cheapAdvanced{Structure_AdvancedWindTrap, 300, 500, 500};
    best = cheapAdvanced;
    REQUIRE(preferGenerator(cheapWind, best));
    best = cheapWind;
    REQUIRE_FALSE(preferGenerator(cheapAdvanced, best));
}

TEST_CASE("Generation aims at a buffer rather than the shortage of the moment", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    auto air = airBase();
    // 1840 demanded, a fifth of it reserved on top, against 1800 standing.
    REQUIRE(anticipatedDemand(air) == 1840);
    REQUIRE(bufferReserve(air) == 368);
    REQUIRE(bufferedTarget(air) == 2208);
    REQUIRE(effectiveCapacity(air) == 1800);
    REQUIRE(bufferedShortfall(air) == 408);
    REQUIRE(operatingShortfall(air) == 40);
    REQUIRE(generatorsNeeded(bufferedShortfall(air), 100) == 5);

    // Being exactly powered is the state the user reported: every new building
    // starts a shortage. A powered base below the buffer still invests.
    auto powered = air; powered.produced = 1840;
    REQUIRE(operatingShortfall(powered) == 0);
    REQUIRE(bufferedShortfall(powered) == 368);
    REQUIRE(generatorsNeeded(bufferedShortfall(powered), 100) == 4);

    // With the reserve standing there is nothing to buy.
    auto reserved = air; reserved.produced = 2208;
    REQUIRE(bufferedShortfall(reserved) == 0);
    REQUIRE(operatingShortfall(reserved) == 0);
    auto spare = air; spare.produced = 5000;
    REQUIRE(bufferedShortfall(spare) == 0);

    // A small base reserves one whole standard generator instead of a token
    // fifth of a tiny demand; a large one reserves the proportionate share.
    QuantBotPowerInvestmentPolicy::PowerTarget small;
    small.demand = 200; small.produced = 200; small.standardOutput = 100;
    REQUIRE(bufferReserve(small) == 100);
    REQUIRE(bufferedTarget(small) == 300);
    QuantBotPowerInvestmentPolicy::PowerTarget large;
    large.demand = 1000; large.produced = 1000; large.standardOutput = 100;
    REQUIRE(bufferReserve(large) == 200);
    // The share rounds up, so it never quietly becomes no reserve at all.
    QuantBotPowerInvestmentPolicy::PowerTarget odd;
    odd.demand = 101; odd.standardOutput = 0;
    REQUIRE(bufferReserve(odd) == 21);
}

TEST_CASE("Ordered demand and every pending generator are counted exactly once", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    auto air = airBase();
    // The buildings already on order are the demand that caused the shortage
    // the user saw, so they belong in the target before it is sized.
    auto ordering = air; ordering.queuedDemand = 160;
    REQUIRE(anticipatedDemand(ordering) == 2000);
    REQUIRE(bufferReserve(ordering) == 400);
    REQUIRE(bufferedTarget(ordering) == 2400);
    REQUIRE(bufferedShortfall(ordering) == 600);

    // Generation already on order anywhere in the base counts as capacity, so
    // a second yard on the same pass cannot buy the package twice.
    auto pending = air; pending.pendingOutput = 408;
    REQUIRE(effectiveCapacity(pending) == 2208);
    REQUIRE(bufferedShortfall(pending) == 0);

    // One pending generator does not close a five-generator package, and does
    // not block it either: the rest is still missing and still gets ordered.
    auto partial = air; partial.pendingOutput = 100;
    REQUIRE(bufferedShortfall(partial) == 308);
    REQUIRE(generatorsNeeded(bufferedShortfall(partial), 100) == 4);
    // Serial delivery: each pass sees one more pending generator and asks for
    // one fewer, and the sequence ends rather than running away.
    int ordered = 0;
    auto serial = air;
    while (bufferedShortfall(serial) > 0 && ordered < 20) {
        serial.pendingOutput += 100;
        ++ordered;
    }
    REQUIRE(ordered == 5);
    REQUIRE(bufferedShortfall(serial) == 0);
}

TEST_CASE("Generation a repair will restore is not bought twice", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    auto air = airBase();
    // A damaged generator recovering 408 of output reaches the buffered target
    // on its own, so repairing it is the whole answer.
    auto recovering = air; recovering.restorable = 408;
    REQUIRE(effectiveCapacity(recovering) == 2208);
    REQUIRE(bufferedShortfall(recovering) == 0);
    // Restored health that covers the shortage but not the buffer leaves the
    // buffer to be bought, and nothing more.
    auto partial = air; partial.restorable = 40;
    REQUIRE(operatingShortfall(partial) == 0);
    REQUIRE(bufferedShortfall(partial) == 368);
    // Repairs and pending generation are both capacity, and are added once.
    auto both = air; both.restorable = 40; both.pendingOutput = 300;
    REQUIRE(effectiveCapacity(both) == 2140);
    REQUIRE(bufferedShortfall(both) == 68);
}

TEST_CASE("A buffered package faces the same cash, income and benefit gates", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    auto buffered = matureBase();
    buffered.deficit = bufferedShortfall(airBase()); // 408: five windtraps
    buffered.netIncome = 1600 * 8;                   // exactly replaces the capital
    buffered.spendable = 5001;
    REQUIRE(packageCapital(buffered) == 1600);
    REQUIRE(evaluate(buffered) == Verdict::BuyWhileProsperous);

    // Affordability is judged on the whole buffered package, once.
    auto poor = buffered; poor.spendable = 1599;
    REQUIRE(evaluate(poor) == Verdict::Unaffordable);
    // Affordable is not the same as justified: 360 credits of avoided repair
    // over ten minutes does not repay 1600, and this base is not prosperous.
    poor.spendable = 1600;
    REQUIRE(evaluate(poor) == Verdict::RepairsTooCheap);

    // A funded base with no measured income still does not spend.
    auto funded = buffered; funded.spendable = 100000; funded.netIncome = 0;
    REQUIRE(evaluate(funded) == Verdict::IncomeTooLow);
    auto earning = funded; earning.netIncome = 1600 * 8;
    REQUIRE(buys(evaluate(earning)));
    earning.netIncome = 1600 * 8 - 1;
    REQUIRE(evaluate(earning) == Verdict::IncomeTooLow);
}

TEST_CASE("A refused buffer falls back to the shortage, never to no gate at all", "[quantbot][power][economy]") {
    using namespace QuantBotPowerInvestmentPolicy;
    auto buffered = matureBase();
    buffered.deficit = 408;            // five windtraps, 1600 credits
    buffered.spendable = 700;
    buffered.netIncome = 3000;
    auto shortage = buffered;
    shortage.deficit = 40;             // one windtrap, 320 credits

    // The buffer is what a base that cannot afford it gives up -- not the
    // power it needs to stop degrading, and not one of the gates.
    REQUIRE(evaluate(buffered) == Verdict::Unaffordable);
    const auto relief = decide(buffered, shortage);
    REQUIRE(relief.trimmedToShortage);
    REQUIRE(relief.deficit == 40);
    REQUIRE(relief.capital == 320);
    REQUIRE(buys(relief.verdict));

    // An affordable buffer is bought whole, and the buffer is never added to
    // the shortage: the two are alternatives, so the capital is one or other.
    auto rich = buffered; rich.spendable = 100000; rich.netIncome = 100000;
    auto richShortage = shortage; richShortage.spendable = 100000; richShortage.netIncome = 100000;
    const auto whole = decide(rich, richShortage);
    REQUIRE_FALSE(whole.trimmedToShortage);
    REQUIRE(whole.deficit == 408);
    REQUIRE(whole.capital == 1600);
    REQUIRE(buys(whole.verdict));

    // A base that is powered has no shortage to fall back to, so a refused
    // buffer simply goes unbought.
    auto noShortage = shortage; noShortage.deficit = 0;
    const auto powered = decide(buffered, noShortage);
    REQUIRE_FALSE(powered.trimmedToShortage);
    REQUIRE(powered.verdict == Verdict::Unaffordable);

    // Nothing is waived: if the shortage package fails the same gates, it is
    // refused too, and the refusal reported is still the buffered one.
    auto broke = buffered; broke.spendable = 100;
    auto brokeShortage = shortage; brokeShortage.spendable = 100;
    const auto refused = decide(broke, brokeShortage);
    REQUIRE_FALSE(refused.trimmedToShortage);
    REQUIRE(refused.verdict == Verdict::Unaffordable);
    auto idle = buffered; idle.netIncome = 0; idle.spendable = 100000;
    auto idleShortage = shortage; idleShortage.netIncome = 0; idleShortage.spendable = 100000;
    REQUIRE(decide(idle, idleShortage).verdict == Verdict::IncomeTooLow);
    REQUIRE_FALSE(decide(idle, idleShortage).trimmedToShortage);
}

TEST_CASE("Bulk foundations choose the open side before closing a residual strip", "[quantbot][concrete][planner]") {
    Ground ground;
    // The right side is occupied, but a bulk slab can overhang on the left.
    ground.blocked.insert({7,4}); ground.blocked.insert({7,5});
    const auto plan = planFoundation(4,4,3,2,true,true,ground);
    REQUIRE(plan.complete);
    REQUIRE(plan.orders.size()==2);
    REQUIRE(plan.orders[0].item==Structure_Slab4);
    REQUIRE(plan.orders[1].item==Structure_Slab4);
    REQUIRE(paves(plan,3,4));
    REQUIRE(paves(plan,3,5));
    REQUIRE_FALSE(paves(plan,7,4));
}

#include <players/McvDeployPolicy.h>
namespace {
struct LocalDeployFixture {
    static constexpr int w = 60, h = 40;
    std::vector<McvDeployPolicy::Tile> tiles = std::vector<McvDeployPolicy::Tile>(w * h);
    McvDeployPolicy::Tile& at(int x, int y) { return tiles[y * w + x]; }
    int index(int x, int y) const { return y * w + x; }
    void rock(int x0, int y0, int x1, int y1) {
        for(int y = y0; y < y1; ++y) for(int x = x0; x < x1; ++x) { at(x,y).rock = true; at(x,y).free = true; }
    }
    void structure(int x, int y) { at(x,y).owned = true; at(x,y).free = false; at(x,y).passable = false; }
    LocalDeployFixture() {
        for(auto& tile : tiles) tile.passable = true;
        rock(10,10,30,25);   // The formation the base already stands on.
        rock(40,10,55,25);   // Untouched rock across the sand: a colony site.
        structure(11,11);
    }
    McvDeployPolicy::Site choose(int x, int y) {
        return McvDeployPolicy::choose(w, h, tiles, index(x,y), 12);
    }
};
}

TEST_CASE("An MCV on usable base rock deploys where it stands", "[quantbot][mcv]") {
    LocalDeployFixture map;
    const auto site = map.choose(20,20);
    REQUIRE(site.valid());
    // Distance is the first key, so the MCV's own footprint wins and the
    // chosen site does not change while it keeps standing there.
    CHECK(site.x == 20);
    CHECK(site.y == 20);
    CHECK(site.distance == 0);
    CHECK(McvDeployPolicy::choose(LocalDeployFixture::w, LocalDeployFixture::h, map.tiles,
        map.index(20,20), 12).x == site.x);
}

TEST_CASE("Local deployment never colonises rock the base does not hold", "[quantbot][mcv]") {
    LocalDeployFixture map;
    for(int y = 10; y < 25; ++y) for(int x = 10; x < 30; ++x) map.at(x,y).blocked = true;
    // Only the far formation is left. Local growth declines instead of
    // starting a colony; that decision belongs to the expansion policy.
    CHECK_FALSE(map.choose(20,20).valid());
    // The foreign formation is genuinely usable, it is simply not ours.
    map.structure(41,11);
    const auto colony = map.choose(20,20);
    REQUIRE(colony.valid());
    CHECK(colony.x >= 40);
}

TEST_CASE("A site remembered by another MCV is not handed out twice", "[quantbot][mcv]") {
    LocalDeployFixture map;
    // The caller blocks the footprint another MCV is already driving at.
    for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx) map.at(20+dx,20+dy).blocked = true;
    const auto site = map.choose(20,20);
    REQUIRE(site.valid());
    CHECK(site.x < 30);
    CHECK((site.x + 1 < 20 || site.x > 21 || site.y + 1 < 20 || site.y > 21));
    CHECK(site.distance > 0);
}

TEST_CASE("A unit parked on the rock moves the footprint, not the formation", "[quantbot][mcv]") {
    LocalDeployFixture map;
    map.at(20,20).free = false;   // Another vehicle crossing the site.
    const auto site = map.choose(20,20);
    REQUIRE(site.valid());
    CHECK(site.x < 30);
    CHECK_FALSE((site.x <= 20 && 20 <= site.x + 1 && site.y <= 20 && 20 <= site.y + 1));
}

TEST_CASE("An MCV walled in gets no destination rather than an unreachable one", "[quantbot][mcv]") {
    LocalDeployFixture map;
    for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx)
        if(dx || dy) map.structure(20+dx,20+dy);
    CHECK_FALSE(map.choose(20,20).valid());
}

TEST_CASE("A cramped home formation still gives the MCV a site", "[quantbot][mcv]") {
    LocalDeployFixture map;
    for(auto& tile : map.tiles) { tile = McvDeployPolicy::Tile(); tile.passable = true; }
    map.rock(10,10,13,14);        // Twelve tiles, one of them built on.
    map.structure(10,10);
    const auto site = map.choose(11,12);
    REQUIRE(site.valid());
    CHECK(site.room < 12);        // Below the wanted room, but not stranded.
    CHECK(site.x >= 10);
    CHECK(site.x <= 11);
}

TEST_CASE("An opening MCV keeps the formation it stands on", "[quantbot][mcv]") {
    LocalDeployFixture map;
    map.at(11,11) = McvDeployPolicy::Tile();
    map.at(11,11).rock = true; map.at(11,11).free = true; map.at(11,11).passable = true;
    const auto site = map.choose(45,20);
    REQUIRE(site.valid());
    CHECK(site.x == 45);
    CHECK(site.y == 20);
}

TEST_CASE("Local deployment ranks travel first and room second", "[quantbot][mcv]") {
    using McvDeployPolicy::Site;
    CHECK(McvDeployPolicy::betterSite(Site{5,5,16,0}, Site{9,9,64,3}));
    CHECK(McvDeployPolicy::betterSite(Site{9,9,64,3}, Site{5,5,16,3}));
    CHECK_FALSE(McvDeployPolicy::betterSite(Site{9,9,64,4}, Site{5,5,16,3}));
}

TEST_CASE("A destination crossed by traffic is kept, not rerolled", "[quantbot][mcv]") {
    LocalDeployFixture map;
    const int trip = map.index(24,14);
    map.at(24,14).free = false;   // A vehicle standing on the destination.
    auto kept = McvDeployPolicy::choose(LocalDeployFixture::w, LocalDeployFixture::h, map.tiles,
        map.index(20,20), 12, trip);
    CHECK(kept.x == 24);
    CHECK(kept.y == 14);
    // A building or a threat on the site does retire it, and the MCV falls
    // back to the rock it is already standing on.
    map.at(24,14).free = true;
    map.at(24,14).blocked = true;
    kept = McvDeployPolicy::choose(LocalDeployFixture::w, LocalDeployFixture::h, map.tiles,
        map.index(20,20), 12, trip);
    CHECK(kept.x == 20);
    CHECK(kept.y == 20);
    // A destination on rock the base does not hold is never confirmed.
    LocalDeployFixture foreign;
    const auto refused = McvDeployPolicy::choose(LocalDeployFixture::w, LocalDeployFixture::h,
        foreign.tiles, foreign.index(20,20), 12, foreign.index(45,14));
    CHECK(refused.x == 20);
    CHECK(refused.y == 20);
}

TEST_CASE("Local deployment searches beyond rejected ground-access candidates", "[quantbot][mcv]") {
    LocalDeployFixture map;
    int rejected=0;
    const auto site=McvDeployPolicy::choose(LocalDeployFixture::w,LocalDeployFixture::h,
        map.tiles,map.index(20,20),12,-1,[&](int x,int y) {
            if(x==27&&y==22) return true;
            ++rejected;
            return false;
        });
    REQUIRE(site.valid());
    CHECK(site.x==27);
    CHECK(site.y==22);
    CHECK(rejected>4);
}

// --- Space-driven colonisation -------------------------------------------
// Ordering an MCV because the city has nowhere left to build is a separate
// decision from growing production capacity: it answers "is there still room
// here", not "are there enough yards".

namespace {

struct ColonyRoom {
    static constexpr int w = 12, h = 12;
    std::vector<char> tiles = std::vector<char>(w*h, 0);
    void rock(int x0,int y0,int x1,int y1) {
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) tiles[y*w+x]=1;
    }
    int footprints(int limit = 64) const {
        return QuantBotColonisationPolicy::freeFootprints(w,h,tiles,2,2,limit);
    }
};

QuantBotColonisationPolicy::Demand builtOutCity() {
    QuantBotColonisationPolicy::Demand demand;
    demand.citySim = true;
    demand.customGame = true;
    demand.siteAvailable = true;
    demand.freeFootprints = 0;
    demand.yards = 6;
    return demand;
}

}

TEST_CASE("Building room counts whole footprints, not free tiles", "[quantbot][colonisation]") {
    ColonyRoom room;
    // A 3x3 patch is nine free tiles and exactly one more building.
    room.rock(1,1,4,4);
    CHECK(room.footprints() == 1);

    // A single wide strip holds no building at all: counting tiles would call
    // twelve of them room to grow.
    ColonyRoom strip;
    strip.rock(0,5,12,6);
    CHECK(strip.footprints() == 0);

    // Four separate corners are four buildings.
    ColonyRoom corners;
    corners.rock(0,0,2,2); corners.rock(10,0,12,2);
    corners.rock(0,10,2,12); corners.rock(10,10,12,12);
    CHECK(corners.footprints() == 4);

    // The count stops at the limit; the decision only needs "cramped or not".
    ColonyRoom plenty;
    plenty.rock(0,0,12,12);
    CHECK(plenty.footprints(4) == 4);
    CHECK(plenty.footprints(64) == 36);

    // A degenerate survey never reads as room.
    CHECK(QuantBotColonisationPolicy::freeFootprints(0,0,{},2,2,4) == 0);
    CHECK(QuantBotColonisationPolicy::freeFootprints(4,4,std::vector<char>(9,1),2,2,4) == 0);
}

TEST_CASE("A city out of building rock orders a colonist whatever its yard count",
          "[quantbot][colonisation]") {
    auto demand = builtOutCity();
    CHECK(QuantBotColonisationPolicy::due(demand));
    // Production capacity is irrelevant: a dozen yards on full rock still
    // cannot build anything more.
    demand.yards = 12;
    CHECK(QuantBotColonisationPolicy::due(demand));

    // The placement search refusing another factory is the same verdict, even
    // while a few odd corners of rock remain.
    auto blocked = builtOutCity();
    blocked.freeFootprints = QuantBotColonisationPolicy::kCrampedFootprints + 2;
    CHECK_FALSE(QuantBotColonisationPolicy::due(blocked));
    blocked.productionRoomBlocked = true;
    CHECK(QuantBotColonisationPolicy::due(blocked));
}

TEST_CASE("Colonisation stays shut where it would be wrong", "[quantbot][colonisation]") {
    const auto base = builtOutCity();

    // Room to build at home: grow there instead of driving across the map.
    auto roomy = base;
    roomy.freeFootprints = QuantBotColonisationPolicy::kCrampedFootprints;
    CHECK_FALSE(QuantBotColonisationPolicy::due(roomy));

    // No destination the survey accepts: unsafe, unreachable or too small rock
    // never produces a site, so nothing is ordered.
    auto nowhere = base;
    nowhere.siteAvailable = false;
    CHECK_FALSE(QuantBotColonisationPolicy::due(nowhere));

    // One colonist at a time: an MCV alive, paid for or queued is the answer.
    auto alreadyBought = base;
    alreadyBought.mcvsIncludingQueued = 1;
    CHECK_FALSE(QuantBotColonisationPolicy::due(alreadyBought));

    // No yard means the base is gone: that is recovery, not colonisation.
    auto homeless = base;
    homeless.yards = 0;
    CHECK_FALSE(QuantBotColonisationPolicy::due(homeless));

    // Campaign missions keep the base the script gave them, and a helper on
    // someone else's house does not found cities of its own.
    auto campaign = base;
    campaign.campaignGame = true;
    CHECK_FALSE(QuantBotColonisationPolicy::due(campaign));
    auto scripted = base;
    scripted.customGame = false;
    CHECK_FALSE(QuantBotColonisationPolicy::due(scripted));
    auto helper = base;
    helper.supportMode = true;
    CHECK_FALSE(QuantBotColonisationPolicy::due(helper));

    // Vanilla games keep their own MCV rule.
    auto vanilla = base;
    vanilla.citySim = false;
    CHECK_FALSE(QuantBotColonisationPolicy::due(vanilla));

    // The game option ceiling is a hard limit on yards, colony or not.
    auto capped = base;
    capped.yardLimit = capped.yards;
    CHECK_FALSE(QuantBotColonisationPolicy::due(capped));
    capped.yardLimit = capped.yards + 1;
    CHECK(QuantBotColonisationPolicy::due(capped));
}

TEST_CASE("Mobile anti-air floor protects launcher share from damage-only learning",
          "[quantbot][production][unitmix]") {
    // The reviewed save: both houses had learned the launcher share down well
    // below the floor while enemy Ornithopters were active.
    UnitMixPolicy::Mix learned{};
    learned[0] = 500;   // tank
    learned[1] = 188;   // siege
    learned[2] = 1081;  // launcher - house 0 Harkonnen, cycle 106425
    learned[3] = 322;   // special
    learned[4] = 3327;  // ornithopter
    learned[5] = 1000; learned[6] = 1000; learned[7] = 2582; // light vehicles
    const int before = std::accumulate(learned.begin(), learned.end(), 0);
    REQUIRE(before == 10000);

    const auto floored = UnitMixPolicy::applyFloor(learned, 2,
        UnitMixPolicy::kMobileAntiAirFloorBps);
    CHECK(floored[2] == UnitMixPolicy::kMobileAntiAirFloorBps);
    // Total preserved, so the mix is still a share of the same budget.
    CHECK(std::accumulate(floored.begin(), floored.end(), 0) == 10000);
    // Funded from the others in proportion; nobody gains, nobody goes negative.
    for(size_t i = 0; i < floored.size(); ++i) {
        if(i == 2) continue;
        CHECK(floored[i] <= learned[i]);
        CHECK(floored[i] >= 0);
    }
    // The largest contributor pays the most.
    CHECK(learned[4] - floored[4] > learned[0] - floored[0]);

    // Deterministic: the same input always produces the same mix.
    CHECK(UnitMixPolicy::applyFloor(learned, 2, UnitMixPolicy::kMobileAntiAirFloorBps) == floored);

    // House 5 Mercenary, cycle 106395.
    UnitMixPolicy::Mix other{};
    other[0] = 263; other[1] = 444; other[2] = 1361; other[3] = 348;
    other[4] = 3799; other[5] = 900; other[6] = 900; other[7] = 1985;
    REQUIRE(std::accumulate(other.begin(), other.end(), 0) == 10000);
    const auto otherFloored = UnitMixPolicy::applyFloor(other, 2,
        UnitMixPolicy::kMobileAntiAirFloorBps);
    CHECK(otherFloored[2] == UnitMixPolicy::kMobileAntiAirFloorBps);
    CHECK(std::accumulate(otherFloored.begin(), otherFloored.end(), 0) == 10000);
}

TEST_CASE("Anti-air floor never lowers a share, invents budget or breaks the cap",
          "[quantbot][production][unitmix]") {
    UnitMixPolicy::Mix healthy{};
    healthy[2] = 4800; healthy[0] = 2600; healthy[4] = 2600;
    REQUIRE(std::accumulate(healthy.begin(), healthy.end(), 0) == 10000);
    // Already above the floor: untouched. The floor is a floor, not a target.
    CHECK(UnitMixPolicy::applyFloor(healthy, 2, UnitMixPolicy::kMobileAntiAirFloorBps) == healthy);

    // A disabled floor is a no-op, which is what an unauthorised house uses.
    UnitMixPolicy::Mix starved{};
    starved[2] = 100; starved[4] = 9900;
    CHECK(UnitMixPolicy::applyFloor(starved, 2, 0) == starved);

    // Nothing to redistribute from: the mix is left alone rather than invented.
    UnitMixPolicy::Mix only{};
    only[2] = 10000;
    CHECK(UnitMixPolicy::applyFloor(only, 2, UnitMixPolicy::kMobileAntiAirFloorBps) == only);

    // The existing per-type upper cap still holds after flooring, because every
    // other entry can only fall.
    UnitMixPolicy::Mix capped{};
    capped[2] = 0; capped[4] = 8000; capped[0] = 2000;
    const auto cappedFloored = UnitMixPolicy::applyFloor(capped, 2,
        UnitMixPolicy::kMobileAntiAirFloorBps);
    CHECK(cappedFloored[4] <= 8000);
    CHECK(cappedFloored[2] == UnitMixPolicy::kMobileAntiAirFloorBps);
    CHECK(std::accumulate(cappedFloored.begin(), cappedFloored.end(), 0) == 10000);
}
