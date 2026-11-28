// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
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

TEST_CASE("Mission objectives: briefing lines", "[MissionObjectives]") {
    Briefing b;
    b.targets = targets({{Population, 20000}});
    b.targets.holdSeconds = 60;
    b.conquestWins = true;
    b.firstAttackSeconds = 118;
    CHECK(briefingLines(b) == std::vector<std::string>{
        "Goal: 20,000 people, held for 1 minute.",
        "Or destroy every enemy house.",
        "Enemy attacks can begin at minute 2."});

    b = Briefing{};
    b.targets = targets({{Population, 75000}, {Crime, 63}});
    b.targets.holdSeconds = 480;
    CHECK(briefingLines(b).front() == "Goal: 75,000 people and crime at most 63 (Safe), held for 8 minutes.");

    // A survival mission: holding out is the goal, conquest the alternative.
    b = Briefing{};
    b.deadlineSeconds = 30 * 60;
    b.deadlineWins = true;
    b.conquestWins = true;
    b.firstAttackSeconds = 20;
    CHECK(briefingLines(b) == std::vector<std::string>{
        "Goal: hold out for 30 minutes.",
        "Or destroy every enemy house.",
        "Enemy attacks can begin within the first minute."});

    // Conquest only; a deadline that loses is a time limit, not a goal.
    b = Briefing{};
    b.conquestWins = true;
    b.deadlineSeconds = 45 * 60;
    CHECK(briefingLines(b) == std::vector<std::string>{"Goal: destroy every enemy house.", "Time limit: 45 minutes."});

    // DefeatEnemies as a goal: no separate conquest alternative.
    b = Briefing{};
    b.targets = targets({{Credits, 30000}, {DefeatEnemies, 1}});
    b.targets.holdSeconds = 0;
    b.conquestWins = true;
    CHECK(briefingLines(b) == std::vector<std::string>{"Goal: 30,000 credits in the treasury and every enemy house destroyed."});
    CHECK(briefingLines(Briefing{}).empty());
}

TEST_CASE("DuneCity campaign: each win colours its share of the planet", "[DuneCityCampaign]") {
    for(const int regions : {27, 10, 31}) {
        int next = 0;
        for(int mission = 1; mission <= DuneCityCampaign::kMissions; ++mission) {
            const auto [first, last] = DuneCityCampaign::conquestRange(mission, regions);
            CHECK(first == next);
            CHECK(last > first);
            next = last;
        }
        CHECK(next == regions);
    }
    CHECK(DuneCityCampaign::conquestRange(1, 27) == std::pair<int, int>{0, 2});
    CHECK(DuneCityCampaign::conquestRange(10, 27) == std::pair<int, int>{24, 27});
}

TEST_CASE("DuneCity campaign maps name their mission and brief its goals", "[DuneCityCampaign]") {
    for(int mission = 1; mission <= DuneCityCampaign::kMissions; ++mission) {
        const std::string name = DuneCityCampaign::scenarioFilename(mission);
        INFO(name);
        INIFile ini(mapDirectory() + name);
        CHECK_FALSE(ini.getStringValue("BRIEFING", "Name").empty());
        for(const char* difficulty : {"Easy", "Medium", "Hard", "Brutal"}) {
            const auto lines = briefingLines(briefingFromINI(ini, difficulty));
            REQUIRE_FALSE(lines.empty());
            CHECK(lines.front().rfind("Goal: ", 0) == 0);
        }
    }
    // Sihaya Basin on Medium: FirstEnemyAttack=10 at strength 5 is a first attack at minute 2.
    INIFile ini(mapDirectory() + DuneCityCampaign::scenarioFilename(1));
    CHECK(briefingFromINI(ini, "Medium").firstAttackSeconds == 120);
    CHECK(briefingFromINI(ini, "").firstAttackSeconds == 600);
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
    CHECK(strengthPermille(2.5) == 2500);
    CHECK(strengthPermille(0.1) == 250);
    CHECK(strengthPermille(40.0) == 10000);
}

TEST_CASE("DuneCity campaign: QuantBot Easiest never starts stronger than Easy", "[DuneCityCampaign]") {
    using namespace MissionObjectivePolicy;
    for(int mission = 1; mission <= DuneCityCampaign::kMissions; mission++) {
        const std::string name = DuneCityCampaign::scenarioFilename(mission);
        INFO(name);
        INIFile ini(mapDirectory() + name);
        const int easy = enemyStrengthPermille(ini, "Easy");
        const int easiest = enemyStrengthPermille(ini, "Easiest");
        // As authored (1.0), or Easy's value where that is lower still (mission 6: 0.85).
        CHECK(easiest == std::min(1000, easy));
        CHECK(easiest <= easy);
    }
    // Other labels without a value still play the map as authored.
    INIFile ini(mapDirectory() + DuneCityCampaign::scenarioFilename(6));
    CHECK(enemyStrengthPermille(ini, "Easy") == 850);
    CHECK(enemyStrengthPermille(ini, "Easiest") == 850);
    CHECK(enemyStrengthPermille(ini, "Defend") == 1000);
    CHECK(enemyStrengthPermille(ini, "") == 1000);
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
        // [DIFFICULTY]: an enemy strength per QuantBot difficulty. Not monotone: on the campaign
        // skill ladder a lower tier thinks more slowly (QuantBotCityCampaignSkills.h), so its houses
        // may need more starting credits than a higher tier's for the same fight.
        if(ini.hasSection("DIFFICULTY")) {
            for(const char* difficulty : {"Easy", "Medium", "Hard", "Brutal"}) {
                REQUIRE(ini.hasKey("DIFFICULTY", difficulty));
                const double value = ini.getDoubleValue("DIFFICULTY", difficulty, 0.0);
                REQUIRE(value >= 0.25);
                REQUIRE(value <= 10.0);
                REQUIRE(MissionObjectivePolicy::strengthPermille(value) == int(value * 1000 + 0.5));
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
