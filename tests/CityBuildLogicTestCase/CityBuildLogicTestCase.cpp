/*
 *  CityBuildLogicTestCase.cpp - Unit tests for city road placement helpers.
 */

#include <catch2/catch_all.hpp>
#include <Command.h>
#include <dunecity/CityConstants.h>

TEST_CASE("CityBuildLogic: road placement uses the city tool road command", "[city][roads][command]") {
    const auto command = DuneCity::getRoadPlacementCommandDescriptor();

    REQUIRE(command.commandId == CMD_CITY_TOOL);
    REQUIRE(command.parameter == static_cast<uint32_t>(DuneCity::CityTool_Road));
}

TEST_CASE("CityBuildLogic: loaded city saves keep the simulation active",
          "[city][save][regression]") {
    REQUIRE(DuneCity::shouldEnableLoadedCityEffects(true));
    REQUIRE_FALSE(DuneCity::shouldEnableLoadedCityEffects(false));
}

TEST_CASE("CityBuildLogic: roads build immediately",
          "[city][roads][concrete][timing]") {
    REQUIRE(DuneCity::getCityBuildTime(Structure_Road, 4) == 1);
}

TEST_CASE("CityBuildLogic: zones use normal configured construction timing", "[city][zones][timing]") {
    for (const int zone : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
        CAPTURE(zone);
        CHECK(DuneCity::getCityBuildTime(zone, 40) == 40);
        CHECK(DuneCity::getCityBuildTime(zone, 24) == 24); // house/mod override
        CHECK(DuneCity::getCityBuildTime(zone, 96) == 96);
        CHECK(DuneCity::getCityBuildTime(zone, 0) == 1); // no divide by zero
    }
    CHECK(DuneCity::getCityBuildTime(Structure_Silo, 48) == 48);
    CHECK(DuneCity::getCityBuildTime(Structure_PoliceStation, 80) == 80);
}

TEST_CASE("CityBuildLogic: city-only placement set covers every city item",
          "[city][placement]") {
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_ZoneResidential));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_ZoneCommercial));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_ZoneIndustrial));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_Road));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_PowerLine));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_NuclearPlant));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_PoliceStation));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_Stadium));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_Airport));
    REQUIRE(DuneCity::isCityOnlyStructure(Structure_Park));

    REQUIRE_FALSE(DuneCity::isCityOnlyStructure(Structure_Slab1));
    REQUIRE_FALSE(DuneCity::isCityOnlyStructure(Structure_ConstructionYard));
    REQUIRE_FALSE(DuneCity::isCityOnlyStructure(Structure_WindTrap));
}

TEST_CASE("CityBuildLogic: city placement preview accepts only gravel or concrete",
          "[city][zones][placement][preview]") {
    REQUIRE(DuneCity::isCityBuildableTerrain(Terrain_Rock));
    REQUIRE(DuneCity::isCityBuildableTerrain(Terrain_Slab));

    REQUIRE_FALSE(DuneCity::isCityBuildableTerrain(Terrain_Sand));
    REQUIRE_FALSE(DuneCity::isCityBuildableTerrain(Terrain_Dunes));
    REQUIRE_FALSE(DuneCity::isCityBuildableTerrain(Terrain_Spice));
    REQUIRE_FALSE(DuneCity::isCityBuildableTerrain(Terrain_ThickSpice));
    REQUIRE_FALSE(DuneCity::isCityBuildableTerrain(Terrain_Mountain));
}

TEST_CASE("CityBuildLogic: road placement accepts concrete terrain", "[city][roads][concrete]") {
    // Tile::isRock() includes Terrain_Slab, so concrete reaches this helper as
    // supported terrain and the road flag replaces the slab visually.
    const auto concreteState = DuneCity::makeCityTilePlacementState(
        true, false, false, false, false);
    REQUIRE(DuneCity::canPlaceRoad(concreteState));
}

TEST_CASE("CityBuildLogic: valid road placement marks tile as road", "[city][roads][placement]") {
    auto state = DuneCity::makeCityTilePlacementState(
        true,   // isRock
        false,  // isMountain
        false,  // hasGroundObject
        false,  // hasCityZone
        false   // hasRoad
    );

    REQUIRE(DuneCity::canPlaceRoad(state));
    REQUIRE(DuneCity::applyRoadPlacement(state));
    REQUIRE(state.hasRoad);
}

TEST_CASE("CityBuildLogic: invalid road placement is rejected cleanly", "[city][roads][placement]") {
    SECTION("occupied tile") {
        const auto state = DuneCity::makeCityTilePlacementState(true, false, true, false, false);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE_FALSE(mutableState.hasRoad);
    }

    SECTION("blocked terrain") {
        const auto state = DuneCity::makeCityTilePlacementState(false, false, false, false, false);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE_FALSE(mutableState.hasRoad);
    }

    SECTION("mountain terrain") {
        const auto state = DuneCity::makeCityTilePlacementState(true, true, false, false, false);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE_FALSE(mutableState.hasRoad);
    }

    SECTION("duplicate road") {
        const auto state = DuneCity::makeCityTilePlacementState(true, false, false, false, true);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE(mutableState.hasRoad);
    }

    SECTION("existing city zone") {
        const auto state = DuneCity::makeCityTilePlacementState(true, false, false, true, false);
        REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
        auto mutableState = state;
        REQUIRE_FALSE(DuneCity::applyRoadPlacement(mutableState));
        REQUIRE_FALSE(mutableState.hasRoad);
    }
}
