/*
 *  PoliceStationTestCase.cpp
 *
 *  Regression tests for the DuneCity Police Station — a 2x2 city-mode
 *  building that replaced Barracks/WOR as the source of police coverage.
 *  Price matches SimCity Classic's TOOL_POLICESTATION (gCostOf[4] = 500
 *  in MicropolisEngine/src/tool.cpp).
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <dunecity/CityEffects.h>

using namespace DuneCity;

TEST_CASE("PoliceStation: assigned the expected ItemID (last structure ID)",
          "[police-station][ids][regression]") {
    REQUIRE(Structure_PoliceStation == 26);
    REQUIRE(Structure_LastID == 26);
}

TEST_CASE("PoliceStation: isStructure recognises it", "[police-station][helpers]") {
    REQUIRE(isStructure(Structure_PoliceStation));
    REQUIRE_FALSE(isUnit(Structure_PoliceStation));
    // Not a R/C/I zone — that helper is reserved for those three.
    REQUIRE_FALSE(isZoneStructure(Structure_PoliceStation));
}

TEST_CASE("PoliceStation: not a city-role structure (R/C/I)",
          "[police-station][role]") {
    // PoliceStation provides police coverage but is NOT a city-role
    // building — it doesn't contribute residential/commercial/industrial
    // supply or population, and it doesn't grow via runZoneGrowth.
    REQUIRE(getStructureCityRole(Structure_PoliceStation) == CityRole::None);
    REQUIRE(getStructureMaxLevel(Structure_PoliceStation) == 0);
}

TEST_CASE("PoliceStation: provides full police coverage",
          "[police-station][police][regression]") {
    REQUIRE(getPoliceCoverage(Structure_PoliceStation) == kPoliceCoverageFull);
    REQUIRE(getPoliceCoverage(Structure_PoliceStation) == 1000);
}

TEST_CASE("PoliceStation: annual upkeep is 100 (designer-tuned from SC's 500)",
          "[police-station][police][regression]") {
    // Originally matched SC's gCostOf[TOOL_POLICESTATION] = 500;
    // reduced to 100 to make Police Stations more accessible.
    REQUIRE(getPoliceAnnualCost(Structure_PoliceStation) == kPoliceCostPoliceStation);
    REQUIRE(getPoliceAnnualCost(Structure_PoliceStation) == 100);
}

TEST_CASE("PoliceStation: the full police source; Barracks/WOR police at half",
          "[police-station][police][regression]") {
    // The PoliceStation replaced Barracks/WOR as the full-coverage source (they once gave a full station's
    // coverage). Since Stefan, 2026-11-28 ("and infantry and wor have 50% police effect") they give exactly
    // half, never full, and cost no upkeep. Pinned so a refactor that changes either is noisy.
    REQUIRE(getPoliceCoverage(Structure_Barracks)   == getPoliceCoverage(Structure_PoliceStation) / 2);
    REQUIRE(getPoliceCoverage(Structure_WOR)        == getPoliceCoverage(Structure_PoliceStation) / 2);
    REQUIRE(getPoliceAnnualCost(Structure_Barracks) == 0);
    REQUIRE(getPoliceAnnualCost(Structure_WOR)      == 0);
}

TEST_CASE("PoliceStation: turrets keep their fractional coverage",
          "[police-station][police]") {
    // Turrets retain their fractional coverage as garrison adjacency.
    // Both Gun and Rocket Turrets now provide 25% after tuning.
    REQUIRE(getPoliceCoverage(Structure_GunTurret)    == kPoliceCoverageGunTurret);
    REQUIRE(getPoliceCoverage(Structure_RocketTurret) == kPoliceCoverageRocketTurret);
}

#include <dunecity/CityFactionPolicy.h>
#include <dunecity/AirPatrolCycle.h>

TEST_CASE("DuneCity gives Harkonnen trike and ornithopter production only at the right factory", "[city][factions]") {
    for (const auto city : {false,true}) {
        CHECK(cityHarkonnenProduct(city,HOUSE_HARKONNEN,Structure_LightFactory,Unit_Trike)==city);
        CHECK(cityHarkonnenProduct(city,HOUSE_HARKONNEN,Structure_HighTechFactory,Unit_Ornithopter)==city);
    }
    CHECK_FALSE(cityHarkonnenProduct(true,HOUSE_HARKONNEN,Structure_HeavyFactory,Unit_Trike));
    CHECK_FALSE(cityHarkonnenProduct(true,HOUSE_ORDOS,Structure_LightFactory,Unit_Trike));
    CHECK_FALSE(cityHarkonnenProduct(true,HOUSE_HARKONNEN,Structure_LightFactory,Unit_Ornithopter));
}

TEST_CASE("Airport patrol cooldown preserves a partially deployed pair across reload", "[city][airport][save]") {
    AirPatrolCycle patrol(10);
    for (int i=0;i<9;++i) { patrol.tick(); CHECK_FALSE(patrol.ready()); }
    patrol.tick(); REQUIRE(patrol.ready());
    patrol.deployed(10);
    CHECK(patrol.ready()); CHECK(patrol.pendingAircraft==1);
    AirPatrolCycle loaded(10);
    loaded.restore(patrol.remainingCycles,patrol.pendingAircraft,10);
    loaded.tick(); CHECK(loaded.ready()); CHECK(loaded.pendingAircraft==1);
    loaded.deployed(10);
    CHECK_FALSE(loaded.ready()); CHECK(loaded.remainingCycles==10); CHECK(loaded.pendingAircraft==2);
    // Blocked deployments don't consume the pair or restart its cooldown.
    AirPatrolCycle blocked(0);
    for (int i=0;i<20;++i) blocked.tick();
    CHECK(blocked.ready()); CHECK(blocked.pendingAircraft==2);
}
