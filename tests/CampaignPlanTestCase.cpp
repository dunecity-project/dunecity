#include <catch2/catch_all.hpp>
#include <Campaign/CampaignPlan.h>

#include <cstdlib>
#include <string>

namespace {
std::string sourceRoot() {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    return root ? root : ".";
}

int opponentCount(const std::array<std::string, 4>& opponents) {
    return static_cast<int>(std::count_if(opponents.begin(), opponents.end(),
        [](const auto& name) { return !name.empty(); }));
}
}

TEST_CASE("Campaign plans retain template roles and stage-specific persistent support", "[tornie][campaign][plan]") {
    INIFile ini(false, std::string());
    ini.setStringValue("Rebels", "Template", "H");
    ini.setStringValue("Rebels", "Opponent1", "Corruptique");
    ini.setStringValue("Rebels", "Opponent2", "Tharpique");
    ini.setStringValue("Rebels", "Opponent3", "Ordos");
    ini.setIntValue("Rebels", "CampaignRevision", 2);
    ini.setIntValue("Rebels", "OpeningQuota", 2000);
    ini.setStringValue("Rebels", "PersistentSupport", "Harkonnen");
    for(int stage = 1; stage <= 9; ++stage) {
        const auto prefix = "Stage" + std::to_string(stage) + "Opponent";
        ini.setStringValue("Rebels", prefix + "1", stage < 3 ? "Corruptique" : "Tharpique");
        if(stage >= 8) ini.setStringValue("Rebels", prefix + "3", "Corruptique");
        if(stage == 9) ini.setStringValue("Rebels", prefix + "4", "Ordos");
    }
    const auto plan = campaign::readCampaignPlan(ini, "Rebels");
    REQUIRE(plan);
    REQUIRE(plan->templateLetter == 'H');
    REQUIRE(plan->opponents[1] == "Tharpique");
    REQUIRE(plan->openingQuota == 2000);
    for(int stage = 0; stage < 9; ++stage) {
        REQUIRE(plan->stageOpponents[stage][1] == "Harkonnen");
        REQUIRE(opponentCount(plan->stageOpponents[stage]) == (stage < 7 ? 2 : stage == 7 ? 3 : 4));
    }

    SECTION("older edition plans do not invent a revised mission roster") {
        ini.removeKey("Rebels", "CampaignRevision");
        const auto older = campaign::readCampaignPlan(ini, "Rebels");
        REQUIRE(older);
        REQUIRE(older->revision == 0);
        REQUIRE(opponentCount(older->stageOpponents[8]) == 0);
    }
}

TEST_CASE("Campaign faction substitutions are whole-word and nonrecursive", "[tornie][campaign][text]") {
    const std::array<std::string_view, 4> roles = {"Atreides", "Rebels", "Kleshmersh", "Tharpique"};
    REQUIRE(campaign::replaceCampaignHouseNames("Harkonnen, ATREIDES, Ordos and the Emperor.", roles)
        == "Atreides, Rebels, Kleshmersh and the Tharpique.");
    REQUIRE(campaign::replaceCampaignHouseNames("Sardaukars and Empereur, not Harkonnenite.", roles)
        == "Tharpique and Tharpique, not Harkonnenite.");
}

TEST_CASE("Bundled 546 campaign recipes supply two, three and four opponents", "[tornie][campaign][content]") {
    const char* houses[] = {"Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar", "Mercenary",
        "Neutral", "Rebels", "Corruptique", "Wildspade", "Kleshmersh", "Tharpique"};
    for(const char* mod : {"Tornie", "TornieLite"}) {
        INFO(mod);
        INIFile ini(sourceRoot() + "/mods/" + mod + "/data/CampaignPlan.ini");
        const int count = std::string_view(mod) == "TornieLite" ? 6 : 12;
        for(int house = 0; house < count; ++house) {
            INFO(houses[house]);
            const auto plan = campaign::readCampaignPlan(ini, houses[house]);
            REQUIRE(plan);
            REQUIRE(plan->revision == 2);
            REQUIRE(plan->openingQuota == 2000);
            for(int stage = 0; stage < 9; ++stage) {
                REQUIRE(plan->stageOpponents[stage][1] == plan->persistentSupport);
                REQUIRE(opponentCount(plan->stageOpponents[stage]) == (stage < 7 ? 2 : stage == 7 ? 3 : 4));
            }
        }
    }
}

TEST_CASE("Tornie Rebels starts its economy with a CHOAM MCV rather than a free yard", "[tornie][campaign][content]") {
    INIFile mission(sourceRoot() + "/mods/Tornie/campaign/scenr001.ini");
    REQUIRE(mission.getIntValue("BASIC", "CampaignRevision", 0) == 2);
    REQUIRE(mission.getStringValue("BASIC", "SupportOpponent") == "Harkonnen");
    REQUIRE(mission.getStringValue("BASIC", "OpeningStyle") == "Trade");
    REQUIRE(mission.getIntValue("Rebels", "Credits", 0) == 1500);
    REQUIRE(mission.getIntValue("Rebels", "Quota", 0) == 2000);
    REQUIRE(mission.getIntValue("CHOAM", "MCV", 0) > 0);
    bool starport = false;
    for(const auto& key : mission.getSection("STRUCTURES")) {
        const auto value = key.getStringValue();
        if(value.rfind("Rebels,", 0) != 0) continue;
        REQUIRE(value.find(",Const Yard,") == std::string::npos);
        starport = starport || value.find(",Starport,") != std::string::npos;
    }
    REQUIRE(starport);
    for(const auto& key : mission.getSection("UNITS"))
        REQUIRE(key.getStringValue().rfind("Rebels,MCV,", 0) != 0);
}
