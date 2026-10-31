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

#ifndef DATA_H
#define DATA_H

// data definitions
typedef enum {
    Bullet_DRocket = 0,
    Bullet_LargeRocket = 1,
    Bullet_Rocket = 2,
    Bullet_TurretRocket = 3,
    Bullet_ShellSmall = 4,
    Bullet_ShellMedium = 5,
    Bullet_ShellLarge = 6,
    Bullet_ShellTurret = 7,
    Bullet_SmallRocket = 8,
    Bullet_Sonic = 9,
    Bullet_Sandworm = 10,
    Bullet_Flame = 11,            ///< Tornie: launcher-like fire missile with flame area damage
    Bullet_SonicTrike = 12,        ///< Tornie: shorter and weaker Sonic Trike wave
    Bullet_Heal = 13,             ///< Tornie: Deviator-style healing missile with direct flight
    Bullet_Slow = 14,             ///< Tornie 546: timed slowing, no conventional damage
    Bullet_Napalm = 15            ///< Tornie 546: Palace missile with persistent impact-area fire
} BulletID_enum;

typedef enum {
    Explosion_Small = 0,
    Explosion_Medium1 = 1,
    Explosion_Medium2 = 2,
    Explosion_Large1 = 3,
    Explosion_Large2 = 4,
    Explosion_Gas = 5,
    Explosion_ShellSmall = 6,
    Explosion_ShellMedium = 7,
    Explosion_ShellLarge = 8,
    Explosion_SmallUnit = 9,
    Explosion_Flames = 10,
    Explosion_SpiceBloom = 11,
    Explosion_FlameImpact = 12,       ///< Tornie: damaging ground fire without the destroyed-vehicle opening frames
    Explosion_FlameImpactVisual = 13 ///< Tornie: visual-only companion flames around the damaging impact
} ExplosionID_enum;

typedef enum {
    ItemID_Invalid = 0,
    ItemID_FirstID = 1,

    Structure_FirstID = 1,
    Structure_Barracks = 1,
    Structure_ConstructionYard = 2,
    Structure_GunTurret = 3,
    Structure_HeavyFactory = 4,
    Structure_HighTechFactory = 5,
    Structure_IX = 6,
    Structure_LightFactory = 7,
    Structure_Palace = 8,
    Structure_Radar = 9,
    Structure_Refinery = 10,
    Structure_RepairYard = 11,
    Structure_RocketTurret = 12,
    Structure_Silo = 13,
    Structure_Slab1 = 14,
    Structure_Slab4 = 15,
    Structure_StarPort = 16,
    Structure_Wall = 17,
    Structure_WindTrap = 18,
    Structure_WOR = 19,
    Structure_ZoneResidential = 20,  ///< DuneCity: Residential zone building (100 credits)
    Structure_ZoneCommercial  = 21,  ///< DuneCity: Commercial zone building (100 credits)
    Structure_ZoneIndustrial  = 22,  ///< DuneCity: Industrial zone building (100 credits)
    Structure_Road = 23,              ///< DuneCity: Road tile (25 credits)
    Structure_PowerLine = 24,         ///< DuneCity: Power line tile (15 credits)
    Structure_NuclearPlant = 25,      ///< DuneCity: Nuclear power plant (2000 credits, -2000 power)
    Structure_PoliceStation = 26,     ///< DuneCity: Police station (500 credits — SC TOOL_POLICESTATION). Sole source of police coverage in city mode.
    Structure_LastID = 26,            ///< End of contiguous structure range (save-compat boundary)

    Unit_FirstID = 27,
    Unit_Carryall = 27,
    Unit_Devastator = 28,
    Unit_Deviator = 29,
    Unit_Frigate = 30,
    Unit_Harvester = 31,
    Unit_Soldier = 32,
    Unit_Launcher = 33,
    Unit_MCV = 34,
    Unit_Ornithopter = 35,
    Unit_Quad = 36,
    Unit_Saboteur = 37,
    Unit_Sandworm = 38,
    Unit_SiegeTank = 39,
    Unit_SonicTank = 40,
    Unit_Tank = 41,
    Unit_Trike = 42,
    Unit_RaiderTrike = 43,
    Unit_Trooper = 44,
    Unit_Special = 45,
    Unit_Infantry = 46,
    Unit_Troopers = 47,
    Unit_LastID = 47,

    // Extended structure IDs — placed after units to preserve save-compat
    // for all pre-existing IDs.  isStructure() handles the gap.
    Structure_Stadium = 48,           ///< DuneCity: Stadium civic building (land-value boost, 3x3)
    Structure_Airport = 49,           ///< DuneCity: Airport economic building (commercial boost, 3x3)
    Structure_ExtLastID = 49,

    // Extended unit IDs — placed after extended structures to preserve
    // save-compat for all pre-existing IDs.  isUnit() handles the gap.
    Unit_AmbientAirplane = 50,        ///< DuneCity: Ambient city airplane (non-combat, spawned by Airport)
    Unit_AmbientHelicopter = 51,      ///< DuneCity: Ambient city helicopter (non-combat, spawned by Airport)

    // Tornie mod units — extended IDs after Ambient units to preserve save-compat.
    Unit_RocketTrike = 52,            ///< Tornie: upgraded Trike (rocket weapon, Light Factory, T9 U4)
    Unit_FlameTank = 53,              ///< Tornie: fire missile weapon, Heavy Factory, T9 U4
    Unit_EliteLauncher = 54,          ///< Tornie: upgraded Launcher, Heavy Factory, T9 U4
    Unit_EliteSiegeTank = 55,         ///< Tornie: upgraded Siege Tank, Heavy Factory, T9 U4
    Unit_ExtLastID = 55,

    // Additional extended structures after units (isStructure handles non-contiguous range)
    Structure_AdvancedWindTrap = 56,  ///< Tornie: Advanced Windtrap 3x3
    Structure_Worfinery = 57,        ///< Tornie: WOR + Refinery combo, can produce Troopers
    Structure_AdvancedWindTrapMK2 = 58, ///< Tornie: Advanced Windtrap 2x3

    // Tornie mod unit — distinct from vanilla Harvester with a dedicated sprite.
    Unit_RebelHarvester = 59,

    // Tornie mod building — Palace-equivalent that spawns 1-3 random vehicles
    // when House IX is unlocked. Spawned units use Hunt behavior.
    Structure_TechCenter = 60,
    Structure_AdvancedWindTrapMK3 = 61, ///< Tornie: Advanced Windtrap 3x2
    Structure_Scoutpost = 62,           ///< Tornie: small power/defense/recon post

    // Appended after every 1.0.517 object ID to preserve save compatibility.
    Unit_SonicTrike = 63,               ///< Tornie: Rebels-only light sonic vehicle
    Unit_ChemicalSiegeTank = 64,        ///< Tornie: custom-house chemical Siege Tank
    Structure_LoveFactory = 65,         ///< Tornie: animated 2x3 Love Factory
    Unit_ChemicalCarryall = 66,         ///< Tornie: Wildspade healing transport
    Delivery_Small = 67,               ///< Love Factory small reinforcement delivery
    Delivery_Medium = 68,              ///< Love Factory medium reinforcement delivery
    Delivery_Heavy = 69,               ///< Love Factory heavy reinforcement delivery
    Delivery_Support = 70,             ///< Love Factory support reinforcement delivery
    Structure_Flamepost = 71,          ///< Tornie: flame variant of the Scoutpost
    Structure_ChaosFactory = 72,       ///< Tornie: two random global unit offers
    Structure_Chemipost = 73,          ///< Tornie: healing variant of the Scoutpost

    // Tornie 1.0.546 additions: append-only across the DuneCity save and wire formats.
    Unit_Troopers5 = 74,
    Unit_Infantry5 = 75,
    Structure_Doublefinery = 76,
    Unit_FlameQuad = 77,
    Unit_GasInfantry = 78,
    Unit_FlameTrooper = 79,
    Unit_EliteOrnithopter = 80,
    Unit_RaiderQuad = 81,
    Unit_RangedTank = 82,
    Unit_SlowTank = 83,
    Structure_Slowpost = 84,
    Unit_CapacityHarvester = 85,
    Unit_FastHarvester = 86,
    Unit_TameSandworm = 87,
    ItemID_LastID = 87,

    Num_ItemID
} ItemID_enum;

typedef enum {
    Terrain_Slab,
    Terrain_Sand,
    Terrain_Rock,
    Terrain_Dunes,
    Terrain_Mountain,
    Terrain_Spice,
    Terrain_ThickSpice,
    Terrain_SpiceBloom,
    Terrain_SpecialBloom,
    Terrain_GreenSpice,
    Terrain_ThickGreenSpice,
    Terrain_GreenSpiceBloom,
    Terrain_RedSpice,
    Terrain_ThickRedSpice,
    Terrain_RedSpiceBloom,
    Terrain_PaleLilacSpice,
    Terrain_ThickPaleLilacSpice,
    Terrain_PaleLilacSpiceBloom,
    Terrain_WhiteSpice,
    Terrain_ThickWhiteSpice,
    Terrain_WhiteSpiceBloom
} TERRAINTYPE;

typedef enum {
    Terrain_HiddenIsland    = 0x0,
    Terrain_HiddenUp        = 0x1,
    Terrain_HiddenRight     = 0x2,
    Terrain_HiddenUpRight   = 0x3,
    Terrain_HiddenDown      = 0x4,
    Terrain_HiddenUpDown    = 0x5,
    Terrain_HiddenDownRight = 0x6,
    Terrain_HiddenNotLeft   = 0x7,
    Terrain_HiddenLeft      = 0x8,
    Terrain_HiddenUpLeft    = 0x9,
    Terrain_HiddenLeftRight = 0xA,
    Terrain_HiddenNotDown   = 0xB,
    Terrain_HiddenDownLeft  = 0xC,
    Terrain_HiddenNotRight  = 0xD,
    Terrain_HiddenNotUp     = 0xE,
    Terrain_HiddenFull      = 0xF
} HIDDENTYPE;

/**
    This function determines if the specified itemID is an unit or not.
    \param itemID   the ID of the item (e.g. Unit_Harvester)
    \return true if it is an unit, false otherwise
*/
inline bool isUnit(int itemID) { return (itemID >= Unit_FirstID && itemID <= Unit_LastID) || (itemID >= Unit_AmbientAirplane && itemID <= Unit_ExtLastID) || itemID == Unit_RebelHarvester || itemID == Unit_SonicTrike || itemID == Unit_ChemicalSiegeTank || itemID == Unit_ChemicalCarryall || itemID == Unit_Troopers5 || itemID == Unit_Infantry5 || (itemID >= Unit_FlameQuad && itemID <= Unit_SlowTank) || (itemID >= Unit_CapacityHarvester && itemID <= Unit_TameSandworm); }

/**
    This function determines if the specified itemID is a structure or not.
    \param itemID   the ID of the item (e.g. Structure_ConstructionYard)
    \return true if it is a structure, false otherwise
*/
inline bool isStructure(int itemID) { return (itemID >= Structure_FirstID && itemID <= Structure_LastID) || (itemID >= Structure_Stadium && itemID <= Structure_ExtLastID) || (itemID >= Structure_AdvancedWindTrap && itemID <= Structure_AdvancedWindTrapMK2) || itemID == Structure_TechCenter || itemID == Structure_AdvancedWindTrapMK3 || itemID == Structure_Scoutpost || itemID == Structure_LoveFactory || itemID == Structure_Flamepost || itemID == Structure_ChaosFactory || itemID == Structure_Chemipost || itemID == Structure_Doublefinery || itemID == Structure_Slowpost; }

/**
    This function determines if the specified itemID is a DuneCity zone structure.
    \param itemID   the ID of the item
    \return true if it is a zone structure, false otherwise
*/
inline bool isZoneStructure(int itemID) { return (itemID == Structure_ZoneResidential || itemID == Structure_ZoneCommercial || itemID == Structure_ZoneIndustrial); }

inline bool isTornieExclusiveItem(int itemID) {
    switch(itemID) {
        case Unit_RocketTrike:
        case Unit_FlameTank:
        case Unit_EliteLauncher:
        case Unit_EliteSiegeTank:
        case Structure_AdvancedWindTrap:
        case Structure_Worfinery:
        case Structure_AdvancedWindTrapMK2:
        case Unit_RebelHarvester:
        case Structure_TechCenter:
        case Structure_AdvancedWindTrapMK3:
        case Structure_Scoutpost:
        case Unit_SonicTrike:
        case Unit_ChemicalSiegeTank:
        case Structure_LoveFactory:
        case Unit_ChemicalCarryall:
        case Delivery_Small:
        case Delivery_Medium:
        case Delivery_Heavy:
        case Delivery_Support:
        case Structure_Flamepost:
        case Structure_ChaosFactory:
        case Structure_Chemipost:
        case Unit_Troopers5:
        case Unit_Infantry5:
        case Structure_Doublefinery:
        case Unit_FlameQuad:
        case Unit_GasInfantry:
        case Unit_FlameTrooper:
        case Unit_EliteOrnithopter:
        case Unit_RaiderQuad:
        case Unit_RangedTank:
        case Unit_SlowTank:
        case Structure_Slowpost:
        case Unit_CapacityHarvester:
        case Unit_FastHarvester:
        case Unit_TameSandworm:
            return true;
        default:
            return false;
    }
}


/**
    This function determines if the specified itemID is a flying unit or not.
    \param itemID   the ID of the item (e.g. Unit_Carryall)
    \return true if it is a flying unit, false otherwise
*/
inline bool isSandwormUnit(int itemID) { return itemID == Unit_Sandworm || itemID == Unit_TameSandworm; }
inline bool isOrnithopterUnit(int itemID) { return itemID == Unit_Ornithopter || itemID == Unit_EliteOrnithopter; }
inline bool isFlyingUnit(int itemID) { return (itemID == Unit_Carryall) || (itemID == Unit_ChemicalCarryall) || isOrnithopterUnit(itemID) || (itemID == Unit_Frigate) || (itemID == Unit_AmbientAirplane) || (itemID == Unit_AmbientHelicopter); }
inline bool isCarryallUnit(int itemID) {
    return (itemID == Unit_Carryall) || (itemID == Unit_ChemicalCarryall);
}

/**
    This function determines if the specified itemID is an ambient city aircraft.
    \param itemID   the ID of the item
    \return true if it is an ambient aircraft, false otherwise
*/
inline bool isAmbientUnit(int itemID) { return (itemID == Unit_AmbientAirplane) || (itemID == Unit_AmbientHelicopter); }

/**
    This function determines if the specified itemID is an infantry unit or not.
    \param itemID   the ID of the item (e.g. Unit_Carryall)
    \return true if it is an infantry unit, false otherwise
*/
inline bool isInfantryUnit(int itemID) { return (itemID == Unit_Soldier) || (itemID == Unit_Trooper) || (itemID == Unit_Infantry) || (itemID == Unit_Troopers) || (itemID == Unit_Saboteur) || (itemID == Unit_Troopers5) || (itemID == Unit_Infantry5) || (itemID == Unit_GasInfantry) || (itemID == Unit_FlameTrooper); }


#endif // DATA_H
