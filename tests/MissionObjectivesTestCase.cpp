// SPDX-License-Identifier: GPL-2.0-or-later
#include <catch2/catch_all.hpp>

#include <FileClasses/INIFile.h>
#include <MissionObjectives.h>
#include <misc/DuneCityCampaign.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

using namespace MissionObjectivePolicy;

namespace {
std::string mapDirectory() {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    return std::string(root ? root : ".") + "/data/maps/singleplayer/";
}

Targets targets(std::initializer_list<std::pair<Goal, int>> goals) {
    Targets t;
    for(const auto& [goal, value] : goals) {
        t.used[goal] = true;
        t.value[goal] = value;
    }
    return t;
}
} // namespace

TEST_CASE("Mission objectives: every listed goal must hold", "[MissionObjectives]") {
    const auto t = targets({{Population, 8000}, {Crime, 63}});
    Metrics m;
    m.population = 9000;
    m.sampled = true;
    m.crime = 40;
    REQUIRE(allMet(t, m));
    m.crime = 64;
    REQUIRE_FALSE(allMet(t, m));
    m.crime = 40;
    m.population = 7999;
    REQUIRE_FALSE(allMet(t, m));
}

TEST_CASE("Mission objectives: an empty city never passes a crime or land value goal", "[MissionObjectives]") {
    Metrics m; // nothing sampled: crime 0 would otherwise look Safe
    REQUIRE_FALSE(met(targets({{Crime, 63}}), m, Crime));
    REQUIRE_FALSE(met(targets({{Pollution, 50}}), m, Pollution));
    REQUIRE_FALSE(met(targets({{LandValue, 10}}), m, LandValue));
    m.sampled = true;
    m.landValue = 10;
    REQUIRE(met(targets({{LandValue, 10}}), m, LandValue));
}

TEST_CASE("Mission objectives: no section means no city victory", "[MissionObjectives]") {
    Targets none;
    Metrics m;
    m.population = 1000000;
    REQUIRE_FALSE(none.any());
    REQUIRE_FALSE(allMet(none, m));
}

TEST_CASE("Mission objectives: goals must hold without a break", "[MissionObjectives]") {
    Uint32 since = INVALID_GAMECYCLE;
    since = holdingSince(true, since, 100);
    REQUIRE(since == 100);
    since = holdingSince(true, since, 132);
    REQUIRE(since == 100);
    REQUIRE_FALSE(held(since, 132, 64));
    REQUIRE(held(since, 164, 64));
    since = holdingSince(false, since, 196);
    REQUIRE(since == INVALID_GAMECYCLE);
    REQUIRE_FALSE(held(since, 1000, 0));
}

TEST_CASE("Mission objectives: defeat enemies counts surviving rivals", "[MissionObjectives]") {
    const auto t = targets({{DefeatEnemies, 1}, {Credits, 500}});
    Metrics m;
    m.credits = 600;
    m.enemiesLeft = 1;
    REQUIRE_FALSE(allMet(t, m));
    m.enemiesLeft = 0;
    REQUIRE(allMet(t, m));
}

TEST_CASE("Mission objectives: grouped numbers", "[MissionObjectives]") {
    REQUIRE(groupedNumber(0) == "0");
    REQUIRE(groupedNumber(999) == "999");
    REQUIRE(groupedNumber(8000) == "8,000");
    REQUIRE(groupedNumber(1234567) == "1,234,567");
    REQUIRE(groupedNumber(-20000) == "-20,000");
}

TEST_CASE("DuneCity campaign: enemy strength by difficulty", "[DuneCityCampaign]") {
    using namespace MissionObjectivePolicy;
    CHECK(scaledCredits(5000, 1000) == 5000);
    CHECK(scaledCredits(5000, 2500) == 12500);
    CHECK(scaledCredits(4750, 1300) == 6200);   // 6175 rounds to 100 credits
    CHECK(scaledCredits(0, 3000) == 0);
    CHECK(scaledGrace(37500, 2000) == 18750);
    CHECK(scaledGrace(37500, 2500) == 15000);
    CHECK(scaledGrace(1000, 0) == 1000);
}

TEST_CASE("DuneCity campaign filenames", "[DuneCityCampaign]") {
    REQUIRE(DuneCityCampaign::scenarioFilename(1) == "DCITY_M01.INI");
    REQUIRE(DuneCityCampaign::scenarioFilename(10) == "DCITY_M10.INI");
    REQUIRE(DuneCityCampaign::isCampaignFilename("DCITY_M07.INI"));
    REQUIRE(DuneCityCampaign::isCampaignFilename("dcity_m07.ini"));
    REQUIRE_FALSE(DuneCityCampaign::isCampaignFilename("maps/DCITY_M07.INI"));
    REQUIRE_FALSE(DuneCityCampaign::isCampaignFilename("RWRK_SCENA001.INI"));
    REQUIRE_FALSE(DuneCityCampaign::isCampaignFilename("SCENA001.INI"));
}

TEST_CASE("DuneCity campaign maps carry a briefing, valid goals and in-bounds breaches", "[DuneCityCampaign]") {
    for(int mission = 1; mission <= DuneCityCampaign::kMissions; ++mission) {
        const std::string name = DuneCityCampaign::scenarioFilename(mission);
        INFO(name);
        INIFile ini(mapDirectory() + name);
        REQUIRE(ini.hasSection("MAP"));
        REQUIRE_FALSE(ini.getStringValue("BRIEFING", "Text").empty());
        REQUIRE_FALSE(ini.getStringValue("BRIEFING", "Win").empty());
        REQUIRE_FALSE(ini.getStringValue("BRIEFING", "Lose").empty());
        REQUIRE(ini.hasSection("Atreides"));
        // [DIFFICULTY]: an enemy strength per QuantBot difficulty, rising with the difficulty.
        if(ini.hasSection("DIFFICULTY")) {
            int previous = 0;
            for(const char* difficulty : {"Easy", "Medium", "Hard", "Brutal"}) {
                const int permille = MissionObjectivePolicy::enemyStrengthPermille(ini, difficulty);
                REQUIRE(permille >= 250);
                REQUIRE(permille <= 10000);
                REQUIRE(permille >= previous);
                previous = permille;
            }
        }
        // Bit 0x10 is meaningless (protocol 85); city goals only come from [OBJECTIVES].
        REQUIRE((ini.getIntValue("BASIC", "WinFlags", 3) & 0x10) == 0);
        const int width = ini.getIntValue("MAP", "SizeX"), height = ini.getIntValue("MAP", "SizeY");
        if(ini.hasSection("EVENTS")) {
            for(const auto& key : ini.getSection("EVENTS")) {
                const std::string keyName = key.getKeyName();
                if(keyName == "FirstEnemyAttack") {
                    const int minutes = key.getIntValue(-1);
                    REQUIRE(minutes >= 0);
                    REQUIRE(minutes <= 120);
                    continue;
                }
                if(keyName.rfind("Breach", 0) != 0 || keyName.find("Message") != std::string::npos) continue;
                std::vector<int> v;
                std::stringstream in(key.getStringValue());
                std::string field;
                while(std::getline(in, field, ',')) v.push_back(std::stoi(field));
                REQUIRE(v.size() >= 5);
                REQUIRE(v[1] >= 0);
                REQUIRE(v[2] >= 0);
                REQUIRE(v[1] + v[3] <= width);
                REQUIRE(v[2] + v[4] <= height);
                // A breach must open something: at least one mountain tile in its rectangle.
                int mountains = 0;
                for(int y = v[2]; y < v[2] + v[4]; ++y) {
                    char row[8];
                    std::snprintf(row, sizeof(row), "%03d", y);
                    const std::string line = ini.getStringValue("MAP", row);
                    for(int x = v[1]; x < v[1] + v[3] && x < static_cast<int>(line.size()); ++x) mountains += line[x] == '@';
                }
                REQUIRE(mountains > 0);
            }
        }
    }
}
