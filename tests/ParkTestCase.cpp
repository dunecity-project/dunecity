// The Park structure (Micropolis park tool) and QuantBot's ParkDistricts switch: identity, city
// gating, land-value role, sprite frames, shipped data and the AI policy. The real-engine
// placement, land value and save/load checks are the `city-park` skirmish probe case.

#include <catch2/catch_test_macros.hpp>
#include <data.h>
#include <Definitions.h>
#include <Network/NetworkPacketTypes.h>
#include <dunecity/CityConstants.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CitySpritePolicy.h>
#include <dunecity/ParkTerrainPolicy.h>
#include <players/ParkDistrictPolicy.h>
#include <players/QuantBotConfig.h>
#include <FileClasses/lodepng.h>

#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace DuneCity;

TEST_CASE("Park: appended item 89, a city-only 1x1 structure", "[city][park][save-compat]") {
    STATIC_REQUIRE(Structure_Park == 89);
    STATIC_REQUIRE(ItemID_LastID == Structure_Park);
    STATIC_REQUIRE(Num_ItemID == 90);
    REQUIRE(isStructure(Structure_Park));
    REQUIRE_FALSE(isUnit(Structure_Park));
    REQUIRE_FALSE(isZoneStructure(Structure_Park));
    REQUIRE_FALSE(isTornieExclusiveItem(Structure_Park));
    REQUIRE(isCityOnlyStructure(Structure_Park));
    REQUIRE_FALSE(isCityZoneStructure(Structure_Park));
    // Configured timing (8), not the road's instant build.
    REQUIRE(getCityBuildTime(Structure_Park, 8) == 8);
    // The save and wire boundaries that carry the new item.
    STATIC_REQUIRE(SAVEGAMEVERSION_PARK == 9888);
    STATIC_REQUIRE(SAVEGAMEVERSION >= SAVEGAMEVERSION_PARK);
    STATIC_REQUIRE(NETWORK_PROTOCOL_VERSION >= 131);
}

TEST_CASE("Park: a +15 park-terrain source that does not develop its block", "[city-effects][park]") {
    REQUIRE(usesParkTerrain(Structure_Park));
    REQUIRE(getParkLandValueBonus(Structure_Park) == kParkLandValueBonus);
    REQUIRE(kParkLandValueBonus == 15);
    REQUIRE(getParkLandValueRadius(Structure_Park) == kParkTerrainSearchRadius);
    REQUIRE(getStructureCityRole(Structure_Park) == CityRole::None);
    REQUIRE(getPoliceCoverage(Structure_Park) == 0);
    REQUIRE_FALSE(developsLandValueBlock(Structure_Park));
    for (int item : {Structure_Wall, Structure_GunTurret, Structure_RocketTurret, Structure_ZoneResidential,
                     Structure_ZoneCommercial, Structure_Palace, Structure_Stadium})
        CHECK(developsLandValueBlock(item));

    // Micropolis smoothing: one park is +7 on its own 3x3 terrain block, +1 next door; parks stack.
    ParkTerrainPolicy terrain;
    terrain.init(18, 18);
    CHECK(terrain.marginalGain(7, 7, getParkLandValueBonus(Structure_Park), 6, 6) == 7);
    terrain.addSource(7, 7, getParkLandValueBonus(Structure_Park));
    CHECK(terrain.valueAt(6, 6) == 7);
    CHECK(terrain.valueAt(9, 7) == 1);
    CHECK(terrain.valueAt(12, 7) == 0);
    CHECK(terrain.marginalGain(8, 8, getParkLandValueBonus(Structure_Park), 6, 6) == 8);
}

TEST_CASE("Park: frames follow the site (woods or fountain), never the random generator", "[city][park][sprites]") {
    using namespace DuneCity::CitySprites;
    std::set<int> variants;
    int fountains = 0;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) {
            const int first = parkFrame(0, x, y);
            REQUIRE(first >= 0);
            REQUIRE(first < parkFrames);
            REQUIRE(parkFrame(0, x, y) == first); // stable
            if (first < parkFountainFrame) {
                variants.insert(first);
                // Woods never animate.
                for (uint32_t cycle : {frameCycles, 5 * frameCycles, 1000u * frameCycles})
                    REQUIRE(parkFrame(cycle, x, y) == first);
            } else {
                ++fountains;
                std::set<int> frames;
                for (uint32_t step = 0; step < 8; ++step) {
                    const int frame = parkFrame(step * frameCycles, x, y);
                    REQUIRE(frame >= parkFountainFrame);
                    REQUIRE(frame < parkFrames);
                    frames.insert(frame);
                }
                REQUIRE(frames.size() == 4); // the four FOUNTAIN tiles
            }
        }
    CHECK(variants == std::set<int>{0, 1, 2, 3});
    // Micropolis's getRandom(4): one park in five is a fountain.
    CHECK(fountains > 1600 / 8);
    CHECK(fountains < 1600 * 2 / 7);
}

TEST_CASE("Park: shipped atlas and ObjectData match the Micropolis tool", "[city][park][assets]") {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(root != nullptr);
    std::vector<unsigned char> rgba;
    unsigned w = 0, h = 0;
    REQUIRE(lodepng::decode(rgba, w, h, std::string(root) + "/imported_sprites/micropolis/atlases/park.png") == 0);
    REQUIRE(w == unsigned(CitySprites::parkFrames * 16));
    REQUIRE(h == 16u);
    for (size_t i = 3; i < rgba.size(); i += 4) REQUIRE(rgba[i] == 255); // opaque tiles

    std::ifstream file(std::string(root) + "/config/ObjectData.ini.default");
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    const std::string ini = text.str();
    const auto at = ini.find("\n[Park]\n");
    REQUIRE(at != std::string::npos);
    const std::string section = ini.substr(at, ini.find("\n[", at + 1) - at);
    CHECK(section.find("\nEnabled = true\n") != std::string::npos);
    CHECK(section.find("\nPrice = 10\n") != std::string::npos);      // gCostOf[TOOL_PARK]
    CHECK(section.find("\nPower = 0\n") != std::string::npos);
    CHECK(section.find("\nBuildTime = 8\n") != std::string::npos);
    CHECK(section.find("\nBuilder = Construction Yard\n") != std::string::npos);
    CHECK(section.find("\nPrerequisite") == std::string::npos);
}

TEST_CASE("ParkDistricts: a park per two R+C lots, only where it adds land value", "[ai][park]") {
    using namespace ParkDistrictPolicy;
    CHECK(parkCap(0) == 0);
    CHECK(parkCap(kMinLots - 1) == 0);
    CHECK(parkCap(kMinLots) == kMinLots / kLotsPerPark);
    CHECK(parkCap(30) == 15);
    CHECK(blockGain(100, 7) == 7);
    CHECK(blockGain(246, 7) == 4);          // never past the 250 ceiling
    CHECK(blockGain(kMaxLandValue, 7) == 0);
    CHECK(blockGain(100, -3) == 0);
    CHECK(wantsPark(10, 2, false, kMinGain));
    CHECK_FALSE(wantsPark(10, 2, true, 20));      // one at a time
    CHECK_FALSE(wantsPark(10, 5, false, 20));     // at the cap
    CHECK_FALSE(wantsPark(10, 2, false, kMinGain - 1));
    CHECK_FALSE(wantsPark(kMinLots - 1, 0, false, 20));
    CHECK(kLotReach == 5);
}

TEST_CASE("ParkDistricts: default off and outside the native config hash", "[ai][park][config]") {
    QuantBotConfig native;
    REQUIRE_FALSE(native.force.parkDistricts);
    QuantBotConfig parks = native;
    parks.force.parkDistricts = true;
    REQUIRE(parks.getConfigHash() != native.getConfigHash());
    parks.force.parkDistricts = false;
    REQUIRE(parks.getConfigHash() == native.getConfigHash());
}
