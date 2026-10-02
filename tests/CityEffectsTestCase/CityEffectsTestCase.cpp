#include <dunecity/CityDemandNoticePolicy.h>
#include <DataTypes.h>
#include <dunecity/HouseColors.h>
/*
 *  CityEffectsTestCase.cpp
 *
 *  Locks the spec for the city effects pipeline:
 *  pollution emission, supply contributions, police coverage,
 *  park-bonus mappings, distance falloff, and demand thresholds.
 *
 *  All city-role structures (zones AND existing Dune buildings that map
 *  to SC-Classic roles) share a uniform `level` parameter 0..maxLevel.
 *  Level 0 = vacant: no supply, no pollution, no population.
 */

#include <catch2/catch_all.hpp>
#include <dunecity/CityEffects.h>
#include <data.h>

#include <dunecity/SpatialFields.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

using namespace DuneCity;

// --- Roles & max level -------------------------------------------------------

TEST_CASE("getStructureCityRole categorises mapped buildings", "[city-effects][role]") {
    REQUIRE(getStructureCityRole(Structure_ZoneResidential)  == CityRole::Residential);
    REQUIRE(getStructureCityRole(Structure_ZoneCommercial)   == CityRole::Commercial);
    REQUIRE(getStructureCityRole(Structure_ZoneIndustrial)   == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Silo)             == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Radar)            == CityRole::Commercial);
    REQUIRE(getStructureCityRole(Structure_HighTechFactory)  == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_IX)               == CityRole::Commercial);
    REQUIRE(getStructureCityRole(Structure_LightFactory)     == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_HeavyFactory)     == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_RepairYard)       == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Refinery)         == CityRole::Industrial);
    // Starport and Airport have specific city roles
    REQUIRE(getStructureCityRole(Structure_StarPort)         == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Airport)          == CityRole::Commercial);
    // Barracks/WOR are residential (infantry garrison = population)
    REQUIRE(getStructureCityRole(Structure_Barracks)         == CityRole::Residential);
    REQUIRE(getStructureCityRole(Structure_WOR)              == CityRole::Residential);
    REQUIRE(getStructureCityRole(Structure_WindTrap)         == CityRole::None);
    // Non-role structures
    REQUIRE(getStructureCityRole(Structure_Wall)             == CityRole::None);
}

TEST_CASE("getStructureMaxLevel matches structure tier", "[city-effects][role]") {
    REQUIRE(getStructureMaxLevel(Structure_ZoneResidential)  == 3);
    REQUIRE(getStructureMaxLevel(Structure_ZoneCommercial)   == 3);
    REQUIRE(getStructureMaxLevel(Structure_ZoneIndustrial)   == 3);
    REQUIRE(getStructureMaxLevel(Structure_Silo)             == 1);  // I-low (no pollution)
    REQUIRE(getStructureMaxLevel(Structure_Radar)            == 2);  // C-medium
    REQUIRE(getStructureMaxLevel(Structure_HighTechFactory)  == 2);  // I-medium
    REQUIRE(getStructureMaxLevel(Structure_IX)               == 3);  // C-high
    REQUIRE(getStructureMaxLevel(Structure_LightFactory)     == 1);  // I-low
    REQUIRE(getStructureMaxLevel(Structure_HeavyFactory)     == 2);  // I-medium
    REQUIRE(getStructureMaxLevel(Structure_RepairYard)       == 2);  // I-medium
    REQUIRE(getStructureMaxLevel(Structure_Refinery)         == 2);  // I-medium
    REQUIRE(getStructureMaxLevel(Structure_Barracks)         == 3);  // R-high
    REQUIRE(getStructureMaxLevel(Structure_WOR)              == 3);  // R-high
    REQUIRE(getStructureMaxLevel(Structure_WindTrap)         == 0);  // power only
}

// --- Pollution ---------------------------------------------------------------

TEST_CASE("Pollution: WindTrap and Nuclear are clean", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_WindTrap, 3)     == 0);
    REQUIRE(getPollutionEmission(Structure_NuclearPlant, 3) == 0);
}

TEST_CASE("Pollution: Starport is clean (per spec override)", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_StarPort, 3) == 0);
}

TEST_CASE("Pollution: civic / commercial / residential / defensive structures are clean", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_ConstructionYard, 3) == 0);
    REQUIRE(getPollutionEmission(Structure_Palace, 3)           == 0);
    REQUIRE(getPollutionEmission(Structure_Radar, 2)            == 0);
    REQUIRE(getPollutionEmission(Structure_Barracks, 3)         == 0);
    REQUIRE(getPollutionEmission(Structure_WOR, 3)              == 0);
    REQUIRE(getPollutionEmission(Structure_GunTurret, 3)        == 0);
    REQUIRE(getPollutionEmission(Structure_RocketTurret, 3)     == 0);
    REQUIRE(getPollutionEmission(Structure_Wall, 3)             == 0);
    REQUIRE(getPollutionEmission(Structure_IX, 3)               == 0);
    REQUIRE(getPollutionEmission(Structure_ZoneResidential, 3)  == 0);
    REQUIRE(getPollutionEmission(Structure_ZoneCommercial, 3)   == 0);
}

TEST_CASE("Pollution: Silo is clean (spice store, no smokestack)", "[city-effects][pollution]") {
    // Silo is I-low for supply but per-spec override emits no pollution.
    REQUIRE(getPollutionEmission(Structure_Silo, 1) == 0);
    REQUIRE(getPollutionEmission(Structure_Silo, 2) == 0);
}

TEST_CASE("Pollution: HighTechFactory retains aircraft manufacturing emissions", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_HighTechFactory, 1) == 10);
    REQUIRE(getPollutionEmission(Structure_HighTechFactory, 3) == 25);  // I-medium cap
}

TEST_CASE("Pollution: industrial sources scale uniformly with level", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 1) == 10);
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 2) == 25);
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 3) == 50);
    // Non-zone industrial buildings emit on the same scale:
    REQUIRE(getPollutionEmission(Structure_LightFactory, 2)   == 10);  // I-low cap
    REQUIRE(getPollutionEmission(Structure_HeavyFactory, 3)   == 25);  // I-medium cap
    REQUIRE(getPollutionEmission(Structure_RepairYard, 3)     == 25);  // I-medium cap
    // Refinery grows up to medium density.
    REQUIRE(getPollutionEmission(Structure_Refinery, 1)       == 10);
}

TEST_CASE("Pollution: vacant (level 0) emits nothing", "[city-effects][pollution]") {
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 0) == 0);
    REQUIRE(getPollutionEmission(Structure_HeavyFactory, 0)   == 0);
    REQUIRE(getPollutionEmission(Structure_Refinery, 0)       == 0);
}

// --- Supply ------------------------------------------------------------------

TEST_CASE("Commercial supply scales by level for any commercial-role structure",
          "[city-effects][supply]") {
    REQUIRE(getCommercialSupply(Structure_ZoneCommercial, 1) == 10);
    REQUIRE(getCommercialSupply(Structure_ZoneCommercial, 2) == 25);
    REQUIRE(getCommercialSupply(Structure_ZoneCommercial, 3) == 50);
    // Non-zone commercial buildings, evaluated at their respective max level:
    REQUIRE(getCommercialSupply(Structure_Radar, 2)            == 25);  // C-medium max
    REQUIRE(getCommercialSupply(Structure_IX, 3)               == 50);
    // Industrial/residential structures don't contribute commercial supply.
    REQUIRE(getCommercialSupply(Structure_HeavyFactory, 3) == 0);
    REQUIRE(getCommercialSupply(Structure_Refinery, 1)     == 0);
    REQUIRE(getCommercialSupply(Structure_Silo, 1)         == 0);       // now industrial
    REQUIRE(getCommercialSupply(Structure_HighTechFactory, 3) == 0);    // I-medium
}

TEST_CASE("Industrial supply scales by level for any industrial-role structure",
          "[city-effects][supply]") {
    REQUIRE(getIndustrialSupply(Structure_ZoneIndustrial, 1) == 10);
    REQUIRE(getIndustrialSupply(Structure_ZoneIndustrial, 2) == 25);
    REQUIRE(getIndustrialSupply(Structure_ZoneIndustrial, 3) == 50);
    REQUIRE(getIndustrialSupply(Structure_LightFactory, 2)   == 10);  // I-low cap
    REQUIRE(getIndustrialSupply(Structure_HeavyFactory, 3)   == 25);  // I-medium cap
    REQUIRE(getIndustrialSupply(Structure_RepairYard, 3)     == 25);  // I-medium cap
    REQUIRE(getIndustrialSupply(Structure_Refinery, 3)       == 25);   // capped I-medium
    REQUIRE(getIndustrialSupply(Structure_Silo, 1)           == 10);   // I-low (was C-low)
    REQUIRE(getIndustrialSupply(Structure_HighTechFactory, 3) == 25);  // I-medium cap
}

TEST_CASE("Residential supply comes from R zones and residential-role structures",
          "[city-effects][supply]") {
    REQUIRE(getResidentialSupply(Structure_ZoneResidential, 1) == 10);
    REQUIRE(getResidentialSupply(Structure_ZoneResidential, 2) == 25);
    REQUIRE(getResidentialSupply(Structure_ZoneResidential, 3) == 50);
    // Barracks/WOR are residential now
    REQUIRE(getResidentialSupply(Structure_Barracks, 3)        == 50);
    REQUIRE(getResidentialSupply(Structure_WOR, 3)             == 50);
    // Palace is residential
    REQUIRE(getResidentialSupply(Structure_Palace, 3)          == 50);
    // Non-residential structures don't provide residential supply
    REQUIRE(getResidentialSupply(Structure_Silo, 1)            == 0);
    REQUIRE(getResidentialSupply(Structure_HeavyFactory, 3)    == 0);
}

TEST_CASE("All city-role structures contribute zero supply at level 0",
          "[city-effects][supply]") {
    REQUIRE(getIndustrialSupply(Structure_Silo, 0)        == 0);
    REQUIRE(getIndustrialSupply(Structure_HighTechFactory, 0) == 0);
    REQUIRE(getIndustrialSupply(Structure_Refinery, 0)    == 0);
    REQUIRE(getIndustrialSupply(Structure_HeavyFactory, 0) == 0);
    REQUIRE(getResidentialSupply(Structure_ZoneResidential, 0) == 0);
    REQUIRE(getResidentialSupply(Structure_Barracks, 0)   == 0);
}

// --- Police coverage ---------------------------------------------------------

TEST_CASE("Police coverage: PoliceStation full, gun turrets 15%, rocket turrets 15%",
          "[city-effects][police]") {
    REQUIRE(getPoliceCoverage(Structure_PoliceStation) == 1000);
    REQUIRE(getPoliceCoverage(Structure_GunTurret)     == 150);
    REQUIRE(getPoliceCoverage(Structure_RocketTurret)  == 150);
    REQUIRE(getPoliceCoverage(Structure_Wall)          == 0);
    REQUIRE(getPoliceCoverage(Structure_HeavyFactory)  == 0);
}

TEST_CASE("Police coverage: Barracks and WOR no longer count as police (regression)",
          "[city-effects][police][regression]") {
    // Barracks and WOR are infantry-production buildings only. Adding
    // them used to grant full police coverage, which was a design bug:
    // putting a barracks down would magically turn slums luxury. Police
    // Station is now the sole full-coverage source.
    REQUIRE(getPoliceCoverage(Structure_Barracks)  == 0);
    REQUIRE(getPoliceCoverage(Structure_WOR)       == 0);
    REQUIRE(getPoliceAnnualCost(Structure_Barracks) == 0);
    REQUIRE(getPoliceAnnualCost(Structure_WOR)      == 0);
}

TEST_CASE("Police annual cost mirrors coverage; PoliceStation costs 100 (designer-tuned)",
          "[city-effects][police]") {
    // Originally matched SC's gCostOf[TOOL_POLICESTATION] = 500; reduced to 100.
    REQUIRE(getPoliceAnnualCost(Structure_PoliceStation) == 100);
    // Gun turrets remain free; rocket turrets cost 15% of station upkeep.
    REQUIRE(getPoliceAnnualCost(Structure_GunTurret) * 2 == 15);
    REQUIRE(getPoliceAnnualCost(Structure_RocketTurret)  == 15);
    REQUIRE(getPoliceAnnualCost(Structure_Wall)          == 0);
    REQUIRE(getPoliceAnnualCost(Structure_HeavyFactory)  == 0);
}

// --- Park bonus --------------------------------------------------------------

TEST_CASE("Park land-value bonus: Wall, Turrets, Palace, Stadium contribute",
          "[city-effects][park]") {
    REQUIRE(getParkLandValueBonus(Structure_Wall)         == kParkLandValueBonus);
    REQUIRE(getParkLandValueBonus(Structure_GunTurret)    == kParkLandValueBonus);
    REQUIRE(getParkLandValueBonus(Structure_RocketTurret) == kParkLandValueBonus);
    REQUIRE(getParkLandValueRadius(Structure_RocketTurret) == getParkLandValueRadius(Structure_GunTurret));
    REQUIRE(getParkLandValueBonus(Structure_Palace)       == kStadiumLandValueBonus);
    REQUIRE(getParkLandValueBonus(Structure_Stadium)      == kStadiumLandValueBonus);
    REQUIRE(getParkLandValueBonus(Structure_HeavyFactory) == 0);
}

// --- Falloff helper ----------------------------------------------------------

TEST_CASE("falloff: zero distance returns full strength", "[city-effects][falloff]") {
    REQUIRE(falloff(100, 0, 5) == 100);
}

TEST_CASE("falloff: distance >= radius returns zero", "[city-effects][falloff]") {
    REQUIRE(falloff(100, 5, 5) == 0);
    REQUIRE(falloff(100, 6, 5) == 0);
    REQUIRE(falloff(100, 99, 3) == 0);
}

TEST_CASE("falloff: linear interpolation between 0 and radius", "[city-effects][falloff]") {
    REQUIRE(falloff(100, 1, 5) == 80);  // (5-1)/5 * 100 = 80
    REQUIRE(falloff(100, 2, 5) == 60);
    REQUIRE(falloff(100, 3, 5) == 40);
    REQUIRE(falloff(100, 4, 5) == 20);
}

TEST_CASE("falloff: degenerate radius returns zero", "[city-effects][falloff]") {
    REQUIRE(falloff(100, 0, 0) == 0);
    REQUIRE(falloff(100, 0, -1) == 0);
}

// --- Population --------------------------------------------------------------

TEST_CASE("Zone population is zero when level is zero", "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 0) == 0);
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 0)  == 0);
    REQUIRE(getZonePopulation(Structure_ZoneIndustrial, 0)  == 0);
}

TEST_CASE("Non-zone city-role buildings ALSO contribute population at their level",
          "[city-effects][population]") {
    // SC Classic values: Industrial L3=4, Commercial L3=5
    REQUIRE(getZonePopulation(Structure_HeavyFactory, 3)   == 3);  // I-medium cap
    REQUIRE(getZonePopulation(Structure_HighTechFactory, 3) == 3);  // I-medium cap
    REQUIRE(getZonePopulation(Structure_IX, 3)             == 5);   // Commercial
    REQUIRE(getZonePopulation(Structure_Silo, 1)           == 1);   // Industrial L1 (was Commercial)
    REQUIRE(getZonePopulation(Structure_Radar, 2)          == 3);   // Commercial L2
    // Refinery is I-high: grows to level 3 = 4 jobs.
    REQUIRE(getZonePopulation(Structure_Refinery, 1)       == 1);
    REQUIRE(getZonePopulation(Structure_Refinery, 2)       == 3);
    REQUIRE(getZonePopulation(Structure_Refinery, 3)       == 3);
    // Barracks/WOR are residential high
    REQUIRE(getZonePopulation(Structure_Barracks, 1)       == 16);
    REQUIRE(getZonePopulation(Structure_Barracks, 3)       == 40);
    REQUIRE(getZonePopulation(Structure_WOR, 3)            == 40);
    // Vacant — contributes nothing
    REQUIRE(getZonePopulation(Structure_Refinery, 0)       == 0);
    REQUIRE(getZonePopulation(Structure_WindTrap, 0)       == 0);
    REQUIRE(getZonePopulation(Structure_WindTrap, 1)       == 0);
    REQUIRE(getZonePopulation(Structure_WindTrap, 2)       == 0);
}

TEST_CASE("Residential zones contribute people, scaled with level (SC Classic values)",
          "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 1) == 16);
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 2) == 24);
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 3) == 40);
}

TEST_CASE("Commercial and industrial zones contribute jobs (SC Classic values)",
          "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 1) == 1);
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 2) == 3);
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 3) == 5);
    REQUIRE(getZonePopulation(Structure_ZoneIndustrial, 1) == 1);
    REQUIRE(getZonePopulation(Structure_ZoneIndustrial, 2) == 3);
    REQUIRE(getZonePopulation(Structure_ZoneIndustrial, 3) == 4);
}

TEST_CASE("Population at level >3 clamps to level-3 value",
          "[city-effects][population]") {
    REQUIRE(getZonePopulation(Structure_ZoneResidential, 5)  == 40);
    REQUIRE(getZonePopulation(Structure_ZoneCommercial, 99)  == 5);
}

// --- Tax ---------------------------------------------------------------------

TEST_CASE("Annual tax is zero for empty city or zero rate",
          "[city-effects][tax]") {
    REQUIRE(computeAnnualTaxRevenue(0, 7)    == 0);
    REQUIRE(computeAnnualTaxRevenue(100, 0)  == 0);
    REQUIRE(computeAnnualTaxRevenue(-5, 7)   == 0);
    REQUIRE(computeAnnualTaxRevenue(100, -3) == 0);
}

TEST_CASE("Annual tax uses Micropolis easy weighting and rate", "[city-effects][tax]") {
    // Values are tax population in eighths; default land value is128.
    CHECK(computeAnnualTaxRevenue(100,7) == 130);
    CHECK(computeAnnualTaxRevenue(200,7) == 261);
    CHECK(computeAnnualTaxRevenue(100,14) == 261);
    CHECK(computeAnnualTaxRevenue(50,20) == 186);
    CHECK(computeAnnualTaxRevenue(100,7,0) == 0);
    CHECK(computeAnnualTaxRevenue(100,7,64) == 65);
    CHECK(computeAnnualTaxRevenue(100,7,250) == 255);
}

TEST_CASE("Palace contributes both R and C at every occupied tier", "[city-effects][tax]") {
    CHECK(isTaxableCityStructure(Structure_Palace));
    for (int level=0;level<=3;++level) {
        const int residential=getZonePopulation(Structure_ZoneResidential,level);
        const int commercial=getZonePopulation(Structure_ZoneCommercial,level);
        const int base=taxablePopulationEighths(Structure_Palace,residential,level);
        CHECK(base == residential+8*commercial);
    }
    // Aggregate100 buildings to verify rates without per-building truncation.
    CHECK(computeAnnualTaxRevenue(100*taxablePopulationEighths(Structure_Palace,40,3),7,128) == 10453);
    CHECK(computeAnnualTaxRevenue(100*taxablePopulationEighths(Structure_ZoneResidential,40,3),7,128) == 10453);
    CHECK(computeAnnualTaxRevenue(100*taxablePopulationEighths(Structure_ZoneCommercial,5,3),7,128) == 10453);
    CHECK(computeAnnualTaxRevenue(100*taxablePopulationEighths(Structure_ZoneIndustrial,4,3),7,128) == 8362);
}

// --- Zone score / growth-decline gating --------------------------------------

TEST_CASE("computeLocalEval: residential scales with land value minus pollution",
          "[city-effects][zscore]") {
    // evalRes formula: clamp((lv-poll)*32, 0, 6000) → -3000 → /4
    // Good neighborhood: (200-0)*32=6400→6000; (6000-3000)/4 = 750
    CHECK(computeLocalEval(CityRole::Residential, 200, 0, true) == 750);
    // With pollution: (200-100)*32=3200; (3200-3000)/4 = 50
    CHECK(computeLocalEval(CityRole::Residential, 200, 100, true) == 50);
    // No road penalty: 750 - 300 (NoRoad) = 450
    CHECK(computeLocalEval(CityRole::Residential, 200, 0, false) == 450);
    // Bad: (20-100)*32=-2560→0; (0-3000)/4=-750; NoRoad -300 = -1050
    CHECK(computeLocalEval(CityRole::Residential, 20, 100, false) == -1050);
}

TEST_CASE("computeLocalEval: commercial uses land value, crime, pollution",
          "[city-effects][zscore]") {
    CHECK(computeLocalEval(CityRole::Commercial, 200, 0, true)  == 400);
    // pollution > 50 → -50 penalty: 400 - 50 = 350
    CHECK(computeLocalEval(CityRole::Commercial, 200, 100, true) == 350);
    CHECK(computeLocalEval(CityRole::Commercial, 0, 0, true)    == 0);
}

TEST_CASE("computeLocalEval: industrial dominated by traffic",
          "[city-effects][zscore]") {
    CHECK(computeLocalEval(CityRole::Industrial, 250, 0, true) == 0);
    // No road → -200 penalty
    CHECK(computeLocalEval(CityRole::Industrial, 0, 250, false) == -200);
}

TEST_CASE("computeZscore: unpowered always returns -500", "[city-effects][zscore]") {
    CHECK(computeZscore(2000, 1000, false) == -500);
    CHECK(computeZscore(-2000, -1000, false) == -500);
}

TEST_CASE("computeZscore: powered adds valve and local", "[city-effects][zscore]") {
    CHECK(computeZscore(-2000, 1000, true) == -1000);
    CHECK(computeZscore(1000, 640, true) == 1640);
    CHECK(computeZscore(0, 0, true) == 0);
}

TEST_CASE("R=-2000 always blocks residential growth via zscore",
          "[city-effects][zscore][regression]") {
    // Even in the best possible neighborhood (lv=250, poll=0, road=true),
    // R=-2000 must produce a zscore below kZscoreGrowthGate (-350).
    const int localEval = computeLocalEval(CityRole::Residential, 250, 0, true);
    const int zscore = computeZscore(-2000, localEval, true);
    CHECK(zscore < kZscoreGrowthGate);
}

TEST_CASE("shouldZoneDecline: no decline at level 0", "[city-effects][decline]") {
    CHECK_FALSE(shouldZoneDecline(-2000, 0, 0));
}

TEST_CASE("shouldZoneDecline: no decline above threshold", "[city-effects][decline]") {
    CHECK_FALSE(shouldZoneDecline(500, 3, 0));
    CHECK_FALSE(shouldZoneDecline(350, 3, 0));
}

TEST_CASE("shouldZoneDecline: strong negative triggers decline at L3",
          "[city-effects][decline]") {
    // zscore <= -1000: 25% chance (roll < 4)
    CHECK(shouldZoneDecline(-1000, 3, 0));
    CHECK(shouldZoneDecline(-1000, 3, 3));
    CHECK_FALSE(shouldZoneDecline(-1000, 3, 4));
}

TEST_CASE("shouldZoneDecline: moderate negative triggers at lower rate",
          "[city-effects][decline]") {
    // zscore <= -500: 12.5% (roll < 2)
    CHECK(shouldZoneDecline(-500, 3, 0));
    CHECK(shouldZoneDecline(-500, 3, 1));
    CHECK_FALSE(shouldZoneDecline(-500, 3, 2));
}

TEST_CASE("shouldZoneDecline: mild negative triggers at lowest rate",
          "[city-effects][decline]") {
    // zscore < 0: 6.25% (roll == 0)
    CHECK(shouldZoneDecline(-1, 2, 0));
    CHECK_FALSE(shouldZoneDecline(-1, 2, 1));
}

TEST_CASE("shouldZoneDecline: L1 protected unless extreme",
          "[city-effects][decline]") {
    // zscore -1000, level 1: protected (zscore > -1500)
    CHECK_FALSE(shouldZoneDecline(-1000, 1, 0));
    // zscore -1500, level 1: extreme enough to decline
    CHECK(shouldZoneDecline(-1500, 1, 0));
    // zscore -1600, level 1: extreme, roll < 4 (25%)
    CHECK(shouldZoneDecline(-1600, 1, 3));
    CHECK_FALSE(shouldZoneDecline(-1600, 1, 4));
}

TEST_CASE("shouldZoneDecline: buffer zone between 0 and kZscoreDeclineGate",
          "[city-effects][decline]") {
    // zscore in [0, 350): no decline even at roll 0
    CHECK_FALSE(shouldZoneDecline(0, 3, 0));
    CHECK_FALSE(shouldZoneDecline(200, 3, 0));
    CHECK_FALSE(shouldZoneDecline(349, 3, 0));
}

TEST_CASE("Demand thresholds increase with level", "[city-effects][demand]") {
    REQUIRE(getDemandResidentialThreshold(1) <  getDemandResidentialThreshold(2));
    REQUIRE(getDemandResidentialThreshold(2) <  getDemandResidentialThreshold(3));
    REQUIRE(getDemandJobsThreshold(1)        <  getDemandJobsThreshold(2));
    REQUIRE(getDemandJobsThreshold(2)        <  getDemandJobsThreshold(3));
    // Land-value floors are zeroed across every level: density is gated by
    // local supply only, while value tier handles "rich vs poor" visuals.
    REQUIRE(getDemandLandValueFloor(1)       == 0);
    REQUIRE(getDemandLandValueFloor(2)       == 0);
    REQUIRE(getDemandLandValueFloor(3)       == 0);
}

// --- Traffic-aware local evaluation -----------------------------------------

TEST_CASE("computeLocalEval (traffic): residential penalised by NoDestination",
          "[city-effects][zscore][traffic]") {
    // Connected traffic, good area
    int connected = computeLocalEval(CityRole::Residential, 200, 0, 0,
                                     TrafficResult::Connected);
    // NoDestination: large penalty
    int noDest = computeLocalEval(CityRole::Residential, 200, 0, 0,
                                  TrafficResult::NoDestination);
    // NoRoad: moderate penalty
    int noRoad = computeLocalEval(CityRole::Residential, 200, 0, 0,
                                  TrafficResult::NoRoad);
    CHECK(connected > noDest);
    CHECK(noDest >= noRoad - 300);  // NoRoad has -300, NoDest has -600
    CHECK(noRoad < connected);
}

TEST_CASE("computeLocalEval (traffic): commercial penalised by traffic failure",
          "[city-effects][zscore][traffic]") {
    int ok = computeLocalEval(CityRole::Commercial, 200, 0, 0,
                              TrafficResult::Connected);
    int noDest = computeLocalEval(CityRole::Commercial, 200, 0, 0,
                                  TrafficResult::NoDestination);
    int noRoad = computeLocalEval(CityRole::Commercial, 200, 0, 0,
                                  TrafficResult::NoRoad);
    CHECK(ok > noDest);
    CHECK(ok > noRoad);
}

TEST_CASE("computeLocalEval (traffic): industrial penalised by traffic failure",
          "[city-effects][zscore][traffic]") {
    int ok = computeLocalEval(CityRole::Industrial, 0, 0, 0,
                              TrafficResult::Connected);
    int noRoad = computeLocalEval(CityRole::Industrial, 0, 0, 0,
                                  TrafficResult::NoRoad);
    CHECK(ok == 0);
    CHECK(noRoad < 0);
}

TEST_CASE("computeLocalEval (traffic): residential crime penalty",
          "[city-effects][zscore][traffic]") {
    int lowCrime = computeLocalEval(CityRole::Residential, 200, 0, 50,
                                    TrafficResult::Connected);
    int highCrime = computeLocalEval(CityRole::Residential, 200, 0, 200,
                                     TrafficResult::Connected);
    CHECK(lowCrime > highCrime);
}

TEST_CASE("computeLocalEval (traffic): commercial crime penalty",
          "[city-effects][zscore][traffic]") {
    int lowCrime = computeLocalEval(CityRole::Commercial, 200, 0, 50,
                                    TrafficResult::Connected);
    int highCrime = computeLocalEval(CityRole::Commercial, 200, 0, 200,
                                     TrafficResult::Connected);
    CHECK(lowCrime > highCrime);
}

// --- Micropolis-shaped demand valves ----------------------------------------

TEST_CASE("computeDemandValves: empty city produces positive R demand",
          "[city-effects][valves]") {
    // With no residents and no jobs, residential demand should be positive
    // (people want to move in). SC default resRatio = 1.3 when resPop is 0.
    ValveInputs vi;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    CHECK(vo.resValve >= 0);
}

TEST_CASE("computeDemandValves: empty industrial demand retains its startup safeguard",
          "[city-effects][valves][regression]") {
    ValveInputs vi;
    vi.taxRate = 7;

    for (int tick = 0; tick < 8; ++tick) {
        const auto vo = computeDemandValves(vi);
        CHECK(vo.indValve >= 0);
        vi.resValve = vo.resValve;
        vi.comValve = vo.comValve;
        vi.indValve = vo.indValve;
    }
}

TEST_CASE("Commercial demand starts before the first shop opens",
          "[city-effects][valves][regression]") {
    ValveInputs in;
    // An undersized market reduces demand, as in Micropolis; commercial
    // population need not exist for either negative or positive demand.
    CHECK(computeDemandValves(in).comValve < 0);
    // Harkonnen's captured opening had ten industrial residents and no
    // commercial residents, yet C remained zero on every scan.
    in.indPop = in.prevIndPop = 10;
    CHECK(computeDemandValves(in).comValve > 0);
    in = ValveInputs{};
    in.resPop = in.prevResPop = 40;
    const auto first = computeDemandValves(in);
    REQUIRE(first.comValve > 0);
    in.comValve = first.comValve;
    CHECK(computeDemandValves(in).comValve > first.comValve);

    // Current opening: residents plus industrial jobs, no shops yet.
    in.indPop = in.prevIndPop = 5;
    in.comValve = 0;
    CHECK(computeDemandValves(in).comValve > 0);

    // The normal tax penalty still applies to the startup market.
    in.taxRate = 20;
    CHECK(computeDemandValves(in).comValve <= 0);
}

TEST_CASE("Empty commercial population uses the raw Micropolis market projection",
          "[city-effects][valves][regression]") {
    ValveInputs in;
    // Two worker-equivalents / 3.7 market divisor, with default labour 1:
    // (2 / 3.7 - 1) * 600 truncates to -275 at neutral 7% tax.
    in.resPop = in.prevResPop = 16;
    CHECK(computeDemandValves(in).comValve == -275);
    in.comValve = -275;
    CHECK(computeDemandValves(in).comValve == -550);
    in.comValve = -1400;
    CHECK(computeDemandValves(in).comValve == -kComValveRange);

    // The same raw projection recovers demand as the market grows.
    in.resPop = in.prevResPop = 40;
    in.comValve = 0;
    CHECK(computeDemandValves(in).comValve == 210);
    in.resPop = in.prevResPop = 80;
    CHECK(computeDemandValves(in).comValve == 600); // ratio capped at 2
}

TEST_CASE("computeDemandValves: jobs-only loaded history does not collapse labor demand",
          "[city-effects][valves][regression]") {
    ValveInputs vi;
    vi.comPop = 16;
    vi.indPop = 16;
    vi.prevComPop = 16;
    vi.prevIndPop = 16;
    vi.prevResPop = 0;
    vi.taxRate = 7;

    const auto vo = computeDemandValves(vi);
    CHECK(vo.comValve > -600);
    CHECK(vo.indValve > 0);
}

TEST_CASE("recoverLoadedEmptyPopulationValve repairs poisoned saves only when population is empty",
          "[city-effects][valves][save-compat][regression]") {
    CHECK(recoverLoadedEmptyPopulationValve(0, -kResValveRange) == 0);
    CHECK(recoverLoadedEmptyPopulationValve(0, -900) == 0);
    CHECK(recoverLoadedEmptyPopulationValve(0, 250) == 250);
    CHECK(recoverLoadedEmptyPopulationValve(16, -900) == -900);
}

TEST_CASE("computeDemandValves: excess residents produce negative R delta",
          "[city-effects][valves]") {
    // Many residents, no jobs: R delta should push valve negative.
    ValveInputs vi;
    vi.resPop = 500;
    vi.prevResPop = 500;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    CHECK(vo.resValve < 0);
}

TEST_CASE("computeDemandValves: balanced city has moderate demand",
          "[city-effects][valves]") {
    ValveInputs vi;
    vi.resPop = 100;
    vi.comPop = 50;
    vi.indPop = 50;
    vi.prevResPop = 100;
    vi.prevComPop = 50;
    vi.prevIndPop = 50;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    CHECK(vo.resValve > -kResValveRange);
    CHECK(vo.resValve <  kResValveRange);
    CHECK(vo.comValve > -kComValveRange);
    CHECK(vo.comValve <  kComValveRange);
    CHECK(vo.indValve > -kIndValveRange);
    CHECK(vo.indValve <  kIndValveRange);
}

TEST_CASE("computeDemandValves: high tax suppresses all demand",
          "[city-effects][valves]") {
    ValveInputs base;
    base.resPop = 100;
    base.comPop = 50;
    base.indPop = 50;
    base.prevResPop = 100;
    base.prevComPop = 50;
    base.prevIndPop = 50;
    base.taxRate = 7;
    const auto low = computeDemandValves(base);

    base.taxRate = 20;  // maximum tax
    const auto high = computeDemandValves(base);

    CHECK(high.resValve < low.resValve);
    CHECK(high.comValve < low.comValve);
    CHECK(high.indValve < low.indValve);
}

TEST_CASE("computeDemandValves: civic caps limit positive demand (SC thresholds)",
          "[city-effects][valves][civic-caps]") {
    // SC thresholds: resPop>500, comPop>100, indPop>70
    ValveInputs vi;
    vi.resPop = 600;  // above SC threshold of 500
    vi.comPop = 200;  // above SC threshold of 100
    vi.indPop = 100;  // above SC threshold of 70
    vi.prevResPop = 600;
    vi.prevComPop = 200;
    vi.prevIndPop = 100;
    vi.taxRate = 5;

    // Without civics: positive demand capped to 0
    const auto noCivic = computeDemandValves(vi);

    // With civics: demand uncapped
    vi.hasStadium  = true;
    vi.hasPalace   = true;
    vi.hasAirport  = true;
    vi.hasStarport = true;
    const auto withCivic = computeDemandValves(vi);

    CHECK(noCivic.resValve <= withCivic.resValve);
    CHECK(noCivic.comValve <= withCivic.comValve);
    CHECK(noCivic.indValve <= withCivic.indValve);
}

TEST_CASE("computeDemandValves: valve accumulates over multiple ticks",
          "[city-effects][valves][accumulation]") {
    // Run two ticks — valve should accumulate
    ValveInputs vi;
    vi.resPop = 100;
    vi.prevResPop = 100;
    vi.comPop = 50;
    vi.prevComPop = 50;
    vi.indPop = 50;
    vi.prevIndPop = 50;
    vi.taxRate = 7;

    const auto tick1 = computeDemandValves(vi);
    // Feed tick1 output back as input
    vi.resValve = tick1.resValve;
    vi.comValve = tick1.comValve;
    vi.indValve = tick1.indValve;
    const auto tick2 = computeDemandValves(vi);

    // With same population, tick2 should have accumulated further
    // (same direction as tick1 if delta is consistently positive/negative)
    if (tick1.resValve > 0) {
        CHECK(tick2.resValve >= tick1.resValve);
    } else if (tick1.resValve < 0) {
        CHECK(tick2.resValve <= tick1.resValve);
    }
}

TEST_CASE("computeDemandValves: R valve negative when residents far exceed jobs",
          "[city-effects][valves][regression]") {
    // The core invariant: if residents >> jobs, R delta is negative.
    ValveInputs vi;
    vi.resPop = 800;
    vi.prevResPop = 800;
    vi.comPop = 10;
    vi.prevComPop = 10;
    vi.indPop = 10;
    vi.prevIndPop = 10;
    vi.taxRate = 7;
    const auto vo = computeDemandValves(vi);
    CHECK(vo.resValve < 0);
}

// --- Tax table ---------------------------------------------------------------

TEST_CASE("getTaxTableEntry: low tax is positive, high tax is strongly negative",
          "[city-effects][valves]") {
    CHECK(getTaxTableEntry(0) == 200);
    CHECK(getTaxTableEntry(7) == 0);
    CHECK(getTaxTableEntry(20) == -600);
}

// --- Palace role and population ----------------------------------------------

TEST_CASE("Palace hosts one residential and one commercial zone",
          "[city-effects][role][palace]") {
    REQUIRE(getStructureCityRole(Structure_Palace) == CityRole::Residential);
    REQUIRE(getStructureMaxLevel(Structure_Palace) == 3);
    // Palace residential portion = one zone: 16/24/40
    REQUIRE(getZonePopulation(Structure_Palace, 1) == 16);
    REQUIRE(getZonePopulation(Structure_Palace, 2) == 24);
    REQUIRE(getZonePopulation(Structure_Palace, 3) == 40);
    // Palace commercial portion = one zone: 1/3/5
    REQUIRE(getPalaceCommercialPopulation(1) == 1);
    REQUIRE(getPalaceCommercialPopulation(2) == 3);
    REQUIRE(getPalaceCommercialPopulation(3) == 5);
    REQUIRE(getPalaceCommercialPopulation(0) == 0);
}

// --- Starport role -----------------------------------------------------------

TEST_CASE("Starport and Airport have correct city roles",
          "[city-effects][role]") {
    REQUIRE(getStructureCityRole(Structure_StarPort) == CityRole::Industrial);
    REQUIRE(getStructureCityRole(Structure_Airport)  == CityRole::Commercial);
    REQUIRE(getStructureMaxLevel(Structure_StarPort) == 3);
    REQUIRE(getStructureMaxLevel(Structure_Airport)  == 3);
}

// --- Traffic pollution -------------------------------------------------------

TEST_CASE("getTrafficPollution: no pollution below light threshold",
          "[city-effects][traffic-pollution]") {
    CHECK(getTrafficPollution(0)  == 0);
    CHECK(getTrafficPollution(63) == 0);
}

TEST_CASE("getTrafficPollution: light traffic contributes small pollution",
          "[city-effects][traffic-pollution]") {
    CHECK(getTrafficPollution(64)  == kTrafficPollutionLight);
    CHECK(getTrafficPollution(100) == kTrafficPollutionLight);
    CHECK(getTrafficPollution(149) == kTrafficPollutionLight);
}

TEST_CASE("getTrafficPollution: heavy traffic contributes more pollution",
          "[city-effects][traffic-pollution]") {
    CHECK(getTrafficPollution(150) == kTrafficPollutionHeavy);
    CHECK(getTrafficPollution(240) == kTrafficPollutionHeavy);
}

// --- Commercial desirability (computeCommercialRate) -------------------------

TEST_CASE("computeCommercialRate: more nearby supply increases score",
          "[city-effects][commercial-rate]") {
    int noSupply = computeCommercialRate(0, 0, false);
    int withRes  = computeCommercialRate(100, 0, false);
    int withBoth = computeCommercialRate(100, 100, false);
    CHECK(withRes > noSupply);
    CHECK(withBoth > withRes);
}

TEST_CASE("computeCommercialRate: airport provides flat bonus",
          "[city-effects][commercial-rate]") {
    int noAirport   = computeCommercialRate(100, 50, false);
    int withAirport  = computeCommercialRate(100, 50, true);
    CHECK(withAirport == noAirport + 100);
}

TEST_CASE("computeCommercialRate: residential supply capped at 200",
          "[city-effects][commercial-rate]") {
    int at200  = computeCommercialRate(200, 0, false);
    int at500  = computeCommercialRate(500, 0, false);
    CHECK(at200 == at500);  // capped
}

// --- Regression scenario tests (spec Slice 6) --------------------------------

TEST_CASE("Regression: R=-2000 with C/I positive — residential must shrink",
          "[city-effects][regression][scenario]") {
    // With R=-2000, even best local eval, zscore must be below growth gate.
    const int bestEval = computeLocalEval(CityRole::Residential, kMaxLandValue, 0, 0,
                                          TrafficResult::Connected);
    const int zscore = computeZscore(-2000, bestEval, true);
    CHECK(zscore < kZscoreGrowthGate);
    // And it should be negative enough to decline.
    CHECK(shouldZoneDecline(zscore, 3, 0));
}

TEST_CASE("Regression: connected residential + jobs + low pollution can grow",
          "[city-effects][regression][scenario]") {
    // Positive valve, good area, powered, connected → should be above growth gate.
    const int goodEval = computeLocalEval(CityRole::Residential, 150, 10, 20,
                                           TrafficResult::Connected);
    const int zscore = computeZscore(500, goodEval, true);
    CHECK(zscore > kZscoreGrowthGate);
}

TEST_CASE("Regression: high pollution > 128 blocks residential immigration",
          "[city-effects][regression][scenario]") {
    // Pollution >= kPollutionGrowthBlock (160) blocks R/C growth.
    CHECK(isPollutionBlockingGrowth(160, CityRole::Residential, 2));
    CHECK(isPollutionBlockingGrowth(200, CityRole::Residential, 1));
    // But industrial is immune.
    CHECK_FALSE(isPollutionBlockingGrowth(200, CityRole::Industrial, 3));
}

TEST_CASE("Regression: high crime tanks land value and growth stalls",
          "[city-effects][regression][scenario]") {
    // Crime > 150 causes -200 penalty on residential eval.
    int lowCrimeEval = computeLocalEval(CityRole::Residential, 150, 0, 50,
                                         TrafficResult::Connected);
    int highCrimeEval = computeLocalEval(CityRole::Residential, 150, 0, 200,
                                          TrafficResult::Connected);
    CHECK(highCrimeEval < lowCrimeEval - 100);
}

TEST_CASE("Regression: Airport raises commercial demand/cap",
          "[city-effects][regression][scenario]") {
    ValveInputs vi;
    vi.resPop = 300;  vi.prevResPop = 300;
    vi.comPop = 200;  vi.prevComPop = 200;  // above SC threshold 100
    vi.indPop = 100;  vi.prevIndPop = 100;
    vi.taxRate = 5;

    vi.hasAirport = false;
    const auto noAirport = computeDemandValves(vi);
    vi.hasAirport = true;
    const auto withAirport = computeDemandValves(vi);

    CHECK(withAirport.comValve >= noAirport.comValve);
}

TEST_CASE("Regression: Starport raises industrial demand/cap",
          "[city-effects][regression][scenario]") {
    ValveInputs vi;
    vi.resPop = 300;  vi.prevResPop = 300;
    vi.comPop = 100;  vi.prevComPop = 100;
    vi.indPop = 200;  vi.prevIndPop = 200;  // above SC threshold 70
    vi.taxRate = 5;

    vi.hasStarport = false;
    const auto noStarport = computeDemandValves(vi);
    vi.hasStarport = true;
    const auto withStarport = computeDemandValves(vi);

    CHECK(withStarport.indValve >= noStarport.indValve);
}

TEST_CASE("Regression: Only a stadium raises the residential cap",
          "[city-effects][regression][scenario]") {
    ValveInputs vi;
    vi.resPop = 600;  vi.prevResPop = 600;  // above SC threshold 500
    vi.comPop = 200;  vi.prevComPop = 200;
    vi.indPop = 200;  vi.prevIndPop = 200;
    vi.taxRate = 5;

    vi.hasStadium = false;
    vi.hasPalace = false;
    const auto noCivic = computeDemandValves(vi);
    vi.hasPalace = true;
    const auto palaceOnly = computeDemandValves(vi);
    CHECK(palaceOnly.resValve == noCivic.resValve);
    CHECK(missingDemandCivics(vi) & NeedStadium);
    vi.hasStadium = true;
    const auto withCivic = computeDemandValves(vi);
    CHECK_FALSE(missingDemandCivics(vi) & NeedStadium);

    CHECK(withCivic.resValve >= noCivic.resValve);
}

TEST_CASE("Regression: Palace provides dual R+C population per spec",
          "[city-effects][regression][scenario]") {
    // Palace is residential-role with one R and one C contribution.
    REQUIRE(getStructureCityRole(Structure_Palace) == CityRole::Residential);
    REQUIRE(getZonePopulation(Structure_Palace, 1) == 16);   // One R L1
    REQUIRE(getZonePopulation(Structure_Palace, 3) == 40);   // One R L3
    REQUIRE(getPalaceCommercialPopulation(3) == 5);          // One C L3
    // Palace provides a stadium-level land-value bonus.
    REQUIRE(getParkLandValueBonus(Structure_Palace) == kStadiumLandValueBonus);
}

// --- Unemployment and hospital/church need -----------------------------------

TEST_CASE("Unemployment: zero when jobs meet or exceed labor force",
          "[city-effects][unemployment]") {
    CHECK(computeUnemploymentRate(0, 0, 0) == 0);
    CHECK(computeUnemploymentRate(80, 10, 10) == 0);  // 10 jobs, 10 normRes → full employment
}

TEST_CASE("Unemployment: positive when labor force exceeds jobs",
          "[city-effects][unemployment]") {
    // resPop=160, normRes=20, jobs(C+I)=5 → employment=5/20=0.25 → unemployment=75%
    CHECK(computeUnemploymentRate(160, 3, 2) == 75);
}

TEST_CASE("Hospital count: auto-created 1 per 256 res pop",
          "[city-effects][hospital]") {
    CHECK(computeHospitalCount(0)   == 0);
    CHECK(computeHospitalCount(255) == 0);
    CHECK(computeHospitalCount(256) == 1);
    CHECK(computeHospitalCount(512) == 2);
}

TEST_CASE("Church count: auto-created 1 per 256 res pop",
          "[city-effects][church]") {
    CHECK(computeChurchCount(0)   == 0);
    CHECK(computeChurchCount(256) == 1);
    CHECK(computeChurchCount(512) == 2);
}

TEST_CASE("Windtraps supply power without city jobs even with old occupancy", "[city-effects][windtrap]") {
    REQUIRE(getStructureMaxLevel(Structure_WindTrap) == 0);
    for (int level = 0; level <= 3; ++level) {
        REQUIRE(getZonePopulation(Structure_WindTrap, level) == 0);
        REQUIRE(getIndustrialSupply(Structure_WindTrap, level) == 0);
        REQUIRE(getPollutionEmission(Structure_WindTrap, level) == 0);
        REQUIRE(getCommercialSupply(Structure_WindTrap, level) == 0);
        REQUIRE(getResidentialSupply(Structure_WindTrap, level) == 0);
    }
}

TEST_CASE("Government buildings retain roles without paying tax", "[city-effects][tax]") {
    for (const int item : {Structure_ConstructionYard, Structure_WindTrap,
            Structure_LightFactory, Structure_Refinery, Structure_Silo,
            Structure_HeavyFactory, Structure_HighTechFactory, Structure_RepairYard,
            Structure_StarPort, Structure_Airport, Structure_Barracks,
            Structure_WOR, Structure_Radar, Structure_IX, Structure_TechCenter,
            Structure_PoliceStation, Structure_RocketTurret, Structure_GunTurret}) {
        CAPTURE(item);
        CHECK_FALSE(isTaxableCityStructure(item));
        CHECK(taxablePopulationEighths(item, 40, 3) == 0);
    }
    CHECK(getZonePopulation(Structure_HeavyFactory, 3) == 3);  // I-medium cap
    CHECK(getZonePopulation(Structure_HighTechFactory, 3) == 3);  // I-medium cap
    CHECK(getZonePopulation(Structure_Barracks, 3) == 40);
    CHECK(taxablePopulationEighths(Structure_ZoneResidential, 2, 3) == 4); // one house
    CHECK(taxablePopulationEighths(Structure_ZoneResidential, 40, 3) == 80);
    CHECK(taxablePopulationEighths(Structure_ZoneCommercial, 5, 3) == 80);
    CHECK(taxablePopulationEighths(Structure_ZoneIndustrial, 4, 3) == 64);
    CHECK(taxablePopulationEighths(Structure_ZoneResidential, 0, 3) == 0);
}

TEST_CASE("Reduced infrastructure tiers cap loaded jobs and emissions", "[city-effects][role]") {
    CHECK(effectiveCityLevel(Structure_Refinery, 3) == 2);
    CHECK(getIndustrialSupply(Structure_Refinery, 3) == 25);
    CHECK(getZonePopulation(Structure_Refinery, 3) == 3);
    CHECK(getPollutionEmission(Structure_Refinery, 3) == 25);
    CHECK(effectiveCityLevel(Structure_Silo, 3) == 1);
    CHECK(getIndustrialSupply(Structure_Silo, 3) == 10);
    CHECK(getZonePopulation(Structure_Silo, 3) == 1);
    CHECK(getPollutionEmission(Structure_Silo, 3) == 0);
}

TEST_CASE("Rocket coverage and upkeep are fifteen percent of a police station", "[city-effects][police]") {
    REQUIRE(getPoliceCoverage(Structure_RocketTurret) * 100 == getPoliceCoverage(Structure_PoliceStation) * 15);
    REQUIRE(getPoliceAnnualCost(Structure_RocketTurret) * 100 == getPoliceAnnualCost(Structure_PoliceStation) * 15);
}

TEST_CASE("Palace population and local supply match one R and C zone through level three", "[city-effects][palace]") {
    for (int level = 0; level <= 4; ++level) {
        REQUIRE(getZonePopulation(Structure_Palace, level) == getZonePopulation(Structure_ZoneResidential, level));
        REQUIRE(getPalaceCommercialPopulation(level) == getZonePopulation(Structure_ZoneCommercial, level));
        REQUIRE(getResidentialSupply(Structure_Palace, level) == getResidentialSupply(Structure_ZoneResidential, level));
        REQUIRE(getCommercialSupply(Structure_Palace, level) == getCommercialSupply(Structure_ZoneCommercial, level));
    }
    REQUIRE(getStructureMaxLevel(Structure_Palace) == 3);
}

TEST_CASE("Fractional turret upkeep survives aggregation and funding", "[city][budget]") {
    const auto guns = DuneCity::getPoliceAnnualCost(Structure_GunTurret);
    const auto rocket = DuneCity::getPoliceAnnualCost(Structure_RocketTurret);
    REQUIRE(guns + guns == rocket);
    REQUIRE((guns + rocket + DuneCity::getPoliceAnnualCost(Structure_PoliceStation)).toDouble() == 122.5);
    REQUIRE((guns * 50 / 100).toDouble() == 3.75);
}

#include <dunecity/PoliceCoveragePolicy.h>
TEST_CASE("Service placement predictions match coverage and capped crime", "[city][crime]") {
    DuneCity::CityMapLayer<int32_t> coverage;
    coverage.init(40,40,DuneCity::kPoliceMapBlockSize);
    DuneCity::addPoliceCoverage(coverage,40,40,7,9,100);
    DuneCity::smoothPoliceCoverage(coverage,40,40);
    for (int y=0;y<40;++y) for (int x=0;x<40;++x)
        REQUIRE(DuneCity::policeCoverageAt(7,9,x,y,2,100,40,40)==coverage.worldGet(x,y));
    // A small turret cannot lower displayed crime while the underlying value
    // remains over the cap; a station must not receive credit twice for plans.
    REQUIRE(DuneCity::marginalCrimeReduction(300,0,15)==0);
    REQUIRE(DuneCity::marginalCrimeReduction(300,0,100)==50);
    REQUIRE(DuneCity::marginalCrimeReduction(150,140,100)==10);
    REQUIRE(DuneCity::marginalCrimeReduction(150,200,100)==0);
}
TEST_CASE("Police contributions are counted once per coarse cell", "[city][crime]") {
    DuneCity::CityMapLayer<int32_t> coverage;
    coverage.init(32,32,2);
    DuneCity::addPoliceCoverage(coverage,32,32,10,10,15);
    REQUIRE(coverage.worldGet(10,10)==15);
    REQUIRE(coverage.worldGet(11,11)==15);
    DuneCity::addPoliceCoverage(coverage,32,32,10,10,15);
    REQUIRE(coverage.worldGet(10,10)==30);
    coverage.init(32,32,2);
    DuneCity::addPoliceCoverage(coverage,32,32,0,0,100);
    REQUIRE(coverage.worldGet(0,0)==100);
    REQUIRE(coverage.worldGet(18,0)==0);
}
TEST_CASE("Micropolis police sources stack before diffusion", "[city][crime]") {
    REQUIRE(DuneCity::policeSourceStrength(1000,100,true,true)==1000);
    REQUIRE(DuneCity::policeSourceStrength(1000,50,true,true)==500);
    REQUIRE(DuneCity::policeSourceStrength(1000,100,false,true)==500);
    REQUIRE(DuneCity::policeSourceStrength(1000,100,false,false)==250);
    DuneCity::CityMapLayer<int32_t> single, stacked;
    single.init(60,60,6); stacked.init(60,60,6);
    DuneCity::addPoliceCoverage(single,60,60,30,30,1000);
    DuneCity::addPoliceCoverage(stacked,60,60,30,30,1000);
    DuneCity::addPoliceCoverage(stacked,60,60,31,31,1000);
    DuneCity::smoothPoliceCoverage(single,60,60);
    DuneCity::smoothPoliceCoverage(stacked,60,60);
    // Source arithmetic: center 1000 -> 500 -> 312 -> 218.
    REQUIRE(single.worldGet(30,30)==218);
    REQUIRE(stacked.worldGet(30,30)==437);
    REQUIRE(single.worldGet(54,30)==0);
    REQUIRE(stacked.worldGet(36,30)>single.worldGet(36,30));
}
TEST_CASE("Crime uses Micropolis intermediate and final caps", "[city][crime]") {
    REQUIRE(DuneCity::computeCrimeBeforePolice(1,255)==300);
    REQUIRE(DuneCity::computeCrimeAfterPolice(1,255,100)==200);
    REQUIRE(DuneCity::computeCrimeAfterPolice(1,0,42)==85);
    REQUIRE(DuneCity::computeCrimeAfterPolice(1,0,150)==0);
    REQUIRE(DuneCity::computeCrimeAfterPolice(0,255,0)==0);
}

TEST_CASE("Micropolis display categories use their original thresholds", "[city][crime]") {
    REQUIRE(std::string{DuneCity::landValueCategory(29)} == "Slum");
    REQUIRE(std::string{DuneCity::landValueCategory(30)} == "Lower Class");
    REQUIRE(std::string{DuneCity::landValueCategory(80)} == "Middle Class");
    REQUIRE(std::string{DuneCity::landValueCategory(150)} == "High");
    REQUIRE(std::string{DuneCity::crimeCategory(63)} == "Safe");
    REQUIRE(std::string{DuneCity::crimeCategory(64)} == "Light");
    REQUIRE(std::string{DuneCity::crimeCategory(128)} == "Moderate");
    REQUIRE(std::string{DuneCity::crimeCategory(192)} == "Dangerous");
    REQUIRE(std::string{DuneCity::pollutionCategory(0)} == "None");
    REQUIRE(std::string{DuneCity::pollutionCategory(1)} == "Moderate");
    REQUIRE(std::string{DuneCity::pollutionCategory(128)} == "Heavy");
    REQUIRE(std::string{DuneCity::pollutionCategory(192)} == "Very Heavy");
}

TEST_CASE("Crime unrest accelerates within Micropolis dangerous band", "[city][crime]") {
    REQUIRE(DuneCity::cityCrimeUnrestRate(250, 1240) == 0);
    REQUIRE(DuneCity::cityCrimeUnrestRate(250, 4999) == 0);
    REQUIRE(DuneCity::cityCrimeUnrestRate(250, 5000) == 150);
    REQUIRE(DuneCity::cityCrimeUnrestRate(191, 50000) == 0);
    REQUIRE(DuneCity::crimeUnrestRate(191) == 0);
    REQUIRE(DuneCity::crimeUnrestRate(192) == 100);
    REQUIRE(DuneCity::crimeUnrestRate(250) == 150);
}

TEST_CASE("Hostile land value penalty fades within four tiles", "[city][value]") {
    REQUIRE(DuneCity::hostileLandValuePenalty(0) == 80);
    REQUIRE(DuneCity::hostileLandValuePenalty(4) == 48);
    REQUIRE(DuneCity::hostileLandValuePenalty(16) == 16);
    REQUIRE(DuneCity::hostileLandValuePenalty(17) == 0);
}

TEST_CASE("DuneCity house markers remain distinct from each other and rock", "[city][colors]") {
    for (int i=0;i<8;++i) {
        const auto c=DuneCity::houseColorShade(i,0);
        for (int j=i+1;j<8;++j) {
            const auto d=DuneCity::houseColorShade(j,0);
            const int dr=int(c.r)-d.r,dg=int(c.g)-d.g,db=int(c.b)-d.b;
            REQUIRE(dr*dr+dg*dg+db*db >= 90*90);
        }
        for(int shade=1;shade<8;++shade) {
            const auto a=DuneCity::houseColorShade(i,shade-1),b=DuneCity::houseColorShade(i,shade);
            REQUIRE(int(a.r)+a.g+a.b > int(b.r)+b.g+b.b);
            REQUIRE(b.a == 255);
        }
    }
    const auto neutral=DuneCity::houseColorShade(HOUSE_NEUTRAL,0);
    REQUIRE(neutral.g > 220);
    REQUIRE(neutral.b > 220);
    REQUIRE(neutral.r < 60);
    const auto rock=DuneCity::radarTerrainColor(COLOR_ROCK);
    REQUIRE(((rock&RMASK)>>RSHIFT) < 100);
}

#include <dunecity/CrimeUnrestPolicy.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <set>
#include <Definitions.h>
TEST_CASE("District outbreaks require four to six minutes of sustained dangerous crime", "[city][crime]") {
    const uint32_t threshold=MILLI2CYCLES(kCrimeUnrestBuildupMs)*100u;
    const uint32_t step=MILLI2CYCLES(30000);
    for (int crime:{192,250}) {
        CrimeUnrestDistrict district;
        const int rate=cityCrimeUnrestRate(crime,5000);
        const int periods=crime==250 ? 8 : 12;
        for (int n=1;n<periods;++n) REQUIRE(district.advance(rate,30,step,threshold)==0);
        REQUIRE(district.advance(rate,30,step,threshold)==30);
        REQUIRE(district.progress==threshold);
        REQUIRE(district.readyCycles==0);
        REQUIRE(district.advance(rate,30,step,threshold)==30); // Mature force held for grouping.
        REQUIRE(district.readyCycles==step);
        district.reset();
        REQUIRE(district.advance(rate,30,step,threshold)==0); // No immediate repeat after release.
    }
}
TEST_CASE("Density-weighted crime clusters make larger outbreaks, not earlier ones", "[city][crime]") {
    const uint32_t threshold=MILLI2CYCLES(kCrimeUnrestBuildupMs)*100u;
    const uint32_t step=MILLI2CYCLES(30000);
    for (int count:{1,4,10,20,80}) {
        CrimeUnrestDistrict district;
        for (int n=1;n<8;++n) REQUIRE(district.advance(150,3*count,step,threshold)==0);
        REQUIRE(district.advance(150,3*count,step,threshold)==3*count);
    }
    CrimeUnrestDistrict sudden;
    for (int n=1;n<8;++n) REQUIRE(sudden.advance(150,1,step,threshold)==0);
    REQUIRE(sudden.advance(150,20,step,threshold)==20); // Size uses the current dangerous district, not an old average.
}
TEST_CASE("Policing or a small population clears pending district outbreaks", "[city][crime]") {
    const uint32_t threshold=MILLI2CYCLES(kCrimeUnrestBuildupMs)*100u;
    for (int rate:{cityCrimeUnrestRate(191,50000),cityCrimeUnrestRate(250,4999)}) {
        CrimeUnrestDistrict district;
        REQUIRE(district.advance(150,20,MILLI2CYCLES(210000),threshold)==0);
        REQUIRE(district.advance(rate,20,MILLI2CYCLES(30000),threshold)==0);
        REQUIRE(district.progress==0);
        REQUIRE(district.readyCycles==0);
    }
}
TEST_CASE("District crime history survives saves and migrates old timers without premature waves", "[city][crime][save-compat]") {
    const uint32_t threshold=MILLI2CYCLES(kCrimeUnrestBuildupMs)*100u;
    std::vector<CrimeUnrestDistrict> original(2),restored(2);
    original[0].advance(150,16,MILLI2CYCLES(240000),threshold);
    original[1].advance(100,6,MILLI2CYCLES(90000),threshold);
    original[0].readyCycles=123; // Preserve a partially gathered outbreak.
    OMemoryStream output; saveCrimeUnrest(output,original); output.writeUint32(123456);
    IMemoryStream input(output.getData(),output.getDataLength());
    loadCrimeUnrest(input,restored,9834);
    REQUIRE(input.readUint32()==123456);
    for (size_t i=0;i<2;++i) {
        REQUIRE(restored[i].progress==original[i].progress);
        REQUIRE(restored[i].readyCycles==original[i].readyCycles);
        REQUIRE(restored[i].advance(150,16,MILLI2CYCLES(120000),threshold)
                ==original[i].advance(150,16,MILLI2CYCLES(120000),threshold));
    }
    IMemoryStream exposureSave(output.getData(),output.getDataLength());
    loadCrimeUnrest(exposureSave,restored,9833);
    REQUIRE(restored[0].progress==original[0].progress);
    REQUIRE(restored[0].readyCycles==0); // Never interpret former building exposure as elapsed time.
    REQUIRE(exposureSave.readUint32()==123456);
    OMemoryStream legacy; legacy.writeUint32(2); legacy.writeUint32(123); legacy.writeUint32(456); legacy.writeUint32(987);
    IMemoryStream old(legacy.getData(),legacy.getDataLength());
    loadCrimeUnrest(old,restored,9832);
    REQUIRE(old.readUint32()==987);
    for (const auto& district:restored) { REQUIRE(district.progress==0); REQUIRE(district.readyCycles==0); }
    IMemoryStream wrong(output.getData(),output.getDataLength());
    std::vector<CrimeUnrestDistrict> wrongSize(3);
    REQUIRE_THROWS(loadCrimeUnrest(wrong,wrongSize,9833));
}

TEST_CASE("Crime wave strength follows occupied building density", "[city][crime]") {
    REQUIRE(crimeRebelsForDensity(0)==0);
    REQUIRE(crimeRebelsForDensity(1)==1);
    REQUIRE(crimeRebelsForDensity(2)==2);
    REQUIRE(crimeRebelsForDensity(3)==3);
    const int mixedDistrict=4*crimeRebelsForDensity(1)+5*crimeRebelsForDensity(2)+6*crimeRebelsForDensity(3);
    CrimeUnrestDistrict district;
    REQUIRE(district.advance(150,mixedDistrict,MILLI2CYCLES(240000),MILLI2CYCLES(kCrimeUnrestBuildupMs)*100u)==32);
}
TEST_CASE("Crime waves use one compact hotspot including map edges", "[city][crime]") {
    for (const auto& center:std::vector<std::pair<int,int>>{{0,0},{30,30},{63,63}}) {
        const auto sites=crimeSpawnSites(center.first,center.second,64,64);
        REQUIRE(sites.front()==center);
        int previous=-1;
        std::set<std::pair<int,int>> unique;
        for (const auto& p:sites) {
            REQUIRE(p.first>=0); REQUIRE(p.first<64);
            REQUIRE(p.second>=0); REQUIRE(p.second<64);
            const int distance=(p.first-center.first)*(p.first-center.first)+(p.second-center.second)*(p.second-center.second);
            REQUIRE(distance>=previous); previous=distance;
            REQUIRE(unique.insert(p).second);
        }
        REQUIRE(sites==crimeSpawnSites(center.first,center.second,64,64));
        REQUIRE(sites.size()>=289); // Space search accommodates waves larger than the former 60 cap.
    }
}

#include <players/CityRoadRepairPolicy.h>
TEST_CASE("Idle road maintenance finds outer-city gaps and prefers reconnecting roads", "[city][roads]") {
    using namespace CityRoadRepairPolicy;
    const std::vector<Footprint> buildings{{50,50,2,2},{53,50,2,2}};
    std::set<std::pair<int,int>> roads{{52,49},{52,51},{50,48}};
    auto hasRoad=[&](int x,int y) { return roads.count({x,y})!=0; };
    auto canPlace=[&](int x,int y) {
        if(x<0 || y<0 || x>=64 || y>=64 || hasRoad(x,y)) return false;
        for(const auto& b:buildings) if(x>=b.x && x<b.x+b.width && y>=b.y && y<b.y+b.height) return false;
        return true;
    };
    auto sites=candidates(buildings,canPlace,hasRoad);
    REQUIRE(!sites.empty());
    REQUIRE(sites.front()==std::make_pair(52,50)); // Missing road between two buildings.
    REQUIRE(std::count(sites.begin(),sites.end(),std::make_pair(52,50))==1);
    roads.emplace(52,50);
    sites=candidates(buildings,canPlace,hasRoad);
    REQUIRE(std::find(sites.begin(),sites.end(),std::make_pair(52,50))==sites.end());
    REQUIRE(candidates(buildings,[](int,int) {return false;},hasRoad).empty()); // Occupied/reserved sites.
    REQUIRE(candidates(std::vector<Footprint>{},canPlace,hasRoad).empty()); // No owned buildings.
    REQUIRE(candidates(buildings,canPlace,[](int,int) {return false;}).empty()); // No speculative isolated roads.
}

TEST_CASE("Neighbouring mature crime districts release together after a bounded gathering window", "[city][crime]") {
    std::vector<CrimeUnrestDistrict> districts(18);
    std::vector<int> strengths(18,0);
    constexpr uint32_t threshold=1000,wait=30;
    districts[0]={threshold,29}; strengths[0]=3;
    districts[1]={threshold,1}; strengths[1]=6;
    districts[2]={threshold-1,0}; strengths[2]=10; // Nearby, but cannot join early.
    REQUIRE(crimeOutbreakGroups(districts,strengths,3,3,threshold,wait).empty());
    districts[0].readyCycles=30;
    const auto groups=crimeOutbreakGroups(districts,strengths,3,3,threshold,wait);
    REQUIRE(groups==std::vector<std::vector<size_t>>{{0,1}});
    REQUIRE(strengths[groups[0][0]]+strengths[groups[0][1]]==9);
    for(auto i:groups[0]) districts[i].reset();
    REQUIRE(crimeOutbreakGroups(districts,strengths,3,3,threshold,wait).empty());
    REQUIRE(districts[2].progress==threshold-1);
}
TEST_CASE("Crime grouping respects house boundaries and does not wrap map rows", "[city][crime]") {
    std::vector<CrimeUnrestDistrict> districts(18);
    std::vector<int> strengths(18,0);
    for(size_t i:{2u,3u,8u,9u}) { districts[i]={1000,30}; strengths[i]=3; }
    const auto groups=crimeOutbreakGroups(districts,strengths,3,3,1000,30);
    REQUIRE(groups==std::vector<std::vector<size_t>>{{2},{3},{8},{9}});
}
TEST_CASE("Policing cancels an already mature outbreak before gathering completes", "[city][crime]") {
    CrimeUnrestDistrict district{1000,20};
    REQUIRE(district.advance(0,30,10,1000)==0);
    REQUIRE(district.progress==0);
    REQUIRE(district.readyCycles==0);
}


TEST_CASE("Demand uses worker-equivalent residential history", "[city-effects][valves][regression]") {
    ValveInputs in;
    in.resPop = in.prevResPop = 800; // 100 workers
    in.comPop = in.prevComPop = 100;
    in.indPop = in.prevIndPop = 100; // 200 jobs: labour shortage
    in.hasStadium = in.hasAirport = in.hasStarport = true;
    in.comValve = in.indValve = 1500;
    const auto first = computeDemandValves(in);
    CHECK(first.comValve < 1500);
    CHECK(first.indValve == 1260); // 0.5 labour * 1.2 external market
    for (int tick = 0; tick < 10; ++tick) {
        const auto out = computeDemandValves(in);
        in.resValve = out.resValve;
        in.comValve = out.comValve;
        in.indValve = out.indValve;
    }
    CHECK(in.comValve < 0);
    CHECK(in.indValve < 0);

    // Jobs equal workers: no artificial 1.3 labour multiplier.
    in.resPop = in.prevResPop = 1600;
    in.indValve = 0;
    CHECK(computeDemandValves(in).indValve >= 119);
    CHECK(computeDemandValves(in).indValve <= 120);
}

TEST_CASE("Civic demand notices require a stadium even with a Palace", "[city-effects][valves]") {
    ValveInputs in;
    in.resPop = in.prevResPop = 800;
    in.comPop = in.prevComPop = 101;
    in.indPop = in.prevIndPop = 71;
    in.resValve = in.comValve = in.indValve = 1000;
    const auto out = computeDemandValves(in);
    CHECK(out.civicDemandBlocked == (NeedStadium | NeedAirport | NeedStarport));
    CHECK(out.resValve == 0);
    CHECK(out.comValve == 0);
    CHECK(out.indValve == 0);
    in.hasPalace = true;
    CHECK(computeDemandValves(in).civicDemandBlocked & NeedStadium);
    CHECK(computeDemandValves(in).resValve == 0);
    in.hasStadium = true;
    CHECK_FALSE(computeDemandValves(in).civicDemandBlocked & NeedStadium);
    in.hasAirport = in.hasStarport = true;
    CHECK(computeDemandValves(in).civicDemandBlocked == 0);
    in = ValveInputs{};
    in.resPop = 500; in.comPop = 100; in.indPop = 70;
    CHECK(missingDemandCivics(in) == 0);
    in.resPop++; in.comPop++; in.indPop++;
    in.resValve = in.comValve = in.indValve = -1500;
    CHECK(computeDemandValves(in).civicDemandBlocked == 0);
}

TEST_CASE("Civic notices are spaced, deduplicated and rearmed after resolution", "[city-effects][messages]") {
    CityDemandNoticePolicy notices;
    const uint8_t all = NeedStadium | NeedAirport | NeedStarport;
    CHECK(notices.update(all, all, 0, 10) == NeedStadium);
    CHECK(notices.update(all, all, 1, 10) == 0);
    CHECK(notices.update(all, 0, 10, 10) == NeedAirport);
    CHECK(notices.update(all, 0, 20, 10) == NeedStarport);
    CHECK(notices.update(all, all, 30, 10) == 0);
    CHECK(notices.update(0, 0, 31, 10) == 0);
    CHECK(notices.update(NeedStadium, NeedStadium, 32, 10) == NeedStadium);
    CityDemandNoticePolicy cancelled;
    CHECK(cancelled.update(all, all, 0, 10) == NeedStadium);
    CHECK(cancelled.update(NeedStadium, 0, 10, 10) == 0);
}


TEST_CASE("Park terrain counts each source once and uses Micropolis smoothing", "[city-effects][park][regression]") {
    ParkTerrainPolicy terrain;
    terrain.init(18,18);
    terrain.addSource(7,7,15);
    // Micropolis non-dither: center15/2=7, cardinal neighbour(15/4)/2=1.
    CHECK(terrain.valueAt(6,6) == 7);
    CHECK(terrain.valueAt(8,8) == 7);
    CHECK(terrain.valueAt(9,6) == 1);
    CHECK(terrain.valueAt(6,9) == 1);
    CHECK(terrain.valueAt(9,9) == 0);
    CHECK(terrain.valueAt(12,6) == 0);
    // Aggregate raw sources BEFORE smoothing, not rounded per-emitter stamps.
    terrain.addSource(8,7,15);
    CHECK(terrain.valueAt(6,6) == 15);
    CHECK(terrain.valueAt(9,6) == 3);
    CHECK(terrain.marginalGain(6,7,15,6,6) == 7);
    CHECK(kParkTerrainBlockSize == 3); // 2-tile zone + unchanged 1-tile road
}

TEST_CASE("Park source boundaries do not alias or wrap at map edges", "[city-effects][park]") {
    ParkTerrainPolicy terrain;
    terrain.init(8,7);
    terrain.addSource(-1,0,15);
    terrain.addSource(8,0,15);
    CHECK(terrain.valueAt(0,0) == 0);
    terrain.addSource(0,0,15);
    CHECK(terrain.valueAt(0,0) == 7);
    CHECK(terrain.valueAt(3,0) == 1);
    CHECK(terrain.valueAt(0,3) == 1);
    CHECK(terrain.valueAt(-1,0) == 0);
    terrain.addSource(7,6,15);
    CHECK(terrain.valueAt(7,6) == 7);
    CHECK(terrain.valueAt(8,6) == 0);
    terrain.init(8,7);
    CHECK(terrain.valueAt(0,0) == 0); // recompute clears destroyed sources
    terrain.addSource(3,3,15);
    CHECK(terrain.landValueContribution(3,3) == 2); // average0,1,1,7, not origin-only0
    CHECK(terrain.landValueContribution(4,4) == 7);
}

TEST_CASE("AI park gain matches runtime with overlap and mismatched grid alignment", "[city-effects][park][ai]") {
    ParkTerrainPolicy terrain;
    terrain.init(11,10);
    terrain.addSource(0,0,15);
    terrain.addSource(1,0,15);
    terrain.addSource(4,4,15);
    terrain.addSource(10,9,15);
    for (int cy=0;cy<10;++cy) for (int cx=0;cx<11;++cx) {
        auto after=terrain;
        after.addSource(cx,cy,15);
        for (int py=0;py<10;++py) for (int px=0;px<11;++px)
            CHECK(terrain.marginalGain(cx,cy,15,px,py) ==
                after.landValueContribution(px,py)-terrain.landValueContribution(px,py));
    }
    // The park enters terrain before pollution and the land-value floor.
    CHECK(computeBaseLandValue(0,7,200) == 1);
    CHECK(computeBaseLandValue(0,7,0)-computeBaseLandValue(0,0,0) == 7);
}

TEST_CASE("Latest government factory tiers apply to old high-density occupancy", "[city-effects][role]") {
    CHECK(effectiveCityLevel(Structure_LightFactory,3) == 1);
    CHECK(getZonePopulation(Structure_LightFactory,3) == 1);
    for (int item : {Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard}) {
        CAPTURE(item);
        CHECK(getStructureCityRole(item) == CityRole::Industrial);
        CHECK(effectiveCityLevel(item,3) == 2);
        CHECK(getZonePopulation(item,3) == 3);
        CHECK(getIndustrialSupply(item,3) == 25);
        CHECK(getCommercialSupply(item,3) == 0);
        CHECK(getPollutionEmission(item,3) == 25);
        CHECK(taxablePopulationEighths(item, 3, 3) == 0);
    }
    CHECK(getStructureMaxLevel(Structure_IX) == 3);
    CHECK(getCommercialSupply(Structure_IX,3) == 50);
    CHECK(getZonePopulation(Structure_IX,3) == 5);
    CHECK(taxablePopulationEighths(Structure_IX, 5, 3) == 0);
}

#include <dunecity/CitySimulation.h>
TEST_CASE("Police funding belongs to the issuing house and clamps safely", "[city][budget]") {
    DuneCity::CitySimulation sim;
    sim.setPoliceFundingPercent(1,50);
    REQUIRE(sim.getPoliceFundingPercent(1)==50);
    REQUIRE(sim.getPoliceFundingPercent(0)==100); // Observer/human unaffected.
    REQUIRE(sim.getPoliceFundingPercent(2)==100); // Another AI unaffected.
    sim.setPoliceFundingPercent(1,125);
    REQUIRE(sim.getPoliceFundingPercent(1)==100);
    sim.setPoliceFundingPercent(1,-5);
    REQUIRE(sim.getPoliceFundingPercent(1)==0);
    sim.setPoliceFundingPercent(-1,25);
    sim.setPoliceFundingPercent(DuneCity::kMaxCityHouses,25);
    REQUIRE(sim.getPoliceFundingPercent(0)==100);
}

TEST_CASE("Completed road redirects to another useful gap when its original tile is repaired", "[city][roads]") {
    using namespace CityRoadRepairPolicy;
    std::vector<Footprint> buildings{{10,10,2,2},{40,40,2,2}};
    std::set<std::pair<int,int>> roads{{12,9},{12,10},{12,11},{42,39},{42,41}};
    std::set<std::pair<int,int>> queued{{40,39}};
    auto hasRoad=[&](int x,int y) {return roads.count({x,y})!=0;};
    auto canPlace=[&](int x,int y) {
        if (hasRoad(x,y) || queued.count({x,y})) return false;
        for (const auto& b:buildings) if (x>=b.x && x<b.x+b.width && y>=b.y && y<b.y+b.height) return false;
        return true;
    };
    const auto sites=candidates(buildings,canPlace,hasRoad);
    REQUIRE(!sites.empty());
    REQUIRE(sites.front()==std::make_pair(42,40)); // Remote broken through-road wins.
    REQUIRE(std::find(sites.begin(),sites.end(),std::make_pair(12,10))==sites.end());
    REQUIRE(std::find(sites.begin(),sites.end(),std::make_pair(40,39))==sites.end());
    REQUIRE(candidates(buildings,[](int,int){return false;},hasRoad).empty()); // Keep finished item for later.
}

TEST_CASE("Road access cannot move police coverage into another district", "[city][crime]") {
    struct RoadTile { bool value=false; bool isRoad() const { return value; } };
    struct RoadMap {
        std::array<RoadTile,64*64> tiles{};
        int getSizeX() const { return 64; }
        int getSizeY() const { return 64; }
        const RoadTile* getTile(int x,int y) const { return &tiles[y*64+x]; }
    } map;
    // 645: station (24,5), first road (23,4), industrial zone (30,4).
    // The arbitrary first road moved the station a full six-tile police cell west.
    map.tiles[4*64+23].value=true;
    const auto source=DuneCity::policeSource(map,24,5,2,2,1000,100,true);
    CHECK(source.x==24); CHECK(source.y==5); CHECK(source.strength==1000);
    map.tiles[4*64+23].value=false;
    map.tiles[5*64+26].value=true;
    const auto opposite=DuneCity::policeSource(map,24,5,2,2,1000,100,true);
    CHECK(opposite.x==source.x); CHECK(opposite.y==source.y);
    CHECK(opposite.strength==source.strength);
    DuneCity::CityMapLayer<int32_t> field;
    field.init(64,64,DuneCity::kPoliceMapBlockSize);
    DuneCity::addPoliceCoverage(field,64,64,source.x,source.y,source.strength);
    DuneCity::addPoliceCoverage(field,64,64,29,3,150); // Nearby rocket.
    DuneCity::smoothPoliceCoverage(field,64,64);
    CHECK(DuneCity::computeCrimeAfterPolice(25,172,field.worldGet(30,4))<192);
    map.tiles[5*64+26].value=false;
    const auto disconnected=DuneCity::policeSource(map,24,5,2,2,1000,100,true);
    CHECK(disconnected.x==24); CHECK(disconnected.y==5); CHECK(disconnected.strength==500);
}

TEST_CASE("Spectator city runtime restores derived state and rejects invalid dimensions or truncation", "[city][spectator]") {
    CitySimulation source;
    source.init(23,17); // Include partial 2x2 and 3x3 cache blocks.
    source.setCityEffectsEnabled(false);
    source.getHouseStateMut(0).taxBaseEighths=123;
    source.getHouseStateMut(0).civicDemandBlocked=5;
    OMemoryStream saved;
    source.saveObserverRuntime(saved);
    CitySimulation restored;
    restored.init(23,17);
    SECTION("exact runtime survives without a growth or effects pass") {
        IMemoryStream input(saved.getData(),saved.getDataLength());
        restored.loadObserverRuntime(input);
        REQUIRE(input.getRemainingLength()==0);
        REQUIRE_FALSE(restored.areCityEffectsEnabled());
        REQUIRE(restored.getHouseState(0).taxBaseEighths==123);
        REQUIRE(restored.getHouseState(0).civicDemandBlocked==5);
        OMemoryStream again;
        restored.saveObserverRuntime(again);
        REQUIRE(std::string(saved.getData(),saved.getDataLength())==std::string(again.getData(),again.getDataLength()));
    }
    SECTION("a different map cannot consume the cache payload") {
        restored.init(24,17);
        IMemoryStream input(saved.getData(),saved.getDataLength());
        REQUIRE_THROWS(restored.loadObserverRuntime(input));
    }
    SECTION("a truncated layer cannot become a complete checkpoint") {
        IMemoryStream input(saved.getData(),saved.getDataLength()-1);
        REQUIRE_THROWS(restored.loadObserverRuntime(input));
    }
}

TEST_CASE("Building condition lowers land value and repairs restore it", "[city-effects][condition]") {
    REQUIRE(conditionLandValue(200,100,100)==200);
    REQUIRE(conditionLandValue(200,75,100)==150);
    REQUIRE(conditionLandValue(200,50,100)==100);
    REQUIRE(conditionLandValue(200,25,100)==50);
    REQUIRE(conditionLandValue(200,0,100)==1);
    REQUIRE(conditionLandValue(0,50,100)==0);
    REQUIRE(conditionLandValue(200,110,100)==200);
}

// ============================================================================
// Hostile land-value penalty: spatial index must equal the old nested loop
// ============================================================================
//
// runEffectsScans() used to compare every active building against every active
// unit. 1.0.794 buckets attack-capable units into cells and visits only the
// cells that can reach each footprint. hostileLandValuePenalty() is zero beyond
// a squared separation of 16, so skipping further pairs cannot change the
// result — but the bucketing must not lose units, and in particular must not
// lose units standing off the map edge, which the original loop (having no
// bounds test at all) did penalise. The two loops below mirror the shipped
// before/after geometry, including the clamped bucket assignment.
namespace {

struct RefBuilding { int x, y, w, h, team; };
struct RefUnit     { int x, y, team; bool active, canAttack, visibleToEnemy; };

constexpr int kRefBlock = 2;    // CityMapLayer default block size
constexpr int kRefReach = 4;    // floor(sqrt(16)), as in CityEffectsRuntime
constexpr int kRefCell  = 8;    // bucket size, as in CityEffectsRuntime

bool refVisible(const RefUnit& unit, int team) {
    return unit.team == team ? true : unit.visibleToEnemy;
}

int refPenaltyFor(const RefBuilding& b, const RefUnit& u) {
    const int dx = std::max({b.x - u.x, 0, u.x - (b.x + b.w - 1)});
    const int dy = std::max({b.y - u.y, 0, u.y - (b.y + b.h - 1)});
    return hostileLandValuePenalty(dx*dx + dy*dy);
}

void stamp(std::map<std::pair<int,int>,int>& out, const RefBuilding& b,
           int penalty, int mapW, int mapH) {
    for (int y = b.y; y < b.y + b.h; ++y)
        for (int x = b.x; x < b.x + b.w; ++x) {
            if (x < 0 || y < 0 || x >= mapW || y >= mapH) continue;
            const auto key = std::make_pair(x / kRefBlock, y / kRefBlock);
            out[key] = std::max(out[key], penalty);
        }
}

// The pre-1.0.794 algorithm: full cross product, max() per covered block.
std::map<std::pair<int,int>,int> referenceLoop(
        int mapW, int mapH,
        const std::vector<RefBuilding>& buildings, const std::vector<RefUnit>& units) {
    std::map<std::pair<int,int>,int> out;
    for (const auto& b : buildings)
        for (const auto& u : units) {
            if (!u.active || !u.canAttack || u.team == b.team || !refVisible(u, b.team)) continue;
            const int penalty = refPenaltyFor(b, u);
            if (penalty) stamp(out, b, penalty, mapW, mapH);
        }
    return out;
}

// The 1.0.794 algorithm: clamped buckets plus reaching-cell queries.
std::map<std::pair<int,int>,int> indexedLoop(
        int mapW, int mapH,
        const std::vector<RefBuilding>& buildings, const std::vector<RefUnit>& units) {
    const int gridW = std::max(1, (mapW + kRefCell - 1) / kRefCell);
    const int gridH = std::max(1, (mapH + kRefCell - 1) / kRefCell);
    std::vector<std::vector<const RefUnit*>> cells(static_cast<size_t>(gridW) * gridH);
    for (const auto& u : units) {
        if (!u.active || !u.canAttack) continue;
        const int cx = std::min(gridW - 1, std::max(0, u.x) / kRefCell);
        const int cy = std::min(gridH - 1, std::max(0, u.y) / kRefCell);
        cells[static_cast<size_t>(cy) * gridW + cx].push_back(&u);
    }
    std::map<std::pair<int,int>,int> out;
    for (const auto& b : buildings) {
        const int cellX0 = std::max(0, b.x - kRefReach) / kRefCell;
        const int cellY0 = std::max(0, b.y - kRefReach) / kRefCell;
        const int cellX1 = std::min(gridW - 1, std::max(0, b.x + b.w - 1 + kRefReach) / kRefCell);
        const int cellY1 = std::min(gridH - 1, std::max(0, b.y + b.h - 1 + kRefReach) / kRefCell);
        std::vector<const RefUnit*> nearby;
        for (int cy = cellY0; cy <= cellY1; ++cy)
            for (int cx = cellX0; cx <= cellX1; ++cx) {
                const auto& bucket = cells[static_cast<size_t>(cy) * gridW + cx];
                nearby.insert(nearby.end(), bucket.begin(), bucket.end());
            }
        for (const RefUnit* u : nearby) {
            if (u->team == b.team || !refVisible(*u, b.team)) continue;
            const int penalty = refPenaltyFor(b, *u);
            if (penalty) stamp(out, b, penalty, mapW, mapH);
        }
    }
    return out;
}

} // namespace

TEST_CASE("Hostile penalty: radius boundary matches the distance formula", "[city-effects][hostile]") {
    REQUIRE(hostileLandValuePenalty(0)  == 80);
    REQUIRE(hostileLandValuePenalty(16) == 16);
    REQUIRE(hostileLandValuePenalty(17) == 0);
    REQUIRE(hostileLandValuePenalty(32) == 0);
    // A unit exactly kRefReach tiles away on one axis still contributes, which
    // is why the query box is widened by kRefReach and not by less.
    const RefBuilding b{10,10,2,2,0};
    REQUIRE(refPenaltyFor(b, RefUnit{10 - kRefReach,10,1,true,true,true}) > 0);
    REQUIRE(refPenaltyFor(b, RefUnit{10 - kRefReach - 1,10,1,true,true,true}) == 0);
}

TEST_CASE("Hostile penalty: off-map and edge units are retained", "[city-effects][hostile]") {
    const int mapW = 24, mapH = 24;
    const std::vector<RefBuilding> buildings{
        {0,0,2,2,0},            // top-left corner
        {mapW-2,mapH-2,2,2,0},  // bottom-right corner
        {12,12,2,2,0},          // interior, must stay unaffected by off-map units
    };
    const std::vector<RefUnit> units{
        {-1,0,1,true,true,true},              // off-map left, one tile from (0,0)
        {0,-1,1,true,true,true},              // off-map top
        {-1,-1,1,true,true,true},             // off-map diagonal
        {-4,-4,1,true,true,true},             // off-map, out of reach
        {mapW,mapH-1,1,true,true,true},       // off-map right of the far corner
        {mapW+3,mapH+3,1,true,true,true},     // far off-map, out of reach
        {-50,-50,1,true,true,true},           // very far off-map, must score zero
    };
    const auto reference = referenceLoop(mapW,mapH,buildings,units);
    REQUIRE_FALSE(reference.empty());          // the corner really is penalised
    REQUIRE(indexedLoop(mapW,mapH,buildings,units) == reference);
}

TEST_CASE("Hostile penalty: team and visibility filters are unchanged", "[city-effects][hostile]") {
    const int mapW = 24, mapH = 24;
    const std::vector<RefBuilding> buildings{{10,10,2,2,0},{14,10,2,2,1}};
    const std::vector<RefUnit> units{
        {11,11,0,true,true,true},    // same team as building 0: never penalises it
        {11,11,1,true,true,true},    // hostile and visible
        {12,11,1,true,true,false},   // hostile but invisible to team 0
        {12,12,1,false,true,true},   // inactive
        {12,12,1,true,false,true},   // cannot attack
    };
    const auto reference = referenceLoop(mapW,mapH,buildings,units);
    REQUIRE(indexedLoop(mapW,mapH,buildings,units) == reference);
}

// ============================================================================
// SpatialFields: the production helpers, against the loops they replace
// ============================================================================
//
// These exercise include/dunecity/SpatialFields.h — the same code the engine
// runs — rather than a copy of the algorithm, so a divergence here is a real
// divergence in the game.
namespace {

struct PlainCoord { int x, y; };

// The loop ChebyshevWeightField replaces, verbatim from manageHarvesterSafety:
//   for (peer : peers) if (cheb(c,peer) < 4) sum += (4 - cheb(c,peer)) * 12;
int bruteCrowdPenalty(const std::vector<PlainCoord>& peers, int x, int y,
                      int radius, int weight) {
    int sum = 0;
    for (const auto& peer : peers) {
        const int d = std::max(std::abs(x - peer.x), std::abs(y - peer.y));
        if (d < radius + 1) sum += (radius + 1 - d) * weight;
    }
    return sum;
}

// The probe BoxAnyField replaces, verbatim from Map::isWithinBuildRange():
// any marked tile in the (2R+1)^2 square, off-map probes simply skipped.
bool bruteAnyWithin(const std::vector<std::vector<bool>>& marked,
                    int x, int y, int radius) {
    const int h = static_cast<int>(marked.size());
    const int w = h ? static_cast<int>(marked[0].size()) : 0;
    for (int i = x - radius; i <= x + radius; ++i)
        for (int j = y - radius; j <= y + radius; ++j) {
            if (i < 0 || j < 0 || i >= w || j >= h) continue;   // getTile_internal == nullptr
            if (marked[j][i]) return true;
        }
    return false;
}

// The footprint OR that okayToPlaceStructure() performs over its tiles.
bool bruteAnyNearFootprint(const std::vector<std::vector<bool>>& marked,
                           int x, int y, int fw, int fh, int radius) {
    for (int i = x; i < x + fw; ++i)
        for (int j = y; j < y + fh; ++j)
            if (bruteAnyWithin(marked, i, j, radius)) return true;
    return false;
}

} // namespace

TEST_CASE("ChebyshevWeightField equals the peer loop it replaces", "[spatial-fields][crowd]") {
    constexpr int radius = 3, weight = 12;   // manageHarvesterSafety's constants

    SECTION("no peers scores zero everywhere") {
        DuneCity::ChebyshevWeightField field(radius, weight);
        field.build(std::vector<PlainCoord>{}, 16, 16);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) REQUIRE(field.at(x,y) == 0);
    }

    SECTION("boundary at distance 3 and 4") {
        const std::vector<PlainCoord> peers{{8,8}};
        DuneCity::ChebyshevWeightField field(radius, weight);
        field.build(peers, 20, 20);
        REQUIRE(field.at(8,8) == 48);     // distance 0 -> (4-0)*12
        REQUIRE(field.at(11,8) == 12);    // distance 3 -> (4-3)*12, still counted
        REQUIRE(field.at(12,8) == 0);     // distance 4 -> excluded
        REQUIRE(field.at(11,11) == 12);   // diagonal distance 3
        REQUIRE(field.at(12,12) == 0);
    }

    SECTION("duplicate peers accumulate, as the loop does") {
        const std::vector<PlainCoord> peers{{5,5},{5,5},{5,5}};
        DuneCity::ChebyshevWeightField field(radius, weight);
        field.build(peers, 16, 16);
        REQUIRE(field.at(5,5) == 3*48);
        REQUIRE(field.at(5,5) == bruteCrowdPenalty(peers,5,5,radius,weight));
    }

    SECTION("off-map peers still reach in-map cells near the edge") {
        const std::vector<PlainCoord> peers{{-1,0},{0,-2},{-3,-3},{17,17}};
        const int w = 16, h = 16;
        DuneCity::ChebyshevWeightField field(radius, weight);
        field.build(peers, w, h);
        REQUIRE(field.at(0,0) > 0);     // the (-1,0) peer must not be dropped
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                REQUIRE(field.at(x,y) == bruteCrowdPenalty(peers,x,y,radius,weight));
    }

    SECTION("random maps and peer sets agree cell for cell") {
        uint32_t seed = 0xc0ffeeu;
        auto next = [&](int lo, int hi) {
            seed = seed*1664525u + 1013904223u;
            return lo + static_cast<int>((seed >> 16) % static_cast<uint32_t>(hi - lo + 1));
        };
        for (int trial = 0; trial < 120; ++trial) {
            const int w = next(1,23), h = next(1,19);   // includes non-square and 1xN
            std::vector<PlainCoord> peers;
            for (int i = 0, n = next(0,12); i < n; ++i)
                peers.push_back({next(-5,w+5), next(-5,h+5)});
            DuneCity::ChebyshevWeightField field(radius, weight);
            field.build(peers, w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    REQUIRE(field.at(x,y) == bruteCrowdPenalty(peers,x,y,radius,weight));
        }
    }
}

TEST_CASE("BoxAnyField equals the build-range probe it replaces", "[spatial-fields][buildrange]") {
    SECTION("single owned tile, radius boundary") {
        std::vector<std::vector<bool>> marked(12, std::vector<bool>(12,false));
        marked[6][6] = true;
        DuneCity::BoxAnyField field;
        field.build(12,12,[&](int x,int y){ return marked[y][x]; });
        for (int r = 0; r <= 4; ++r)
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < 12; ++x)
                    REQUIRE(field.anyWithin(x,y,r) == bruteAnyWithin(marked,x,y,r));
    }

    SECTION("no owned tiles is always false") {
        DuneCity::BoxAnyField field;
        field.build(9,7,[](int,int){ return false; });
        REQUIRE_FALSE(field.anyWithin(4,3,2));
        REQUIRE_FALSE(field.anyNearFootprint(0,0,2,2,2));
    }

    SECTION("every owner, edges, radii and non-square maps") {
        uint32_t seed = 0x1234abcdu;
        auto next = [&](int lo, int hi) {
            seed = seed*1664525u + 1013904223u;
            return lo + static_cast<int>((seed >> 16) % static_cast<uint32_t>(hi - lo + 1));
        };
        for (int trial = 0; trial < 90; ++trial) {
            const int w = next(1,21), h = next(1,13);
            const int owners = next(1,4);
            const int houseID = next(0,owners-1);
            // Per-tile owner, mirroring isConstructionAnchor(tileOwner, houseID).
            std::vector<std::vector<bool>> marked(h, std::vector<bool>(w,false));
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    marked[y][x] = (next(0,owners) % (owners+1)) == houseID;
            DuneCity::BoxAnyField field;
            field.build(w,h,[&](int x,int y){ return marked[y][x]; });
            const int radius = next(0,3);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    REQUIRE(field.anyWithin(x,y,radius) == bruteAnyWithin(marked,x,y,radius));
            // Footprints, including ones hanging off the right/bottom edges.
            for (int fh = 1; fh <= 3; ++fh)
                for (int fw = 1; fw <= 3; ++fw)
                    for (int y = -1; y <= h; ++y)
                        for (int x = -1; x <= w; ++x)
                            REQUIRE(field.anyNearFootprint(x,y,fw,fh,radius)
                                    == bruteAnyNearFootprint(marked,x,y,fw,fh,radius));
        }
    }
}

TEST_CASE("Hostile penalty: dense adversarial sweep matches the old loop", "[city-effects][hostile]") {
    // Deterministic sweep straddling every edge, with coordinates outside the
    // map on all four sides and map dimensions that are not cell multiples.
    const int mapW = 33, mapH = 17;
    uint32_t seed = 0x5eed1234u;
    auto next = [&](int lo, int hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + static_cast<int>((seed >> 16) % static_cast<uint32_t>(hi - lo + 1));
    };
    for (int trial = 0; trial < 200; ++trial) {
        std::vector<RefBuilding> buildings;
        for (int i = 0, n = next(1,6); i < n; ++i) {
            const int w = next(1,3), h = next(1,3);
            buildings.push_back({next(0,mapW-w), next(0,mapH-h), w, h, next(0,1)});
        }
        std::vector<RefUnit> units;
        for (int i = 0, n = next(1,20); i < n; ++i)
            units.push_back({next(-6,mapW+6), next(-6,mapH+6), next(0,1),
                             next(0,9) > 0, next(0,9) > 0, next(0,3) > 0});
        REQUIRE(indexedLoop(mapW,mapH,buildings,units)
                == referenceLoop(mapW,mapH,buildings,units));
    }
}
