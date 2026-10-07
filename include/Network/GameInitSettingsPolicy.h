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

#ifndef GAMEINITSETTINGSPOLICY_H
#define GAMEINITSETTINGSPOLICY_H

#include <DataTypes.h>
#include <Definitions.h>
#include <GameInitSettings.h>

#include <cstddef>
#include <string>

/**
    Semantic limits for a GameInitSettings that arrived over the network.

    This is a *network* policy: it is applied to the settings a host sends in SENDGAMEINFO and
    in COOP_MISSION, never to savegames or map files loaded from disk. Local loading keeps its
    existing behaviour, so old saves keep working; what changes is that a hostile or broken
    host can no longer hand a client a snapshot with impossible counts, unknown enums or a
    multi-megabyte payload and have it reach the lobby.

    The size bounds are deliberately generous compared with real content (a Dune map INI is
    tens of KiB) and the count bounds follow the lobby's own MAX_CUSTOM_GAME_PLAYERS.
*/
namespace GameInitSettingsPolicy {

/// Largest map payload accepted inside a received game info packet.
constexpr std::size_t kMaxMapFileSize = 1024 * 1024;
// Saves include simulation state, so they may be larger than map INIs. ENet additionally
// bounds the complete packet to 4 MiB before reassembly.
constexpr std::size_t kMaxSaveFileSize = 4 * 1024 * 1024;
/// Largest filename accepted for a received map.
constexpr std::size_t kMaxFilenameLength = 128;
/// Largest mod name accepted in a received game info packet.
constexpr std::size_t kMaxModNameLength = 128;
/// Largest player name and player class string accepted in a received game info packet.
constexpr std::size_t kMaxPlayerNameLength = 64;
constexpr std::size_t kMaxPlayerClassLength = 64;
/// Seats per house in the lobby (single seat, or the co-op pair).
constexpr std::size_t kMaxPlayersPerHouse = 2;

/**
    \param  gameType    the value decoded from the packet
    \return true if it names a real game type
*/
inline bool isKnownGameType(GameType gameType) {
    switch(gameType) {
        case GameType::Invalid:
        case GameType::LoadSavegame:
        case GameType::Campaign:
        case GameType::CustomGame:
        case GameType::Skirmish:
        case GameType::CustomMultiplayer:
        case GameType::LoadMultiplayer:
        case GameType::CampaignCoop:
        case GameType::SkirmishCoop:
        case GameType::LoadCoop:
            return true;
        default:
            return false;
    }
}

/**
    \param  houseID the value decoded from the packet
    \return true if it names a real house (or the "no house" marker the lobby uses)
*/
inline bool isKnownHouse(HOUSETYPE houseID) {
    return houseID == HOUSE_INVALID || (houseID >= HOUSE_HARKONNEN && houseID < NUM_HOUSES);
}

/**
    Validates a complete received snapshot. Nothing in the session is committed before this
    passes, so a rejected packet leaves the receiver exactly as it was.
    \param  settings    the decoded settings
    \param  reason      set to a short description when the settings are refused
    \return true if the settings may be used
*/
inline bool isAcceptableReceivedGameInitSettings(const GameInitSettings& settings,
                                                 std::string& reason, bool allowCampaignEnd = false) {
    if(!isKnownGameType(settings.getGameType())) {
        reason = "unknown game type";
        return false;
    }
    // Recognising an enum is not permission to invoke its local-file loader. Network
    // sessions carry their content in the packet; only COOP_MISSION may carry an end marker.
    if(!isNetworkGameType(settings.getGameType())
       && !(allowCampaignEnd && settings.getGameType() == GameType::Invalid)) {
        reason = "game type is not a network session";
        return false;
    }
    if(!isKnownHouse(settings.getHouseID())) {
        reason = "unknown house";
        return false;
    }
    if(settings.getFilename().size() > kMaxFilenameLength) {
        reason = "filename too long";
        return false;
    }
    const bool savedGame = settings.getGameType() == GameType::LoadMultiplayer
        || settings.getGameType() == GameType::LoadCoop;
    if(settings.getFiledata().size() > (savedGame ? kMaxSaveFileSize : kMaxMapFileSize)) {
        reason = savedGame ? "save payload too large" : "map payload too large";
        return false;
    }
    if(settings.getModName().size() > kMaxModNameLength) {
        reason = "mod name too long";
        return false;
    }

    const GameInitSettings::HouseInfoList& houses = settings.getHouseInfoList();
    if(houses.size() > static_cast<std::size_t>(MAX_CUSTOM_GAME_PLAYERS)) {
        reason = "too many houses";
        return false;
    }

    for(const GameInitSettings::HouseInfo& houseInfo : houses) {
        // Checked before the closed-row skip below: a closed row can be reopened in the lobby
        // and would then play with whatever factor it carried, so every row is bounded.
        if(!SpiceIncome::isValid(houseInfo.spiceIncomeMultiplier)) {
            reason = "spice income factor out of range";
            return false;
        }
        // Saved multiplayer lobbies retain explicitly closed rows.
        if(houseInfo.houseID == HOUSE_UNUSED && houseInfo.playerInfoList.empty()) continue;
        if(!isKnownHouse(houseInfo.houseID)) {
            reason = "unknown house in house list";
            return false;
        }
        if(houseInfo.team < -1 || houseInfo.team > MAX_CUSTOM_GAME_PLAYERS) {
            reason = "team out of range";
            return false;
        }
        if(houseInfo.playerInfoList.size() > kMaxPlayersPerHouse) {
            reason = "too many players in one house";
            return false;
        }
        for(const GameInitSettings::PlayerInfo& playerInfo : houseInfo.playerInfoList) {
            if(playerInfo.playerName.size() > kMaxPlayerNameLength
               || playerInfo.playerClass.size() > kMaxPlayerClassLength) {
                reason = "player identification too long";
                return false;
            }
        }
    }

    const SettingsClass::GameOptionsClass& options = settings.getGameOptions();
    if(options.gameSpeed < GAMESPEED_MIN || options.gameSpeed > GAMESPEED_MAX) {
        reason = "game speed out of range";
        return false;
    }
    if(options.maximumNumberOfUnitsOverride < -1 || options.maximumNumberOfUnitsOverride > 10000) {
        reason = "unit limit override out of range";
        return false;
    }
    if(options.maximumNumberOfHarvestersOverride < -1
       || options.maximumNumberOfHarvestersOverride > 10000) {
        reason = "harvester limit override out of range";
        return false;
    }

    if(options.maximumNumberOfConstructionYardsOverride < -1
       || options.maximumNumberOfConstructionYardsOverride > 999) {
        reason = "construction yard limit out of range";
        return false;
    }

    reason.clear();
    return true;
}

} // namespace GameInitSettingsPolicy

#endif // GAMEINITSETTINGSPOLICY_H
