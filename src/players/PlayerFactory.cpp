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

#include <players/PlayerFactory.h>

#include <players/HumanPlayer.h>
#include <players/AIPlayer.h>
#include <players/CampaignAIPlayer.h>
#include <players/SmartBot.h>
#include <players/QuantBot.h>

#include <tuple>
#include <utility>

std::vector<PlayerFactory::PlayerData> PlayerFactory::playerDataList;
std::vector<std::pair<std::string, PlayerFactory::PlayerData>> PlayerFactory::retiredPlayerDataList;

namespace {

const std::pair<const char*, QuantBot::Difficulty> kQuantBotDifficulties[] = {
    {"Easy", QuantBot::Difficulty::Easy},
    {"Medium", QuantBot::Difficulty::Medium},
    {"Hard", QuantBot::Difficulty::Hard},
    {"Brutal", QuantBot::Difficulty::Brutal},
    {"Defend", QuantBot::Difficulty::Defend},
};

} // namespace

void PlayerFactory::registerAllPlayers() {

    playerDataList.emplace_back(  HUMANPLAYERCLASS,
                                            "Human Player",
                                            [](House* house,const std::string& playername) { return std::make_unique<HumanPlayer>(house, playername); },
                                            [](InputStream& inputStream, House* house) { return std::make_unique<HumanPlayer>(inputStream, house); } );

    // The lobby sends a player as its index in this list (ChangeEventList), so a new entry is
    // appended and changes NETWORK_PROTOCOL_VERSION. The class strings are saved: never rename them.
    const auto addQuantBot = [](const std::string& prefix, AIRole role, const std::pair<const char*, QuantBot::Difficulty>& level) {
        const std::string difficulty = level.first;
        const QuantBot::Difficulty value = level.second;
        const std::string name = role == AIRole::Both ? "QuantBot " + difficulty
                                                       : "QuantBot " + difficulty + " (" + aiRoleName(role) + ")";
        playerDataList.emplace_back(prefix + difficulty, name,
            [value, role](House* house, const std::string& playername) { return std::make_unique<QuantBot>(house, playername, value, role); },
            [](InputStream& inputStream, House* house) { return std::make_unique<QuantBot>(inputStream, house); },
            "QuantBot", difficulty, role);
    };

    for(const auto& level : kQuantBotDifficulties) addQuantBot("qBot", AIRole::Both, level);
    // Builders keep the class names of the former "AI Support" players; Defend was registered first.
    addQuantBot("qBotSupport", AIRole::Builder, kQuantBotDifficulties[4]);
    for(int i = 0; i < 4; i++) addQuantBot("qBotSupport", AIRole::Builder, kQuantBotDifficulties[i]);

    // The Mentat AI was retired on 2026-10-31 (its build structure lives on in QuantBot). Saves,
    // replays and settings naming its classes resolve to the equivalent QuantBot; its stream is the
    // QuantBot prefix plus a support flag (QuantBot::RetiredMentatSave). Never listed.
    for(const auto& [prefix, retiredPrefix, role] : {std::make_tuple("qBot", "mentat", AIRole::Both),
                                                     std::make_tuple("qBotSupport", "mentatSupport", AIRole::Builder)}) {
        for(const auto& level : kQuantBotDifficulties) {
            const std::string difficulty = level.first;
            const QuantBot::Difficulty value = level.second;
            retiredPlayerDataList.emplace_back(retiredPrefix + difficulty, PlayerData(prefix + difficulty,
                role == AIRole::Both ? "QuantBot " + difficulty : "QuantBot " + difficulty + " (" + aiRoleName(role) + ")",
                [value, role = role](House* house, const std::string& playername) { return std::make_unique<QuantBot>(house, playername, value, role); },
                [](InputStream& inputStream, House* house) { return std::make_unique<QuantBot>(inputStream, house, QuantBot::RetiredMentatSave{}); },
                "QuantBot", difficulty, role));
        }
    }

    playerDataList.emplace_back(  "SmartBot",
                                            "SmartBot",
        [](House* house, const std::string& playername) { return std::make_unique<SmartBot>(house, playername, SmartBot::Difficulty::Normal); },
        [](InputStream& inputStream, House* house) { return std::make_unique<SmartBot>(inputStream, house); },
        "SmartBot");

    playerDataList.emplace_back(  "AIPlayerEasy",
                                            "AI Player (easy)",
        [](House* house, const std::string& playername) { return std::make_unique<AIPlayer>(house, playername, AIPlayer::Difficulty::Easy); },
        [](InputStream& inputStream, House* house) { return std::make_unique<AIPlayer>(inputStream, house); },
        "AI Player", "Easy");

    playerDataList.emplace_back(  "AIPlayerMedium",
                                            "AI Player (medium)",
        [](House* house, const std::string& playername) { return std::make_unique<AIPlayer>(house, playername, AIPlayer::Difficulty::Medium); },
        [](InputStream& inputStream, House* house) { return std::make_unique<AIPlayer>(inputStream, house); },
        "AI Player", "Medium");

    playerDataList.emplace_back(  "AIPlayerHard",
                                            "AI Player (hard)",
        [](House* house, const std::string& playername) { return std::make_unique<AIPlayer>(house, playername, AIPlayer::Difficulty::Hard); },
        [](InputStream& inputStream, House* house) { return std::make_unique<AIPlayer>(inputStream, house); },
        "AI Player", "Hard");

    playerDataList.emplace_back(  "CampaignAIPlayer",
                                            "CampaignAIPlayer",
        [](House* house, const std::string& playername) { return std::make_unique<CampaignAIPlayer>(house, playername); },
        [](InputStream& inputStream, House* house) { return std::make_unique<CampaignAIPlayer>(inputStream, house); },
        "Campaign AI");

    for(const auto& level : kQuantBotDifficulties) addQuantBot("qBotFighter", AIRole::Fighter, level);
}
