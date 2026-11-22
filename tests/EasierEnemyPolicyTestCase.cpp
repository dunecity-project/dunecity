#include <catch2/catch_all.hpp>
#include <players/EasierEnemyPolicy.h>

#include <string>
#include <vector>

using namespace EasierEnemyPolicy;

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

TEST_CASE("QuantBot enemies step one difficulty down and stop at Easiest outside a campaign", "[menu][difficulty][policy]") {
    REQUIRE(easierClass("qBotBrutal", false) == "qBotHard");
    REQUIRE(easierClass("qBotHard", false) == "qBotMedium");
    REQUIRE(easierClass("qBotMedium", false) == "qBotEasy");
    REQUIRE(easierClass("qBotEasy", false) == "qBotEasiest");
    REQUIRE(easierClass("qBotEasiest", false).empty());
    // Everything else has no easier step.
    for(const char* playerClass : {"qBotDefend", "CampaignAIPlayer", "SmartBot", "AIPlayerHard", "qBotFighterHard",
                                   "qBotSupportHard", HUMANPLAYERCLASS, ""}) {
        REQUIRE(easierClass(playerClass, false).empty());
        REQUIRE(easierClass(playerClass, true).empty());
    }
}

TEST_CASE("In a campaign the step below QuantBot Easiest is the Campaign AI", "[menu][difficulty][policy][campaign]") {
    REQUIRE(easierClass("qBotEasy", true) == "qBotEasiest");
    REQUIRE(easierClass("qBotEasiest", true) == "CampaignAIPlayer");
    REQUIRE(easierClass("qBotMedium", true) == "qBotEasy");
    REQUIRE(easierClass("CampaignAIPlayer", true).empty());
    REQUIRE(label("qBotEasy") == "QuantBot Easy");
    REQUIRE(label("qBotEasiest") == "QuantBot Easiest");
    REQUIRE(rank("qBotEasiest") < rank("qBotEasy"));
    REQUIRE(label("CampaignAIPlayer") == "Campaign AI");
    // Each step lands on a lower rank, so repeating the button always ends.
    for(std::string current = "qBotBrutal"; !easierClass(current, true).empty(); current = easierClass(current, true)) {
        REQUIRE(rank(easierClass(current, true)) == rank(current) - 1);
    }
}

TEST_CASE("Only enemies of the human get easier; partners and allies keep their strength", "[menu][difficulty][policy]") {
    GameInitSettings::HouseInfoList houses = {
        house(HOUSE_ATREIDES, 1, {HUMANPLAYERCLASS, "qBotSupportHard"}),   // human with a same-house partner
        house(HOUSE_FREMEN, 1, {"qBotBrutal"}),                            // allied AI house
        house(HOUSE_HARKONNEN, 2, {"qBotHard"}),
        house(HOUSE_ORDOS, 3, {"qBotEasy"}),
        house(HOUSE_SARDAUKAR, 2, {"SmartBot"}),
    };

    const Step custom = nextStep(houses, false);
    REQUIRE(custom.available());
    REQUIRE(custom.from == "qBotHard");
    REQUIRE(custom.to == "qBotMedium");

    REQUIRE(apply(houses, false));
    REQUIRE(classesOf(houses) == std::vector<std::string>{HUMANPLAYERCLASS, "qBotSupportHard", "qBotBrutal",
                                                          "qBotMedium", "qBotEasy", "SmartBot"});
    REQUIRE(apply(houses, false));      // Medium -> Easy, Easy -> Easiest
    REQUIRE(classesOf(houses)[3] == "qBotEasy");
    REQUIRE(classesOf(houses)[4] == "qBotEasiest");
    REQUIRE(apply(houses, false));      // Easy -> Easiest; the Easiest house has nowhere to go
    REQUIRE_FALSE(nextStep(houses, false).available());
    REQUIRE_FALSE(apply(houses, false));
    REQUIRE(classesOf(houses)[3] == "qBotEasiest");
    REQUIRE(classesOf(houses)[4] == "qBotEasiest");
}

TEST_CASE("A campaign restart carries the easier enemies in its settings", "[menu][difficulty][policy][campaign]") {
    GameInitSettings current;
    current.addHouseInfo(house(HOUSE_ATREIDES, 1, {HUMANPLAYERCLASS}));
    current.addHouseInfo(house(HOUSE_HARKONNEN, 2, {"qBotEasy"}));
    current.addHouseInfo(house(HOUSE_ORDOS, 2, {"qBotEasy"}));

    const Step step = nextStep(current.getHouseInfoList(), true);
    REQUIRE(step.from == "qBotEasy");
    REQUIRE(step.to == "qBotEasiest");
    REQUIRE(nextStep(current.getHouseInfoList(), false).to == "qBotEasiest");

    const GameInitSettings easiest = easierSettings(current, true);
    REQUIRE(classesOf(easiest.getHouseInfoList()) == std::vector<std::string>{HUMANPLAYERCLASS, "qBotEasiest", "qBotEasiest"});
    REQUIRE(nextStep(easiest.getHouseInfoList(), true).to == "CampaignAIPlayer");
    REQUIRE_FALSE(nextStep(easiest.getHouseInfoList(), false).available());

    const GameInitSettings next = easierSettings(easiest, true);
    REQUIRE(classesOf(next.getHouseInfoList()) == std::vector<std::string>{HUMANPLAYERCLASS, "CampaignAIPlayer", "CampaignAIPlayer"});
    REQUIRE(next.getHouseInfoList()[1].houseID == HOUSE_HARKONNEN);
    REQUIRE(next.getHouseInfoList()[1].team == 2);
    // The running game's settings are untouched, and the bottom of the ladder offers nothing.
    REQUIRE(classesOf(current.getHouseInfoList())[1] == "qBotEasy");
    REQUIRE_FALSE(nextStep(next.getHouseInfoList(), true).available());
}

TEST_CASE("A game without a human player offers no easier enemies", "[menu][difficulty][policy]") {
    GameInitSettings::HouseInfoList houses = {house(HOUSE_ATREIDES, 1, {"qBotHard"}), house(HOUSE_ORDOS, 2, {"qBotBrutal"})};
    REQUIRE_FALSE(nextStep(houses, true).available());
    REQUIRE_FALSE(apply(houses, true));
}
