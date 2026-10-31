#pragma once

#include <data.h>

// Keep scenario forces and objectives when custom content is switched off.
// Callers restrict this policy to Tornie-family content with the option disabled.
inline int neutralVanillaMapItem(int item) {
    switch(item) {
        case Unit_RaiderQuad:
        case Unit_FlameQuad: return Unit_Quad;
        case Unit_RangedTank:
        case Unit_SlowTank:
        case Unit_TameSandworm: return Unit_Tank;
        case Unit_EliteOrnithopter: return Unit_Ornithopter;
        case Unit_CapacityHarvester:
        case Unit_FastHarvester: return Unit_Harvester;
        case Structure_Slowpost: return Structure_GunTurret;
        case Unit_GasInfantry:
        case Unit_FlameTrooper: return Unit_Trooper;
        case Unit_Infantry5: return Unit_Infantry;
        case Unit_Troopers5: return Unit_Troopers;
        case Unit_RocketTrike:
        case Unit_SonicTrike: return Unit_RaiderTrike;
        case Unit_FlameTank: return Unit_Tank;
        case Unit_EliteLauncher: return Unit_Launcher;
        case Unit_EliteSiegeTank:
        case Unit_ChemicalSiegeTank: return Unit_SiegeTank;
        case Unit_ChemicalCarryall: return Unit_Carryall;
        case Unit_RebelHarvester: return Unit_Harvester;
        case Structure_AdvancedWindTrap:
        case Structure_AdvancedWindTrapMK2:
        case Structure_AdvancedWindTrapMK3: return Structure_WindTrap;
        case Structure_Doublefinery: return Structure_Refinery;
        case Structure_Worfinery: return Structure_WOR;
        case Structure_TechCenter: return Structure_Palace;
        case Structure_Scoutpost: return Structure_Radar;
        case Structure_LoveFactory: return Structure_HeavyFactory;
        case Structure_Flamepost: return Structure_GunTurret;
        case Structure_ChaosFactory: return Structure_StarPort;
        case Structure_Chemipost: return Structure_RocketTurret;
        default: return item;
    }
}
