#include <catch2/catch_all.hpp>
#include <players/EnemyDifficultyPolicy.h>

#include <string>
#include <vector>

using namespace EnemyDifficultyPolicy;

namespace {
GameInitSettings::HouseInfo house(HOUSETYPE id, int team, const std::vector<std::string>& classes) {
    GameInitSettings::HouseInfo info(id, team);
    for(const auto& playerClass : classes) info.addPlayerInfo(GameInitSettings::PlayerInfo("p", playerClass));
    return info;
}

std::vector<std::string> classesOf(const GameInitSettings::HouseInfoList& houses) {
    std::vector<std::string> result;
    for(const auto& info : houses) {
        for(const auto& player : info.playerInfoList) result.push_back(player.playerClass);
    }
    return result;
}
} // namespace

TEST_CASE("The difficulties a restart offers: five QuantBot levels, and the Campaign AI in a campaign",
          "[menu][difficulty][policy]") {
    REQUIRE(choices(false) == std::vector<std::string>{"qBotEasiest", "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal"});
    REQUIRE(choices(true) == std::vector<std::string>{"qBotEasiest", "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal",
                                                      "CampaignAIPlayer"});
    REQUIRE(std::string(kDefaultEnemy) == "qBotEasiest");
    REQUIRE(label("qBotEasiest") == "QuantBot Easiest");
    REQUIRE(label("qBotBrutal") == "QuantBot Brutal");
    REQUIRE(label("CampaignAIPlayer") == "Campaign AI");
    REQUIRE(rank("qBotEasiest") < rank("qBotEasy"));
    REQUIRE(rank("qBotHard") < rank("qBotBrutal"));
    // Classes the player cannot pick.
    for(const char* playerClass : {"qBotDefend", "SmartBot", "AIPlayerHard", "qBotFighterHard", "qBotSupportHard",
                                   HUMANPLAYERCLASS, ""}) REQUIRE(rank(playerClass) < 0);
}

TEST_CASE("A chosen difficulty reaches the enemies only; partners, allies and other AI families stay",
          "[menu][difficulty][policy]") {
    GameInitSettings::HouseInfoList houses = {
        house(HOUSE_ATREIDES, 1, {HUMANPLAYERCLASS, "qBotSupportHard"}),   // human with a same-house partner
        house(HOUSE_FREMEN, 1, {"qBotBrutal"}),                            // allied AI house
        house(HOUSE_HARKONNEN, 2, {"qBotHard"}),
        house(HOUSE_ORDOS, 3, {"qBotEasy"}),
        house(HOUSE_SARDAUKAR, 2, {"SmartBot"}),
        house(HOUSE_MERCENARY, 4, {"qBotDefend"}),
    };
    // The menu shows the hardest enemy that can change.
    REQUIRE(current(houses, false) == "qBotHard");

    REQUIRE(apply(houses, "qBotEasiest", false));
    REQUIRE(classesOf(houses) == std::vector<std::string>{HUMANPLAYERCLASS, "qBotSupportHard", "qBotBrutal",
                                                          "qBotEasiest", "qBotEasiest", "SmartBot", "qBotDefend"});
    REQUIRE(current(houses, false) == "qBotEasiest");
    REQUIRE_FALSE(apply(houses, "qBotEasiest", false));    // nothing left to change
    // Harder works as well as easier.
    REQUIRE(apply(houses, "qBotBrutal", false));
    REQUIRE(classesOf(houses)[3] == "qBotBrutal");
    REQUIRE(classesOf(houses)[4] == "qBotBrutal");
    // A class off the list is refused, and the Campaign AI is a campaign choice only.
    REQUIRE_FALSE(apply(houses, "SmartBot", false));
    REQUIRE_FALSE(apply(houses, "CampaignAIPlayer", false));
    REQUIRE(classesOf(houses)[3] == "qBotBrutal");
}

TEST_CASE("A campaign restart carries the chosen enemies in its settings", "[menu][difficulty][policy][campaign]") {
    GameInitSettings settings;
    settings.addHouseInfo(house(HOUSE_ATREIDES, 1, {HUMANPLAYERCLASS}));
    settings.addHouseInfo(house(HOUSE_HARKONNEN, 2, {"qBotEasiest"}));
    settings.addHouseInfo(house(HOUSE_ORDOS, 2, {"qBotEasiest"}));
    REQUIRE(current(settings.getHouseInfoList(), true) == "qBotEasiest");

    const GameInitSettings hard = withEnemies(settings, "qBotHard", true);
    REQUIRE(classesOf(hard.getHouseInfoList()) == std::vector<std::string>{HUMANPLAYERCLASS, "qBotHard", "qBotHard"});
    REQUIRE(hard.getHouseInfoList()[1].houseID == HOUSE_HARKONNEN);
    REQUIRE(hard.getHouseInfoList()[1].team == 2);
    // The running game's settings are untouched.
    REQUIRE(classesOf(settings.getHouseInfoList())[1] == "qBotEasiest");

    // In a campaign the Campaign AI can be chosen, and chosen away from again.
    const GameInitSettings original = withEnemies(hard, "CampaignAIPlayer", true);
    REQUIRE(classesOf(original.getHouseInfoList()) == std::vector<std::string>{HUMANPLAYERCLASS, "CampaignAIPlayer",
                                                                                 "CampaignAIPlayer"});
    REQUIRE(current(original.getHouseInfoList(), true) == "CampaignAIPlayer");
    REQUIRE(classesOf(withEnemies(original, "qBotMedium", true).getHouseInfoList())[2] == "qBotMedium");
    // Outside a campaign a Campaign AI enemy is not something the menu changes.
    REQUIRE(current(original.getHouseInfoList(), false).empty());
    REQUIRE(classesOf(withEnemies(original, "qBotMedium", false).getHouseInfoList())[1] == "CampaignAIPlayer");
}

TEST_CASE("No human player, or no enemy that can change, offers no restart on a new difficulty",
          "[menu][difficulty][policy]") {
    GameInitSettings::HouseInfoList watched = {house(HOUSE_ATREIDES, 1, {"qBotHard"}), house(HOUSE_ORDOS, 2, {"qBotBrutal"})};
    REQUIRE(current(watched, true).empty());
    REQUIRE_FALSE(apply(watched, "qBotEasy", true));

    GameInitSettings::HouseInfoList fixed = {house(HOUSE_ATREIDES, 1, {HUMANPLAYERCLASS}), house(HOUSE_ORDOS, 2, {"SmartBot"}),
                                             house(HOUSE_HARKONNEN, 2, {"qBotDefend"})};
    REQUIRE(current(fixed, false).empty());
    REQUIRE_FALSE(apply(fixed, "qBotEasy", false));
}
