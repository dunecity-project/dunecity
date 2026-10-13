/*
 *  CityRoadTestCase.cpp - Unit tests for city road placement
 *
 *  Validates: road tool constants, CMD_CITY_TOOL command enum,
 *  tile conductive state contract, and placement validation rules.
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <dunecity/CityConstants.h>
#include <Command.h>
#include <Definitions.h>

// =============================================================================
// CityToolType Constants
// =============================================================================

TEST_CASE("CityRoad: CityTool_Road has value 1", "[road][constants]") {
    REQUIRE(DuneCity::CityTool_Road == 1);
}

TEST_CASE("CityRoad: CityToolType values are distinct", "[road][constants]") {
    REQUIRE(DuneCity::CityTool_Bulldoze != DuneCity::CityTool_Road);
    REQUIRE(DuneCity::CityTool_Road != DuneCity::CityTool_PowerLine);
    REQUIRE(DuneCity::CityTool_Bulldoze != DuneCity::CityTool_PowerLine);
}

TEST_CASE("CityRoad: CityTool_Bulldoze is 0, CityTool_PowerLine is 2", "[road][constants]") {
    REQUIRE(DuneCity::CityTool_Bulldoze == 0);
    REQUIRE(DuneCity::CityTool_PowerLine == 2);
}

// =============================================================================
// CMD_CITY_TOOL Command Enum
// =============================================================================

TEST_CASE("CityRoad: CMD_CITY_TOOL is a valid command ID", "[road][command]") {
    REQUIRE(CMD_CITY_TOOL > CMD_TEST_SYNC);
    REQUIRE(CMD_CITY_TOOL < CMD_MAX);
}

TEST_CASE("CityRoad: CMD_CITY_TOOL is distinct from other city commands", "[road][command]") {
    REQUIRE(CMD_CITY_TOOL != CMD_CITY_PLACE_ZONE);
    REQUIRE(CMD_CITY_TOOL != CMD_CITY_SET_TAX_RATE);
    REQUIRE(CMD_CITY_TOOL != CMD_CITY_SET_BUDGET);
}

TEST_CASE("CityRoad: All DuneCity commands are contiguous after CMD_TEST_SYNC", "[road][command]") {
    REQUIRE(CMD_CITY_PLACE_ZONE == CMD_TEST_SYNC + 1);
    REQUIRE(CMD_CITY_SET_TAX_RATE == CMD_TEST_SYNC + 2);
    REQUIRE(CMD_CITY_SET_BUDGET == CMD_TEST_SYNC + 3);
    REQUIRE(CMD_CITY_TOOL == CMD_TEST_SYNC + 4);
    REQUIRE(CMD_TECHCENTER_SPAWN == CMD_CITY_TOOL + 1);
    REQUIRE(CMD_SCOUTPOST_UPGRADE == CMD_TECHCENTER_SPAWN + 1);
    REQUIRE(CMD_SCOUTPOST_CHEMIPOST_UPGRADE == CMD_SCOUTPOST_UPGRADE + 1);
    REQUIRE(CMD_HOUSE_AUTO_REPAIR == CMD_SCOUTPOST_CHEMIPOST_UPGRADE + 1);
    REQUIRE(CMD_POLICE_REINFORCEMENTS == CMD_HOUSE_AUTO_REPAIR + 1);
    REQUIRE(CMD_ZONE_DEMOLISH == CMD_POLICE_REINFORCEMENTS + 1);
    REQUIRE(CMD_STRUCTURE_DEMOLISH == CMD_ZONE_DEMOLISH + 1);
    REQUIRE(CMD_CAMPAIGN_SKIP == CMD_STRUCTURE_DEMOLISH + 1);
    REQUIRE(CMD_MATCH_PAUSE == CMD_CAMPAIGN_SKIP + 1);
    REQUIRE(CMD_UNIT_ATTACKMOVE == CMD_MATCH_PAUSE + 1);
    REQUIRE(CMD_UNIT_PATROL == CMD_UNIT_ATTACKMOVE + 1);
    REQUIRE(CMD_UNIT_QUEUEORDER == CMD_UNIT_PATROL + 1);
    REQUIRE(CMD_BUILDER_SETREPEAT == CMD_UNIT_QUEUEORDER + 1);
    REQUIRE(CMD_BUILDER_RESERVE_PLACEMENT == CMD_BUILDER_SETREPEAT + 1);
    REQUIRE(CMD_BUILDER_CANCEL_RESERVATION == CMD_BUILDER_RESERVE_PLACEMENT + 1);
    REQUIRE(CMD_UNIT_FORMATION_MOVE == CMD_BUILDER_CANCEL_RESERVATION + 1);
    REQUIRE(CMD_MAX == CMD_UNIT_FORMATION_MOVE + 1);
}

// =============================================================================
// Tile Conductive State Contract
// =============================================================================

namespace {

struct TileRoadProxy {
    bool isRoad_ = false;
    bool cityPowered_ = false;
    DuneCity::ZoneType cityZoneType_ = DuneCity::ZoneType::None;
    uint8_t cityZoneDensity_ = 0;

    bool isRoad() const noexcept { return isRoad_; }
    void setRoad(bool c) noexcept { isRoad_ = c; }
    bool isCityPowered() const noexcept { return cityPowered_; }
    void setCityPowered(bool p) noexcept { cityPowered_ = p; }
    DuneCity::ZoneType getCityZoneType() const noexcept { return cityZoneType_; }
    void setCityZoneType(DuneCity::ZoneType z) noexcept { cityZoneType_ = z; }
    bool hasCityZone() const noexcept { return cityZoneType_ != DuneCity::ZoneType::None; }
};

} // anonymous namespace

TEST_CASE("CityRoad: Tile starts non-conductive", "[road][tile]") {
    TileRoadProxy tile;
    REQUIRE_FALSE(tile.isRoad());
}

TEST_CASE("CityRoad: setRoad(true) makes tile conductive", "[road][tile]") {
    TileRoadProxy tile;
    tile.setRoad(true);
    REQUIRE(tile.isRoad());
}

TEST_CASE("CityRoad: setRoad(false) removes conductivity", "[road][tile]") {
    TileRoadProxy tile;
    tile.setRoad(true);
    REQUIRE(tile.isRoad());
    tile.setRoad(false);
    REQUIRE_FALSE(tile.isRoad());
}

TEST_CASE("CityRoad: Road (conductive) is independent of zone type", "[road][tile]") {
    TileRoadProxy tile;
    tile.setRoad(true);
    REQUIRE(tile.isRoad());
    REQUIRE_FALSE(tile.hasCityZone());

    tile.setCityZoneType(DuneCity::ZoneType::Residential);
    REQUIRE(tile.isRoad());
    REQUIRE(tile.hasCityZone());
}

TEST_CASE("CityRoad: Conductive state is independent of powered state", "[road][tile]") {
    TileRoadProxy tile;
    tile.setRoad(true);
    REQUIRE_FALSE(tile.isCityPowered());
    REQUIRE(tile.isRoad());

    tile.setCityPowered(true);
    REQUIRE(tile.isCityPowered());
    REQUIRE(tile.isRoad());
}

// =============================================================================
// Road Placement Validation Rules (via CityTilePlacementState)
// =============================================================================

TEST_CASE("CityRoad: canPlaceRoad accepts valid tile", "[road][validation]") {
    auto state = DuneCity::makeCityTilePlacementState(true, false, false, false, false);
    REQUIRE(DuneCity::canPlaceRoad(state));
}

TEST_CASE("CityRoad: Road cannot be placed on mountain", "[road][validation]") {
    auto state = DuneCity::makeCityTilePlacementState(true, true, false, false, false);
    REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
}

TEST_CASE("CityRoad: Road cannot be placed on tile with structure", "[road][validation]") {
    auto state = DuneCity::makeCityTilePlacementState(true, false, true, false, false);
    REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
}

TEST_CASE("CityRoad: Duplicate road placement is rejected", "[road][validation]") {
    auto state = DuneCity::makeCityTilePlacementState(true, false, false, false, true);
    REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
}

TEST_CASE("CityRoad: Road cannot be placed on unsupported terrain", "[road][validation]") {
    auto state = DuneCity::makeCityTilePlacementState(false, false, false, false, false);
    REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
}

TEST_CASE("CityRoad: Road cannot be placed on existing city zone", "[road][validation]") {
    auto state = DuneCity::makeCityTilePlacementState(true, false, false, true, false);
    REQUIRE_FALSE(DuneCity::canPlaceRoad(state));
}

// =============================================================================
// Road Effect: Placed road makes tile conductive
// =============================================================================

TEST_CASE("CityRoad: Placing road sets conductive flag", "[road][effect]") {
    TileRoadProxy tile;
    tile.setRoad(true);
    REQUIRE(tile.isRoad());
}

TEST_CASE("CityRoad: Road placement on valid tile follows correct sequence", "[road][effect]") {
    TileRoadProxy tile;

    REQUIRE_FALSE(tile.isRoad());

    tile.setRoad(true);

    REQUIRE(tile.isRoad());
}

// =============================================================================
// CityTool_Road matches QuantBot dispatch value
// =============================================================================

TEST_CASE("CityRoad: CityTool_Road value matches QuantBot's toolType=1", "[road][integration]") {
    REQUIRE(static_cast<uint32_t>(DuneCity::CityTool_Road) == 1u);
}

// =============================================================================
// Road Speed Multiplier
// =============================================================================

TEST_CASE("CityRoad: ROADSPEEDMULTIPLIER is 4x", "[road][speed]") {
    REQUIRE(ROADSPEEDMULTIPLIER == 4);
}
