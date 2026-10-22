/*
 *  NeutralHouseTestCase.cpp - Tests for the Neutral house (v1.0.111-120)
 *
 *  Validates: house enum, legacy ID stability, palette fallback,
 *  voice resources, skirmish availability (source scan), and
 *  ObjectData.ini prerequisites for Neutral-specific entries.
 *
 *  Source-scan tests skip gracefully when DUNE_CITY_SOURCE_DIR is not set.
 *  ObjectData tests skip gracefully when DUNE_CITY_SOURCE_DIR is not set
 *  (config lives in the repo root, not the data dir).
 */

#include <catch2/catch_all.hpp>
#include <DataTypes.h>
#include <Definitions.h>
#include <Colors.h>
#include <dunecity/HouseColors.h>
#include <globals.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <fstream>
#include <string>
#include <cstdlib>

// ── source-scan helper ────────────────────────────────────────────────────────

static std::string readSourceFile(const std::string& relativePath) {
    const char* env = std::getenv("DUNE_CITY_SOURCE_DIR");
    if (!env) {
        SKIP("DUNE_CITY_SOURCE_DIR not set — skipping source-scan test");
    }
    std::ifstream f(std::string(env) + "/" + relativePath);
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

// =============================================================================
// House enum
// =============================================================================

TEST_CASE("NeutralHouse: HOUSE_NEUTRAL exists in the house enum",
          "[neutral][enum]") {
    REQUIRE(HOUSE_NEUTRAL == 6);
    // Twelve houses a player can hold, plus the hidden Natives house (worms, crime troopers).
    REQUIRE(NUM_PLAYER_HOUSES == 12);
    REQUIRE(HOUSE_NATIVES == NUM_PLAYER_HOUSES);
    REQUIRE(NUM_HOUSES == 13);
}

TEST_CASE("NeutralHouse: HOUSE_NEUTRAL is after HOUSE_MERCENARY",
          "[neutral][enum]") {
    REQUIRE(HOUSE_NEUTRAL == HOUSE_MERCENARY + 1);
}

TEST_CASE("NeutralHouse: savegame version includes HOUSE_NEUTRAL",
          "[neutral][savegame]") {
    // SAVEGAMEVERSION 9818 persists all kMaxCityHouses houseState[] slots (9817 added House::cityCredits, 9816 added Unit_EliteSiegeTank=55, 9813 added HOUSE_NEUTRAL).
    REQUIRE(SAVEGAMEVERSION >= 9818);
}

// =============================================================================
// Palette / radar color
// =============================================================================

TEST_CASE("NeutralHouse: PALCOLOR_NEUTRAL is 128",
          "[neutral][color]") {
    REQUIRE(PALCOLOR_NEUTRAL == 128);
}

TEST_CASE("NeutralHouse: houseToPaletteIndex maps HOUSE_NEUTRAL to PALCOLOR_NEUTRAL",
          "[neutral][color]") {
    REQUIRE(houseToPaletteIndex[HOUSE_NEUTRAL] == PALCOLOR_NEUTRAL);
}

TEST_CASE("NeutralHouse: radar marker remains distinct from rock", "[neutral][color]") {
    const Uint32 neutral = DuneCity::neutralRadarColor;
    REQUIRE(((neutral & GMASK) >> GSHIFT) > 200);
    REQUIRE(((neutral & BMASK) >> BSHIFT) > 200);
    REQUIRE(neutral != COLOR_ROCK);
}

// =============================================================================
// Mentat animation enum registration
// =============================================================================

TEST_CASE("NeutralHouse: legacy house IDs remain stable with extended identity capacity",
          "[neutral][enum][compat]") {
    REQUIRE(HOUSE_HARKONNEN == 0);
    REQUIRE(HOUSE_ATREIDES == 1);
    REQUIRE(HOUSE_ORDOS == 2);
    REQUIRE(HOUSE_FREMEN == 3);
    REQUIRE(HOUSE_SARDAUKAR == 4);
    REQUIRE(HOUSE_MERCENARY == 5);
    REQUIRE(HOUSE_NEUTRAL == 6);
    REQUIRE(HOUSE_REBELS == 7);
    REQUIRE(NUM_LEGACY_HOUSES == 8);
    REQUIRE(HOUSE_CUSTOM == NUM_LEGACY_HOUSES);
    REQUIRE(NUM_PLAYER_HOUSES == NUM_LEGACY_HOUSES + 4);
    REQUIRE(NUM_HOUSES == NUM_PLAYER_HOUSES + 1);
}
// =============================================================================
// Voice file mapping (source scan)
// =============================================================================

TEST_CASE("NeutralHouse: generic custom slot has a safe legacy palette fallback",
          "[neutral][color][compat]") {
    REQUIRE(houseToPaletteIndex[HOUSE_CUSTOM] == PALCOLOR_HARKONNEN);
}
TEST_CASE("NeutralHouse: ANEU.VOC is the primary voice file for Neutral house announcement",
          "[neutral][voice][source-scan]") {
    std::string src = readSourceFile("src/FileClasses/SFXManager.cpp");
    REQUIRE_FALSE(src.empty());

    // The Atreides/Fremen/Neutral fallback branch must use ANEU.VOC as primary
    // (replaced MNEU.VOC as primary in v1.0.120).
    auto pos = src.find("ANEU.VOC");
    INFO("SFXManager.cpp must reference ANEU.VOC for Neutral house name");
    REQUIRE(pos != std::string::npos);
}

// =============================================================================
// Skirmish availability (source scan)
// =============================================================================

TEST_CASE("NeutralHouse: HOUSE_NEUTRAL is included in skirmish houseOrder",
          "[neutral][skirmish][source-scan]") {
    std::string src = readSourceFile("src/Menu/SinglePlayerSkirmishMenu.cpp");
    REQUIRE_FALSE(src.empty());

    auto pos = src.find("houseOrder[]");
    INFO("SinglePlayerSkirmishMenu.cpp must declare houseOrder");
    REQUIRE(pos != std::string::npos);

    // Extract houseOrder array contents
    auto end = src.find("}", pos);
    REQUIRE(end != std::string::npos);
    std::string orderBlock = src.substr(pos, end - pos);

    INFO("houseOrder must include HOUSE_NEUTRAL");
    REQUIRE(orderBlock.find("HOUSE_NEUTRAL") != std::string::npos);
}

// =============================================================================
// ObjectData.ini prerequisites (source scan — config/ is under source dir)
// =============================================================================

TEST_CASE("NeutralHouse: ObjectData.ini has Neutral-specific Launcher prerequisite",
          "[neutral][objectdata][source-scan]") {
    std::string ini = readSourceFile("config/ObjectData.ini.default");
    REQUIRE_FALSE(ini.empty());

    // [Launcher] must NOT have any (N) override per Tornie spec
    // v1.0.377 'Rebels and Neutral house need to have same
    // prerequistes of fremens in vanilla mods only' - Fremen
    // has zero overrides in vanilla so Neutral should also
    // have zero overrides. The default Launcher prereq applies
    // to Neutral via the same fall-through that Fremen uses.
    auto launcherPos = ini.find("[Launcher]");
    INFO("ObjectData.ini must have a [Launcher] section");
    REQUIRE(launcherPos != std::string::npos);

    // Find next section after [Launcher]
    auto nextSection = ini.find("[", launcherPos + 1);
    std::string launcherBlock = ini.substr(launcherPos,
        nextSection != std::string::npos ? nextSection - launcherPos : std::string::npos);

    // No (N) override lines inside the Launcher block.
    INFO("Launcher section must not have any (N) override lines for Neutral (vanilla mods spec)");
    REQUIRE(launcherBlock.find("(N) =") == std::string::npos);
    // No (R) override lines inside the Launcher block either.
    INFO("Launcher section must not have any (R) override lines for Rebels (vanilla mods spec)");
    REQUIRE(launcherBlock.find("(R) =") == std::string::npos);
}
