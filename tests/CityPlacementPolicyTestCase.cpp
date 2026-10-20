#include <catch2/catch_test_macros.hpp>
#include <Definitions.h>
#include <players/CityPlacementPolicy.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/QuantBotFoundationPolicy.h>
#include <players/CityServiceInvestmentPolicy.h>
#include <dunecity/PopulationDensityPolicy.h>
#include <players/RocketTurretPolicy.h>
#include <dunecity/CityConstants.h>
#include <fstream>
#include <cstdlib>

TEST_CASE("Spice opening funds refinery workers before factory infrastructure", "[ai][economy]") {
    using namespace QuantBotBuildPolicy;
    REQUIRE(openingSpiceRefineries(desiredSpiceHarvesters(773600,4,40)) == 3);
    REQUIRE(openingSpiceRefineries(desiredSpiceHarvesters(24000,4,40)) == 2);
    REQUIRE(openingSpiceRefineries(desiredSpiceHarvesters(0,4,40)) == 1);
    REQUIRE(openingSpiceRefineries(desiredSpiceHarvesters(773600,4,1)) == 1);
}

TEST_CASE("Harvester investment follows spice and processing rather than army size", "[ai][economy]") {
    using QuantBotBuildPolicy::fundedSpiceHarvesters;
    REQUIRE(fundedSpiceHarvesters(40,3) == 9);
    REQUIRE(fundedSpiceHarvesters(40,14) == 40);
    REQUIRE(fundedSpiceHarvesters(2,14) == 2);
    REQUIRE(fundedSpiceHarvesters(40,0) == 0);
}

TEST_CASE("Generators compare incremental cost space and protected working capital", "[ai][power]") {
    using QuantBotBuildPolicy::preferNuclearPower;
    REQUIRE_FALSE(preferNuclearPower(100,100,300,2000,10,10000,600));
    REQUIRE_FALSE(preferNuclearPower(600,100,300,2000,10,10000,600));
    REQUIRE(preferNuclearPower(700,100,300,2000,10,10000,600));
    REQUIRE_FALSE(preferNuclearPower(700,100,300,2000,10,2500,600));
    REQUIRE(preferNuclearPower(300,100,300,2000,2,10000,600));
    REQUIRE_FALSE(preferNuclearPower(700,100,100,2000,10,10000,600));
    REQUIRE_FALSE(preferNuclearPower(0,100,300,2000,0,10000,600));
}

TEST_CASE("Power growth forecast escapes repeated small windtrap top-ups", "[ai][power]") {
    using namespace QuantBotBuildPolicy;
    const int growth = projectedPowerGrowth(6000,6200,30,120);
    REQUIRE(growth == 800);
    REQUIRE_FALSE(preferNuclearPower(100,100,300,2000,20,10000,600));
    REQUIRE(preferNuclearPower(100+growth,100,300,2000,20,10000,600));
    REQUIRE(projectedPowerGrowth(6200,6200,30,120) == 0);
    REQUIRE(projectedPowerGrowth(6200,6000,30,120) == 0);
    REQUIRE(projectedPowerGrowth(6000,6200,0,120) == 0);
    REQUIRE(projectedPowerGrowth(0,100,30,120) == 100); // bounded startup/rebuild jump
}

TEST_CASE("Rich cities and blackout recovery invest in nuclear capacity", "[ai][power]") {
    using namespace QuantBotBuildPolicy;
    // Recorded 40.75-minute choice: rich, legal nuclear site, only 489 short.
    REQUIRE(preferNuclearPower(489,100,300,2000,5,234226,300));
    REQUIRE(preferNuclearPower(50,100,300,2000,20,2300,300,true));
    REQUIRE_FALSE(preferNuclearPower(50,100,300,2000,20,2299,300,true));
    REQUIRE_FALSE(preferNuclearPower(50,100,300,2000,20,2300,300,false));
    REQUIRE_FALSE(preferNuclearPower(0,100,300,2000,20,300000,300,true));
    REQUIRE(preferNuclearPower(50,100,300,2000,20,10300,300));
    REQUIRE_FALSE(preferNuclearPower(50,100,300,2000,20,10299,300));
}

TEST_CASE("Zone recovery preserves the same planned load after blackout shrinkage", "[ai][power]") {
    using namespace QuantBotBuildPolicy;
    const int otherLoad = 800, matureLots = 1200;
    const int before = otherLoad + 600 + cityGrowthPowerHeadroom(600,matureLots,80,24);
    const int after = otherLoad + 200 + cityGrowthPowerHeadroom(200,matureLots,0,24);
    REQUIRE(before == 2024);
    REQUIRE(after == before);
    REQUIRE(cityGrowthPowerHeadroom(1200,1200,400,24) == 424);
    REQUIRE(cityGrowthPowerHeadroom(0,0,0,0) == 0);
    REQUIRE(cityGrowthPowerHeadroom(0,36,20,18) == 54);
}

TEST_CASE("Early land-value turrets must repay costs without relying on crime utility", "[city][placement]") {
    CityServiceInvestmentPolicy::Value v;
    v.buildCost=250; v.upkeep=15; v.powerCost=50;
    v.crime=1000; v.defense=1000; v.tax=300;
    REQUIRE_FALSE(v.repaysThroughLandValue());
    v.growthTax=16;
    REQUIRE(v.repaysThroughLandValue());
    v.overlapPenalty=20;
    REQUIRE_FALSE(v.repaysThroughLandValue());
    v.overlapPenalty=0; v.neighbourhoodTax=100; v.crime=0;
    REQUIRE_FALSE(v.landValueTurretEligible());
    v.crime=1;
    REQUIRE(v.landValueTurretEligible());
    auto industrial = v;
    industrial.neighbourhoodTax=0;
    REQUIRE_FALSE(industrial.landValueTurretEligible());
    REQUIRE(v.betterThan(industrial));
}

TEST_CASE("Population density diffuses point sources instead of saturating starter blocks", "[city][crime]") {
    DuneCity::CityMapLayer<uint8_t> density;
    density.init(32,32,2);
    density.set(8,8,128); // Level-one residential source: 16 * 8.
    DuneCity::smoothPopulationDensity(density,32,32);
    REQUIRE(density.get(8,8) == 52); // 13 centre-return walks / 4^3, then doubled.
    REQUIRE(density.get(9,8) == 48);
    REQUIRE(density.get(12,8) == 0);
    density.init(32,32,2);
    DuneCity::smoothPopulationDensity(density,32,32);
    REQUIRE(density.get(8,8) == 0);
}

TEST_CASE("Service investment compares economic return without bypassing emergency crime", "[city][placement]") {
    using namespace CityServiceInvestmentPolicy;
    Value police{600,100,0,0,500,100,10};
    Value turret{100,700,50,100,250,15,10};
    REQUIRE(turret.betterThan(police));
    REQUIRE(turret.useful(true));
    turret.crime = 0;
    REQUIRE_FALSE(turret.useful(false));
    REQUIRE_FALSE(turret.useful(true));
    turret.tax = 0; turret.growthTax = 0; turret.defense = 0;
    REQUIRE_FALSE(turret.useful(false));
    turret.crime = 100;
    REQUIRE(police.betterThan(turret));
    Value expensive = police;
    expensive.upkeep += 500;
    REQUIRE(police.betterThan(expensive));
    REQUIRE(annualTaxGain(1000,7,128,100) == 13);
    REQUIRE(annualTaxGain(1000,7,0,100) == 0);
    REQUIRE(annualTaxGain(1000,7,128,0) == 0);
    DuneCity::ParkTerrainPolicy terrain;
    terrain.init(120,120);
    REQUIRE(parkContribution(Structure_PoliceStation,10,10,10,10,2,terrain) == 0);
    REQUIRE(parkContribution(Structure_RocketTurret,10,10,10,10,2,terrain) == 7);
    REQUIRE(parkContribution(Structure_RocketTurret,10,10,11,11,2,terrain) == 7);
    REQUIRE(parkContribution(Structure_RocketTurret,10,10,12,10,2,terrain) == 1);
    REQUIRE(parkContribution(Structure_RocketTurret,10,10,100,100,2,terrain) == 0);
    terrain.addSource(10,11,15); // another yard's planned source
    REQUIRE(parkContribution(Structure_RocketTurret,10,10,10,10,2,terrain) == 8);
    expensive = police;
    expensive.overlapPenalty = 500;
    REQUIRE(police.betterThan(expensive));
}

TEST_CASE("Dangerous developed districts reserve a share of construction", "[city][placement]") {
    using namespace QuantBotBuildPolicy;
    REQUIRE(crimeServiceOrderInterval(0, 0) == 0);
    REQUIRE(crimeServiceOrderInterval(100, 0) == 0);
    REQUIRE(crimeServiceOrderInterval(100, 1) == 4);
    REQUIRE(crimeServiceOrderInterval(100, 24) == 4);
    REQUIRE(crimeServiceOrderInterval(100, 0, 25) == 6);
    REQUIRE(crimeServiceOrderInterval(100, 25) == 4);
    REQUIRE(crimeServiceOrderInterval(100, 50) == 4);
    REQUIRE(crimeServiceOrderInterval(100, 51) == 2);
    REQUIRE_FALSE(crimeServiceOrderDue(0, 3));
    for (unsigned interval : {2u, 4u}) {
        unsigned progress = 3, services = 0;
        for (unsigned order = 0; order < 12; ++order) {
            if (crimeServiceOrderDue(interval, progress)) {
                ++services;
                progress = 0;
            } else ++progress;
        }
        REQUIRE(services == 12 / interval);
    }
    REQUIRE_FALSE(crimeServiceOrderDue(4, 1));
    REQUIRE(crimeServiceOrderDue(2, 1)); // Worsening crime accelerates response.
}

TEST_CASE("City turret placement preserves cross T and corner road arms", "[city][placement]") {
    const auto cross = CityPlacementPolicy::assessRoads(0,0,1,1,true,[](int x,int y){return x==0 || y==0;});
    const auto tee = CityPlacementPolicy::assessRoads(0,0,1,1,true,[](int x,int y){return y==0 || (x==0 && y<0);});
    const auto corner = CityPlacementPolicy::assessRoads(0,0,1,1,true,[](int x,int y){return (x==0 && y<=0)||(y==0 && x>=0);});
    REQUIRE(cross.preservesConnections);
    REQUIRE(tee.preservesConnections);
    REQUIRE(corner.preservesConnections);
    REQUIRE(cross.junctionBonus > tee.junctionBonus);
    REQUIRE(tee.junctionBonus > corner.junctionBonus);
    REQUIRE(corner.junctionBonus > 0);
}

TEST_CASE("City placement does not cut through a straight road", "[city][placement]") {
    REQUIRE_FALSE(CityPlacementPolicy::assessRoads(0,0,1,1,true,[](int,int y){return y==0;}).preservesConnections);
    REQUIRE_FALSE(CityPlacementPolicy::assessRoads(0,0,3,2,false,[](int,int y){return y==0;}).preservesConnections);
    REQUIRE(CityPlacementPolicy::assessRoads(0,1,3,2,false,[](int,int y){return y==0;}).preservesConnections);
    // Existing perimeter loop provides an intact local detour around the factory.
    REQUIRE(CityPlacementPolicy::assessRoads(0,0,3,2,false,[](int x,int y){
        return y==-1 || y==2 || x==-1 || x==3 || y==0;
    }).preservesConnections);
}

TEST_CASE("Rocket turrets are traffic connectors but factories and units are not", "[city][traffic]") {
    REQUIRE(DuneCity::isTrafficConnector(false, Structure_RocketTurret));
    REQUIRE(DuneCity::isTrafficConnector(true, NONE_ID));
    REQUIRE_FALSE(DuneCity::isTrafficConnector(false, Structure_HeavyFactory));
    REQUIRE_FALSE(DuneCity::isTrafficConnector(false, Unit_Tank));
}

static std::string placementSource(const std::string& file) {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(root != nullptr);
    std::ifstream input(std::string(root) + "/" + file);
    REQUIRE(input.good());
    return std::string(std::istreambuf_iterator<char>(input), {});
}

TEST_CASE("Road restoration clears damage and traffic uses the shared connector", "[city][road][regression]") {
    const auto tile = placementSource("include/Tile.h");
    const auto start = tile.find("void setRoad(bool");
    const auto block = tile.substr(start, tile.find("bool isRoadConnection", start)-start);
    REQUIRE(block.find("DestroyedStructure_None") != std::string::npos);
    REQUIRE(block.find("damage.clear()") != std::string::npos);
    REQUIRE(block.find("deadUnits.clear()") != std::string::npos);
    REQUIRE(placementSource("src/dunecity/TrafficSimulation.cpp").find("->isRoadConnection()") != std::string::npos);
}

TEST_CASE("Ordinary buildings cannot rely on diagonal-only traffic detours", "[city][placement]") {
    REQUIRE_FALSE(CityPlacementPolicy::assessRoads(0,0,1,1,false,[](int x,int y){return x==0 || y==0;}).preservesConnections);
}


TEST_CASE("Construction reservations reject overlapping footprints and permit adjacent lots", "[quantbot][placement]") {
    // A heavy factory reservation protects every covered tile against a second yard.
    REQUIRE(CityPlacementPolicy::overlaps(10,10,3,2,12,11,2,2));
    REQUIRE(CityPlacementPolicy::overlaps(12,11,2,2,10,10,3,2));
    REQUIRE(CityPlacementPolicy::overlaps(10,10,3,2,10,10,3,2));
    REQUIRE_FALSE(CityPlacementPolicy::overlaps(10,10,3,2,13,10,2,2));
    REQUIRE_FALSE(CityPlacementPolicy::overlaps(10,10,3,2,10,12,2,2));
}

TEST_CASE("Economic rebuilding avoids recent destruction but releases the site after cooldown", "[quantbot][placement]") {
    REQUIRE(CityPlacementPolicy::recentLossBlocks(10,10,2,2,10,10,2,2,30,60));
    REQUIRE(CityPlacementPolicy::recentLossBlocks(12,10,2,2,10,10,2,2,30,60));
    REQUIRE_FALSE(CityPlacementPolicy::recentLossBlocks(14,10,2,2,10,10,2,2,30,60));
    REQUIRE_FALSE(CityPlacementPolicy::recentLossBlocks(10,10,2,2,10,10,2,2,60,60));
}

TEST_CASE("Rocket turrets protect reactors before factories and useful city junctions", "[city][placement]") {
    using namespace RocketTurretPolicy;
    REQUIRE(defenseWeight(Structure_NuclearPlant) == 2 * defenseWeight(Structure_HeavyFactory));
    REQUIRE(defenseWeight(Structure_RepairYard) == defenseWeight(Structure_HeavyFactory));
    REQUIRE(defenseWeight(Structure_ZoneResidential) == 1);
    Score reactor{2, 120, 0, 1};
    Score factory{1, 240, 100, 10};
    Score city{0, 240, 1000, 0};
    REQUIRE(reactor.betterThan(factory));
    REQUIRE(factory.betterThan(city));
    REQUIRE(city.useful());
    REQUIRE_FALSE((Score{0, 0, 100, 0}).useful()); // amenities require a junction/corner
    REQUIRE_FALSE((Score{0, 240, 0, 0}).useful()); // no endless unused turrets
    REQUIRE((Score{1, 240, 0, 1}).betterThan(Score{1, 0, 0, 10}));
}

TEST_CASE("Rocket amenities use smoothed park gain rather than a radial bonus", "[city][placement]") {
    using namespace RocketTurretPolicy;
    REQUIRE(DuneCity::getPoliceCoverage(Structure_RocketTurret) == 150);
    REQUIRE(DuneCity::getParkLandValueBonus(Structure_RocketTurret) == 15);
    REQUIRE(amenityBenefit(100, false, 7) == 7);
    REQUIRE(amenityBenefit(100, false, 1) == 1);
    REQUIRE(amenityBenefit(245, false, 7) == 5);
    REQUIRE(amenityBenefit(250, false, 7) == 0);
    REQUIRE(amenityBenefit(100, true, 7) == 0);
    REQUIRE(amenityBenefit(100, false, 0) == 0);
}

TEST_CASE("Polluting factories and R/C keep a footprint-aware buffer", "[city][placement]") {
    using namespace CityPlacementPolicy;
    REQUIRE(footprintDistance(0,0,3,2,3,0,2,2) == 1);
    REQUIRE(footprintDistance(3,0,2,2,0,0,3,2) == 1);
    REQUIRE(pollutionSeparationScore(6) > pollutionSeparationScore(1));
    REQUIRE(pollutionSeparationScore(6) > pollutionSeparationScore(20));
    REQUIRE(pollutionSeparationScore(5) < 0);
    REQUIRE(residentialCommercialEnvironmentScore(0,100,0)
          > residentialCommercialEnvironmentScore(100,100,0));
    REQUIRE(residentialCommercialEnvironmentScore(0,150,0)
          > residentialCommercialEnvironmentScore(0,100,0));
    REQUIRE(residentialCommercialEnvironmentScore(0,100,8)
          > residentialCommercialEnvironmentScore(0,100,0));
    REQUIRE(residentialCommercialEnvironmentScore(0,100,0,100)
          > residentialCommercialEnvironmentScore(0,100,0,240));
    // Sand and value should not outweigh severe industrial pollution.
    REQUIRE(residentialCommercialEnvironmentScore(0,80,0)
          > residentialCommercialEnvironmentScore(200,250,12));
}

TEST_CASE("Industry prefers a clean local commute over adjacent or distant lots", "[city][placement]") {
    using namespace CityPlacementPolicy;
    REQUIRE(cityPlacementTier(true, 6) > cityPlacementTier(true, 1));
    REQUIRE(cityPlacementTier(true, 6) > cityPlacementTier(false, 20));
    REQUIRE(cityPlacementTier(true, 1) > cityPlacementTier(false, 20));
    REQUIRE(cityPlacementTier(true, 5) < cityPlacementTier(true, 6));
    REQUIRE(sensitivePlacementTier(true, 8, 40, 120)
          > sensitivePlacementTier(true, 8, 180, 240));
}

#include <players/TacticalSafetyPolicy.h>
#include <dunecity/NuclearBlastPolicy.h>
TEST_CASE("Reactor spacing clears blast geometry for neighbouring factory footprints", "[city][safety]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE_FALSE(blastClearance(0,0,3,3,6,0,3,2));
    REQUIRE(blastClearance(0,0,3,3,7,0,3,2));
    REQUIRE(blastClearance(0,0,3,3,0,7,3,2));
    REQUIRE(blastClearance(0,0,3,3,7,7,3,2));
    REQUIRE_FALSE(DuneCity::NuclearBlastPolicy::contains(5*TILESIZE,0));
    REQUIRE(blastClearance(7,0,3,2,0,0,3,3));
}
TEST_CASE("Recent loss influence fades and overlapping losses accumulate", "[city][safety]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE(lossStrength(0,300) > lossStrength(150,300));
    REQUIRE(lossStrength(299,300) > 0);
    REQUIRE(lossStrength(300,300) == 0);
    REQUIRE(lossStrength(1000,300) == 0);
    REQUIRE(lossStrength(0,0) == 0);
    REQUIRE(lossStrength(10,300)*2 > lossStrength(10,300));
}
TEST_CASE("Harvester corridor assessment spots fire between safe endpoints", "[quantbot][harvester]") {
    using namespace TacticalSafetyPolicy;
    auto danger = [](int x,int y) { return x==5 && y==5 ? 100 : 0; };
    REQUIRE(corridorDanger(0,0,10,10,danger) == 100);
    REQUIRE(corridorDanger(0,0,10,0,danger) == 0);
    REQUIRE(corridorDanger(10,10,0,0,danger) == 100);
    REQUIRE(corridorDanger(5,5,5,5,danger) == 0);
    REQUIRE(corridorDanger(5,5,10,5,danger) == 0);
}

TEST_CASE("Harvester escape permits leaving danger but rejects crossing another threat", "[tactical][harvester]") {
    using TacticalSafetyPolicy::escapeCorridor;
    REQUIRE(escapeCorridor(0,0,6,0,[](int x,int) { return x<3 ? 100:0; }));
    REQUIRE_FALSE(escapeCorridor(0,0,6,0,[](int x,int) { return x<2 || x==5 ? 100:0; }));
    REQUIRE_FALSE(escapeCorridor(0,0,6,0,[](int x,int) { return x==3 ? 100:0; }));
    REQUIRE(escapeCorridor(0,0,6,0,[](int,int) { return 0; }));
    REQUIRE_FALSE(escapeCorridor(0,0,0,0,[](int,int) { return 100; }));
}

TEST_CASE("Harvester field choice picks what the sequential corridor sweep picked", "[tactical][harvester]") {
    // The sweep it replaces (QuantBot::manageHarvesterSafety before 2026-10-21): every
    // candidate beating the running best is route-tested, the first safe one of the lowest
    // score wins. Random scores with many ties and wide ranges, random safe sets from
    // none to all; the pick must be the same and only candidates at least as good as it
    // may be tested.
    uint32_t state = 12345u;
    auto next = [&] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
    for (int round = 0; round < 3000; ++round) {
        const size_t count = next() % 400;
        const int range = 1 + static_cast<int>(next() % (round % 3 == 0 ? 8 : 3000));
        const uint32_t safeShare = next() % 101;
        std::vector<int> scores(count);
        std::vector<bool> safe(count);
        for (size_t k = 0; k < count; ++k) {
            scores[k] = static_cast<int>(next() % range);
            safe[k] = next() % 100 < safeShare;
        }
        int expected = -1, best = std::numeric_limits<int>::max();
        for (size_t k = 0; k < count; ++k) {
            if (scores[k] >= best || !safe[k]) continue;
            best = scores[k]; expected = static_cast<int>(k);
        }
        int rejected = -1, tests = 0;
        const int pick = TacticalSafetyPolicy::lowestSafeCandidate(scores, [&](int k) {
            ++tests;
            if (expected >= 0) {
                // Never a candidate worse than the pick (lower (score, position) first).
                REQUIRE(std::make_pair(scores[k], k) <= std::make_pair(scores[expected], expected));
            }
            return static_cast<bool>(safe[k]);
        }, rejected);
        REQUIRE(pick == expected);
        REQUIRE(rejected == tests - (pick >= 0 ? 1 : 0));
        if (pick < 0) REQUIRE(tests == static_cast<int>(count));
    }
}

#include <players/RedevelopmentPolicy.h>
TEST_CASE("Redevelopment compares normalized demand and displacement", "[city][placement]") {
    using RedevelopmentPolicy::displacementCost;
    REQUIRE(displacementCost(1,30,2000,2000)==displacementCost(1,30,1500,1500));
    REQUIRE(displacementCost(1,30,-500,1500)<displacementCost(1,30,1500,1500));
    REQUIRE(displacementCost(0,30,1500,1500)<displacementCost(1,30,1500,1500));
    REQUIRE(displacementCost(1,5,0,1500)<displacementCost(1,60,0,1500));
}

TEST_CASE("Four-zone preference uses four 2x2 lots and an outer road gap", "[city][placement]") {
    using CityPlacementPolicy::fourZoneGridSlot;
    REQUIRE(fourZoneGridSlot(10,10,10,10));
    REQUIRE(fourZoneGridSlot(12,10,10,10));
    REQUIRE(fourZoneGridSlot(10,12,10,10));
    REQUIRE(fourZoneGridSlot(12,12,10,10));
    REQUIRE_FALSE(fourZoneGridSlot(14,12,10,10));
    REQUIRE(fourZoneGridSlot(15,15,10,10));
}
TEST_CASE("Planned automatic perimeter roads may replace an internal road", "[city][roads]") {
    auto road=[](int x,int y) { return x==0 && y>=-1 && y<=2; };
    REQUIRE_FALSE(CityPlacementPolicy::assessRoads(0,0,2,2,false,road).preservesConnections);
    REQUIRE(CityPlacementPolicy::assessRoads(0,0,2,2,false,road,[](int,int) { return true; }).preservesConnections);
}

TEST_CASE("Road paving assessment never reads beyond map edges", "[city][placement][regression]") {
    constexpr int mapWidth = 128, mapHeight = 128;
    int reads = 0;
    auto tile = [&](int x, int y) {
        REQUIRE(x >= 0);
        REQUIRE(y >= 0);
        REQUIRE(x < mapWidth);
        REQUIRE(y < mapHeight);
        ++reads;
        return true;
    };
    // Actual crash: factory near x=93, y=0 evaluated perimeter (92,-1).
    for (const auto& origin : std::vector<std::pair<int,int>>{
            {93,0}, {0,40}, {125,40}, {40,126}, {0,0}, {125,126}}) {
        const auto impact = CityPlacementPolicy::assessRoadsOnMap(
            mapWidth,mapHeight,origin.first,origin.second,3,2,false,tile,tile);
        REQUIRE(impact.roadsCovered == 6);
    }
    // The separate turret-arm lookup also must stay bounded.
    for (const auto& origin : std::vector<std::pair<int,int>>{{0,0},{127,0},{0,127},{127,127}})
        REQUIRE(CityPlacementPolicy::assessRoadsOnMap(
            mapWidth,mapHeight,origin.first,origin.second,1,1,true,tile,tile).preservesConnections);
    REQUIRE(reads > 0);
}

TEST_CASE("Four-zone blocks reject off-map neighbours and perimeter roads", "[city][placement][regression]") {
    constexpr int width=128, height=128;
    for (int y=0; y<height-1; ++y) for (int x=0; x<width-1; ++x)
        for (int oy : {0,2}) for (int ox : {0,2}) {
            const int bx=x-ox, by=y-oy;
            if (!CityPlacementPolicy::fourZoneBlockFits(width,height,bx,by)) continue;
            for (int dy=-1; dy<=4; ++dy) for (int dx=-1; dx<=4; ++dx) {
                REQUIRE(bx+dx >= 0);
                REQUIRE(by+dy >= 0);
                REQUIRE(bx+dx < width);
                REQUIRE(by+dy < height);
            }
        }
    REQUIRE_FALSE(CityPlacementPolicy::fourZoneBlockFits(128,128,-1,20));
    REQUIRE_FALSE(CityPlacementPolicy::fourZoneBlockFits(128,128,20,0));
    REQUIRE_FALSE(CityPlacementPolicy::fourZoneBlockFits(128,128,124,20));
    REQUIRE_FALSE(CityPlacementPolicy::fourZoneBlockFits(128,128,20,124));
    REQUIRE(CityPlacementPolicy::fourZoneBlockFits(128,128,1,1));
    REQUIRE(CityPlacementPolicy::fourZoneBlockFits(128,128,123,123));
}

TEST_CASE("Police purchases value actual harm instead of repeatedly polishing safe streets", "[city][placement]") {
    using namespace CityServiceInvestmentPolicy;
    Value polish{600,0,0,0,500,100,10};
    REQUIRE_FALSE(polish.useful(false));
    REQUIRE_FALSE(polish.useful(true)); // raw crime points no longer bypass purchase value
    polish.dangerousRelief=40;
    REQUIRE(polish.useful(true));
    REQUIRE_FALSE(polish.useful(false));
    REQUIRE(crimeHarm(63,Structure_ZoneResidential,20)==0);
    REQUIRE(crimeHarm(101,Structure_ZoneResidential,20)>crimeHarm(100,Structure_ZoneResidential,20));
    REQUIRE(crimeHarm(121,Structure_ZoneCommercial,10)>crimeHarm(120,Structure_ZoneCommercial,10));
}

TEST_CASE("Harvesters avoid launcher approach range before actual weapon reach", "[tactical][harvester]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE(harvesterThreatRadius(Unit_Launcher,9)==12);
    REQUIRE(harvesterThreatRadius(Unit_Tank,4)==4);
    REQUIRE(harvesterThreatRadius(Unit_Trooper,5)==-1);
    auto danger=[](int x,int) { return std::abs(x-20)<=harvesterThreatRadius(Unit_Launcher,9) ? 100 : 0; };
    REQUIRE_FALSE(escapeCorridor(0,0,9,0,danger)); // Destination outside weapon range but too close.
    REQUIRE_FALSE(escapeCorridor(0,0,40,0,danger)); // Safe endpoints cannot justify crossing the launcher.
    REQUIRE(escapeCorridor(9,0,0,0,danger)); // Allow escape out of the warning margin.
}

TEST_CASE("Harvester evacuation prefers another field over unnecessary unloading", "[ai][harvester]") {
    using TacticalSafetyPolicy::needsRefineryRefuge;
    REQUIRE_FALSE(needsRefineryRefuge(false,false,false,true));
    REQUIRE_FALSE(needsRefineryRefuge(false,false,true,true));
    REQUIRE_FALSE(needsRefineryRefuge(true,false,false,false));
    REQUIRE(needsRefineryRefuge(false,true,true,true));
    REQUIRE(needsRefineryRefuge(true,false,true,true));
    REQUIRE(needsRefineryRefuge(false,false,true,false));
}

TEST_CASE("A spare lane can become foundation while the other lane stays connected", "[city][placement][roads]") {
    auto doubleRoad=[](int x,int y){return x>=0 && x<10 && (y==3 || y==4);};
    const auto impact=CityPlacementPolicy::assessRoads(3,2,3,2,false,doubleRoad);
    REQUIRE(impact.preservesConnections);
    REQUIRE(impact.roadsCovered==3);
    REQUIRE(impact.redundantRoadsCovered==3);
    auto singleRoad=[](int x,int y){return x>=0 && x<10 && y==3;};
    const auto blocked=CityPlacementPolicy::assessRoads(3,2,3,2,false,singleRoad);
    REQUIRE_FALSE(blocked.preservesConnections);
    REQUIRE(blocked.redundantRoadsCovered==0);
}
TEST_CASE("Mixed road foundation needs only the missing individual slabs", "[quantbot][placement][roads]") {
    using namespace QuantBotFoundationPolicy;
    // A 3x2 factory in the top-left corner whose lower row is already road.
    // Every 2x2 slab that could reach the bare row would have to pave a road
    // tile or a tile off the map, so the plan falls back to single slabs.
    auto query=[](int x,int y) {
        TileState state;
        if (x<0 || y<0 || x>9 || y>9) return state;
        state.exists=true;
        state.road=(y==1 && x<3);
        state.prepared=state.road;
        state.paveable=!state.road;
        state.inBuildRange=true;
        return state;
    };
    const auto plan=planFoundation(0,0,3,2,true,true,query);
    REQUIRE(plan.complete);
    REQUIRE(plan.orders.size()==3);
    for (const auto& order:plan.orders) {
        REQUIRE(order.item==Structure_Slab1);
        REQUIRE(order.y==0);
        REQUIRE_FALSE(query(order.x,order.y).road);
    }
}

TEST_CASE("Service placement penalises clusters and favours underserved crime", "[city][placement]") {
    using namespace CityServiceInvestmentPolicy;
    Value unserved;
    unserved.buildCost=500; unserved.upkeep=100; unserved.crime=500;
    unserved.crimeUtility=underservedUtility(1500,0);
    Value duplicate=unserved;
    duplicate.crimeUtility=underservedUtility(2500,200);
    duplicate.overlapPenalty=stationOverlapCost(500,3);
    REQUIRE(unserved.betterThan(duplicate));
    REQUIRE(unserved.useful(false));
    REQUIRE_FALSE(duplicate.useful(false));
    auto cluster=duplicate;
    cluster.overlapPenalty+=stationOverlapCost(500,6);
    REQUIRE(duplicate.betterThan(cluster)); // the second neighbour matters too
    REQUIRE(stationOverlapCost(500,12)==0);
    REQUIRE(stationOverlapCost(500,100)==0);
    REQUIRE(stationOverlapCost(500,3)>stationOverlapCost(500,6));
    REQUIRE(underservedUtility(1500,0)>underservedUtility(1500,100));
    // An exceptionally bad district can still justify overlapping stations.
    cluster.crimeUtility=underservedUtility(20000,200);
    REQUIRE(cluster.useful(false));
}

TEST_CASE("Reactor placement puts weapon distance and rear safety ahead of packing", "[city][safety]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE(reactorPlacementAllowed(Structure_NuclearPlant,false));
    REQUIRE_FALSE(reactorPlacementAllowed(Structure_HeavyFactory,false));
    // Live fire cannot win merely because the sheltered plot has loss history
    // or cannot clear every neighbouring critical building's blast radius.
    REQUIRE(reactorSiteRank(0,100,1,false,-80,-10000) > reactorSiteRank(100,0,0,true,800,10000));
    REQUIRE(reactorSiteRank(100,0,0,false,0,0) > reactorSiteRank(200,0,0,true,800,10000));
    REQUIRE(reactorSiteRank(0,0,6,false,0,-10000) > reactorSiteRank(0,0,1,true,800,10000));
    REQUIRE(reactorSiteRank(0,0,12,true,160,-10000) > reactorSiteRank(0,0,12,true,-160,10000));
    REQUIRE(reactorSiteRank(0,0,12,true,0,-10000) > reactorSiteRank(0,0,12,false,0,10000));
    REQUIRE(reactorSiteRank(0,0,6,true,0,0) > reactorSiteRank(0,100,12,true,800,10000));
    REQUIRE(reactorSiteRank(0,0,12,true,0,1) > reactorSiteRank(0,0,12,true,0,0));
}

TEST_CASE("Rocket coverage includes city districts and whole diagonal footprints", "[city][placement][air]") {
    using namespace RocketTurretPolicy;
    for(int item : {Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial,
        Structure_Refinery,Structure_WindTrap,Structure_HighTechFactory,Structure_PoliceStation,
        Structure_Silo,Structure_Palace,Structure_ConstructionYard}) CHECK(defenseWeight(item)>0);
    for(int item : {Structure_Road,Structure_Slab1,Structure_Slab4,Structure_Wall,
        Structure_RocketTurret,Structure_GunTurret}) CHECK(defenseWeight(item)==0);
    CHECK(coversBuilding(Coord(10,10),Coord(15,10),Coord(2,2),7));
    CHECK_FALSE(coversBuilding(Coord(10,10),Coord(15,15),Coord(2,2),7));
    CHECK_FALSE(coversBuilding(Coord(10,10),Coord(17,10),Coord(2,2),7));
    CHECK(coversBuilding(Coord(10,10),Coord(17,10),Coord(1,1),7));
}

TEST_CASE("Placement fallback reaches disconnected districts without rescanning the centre", "[city][placement][regression]") {
    using CityPlacementPolicy::inPlacementSearchPass;
    // A base extending along two edges has an empty averaged centre.
    REQUIRE(inPlacementSearchPass(63,63,63,63,50,0));
    REQUIRE_FALSE(inPlacementSearchPass(1,120,63,63,50,0));
    REQUIRE(inPlacementSearchPass(1,120,63,63,50,1));
    REQUIRE(inPlacementSearchPass(126,3,63,63,50,1));
    for (int y=0;y<127;++y) for (int x=0;x<127;++x) {
        const int visits=int(inPlacementSearchPass(x,y,63,63,50,0))
            + int(inPlacementSearchPass(x,y,63,63,50,1));
        REQUIRE(visits==1);
    }
}

#include <players/CityDistanceField.h>
TEST_CASE("Placement distance fields match direct footprint scans", "[ai][placement]") {
    constexpr int w=9,h=7;
    struct Rect {int x,y,w,h;};
    // Vary source count, origins, overlaps and footprints deterministically,
    // checking every candidate origin and size, including all map edges.
    for (unsigned scenario=0; scenario<80; ++scenario) {
        CityDistanceField field(w,h);
        std::vector<Rect> sources;
        unsigned random=scenario+1;
        auto next=[&] {random=random*1664525u+1013904223u;return random;};
        for (unsigned i=0; i<scenario%11; ++i) {
            const int sw=1+next()%3,sh=1+next()%3;
            Rect r{int(next()%(w-sw+1)),int(next()%(h-sh+1)),sw,sh};
            sources.push_back(r);field.add(r.x,r.y,r.w,r.h);
        }
        field.build();
        for (int ch=1;ch<=3;++ch) for (int cw=1;cw<=3;++cw)
            for (int y=0;y<=h-ch;++y) for (int x=0;x<=w-cw;++x) {
                int expected=CityDistanceField::missing;
                for (const auto& r:sources)
                    expected=std::min(expected,CityPlacementPolicy::footprintDistance(x,y,cw,ch,r.x,r.y,r.w,r.h));
                REQUIRE(field.footprint(x,y,cw,ch)==expected);
            }
    }
}
TEST_CASE("Placement fields are rebuilt after a reservation changes", "[ai][placement]") {
    CityDistanceField first(12,10);first.add(2,3,3,2);first.build();
    CityDistanceField next(12,10);next.add(9,7,2,3);next.build();
    REQUIRE(first.get(2,3)==0);
    REQUIRE(next.get(2,3)==7);
    REQUIRE(first.get(9,7)==5);
    REQUIRE(next.get(9,7)==0);
}
TEST_CASE("Placement distance fields equal the former map-sized chamfer field", "[ai][placement][regression]") {
    // The field of 1.0.849 and earlier: every map cell, two chamfer passes (exact for the
    // unit-cost eight-neighbour metric). The query-based field must answer get()
    // and footprint() identically for every cell, edge and off-map probe.
    struct Chamfer {
        int w,h; std::vector<int> cells;
        Chamfer(int width,int height):w(width),h(height),cells(width*height,CityDistanceField::missing) {}
        int get(int x,int y) const { return x<0||y<0||x>=w||y>=h ? CityDistanceField::missing : cells[y*w+x]; }
        void add(int x,int y,int rw,int rh) {
            for (int yy=std::max(0,y); yy<std::min(h,y+rh); ++yy)
                for (int xx=std::max(0,x); xx<std::min(w,x+rw); ++xx) cells[yy*w+xx]=0;
        }
        void build() {
            for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
                int& d=cells[y*w+x];
                if (x>0) d=std::min(d,get(x-1,y)+1);
                if (y>0) for (int dx=-1;dx<=1;++dx) d=std::min(d,get(x+dx,y-1)+1);
            }
            for (int y=h-1;y>=0;--y) for (int x=w-1;x>=0;--x) {
                int& d=cells[y*w+x];
                if (x+1<w) d=std::min(d,get(x+1,y)+1);
                if (y+1<h) for (int dx=-1;dx<=1;++dx) d=std::min(d,get(x+dx,y+1)+1);
            }
        }
        int footprint(int x,int y,int fw,int fh) const {
            int d=CityDistanceField::missing;
            for (int yy=y;yy<y+fh;++yy) for (int xx=x;xx<x+fw;++xx) d=std::min(d,get(xx,yy));
            return d;
        }
    };
    unsigned random=12345;
    auto next=[&] {random=random*1664525u+1013904223u;return random>>8;};
    for (unsigned scenario=0; scenario<60; ++scenario) {
        const int w=1+int(next()%97), h=1+int(next()%83);
        Chamfer reference(w,h);
        CityDistanceField field(w,h);
        // Sparse to dense, clustered or scattered, including rectangles off the map.
        const unsigned sources=scenario%7==0 ? 0u : 1u+next()%(scenario%3==0 ? 3u : 60u);
        const int clusterX=int(next()%w), clusterY=int(next()%h), spread=1+int(next()%(w+h));
        for (unsigned i=0; i<sources; ++i) {
            const int x=clusterX-spread+int(next()%(2*spread+1))-1, y=clusterY-spread+int(next()%(2*spread+1))-1;
            const int rw=1+int(next()%4), rh=1+int(next()%4);
            reference.add(x,y,rw,rh); field.add(x,y,rw,rh);
        }
        reference.build(); field.build();
        for (int y=-2; y<h+2; ++y) for (int x=-2; x<w+2; ++x) {
            REQUIRE(field.get(x,y)==reference.get(x,y));
            for (int fh=1; fh<=3; ++fh) for (int fw=1; fw<=3; ++fw)
                REQUIRE(field.footprint(x,y,fw,fh)==reference.footprint(x,y,fw,fh));
        }
    }
    // A Huge map with a few far-apart cities, as in an eight-house 384x384 game.
    Chamfer reference(384,384);
    CityDistanceField field(384,384);
    for (int i=0; i<40; ++i) {
        const int x=(i%4)*96+int(next()%20), y=(i/10)*90+int(next()%30);
        reference.add(x,y,2,2); field.add(x,y,2,2);
    }
    reference.build(); field.build();
    for (int y=0; y<384; y+=3) for (int x=0; x<384; x+=5) {
        REQUIRE(field.get(x,y)==reference.get(x,y));
        REQUIRE(field.footprint(x,y,2,2)==reference.footprint(x,y,2,2));
    }
}

TEST_CASE("Police placement removes the most occupied-building crime before tax or spacing preferences", "[city][placement]") {
    using CityServiceInvestmentPolicy::Value;
    Value centre, fringe;
    centre.crime=900;centre.dangerousRelief=150;centre.buildCost=500;centre.overlapPenalty=300;
    fringe.crime=300;fringe.dangerousRelief=100;fringe.buildCost=500;fringe.tax=1000;
    CHECK(centre.betterPoliceSiteThan(fringe));
    CHECK_FALSE(fringe.betterPoliceSiteThan(centre));
    // Equal total relief breaks toward the more dangerous neighbourhood.
    fringe.crime=900;
    CHECK(centre.betterPoliceSiteThan(fringe));
}
