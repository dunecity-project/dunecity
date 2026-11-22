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

#ifndef ENEMYDIFFICULTYPOLICY_H
#define ENEMYDIFFICULTYPOLICY_H

#include <Definitions.h>
#include <GameInitSettings.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

/**
    "Restart on New Difficulty" (in-game menu): the player picks a difficulty and the game starts
    again with every enemy AI of the human player on it (docs/easier-enemy-restart.md; Stefan,
    2026-11-22: "a restart on new difficulty - you can select what difficulty").

    The difficulties: QuantBot Easiest, Easy, Medium, Hard and Brutal; in a campaign also the
    original Campaign AI. Only enemies already on that list change: QuantBot Defend,
    role-limited QuantBots and the other AI families keep their class.

    An enemy is an AI in a house without a human player whose team no human plays on, so a
    partner sharing the human's house and an allied AI house keep their strength. The class
    strings are the saved ones registered in PlayerFactory.
*/
namespace EnemyDifficultyPolicy {

inline const char* const kCampaignAI = "CampaignAIPlayer";
/// What the menus select for a new game's enemies (Stefan, 2026-11-22: "set easiest as the default").
inline const char* const kDefaultEnemy = "qBotEasiest";

/// How hard a selectable class is: Campaign AI 0, QuantBot Easiest 1, Easy 2 ... Brutal 5;
/// -1 for a class the player cannot pick.
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

/// The classes the player may restart against, easiest QuantBot first; the Campaign AI last and
/// only in a campaign.
inline std::vector<std::string> choices(bool campaign) {
    std::vector<std::string> classes{"qBotEasiest", "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal"};
    if(campaign) classes.push_back(kCampaignAI);
    return classes;
}

namespace detail {
    inline bool hasHuman(const GameInitSettings::HouseInfo& house) {
        return std::any_of(house.playerInfoList.begin(), house.playerInfoList.end(),
                           [](const GameInitSettings::PlayerInfo& player) { return player.playerClass == HUMANPLAYERCLASS; });
    }

    inline bool selectable(const std::string& playerClass, bool campaign) {
        return rank(playerClass) > 0 || (campaign && playerClass == kCampaignAI);
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

/// The difficulty the enemies play now: the hardest selectable enemy class, or "" when no enemy
/// can change (then the menu offers nothing).
inline std::string current(const GameInitSettings::HouseInfoList& houses, bool campaign) {
    std::string hardest;
    detail::forEachEnemy(houses, [&](const GameInitSettings::PlayerInfo& player) {
        if(detail::selectable(player.playerClass, campaign)
           && (hardest.empty() || rank(player.playerClass) > rank(hardest))) hardest = player.playerClass;
    });
    return hardest;
}

/// Puts every enemy that can change on playerClass. Returns whether anything changed.
inline bool apply(GameInitSettings::HouseInfoList& houses, const std::string& playerClass, bool campaign) {
    if(!detail::selectable(playerClass, campaign)) return false;
    bool changed = false;
    detail::forEachEnemy(houses, [&](GameInitSettings::PlayerInfo& player) {
        if(detail::selectable(player.playerClass, campaign) && player.playerClass != playerClass) {
            player.playerClass = playerClass;
            changed = true;
        }
    });
    return changed;
}

/// The settings to restart with: the same game against enemies on playerClass.
inline GameInitSettings withEnemies(const GameInitSettings& settings, const std::string& playerClass, bool campaign) {
    GameInitSettings next = settings;
    GameInitSettings::HouseInfoList houses = next.getHouseInfoList();
    if(apply(houses, playerClass, campaign)) {
        next.clearHouseInfo();
        for(const auto& house : houses) next.addHouseInfo(house);
    }
    return next;
}

} // namespace EnemyDifficultyPolicy

#endif // ENEMYDIFFICULTYPOLICY_H
