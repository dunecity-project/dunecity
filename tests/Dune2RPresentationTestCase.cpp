#include <catch2/catch_test_macros.hpp>
#include <ScreenBorder.h>
#include <Tile.h>
#include <misc/DrawingRectHelper.h>
#include <misc/Dune2RInfantryOverlay.h>
#include <misc/EnhancedAnimationTimeline.h>
#include <misc/EnhancedBuildingGeometry.h>
#include <misc/EnhancedUnitGeometry.h>
#include <misc/OMemoryStream.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct RestoreZoom {
    int previous = currentZoomlevel;
    ~RestoreZoom() { currentZoomlevel = previous; }
};

std::string readSource(const std::string& relative) {
    const char* configured = std::getenv("DUNE_CITY_SOURCE_DIR");
    const auto root = configured ? std::filesystem::path(configured)
        : std::filesystem::path(__FILE__).parent_path().parent_path();
    std::ifstream input(root / relative);
    REQUIRE(input.good());
    return {(std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()};
}

std::string body(const std::string& source, const std::string& signature) {
    const auto start = source.find(signature);
    REQUIRE(start != std::string::npos);
    const auto brace = source.find('{', start);
    REQUIRE(brace != std::string::npos);
    int depth = 1;
    auto end = brace + 1;
    while(end < source.size() && depth > 0) {
        if(source[end] == '{') ++depth;
        if(source[end] == '}') --depth;
        ++end;
    }
    REQUIRE(depth == 0);
    return source.substr(brace + 1, end - brace - 2);
}

std::string withoutWhitespace(std::string source) {
    source.erase(std::remove_if(source.begin(), source.end(),
        [](unsigned char c) { return std::isspace(c) != 0; }), source.end());
    return source;
}

// This is the unchanged stream layout pinned against Tile::save below.
std::string legacyCorpseBytes(const DEADUNITTYPE& corpse) {
    OMemoryStream stream;
    stream.open();
    stream.writeUint8(corpse.type);
    stream.writeUint8(corpse.house);
    stream.writeBool(corpse.onSand);
    stream.writeSint32(corpse.realPos.x);
    stream.writeSint32(corpse.realPos.y);
    stream.writeSint16(corpse.timer);
    return {stream.getData(), stream.getDataLength()};
}
}

TEST_CASE("World presentation belongs only to the exact Dune2R enhanced switch", "[dune2r][presentation]") {
    CHECK(dune2rPresentationScale("Dune2R", true) == 3);
    CHECK(dune2rPresentationScale("Dune2R", false) == 1);
    for(const std::string mod : {"", "DuneLegacy", "dunecity", "DuneCity", "Tornie", "Dune2R-derived"}) {
        CHECK(dune2rPresentationScale(mod, true) == 1);
        CHECK(dune2rPresentationScale(mod, false) == 1);
    }
    CHECK(TILESIZE == 64);
    CHECK(D2_TILESIZE == 16);
}

TEST_CASE("Dune2R presentation preserves world camera and click coordinates independently of zoom", "[dune2r][presentation][camera]") {
    RestoreZoom restore;
    for(int zoom = 0; zoom < static_cast<int>(NUM_ZOOMLEVEL); ++zoom) {
        INFO(zoom);
        currentZoomlevel = zoom;
        ScreenBorder border({20, 60, 800, 600});
        border.adjustScreenBorderToMapsize(128, 128);
        border.setNewScreenCenter({4096, 4096});
        const Coord classicCenter = border.getCurrentCenter();
        border.setPresentationScale(3);
        CHECK(currentZoomlevel == zoom);
        CHECK(border.getCurrentCenter() == classicCenter);
        const int worldX = 4096 + TILESIZE / 2;
        const int worldY = 4096 + TILESIZE / 2;
        const int x = border.world2screenX(worldX);
        const int y = border.world2screenY(worldY);
        CHECK(std::abs(border.screen2worldX(x) - worldX) <= 1);
        CHECK(std::abs(border.screen2worldY(y) - worldY) <= 1);
        CHECK(border.screen2MapX(x) == worldX / TILESIZE);
        CHECK(border.screen2MapY(y) == worldY / TILESIZE);
        CHECK(border.isScreenCoordInsideMap(x, y));
        CHECK(border.world2screenX(worldX + TILESIZE) - x == D2_TILESIZE * (zoom + 1) * 3);
        // Source-atlas conversion is deliberately not changed by world presentation.
        CHECK(world2zoomedWorld(TILESIZE) == D2_TILESIZE * (zoom + 1));
        border.setPresentationScale(1);
        CHECK(border.getCurrentCenter() == classicCenter);
        CHECK(currentZoomlevel == zoom);
    }
}

TEST_CASE("Small Dune2R maps stay centered and reject clicks in their black margins", "[dune2r][presentation][camera]") {
    RestoreZoom restore;
    currentZoomlevel = 0;
    ScreenBorder border({20, 60, 800, 600});
    border.adjustScreenBorderToMapsize(4, 3);
    border.setNewScreenCenter({128, 96});
    border.setPresentationScale(3);
    CHECK(border.world2screenX(0) == 324);
    CHECK(border.world2screenY(0) == 288);
    CHECK(border.isScreenCoordInsideMap(324, 288));
    CHECK_FALSE(border.isScreenCoordInsideMap(323, 288));
    CHECK_FALSE(border.isScreenCoordInsideMap(516, 288));
    CHECK_FALSE(border.isScreenCoordInsideMap(324, 432));
    border.setNewScreenCenter({0, 0});
    CHECK(border.getCurrentCenter() == Coord(128, 96));
}

TEST_CASE("Classic fallback destination scales without changing source cells or subsequent UI", "[dune2r][presentation][atlas]") {
    auto* surface = SDL_CreateRGBSurfaceWithFormat(0, 64, 32, 32, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface != nullptr);
    const auto source = calcSpriteSourceRect(surface, 2, 4, 1, 2);
    const auto classic = calcSpriteDrawingRect(surface, 100, 120, 4, 2, HAlign::Center, VAlign::Center);
    CHECK(classic.w == 16);
    {
        Dune2RWorldDrawingScope scope(3);
        const auto enhanced = calcSpriteDrawingRect(surface, 100, 120, 4, 2, HAlign::Center, VAlign::Center);
        CHECK(enhanced.w == 48);
        CHECK(enhanced.h == 48);
        CHECK(enhanced.x == 76);
        CHECK(enhanced.y == 96);
        const auto unchanged = calcSpriteSourceRect(surface, 2, 4, 1, 2);
        CHECK(unchanged.x == source.x);
        CHECK(unchanged.y == source.y);
        CHECK(unchanged.w == source.w);
        CHECK(unchanged.h == source.h);
        {
            Dune2RWorldDrawingScope ui(1);
            CHECK(calcDrawingRect(surface, 0, 0).w == 64);
        }
        CHECK(dune2rWorldDrawingScale == 3);
        scope.reset();
        CHECK(calcDrawingRect(surface, 0, 0).w == 64);
    }
    CHECK(dune2rWorldDrawingScale == 1);
    SDL_FreeSurface(surface);
}

TEST_CASE("HQ unit size is authored once while building footprint follows the Dune2R world", "[dune2r][presentation][geometry]") {
    Dune2RWorldDrawingScope world(3);
    for(unsigned int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
        const auto tank = calcEnhancedUnitDrawingRect({48, 48}, zoom, 1.0, {192, 192}, {96, 96}, {300, 400});
        const auto ordos = calcEnhancedUnitDrawingRect({48, 48}, zoom, 1.21, {192, 192}, {96, 96}, {300, 400});
        const auto infantry = calcEnhancedUnitDrawingRect({40, 40}, zoom, 1.0, {512, 512}, {256, 256}, {300, 400});
        CHECK(tank.w == 48 * static_cast<int>(zoom + 1));
        CHECK(ordos.w == static_cast<int>(std::lround(58.08 * (zoom + 1))));
        CHECK(infantry.w == 40 * static_cast<int>(zoom + 1));
        const auto defaultBuilding = calcEnhancedBuildingDrawingRect(3, zoom, {576, 603}, {288, 603}, {300, 400});
        const auto remasteredBuilding = calcEnhancedBuildingDrawingRect(3, zoom, {576, 603}, {288, 603}, {300, 400}, 3);
        CHECK(defaultBuilding.w == 48 * static_cast<int>(zoom + 1));
        CHECK(remasteredBuilding.w == defaultBuilding.w * 3);
        CHECK(remasteredBuilding.y + remasteredBuilding.h == 400);
        const auto higherResolution = calcEnhancedBuildingDrawingRect(3, zoom, {1152, 1206}, {576, 1206}, {300, 400}, 3);
        CHECK(higherResolution.x == remasteredBuilding.x);
        CHECK(higherResolution.y == remasteredBuilding.y);
        CHECK(higherResolution.h == remasteredBuilding.h);
    }
}

TEST_CASE("Variable infantry pose timings preserve loop and one-shot boundaries", "[dune2r][infantry][animation]") {
    const auto durations = parseEnhancedFrameDurations("30,60,120", 3);
    CHECK(enhancedAnimationDuration(3, 50, durations) == 210);
    CHECK(enhancedAnimationFrame(29, 3, 50, true, durations) == 0);
    CHECK(enhancedAnimationFrame(30, 3, 50, true, durations) == 1);
    CHECK(enhancedAnimationFrame(89, 3, 50, true, durations) == 1);
    CHECK(enhancedAnimationFrame(90, 3, 50, true, durations) == 2);
    CHECK(enhancedAnimationFrame(210, 3, 50, true, durations) == 0);
    CHECK(enhancedAnimationFrame(210, 3, 50, false, durations) == 2);
    CHECK(enhancedAnimationFrame(5000, 3, 50, false, durations) == 2);
    CHECK(enhancedAnimationDuration(3, 50, {}) == 150);
    CHECK(enhancedAnimationFrame(150, 3, 50, true, {}) == 0);
    CHECK(parseEnhancedFrameDurations("", 3).empty());
    for(const std::string invalid : {"30,60", "30,60,120,40", "30,0,120", "30,60001,120", "30,60,", "30,-1,120", "30,x,120"}) {
        CHECK_THROWS(parseEnhancedFrameDurations(invalid, 3));
    }
}

TEST_CASE("Infantry fall history is bounded local state and cannot confuse another corpse", "[dune2r][infantry][isolation]") {
    Dune2RInfantryOverlay cache;
    CHECK(cache.record(100, -1, 0) == 0);
    CHECK(cache.record(100, 8, 0) == 0);
    CHECK(cache.record(100, 0, 1) == 0);
    const auto first = cache.record(100, 3, 0);
    const auto second = cache.record(100, 6, 0);
    REQUIRE(first != second);
    REQUIRE(cache.find(first, 101, 0));
    CHECK(cache.find(first, 101, 0)->direction == 3);
    CHECK(cache.find(first, 101, 1) == nullptr);
    CHECK(cache.find(0, 101, 0) == nullptr);
    cache.clear();
    const auto nextMap = cache.record(0, 2, 0);
    CHECK(nextMap != first);
    CHECK(cache.find(first, 0, 0) == nullptr);
    for(std::size_t i = 0; i <= Dune2RInfantryOverlay::capacity; ++i) cache.record(10, 0, 0);
    CHECK(cache.size() == Dune2RInfantryOverlay::capacity);
    CHECK(cache.find(nextMap, 10, 0) == nullptr);
    CHECK(cache.find(second, 10 + Dune2RInfantryOverlay::lifetimeMs + 1, 0) == nullptr);
    CHECK(cache.size() == 0);
    cache.record(5000, 0, 0);
    cache.find(0, 0, 0);
    CHECK(cache.size() == 0);
}

TEST_CASE("Cosmetic death token does not enter save bytes, state digests, RNG or commands", "[dune2r][infantry][isolation][save-compat]") {
    DEADUNITTYPE corpse{};
    CHECK(corpse.dune2rVisualToken == 0);
    corpse.type = DeadUnit_Infantry;
    corpse.house = HOUSE_HARKONNEN;
    corpse.onSand = true;
    corpse.realPos = {193, 271};
    corpse.timer = 2000;
    const auto classicBytes = legacyCorpseBytes(corpse);
    CHECK(classicBytes.size() == 13);
    for(const std::string mod : {"DuneLegacy", "Tornie", "dunecity", "Dune2R"}) {
        INFO(mod);
        corpse.dune2rVisualToken = mod == "Dune2R" ? 1729 : 0;
        CHECK(legacyCorpseBytes(corpse) == classicBytes);
    }
    const auto tile = readSource("src/Tile.cpp");
    const auto savedCorpse = withoutWhitespace(body(body(tile, "void Tile::save("), "for (const auto& deadUnit : deadUnits)"));
    CHECK(savedCorpse == "stream.writeUint8(deadUnit.type);stream.writeUint8(deadUnit.house);stream.writeBool(deadUnit.onSand);stream.writeSint32(deadUnit.realPos.x);stream.writeSint32(deadUnit.realPos.y);stream.writeSint16(deadUnit.timer);");
    CHECK(body(tile, "void Tile::save(").find("dune2rVisualToken") == std::string::npos);
    CHECK(body(tile, "void Tile::load(").find("dune2rVisualToken") == std::string::npos);
    CHECK(body(readSource("src/Game.cpp"), "GameStateDigest::Digest Game::computeStateDigest()").find("dune2r") == std::string::npos);
    const auto gfx = readSource("src/FileClasses/GFXManager.cpp");
    const auto record = body(gfx, "Uint32 GFXManager::recordEnhancedInfantryFall(");
    CHECK(record.find("itemID != Unit_Soldier") != std::string::npos);
    CHECK(record.find("house != HOUSE_HARKONNEN") != std::string::npos);
    CHECK(record.find("getActiveModName() != \"Dune2R\"") != std::string::npos);
    CHECK(record.find("randomGen") == std::string::npos);
    CHECK(record.find("sendCommand") == std::string::npos);
    CHECK(body(gfx, "void GFXManager::invalidateAllSpriteTextures()").find("enhancedInfantryOverlay.clear()") != std::string::npos);
    CHECK(body(gfx, "void GFXManager::invalidateEnhancedUnitMountsIfChanged(").find("enhancedInfantryOverlay.clear()") != std::string::npos);
    const auto destroyed = body(readSource("src/units/InfantryBase.cpp"), "void InfantryBase::destroy()");
    CHECK(destroyed.find("recordEnhancedInfantryFall") < destroyed.find("GroundUnit::destroy();"));
    CHECK(destroyed.find("GroundUnit::destroy();") != std::string::npos);
    CHECK(destroyed.find("currentGame->randomGen.randBool()") != std::string::npos);
    CHECK(destroyed.find("getRandomOf({Sound_Scream1") != std::string::npos);
    CHECK(destroyed.find("DamageExploded") == std::string::npos);
    CHECK(destroyed.find("getEnhancedUnitAnimationDuration") == std::string::npos);
}
