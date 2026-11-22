/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EASIERENEMYPOLICY_H
#define EASIERENEMYPOLICY_H

#include <Definitions.h>
#include <GameInitSettings.h>

#include <algorithm>
#include <set>
#include <string>

/**
    "Restart with Easier AI" (in-game menu): every enemy AI of the human player steps one
    difficulty down and the game starts again.

    QuantBot Brutal > Hard > Medium > Easy > Easiest. In a campaign the step below QuantBot
    Easiest is the original Campaign AI (Stefan, 2026-11-22: the Campaign AI is the last step;
    Easiest was added the same day as the difficulty below Easy). Every other class has no
    easier step: QuantBot Defend, the Campaign AI itself, role-limited QuantBots and the other
    AI families.

    An enemy is an AI in a house without a human player whose team no human plays on, so a
    partner sharing the human's house and an allied AI house keep their strength. The class
    strings are the saved ones registered in PlayerFactory.
*/
namespace EasierEnemyPolicy {

inline const char* const kCampaignAI = "CampaignAIPlayer";

/// The class one step easier than playerClass, or "" when there is none.
inline std::string easierClass(const std::string& playerClass, bool campaign) {
    if(playerClass == "qBotBrutal") return "qBotHard";
    if(playerClass == "qBotHard") return "qBotMedium";
    if(playerClass == "qBotMedium") return "qBotEasy";
    if(playerClass == "qBotEasy") return "qBotEasiest";
    if(playerClass == "qBotEasiest" && campaign) return kCampaignAI;
    return "";
}

/// How hard a class on the ladder is: Campaign AI 0, QuantBot Easiest 1, Easy 2 ... Brutal 5;
/// -1 off the ladder.
inline int rank(const std::string& playerClass) {
    if(playerClass == kCampaignAI) return 0;
    if(playerClass == "qBotEasiest") return 1;
    if(playerClass == "qBotEasy") return 2;
    if(playerClass == "qBotMedium") return 3;
    if(playerClass == "qBotHard") return 4;
    if(playerClass == "qBotBrutal") return 5;
    return -1;
}

/// The name shown to the player.
inline std::string label(const std::string& playerClass) {
    switch(rank(playerClass)) {
        case 0: return "Campaign AI";
        case 1: return "QuantBot Easiest";
        case 2: return "QuantBot Easy";
        case 3: return "QuantBot Medium";
        case 4: return "QuantBot Hard";
        case 5: return "QuantBot Brutal";
        default: return playerClass;
    }
}

/// The step the menu offers: the hardest enemy that can still step down, and what it becomes.
struct Step {
    std::string from;
    std::string to;
    bool available() const { return !to.empty(); }
};

namespace detail {
    inline bool hasHuman(const GameInitSettings::HouseInfo& house) {
        return std::any_of(house.playerInfoList.begin(), house.playerInfoList.end(),
                           [](const GameInitSettings::PlayerInfo& player) { return player.playerClass == HUMANPLAYERCLASS; });
    }

    /// Calls visit(playerInfo) for every enemy AI of the human players; nothing without a human.
    template<typename Houses, typename Visitor>
    void forEachEnemy(Houses& houses, Visitor visit) {
        std::set<int> humanTeams;
        for(const auto& house : houses) {
            if(hasHuman(house)) humanTeams.insert(house.team);
        }
        if(humanTeams.empty()) return;
        for(auto& house : houses) {
            if(hasHuman(house) || humanTeams.count(house.team) != 0) continue;
            for(auto& player : house.playerInfoList) visit(player);
        }
    }
} // namespace detail

inline Step nextStep(const GameInitSettings::HouseInfoList& houses, bool campaign) {
    Step step;
    detail::forEachEnemy(houses, [&](const GameInitSettings::PlayerInfo& player) {
        const std::string easier = easierClass(player.playerClass, campaign);
        if(!easier.empty() && (step.from.empty() || rank(player.playerClass) > rank(step.from))) {
            step.from = player.playerClass;
            step.to = easier;
        }
    });
    return step;
}

/// Steps every enemy that can one difficulty down. Returns whether anything changed.
inline bool apply(GameInitSettings::HouseInfoList& houses, bool campaign) {
    bool changed = false;
    detail::forEachEnemy(houses, [&](GameInitSettings::PlayerInfo& player) {
        const std::string easier = easierClass(player.playerClass, campaign);
        if(!easier.empty()) {
            player.playerClass = easier;
            changed = true;
        }
    });
    return changed;
}

/// The settings to restart with: the same game against the easier enemies.
inline GameInitSettings easierSettings(const GameInitSettings& current, bool campaign) {
    GameInitSettings next = current;
    GameInitSettings::HouseInfoList houses = next.getHouseInfoList();
    if(apply(houses, campaign)) {
        next.clearHouseInfo();
        for(const auto& house : houses) next.addHouseInfo(house);
    }
    return next;
}

} // namespace EasierEnemyPolicy

#endif // EASIERENEMYPOLICY_H
