// SPDX-License-Identifier: GPL-2.0-or-later
#include <catch2/catch_all.hpp>

#include <FileClasses/INIFile.h>
#include <GameInitSettings.h>
#include <MapSeed.h>
#include <misc/FileSystem.h>
#include <misc/ReworkedCampaign.h>

#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace {
std::string mapDirectory() {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    return std::string(root ? root : ".") + "/data/maps/singleplayer/";
}

std::pair<int, int> structureSize(const std::string& type) {
    static const std::map<std::string, std::pair<int, int>> sizes = {
        {"Const Yard", {2, 2}}, {"Windtrap", {2, 2}}, {"Refinery", {3, 2}}, {"Spice Silo", {2, 2}},
        {"Outpost", {2, 2}}, {"Light Fctry", {2, 2}}, {"Heavy Fctry", {3, 2}}, {"Hi-Tech", {3, 2}},
        {"Repair", {3, 2}}, {"WOR", {2, 2}}, {"Barracks", {2, 2}}, {"Palace", {3, 3}}, {"IX", {2, 2}},
        {"Starport", {3, 3}}, {"Turret", {1, 1}}, {"R-Turret", {1, 1}}};
    const auto it = sizes.find(type);
    return it == sizes.end() ? std::make_pair(0, 0) : it->second;
}

std::string split(const std::string& value, int field) {
    size_t start = 0;
    for(int i = 0; i < field; ++i) {
        start = value.find(',', start);
        if(start == std::string::npos) return {};
        ++start;
    }
    const size_t end = value.find(',', start);
    auto text = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
    while(!text.empty() && text.front() == ' ') text.erase(text.begin());
    while(!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}
} // namespace

TEST_CASE("Reworked campaign missions are named by the RWRK_ prefix", "[campaign][reworked]") {
    REQUIRE(ReworkedCampaign::isReworkedFilename("RWRK_SCENA012.INI"));
    REQUIRE(ReworkedCampaign::isReworkedFilename("rwrk_scena012.ini"));
    REQUIRE_FALSE(ReworkedCampaign::isReworkedFilename("SCENA012.INI"));
    REQUIRE_FALSE(ReworkedCampaign::isReworkedFilename("maps/singleplayer/RWRK_SCENA012.INI"));
    REQUIRE_FALSE(ReworkedCampaign::isReworkedFilename("RWRK"));
    REQUIRE(ReworkedCampaign::toReworked("SCENA012.INI") == "RWRK_SCENA012.INI");
    REQUIRE(ReworkedCampaign::toReworked("RWRK_SCENA012.INI") == "RWRK_SCENA012.INI");
    REQUIRE(ReworkedCampaign::scenarioFilename('A', 7) == "RWRK_SCENA007.INI");
    REQUIRE(ReworkedCampaign::scenarioFilename('A', 22) == "RWRK_SCENA022.INI");
}

TEST_CASE("A house offers the reworked campaign only with all 22 missions", "[campaign][reworked]") {
    std::set<std::string> files;
    for(int mission = 1; mission <= 22; ++mission) files.insert(ReworkedCampaign::scenarioFilename('A', mission));
    const auto exists = [&](const std::string& name) { return files.count(name) > 0; };
    REQUIRE(ReworkedCampaign::isCompleteSet('A', exists));
    REQUIRE_FALSE(ReworkedCampaign::isCompleteSet('H', exists));
    REQUIRE_FALSE(ReworkedCampaign::isCompleteSet('?', exists));
    files.erase("RWRK_SCENA015.INI");
    REQUIRE_FALSE(ReworkedCampaign::isCompleteSet('A', exists));
}

TEST_CASE("A reworked campaign continues and retries on reworked maps", "[campaign][reworked]") {
    GameInitSettings original(HOUSE_ATREIDES, SettingsClass::GameOptionsClass{}, 4);
    REQUIRE(original.getFilename() == "SCENA008.INI");
    REQUIRE_FALSE(original.isReworkedCampaign());
    REQUIRE(GameInitSettings(original, 11, 0, 0).getFilename() == "SCENA011.INI");

    GameInitSettings reworked(HOUSE_ATREIDES, SettingsClass::GameOptionsClass{}, 4);
    reworked.useReworkedMaps();
    REQUIRE(reworked.getFilename() == "RWRK_SCENA008.INI");
    REQUIRE(reworked.isReworkedCampaign());
    const GameInitSettings next(reworked, 12, 1u << 5, 0);
    REQUIRE(next.getFilename() == "RWRK_SCENA012.INI");
    REQUIRE(next.isReworkedCampaign());
    REQUIRE(next.getMission() == 12);
    REQUIRE(GameInitSettings(next, 22, 0, 0).getFilename() == "RWRK_SCENA022.INI");

    // A single mission (the Skirmish entry of the campaign screen) uses the same maps.
    GameInitSettings single(HOUSE_ATREIDES, 17, SettingsClass::GameOptionsClass{});
    single.useReworkedMaps();
    REQUIRE(single.getFilename() == "RWRK_SCENA017.INI");
    REQUIRE(single.isReworkedCampaign());

    // Custom games may open an RWRK_ map by path; that is not a reworked campaign.
    GameInitSettings custom(mapDirectory() + "RWRK_SCENA012.INI", "", false, SettingsClass::GameOptionsClass{});
    REQUIRE_FALSE(custom.isReworkedCampaign());
    REQUIRE_THROWS_AS(custom.useReworkedMaps(), std::invalid_argument);
}

TEST_CASE("Every Atreides mission has a playable reworked map", "[campaign][reworked][data]") {
    const int mission = GENERATE(range(1, 23));
    const std::string name = ReworkedCampaign::scenarioFilename('A', mission);
    INFO(name);
    std::string path = mapDirectory() + name;
    REQUIRE(getCaseInsensitiveFilename(path));
    INIFile ini(path);

    REQUIRE(ini.getStringValue("Atreides", "Brain") == "Human");
    int computerHouses = 0;
    for(const char* house : {"Harkonnen", "Ordos", "Sardaukar", "Mercenary", "Fremen"}) {
        if(ini.hasSection(house) && ini.getStringValue(house, "Brain") == "CPU") ++computerHouses;
    }
    REQUIRE(computerHouses >= 1);

    const int scale = ini.getIntValue("BASIC", "MapScale", 0);
    const auto map = createMapWithSeed(static_cast<Uint32>(ini.getIntValue("MAP", "Seed", -1)), scale);
    const int offset = scale == 0 ? 1 : (scale == 1 ? 16 : 11);

    bool atreidesYard = false;
    std::set<std::pair<int, int>> occupied;
    for(const auto& key : ini.getSection("STRUCTURES")) {
        if(key.getKeyName().compare(0, 3, "GEN") == 0) continue; // concrete and walls
        const std::string value = key.getStringValue();
        const std::string house = split(value, 0), type = split(value, 1);
        const int position = std::stoi(split(value, 3));
        INFO(key.getKeyName() << "=" << value);
        const auto [w, h] = structureSize(type);
        REQUIRE(w > 0);
        if(house == "Atreides" && type == "Const Yard") atreidesYard = true;
        for(int dy = 0; dy < h; ++dy) {
            for(int dx = 0; dx < w; ++dx) {
                const int x = position % 64 + dx - offset, y = position / 64 + dy - offset;
                REQUIRE(x >= 0);
                REQUIRE(y >= 0);
                REQUIRE(x < map.getSizeX());
                REQUIRE(y < map.getSizeY());
                REQUIRE(map(x, y) == Terrain_Rock);
                REQUIRE(occupied.insert({x, y}).second);
            }
        }
    }
    REQUIRE(atreidesYard);
}
