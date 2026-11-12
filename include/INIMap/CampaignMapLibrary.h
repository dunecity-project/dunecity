#ifndef CAMPAIGNMAPLIBRARY_H
#define CAMPAIGNMAPLIBRARY_H

#include <string>

// The active mod's campaign scenarios (SCEN?0??.INI, read from its PAK files or
// mod overrides) copied out as ordinary map files, so Custom Game can play them
// and the map editor can open them. They are written to the user's data
// directory at runtime from the player's own game data; nothing is shipped.
namespace CampaignMapLibrary {

// "1", "2 (A)", "2 (B)", ... "8 (B)", "9". Missions 1-22 map onto levels 1-9:
// level 1 and 9 have one mission, levels 2-7 three and level 8 two, one per
// region the player could pick on the campaign map.
inline std::string missionLabel(int mission) {
    const auto level = [](int m) { return m != 22 ? ((m + 1) / 3) + 1 : 9; };
    if(mission < 1 || mission > 22) return std::to_string(mission);
    int first = mission, count = 0;
    while(first > 1 && level(first - 1) == level(mission)) --first;
    for(int m = first; m <= 22 && level(m) == level(mission); ++m) ++count;
    std::string label = std::to_string(level(mission));
    if(count > 1) label += std::string(" (") + static_cast<char>('A' + mission - first) + ")";
    return label;
}

// "<user data>/maps/campaign/<active mod>/", created on demand.
std::string directory();

// Writes "<House> Mission <label>.ini" for every scenario the active mod has.
// Existing files are kept, so edits saved over them survive; delete a file to
// get the original back. Returns the directory.
std::string exportMaps();

} // namespace CampaignMapLibrary

#endif // CAMPAIGNMAPLIBRARY_H
