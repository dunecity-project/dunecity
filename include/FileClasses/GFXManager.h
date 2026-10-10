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

#ifndef GFXMANAGER_H
#define GFXMANAGER_H

#include "Animation.h"
#include "Shpfile.h"
#include "Wsafile.h"
#include <DataTypes.h>

#include <misc/SDL2pp.h>
#include <misc/Dune2RInfantryOverlay.h>

#include <string>
#include <array>
#include <map>
#include <memory>
#include <vector>

class EnhancedAtlasCache;

#define NUM_TERRAIN_TILES_X 11
#define NUM_TERRAIN_TILES_Y 8
#define NUM_MAPCHOICEPIECES 28
#define NUM_WINDTRAP_ANIMATIONS (2*STRUCTURE_ANIMATIONTIMER+4)
#define NUM_WINDTRAP_ANIMATIONS_PER_ROW 10
#define NUM_STATIC_ANIMATIONS_PER_ROW 7

// ObjPics
typedef enum {
    ObjPic_Tank_Base,
    ObjPic_Tank_Gun,
    ObjPic_Siegetank_Base,
    ObjPic_Siegetank_Gun,
    ObjPic_Devastator_Base,
    ObjPic_Devastator_Gun,
    ObjPic_Sonictank_Gun,
    ObjPic_Launcher_Gun,
    ObjPic_DeviatorGunTornie,      ///< Tornie: green Deviator turret overlay
    ObjPic_RocketTrike,             ///< Tornie: dedicated sprite (data/RocketTrike.png)
    ObjPic_FlameTankGunTornie,      ///< Tornie: fire-coloured launcher turret overlay
    ObjPic_EliteSiegeTankGunTornie, ///< Tornie: elite Siege Tank turret overlay
    ObjPic_ChemicalSiegeTankGunTornie, ///< Tornie: chemical Siege Tank turret overlay
    ObjPic_Quad,
    ObjPic_Trike,
    ObjPic_Harvester,
    ObjPic_Harvester_Sand,
    ObjPic_MCV,
    ObjPic_Carryall,
    ObjPic_CarryallShadow,
    ObjPic_Frigate,
    ObjPic_FrigateShadow,
    ObjPic_Ornithopter,
    ObjPic_OrnithopterShadow,
    ObjPic_Trooper,
    ObjPic_Troopers,
    ObjPic_Soldier,
    ObjPic_Infantry,
    ObjPic_Saboteur,
    ObjPic_Sandworm,
    ObjPic_ConstructionYard,
    ObjPic_Windtrap,
    ObjPic_AdvancedWindTrap,       ///< Tornie: 3x3 Advanced Windtrap with vanilla Windtrap color-cycle animation
    ObjPic_AdvancedWindTrap2x3,    ///< Tornie: 2x3 Advanced Windtrap with vanilla Windtrap color-cycle animation
    ObjPic_AdvancedWindTrap3x2,    ///< Tornie: 3x2 Advanced Windtrap with vanilla Windtrap color-cycle animation
    ObjPic_Refinery,
    ObjPic_Barracks,
    ObjPic_WOR,
    ObjPic_Radar,
    ObjPic_LightFactory,
    ObjPic_Silo,
    ObjPic_HeavyFactory,
    ObjPic_HighTechFactory,
    ObjPic_IX,
    ObjPic_Palace,
    ObjPic_RepairYard,
    ObjPic_Starport,
    ObjPic_GunTurret,
    ObjPic_RocketTurret,
    ObjPic_Wall,
    ObjPic_Bullet_SmallRocket,
    ObjPic_Bullet_MediumRocket,
    ObjPic_Bullet_LargeRocket,
    ObjPic_Bullet_Small,
    ObjPic_Bullet_Medium,
    ObjPic_Bullet_Large,
    ObjPic_Bullet_Sonic,
    ObjPic_Bullet_SonicTemp,
    ObjPic_Hit_Gas,
    ObjPic_Hit_ShellSmall,
    ObjPic_Hit_ShellMedium,
    ObjPic_Hit_ShellLarge,
    ObjPic_ExplosionSmall,
    ObjPic_ExplosionMedium1,
    ObjPic_ExplosionMedium2,
    ObjPic_ExplosionLarge1,
    ObjPic_ExplosionLarge2,
    ObjPic_ExplosionSmallUnit,
    ObjPic_ExplosionFlames,
    ObjPic_ExplosionSpiceBloom,
    ObjPic_DeadInfantry,
    ObjPic_DeadAirUnit,
    ObjPic_Smoke,
    ObjPic_SandwormShimmerMask,
    ObjPic_SandwormShimmerTemp,
    ObjPic_Terrain,
    ObjPic_Terrain_GreenSpice,
    ObjPic_Terrain_RedSpice,
    ObjPic_DestroyedStructure,
    ObjPic_RockDamage,
    ObjPic_SandDamage,
    ObjPic_Terrain_Hidden,
    ObjPic_Terrain_HiddenFog,
    ObjPic_Terrain_Tracks,
    ObjPic_Star,
    ObjPic_RebelHarvester,        ///< Tornie: dedicated sprite for Rebel-only Harvester
    ObjPic_Worfinery,             ///< Tornie: WOR + Refinery combo (48x64 = 2 vertical frames at 3x2)
    ObjPic_TechCenter,            ///< Tornie: Tech Center (Palace-equivalent, 48x64 = 2 vertical frames at 3x2)
    ObjPic_Scoutpost,             ///< Tornie: Scoutpost (16x32 = 2 vertical frames at 1x1)
    ObjPic_LoveFactory,           ///< Tornie: animated Love Factory (2x3, six active frames)
    ObjPic_ZoneResidential,
    ObjPic_ZoneCommercial,
    ObjPic_ZoneIndustrial,
    ObjPic_CityRoad,
    ObjPic_NuclearPlant,   ///< DuneCity: Micropolis nuclear-plant sprite (3x3 footprint)
    ObjPic_PoliceStation,  ///< DuneCity: Micropolis police-station sprite (2x2 footprint)
    ObjPic_Stadium,        ///< DuneCity: Micropolis stadium sprite (3x3 footprint)
    ObjPic_Airport,        ///< DuneCity: Micropolis airport sprite (3x3 footprint)
    ObjPic_Hospital,       ///< DuneCity: Micropolis hospital sprite (2x2, auto-placed on residential)
    ObjPic_Church,         ///< DuneCity: Micropolis church sprite (2x2, auto-placed on residential)
    ObjPic_SonicTrike,     ///< Tornie: Rebels-only light sonic vehicle
    ObjPic_EliteLauncherGunTornie, ///< Tornie: elite Launcher turret overlay
    ObjPic_RebelSonicTankGun,      ///< Tornie: Rebels-only violet Sonic Tank turret
    ObjPic_HarvestankGunTornie,     ///< Tornie: Harvestank turret overlay
    ObjPic_ChemicalCarryall,        ///< Tornie: dedicated 8x2 healing Carryall atlas
    ObjPic_Flamepost,               ///< Tornie: dedicated Flamepost atlas
    ObjPic_Chemipost,               ///< Tornie: dedicated healing post atlas
    ObjPic_ChaosFactory,            ///< Tornie: animated 3x2 Chaos Factory atlas
    ObjPic_CityHelicopter, ///< DuneCity: original Micropolis traffic helicopter (8 headings, 32x32)
    ObjPic_CityAirplane,   ///< DuneCity: original Micropolis airplane (8 headings + 3 take-off rows, 48x48)
    NUM_OBJPICS
} ObjPic_enum;

static const std::array<std::string, NUM_OBJPICS> ObjPicNames =  { { "Tank_Base", "Tank_Gun", "Siegetank_Base", "Siegetank_Gun", "Devastator_Base",
    "Devastator_Gun", "Sonictank_Gun", "Launcher_Gun", "DeviatorGunTornie", "RocketTrike", "FlameTankGunTornie", "EliteSiegeTankGunTornie", "ChemicalSiegeTankGunTornie",
    "Quad", "Trike", "Harvester", "Harvester_Sand", "MCV", "Carryall", "CarryallShadow",
    "Frigate", "FrigateShadow", "Ornithopter", "OrnithopterShadow", "Trooper", "Troopers", "Soldier", "Infantry", "Saboteur", "Sandworm",
    "ConstructionYard", "Windtrap", "AdvancedWindTrap", "AdvancedWindTrap2x3", "AdvancedWindTrap3x2", "Refinery", "Barracks", "WOR", "Radar", "LightFactory", "Silo", "HeavyFactory", "HighTechFactory",
    "IX", "Palace", "RepairYard", "Starport", "GunTurret", "RocketTurret", "Wall",
    "Bullet_SmallRocket", "Bullet_MediumRocket", "Bullet_LargeRocket", "Bullet_Small", "Bullet_Medium", "Bullet_Large", "Bullet_Sonic",
    "Bullet_SonicTemp", "Hit_Gas", "Hit_ShellSmall", "Hit_ShellMedium", "Hit_ShellLarge", "ExplosionSmall", "ExplosionMedium1",
    "ExplosionMedium2", "ExplosionLarge1", "ExplosionLarge2", "ExplosionSmallUnit", "ExplosionFlames", "ExplosionSpiceBloom",
    "DeadInfantry", "DeadAirUnit", "Smoke", "SandwormShimmerMask", "SandwormShimmerTemp", "Terrain", "Terrain_GreenSpice", "Terrain_RedSpice", "DestroyedStructure", "RockDamage",
    "SandDamage", "Terrain_Hidden", "Terrain_HiddenFog", "Terrain_Tracks", "Star", "RebelHarvester", "Worfinery", "TechCenter", "Scoutpost", "LoveFactory",
    "ZoneResidential", "ZoneCommercial", "ZoneIndustrial", "CityRoad", "NuclearPlant", "PoliceStation",
    "Stadium", "Airport", "Hospital", "Church", "SonicTrike", "EliteLauncherGunTornie", "RebelSonicTankGun",
    "HarvestankGunTornie", "ChemicalCarryall", "Flamepost", "Chemipost", "ChaosFactory",
    "CityHelicopter", "CityAirplane" } };

#define GROUNDUNIT_ROW(i) (i+2)|TILE_NORMAL,(i+1)|TILE_NORMAL,i|TILE_NORMAL,(i+1)|TILE_FLIPV,(i+2)|TILE_FLIPV,(i+3)|TILE_FLIPV, (i+4)|TILE_NORMAL,(i+3)|TILE_NORMAL
#define AIRUNIT_ROW(i) (i+2)|TILE_NORMAL,(i+1)|TILE_NORMAL,i|TILE_NORMAL,(i+1)|TILE_FLIPV,(i+2)|TILE_FLIPV,(i+1)|TILE_ROTATE, i|TILE_FLIPH,(i+1)|TILE_FLIPH
#define ORNITHOPTER_ROW(i) (i+6)|TILE_NORMAL,(i+3)|TILE_NORMAL,i|TILE_NORMAL,(i+3)|TILE_FLIPV,(i+6)|TILE_FLIPV,(i+3)|TILE_ROTATE, i|TILE_FLIPH,(i+3)|TILE_FLIPH
#define INFANTRY_ROW(i) (i+3)|TILE_NORMAL,i|TILE_NORMAL,(i+3)|TILE_FLIPV,(i+6)|TILE_NORMAL
#define MULTIINFANTRY_ROW(i) (i+4)|TILE_NORMAL,i|TILE_NORMAL,(i+4)|TILE_FLIPV,(i+8)|TILE_NORMAL
#define HARVESTERSAND_ROW(i) (i+6)|TILE_NORMAL,(i+3)|TILE_NORMAL,i|TILE_NORMAL,(i+3)|TILE_FLIPV,(i+6)|TILE_FLIPV,(i+9)|TILE_FLIPV,(i+12)|TILE_NORMAL,(i+9)|TILE_NORMAL
#define ROCKET_ROW(i)   (i+4)|TILE_NORMAL,(i+3)|TILE_NORMAL,(i+2)|TILE_NORMAL,(i+1)|TILE_NORMAL,i|TILE_NORMAL,(i+1)|TILE_FLIPV,(i+2)|TILE_FLIPV,(i+3)|TILE_FLIPV, \
                        (i+4)|TILE_FLIPV,(i+3)|TILE_ROTATE,(i+2)|TILE_ROTATE, (i+1)|TILE_ROTATE,i|TILE_FLIPH,(i+1)|TILE_FLIPH,(i+2)|TILE_FLIPH,(i+3)|TILE_FLIPH


// SmallDetailPics
typedef enum {
    Picture_Barracks,
    Picture_ConstructionYard,
    Picture_Carryall,
    Picture_Devastator,
    Picture_Deviator,
    Picture_DeathHand,
    Picture_Fremen,
    Picture_Frigate,
    Picture_GunTurret,
    Picture_Harvester,
    Picture_HeavyFactory,
    Picture_HighTechFactory,
    Picture_Soldier,
    Picture_IX,
    Picture_Launcher,
    Picture_LightFactory,
    Picture_MCV,
    Picture_Ornithopter,
    Picture_Palace,
    Picture_Quad,
    Picture_Radar,
    Picture_RaiderTrike,
    Picture_Refinery,
    Picture_RepairYard,
    Picture_RocketTurret,
    Picture_Saboteur,
    Picture_Sandworm,
    Picture_Sardaukar,
    Picture_SiegeTank,
    Picture_Silo,
    Picture_Slab1,
    Picture_Slab4,
    Picture_SonicTank,
    Picture_Special,
    Picture_StarPort,
    Picture_Tank,
    Picture_Trike,
    Picture_Trooper,
    Picture_Wall,
    Picture_WindTrap,
    Picture_WOR,
    Picture_ZoneResidential,
    Picture_ZoneCommercial,
    Picture_ZoneIndustrial,
    Picture_Road,
    Picture_PowerLine,
    Picture_NuclearPlant,
    Picture_PoliceStation,
    Picture_Stadium,
    Picture_Airport,
    Picture_AdvancedWindTrap,
    Picture_RocketTrike,           ///< Tornie: portrait from RocketTrikeIcon.png (91x55)
    Picture_FlameTank,             ///< Tornie: portrait from FlameTankIcon.png (91x55)
    Picture_EliteLauncher,         ///< Tornie: portrait from EliteLauncherIcon.png (91x55)
    Picture_EliteSiegeTank,        ///< Tornie: portrait from EliteSiegeTankIcon.png (91x55)
    Picture_ChemicalSiegeTank,     ///< Tornie: portrait from ChemicalSiegeTankIcon.png (91x55)
    Picture_Worfinery,             ///< Tornie: portrait from WorfineryIcon.png (91x55)
    Picture_TechCenter,            ///< Tornie: portrait from TechCenterIcon.png (91x55)
    Picture_Scoutpost,             ///< Tornie: portrait from ScoutpostIcon.png
    Picture_LoveFactory,          ///< Tornie: portrait from LoveFactoryIcon.png
    Picture_PalaceLightVehicles,   ///< Tornie: Neutral/Rebels Palace Trike/Quad call icon
    Picture_PalaceRebelsCharging,  ///< Tornie main Rebels: random Palace ability charging icon
    Picture_SonicTrike,            ///< Tornie: portrait from SonicTrikeIcon.png
    Picture_Harvestank,            ///< Tornie: portrait from HarvestankIcon.png
    Picture_ChemicalCarryall,      ///< Tornie: Chemical Carryall portrait
    Picture_Flamepost,             ///< Tornie: Flamepost portrait
    Picture_Chemipost,             ///< Tornie: Chemipost portrait
    Picture_ChaosFactory,          ///< Tornie: Chaos Factory portrait
    NUM_SMALLDETAILPICS
} SmallDetailPics_Enum;

// tiny pictures used for tutorial hints (has the same order as ItemID_enum, except the first entry)
typedef enum {
    TinyPicture_Spice = 0,
    TinyPicture_Barracks = 1,
    TinyPicture_ConstructionYard = 2,
    TinyPicture_GunTurret = 3,
    TinyPicture_HeavyFactory = 4,
    TinyPicture_HighTechFactory = 5,
    TinyPicture_IX = 6,
    TinyPicture_LightFactory = 7,
    TinyPicture_Palace = 8,
    TinyPicture_Radar = 9,
    TinyPicture_Refinery = 10,
    TinyPicture_RepairYard = 11,
    TinyPicture_RocketTurret = 12,
    TinyPicture_Silo = 13,
    TinyPicture_Slab1 = 14,
    TinyPicture_Slab4 = 15,
    TinyPicture_StarPort = 16,
    TinyPicture_Wall = 17,
    TinyPicture_WindTrap = 18,
    TinyPicture_WOR = 19,
    TinyPicture_Carryall = 20,
    TinyPicture_Devastator = 21,
    TinyPicture_Deviator = 22,
    TinyPicture_Frigate = 23,
    TinyPicture_Harvester = 24,
    TinyPicture_Soldier = 25,
    TinyPicture_Launcher = 26,
    TinyPicture_MCV = 27,
    TinyPicture_Ornithopter = 28,
    TinyPicture_Quad = 29,
    TinyPicture_Saboteur = 30,
    TinyPicture_Sandworm = 31,
    TinyPicture_SiegeTank = 32,
    TinyPicture_SonicTank = 33,
    TinyPicture_Tank = 34,
    TinyPicture_Trike = 35,
    TinyPicture_RaiderTrike = 36,
    TinyPicture_Trooper = 37,
    TinyPicture_Special = 38,
    TinyPicture_Infantry = 39,
    TinyPicture_Troopers = 40,
    NUM_TINYPICTURE
} TinyPicture_Enum;

// UI Graphics
typedef enum {
    UI_RadarAnimation,
    UI_CursorNormal,
    UI_CursorUp,
    UI_CursorRight,
    UI_CursorDown,
    UI_CursorLeft,
    UI_CursorMove_Zoomlevel0,
    UI_CursorAttack_Zoomlevel0,
    UI_CursorHeal_Zoomlevel0,
    UI_CursorCapture_Zoomlevel0,
    UI_CursorCarryallDrop_Zoomlevel0,
    UI_SendToRepairIcon,
    UI_ReturnIcon,
    UI_DeployIcon,
    UI_DestructIcon,
    UI_CreditsDigits,
    UI_SideBar,
    UI_Indicator,
    UI_InvalidPlace_Zoomlevel0,
    UI_InvalidPlace_Zoomlevel1,
    UI_InvalidPlace_Zoomlevel2,
    UI_ValidPlace_Zoomlevel0,
    UI_ValidPlace_Zoomlevel1,
    UI_ValidPlace_Zoomlevel2,
    UI_GreyPlace_Zoomlevel0,
    UI_GreyPlace_Zoomlevel1,
    UI_GreyPlace_Zoomlevel2,
    UI_MenuBackground,
    UI_GameStatsBackground,
    UI_SelectionBox_Zoomlevel0,
    UI_SelectionBox_Zoomlevel1,
    UI_SelectionBox_Zoomlevel2,
    UI_OtherPlayerSelectionBox_Zoomlevel0,
    UI_OtherPlayerSelectionBox_Zoomlevel1,
    UI_OtherPlayerSelectionBox_Zoomlevel2,
    UI_TopBar,
    UI_ButtonUp,
    UI_ButtonUp_Pressed,
    UI_ButtonDown,
    UI_ButtonDown_Pressed,
    UI_BuilderListUpperCap,
    UI_BuilderListLowerCap,
    UI_CustomGamePlayersArrow,
    UI_CustomGamePlayersArrowNeutral,
    UI_MessageBox,
    UI_Mentat,
    UI_Mentat_Pressed,
    UI_Options,
    UI_Options_Pressed,
    UI_Upgrade,
    UI_Upgrade_Pressed,
    UI_Repair,
    UI_Repair_Pressed,
    UI_HouseSelect,
    UI_SelectYourHouseLarge,
    UI_Herald_Colored,
    UI_Herald_ColoredLarge,
    UI_Herald_Grey,
    UI_Herald_ArrowLeft,
    UI_Herald_ArrowLeftLarge,
    UI_Herald_ArrowLeftHighlight,
    UI_Herald_ArrowLeftHighlightLarge,
    UI_Herald_ArrowRight,
    UI_Herald_ArrowRightLarge,
    UI_Herald_ArrowRightHighlight,
    UI_Herald_ArrowRightHighlightLarge,
    UI_Minus,
    UI_Minus_Active,
    UI_Minus_Pressed,
    UI_Plus,
    UI_Plus_Active,
    UI_Plus_Pressed,
    UI_MissionSelect,
    UI_OptionsMenu,
    UI_LoadSaveWindow,
    UI_NewMapWindow,
    UI_GameMenu,
    UI_MentatBackground,
    UI_MentatBackgroundBene,
    UI_MentatHouseChoiceInfoQuestion,
    UI_MentatYes,
    UI_MentatYes_Pressed,
    UI_MentatNo,
    UI_MentatNo_Pressed,
    UI_MentatExit,
    UI_MentatExit_Pressed,
    UI_MentatProcced,
    UI_MentatProcced_Pressed,
    UI_MentatRepeat,
    UI_MentatRepeat_Pressed,
    UI_PlanetBackground,
    UI_MenuButtonBorder,
    UI_DuneLegacy,
    UI_MapChoiceScreen,
    UI_MapChoicePlanet,
    UI_MapChoiceMapOnly,
    UI_MapChoiceMap,
    UI_MapChoiceClickMap,
    UI_MapChoiceArrow_None,
    UI_MapChoiceArrow_LeftUp,
    UI_MapChoiceArrow_Up,
    UI_MapChoiceArrow_RightUp,
    UI_MapChoiceArrow_Right,
    UI_MapChoiceArrow_RightDown,
    UI_MapChoiceArrow_Down,
    UI_MapChoiceArrow_LeftDown,
    UI_MapChoiceArrow_Left,
    UI_StructureSizeLattice,
    UI_StructureSizeConcrete,
    UI_MapEditor_SideBar,
    UI_MapEditor_BottomBar,
    UI_MapEditor_ExitIcon,
    UI_MapEditor_NewIcon,
    UI_MapEditor_LoadIcon,
    UI_MapEditor_SaveIcon,
    UI_MapEditor_UndoIcon,
    UI_MapEditor_RedoIcon,
    UI_MapEditor_PlayerIcon,
    UI_MapEditor_MapSettingsIcon,
    UI_MapEditor_ChoamIcon,
    UI_MapEditor_ReinforcementsIcon,
    UI_MapEditor_TeamsIcon,
    UI_MapEditor_MirrorNoneIcon,
    UI_MapEditor_MirrorHorizontalIcon,
    UI_MapEditor_MirrorVerticalIcon,
    UI_MapEditor_MirrorBothIcon,
    UI_MapEditor_MirrorPointIcon,
    UI_MapEditor_ArrowUp,
    UI_MapEditor_ArrowUp_Active,
    UI_MapEditor_ArrowDown,
    UI_MapEditor_ArrowDown_Active,
    UI_MapEditor_Plus,
    UI_MapEditor_Plus_Active,
    UI_MapEditor_Minus,
    UI_MapEditor_Minus_Active,
    UI_MapEditor_RotateLeftIcon,
    UI_MapEditor_RotateLeftHighlightIcon,
    UI_MapEditor_RotateRightIcon,
    UI_MapEditor_RotateRightHighlightIcon,
    UI_MapEditor_Sand,
    UI_MapEditor_Dunes,
    UI_MapEditor_SpecialBloom,
    UI_MapEditor_Spice,
    UI_MapEditor_ThickSpice,
    UI_MapEditor_GreenSpice,
    UI_MapEditor_ThickGreenSpice,
    UI_MapEditor_GreenSpiceBloom,
    UI_MapEditor_RedSpice,
    UI_MapEditor_ThickRedSpice,
    UI_MapEditor_RedSpiceBloom,
    UI_MapEditor_SpiceBloom,
    UI_MapEditor_Slab,
    UI_MapEditor_Rock,
    UI_MapEditor_Mountain,
    UI_MapEditor_Slab1,
    UI_MapEditor_Wall,
    UI_MapEditor_GunTurret,
    UI_MapEditor_RocketTurret,
    UI_MapEditor_ConstructionYard,
    UI_MapEditor_Windtrap,
    UI_MapEditor_AdvancedWindTrap,   ///< Tornie: 3x3 high-output power building
    UI_MapEditor_AdvancedWindTrapMK2, ///< Tornie: 2x3 high-output power building
    UI_MapEditor_AdvancedWindTrapMK3, ///< Tornie: 3x2 high-output power building
    UI_MapEditor_Radar,
    UI_MapEditor_Silo,
    UI_MapEditor_IX,
    UI_MapEditor_Barracks,
    UI_MapEditor_WOR,
    UI_MapEditor_Worfinery,            ///< Tornie: WOR + Refinery combo
    UI_MapEditor_LightFactory,
    UI_MapEditor_Refinery,
    UI_MapEditor_HighTechFactory,
    UI_MapEditor_HeavyFactory,
    UI_MapEditor_RepairYard,
    UI_MapEditor_Starport,
    UI_MapEditor_Palace,
    UI_MapEditor_TechCenter,               ///< Tornie: Palace-equivalent that spawns vehicles
    UI_MapEditor_Scoutpost,                ///< Tornie: power/defense/recon post
    UI_MapEditor_LoveFactory,             ///< Tornie: animated 2x3 Love Factory
    UI_MapEditor_Soldier,
    UI_MapEditor_Trooper,
    UI_MapEditor_Harvester,
    UI_MapEditor_RebelHarvester,         ///< Tornie: Rebel-only Harvester
    UI_MapEditor_Infantry,
    UI_MapEditor_Troopers,
    UI_MapEditor_MCV,
    UI_MapEditor_Trike,
    UI_MapEditor_Raider,
    UI_MapEditor_Quad,
    UI_MapEditor_Tank,
    UI_MapEditor_SiegeTank,
    UI_MapEditor_Launcher,
    UI_MapEditor_Devastator,
    UI_MapEditor_SonicTank,
    UI_MapEditor_Deviator,
    UI_MapEditor_RocketTrike,           ///< Tornie: upgraded Trike
    UI_MapEditor_FlameTank,             ///< Tornie: sonic-line flame weapon
    UI_MapEditor_EliteLauncher,         ///< Tornie: upgraded Launcher
    UI_MapEditor_EliteSiegeTank,        ///< Tornie: upgraded Siege Tank
    UI_MapEditor_ChemicalSiegeTank,     ///< Tornie: chemical Siege Tank
    UI_MapEditor_Saboteur,
    UI_MapEditor_Sandworm,
    UI_MapEditor_SpecialUnit,
    UI_MapEditor_Carryall,
    UI_MapEditor_Ornithopter,
    UI_MapEditor_Pen1x1,
    UI_MapEditor_Pen3x3,
    UI_MapEditor_Pen5x5,
    UI_MapEditor_ZoneResidential,   ///< DuneCity: map-editor icon for R zone
    UI_MapEditor_ZoneCommercial,    ///< DuneCity: map-editor icon for C zone
    UI_MapEditor_ZoneIndustrial,    ///< DuneCity: map-editor icon for I zone
    UI_MapEditor_NuclearPlant,      ///< DuneCity: map-editor icon for nuclear plant
    UI_MapEditor_Road,              ///< DuneCity: map-editor icon for road tile
    UI_MapEditor_SonicTrike,        ///< Tornie: Rebels-only light sonic vehicle
    UI_MapEditor_ChemicalCarryall,  ///< Tornie: healing Carryall with special-unit star
    UI_MapEditor_Flamepost,         ///< Tornie: dedicated Flamepost
    UI_MapEditor_Chemipost,         ///< Tornie: dedicated healing post
    UI_MapEditor_ChaosFactory,      ///< Tornie: 3x2 Chaos Factory
    UI_MapEditor_PoliceStation,
    UI_MapEditor_Stadium,
    UI_MapEditor_Airport,
    NUM_UIGRAPHICS
} UIGraphics_Enum;

//Animation
typedef enum {
    Anim_HarkonnenEyes,
    Anim_HarkonnenMouth,
    Anim_HarkonnenShoulder,
    Anim_AtreidesEyes,
    Anim_AtreidesMouth,
    Anim_AtreidesShoulder,
    Anim_AtreidesBook,
    Anim_OrdosEyes,
    Anim_OrdosMouth,
    Anim_OrdosShoulder,
    Anim_OrdosRing,
    Anim_FremenEyes,
    Anim_FremenMouth,
    Anim_FremenShoulder,
    Anim_FremenBook,
    Anim_SardaukarEyes,
    Anim_SardaukarMouth,
    Anim_SardaukarShoulder,
    Anim_MercenaryEyes,
    Anim_MercenaryMouth,
    Anim_MercenaryShoulder,
    Anim_MercenaryRing,
    Anim_BeneEyes,
    Anim_BeneMouth,
    Anim_HarkonnenPlanet,
    Anim_AtreidesPlanet,
    Anim_OrdosPlanet,
    Anim_FremenPlanet,
    Anim_SardaukarPlanet,
    Anim_MercenaryPlanet,
    Anim_NeutralPlanet,
    Anim_RebelsPlanet,
    Anim_Win1,
    Anim_Win2,
    Anim_Lose1,
    Anim_Lose2,
    Anim_Barracks,
    Anim_Carryall,
    Anim_ConstructionYard,
    Anim_Fremen,
    Anim_DeathHand,
    Anim_Devastator,
    Anim_Harvester,
    Anim_Radar,
    Anim_HighTechFactory,
    Anim_SiegeTank,
    Anim_HeavyFactory,
    Anim_Trooper,
    Anim_Infantry,
    Anim_IX,
    Anim_LightFactory,
    Anim_Tank,
    Anim_MCV,
    Anim_Deviator,
    Anim_Ornithopter,
    Anim_Raider,
    Anim_Palace,
    Anim_Quad,
    Anim_Refinery,
    Anim_RepairYard,
    Anim_Launcher,
    Anim_RocketTurret,
    Anim_Saboteur,
    Anim_Slab1,
    Anim_SonicTank,
    Anim_StarPort,
    Anim_Silo,
    Anim_Trike,
    Anim_GunTurret,
    Anim_Wall,
    Anim_WindTrap,
    Anim_WOR,
    Anim_Sandworm,
    Anim_Sardaukar,
    Anim_Frigate,
    Anim_Slab4,
    NUM_ANIMATION
} Animation_enum;


class GFXManager {
public:
    enum class EnhancedUnitState {
        Idle,
        Movement,
        Combat,
        DamageSmoking,
        DamageDamaged,
        DamageExploded,
        DamageAftermath,
        DamageDissipation,
        CombatReturn,
        Count
    };

    enum class EnhancedRenderMode {
        Layered,
        FullAnimation,
        Random
    };

    enum class EnhancedBuildingState {
        Placement,
        Construction,
        Idle,
        Working,
        Damaged,
        Repair,
        Destroyed,
        Count
    };

    enum class DuneCityZoneActivity {
        Idle,
        Active,
        Growing,
        Damaged,
        Repair,
        Count
    };

    struct EnhancedUnitEditorInfo {
        std::string sourceUnit;
        int itemID = -1;
        int houseID = -1;
        std::array<std::array<bool, 8>, static_cast<size_t>(EnhancedUnitState::Count)> available{};
    };

    GFXManager();
    ~GFXManager();

    GFXManager(const GFXManager &) = delete;
    GFXManager(GFXManager &&) = default;
    GFXManager& operator=(const GFXManager &) = default;
    GFXManager& operator=(GFXManager &&) = default;

    SDL_Texture*     getZoomedObjPic(unsigned int id, int house, unsigned int z);
    SDL_Texture*     getZoomedObjPic(unsigned int id, unsigned int z) { return getZoomedObjPic(id, HOUSE_HARKONNEN, z); };
    zoomable_texture getObjPic(unsigned int id, int house=HOUSE_HARKONNEN);
    bool             drawHDObjPic(unsigned int id, int house, unsigned int z,
                                  int col, int numCols, int row, int numRows,
                                  int x, int y);
    bool             drawEnhancedUnit(int itemID, int house, unsigned int z,
                                      EnhancedUnitState state, int direction,
                                      Uint32 elapsedMs, int x, int y);
    Uint32           recordEnhancedInfantryFall(int itemID, int house,
                                                 int direction, Uint32 nowMs);
    bool             drawEnhancedInfantryFall(Uint32 token, int house,
                                               Uint32 nowMs, int x, int y);
    bool             drawEnhancedTerrain(int terrainType, int variant,
                                         const SDL_Rect& destination);
    bool             drawEnhancedBuilding(int itemID, int house, unsigned int z,
                                           EnhancedBuildingState state,
                                           Uint32 elapsedMs, int anchorX, int anchorY);
    Uint32           getEnhancedBuildingAnimationDuration(int itemID, int house,
                                                           EnhancedBuildingState state);
    bool             drawDuneCityZone(int itemID, int house, unsigned int z,
                                      int density, int valueTier,
                                      DuneCityZoneActivity activity,
                                      Uint32 elapsedMs, int anchorX, int anchorY,
                                      const SDL_Rect* previewBounds = nullptr);
    bool             drawDuneCityBuilding(int itemID, int house, int frame,
                                          const SDL_Rect& destination);
    Uint8            getDune2RVisualBlend();
    bool             isDune2RVisualsEnabled();
    void             setDune2RVisualsEnabled(bool enabled);
    void             toggleDune2RVisuals();
    Uint32           getEnhancedUnitAnimationDuration(int itemID, int house,
                                                      EnhancedUnitState state,
                                                      int direction);
    bool             hasEnhancedUnitAnimation(int itemID, int house,
                                              EnhancedUnitState state,
                                              int direction);
    std::vector<EnhancedUnitEditorInfo> getEnhancedUnitEditorInfo();
    EnhancedRenderMode getEnhancedUnitRenderMode(int itemID, int house,
                                                 EnhancedUnitState state,
                                                 int direction);
    bool             setEnhancedUnitRenderMode(int itemID, int house,
                                               EnhancedUnitState state,
                                               int direction,
                                               EnhancedRenderMode mode);
    void             reloadEnhancedUnitMounts();
    bool             hasObjPic(unsigned int id, int house=HOUSE_HARKONNEN, unsigned int z=0) const;
    void             resetDuneCityGraphicsSkins();
    void             setDuneCityHouseGraphicsSkin(int house, bool useDune2);
    bool             isDuneCityHouseUsingDune2(int house) const;

    // DuneCity 1.0.487: invalidate sprite texture cache
    // (objPicTex + objPic, NOT uiGraphic). Re-applied per
    // Tornie's OOB 'ajouter ces fonctions aussi'.
    void invalidateAllSpriteTextures();
    void reloadAllObjectGraphicsForActiveMod();
    void reloadModDependentUiGraphics();
    Animation* getMentatEyesAnimation(int house);
    Animation* getMentatMouthAnimation(int house);
    SDL_Texture* getMentatForeground(int house);

    SDL_Texture*     getSmallDetailPic(unsigned int id);
    SDL_Texture*     getSmallDetailPic(unsigned int id, int house);
    SDL_Texture*     getTinyPicture(unsigned int id);
    SDL_Texture*     getUIGraphic(unsigned int id, int house=HOUSE_HARKONNEN);
    SDL_Texture*     getMapChoicePiece(unsigned int num, int house);

    SDL_Surface*     getUIGraphicSurface(unsigned int id, int house=HOUSE_HARKONNEN);
    SDL_Surface*     getMapChoicePieceSurface(unsigned int num, int house);

    SDL_Surface*     getBackgroundSurface() { return pBackgroundSurface.get(); };

    Animation*       getAnimation(unsigned int id);

private:
    std::unique_ptr<Animation>  loadAnimationFromWsa(const std::string& filename) const;
    sdl2::surface_ptr           generateWindtrapAnimationFrames(SDL_Surface* windtrapPic) const;
    sdl2::surface_ptr           generateMapChoiceArrowFrames(SDL_Surface* arrowPic, int house=HOUSE_HARKONNEN) const;

    std::unique_ptr<Shpfile>  loadShpfile(const std::string& filename) const;
    std::unique_ptr<Wsafile>  loadWsafile(const std::string& filename) const;

    sdl2::texture_ptr   extractSmallDetailPic(const std::string& filename) const;


    sdl2::surface_ptr   generateDoubledObjPic(unsigned int id, int h) const;
    sdl2::surface_ptr   generateTripledObjPic(unsigned int id, int h) const;
    void                loadCompactObjPicOverrides();
    void                loadDuneCitySkinOverrides();
    bool                loadHDObjPicOverride(unsigned int id);
    void                loadEnhancedUnitManifests();
    void                loadEnhancedWorldManifests();
    void                loadDuneCityZoneManifests();
    void                loadDuneCityBuildingManifests();
    void                invalidateEnhancedUnitMountsIfChanged(bool force = false);
    void                loadEnhancedRenderModes();
    void                loadDune2RVisualPreference();
    void                loadMentatGraphics();
    void                loadCustomHouseHerald();
    void                reloadModDependentObjectGraphics();
    void                reloadRuntimeModPortraits();
    void                rebuildModDependentEditorGraphics();

    struct HDObjPicOverride {
        std::array<sdl2::texture_ptr, NUM_HOUSES> texture;
        int columns = 1;
        int rows = 1;
        int anchorX = -1;
        int anchorY = -1;
        int baseWidth = 0;
        int baseHeight = 0;
        double scale = 1.0;
        bool attempted = false;
        bool loaded = false;
    };

    struct EnhancedUnitAnimation {
        EnhancedUnitAnimation() = default;
        EnhancedUnitAnimation(const EnhancedUnitAnimation&) = delete;
        EnhancedUnitAnimation& operator=(const EnhancedUnitAnimation&) = delete;
        EnhancedUnitAnimation(EnhancedUnitAnimation&&) noexcept = default;
        EnhancedUnitAnimation& operator=(EnhancedUnitAnimation&&) noexcept = default;

        std::string atlasPath;
        sdl2::texture_ptr texture;
        int columns = 1;
        int rows = 1;
        int frameCount = 1;
        int frameMs = 100;
        std::vector<Uint32> durationsMs;
        int anchorX = -1;
        int anchorY = -1;
        bool loop = true;
        bool loadAttempted = false;
    };

    struct EnhancedUnitDefinition {
        EnhancedUnitDefinition() = default;
        EnhancedUnitDefinition(const EnhancedUnitDefinition&) = delete;
        EnhancedUnitDefinition& operator=(const EnhancedUnitDefinition&) = delete;
        EnhancedUnitDefinition(EnhancedUnitDefinition&&) noexcept = default;
        EnhancedUnitDefinition& operator=(EnhancedUnitDefinition&&) noexcept = default;

        int itemID = -1;
        int houseID = -1;
        std::string sourceUnit;
        int baseWidth = 0;
        int baseHeight = 0;
        double scale = 1.0;
        std::map<int, EnhancedUnitAnimation> animations;
    };

    struct EnhancedAtlasChunk {
        EnhancedAtlasChunk() = default;
        EnhancedAtlasChunk(const EnhancedAtlasChunk&) = delete;
        EnhancedAtlasChunk& operator=(const EnhancedAtlasChunk&) = delete;
        EnhancedAtlasChunk(EnhancedAtlasChunk&&) noexcept = default;
        EnhancedAtlasChunk& operator=(EnhancedAtlasChunk&&) noexcept = default;

        std::string atlasPath;
        int firstFrame = 0;
        int frameCount = 0;
        int columns = 1;
        int rows = 1;
    };

    struct EnhancedBuildingAnimation {
        EnhancedBuildingAnimation() = default;
        EnhancedBuildingAnimation(const EnhancedBuildingAnimation&) = delete;
        EnhancedBuildingAnimation& operator=(const EnhancedBuildingAnimation&) = delete;
        EnhancedBuildingAnimation(EnhancedBuildingAnimation&&) noexcept = default;
        EnhancedBuildingAnimation& operator=(EnhancedBuildingAnimation&&) noexcept = default;

        std::vector<EnhancedAtlasChunk> chunks;
        std::string stillPath;
        sdl2::texture_ptr stillTexture;
        int stillWidth = 0;
        int stillHeight = 0;
        int stillAnchorX = 0;
        int stillAnchorY = 0;
        bool stillAttempted = false;
        int frameCount = 1;
        int frameMs = 100;
        int frameWidth = 1;
        int frameHeight = 1;
        int anchorX = 0;
        int anchorY = 0;
        bool loop = true;
    };

    struct EnhancedBuildingDefinition {
        EnhancedBuildingDefinition() = default;
        EnhancedBuildingDefinition(const EnhancedBuildingDefinition&) = delete;
        EnhancedBuildingDefinition& operator=(const EnhancedBuildingDefinition&) = delete;
        EnhancedBuildingDefinition(EnhancedBuildingDefinition&&) noexcept = default;
        EnhancedBuildingDefinition& operator=(EnhancedBuildingDefinition&&) noexcept = default;

        int itemID = -1;
        int houseID = -1;
        int footprintWidth = 1;
        int footprintHeight = 1;
        std::string sourceUnit;
        std::map<int, EnhancedBuildingAnimation> animations;
    };

    struct DuneCityZoneDefinition {
        DuneCityZoneDefinition() = default;
        DuneCityZoneDefinition(const DuneCityZoneDefinition&) = delete;
        DuneCityZoneDefinition& operator=(const DuneCityZoneDefinition&) = delete;
        DuneCityZoneDefinition(DuneCityZoneDefinition&&) noexcept = default;
        DuneCityZoneDefinition& operator=(DuneCityZoneDefinition&&) noexcept = default;

        int itemID = -1;
        int houseID = -1;
        int footprintWidth = 2;
        int footprintHeight = 2;
        std::string sourceUnit;
        std::map<int, EnhancedBuildingAnimation> animations;
    };

    struct DuneCityBuildingFrame {
        std::string imagePath;
        int width = 0;
        int height = 0;
    };

    struct DuneCityBuildingDefinition {
        int itemID = -1;
        int houseID = -1;
        std::string sourceUnit;
        std::vector<DuneCityBuildingFrame> frames;
    };

    struct EnhancedTerrainVariant {
        EnhancedTerrainVariant() = default;
        EnhancedTerrainVariant(const EnhancedTerrainVariant&) = delete;
        EnhancedTerrainVariant& operator=(const EnhancedTerrainVariant&) = delete;
        EnhancedTerrainVariant(EnhancedTerrainVariant&&) noexcept = default;
        EnhancedTerrainVariant& operator=(EnhancedTerrainVariant&&) noexcept = default;

        std::string imagePath;
        sdl2::texture_ptr texture;
        bool loadAttempted = false;
    };

    struct EnhancedTerrainDefinition {
        EnhancedTerrainDefinition() = default;
        EnhancedTerrainDefinition(const EnhancedTerrainDefinition&) = delete;
        EnhancedTerrainDefinition& operator=(const EnhancedTerrainDefinition&) = delete;
        EnhancedTerrainDefinition(EnhancedTerrainDefinition&&) noexcept = default;
        EnhancedTerrainDefinition& operator=(EnhancedTerrainDefinition&&) noexcept = default;

        int terrainType = -1;
        std::string sourceUnit;
        std::array<EnhancedTerrainVariant, 16> variants;
    };

    // 8-bit surfaces kept in main memory for processing as needed, e.g. color remapping
    std::array<std::array<std::array<sdl2::surface_ptr, NUM_ZOOMLEVEL>, NUM_HOUSE_COLOR_SLOTS>, NUM_OBJPICS> objPic;
    std::array<std::array<std::array<sdl2::surface_ptr, NUM_ZOOMLEVEL>, NUM_HOUSES>, 3> duneCitySimCityZonePic;
    std::array<std::array<std::array<sdl2::surface_ptr, NUM_ZOOMLEVEL>, NUM_HOUSES>, 3> duneCityDune2ZonePic;
    std::array<std::array<std::array<sdl2::surface_ptr, NUM_ZOOMLEVEL>, NUM_HOUSES>, 6> duneCitySimCityBuildingPic;
    std::array<std::array<std::array<sdl2::surface_ptr, NUM_ZOOMLEVEL>, NUM_HOUSES>, 6> duneCityDune2BuildingPic;
    std::array<bool, NUM_HOUSES> duneCityHouseUsesDune2{};
    std::array<std::array<sdl2::surface_ptr, NUM_ZOOMLEVEL>, NUM_HOUSE_COLOR_SLOTS> scoutpostBaseGraphics{};
    std::array<std::array<sdl2::surface_ptr, NUM_ZOOMLEVEL>, NUM_HOUSE_COLOR_SLOTS> chaosFactoryBaseGraphics{};
    std::array<std::array<sdl2::surface_ptr, NUM_HOUSE_COLOR_SLOTS>, NUM_UIGRAPHICS> uiGraphic;
    std::array<std::array<sdl2::surface_ptr, NUM_HOUSE_COLOR_SLOTS>, NUM_MAPCHOICEPIECES> mapChoicePieces;
    std::array<std::unique_ptr<Animation>, NUM_ANIMATION> animation{};
    std::array<sdl2::surface_ptr, NUM_HOUSE_COLOR_SLOTS> modMentatForeground{};
    std::array<std::unique_ptr<Animation>, NUM_HOUSE_COLOR_SLOTS> modMentatEyes{};
    std::array<std::unique_ptr<Animation>, NUM_HOUSE_COLOR_SLOTS> modMentatMouth{};

    // 32-bit surfaces
    sdl2::surface_ptr    pBackgroundSurface;

    // Textures
    std::array<std::array<std::array<sdl2::texture_ptr, NUM_ZOOMLEVEL>, NUM_HOUSE_COLOR_SLOTS>, NUM_OBJPICS> objPicTex;
    std::array<HDObjPicOverride, NUM_OBJPICS> hdObjPicOverrides;
    std::vector<EnhancedUnitDefinition> enhancedUnitDefinitions;
    Dune2RInfantryOverlay enhancedInfantryOverlay;
    std::vector<EnhancedBuildingDefinition> enhancedBuildingDefinitions;
    std::vector<DuneCityZoneDefinition> duneCityZoneDefinitions;
    std::vector<DuneCityBuildingDefinition> duneCityBuildingDefinitions;
    std::unique_ptr<EnhancedAtlasCache> enhancedBuildingAtlasCache;
    std::vector<EnhancedTerrainDefinition> enhancedTerrainDefinitions;
    bool enhancedUnitManifestsLoaded = false;
    bool enhancedWorldManifestsLoaded = false;
    bool duneCityZoneManifestsLoaded = false;
    bool duneCityBuildingManifestsLoaded = false;
    std::string enhancedUnitMountRevision;
    Uint32 enhancedUnitMountLastCheck = 0;
    std::map<int, EnhancedRenderMode> enhancedUnitRenderModes;
    bool enhancedRenderModesLoaded = false;
    bool dune2rVisualPreferenceLoaded = false;
    bool dune2rVisualTargetEnabled = true;
    bool dune2rVisualTransitionActive = false;
    Uint8 dune2rVisualTransitionStartBlend = SDL_ALPHA_OPAQUE;
    Uint8 dune2rVisualBlend = SDL_ALPHA_OPAQUE;
    Uint32 dune2rVisualTransitionStartTicks = 0;
    std::array<sdl2::texture_ptr, NUM_SMALLDETAILPICS> smallDetailPicTex;
    std::array<std::array<sdl2::texture_ptr, NUM_HOUSE_COLOR_SLOTS>, NUM_SMALLDETAILPICS> houseSmallDetailPicTex;
    std::array<std::array<sdl2::texture_ptr, NUM_HOUSES>, NUM_SMALLDETAILPICS> duneCityDune2DetailPicTex;
    std::array<sdl2::texture_ptr, NUM_TINYPICTURE> tinyPictureTex;
    std::array<std::array<sdl2::texture_ptr, NUM_HOUSE_COLOR_SLOTS>, NUM_UIGRAPHICS> uiGraphicTex;
    std::array<std::array<sdl2::texture_ptr, NUM_HOUSE_COLOR_SLOTS>, NUM_MAPCHOICEPIECES> mapChoicePiecesTex;
    std::array<sdl2::texture_ptr, NUM_HOUSE_COLOR_SLOTS> modMentatForegroundTex{};
};

#endif // GFXMANAGER_H
