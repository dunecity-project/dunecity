// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef REWORKEDCAMPAIGN_H
#define REWORKEDCAMPAIGN_H

#include <DataTypes.h>

#include <cctype>
#include <functional>
#include <string>

/**
    The reworked ("Remastered") campaign plays the RWRK_SCEN?0??.INI missions in
    data/maps/singleplayer through the normal campaign flow: house choice, briefing,
    map choice, debriefing and the same campaign AI. Dmitri Fatkin reworked most of the
    Atreides missions for Dune Legacy; DuneCity added the missing ones (docs/reworked-campaign.md).

    A game is reworked when its scenario filename carries the RWRK_ prefix. The filename
    already travels through saves, the next mission and co-op payloads, so no other state
    is stored and older saves are unaffected.
*/
namespace ReworkedCampaign {

inline constexpr const char* kPrefix = "RWRK_";
inline constexpr const char* kWatermark = "REMASTERED";
/// Bundled reworked missions, relative to each data search path.
inline constexpr const char* kMapDirectory = "maps/singleplayer";

/// True for "RWRK_SCENA012.INI" (any case), false for an original mission or a path.
inline bool isReworkedFilename(const std::string& filename) {
    if(filename.size() < 5 || filename.find_first_of("/\\") != std::string::npos) return false;
    for(int i = 0; i < 5; ++i) {
        if(std::toupper(static_cast<unsigned char>(filename[i])) != kPrefix[i]) return false;
    }
    return true;
}

/// "SCENA012.INI" -> "RWRK_SCENA012.INI"; a reworked name is returned unchanged.
inline std::string toReworked(const std::string& scenarioFilename) {
    return isReworkedFilename(scenarioFilename) ? scenarioFilename : kPrefix + scenarioFilename;
}

/// "RWRK_SCENA012.INI" for a house's scenario letter and a mission (1..22).
inline std::string scenarioFilename(char houseLetter, int mission) {
    std::string name = "RWRK_SCEN?0??.INI";
    name[9] = houseLetter;
    name[11] = static_cast<char>('0' + mission / 10);
    name[12] = static_cast<char>('0' + mission % 10);
    return name;
}

/// Every one of the 22 missions of this scenario letter exists (`exists` checks a file name).
inline bool isCompleteSet(char houseLetter, const std::function<bool(const std::string&)>& exists) {
    if(houseLetter == '?') return false;
    for(int mission = 1; mission <= 22; ++mission) {
        if(!exists(scenarioFilename(houseLetter, mission))) return false;
    }
    return true;
}

/// isCompleteSet() for this house in the bundled map directories of the data search path.
bool isAvailableForHouse(HOUSETYPE house);

/// The menus between reworked missions (briefing, map choice, debriefing, statistics) show the
/// watermark while this is set. The game screen draws its own from its init settings.
void setWatermarkVisible(bool visible);
bool isWatermarkVisible();

/// Sets the menu watermark for one campaign loop iteration and restores the previous state.
class WatermarkScope {
public:
    explicit WatermarkScope(bool visible) : previous(isWatermarkVisible()) { setWatermarkVisible(visible); }
    ~WatermarkScope() { setWatermarkVisible(previous); }
    WatermarkScope(const WatermarkScope&) = delete;
    WatermarkScope& operator=(const WatermarkScope&) = delete;
private:
    bool previous;
};

/// Draws the REMASTERED badge with its right edge at `right` and its top (or, with `fromBottom`,
/// its bottom) at `y`, in screen pixels.
void drawWatermark(int right, int y, bool fromBottom = false);

} // namespace ReworkedCampaign

#endif // REWORKEDCAMPAIGN_H
