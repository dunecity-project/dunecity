#include <catch2/catch_all.hpp>
#include <Network/CustomGamePlayerPolicy.h>
#include <FileClasses/INIFile.h>
#include <INIMap/MapMetadata.h>

#include <cstdlib>
#include <sstream>
#include <numeric>
#include <vector>

using namespace CustomGamePlayerPolicy;

TEST_CASE("Tornie family alone enables twelve custom player rows", "[tornie][lobby][policy]") {
    REQUIRE(maxPlayers(false) == 9);
    REQUIRE(maxPlayers(true) == 12);
    REQUIRE(maxPlayers(true) <= MAX_CUSTOM_GAME_PLAYERS);
    REQUIRE(HOUSE_NATIVES == 12);
    REQUIRE(NUM_HOUSES == 13);
}

TEST_CASE("Family faction selection follows the edition roster and never selects Natives", "[tornie][houses][policy]") {
    std::vector<int> full(NUM_PLAYER_HOUSES);
    std::iota(full.begin(), full.end(), 0);
    const auto twelve = selectableHouses(true, true, full);
    REQUIRE(twelve.count == 12);
    for(const int house : full) REQUIRE(twelve.contains(house));
    REQUIRE_FALSE(twelve.contains(HOUSE_NATIVES));

    const std::vector<int> lite = {0, 1, 2, 3, 4, 5};
    const auto six = selectableHouses(true, true, lite);
    REQUIRE(six.count == 6);
    REQUIRE(six[0] == HOUSE_ATREIDES);
    REQUIRE(six[1] == HOUSE_ORDOS);
    REQUIRE(six[2] == HOUSE_HARKONNEN);
    for(const int house : lite) REQUIRE(six.contains(house));
    REQUIRE_FALSE(six.contains(HOUSE_CUSTOM));
    REQUIRE_FALSE(six.contains(HOUSE_WILDSPADE));
    REQUIRE_FALSE(six.contains(HOUSE_NATIVES));

    const auto derived = selectableHouses(true, false, {HOUSE_WILDSPADE, HOUSE_ATREIDES,
        HOUSE_WILDSPADE, HOUSE_NATIVES, HOUSE_INVALID, 99});
    REQUIRE(derived.count == 2);
    REQUIRE(derived[0] == HOUSE_ATREIDES);
    REQUIRE(derived[1] == HOUSE_WILDSPADE);
    REQUIRE(selectableHouses(true, true, {}).count == 0);
    REQUIRE_FALSE(isPlayableHouseID(HOUSE_NATIVES));
    REQUIRE_FALSE(isPlayableHouseID(HOUSE_INVALID));
    REQUIRE_FALSE(isPlayableHouseID(NUM_HOUSES));
}

TEST_CASE("Nonfamily house choices ignore Tornie edition declarations", "[tornie][houses][isolation]") {
    const std::vector<int> foreignRoster = {HOUSE_WILDSPADE, HOUSE_KLESHMERSH, HOUSE_NATIVES};
    const auto legacy = selectableHouses(false, false, foreignRoster);
    REQUIRE(legacy.count == 8);
    REQUIRE(legacy[0] == HOUSE_ATREIDES);
    REQUIRE(legacy[5] == HOUSE_SARDAUKAR);
    REQUIRE(legacy[7] == HOUSE_REBELS);
    REQUIRE_FALSE(legacy.contains(HOUSE_CUSTOM));
    REQUIRE_FALSE(legacy.contains(HOUSE_WILDSPADE));
    const auto custom = selectableHouses(false, true, foreignRoster);
    REQUIRE(custom.count == 9);
    REQUIRE(custom[8] == HOUSE_CUSTOM);
    REQUIRE_FALSE(custom.contains(HOUSE_NATIVES));
}

TEST_CASE("Bundled edition rosters drive six or twelve faction choices", "[tornie][houses][content]") {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    for(const char* mod : {"Tornie", "TornieLite", "Jericho", "JerichoLite", "ArcaKev", "ArcaKevLite"}) {
        INFO(mod);
        const std::string name = mod;
        INIFile ini(std::string(root ? root : ".") + "/mods/" + name + "/mod.ini");
        std::istringstream text(ini.getStringValue("Content Options", "Playable Houses", ""));
        std::vector<int> declared;
        for(std::string entry; std::getline(text, entry, ',');) declared.push_back(std::stoi(entry));
        const int expected = name.find("Lite") == std::string::npos ? 12 : 6;
        REQUIRE(declared.size() == static_cast<std::size_t>(expected));
        const auto choices = selectableHouses(true, expected == 12, declared);
        REQUIRE(choices.count == expected);
        for(int house = 0; house < expected; ++house) REQUIRE(choices.contains(house));
        REQUIRE_FALSE(choices.contains(HOUSE_NATIVES));
        REQUIRE(selectableHouses(false, false, declared).count == 8);
    }
}

TEST_CASE("Custom map metadata counts twelve family identities but not Natives", "[tornie][houses][map-catalogue]") {
    INIFile ini(false, std::string());
    for(const char* house : {"Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar", "Mercenary",
                            "Neutral", "Rebels", "Corruptique", "Wildspade", "Kleshmersh", "Tharpique", "Natives"})
        ini.setStringValue(house, "Brain", "CPU");
    REQUIRE(MapMetadata::countPlayers(ini) == 12);
    ini.setStringValue("Custom", "Brain", "CPU");
    REQUIRE(MapMetadata::countPlayers(ini) == 12);
}
