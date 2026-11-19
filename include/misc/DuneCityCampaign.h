// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef DUNECITYCAMPAIGN_H
#define DUNECITYCAMPAIGN_H

#include <DataTypes.h>

#include <cctype>
#include <string>

/**
    The DuneCity campaign (docs/city-campaign.md): ten Atreides city-sim missions,
    DCITY_M01.INI .. DCITY_M10.INI in data/maps/singleplayer, played through the normal
    campaign flow (briefing, debriefing, retry) without the region map. Like the reworked
    campaign, a game belongs to it when its scenario filename carries the prefix, so saves,
    the next mission and retries need no other state.

    The missions are authored as Custom-Game city maps, so the campaign keeps their rules:
    the INI TechLevel (not the mission number), their Brain=TeamN rivalries, and Custom QuantBot
    behaviour rather than the original-campaign difficulty rules.
*/
namespace DuneCityCampaign {

inline constexpr const char* kPrefix = "DCITY_M";
inline constexpr int kMissions = 10;
inline constexpr HOUSETYPE kHouse = HOUSE_ATREIDES;
inline constexpr const char* kMapDirectory = "maps/singleplayer";

/// True for "DCITY_M03.INI" (any case); false for paths and other names.
inline bool isCampaignFilename(const std::string& filename) {
    const std::string prefix = kPrefix;
    if(filename.size() < prefix.size() || filename.find_first_of("/\\") != std::string::npos) return false;
    for(size_t i = 0; i < prefix.size(); ++i) {
        if(std::toupper(static_cast<unsigned char>(filename[i])) != prefix[i]) return false;
    }
    return true;
}

/// "DCITY_M01.INI" for mission 1 (1..kMissions).
inline std::string scenarioFilename(int mission) {
    std::string name = "DCITY_M00.INI";
    name[7] = static_cast<char>('0' + (mission / 10) % 10);
    name[8] = static_cast<char>('0' + mission % 10);
    return name;
}

/// Every mission file is bundled in the data search path.
bool isAvailable();

} // namespace DuneCityCampaign

#endif // DUNECITYCAMPAIGN_H
