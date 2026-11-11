#ifndef DUNECITY_STRUCTURETERRAINPOLICY_H
#define DUNECITY_STRUCTURETERRAINPOLICY_H

#include <data.h>

namespace DuneCity {
// Transparent structure corners must expose terrain, not the black render clear.
// This is independent of the global Dune2R crossfade: DuneCity house skins can
// be enabled while that crossfade is zero. Keep opaque vanilla culling intact.
inline bool shouldDrawTerrainBelowStructure(int itemID) noexcept {
    switch(itemID) {
        case Structure_NuclearPlant:
        case Structure_PoliceStation:
        case Structure_Stadium:
        case Structure_Airport:
        case Structure_AdvancedWindTrap:
        case Structure_AdvancedWindTrapMK2:
        case Structure_AdvancedWindTrapMK3:
        case Structure_Worfinery:
        case Structure_TechCenter:
        case Structure_ResearchLab:
        case Structure_Scoutpost:
        case Structure_Flamepost:
        case Structure_Chemipost:
        case Structure_LoveFactory:
        case Structure_ChaosFactory:
            return true;
        default:
            return false;
    }
}
}

#endif
