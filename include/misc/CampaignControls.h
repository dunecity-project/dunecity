#ifndef DUNECITY_CAMPAIGN_CONTROLS_H
#define DUNECITY_CAMPAIGN_CONTROLS_H

#include <DataTypes.h>
#include <stdexcept>

namespace CampaignControls {
// Levels 2–8 have three alternative scenarios; 1 and 9 have one each.
inline int firstMission(int level) {
    if(level < 1 || level > 9) throw std::invalid_argument("Campaign level must be between 1 and 9.");
    return level == 1 ? 1 : (level == 9 ? 22 : (level - 1) * 3 - 1);
}

// Only a player house can be the campaign's house; the Natives house never issues a skip.
inline bool maySkip(GameType type, int campaignHouse, int issuerHouse, bool human) {
    return isCampaignGameType(type) && human && isPlayerHouseID(campaignHouse)
        && issuerHouse == campaignHouse;
}
}
#endif
