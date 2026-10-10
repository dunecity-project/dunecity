#include <mod/Workshop.h>
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

#include <FileClasses/GFXManager.h>
#include <dunecity/CitySpritePolicy.h>
#include <FileClasses/EnhancedAtlasCache.h>

#include <globals.h>
#include <GUI/dune/DuneStyle.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/INIFile.h>
#include <mod/ModManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/PictureFactory.h>
#include <FileClasses/LoadSavePNG.h>
#include <FileClasses/Shpfile.h>
#include <FileClasses/Cpsfile.h>
#include <FileClasses/Icnfile.h>
#include <FileClasses/Wsafile.h>
#include <FileClasses/Palfile.h>

#include <Colors.h>

#include <misc/FileSystem.h>
#include <main.h>

#include <misc/draw_util.h>
#include <misc/EnhancedBuildingGeometry.h>
#include <misc/EnhancedAnimationTimeline.h>
#include <misc/EnhancedUnitGeometry.h>
#include <misc/Dune2RPresentation.h>
#include <misc/Scaler.h>
#include <misc/exceptions.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {

bool usesSharedCityAtlas(unsigned int id) {
    return id == ObjPic_ZoneResidential || id == ObjPic_ZoneCommercial
        || id == ObjPic_ZoneIndustrial || id == ObjPic_CityRoad
        || id == ObjPic_Stadium || id == ObjPic_Airport || id == ObjPic_NuclearPlant
        || id == ObjPic_CityHelicopter || id == ObjPic_CityAirplane;
}

constexpr int kEnhancedDirectionCount = 8;

const std::array<const char*, kEnhancedDirectionCount> kEnhancedDirectionNames = {
    "east", "north_east", "north", "north_west",
    "west", "south_west", "south", "south_east"
};

const std::array<const char*, static_cast<size_t>(GFXManager::EnhancedUnitState::Count)> kEnhancedStateNames = {
    "Idle", "Movement", "Combat", "DamageSmoking", "DamageDamaged",
    "DamageExploded", "DamageAftermath", "DamageDissipation", "CombatReturn"
};

const std::array<const char*, static_cast<size_t>(GFXManager::EnhancedBuildingState::Count)>
kEnhancedBuildingStateNames = {
    "Placement", "Construction", "Idle", "Working", "Damaged", "Repair", "Destroyed"
};

const std::array<const char*, static_cast<size_t>(GFXManager::DuneCityZoneActivity::Count)>
kDuneCityZoneActivityNames = {"Idle", "Active", "Growing", "Damaged", "Repair"};

int duneCityZoneAnimationKey(int density, int valueTier,
                             GFXManager::DuneCityZoneActivity activity) {
    return ((std::clamp(valueTier, 0, 3) * 4 + std::clamp(density, 0, 3))
            * static_cast<int>(GFXManager::DuneCityZoneActivity::Count))
           + static_cast<int>(activity);
}

constexpr Uint32 kDune2RVisualFadeMs = 350;

int enhancedAnimationKey(GFXManager::EnhancedUnitState state, int direction) {
    return static_cast<int>(state) * kEnhancedDirectionCount + direction;
}

int enhancedRenderModeKey(int itemID, int house, GFXManager::EnhancedUnitState state,
                          int direction) {
    return ((((itemID * (static_cast<int>(NUM_HOUSES) + 1)) + (house + 1))
             * static_cast<int>(kEnhancedStateNames.size()))
            + static_cast<int>(state))
           * kEnhancedDirectionCount + direction;
}

std::string enhancedRenderModeConfigKey(int itemID, int house,
                                        GFXManager::EnhancedUnitState state,
                                        int direction) {
    return "Unit" + std::to_string(itemID)
           + "House" + std::to_string(house)
           + kEnhancedStateNames[static_cast<int>(state)]
           + kEnhancedDirectionNames[direction];
}

const char* enhancedRenderModeName(GFXManager::EnhancedRenderMode mode) {
    switch(mode) {
        case GFXManager::EnhancedRenderMode::Layered:       return "layered";
        case GFXManager::EnhancedRenderMode::Random:        return "random";
        case GFXManager::EnhancedRenderMode::FullAnimation: return "full";
    }
    return "full";
}

GFXManager::EnhancedRenderMode parseEnhancedRenderMode(const std::string& value) {
    if(value == "layered") {
        return GFXManager::EnhancedRenderMode::Layered;
    }
    if(value == "random") {
        return GFXManager::EnhancedRenderMode::Random;
    }
    return GFXManager::EnhancedRenderMode::FullAnimation;
}

bool isPathInside(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto relative = std::filesystem::relative(child, parent);
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

/**
    Number of columns and rows each obj pic has
*/
static const Coord objPicTiles[] {
    { 8, 1 },   // ObjPic_Tank_Base
    { 8, 1 },   // ObjPic_Tank_Gun
    { 8, 1 },   // ObjPic_Siegetank_Base
    { 8, 1 },   // ObjPic_Siegetank_Gun
    { 8, 1 },   // ObjPic_Devastator_Base
    { 8, 1 },   // ObjPic_Devastator_Gun
    { 8, 1 },   // ObjPic_Sonictank_Gun
    { 8, 1 },   // ObjPic_Launcher_Gun
    { 8, 1 },   // ObjPic_DeviatorGunTornie
    { 8, 1 },   // ObjPic_RocketTrike (Tornie — derived from RocketTrike.png sprite sheet)
    { 8, 1 },   // ObjPic_FlameTank (Tornie — derived from FlameTank.png sprite sheet)
    { 8, 1 },   // ObjPic_EliteSiegeTankCustom (Tornie — derived from EliteSiegeTank.png sprite sheet)
    { 8, 1 },   // ObjPic_ChemicalSiegeTankGunTornie
    { 8, 1 },   // ObjPic_Quad
    { 8, 1 },   // ObjPic_Trike
    { 8, 1 },   // ObjPic_Harvester
    { 8, 3 },   // ObjPic_Harvester_Sand
    { 8, 1 },   // ObjPic_MCV
    { 8, 2 },   // ObjPic_Carryall
    { 8, 2 },   // ObjPic_CarryallShadow
    { 8, 1 },   // ObjPic_Frigate
    { 8, 1 },   // ObjPic_FrigateShadow
    { 8, 3 },   // ObjPic_Ornithopter
    { 8, 3 },   // ObjPic_OrnithopterShadow
    { 4, 3 },   // ObjPic_Trooper
    { 4, 3 },   // ObjPic_Troopers
    { 4, 3 },   // ObjPic_Soldier
    { 4, 3 },   // ObjPic_Infantry
    { 4, 3 },   // ObjPic_Saboteur
    { 1, 9 },   // ObjPic_Sandworm
    { 4, 1 },   // ObjPic_ConstructionYard
    { 4, 1 },   // ObjPic_Windtrap
    { 10, 7 },  // ObjPic_AdvancedWindTrap (2 build frames + 66 animated frames)
    { 10, 7 },  // ObjPic_AdvancedWindTrap2x3 (2 build frames + 66 animated frames)
    { 10, 7 },  // ObjPic_AdvancedWindTrap3x2 (2 build frames + 66 animated frames)
    { 10, 1 },  // ObjPic_Refinery
    { 4, 1 },   // ObjPic_Barracks
    { 4, 1 },   // ObjPic_WOR
    { 4, 1 },   // ObjPic_Radar
    { 6, 1 },   // ObjPic_LightFactory
    { 4, 1 },   // ObjPic_Silo
    { 8, 1 },   // ObjPic_HeavyFactory
    { 8, 1 },   // ObjPic_HighTechFactory
    { 4, 1 },   // ObjPic_IX
    { 4, 1 },   // ObjPic_Palace
    { 10, 1 },  // ObjPic_RepairYard
    { 10, 1 },  // ObjPic_Starport
    { 10, 1 },  // ObjPic_GunTurret
    { 10, 1 },  // ObjPic_RocketTurret
    { 25, 3 },  // ObjPic_Wall
    { 16, 1 },  // ObjPic_Bullet_SmallRocket
    { 16, 1 },  // ObjPic_Bullet_MediumRocket
    { 16, 1 },  // ObjPic_Bullet_LargeRocket
    { 1, 1 },   // ObjPic_Bullet_Small
    { 1, 1 },   // ObjPic_Bullet_Medium
    { 1, 1 },   // ObjPic_Bullet_Large
    { 1, 1 },   // ObjPic_Bullet_Sonic
    { 1, 1 },   // ObjPic_Bullet_SonicTemp
    { 5, 1 },   // ObjPic_Hit_Gas
    { 1, 1 },   // ObjPic_Hit_ShellSmall
    { 1, 1 },   // ObjPic_Hit_ShellMedium
    { 1, 1 },   // ObjPic_Hit_ShellLarge
    { 5, 1 },   // ObjPic_ExplosionSmall
    { 5, 1 },   // ObjPic_ExplosionMedium1
    { 5, 1 },   // ObjPic_ExplosionMedium2
    { 5, 1 },   // ObjPic_ExplosionLarge1
    { 5, 1 },   // ObjPic_ExplosionLarge2
    { 2, 1 },   // ObjPic_ExplosionSmallUnit
    { 21, 1 },  // ObjPic_ExplosionFlames
    { 3, 1 },   // ObjPic_ExplosionSpiceBloom
    { 6, 1 },   // ObjPic_DeadInfantry
    { 6, 1 },   // ObjPic_DeadAirUnit
    { 3, 1 },   // ObjPic_Smoke
    { 1, 1 },   // ObjPic_SandwormShimmerMask
    { 1, 1 },   // ObjPic_SandwormShimmerTemp
    { NUM_TERRAIN_TILES_X, NUM_TERRAIN_TILES_Y },  // ObjPic_Terrain
    { NUM_TERRAIN_TILES_X, NUM_TERRAIN_TILES_Y },  // ObjPic_Terrain_GreenSpice
    { NUM_TERRAIN_TILES_X, NUM_TERRAIN_TILES_Y },  // ObjPic_Terrain_RedSpice
    { 14, 1 },  // ObjPic_DestroyedStructure
    { 6, 1 },   // ObjPic_RockDamage
    { 3, 1 },   // ObjPic_SandDamage
    { 16, 1 },  // ObjPic_Terrain_Hidden
    { 16, 1 },  // ObjPic_Terrain_HiddenFog
    { 8, 1 },   // ObjPic_Terrain_Tracks
    { 1, 1 },   // ObjPic_Star
    { 8, 1 },   // ObjPic_RebelHarvester
    { 10, 1 },  // ObjPic_Worfinery (vanilla Refinery animation layout)
    { 4, 1 },   // ObjPic_TechCenter
    { 4, 1 },   // ObjPic_Scoutpost
    { 10, 1 },  // ObjPic_LoveFactory
    { DuneCity::CitySprites::residentialColumns, DuneCity::CitySprites::residentialRows }, // ObjPic_ZoneResidential
    { DuneCity::CitySprites::commercialColumns, 4 }, // ObjPic_ZoneCommercial
    { DuneCity::CitySprites::industrialColumns, DuneCity::CitySprites::industrialRows }, // ObjPic_ZoneIndustrial
    { 16, DuneCity::CitySprites::roadRows }, // ObjPic_CityRoad
    { DuneCity::CitySprites::specialFrames, 1 }, // ObjPic_NuclearPlant
    { 4, 1 },   // ObjPic_PoliceStation (4 frame slots, all identical; 2x2 footprint)
    { DuneCity::CitySprites::specialFrames, 1 }, // ObjPic_Stadium
    { DuneCity::CitySprites::specialFrames, 1 }, // ObjPic_Airport
    { 1, 1 },   // ObjPic_Hospital (single cell, 2x2 footprint, auto-placed on residential)
    { 1, 1 },   // ObjPic_Church   (single cell, 2x2 footprint, auto-placed on residential)
    { 8, 1 },   // ObjPic_SonicTrike
    { 8, 1 },   // ObjPic_EliteLauncherGunTornie
    { 8, 1 },   // ObjPic_RebelSonicTankGun
    { 8, 1 },   // ObjPic_HarvestankGunTornie
    { 8, 2 },   // ObjPic_ChemicalCarryall
    { 4, 1 },   // ObjPic_Flamepost
    { 4, 1 },   // ObjPic_Chemipost
    { 4, 1 },   // ObjPic_ChaosFactory
    { 8, 1 },   // ObjPic_CityHelicopter (one 32x32 original frame per heading)
    { 8, 4 },   // ObjPic_CityAirplane (48x48: cruise headings + three take-off rows)
};
static_assert(sizeof(objPicTiles) / sizeof(objPicTiles[0]) == NUM_OBJPICS,
              "objPicTiles must have one entry per ObjPic enum value");

static void applyRebelsTint(SDL_Surface* surface, int colorSlot);
static bool usesPrivateVisualColorRamp(int colorSlot) {
    return isDuneCityHouseColorSlot(colorSlot) || isTornieRebelsColorSlot(colorSlot) || isVanillaRebelsColorSlot(colorSlot)
        || colorSlot == HOUSE_CUSTOM || isCustomHouseColorSlot(colorSlot)
        || isTornieGuestHouseColorSlot(colorSlot);
}
static int getVisualRemapPaletteIndex(int colorSlot) {
    return usesPrivateVisualColorRamp(colorSlot)
        ? PALCOLOR_HARKONNEN
        : getHouseColorPaletteIndexFromSlot(colorSlot);
}
static void applyCustomVisualColorRamp(SDL_Surface* surface, int colorSlot);
static sdl2::surface_ptr remapTruecolorHouseColorRange(SDL_Surface* source, int colorSlot, int shadeCount = 8);
static void preserveOpaqueBlackIndex(SDL_Surface* surface);
static void normalizeTransparentPaletteIndexes(SDL_Surface* surface);
static sdl2::surface_ptr convertTornieIndexedSurfaceToRGBA(SDL_Surface* source, const char* label, int house, unsigned int zoom, bool useTextureMask = false);
static void logTornieStructureSurfaceDiagnostics(const char* stage, const char* label, SDL_Surface* surface, int frameWidth, int frameHeight);
static bool isTornieStructureObjPic(unsigned int id);
static const char* getTornieStructureObjPicName(unsigned int id);
static sdl2::surface_ptr remapIndexedSurfaceToPalette(SDL_Surface* source, const SDL_Palette* targetPalette);
static sdl2::surface_ptr convertTruecolorSurfaceToPalette(SDL_Surface* source,
                                                         const SDL_Palette* targetPalette,
                                                         int reservedIndex = -1,
                                                         SDL_Color reservedColor = SDL_Color{});
static sdl2::surface_ptr generateTornieWindtrapAnimationFrames(SDL_Surface* windtrapPic);
static void normalizeHouseColorRangesToHarkonnen(SDL_Surface* surface);
static void normalizeHarkonnenTeamRed(SDL_Surface* surface);
static void normalizeLooseTeamPaintToHarkonnen(SDL_Surface* surface);
static void normalizeTornieStructureTeamPaintToHarkonnen(SDL_Surface* surface, unsigned int objPicID);
static sdl2::surface_ptr createTintedTerrainSpiceSurface(SDL_Surface* source, SDL_Color thinTint, SDL_Color thickTint);
static sdl2::surface_ptr createTintedMapEditorIcon(SDL_Surface* source, SDL_Surface* sand, SDL_Color tint);
static sdl2::surface_ptr createCustomMapEditorStar(SDL_Surface* source);
static sdl2::surface_ptr resizeSurfaceNearest(SDL_Surface* source, int width, int height);
static sdl2::surface_ptr scaleSurfaceNearest(SDL_Surface* source, int factor);
static sdl2::surface_ptr scaleDuneCitySkinEdgeAware(SDL_Surface* source, int factor,
                                                   int tilesX, int tilesY);
static std::unique_ptr<Animation> loadPngStripAnimation(const std::string& filename, int frameCount, double frameRate, bool bDoublePic = true, int transparentColorKey = -1);


GFXManager::GFXManager() {

    // open all shp files
    std::unique_ptr<Shpfile> units = loadShpfile("UNITS.SHP");
    std::unique_ptr<Shpfile> units1 = loadShpfile("UNITS1.SHP");
    std::unique_ptr<Shpfile> units2 = loadShpfile("UNITS2.SHP");
    std::unique_ptr<Shpfile> mouse = loadShpfile("MOUSE.SHP");
    std::unique_ptr<Shpfile> shapes = loadShpfile("SHAPES.SHP");
    std::unique_ptr<Shpfile> menshpa = loadShpfile("MENSHPA.SHP");
    std::unique_ptr<Shpfile> menshph = loadShpfile("MENSHPH.SHP");
    std::unique_ptr<Shpfile> menshpo = loadShpfile("MENSHPO.SHP");
    std::unique_ptr<Shpfile> menshpm = loadShpfile("MENSHPM.SHP");

    std::unique_ptr<Shpfile> choam;
    if(pFileManager->exists("CHOAM." + _("LanguageFileExtension"))) {
        choam = loadShpfile("CHOAM." + _("LanguageFileExtension"));
    } else if(pFileManager->exists("CHOAMSHP.SHP")) {
        choam = loadShpfile("CHOAMSHP.SHP");
    } else {
        THROW(std::runtime_error, "GFXManager::GFXManager(): Cannot open CHOAMSHP.SHP or CHOAM."+_("LanguageFileExtension")+"!");
    }

    std::unique_ptr<Shpfile> bttn;
    if(pFileManager->exists("BTTN." + _("LanguageFileExtension"))) {
        bttn = loadShpfile("BTTN." + _("LanguageFileExtension"));
    } else {
        // The US-Version has the buttons in SHAPES.SHP
        // => bttn == nullptr
    }

    std::unique_ptr<Shpfile> mentat;
    if(pFileManager->exists("MENTAT." + _("LanguageFileExtension"))) {
        mentat = loadShpfile("MENTAT." + _("LanguageFileExtension"));
    } else {
        mentat = loadShpfile("MENTAT.SHP");
    }

    std::unique_ptr<Shpfile> pieces = loadShpfile("PIECES.SHP");
    std::unique_ptr<Shpfile> arrows = loadShpfile("ARROWS.SHP");

    // Load icon file
    std::unique_ptr<Icnfile> icon = std::make_unique<Icnfile>(  pFileManager->openFile("ICON.ICN").get(),
                                                                pFileManager->openFile("ICON.MAP").get());

    // Load radar static
    std::unique_ptr<Wsafile> radar = loadWsafile("STATIC.WSA");

    // open bene palette
    Palette benePalette = LoadPalette_RW(pFileManager->openFile("BENE.PAL").get());

    //create PictureFactory
    std::unique_ptr<PictureFactory> PicFactory = std::make_unique<PictureFactory>();



    // load object pics in the original resolution
    objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(0));
    objPic[ObjPic_Tank_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(5));
    objPic[ObjPic_Siegetank_Base][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(10));
    objPic[ObjPic_Siegetank_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(15));
    objPic[ObjPic_Devastator_Base][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(20));
    objPic[ObjPic_Devastator_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(25));
    objPic[ObjPic_Sonictank_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(30));
    objPic[ObjPic_Launcher_Gun][HOUSE_HARKONNEN][0] = units2->getPictureArray(8,1,GROUNDUNIT_ROW(35));
    objPic[ObjPic_Quad][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(0));
    objPic[ObjPic_Trike][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(5));
    objPic[ObjPic_Harvester][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(10));
    objPic[ObjPic_Harvester_Sand][HOUSE_HARKONNEN][0] = units1->getPictureArray(8,3,HARVESTERSAND_ROW(72),HARVESTERSAND_ROW(73),HARVESTERSAND_ROW(74));
    objPic[ObjPic_MCV][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(15));
    objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0] = units->getPictureArray(8,2,AIRUNIT_ROW(45),AIRUNIT_ROW(48));
    objPic[ObjPic_CarryallShadow][HOUSE_HARKONNEN][0] = nullptr;    // create shadow after scaling
    objPic[ObjPic_Frigate][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,AIRUNIT_ROW(60));
    objPic[ObjPic_FrigateShadow][HOUSE_HARKONNEN][0] = nullptr;     // create shadow after scaling
    objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][0] = units->getPictureArray(8,3,ORNITHOPTER_ROW(51),ORNITHOPTER_ROW(52),ORNITHOPTER_ROW(53));
    objPic[ObjPic_OrnithopterShadow][HOUSE_HARKONNEN][0] = nullptr; // create shadow after scaling
    objPic[ObjPic_Trooper][HOUSE_HARKONNEN][0] = units->getPictureArray(4,3,INFANTRY_ROW(82),INFANTRY_ROW(83),INFANTRY_ROW(84));
    objPic[ObjPic_Troopers][HOUSE_HARKONNEN][0] = units->getPictureArray(4,4,MULTIINFANTRY_ROW(103),MULTIINFANTRY_ROW(104),MULTIINFANTRY_ROW(105),MULTIINFANTRY_ROW(106));
    objPic[ObjPic_Soldier][HOUSE_HARKONNEN][0] = units->getPictureArray(4,3,INFANTRY_ROW(73),INFANTRY_ROW(74),INFANTRY_ROW(75));
    objPic[ObjPic_Infantry][HOUSE_HARKONNEN][0] = units->getPictureArray(4,4,MULTIINFANTRY_ROW(91),MULTIINFANTRY_ROW(92),MULTIINFANTRY_ROW(93),MULTIINFANTRY_ROW(94));
    objPic[ObjPic_Saboteur][HOUSE_HARKONNEN][0] = units->getPictureArray(4,3,INFANTRY_ROW(63),INFANTRY_ROW(64),INFANTRY_ROW(65));
    objPic[ObjPic_Sandworm][HOUSE_HARKONNEN][0] = units1->getPictureArray(1,9,71|TILE_NORMAL,70|TILE_NORMAL,69|TILE_NORMAL,68|TILE_NORMAL,67|TILE_NORMAL,68|TILE_NORMAL,69|TILE_NORMAL,70|TILE_NORMAL,71|TILE_NORMAL);
    objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][0] = icon->getPictureArray(17);
    objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0] = icon->getPictureArray(19);
    objPic[ObjPic_Refinery][HOUSE_HARKONNEN][0] = icon->getPictureArray(21);
    objPic[ObjPic_Barracks][HOUSE_HARKONNEN][0] = icon->getPictureArray(18);
    objPic[ObjPic_WOR][HOUSE_HARKONNEN][0] = icon->getPictureArray(16);
    objPic[ObjPic_Radar][HOUSE_HARKONNEN][0] = icon->getPictureArray(26);
    objPic[ObjPic_LightFactory][HOUSE_HARKONNEN][0] = icon->getPictureArray(12);
    objPic[ObjPic_Silo][HOUSE_HARKONNEN][0] = icon->getPictureArray(25);
    objPic[ObjPic_HeavyFactory][HOUSE_HARKONNEN][0] = icon->getPictureArray(13);
    objPic[ObjPic_HighTechFactory][HOUSE_HARKONNEN][0] = icon->getPictureArray(14);
    objPic[ObjPic_IX][HOUSE_HARKONNEN][0] = icon->getPictureArray(15);
    objPic[ObjPic_Palace][HOUSE_HARKONNEN][0] = icon->getPictureArray(11);
    objPic[ObjPic_RepairYard][HOUSE_HARKONNEN][0] = icon->getPictureArray(22);
    objPic[ObjPic_Starport][HOUSE_HARKONNEN][0] = icon->getPictureArray(20);
    objPic[ObjPic_GunTurret][HOUSE_HARKONNEN][0] = icon->getPictureArray(23);
    objPic[ObjPic_RocketTurret][HOUSE_HARKONNEN][0] = icon->getPictureArray(24);
    objPic[ObjPic_Wall][HOUSE_HARKONNEN][0] = icon->getPictureArray(6,25,3,1);

    // Prebuilt Micropolis atlases include every model and animation phase.
    // Only texture/source-rectangle selection happens during gameplay.
    {
        // Scale a 32-bit surface by an integer factor using SDL_BlitScaled
        // (nearest-neighbour).  This avoids the legacy 8-bit Scaler path.
        auto scaleRGBASurface = [](SDL_Surface* src, int factor) -> sdl2::surface_ptr {
            sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                src->w * factor, src->h * factor,
                src->format->BitsPerPixel,
                src->format->Rmask, src->format->Gmask,
                src->format->Bmask, src->format->Amask) };
            if (dst) {
                SDL_BlitScaled(src, nullptr, dst.get(), nullptr);
            }
            return dst;
        };

        // Find imported city art in installed and development locations.
        // Search order: installed data dir, then source-tree-relative
        // paths for dev builds (binary in build/bin/ or app bundle).
        char* sdlBasePath = SDL_GetBasePath();
        std::string binDir = sdlBasePath ? sdlBasePath : "./";
        if (sdlBasePath) SDL_free(sdlBasePath);

        std::vector<std::string> searchDirs = {
            getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_2x2/",
            binDir + "imported_sprites/micropolis/composites_2x2/",               // CMake-copied next to binary
            binDir + "../../imported_sprites/micropolis/composites_2x2/",          // build/bin -> root
            binDir + "../../../../../imported_sprites/micropolis/composites_2x2/",  // .app/Contents/MacOS -> root
        };
        // Dev-friendly fallback: DUNE_CITY_SOURCE_DIR env points at the
        // source tree so imported sprites are found without installing.
        const char* srcDirEnv = SDL_getenv("DUNE_CITY_SOURCE_DIR");
        if (srcDirEnv && srcDirEnv[0]) {
            std::string srcDir = srcDirEnv;
            if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
            searchDirs.push_back(srcDir + "imported_sprites/micropolis/composites_2x2/");
        }
        std::vector<std::string> atlasDirs;
        for (const auto& dir : searchDirs)
            atlasDirs.push_back(dir.substr(0, dir.size() - std::string("composites_2x2/").size()) + "atlases/");
        struct CityAtlasSpec { int id; const char* name; int cellSize; };
        const CityAtlasSpec cityAtlases[] = {
            {ObjPic_ZoneResidential, "residential", 2 * D2_TILESIZE},
            {ObjPic_ZoneCommercial, "commercial", 2 * D2_TILESIZE},
            {ObjPic_ZoneIndustrial, "industrial", 2 * D2_TILESIZE},
            {ObjPic_CityRoad, "roads", D2_TILESIZE},
            {ObjPic_Stadium, "stadium", 3 * D2_TILESIZE},
            {ObjPic_Airport, "airport", 3 * D2_TILESIZE},
            {ObjPic_NuclearPlant, "nuclear", 3 * D2_TILESIZE},
        };
        for (const auto& spec : cityAtlases) {
            sdl2::surface_ptr atlas;
            for (const auto& dir : atlasDirs) {
                auto rw = sdl2::RWops_ptr{SDL_RWFromFile((dir + spec.name + ".png").c_str(), "rb")};
                if (rw) atlas = LoadPNG_RW(rw.get());
                if (atlas) break;
            }
            // These small generated PNGs are tracked and bundled on every
            // platform. Fail clearly on incomplete/stale data, never sample
            // a different-sized atlas or silently hide missing buildings.
            if (!atlas || atlas->w != objPicTiles[spec.id].x * spec.cellSize
                       || atlas->h != objPicTiles[spec.id].y * spec.cellSize)
                THROW(std::runtime_error, "Missing or invalid city atlas %s.png; reinstall matching game data", spec.name);
            SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_NONE);
            objPic[spec.id][HOUSE_HARKONNEN][0] = std::move(atlas);
            for (int z = 1; z < NUM_ZOOMLEVEL; ++z) {
                objPic[spec.id][HOUSE_HARKONNEN][z] = scaleRGBASurface(objPic[spec.id][HOUSE_HARKONNEN][0].get(), z + 1);
                if (!objPic[spec.id][HOUSE_HARKONNEN][z])
                    THROW(std::runtime_error, "Unable to scale city atlas %s", spec.name);
            }
            // House-independent art shares these surfaces and cached textures.
            // Do not duplicate animation sheets for each of the 18 colour slots.
        }

        // ----- Original Micropolis city aircraft -----
        //
        // The unmodified sprite art from micropolis-activity/images/obj2-*.xpm
        // (helicopter) and obj3-*.xpm (airplane), re-packed by
        // scripts/import-micropolis-aircraft.py into ANGLETYPE column order.
        // Civilian aircraft are not house-coloured in the original, so these
        // share one surface set like the other city atlases.
        {
            std::vector<std::string> aircraftDirs;
            for (const auto& dir : searchDirs)
                aircraftDirs.push_back(dir.substr(0, dir.size() - std::string("composites_2x2/").size()) + "aircraft/");
            struct AircraftSpec { int id; const char* name; int cell; };
            const AircraftSpec cityAircraft[] = {
                {ObjPic_CityHelicopter, "city_helicopter", 2 * D2_TILESIZE},
                {ObjPic_CityAirplane,   "city_airplane",   3 * D2_TILESIZE},
            };
            for (const auto& spec : cityAircraft) {
                sdl2::surface_ptr sheet;
                for (const auto& dir : aircraftDirs) {
                    auto rw = sdl2::RWops_ptr{SDL_RWFromFile((dir + spec.name + ".png").c_str(), "rb")};
                    if (rw) sheet = LoadPNG_RW(rw.get());
                    if (sheet) break;
                }
                // Tracked, bundled art: fail loudly rather than sampling a
                // different-sized sheet or flying an invisible aircraft.
                if (!sheet || sheet->w != objPicTiles[spec.id].x * spec.cell
                           || sheet->h != objPicTiles[spec.id].y * spec.cell)
                    THROW(std::runtime_error, "Missing or invalid city aircraft sprite %s.png; reinstall matching game data", spec.name);
                // Copy, never blend, while scaling: the original art is
                // transparent-keyed and its alpha has to survive verbatim.
                // getZoomedObjPic() then turns on texture blending because
                // these surfaces are truecolor.
                SDL_SetSurfaceBlendMode(sheet.get(), SDL_BLENDMODE_NONE);
                objPic[spec.id][HOUSE_HARKONNEN][0] = std::move(sheet);
                for (int z = 1; z < NUM_ZOOMLEVEL; ++z) {
                    objPic[spec.id][HOUSE_HARKONNEN][z] = scaleRGBASurface(objPic[spec.id][HOUSE_HARKONNEN][0].get(), z + 1);
                    if (!objPic[spec.id][HOUSE_HARKONNEN][z])
                        THROW(std::runtime_error, "Unable to scale city aircraft sprite %s", spec.name);
                }
            }
        }

        // ----- DuneCity police-station sprite -----
        //
        // 2x2 footprint, no animation. We still
        // build a multi-frame atlas (4 horizontal copies) so StructureBase
        // animation indexing has somewhere to land — all frames are the
        // same image, so the sprite never appears to "animate".
        {
            std::vector<std::string> policeDirs = {
                getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_2x2/",
                binDir + "imported_sprites/micropolis/composites_2x2/",
                binDir + "../../imported_sprites/micropolis/composites_2x2/",
                binDir + "../../../../../imported_sprites/micropolis/composites_2x2/",
            };
            if (srcDirEnv && srcDirEnv[0]) {
                std::string srcDir = srcDirEnv;
                if (srcDir.back() != '/' && srcDir.back() != '\\') srcDir += '/';
                policeDirs.push_back(srcDir + "imported_sprites/micropolis/composites_2x2/");
            }

            const int frameW    = 2 * D2_TILESIZE;   // 32 px at zoom 0
            const int frameH    = 2 * D2_TILESIZE;
            const int numFrames = 4;
            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0,
                numFrames * frameW, frameH,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (atlas) {
                SDL_FillRect(atlas.get(), nullptr,
                             SDL_MapRGBA(atlas->format, 0, 0, 0, 0));
            }

            sdl2::surface_ptr policeSrc;
            for (const auto& dir : policeDirs) {
                std::string path = dir + "police_station_2x2.png";
                auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(path.c_str(), "rb") };
                if (rwops) {
                    policeSrc = LoadPNG_RW(rwops.get());
                    if (policeSrc) {
                        SDL_Log("Loaded police station sprite from: %s", path.c_str());
                        break;
                    }
                }
            }

            if (atlas && policeSrc) {
                SDL_SetSurfaceBlendMode(policeSrc.get(), SDL_BLENDMODE_NONE);
                for (int f = 0; f < numFrames; ++f) {
                    SDL_Rect dst{ f * frameW, 0, frameW, frameH };
                    SDL_BlitScaled(policeSrc.get(), nullptr, atlas.get(), &dst);
                }

                objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0] = std::move(atlas);
                objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][1] =
                    scaleRGBASurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0].get(), 2);
                objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][2] =
                    scaleRGBASurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0].get(), 3);

                // House-independent sprite — clone for every house slot
                // so getZoomedObjPic() never attempts a palette remap on
                // RGBA data.
                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z]) {
                            objPic[ObjPic_PoliceStation][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z].get(),
                                                   objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            } else {
                SDL_Log("Police station sprite not found; using placeholder");
                // Fill the already-cleared atlas with a visible debug color so
                // objPic is never null and getZoomedObjPic won't crash.
                if (atlas) {
                    SDL_FillRect(atlas.get(), nullptr,
                                 SDL_MapRGBA(atlas->format, 100, 120, 180, 255));
                    objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0] = std::move(atlas);
                    objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][1] =
                        scaleRGBASurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0].get(), 2);
                    objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][2] =
                        scaleRGBASurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][0].get(), 3);
                    for (int h = 1; h < NUM_HOUSES; h++) {
                        for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                            if (objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z]) {
                                objPic[ObjPic_PoliceStation][h][z] = sdl2::surface_ptr{
                                    SDL_ConvertSurface(objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z].get(),
                                                       objPic[ObjPic_PoliceStation][HOUSE_HARKONNEN][z]->format, 0)
                                };
                            }
                        }
                    }
                }
            }
        }

    // ----- DuneCity hospital & church sprites -----
    // Auto-placed on residential zones by the game (SC Classic behavior).
    // Source: Micropolis 2x2 composites (32×32), matching zone cell size.
    {
        struct CivicSpec { int objPicID; const char* fileName; };
        const CivicSpec civics[] = {
            { ObjPic_Hospital, "hospital_2x2.png" },
            { ObjPic_Church,   "church_2x2.png"   },
        };
        std::vector<std::string> civicDirs = {
            getDuneLegacyDataDir() + "imported_sprites/micropolis/composites_2x2/",
            binDir + "imported_sprites/micropolis/composites_2x2/",
            binDir + "../../imported_sprites/micropolis/composites_2x2/",
            binDir + "../../../../../imported_sprites/micropolis/composites_2x2/",
        };
        if (srcDirEnv && srcDirEnv[0]) {
            std::string sd = srcDirEnv;
            if (sd.back() != '/' && sd.back() != '\\') sd += '/';
            civicDirs.push_back(sd + "imported_sprites/micropolis/composites_2x2/");
        }
        for (const auto& cs : civics) {
            sdl2::surface_ptr cell;
            for (const auto& dir : civicDirs) {
                auto rw = sdl2::RWops_ptr{ SDL_RWFromFile((dir + cs.fileName).c_str(), "rb") };
                if (rw) {
                    cell = LoadPNG_RW(rw.get());
                    if (cell) { SDL_Log("Loaded civic sprite: %s from %s", cs.fileName, dir.c_str()); break; }
                }
            }
            const int cellSize = 2 * D2_TILESIZE;  // 32
            if (!cell) {
                SDL_Log("Civic sprite %s not found; using placeholder", cs.fileName);
                cell = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, cellSize, cellSize,
                    SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                if (cell) SDL_FillRect(cell.get(), nullptr, SDL_MapRGBA(cell->format, 200, 200, 200, 255));
            }
            if (cell) {
                if (cell->w != cellSize || cell->h != cellSize) {
                    sdl2::surface_ptr scaled{ SDL_CreateRGBSurface(0, cellSize, cellSize,
                        SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                    if (scaled) {
                        SDL_SetSurfaceBlendMode(cell.get(), SDL_BLENDMODE_NONE);
                        SDL_BlitScaled(cell.get(), nullptr, scaled.get(), nullptr);
                        cell = std::move(scaled);
                    }
                }
                objPic[cs.objPicID][HOUSE_HARKONNEN][0] = std::move(cell);
                objPic[cs.objPicID][HOUSE_HARKONNEN][1] = scaleRGBASurface(objPic[cs.objPicID][HOUSE_HARKONNEN][0].get(), 2);
                objPic[cs.objPicID][HOUSE_HARKONNEN][2] = scaleRGBASurface(objPic[cs.objPicID][HOUSE_HARKONNEN][0].get(), 3);
                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[cs.objPicID][HOUSE_HARKONNEN][z]) {
                            objPic[cs.objPicID][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[cs.objPicID][HOUSE_HARKONNEN][z].get(),
                                                   objPic[cs.objPicID][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            }
        }
    }
    } // end city-sprite loading scope (binDir, srcDirEnv, scaleRGBASurface)

    // Final safety net: ensure every DuneCity civic sprite has a populated
    // objPic for HOUSE_HARKONNEN at all zoom levels, and cloned for every
    // house.  If ANY of the per-sprite load blocks above failed to populate
    // (e.g. SDL_CreateRGBSurface returned null, or an unexpected code path),
    // clone from ConstructionYard so getObjPic/getZoomedObjPic never throws.
    {
        static const unsigned int civicIds[] = {
            ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
            ObjPic_Airport, ObjPic_Hospital, ObjPic_Church
        };
        for (auto cid : civicIds) {
            if (!objPic[cid][HOUSE_HARKONNEN][0]) {
                SDL_Log("GFXManager: civic sprite ID %u still null after load — cloning ConstructionYard as fallback", cid);
                for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                    if (objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]) {
                        objPic[cid][HOUSE_HARKONNEN][z] = sdl2::surface_ptr{
                            SDL_ConvertSurface(objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z].get(),
                                               objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]->format, 0)
                        };
                    }
                }
                for (int h = 1; h < NUM_HOUSES; h++) {
                    for (int z = 0; z < NUM_ZOOMLEVEL; z++) {
                        if (objPic[cid][HOUSE_HARKONNEN][z]) {
                            objPic[cid][h][z] = sdl2::surface_ptr{
                                SDL_ConvertSurface(objPic[cid][HOUSE_HARKONNEN][z].get(),
                                                   objPic[cid][HOUSE_HARKONNEN][z]->format, 0)
                            };
                        }
                    }
                }
            }
        }
    }

    objPic[ObjPic_Bullet_SmallRocket][HOUSE_HARKONNEN][0] = units->getPictureArray(16,1,ROCKET_ROW(35));
    objPic[ObjPic_Bullet_MediumRocket][HOUSE_HARKONNEN][0] = units->getPictureArray(16,1,ROCKET_ROW(20));
    objPic[ObjPic_Bullet_LargeRocket][HOUSE_HARKONNEN][0] = units->getPictureArray(16,1,ROCKET_ROW(40));
    objPic[ObjPic_Bullet_Small][HOUSE_HARKONNEN][0] = units1->getPicture(23);
    objPic[ObjPic_Bullet_Medium][HOUSE_HARKONNEN][0] = units1->getPicture(24);
    objPic[ObjPic_Bullet_Large][HOUSE_HARKONNEN][0] = units1->getPicture(25);
    objPic[ObjPic_Bullet_Sonic][HOUSE_HARKONNEN][0] = units1->getPicture(10);
    replaceColor(objPic[ObjPic_Bullet_Sonic][HOUSE_HARKONNEN][0].get(), PALCOLOR_WHITE, PALCOLOR_BLACK);
    objPic[ObjPic_Bullet_SonicTemp][HOUSE_HARKONNEN][0] = units1->getPicture(10);
    objPic[ObjPic_Hit_Gas][HOUSE_ORDOS][0] = units1->getPictureArray(5,1,57|TILE_NORMAL,58|TILE_NORMAL,59|TILE_NORMAL,60|TILE_NORMAL,61|TILE_NORMAL);
    objPic[ObjPic_Hit_Gas][HOUSE_HARKONNEN][0] = mapSurfaceColorRange(objPic[ObjPic_Hit_Gas][HOUSE_ORDOS][0].get(), PALCOLOR_ORDOS, PALCOLOR_HARKONNEN);
    objPic[ObjPic_Hit_ShellSmall][HOUSE_HARKONNEN][0] = units1->getPicture(2);
    objPic[ObjPic_Hit_ShellMedium][HOUSE_HARKONNEN][0] = units1->getPicture(3);
    objPic[ObjPic_Hit_ShellLarge][HOUSE_HARKONNEN][0] = units1->getPicture(4);
    objPic[ObjPic_ExplosionSmall][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,32|TILE_NORMAL,33|TILE_NORMAL,34|TILE_NORMAL,35|TILE_NORMAL,36|TILE_NORMAL);
    objPic[ObjPic_ExplosionMedium1][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,47|TILE_NORMAL,48|TILE_NORMAL,49|TILE_NORMAL,50|TILE_NORMAL,51|TILE_NORMAL);
    objPic[ObjPic_ExplosionMedium2][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,52|TILE_NORMAL,53|TILE_NORMAL,54|TILE_NORMAL,55|TILE_NORMAL,56|TILE_NORMAL);
    objPic[ObjPic_ExplosionLarge1][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,37|TILE_NORMAL,38|TILE_NORMAL,39|TILE_NORMAL,40|TILE_NORMAL,41|TILE_NORMAL);
    objPic[ObjPic_ExplosionLarge2][HOUSE_HARKONNEN][0] = units1->getPictureArray(5,1,42|TILE_NORMAL,43|TILE_NORMAL,44|TILE_NORMAL,45|TILE_NORMAL,46|TILE_NORMAL);
    objPic[ObjPic_ExplosionSmallUnit][HOUSE_HARKONNEN][0] = units1->getPictureArray(2,1,0|TILE_NORMAL,1|TILE_NORMAL);
    objPic[ObjPic_ExplosionFlames][HOUSE_HARKONNEN][0] = units1->getPictureArray(21,1,  11|TILE_NORMAL,12|TILE_NORMAL,13|TILE_NORMAL,17|TILE_NORMAL,18|TILE_NORMAL,19|TILE_NORMAL,17|TILE_NORMAL,
                                                                                    18|TILE_NORMAL,19|TILE_NORMAL,17|TILE_NORMAL,18|TILE_NORMAL,19|TILE_NORMAL,17|TILE_NORMAL,18|TILE_NORMAL,
                                                                                    19|TILE_NORMAL,17|TILE_NORMAL,18|TILE_NORMAL,19|TILE_NORMAL,20|TILE_NORMAL,21|TILE_NORMAL,22|TILE_NORMAL);
    objPic[ObjPic_ExplosionSpiceBloom][HOUSE_HARKONNEN][0] = units1->getPictureArray(3,1,7|TILE_NORMAL,6|TILE_NORMAL,5|TILE_NORMAL);
    objPic[ObjPic_DeadInfantry][HOUSE_HARKONNEN][0] = icon->getPictureArray(4,1,1,6);
    objPic[ObjPic_DeadAirUnit][HOUSE_HARKONNEN][0] = icon->getPictureArray(3,1,1,6);
    objPic[ObjPic_Smoke][HOUSE_HARKONNEN][0] = units1->getPictureArray(3,1,29|TILE_NORMAL,30|TILE_NORMAL,31|TILE_NORMAL);
    objPic[ObjPic_SandwormShimmerMask][HOUSE_HARKONNEN][0] = units1->getPicture(10);
    replaceColor(objPic[ObjPic_SandwormShimmerMask][HOUSE_HARKONNEN][0].get(), PALCOLOR_WHITE, PALCOLOR_BLACK);
    objPic[ObjPic_SandwormShimmerTemp][HOUSE_HARKONNEN][0] = units1->getPicture(10);
    objPic[ObjPic_Terrain][HOUSE_HARKONNEN][0] = icon->getPictureRow(124,209,NUM_TERRAIN_TILES_X);
    objPic[ObjPic_Terrain_GreenSpice][HOUSE_HARKONNEN][0] =
        createTintedTerrainSpiceSurface(objPic[ObjPic_Terrain][HOUSE_HARKONNEN][0].get(),
                                        SDL_Color{ 24, 112, 48, 255 },
                                        SDL_Color{ 20, 84, 42, 255 });
    objPic[ObjPic_Terrain_RedSpice][HOUSE_HARKONNEN][0] =
        createTintedTerrainSpiceSurface(objPic[ObjPic_Terrain][HOUSE_HARKONNEN][0].get(),
                                        SDL_Color{ 136, 48, 40, 255 },
                                        SDL_Color{ 96, 32, 30, 255 });
    objPic[ObjPic_DestroyedStructure][HOUSE_HARKONNEN][0] = icon->getPictureRow2(14, 33, 125, 213, 214, 215, 223, 224, 225, 232, 233, 234, 240, 246, 247);
    objPic[ObjPic_RockDamage][HOUSE_HARKONNEN][0] = icon->getPictureRow(1,6);
    objPic[ObjPic_SandDamage][HOUSE_HARKONNEN][0] = icon->getPictureRow(7,12);
    objPic[ObjPic_Terrain_Hidden][HOUSE_HARKONNEN][0] = icon->getPictureRow(108,123);
    objPic[ObjPic_Terrain_HiddenFog][HOUSE_HARKONNEN][0] = icon->getPictureRow(108,123);
    objPic[ObjPic_Terrain_Tracks][HOUSE_HARKONNEN][0] = icon->getPictureRow(25,32);
    objPic[ObjPic_Star][HOUSE_HARKONNEN][0] = LoadPNG_RW(pFileManager->openFile("Star5x5.png").get());
    objPic[ObjPic_Star][HOUSE_HARKONNEN][1] = LoadPNG_RW(pFileManager->openFile("Star7x7.png").get());
    objPic[ObjPic_Star][HOUSE_HARKONNEN][2] = LoadPNG_RW(pFileManager->openFile("Star11x11.png").get());

    // Load IBM.PAL early so Tornie's 8-bit sprites use the standard sprite
    // palette before house-color remapping.
    // Custom_IBM.PAL is reserved for extra house-color ramps and is not used
    // to recolor sprite assets.
    Palette ibmPalette(256);
    bool ibmPaletteLoaded = false;
    try {
        if(pFileManager->exists("IBM.PAL")) {
            ibmPalette = LoadPalette_RW(pFileManager->openFile("IBM.PAL").get());
            ibmPaletteLoaded = true;
        }
        SDL_Log("GFXManager: ibmPalette loaded (%d colors, source=%s)",
                ibmPalette.getNumColors(),
                ibmPaletteLoaded ? "IBM.PAL" : "PNG palette");
    } catch(const std::exception& e) {
        SDL_Log("GFXManager: ibmPalette load failed (%s) — Tornie sprite tinting disabled", e.what());
    }

    auto openTornieAsset = [&](const char* filename, const char* label) -> sdl2::RWops_ptr {
        const bool tornieActive = ModManager::instance().isInitialized()
            && ModManager::instance().isTornieContentActive();

        if(!tornieActive) {
            return nullptr;
        }

        if(pFileManager->exists(filename)) {
            SDL_Log("GFXManager: %s asset '%s' loaded through active Tornie lookup", label, filename);
            return pFileManager->openFile(filename);
        }

        if(auto packedAsset = pFileManager->openFileFromNamedPak(filename, "Tornie.PAK")) {
            SDL_Log("GFXManager: %s asset '%s' loaded directly from Tornie.PAK", label, filename);
            return packedAsset;
        }

        return nullptr;
    };

    auto getTornieFrameCount = [](SDL_Surface* surface, int frameWidth, int frameHeight) -> int {
        if(!surface || frameWidth <= 0 || frameHeight <= 0) {
            return 0;
        }

        if(surface->w >= 2 * frameWidth && surface->h >= frameHeight && surface->h < 2 * frameHeight) {
            return std::max(1, surface->w / frameWidth);
        }

        if(surface->h >= 2 * frameHeight) {
            return std::max(1, surface->h / frameHeight);
        }

        return 1;
    };

    auto getTornieFrameRect = [&](SDL_Surface* surface, int frameWidth, int frameHeight, int frame) -> SDL_Rect {
        if(!surface || frameWidth <= 0 || frameHeight <= 0) {
            return SDL_Rect{0, 0, 0, 0};
        }

        const bool horizontal =
            surface->w >= 2 * frameWidth
            && surface->h >= frameHeight
            && surface->h < 2 * frameHeight;
        const int frameCount = getTornieFrameCount(surface, frameWidth, frameHeight);
        const int clampedFrame = std::max(0, std::min(frame, std::max(0, frameCount - 1)));

        if(horizontal) {
            const int sourceX = clampedFrame * frameWidth;
            return SDL_Rect{
                sourceX,
                0,
                std::min(frameWidth, std::max(0, surface->w - sourceX)),
                std::min(frameHeight, surface->h)
            };
        }

        const int sourceY = (frameCount > 1) ? clampedFrame * frameHeight : 0;
        return SDL_Rect{
            0,
            sourceY,
            std::min(frameWidth, surface->w),
            std::min(frameHeight, std::max(0, surface->h - sourceY))
        };
    };

    auto loadTorniePalettedSprite = [&](unsigned int objPicEnum,
                                        const char* pngName,
                                        const char* label) {
        try {
            auto rwop = openTornieAsset(pngName, label);
            if(!rwop) {
                SDL_Log("GFXManager: %s sprite '%s' missing", label, pngName);
                return;
            }

            auto raw = LoadPNG_RW(rwop.get());
            if(!raw) {
                SDL_Log("GFXManager: %s sprite '%s' failed to decode", label, pngName);
                return;
            }

            if(raw->format->BitsPerPixel != 8 || !raw->format->palette) {
                SDL_Log("GFXManager: %s sprite '%s' is not 8-bit indexed, refusing it", label, pngName);
                return;
            }
            normalizeTransparentPaletteIndexes(raw.get());
            if(ibmPaletteLoaded) {
                if(auto remapped = remapIndexedSurfaceToPalette(raw.get(), ibmPalette.getSDLPalette())) {
                    raw = std::move(remapped);
                } else {
                    ibmPalette.applyToSurface(raw.get());
                }
                normalizeTransparentPaletteIndexes(raw.get());
            }

            objPic[objPicEnum][HOUSE_HARKONNEN][0] = std::move(raw);
            if(objPic[objPicEnum][HOUSE_HARKONNEN][0]) {
                objPic[objPicEnum][HOUSE_HARKONNEN][1] =
                    Scaler::defaultDoubleSurface(objPic[objPicEnum][HOUSE_HARKONNEN][0].get());
                if(objPic[objPicEnum][HOUSE_HARKONNEN][1]) {
                    objPic[objPicEnum][HOUSE_HARKONNEN][2] =
                        Scaler::defaultDoubleSurface(objPic[objPicEnum][HOUSE_HARKONNEN][1].get());
                }
            }
            SDL_Log("GFXManager: %s sprite '%s' loaded", label, pngName);
        } catch(std::exception& e) {
            SDL_Log("GFXManager: %s sprite load failed (%s)", label, e.what());
        }
    };

    auto loadTornieIndexedSheet = [&](const char* pngName, const char* label) -> sdl2::surface_ptr {
        auto rwop = openTornieAsset(pngName, label);
        if(!rwop) {
            SDL_Log("GFXManager: %s sheet '%s' missing", label, pngName);
            return nullptr;
        }

        auto sheet = LoadPNG_RW(rwop.get());
        if(!sheet || sheet->format->BitsPerPixel != 8 || !sheet->format->palette) {
            SDL_Log("GFXManager: %s sheet '%s' is not 8-bit indexed", label, pngName);
            return nullptr;
        }

        normalizeTransparentPaletteIndexes(sheet.get());
        if(ibmPaletteLoaded) {
            ibmPalette.applyToSurface(sheet.get());
        }
        SDL_SetColorKey(sheet.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
        return sheet;
    };

    auto createGroundUnitAtlas = [&](SDL_Surface* sheet, int firstFrame,
                                      const char* label) -> sdl2::surface_ptr {
        constexpr int frameSize = D2_TILESIZE;
        constexpr int sourceColumns = 10;
        if(!sheet || sheet->w < sourceColumns * frameSize || firstFrame < 0
                || (firstFrame + 4) / sourceColumns * frameSize + frameSize > sheet->h) {
            SDL_Log("GFXManager: %s has an invalid ground-unit sheet layout", label);
            return nullptr;
        }

        auto atlas = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, NUM_ANGLES * frameSize,
                                                              frameSize, 8, 0, 0, 0, 0) };
        if(!atlas || !atlas->format->palette) {
            return nullptr;
        }

        SDL_SetPaletteColors(atlas->format->palette, sheet->format->palette->colors,
                             0, sheet->format->palette->ncolors);
        SDL_FillRect(atlas.get(), nullptr, PALCOLOR_TRANSPARENT);
        SDL_SetColorKey(atlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

        static const int sourceFrames[NUM_ANGLES] = { 2, 1, 0, 1, 2, 3, 4, 3 };
        static const bool mirrorFrames[NUM_ANGLES] = { false, false, false, true,
                                                       true, true, false, false };
        for(int angle = 0; angle < NUM_ANGLES; ++angle) {
            const int sourceIndex = firstFrame + sourceFrames[angle];
            const int sourceX = (sourceIndex % sourceColumns) * frameSize;
            const int sourceY = (sourceIndex / sourceColumns) * frameSize;
            auto frame = getSubPicture(sheet, sourceX, sourceY, frameSize, frameSize);
            if(mirrorFrames[angle]) {
                frame = flipVSurface(frame.get());
            }
            SDL_Rect destination{ angle * frameSize, 0, frameSize, frameSize };
            SDL_BlitSurface(frame.get(), nullptr, atlas.get(), &destination);
        }

        return atlas;
    };

    auto installGroundUnitAtlas = [&](unsigned int objPicEnum, SDL_Surface* sheet,
                                      int firstFrame, const char* label) {
        auto atlas = createGroundUnitAtlas(sheet, firstFrame, label);
        if(!atlas) {
            return false;
        }

        objPic[objPicEnum][HOUSE_HARKONNEN][0] = std::move(atlas);
        objPic[objPicEnum][HOUSE_HARKONNEN][1] =
            Scaler::defaultDoubleSurface(objPic[objPicEnum][HOUSE_HARKONNEN][0].get());
        if(objPic[objPicEnum][HOUSE_HARKONNEN][1]) {
            objPic[objPicEnum][HOUSE_HARKONNEN][2] =
                Scaler::defaultDoubleSurface(objPic[objPicEnum][HOUSE_HARKONNEN][1].get());
        }
        SDL_Log("GFXManager: installed Tornie %s ground-unit atlas", label);
        return true;
    };

    loadTorniePalettedSprite(ObjPic_DeviatorGunTornie,
                             "DeviatorGun.png",
                             "Deviator turret");
    loadTorniePalettedSprite(ObjPic_FlameTankGunTornie,
                             "FlameTankGun.png",
                             "Flame Tank turret");
    loadTorniePalettedSprite(ObjPic_EliteLauncherGunTornie,
                             "EliteLauncherGun.png",
                             "Elite Launcher turret");
    loadTorniePalettedSprite(ObjPic_ChemicalSiegeTankGunTornie,
                             "ChemicalSiegeTank.png",
                             "Chemical Siege Tank turret");
    loadTorniePalettedSprite(ObjPic_HarvestankGunTornie,
                             "HarvestankGun.png",
                             "Harvestank turret");

    try {
        auto setAdvancedWindtrapAtlas = [&](int objPicEnum, sdl2::surface_ptr atlas, const char* label) {
            if(!atlas) {
                return false;
            }

            normalizeTornieStructureTeamPaintToHarkonnen(
                atlas.get(), static_cast<unsigned int>(objPicEnum));
            normalizeTransparentPaletteIndexes(atlas.get());

            bool installedHarkonnen = false;
            for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
                sdl2::surface_ptr indexed;
                if(colorSlot == HOUSE_HARKONNEN) {
                    indexed = copySurface(atlas.get());
                } else {
                    indexed = mapSurfaceColorRange(
                        atlas.get(), PALCOLOR_HARKONNEN, getVisualRemapPaletteIndex(colorSlot));
                    if(indexed) {
                        applyCustomVisualColorRamp(indexed.get(), colorSlot);
                        applyRebelsTint(indexed.get(), colorSlot);
                    }
                }

                if(!indexed) {
                    SDL_Log("GFXManager: Advanced Windtrap %s color slot %d remap failed",
                            label, colorSlot);
                    continue;
                }

                normalizeTransparentPaletteIndexes(indexed.get());
                auto animatedAtlas = generateTornieWindtrapAnimationFrames(indexed.get());
                if(!animatedAtlas) {
                    SDL_Log("GFXManager: Advanced Windtrap %s color slot %d animation atlas generation failed",
                            label, colorSlot);
                    continue;
                }

                objPic[objPicEnum][colorSlot][0] = std::move(animatedAtlas);
                objPic[objPicEnum][colorSlot][1] =
                    scaleSurfaceNearest(objPic[objPicEnum][colorSlot][0].get(), 2);
                if(objPic[objPicEnum][colorSlot][1]) {
                    objPic[objPicEnum][colorSlot][2] =
                        scaleSurfaceNearest(objPic[objPicEnum][colorSlot][0].get(), 3);
                }
                installedHarkonnen = installedHarkonnen || colorSlot == HOUSE_HARKONNEN;
            }

            if(installedHarkonnen) {
                SDL_Log("GFXManager: Advanced Windtrap %s sprite loaded for %d visual colour slots",
                        label, NUM_HOUSE_COLOR_SLOTS);
            }
            return installedHarkonnen;
        };

        auto loadIndexedTornieAtlasSource = [&](const char* pngName, const char* label, bool useAssetSpecificTeamPaint = false) -> sdl2::surface_ptr {
            auto rwop = openTornieAsset(pngName, label);
            if(!rwop) {
                SDL_Log("GFXManager: %s sprite '%s' missing", label, pngName);
                return nullptr;
            }

            auto raw = LoadPNG_RW(rwop.get());
            if(!raw) {
                SDL_Log("GFXManager: %s sprite '%s' could not be decoded", label, pngName);
                return nullptr;
            }

            if(raw->format->BytesPerPixel != 1 || !raw->format->palette) {
                SDL_Surface* paletteReference = objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0].get();
                const SDL_Palette* targetPalette =
                    (paletteReference && paletteReference->format)
                        ? paletteReference->format->palette
                        : nullptr;
                auto indexed = convertTruecolorSurfaceToPalette(
                    raw.get(),
                    targetPalette,
                    PALCOLOR_WINDTRAP_COLORCYCLE,
                    SDL_Color{251, 108, 245, SDL_ALPHA_OPAQUE});
                if(!indexed) {
                    SDL_Log("GFXManager: %s sprite '%s' could not be converted to the indexed Windtrap palette",
                            label, pngName);
                    return nullptr;
                }
                raw = std::move(indexed);
                SDL_Log("GFXManager: %s sprite '%s' converted from truecolor to the indexed Windtrap palette",
                        label, pngName);
            }

            preserveOpaqueBlackIndex(raw.get());
            normalizeTransparentPaletteIndexes(raw.get());
            if(ibmPaletteLoaded) {
                // Tornie structure PNGs encode team-paint brightness in their original
                // palette indices. Apply IBM.PAL without reassigning those indices.
                ibmPalette.applyToSurface(raw.get());
                normalizeTransparentPaletteIndexes(raw.get());
            }
            normalizeHouseColorRangesToHarkonnen(raw.get());
            if(!useAssetSpecificTeamPaint) {
                normalizeHarkonnenTeamRed(raw.get());
                normalizeLooseTeamPaintToHarkonnen(raw.get());
            }

            return raw;
        };

        auto loadAdvancedWindtrapVariant = [&](int objPicEnum,
                                               const char* pngName,
                                               Coord footprint,
                                               const char* label,
                                               const char* buildSiteName = nullptr) {
            auto raw = loadIndexedTornieAtlasSource(
                pngName,
                std::string("Advanced Windtrap ").append(label).c_str(),
                objPicEnum == ObjPic_AdvancedWindTrap);
            if(!raw) {
                return false;
            }

            const int frameWidth = footprint.x * D2_TILESIZE;
            const int frameHeight = footprint.y * D2_TILESIZE;
            const int rawFrameCount = getTornieFrameCount(raw.get(), frameWidth, frameHeight);
            const bool rawHasFullHorizontalAtlas =
                raw->w >= 4 * frameWidth
                && raw->h >= frameHeight
                && raw->h < 2 * frameHeight;
            if(rawFrameCount <= 0) {
                SDL_Log("GFXManager: Advanced Windtrap %s sprite '%s' has an unsupported size", label, pngName);
                return false;
            }
            logTornieStructureSurfaceDiagnostics("raw-normalized", label, raw.get(), frameWidth, frameHeight);

            sdl2::surface_ptr buildSite;
            if(buildSiteName != nullptr && pFileManager->exists(buildSiteName)) {
                buildSite = loadIndexedTornieAtlasSource(buildSiteName, std::string("Advanced Windtrap build site ").append(label).c_str());
                if(buildSite) {
                    logTornieStructureSurfaceDiagnostics("build-normalized", label, buildSite.get(), frameWidth, frameHeight);
                }
            }
            SDL_Surface* buildSource = buildSite ? buildSite.get() : raw.get();
            const int buildFrameCount = getTornieFrameCount(buildSource, frameWidth, frameHeight);

            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, 4 * frameWidth, frameHeight, 8, 0, 0, 0, 0) };
            if(!atlas || !atlas->format->palette) {
                return false;
            }

            SDL_SetPaletteColors(atlas->format->palette,
                                 raw->format->palette->colors,
                                 0,
                                 raw->format->palette->ncolors);
            SDL_SetSurfaceBlendMode(raw.get(), SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(buildSource, SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_NONE);
            SDL_SetColorKey(raw.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(buildSource, SDL_TRUE, PALCOLOR_TRANSPARENT);
            SDL_FillRect(atlas.get(), nullptr, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(atlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

            SDL_Rect srcTop = getTornieFrameRect(raw.get(), frameWidth, frameHeight, rawHasFullHorizontalAtlas ? 2 : 0);
            SDL_Rect srcBottom = getTornieFrameRect(raw.get(), frameWidth, frameHeight,
                                                    rawHasFullHorizontalAtlas ? 3 : (rawFrameCount > 1 ? 1 : 0));
            SDL_Rect buildTop = getTornieFrameRect(buildSource, frameWidth, frameHeight, 0);
            SDL_Rect buildBottom = getTornieFrameRect(buildSource, frameWidth, frameHeight, buildFrameCount > 1 ? 1 : 0);

            const bool protectOpaqueBlack =
                raw->format->palette->ncolors > PALCOLOR_BLACK
                && buildSource->format->palette->ncolors > PALCOLOR_BLACK
                && atlas->format->palette->ncolors > PALCOLOR_BLACK;
            const SDL_Color rawBlack = protectOpaqueBlack ? raw->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            const SDL_Color buildBlack = protectOpaqueBlack ? buildSource->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            const SDL_Color atlasBlack = protectOpaqueBlack ? atlas->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            if(protectOpaqueBlack) {
                raw->format->palette->colors[PALCOLOR_BLACK].g = 1;
                buildSource->format->palette->colors[PALCOLOR_BLACK].g = 1;
                atlas->format->palette->colors[PALCOLOR_BLACK].g = 1;
            }

            auto blitFrame = [&](SDL_Surface* source, SDL_Rect* src, int frame) {
                if(!source || src->w <= 0 || src->h <= 0) {
                    return;
                }
                SDL_Rect dst{frame * frameWidth + (frameWidth - src->w) / 2, frameHeight - src->h, src->w, src->h};
                SDL_BlitSurface(source, src, atlas.get(), &dst);
            };

            blitFrame(buildSource, &buildTop, 0);
            blitFrame(buildSource, &buildBottom, 1);
            blitFrame(raw.get(), &srcTop, 2);
            blitFrame(raw.get(), &srcBottom, 3);
            logTornieStructureSurfaceDiagnostics("atlas-built", label, atlas.get(), frameWidth, frameHeight);

            if(protectOpaqueBlack) {
                raw->format->palette->colors[PALCOLOR_BLACK] = rawBlack;
                buildSource->format->palette->colors[PALCOLOR_BLACK] = buildBlack;
                atlas->format->palette->colors[PALCOLOR_BLACK] = atlasBlack;
            }

            normalizeTransparentPaletteIndexes(atlas.get());
            logTornieStructureSurfaceDiagnostics("atlas-final", label, atlas.get(), frameWidth, frameHeight);

            return setAdvancedWindtrapAtlas(objPicEnum, std::move(atlas), label);
        };

        auto createAdvancedWindtrapPlaceholder = [&](int objPicEnum, Coord footprint, const char* label) {
            if(objPic[objPicEnum][HOUSE_HARKONNEN][0] != nullptr) {
                return;
            }

            const int frameWidth = footprint.x * D2_TILESIZE;
            const int frameHeight = footprint.y * D2_TILESIZE;
            sdl2::surface_ptr placeholder{ SDL_CreateRGBSurface(0, 4 * frameWidth, frameHeight, 8, 0, 0, 0, 0) };
            SDL_Surface* windtrap = objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0].get();
            if(!placeholder || !placeholder->format->palette || !windtrap || !windtrap->format->palette) {
                return;
            }

            SDL_SetPaletteColors(placeholder->format->palette,
                                 windtrap->format->palette->colors,
                                 0,
                                 windtrap->format->palette->ncolors);
            SDL_SetSurfaceBlendMode(placeholder.get(), SDL_BLENDMODE_NONE);
            SDL_FillRect(placeholder.get(), nullptr, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(placeholder.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

            const int windtrapFrameSize = 2 * D2_TILESIZE;
            SDL_Rect src{2 * windtrapFrameSize, 0, windtrapFrameSize, windtrapFrameSize};
            src.w = std::min(src.w, frameWidth);
            src.h = std::min(src.h, frameHeight);
            for(int frame = 0; frame < 4; frame++) {
                SDL_Rect dst{frame * frameWidth + (frameWidth - src.w) / 2, frameHeight - src.h, src.w, src.h};
                SDL_BlitSurface(windtrap, &src, placeholder.get(), &dst);
            }

            normalizeTransparentPaletteIndexes(placeholder.get());
            logTornieStructureSurfaceDiagnostics("atlas-final", label, placeholder.get(), frameWidth, frameHeight);

            setAdvancedWindtrapAtlas(objPicEnum, std::move(placeholder), label);
        };

        if(!loadAdvancedWindtrapVariant(ObjPic_AdvancedWindTrap, "Tornie_AdvancedWindtrap_gfx.png", Coord(3,3), "3x3", "BUILDING_3x3_prebuild.png")) {
            loadAdvancedWindtrapVariant(ObjPic_AdvancedWindTrap, "super_power_plant.png", Coord(3,3), "3x3 fallback");
        }
        loadAdvancedWindtrapVariant(ObjPic_AdvancedWindTrap2x3, "advanced_power_2x3.png", Coord(2,3), "2x3", "BuildSite_2x3.png");
        loadAdvancedWindtrapVariant(ObjPic_AdvancedWindTrap3x2, "Advanced_Power_Plant.png", Coord(3,2), "3x2", "BUILDING_3x2_prebuild.png");

        createAdvancedWindtrapPlaceholder(ObjPic_AdvancedWindTrap, Coord(3,3), "3x3 placeholder");
        createAdvancedWindtrapPlaceholder(ObjPic_AdvancedWindTrap2x3, Coord(2,3), "2x3 placeholder");
        createAdvancedWindtrapPlaceholder(ObjPic_AdvancedWindTrap3x2, Coord(3,2), "3x2 placeholder");
    } catch(std::exception& e) {
        SDL_Log("GFXManager: Advanced Windtrap sprite load failed (%s)", e.what());
    }

    // DuneCity 1.0.503: Rocket Trike uses its own dedicated sprite (8-frame,
    // 1-tile-tall strip, 128x16) with a separate RocketTrikeMask.png that holds
    // the per-house colour tint via the benePalette remap path. Restored from
    // v1.0.250 (commit 4265a5f in v1.0.251 dropped this; Tornie confirms the
    // mask carries the intended red-tint variant).
    //
    // Preferred path: RocketTrikeMask.png (8-bit palette-indexed) — load into
    // HOUSE_HARKONNEN only; getZoomedObjPic remaps indices 144-150 per house.
    // Fallback: RocketTrike.png (RGBA) — pre-build all zoom/house slots.
    // Final fallback: vanilla Trike sprite.
    objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0] = units->getPictureArray(8,1,GROUNDUNIT_ROW(5));
    {
        bool usedPaletteIndexed = false;
        try {
            if(pFileManager->exists("RocketTrikeMask.png")) {
                auto rtMask = LoadPNG_RW(pFileManager->openFile("RocketTrikeMask.png").get());
                if(rtMask && rtMask->format->BitsPerPixel == 8 && rtMask->format->palette) {
                    // v1.0.509 (Tornie OOB): switch from benePalette to ibmPalette so
                    // the RocketTrike participates in the per-house color remap that
                    // mapSurfaceColorRange drives from PALCOLOR_HARKONNEN. benePalette
                    // was the original v1.0.250 fix but gave the RocketTrike a fixed
                    // red tint that didn't shift with the owning house — that was a
                    // mistake per the user.
                    if(ibmPaletteLoaded) {
                        ibmPalette.applyToSurface(rtMask.get());
                    }
                    objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0] = std::move(rtMask);
                    usedPaletteIndexed = true;
                    SDL_Log("GFXManager: Loaded RocketTrikeMask.png (palette-indexed, per-house remap)");
                } else if(rtMask) {
                    SDL_Log("GFXManager: RocketTrikeMask.png is not 8-bit palette-indexed (%d bpp), falling back to RGBA path",
                            rtMask->format->BitsPerPixel);
                }
            }
            if(!usedPaletteIndexed && pFileManager->exists("RocketTrike.png")) {
                auto rtRaw = LoadPNG_RW(pFileManager->openFile("RocketTrike.png").get());
                if(rtRaw) {
                    sdl2::surface_ptr rtSurf{ SDL_ConvertSurfaceFormat(rtRaw.get(), SCREEN_FORMAT, 0) };
                    if(rtSurf) {
                        auto scaleRT = [](SDL_Surface* src, int factor) -> sdl2::surface_ptr {
                            sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                                src->w * factor, src->h * factor,
                                src->format->BitsPerPixel,
                                src->format->Rmask, src->format->Gmask,
                                src->format->Bmask, src->format->Amask) };
                            if(dst) SDL_BlitScaled(src, nullptr, dst.get(), nullptr);
                            return dst;
                        };
                        objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0] = std::move(rtSurf);
                        objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][1] = scaleRT(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0].get(), 2);
                        objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][2] = scaleRT(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0].get(), 3);
                        for(int h = 1; h < (int)NUM_HOUSES; h++) {
                            for(int z = 0; z < NUM_ZOOMLEVEL; z++) {
                                if(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][z]) {
                                    objPic[ObjPic_RocketTrike][h][z] = sdl2::surface_ptr{
                                        SDL_ConvertSurface(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][z].get(),
                                                           objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][z]->format, 0) };
                                }
                            }
                        }
                    }
                }
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: RocketTrike sprite load failed (%s) — falling back to vanilla Trike", e.what());
        }
    }

    // Sonic Trike follows the same simple indexed eight-frame loading path as
    // Rocket Trike. The supplied PNG already contains all eight directions, so
    // it must not be normalized or rebuilt as a five-frame source strip.
    objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0] = units->getPictureArray(8, 1, GROUNDUNIT_ROW(5));
    {
        bool usedPaletteIndexed = false;
        try {
            if(pFileManager->exists("SonicTrikeMask.png")) {
                auto stMask = LoadPNG_RW(pFileManager->openFile("SonicTrikeMask.png").get());
                if(stMask && stMask->format->BitsPerPixel == 8 && stMask->format->palette
                   && stMask->h > 0 && stMask->w == stMask->h * NUM_ANGLES) {
                    if(ibmPaletteLoaded) {
                        ibmPalette.applyToSurface(stMask.get());
                    }
                    objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0] = std::move(stMask);
                    usedPaletteIndexed = true;
                    SDL_Log("GFXManager: Loaded SonicTrikeMask.png (palette-indexed, per-house remap)");
                } else if(stMask) {
                    SDL_Log("GFXManager: SonicTrikeMask.png is not an indexed 8-frame strip; falling back to SonicTrike.png");
                }
            }

            if(!usedPaletteIndexed && pFileManager->exists("SonicTrike.png")) {
                auto stRaw = LoadPNG_RW(pFileManager->openFile("SonicTrike.png").get());
                if(stRaw && stRaw->h > 0 && stRaw->w == stRaw->h * NUM_ANGLES) {
                    sdl2::surface_ptr stSurf{ SDL_ConvertSurfaceFormat(stRaw.get(), SCREEN_FORMAT, 0) };
                    if(stSurf) {
                        auto scaleST = [](SDL_Surface* src, int factor) -> sdl2::surface_ptr {
                            sdl2::surface_ptr dst{ SDL_CreateRGBSurface(0,
                                src->w * factor, src->h * factor,
                                src->format->BitsPerPixel,
                                src->format->Rmask, src->format->Gmask,
                                src->format->Bmask, src->format->Amask) };
                            if(dst) {
                                SDL_BlitScaled(src, nullptr, dst.get(), nullptr);
                            }
                            return dst;
                        };

                        objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0] = std::move(stSurf);
                        objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][1] =
                            scaleST(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0].get(), 2);
                        objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][2] =
                            scaleST(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0].get(), 3);

                        for(int h = 1; h < static_cast<int>(NUM_HOUSES); ++h) {
                            for(int z = 0; z < NUM_ZOOMLEVEL; ++z) {
                                if(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][z]) {
                                    objPic[ObjPic_SonicTrike][h][z] = sdl2::surface_ptr{
                                        SDL_ConvertSurface(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][z].get(),
                                                           objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][z]->format, 0) };
                                }
                            }
                        }
                        SDL_Log("GFXManager: Loaded SonicTrike.png (RGBA eight-direction fallback)");
                    }
                } else if(stRaw) {
                    SDL_Log("GFXManager: SonicTrike.png is not an 8-frame strip; using vanilla Trike fallback");
                }
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: SonicTrike sprite load failed (%s), falling back to vanilla Trike", e.what());
        }
    }

#if 0 // Replaced by the native 80x10 FlameTankGun.png turret strip above.
    SDL_Log("GFXManager: Loading FlameTank.png...");
    try {
        SDL_Log("GFXManager: FlameTank step 1: openFile");
        auto ftRaw = LoadPNG_RW(pFileManager->openFile("FlameTank.png").get());
        SDL_Log("GFXManager: FlameTank step 2: LoadPNG_RW returned (%s)",
                ftRaw ? "valid surface" : "null surface");
        if(ftRaw) {
            SDL_Log("GFXManager: FlameTank step 3: BitsPerPixel=%d palette=%s",
                    ftRaw->format->BitsPerPixel,
                    ftRaw->format->palette ? "yes" : "no");
            // Apply palette: 8-bit palette-indexed sprites use ibmPalette (authored
            // against IBM.PAL), not benePalette.
            if(ibmPaletteLoaded && ftRaw->format->BitsPerPixel == 8 && ftRaw->format->palette) {
                SDL_Log("GFXManager: FlameTank step 3a: apply ibmPalette");
                ibmPalette.applyToSurface(ftRaw.get());
                SDL_Log("GFXManager: FlameTank step 3b: ibmPalette applied");
            }
            SDL_Log("GFXManager: FlameTank step 4: std::move to objPic");
            objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][0] = std::move(ftRaw);
            // Generate zoom levels 1 and 2 so getZoomedObjPic never throws on a
            // null HOUSE_HARKONNEN[z>0] entry. Same pattern as v1.0.240 EliteSiegeTank fix.
            SDL_Log("GFXManager: FlameTank step 5: generate zoom 1");
            if(objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][0]) {
                objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][1] =
                    Scaler::defaultDoubleSurface(objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][0].get());
                SDL_Log("GFXManager: FlameTank step 6: generate zoom 2");
                if(objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][1]) {
                    objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][2] =
                        Scaler::defaultDoubleSurface(objPic[ObjPic_FlameTankGunTornie][HOUSE_HARKONNEN][1].get());
                }
            }
            SDL_Log("GFXManager: FlameTank.png loaded (all zoom levels)");
        } else {
            SDL_Log("GFXManager: FlameTank.png: surface is null, skipping");
        }
    } catch(std::exception& e) {
        SDL_Log("GFXManager: %s — FlameTank sprite missing, units will fall back to placeholder", e.what());
    }

#endif

    SDL_Log("GFXManager: Loading EliteSiegeTank.png...");
    try {
        auto estRaw = LoadPNG_RW(pFileManager->openFile("EliteSiegeTank.png").get());
        if(estRaw) {
            // Use ibmPalette (not benePalette) — sprite is authored against IBM.PAL.
            if(estRaw->format->BitsPerPixel != 8 || !estRaw->format->palette) {
                SDL_Log("GFXManager: EliteSiegeTank.png is not 8-bit indexed, refusing it");
                estRaw.reset();
            } else if(ibmPaletteLoaded) {
                ibmPalette.applyToSurface(estRaw.get());
            }
        }

        if(estRaw) {
            objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0] = std::move(estRaw);
            // Generate zoom levels 1 and 2 so getZoomedObjPic never throws on a
            // null HOUSE_HARKONNEN[z>0] entry. Fix from v1.0.240 EliteSiegeTank crash.
            if(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0]) {
                objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][1] =
                    Scaler::defaultDoubleSurface(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0].get());
                if(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][1]) {
                    objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][2] =
                        Scaler::defaultDoubleSurface(objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][1].get());
                }
            }
            SDL_Log("GFXManager: EliteSiegeTank.png loaded (all zoom levels)");
        }
    } catch(std::exception& e) {
        SDL_Log("GFXManager: %s — EliteSiegeTank sprite missing, units will fall back to placeholder", e.what());
    }

    // Keep the validated Elite Siege Tank turret from Tornie's indexed UNITS2
    // reconversion. The other experimental atlas replacements are deliberately
    // left on their previous graphics until their frame layouts are corrected.
    {
        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
                objPic[ObjPic_EliteSiegeTankGunTornie][colorSlot][zoom].reset();
                objPicTex[ObjPic_EliteSiegeTankGunTornie][colorSlot][zoom].reset();
            }
        }

        auto units2Sp = loadTornieIndexedSheet("TornieUnits2.png", "Tornie UNITS2");
        if(units2Sp) {
            installGroundUnitAtlas(ObjPic_EliteSiegeTankGunTornie, units2Sp.get(), 15,
                                   "Elite Siege Tank turret");
        }

        if(!objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][0]) {
            for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
                if(objPic[ObjPic_Siegetank_Gun][HOUSE_HARKONNEN][zoom]) {
                    objPic[ObjPic_EliteSiegeTankGunTornie][HOUSE_HARKONNEN][zoom] =
                        copySurface(objPic[ObjPic_Siegetank_Gun][HOUSE_HARKONNEN][zoom].get());
                }
            }
        }
    }

    // DuneCity 1.0.509: Tornie Worfinery + Tech Center dedicated sprites.
    // 48x64 PNG, 8-bit palette-indexed, 2 vertical frames (3 tiles wide × 2 tiles tall
    // per frame). Animation runs at ConstructionYard speed (handled by the
    // structure itself via frame index based on game cycle).
    auto loadTornieStructureSprite = [&](unsigned int objPicEnum,
                                          const char* pngName,
                                          Coord footprint,
                                          const char* buildSiteName,
                                          const char* label,
                                          bool normalizeTeamRed = false,
                                          bool normalizeLooseTeamPaint = false) {
        try {
            auto rwop = openTornieAsset(pngName, label);
            if(!rwop) {
                SDL_Log("GFXManager: %s sprite '%s' missing — using vanilla fallback", label, pngName);
                return;
            }
            auto raw = LoadPNG_RW(rwop.get());
            if(!raw) {
                SDL_Log("GFXManager: %s sprite '%s' failed to decode — using vanilla fallback", label, pngName);
                return;
            }
            if(raw->format->BitsPerPixel != 8 || !raw->format->palette) {
                SDL_Log("GFXManager: %s sprite '%s' is not 8-bit indexed, refusing it", label, pngName);
                return;
            }
            preserveOpaqueBlackIndex(raw.get());
            normalizeTransparentPaletteIndexes(raw.get());
            if(ibmPaletteLoaded) {
                if(auto remapped = remapIndexedSurfaceToPalette(raw.get(), ibmPalette.getSDLPalette())) {
                    raw = std::move(remapped);
                } else {
                    ibmPalette.applyToSurface(raw.get());
                }
                normalizeTransparentPaletteIndexes(raw.get());
            }
            // The authored Tech Center uses the eighth Harkonnen-range entry as decorative cyan,
            // not team paint. Move those pixels to an identical global entry
            // before the seven actual team shades are remapped.
            if(objPicEnum == ObjPic_TechCenter && raw->format->palette != nullptr) {
                SDL_Palette* palette = raw->format->palette;
                const int decorativeCyanIndex = PALCOLOR_HARKONNEN + 7;
                if(decorativeCyanIndex < palette->ncolors) {
                    const SDL_Color cyan = palette->colors[decorativeCyanIndex];
                    int safeCyanIndex = -1;
                    for(int index = 1;
                        index < PALCOLOR_HARKONNEN && index < palette->ncolors;
                        ++index) {
                        const SDL_Color candidate = palette->colors[index];
                        if(candidate.r == cyan.r && candidate.g == cyan.g && candidate.b == cyan.b) {
                            safeCyanIndex = index;
                            break;
                        }
                    }
                    if(safeCyanIndex >= 0) {
                        sdl2::surface_lock lock{raw.get()};
                        for(int y = 0; y < raw->h; ++y) {
                            Uint8* pixels = static_cast<Uint8*>(lock.pixels()) + y * raw->pitch;
                            for(int x = 0; x < raw->w; ++x) {
                                if(pixels[x] == decorativeCyanIndex) {
                                    pixels[x] = static_cast<Uint8>(safeCyanIndex);
                                }
                            }
                        }
                    }
                }
            }
            normalizeHouseColorRangesToHarkonnen(raw.get());
            const bool useAssetSpecificTeamPaint =
                objPicEnum == ObjPic_TechCenter || objPicEnum == ObjPic_Scoutpost;
            if(!useAssetSpecificTeamPaint) {
                if(normalizeTeamRed) {
                    normalizeHarkonnenTeamRed(raw.get());
                }
                if(normalizeLooseTeamPaint) {
                    normalizeLooseTeamPaintToHarkonnen(raw.get());
                }
            }

            const int frameWidth = footprint.x * D2_TILESIZE;
            const int frameHeight = footprint.y * D2_TILESIZE;
            const int rawFrameCount = getTornieFrameCount(raw.get(), frameWidth, frameHeight);
            const bool rawHasFullHorizontalAtlas =
                raw->w >= 4 * frameWidth
                && raw->h >= frameHeight
                && raw->h < 2 * frameHeight;
            if(rawFrameCount <= 0) {
                SDL_Log("GFXManager: %s sprite '%s' has an unsupported size", label, pngName);
                return;
            }
            logTornieStructureSurfaceDiagnostics("raw-normalized", label, raw.get(), frameWidth, frameHeight);

            sdl2::surface_ptr buildSite;
            if(buildSiteName != nullptr) {
                auto buildRwop = openTornieAsset(buildSiteName, label);
                buildSite = buildRwop ? LoadPNG_RW(buildRwop.get()) : nullptr;
                if(buildSite && buildSite->format->BitsPerPixel == 8 && buildSite->format->palette) {
                    preserveOpaqueBlackIndex(buildSite.get());
                    normalizeTransparentPaletteIndexes(buildSite.get());
                    if(ibmPaletteLoaded) {
                        if(auto remapped = remapIndexedSurfaceToPalette(buildSite.get(), ibmPalette.getSDLPalette())) {
                            buildSite = std::move(remapped);
                        } else {
                            ibmPalette.applyToSurface(buildSite.get());
                        }
                        normalizeTransparentPaletteIndexes(buildSite.get());
                    }
                    normalizeHouseColorRangesToHarkonnen(buildSite.get());
                    if(normalizeTeamRed) {
                        normalizeHarkonnenTeamRed(buildSite.get());
                    }
                    if(normalizeLooseTeamPaint) {
                        normalizeLooseTeamPaintToHarkonnen(buildSite.get());
                    }
                    logTornieStructureSurfaceDiagnostics("build-normalized", label, buildSite.get(), frameWidth, frameHeight);
                } else {
                    SDL_Log("GFXManager: %s build-site sprite '%s' is not 8-bit indexed, using active frame", label, buildSiteName);
                    buildSite.reset();
                }
            }

            SDL_Surface* buildSource = buildSite ? buildSite.get() : raw.get();
            const int buildFrameCount = getTornieFrameCount(buildSource, frameWidth, frameHeight);

            const int atlasFrameCount = objPicEnum == ObjPic_Worfinery ? 10 : (objPicEnum == ObjPic_LoveFactory ? 10 : 4);
            sdl2::surface_ptr atlas{ SDL_CreateRGBSurface(0, atlasFrameCount * frameWidth, frameHeight, 8, 0, 0, 0, 0) };
            if(!atlas || !atlas->format->palette) {
                return;
            }
            SDL_SetPaletteColors(atlas->format->palette,
                                 raw->format->palette->colors,
                                 0,
                                 raw->format->palette->ncolors);

            SDL_SetSurfaceBlendMode(raw.get(), SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(buildSource, SDL_BLENDMODE_NONE);
            SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_NONE);
            SDL_SetColorKey(raw.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(buildSource, SDL_TRUE, PALCOLOR_TRANSPARENT);
            SDL_FillRect(atlas.get(), nullptr, PALCOLOR_TRANSPARENT);
            SDL_SetColorKey(atlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

            SDL_Rect srcTop = getTornieFrameRect(raw.get(), frameWidth, frameHeight, rawHasFullHorizontalAtlas ? 2 : 0);
            SDL_Rect srcBottom = getTornieFrameRect(raw.get(), frameWidth, frameHeight,
                                                    rawHasFullHorizontalAtlas ? 3 : (rawFrameCount > 1 ? 1 : 0));
            SDL_Rect buildTop = getTornieFrameRect(buildSource, frameWidth, frameHeight, 0);
            SDL_Rect buildBottom = getTornieFrameRect(buildSource, frameWidth, frameHeight, buildFrameCount > 1 ? 1 : 0);

            const bool protectOpaqueBlack =
                raw->format->palette->ncolors > PALCOLOR_BLACK
                && buildSource->format->palette->ncolors > PALCOLOR_BLACK
                && atlas->format->palette->ncolors > PALCOLOR_BLACK;
            const SDL_Color rawBlack = protectOpaqueBlack ? raw->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            const SDL_Color buildBlack = protectOpaqueBlack ? buildSource->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            const SDL_Color atlasBlack = protectOpaqueBlack ? atlas->format->palette->colors[PALCOLOR_BLACK] : SDL_Color{};
            if(protectOpaqueBlack) {
                raw->format->palette->colors[PALCOLOR_BLACK].g = 1;
                buildSource->format->palette->colors[PALCOLOR_BLACK].g = 1;
                atlas->format->palette->colors[PALCOLOR_BLACK].g = 1;
            }

            auto blitFrame = [&](SDL_Surface* source, SDL_Rect* src, int frame) {
                if(!source || src->w <= 0 || src->h <= 0) {
                    return;
                }
                SDL_Rect dst{frame * frameWidth + (frameWidth - src->w) / 2, frameHeight - src->h, src->w, src->h};
                SDL_BlitSurface(source, src, atlas.get(), &dst);
            };

            blitFrame(buildSource, &buildTop, 0);
            blitFrame(buildSource, &buildBottom, 1);
            if(objPicEnum == ObjPic_Worfinery) {
                // Vanilla Refinery layout: 2-7 approach/idle and 8-9 loaded.
                // The supplied vertical sheet contains five normal frames and
                // two loaded frames; repeat frame 0 to complete the six-frame range.
                const int sevenFrameMap[8] = { 0, 1, 2, 3, 4, 0, 5, 6 };
                for(int atlasFrame = 2; atlasFrame <= 9; ++atlasFrame) {
                    const int activeIndex = atlasFrame - 2;
                    const int sourceIndex = rawFrameCount >= 8
                        ? activeIndex
                        : (rawFrameCount == 7 ? sevenFrameMap[activeIndex] : activeIndex % rawFrameCount);
                    SDL_Rect activeFrame = getTornieFrameRect(raw.get(), frameWidth, frameHeight, sourceIndex);
                    blitFrame(raw.get(), &activeFrame, atlasFrame);
                }
            } else if(objPicEnum == ObjPic_LoveFactory) {
                for(int atlasFrame = 2; atlasFrame <= 9; ++atlasFrame) {
                    SDL_Rect activeFrame = getTornieFrameRect(raw.get(), frameWidth, frameHeight,
                                                                 (atlasFrame - 2) % rawFrameCount);
                    blitFrame(raw.get(), &activeFrame, atlasFrame);
                }
            } else {
                blitFrame(raw.get(), &srcTop, 2);
                blitFrame(raw.get(), &srcBottom, 3);
            }
            logTornieStructureSurfaceDiagnostics("atlas-built", label, atlas.get(), frameWidth, frameHeight);

            if(protectOpaqueBlack) {
                raw->format->palette->colors[PALCOLOR_BLACK] = rawBlack;
                buildSource->format->palette->colors[PALCOLOR_BLACK] = buildBlack;
                atlas->format->palette->colors[PALCOLOR_BLACK] = atlasBlack;
            }

            normalizeTornieStructureTeamPaintToHarkonnen(atlas.get(), objPicEnum);
            normalizeTransparentPaletteIndexes(atlas.get());
            logTornieStructureSurfaceDiagnostics("atlas-final", label, atlas.get(), frameWidth, frameHeight);

            objPic[objPicEnum][HOUSE_HARKONNEN][0] = std::move(atlas);
            // Generate zoom levels 1 and 2
            if(objPic[objPicEnum][HOUSE_HARKONNEN][0]) {
                objPic[objPicEnum][HOUSE_HARKONNEN][1] =
                    scaleSurfaceNearest(objPic[objPicEnum][HOUSE_HARKONNEN][0].get(), 2);
                if(objPic[objPicEnum][HOUSE_HARKONNEN][1]) {
                    objPic[objPicEnum][HOUSE_HARKONNEN][2] =
                        scaleSurfaceNearest(objPic[objPicEnum][HOUSE_HARKONNEN][0].get(), 3);
                }
            }
            SDL_Log("GFXManager: %s sprite '%s' loaded (all zoom levels)", label, pngName);
        } catch(std::exception& e) {
            SDL_Log("GFXManager: %s — %s sprite load failed, using vanilla fallback", e.what(), label);
        }
    };
    loadTornieStructureSprite(ObjPic_Worfinery,  "BUILDING_3x2_worfinery.png",  Coord(3,2), "BUILDING_3x2_prebuild.png", "Worfinery", true, true);
    loadTornieStructureSprite(ObjPic_TechCenter, "TechCenter.png", Coord(3,2), "BUILDING_3x2_prebuild.png", "TechCenter", true);
    loadTornieStructureSprite(ObjPic_Scoutpost,  "Scoutpost.png",  Coord(1,1), "BUILDING_1x1_prebuild.png", "Scoutpost", true);
    loadTornieStructureSprite(ObjPic_LoveFactory, "LoveFactory.png", Coord(2,3), nullptr, "LoveFactory", true, true);
    loadTornieStructureSprite(ObjPic_ChaosFactory, "ChaosFactory.png", Coord(3,2), "BUILDING_3x2_prebuild.png", "ChaosFactory", true, true);

    // Advanced Windtraps are installed per colour slot before their indexed
    // source atlases are expanded into the 10x7 RGBA animation sheets.
    // v1.0.173-compatible rendering for the remaining Tornie structures.
    // Resolve every house palette while indexed, then freeze the final colors
    // into RGBA before SDL texture creation.
    auto installTornieStructureTruecolorSlots = [&](unsigned int objPicEnum, const char* label) {
        SDL_Surface* base = objPic[objPicEnum][HOUSE_HARKONNEN][0].get();
        if(base == nullptr || base->format == nullptr || base->format->BytesPerPixel != 1
           || base->format->palette == nullptr) {
            SDL_Log("TornieGFX: v173-rgba %s skipped (base atlas is not indexed)", label);
            return;
        }

        // Preserve the indexed base while every visual colour slot is derived.
        sdl2::surface_ptr indexedBase = copySurface(base);
        if(!indexedBase) {
            SDL_Log("TornieGFX: v173-rgba %s skipped (base copy failed)", label);
            return;
        }

        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            sdl2::surface_ptr indexed;
            if(colorSlot == HOUSE_HARKONNEN) {
                indexed = copySurface(indexedBase.get());
            } else {
                indexed = mapSurfaceColorRange(indexedBase.get(),
                                               PALCOLOR_HARKONNEN,
                                               getVisualRemapPaletteIndex(colorSlot));
                if(indexed) {
                    applyCustomVisualColorRamp(indexed.get(), colorSlot);
                    if(objPicEnum != ObjPic_TechCenter) {
                        applyRebelsTint(indexed.get(), colorSlot);
                    }
                }
            }

            if(!indexed) {
                SDL_Log("TornieGFX: v173-rgba %s colorSlot=%d remap failed", label, colorSlot);
                continue;
            }

            normalizeTransparentPaletteIndexes(indexed.get());
            SDL_SetColorKey(indexed.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);

            auto rgba = convertTornieIndexedSurfaceToRGBA(indexed.get(), label, colorSlot, 0, false);
            if(!rgba) {
                SDL_Log("TornieGFX: v173-rgba %s colorSlot=%d conversion failed", label, colorSlot);
                continue;
            }

            SDL_SetColorKey(rgba.get(), SDL_FALSE, 0);
            SDL_SetSurfaceBlendMode(rgba.get(), SDL_BLENDMODE_BLEND);

            objPic[objPicEnum][colorSlot][0] = std::move(rgba);
            objPic[objPicEnum][colorSlot][1] =
                scaleSurfaceNearest(objPic[objPicEnum][colorSlot][0].get(), 2);
            objPic[objPicEnum][colorSlot][2] =
                scaleSurfaceNearest(objPic[objPicEnum][colorSlot][0].get(), 3);

            if(objPic[objPicEnum][colorSlot][1]) {
                SDL_SetSurfaceBlendMode(objPic[objPicEnum][colorSlot][1].get(), SDL_BLENDMODE_BLEND);
            }
            if(objPic[objPicEnum][colorSlot][2]) {
                SDL_SetSurfaceBlendMode(objPic[objPicEnum][colorSlot][2].get(), SDL_BLENDMODE_BLEND);
            }
        }

        SDL_Log("TornieGFX: v173-rgba %s installed for %d visual colour slots",
                label, NUM_HOUSE_COLOR_SLOTS);
    };

    installTornieStructureTruecolorSlots(ObjPic_Worfinery,           "Worfinery");
    installTornieStructureTruecolorSlots(ObjPic_TechCenter,          "TechCenter");
    installTornieStructureTruecolorSlots(ObjPic_Scoutpost,           "Scoutpost");
    installTornieStructureTruecolorSlots(ObjPic_LoveFactory,          "LoveFactory");
    installTornieStructureTruecolorSlots(ObjPic_ChaosFactory,         "ChaosFactory");
    for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
        for(unsigned int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
            if(objPic[ObjPic_Scoutpost][colorSlot][zoom]) {
                scoutpostBaseGraphics[colorSlot][zoom] =
                    copySurface(objPic[ObjPic_Scoutpost][colorSlot][zoom].get());
                objPic[ObjPic_Flamepost][colorSlot][zoom] =
                    copySurface(objPic[ObjPic_Scoutpost][colorSlot][zoom].get());
                objPic[ObjPic_Chemipost][colorSlot][zoom] =
                    copySurface(objPic[ObjPic_Scoutpost][colorSlot][zoom].get());
            }
            if(objPic[ObjPic_ChaosFactory][colorSlot][zoom]) {
                chaosFactoryBaseGraphics[colorSlot][zoom] =
                    copySurface(objPic[ObjPic_ChaosFactory][colorSlot][zoom].get());
            }
        }
    }

    reloadModDependentObjectGraphics();
    SDL_Color fogTransparent = { 0, 0, 0, 96};
    SDL_SetPaletteColors(objPic[ObjPic_Terrain_HiddenFog][HOUSE_HARKONNEN][0]->format->palette, &fogTransparent, PALCOLOR_BLACK, 1);

    loadCompactObjPicOverrides();

    // scale obj pics and apply color key
    for(int id = 0; id < NUM_OBJPICS; id++) {
        // Some built-in sprites and mod overrides are 32-bit RGBA with per-pixel alpha.
        // SDL_SetColorKey with PALCOLOR_TRANSPARENT (== 0) on them would
        // treat any pixel with raw value 0 as color-keyed, overriding the
        // alpha channel and producing black squares where transparent
        // pixels have non-zero RGB.  Skip color keying for these IDs.
        const bool isTruecolorSprite = (objPic[id][HOUSE_HARKONNEN][0] != nullptr
                                     && objPic[id][HOUSE_HARKONNEN][0]->format->BytesPerPixel >= 3)
                                     || (id == ObjPic_ZoneResidential
                                     || id == ObjPic_ZoneCommercial
                                     || id == ObjPic_ZoneIndustrial
                                     || id == ObjPic_CityRoad
                                     || id == ObjPic_NuclearPlant
                                     || id == ObjPic_PoliceStation
                                     || id == ObjPic_Stadium
                                     || id == ObjPic_Airport
                                     || id == ObjPic_Star);

        for(int h = 0; h < (int) NUM_HOUSES; h++) {
            if(objPic[id][h][0] != nullptr) {
                const bool isCurrentTruecolorSprite = isTruecolorSprite || objPic[id][h][0]->format->BytesPerPixel != 1;
                if(objPic[id][h][1] == nullptr) {
                    objPic[id][h][1] = generateDoubledObjPic(id, h);
                }
                if(!isCurrentTruecolorSprite) {
                    SDL_SetColorKey(objPic[id][h][1].get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                }

                if(objPic[id][h][2] == nullptr) {
                    objPic[id][h][2] = generateTripledObjPic(id, h);
                }
                if(!isCurrentTruecolorSprite) {
                    SDL_SetColorKey(objPic[id][h][2].get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                }

                if(!isCurrentTruecolorSprite) {
                    SDL_SetColorKey(objPic[id][h][0].get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                }
            }
        }
    }

    loadDuneCitySkinOverrides();

    objPic[ObjPic_CarryallShadow][HOUSE_HARKONNEN][0] = createShadowSurface(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0].get());
    objPic[ObjPic_CarryallShadow][HOUSE_HARKONNEN][1] = createShadowSurface(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][1].get());
    objPic[ObjPic_CarryallShadow][HOUSE_HARKONNEN][2] = createShadowSurface(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][2].get());
    objPic[ObjPic_FrigateShadow][HOUSE_HARKONNEN][0] = createShadowSurface(objPic[ObjPic_Frigate][HOUSE_HARKONNEN][0].get());
    objPic[ObjPic_FrigateShadow][HOUSE_HARKONNEN][1] = createShadowSurface(objPic[ObjPic_Frigate][HOUSE_HARKONNEN][1].get());
    objPic[ObjPic_FrigateShadow][HOUSE_HARKONNEN][2] = createShadowSurface(objPic[ObjPic_Frigate][HOUSE_HARKONNEN][2].get());
    objPic[ObjPic_OrnithopterShadow][HOUSE_HARKONNEN][0] = createShadowSurface(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][0].get());
    objPic[ObjPic_OrnithopterShadow][HOUSE_HARKONNEN][1] = createShadowSurface(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][1].get());
    objPic[ObjPic_OrnithopterShadow][HOUSE_HARKONNEN][2] = createShadowSurface(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][2].get());

    // load small detail pics
    smallDetailPicTex[Picture_Barracks] = extractSmallDetailPic("BARRAC.WSA");
    smallDetailPicTex[Picture_ConstructionYard] = extractSmallDetailPic("CONSTRUC.WSA");
    smallDetailPicTex[Picture_Carryall] = extractSmallDetailPic("CARRYALL.WSA");
    smallDetailPicTex[Picture_Devastator] = extractSmallDetailPic("HARKTANK.WSA");
    smallDetailPicTex[Picture_Deviator] = extractSmallDetailPic("ORDRTANK.WSA");
    smallDetailPicTex[Picture_DeathHand] = extractSmallDetailPic("GOLD-BB.WSA");
    smallDetailPicTex[Picture_Fremen] = extractSmallDetailPic("FREMEN.WSA");
    if(pFileManager->exists("FRIGATE.WSA")) {
        smallDetailPicTex[Picture_Frigate] = extractSmallDetailPic("FRIGATE.WSA");
    } else {
        // US-Version 1.07 does not contain FRIGATE.WSA
        // We replace it with the starport
        smallDetailPicTex[Picture_Frigate] = extractSmallDetailPic("STARPORT.WSA");
    }
    smallDetailPicTex[Picture_GunTurret] = extractSmallDetailPic("TURRET.WSA");
    smallDetailPicTex[Picture_Harvester] = extractSmallDetailPic("HARVEST.WSA");
    smallDetailPicTex[Picture_HeavyFactory] = extractSmallDetailPic("HVYFTRY.WSA");
    smallDetailPicTex[Picture_HighTechFactory] = extractSmallDetailPic("HITCFTRY.WSA");
    smallDetailPicTex[Picture_Soldier] = extractSmallDetailPic("INFANTRY.WSA");
    smallDetailPicTex[Picture_IX] = extractSmallDetailPic("IX.WSA");
    smallDetailPicTex[Picture_Launcher] = extractSmallDetailPic("RTANK.WSA");
    smallDetailPicTex[Picture_LightFactory] = extractSmallDetailPic("LITEFTRY.WSA");
    smallDetailPicTex[Picture_MCV] = extractSmallDetailPic("MCV.WSA");
    smallDetailPicTex[Picture_Ornithopter] = extractSmallDetailPic("ORNI.WSA");
    smallDetailPicTex[Picture_Palace] = extractSmallDetailPic("PALACE.WSA");
    smallDetailPicTex[Picture_Quad] = extractSmallDetailPic("QUAD.WSA");
    smallDetailPicTex[Picture_Radar] = extractSmallDetailPic("HEADQRTS.WSA");
    smallDetailPicTex[Picture_RaiderTrike] = extractSmallDetailPic("OTRIKE.WSA");
    smallDetailPicTex[Picture_Refinery] = extractSmallDetailPic("REFINERY.WSA");
    smallDetailPicTex[Picture_RepairYard] = extractSmallDetailPic("REPAIR.WSA");
    smallDetailPicTex[Picture_RocketTurret] = extractSmallDetailPic("RTURRET.WSA");
    smallDetailPicTex[Picture_Saboteur] = extractSmallDetailPic("SABOTURE.WSA");
    smallDetailPicTex[Picture_Sandworm] = extractSmallDetailPic("WORM.WSA");
    smallDetailPicTex[Picture_Sardaukar] = extractSmallDetailPic("SARDUKAR.WSA");
    smallDetailPicTex[Picture_SiegeTank] = extractSmallDetailPic("HTANK.WSA");
    smallDetailPicTex[Picture_Silo] = extractSmallDetailPic("STORAGE.WSA");
    smallDetailPicTex[Picture_Slab1] = extractSmallDetailPic("SLAB.WSA");
    smallDetailPicTex[Picture_Slab4] = extractSmallDetailPic("4SLAB.WSA");
    smallDetailPicTex[Picture_SonicTank] = extractSmallDetailPic("STANK.WSA");
    smallDetailPicTex[Picture_Special]  = nullptr;
    smallDetailPicTex[Picture_StarPort] = extractSmallDetailPic("STARPORT.WSA");
    smallDetailPicTex[Picture_Tank] = extractSmallDetailPic("LTANK.WSA");
    smallDetailPicTex[Picture_Trike] = extractSmallDetailPic("TRIKE.WSA");
    smallDetailPicTex[Picture_Trooper] = extractSmallDetailPic("HYINFY.WSA");
    smallDetailPicTex[Picture_Wall] = extractSmallDetailPic("WALL.WSA");
    smallDetailPicTex[Picture_WindTrap] = extractSmallDetailPic("WINDTRAP.WSA");
    smallDetailPicTex[Picture_WOR] = extractSmallDetailPic("WOR.WSA");

    // DuneCity zone build-menu icons — scale the imported zone sprite down
    // to 91x55 for the small detail pic.  Falls back to SLAB.WSA if the
    // zone surface was not loaded.
    {
        auto makeZoneDetailPic = [&](int objPicId) -> sdl2::texture_ptr {
            SDL_Surface* zoneSrc = objPic[objPicId][HOUSE_HARKONNEN][0].get();
            if (!zoneSrc) return extractSmallDetailPic("SLAB.WSA");

            // Representative inhabited model; phase zero, value tier zero.
            const int cellSize = 2 * D2_TILESIZE;
            const int model = objPicId == ObjPic_ZoneResidential ? 5 : 3;
            SDL_Rect srcRect = { model * cellSize, 0, cellSize, cellSize };

            // Create a transparent 91x55 canvas.  Reserve gutters so the
            // icon doesn't overlap the top-left lattice overlay (13x13 at
            // offset 2,2) or the bottom-left price text (~12px tall).
            sdl2::surface_ptr canvas{ SDL_CreateRGBSurface(0, 91, 55,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (!canvas) return extractSmallDetailPic("SLAB.WSA");
            SDL_FillRect(canvas.get(), nullptr, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));

            const int leftGutter  = 16;  // clear the 13px lattice + padding
            const int bottomGutter = 14; // clear the ~12px price text
            const int topMargin   = 2;
            const int rightMargin = 4;
            const int usableW = 91 - leftGutter - rightMargin;  // 71
            const int usableH = 55 - topMargin - bottomGutter;  // 39
            float scale = std::min(static_cast<float>(usableW) / cellSize,
                                   static_cast<float>(usableH) / cellSize);
            // Cap at 1.5x to avoid over-enlarging small sprites.
            if (scale > 1.5f) scale = 1.5f;
            int destW = static_cast<int>(cellSize * scale);
            int destH = static_cast<int>(cellSize * scale);
            // Center within the usable area (right of lattice, above price).
            int destX = leftGutter + (usableW - destW) / 2;
            int destY = topMargin  + (usableH - destH) / 2;
            SDL_Rect destRect = { destX, destY, destW, destH };

            SDL_BlendMode prevMode;
            SDL_GetSurfaceBlendMode(zoneSrc, &prevMode);
            SDL_SetSurfaceBlendMode(zoneSrc, SDL_BLENDMODE_NONE);
            SDL_BlitScaled(zoneSrc, &srcRect, canvas.get(), &destRect);
            SDL_SetSurfaceBlendMode(zoneSrc, prevMode);

            auto tex = convertSurfaceToTexture(canvas.get());
            if (tex) SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
            return tex ? std::move(tex) : extractSmallDetailPic("SLAB.WSA");
        };
        smallDetailPicTex[Picture_ZoneResidential] = makeZoneDetailPic(ObjPic_ZoneResidential);
        smallDetailPicTex[Picture_ZoneCommercial]  = makeZoneDetailPic(ObjPic_ZoneCommercial);
        smallDetailPicTex[Picture_ZoneIndustrial]  = makeZoneDetailPic(ObjPic_ZoneIndustrial);

        // Road build-menu icon: use the cross-intersection frame (mask=15)
        // of the Micropolis road atlas so the icon visually reads as a road,
        // not as a slab of concrete (the previous SLAB.WSA placeholder).
        SDL_Surface* roadAtlas = objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][0].get();
        if (roadAtlas) {
            const int crossFrame = 15;  // four-way intersection — most "road"-looking glyph
            sdl2::surface_ptr crossTile = getSubPicture(roadAtlas, crossFrame * D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
            if (crossTile) {
                sdl2::surface_ptr canvas{ SDL_CreateRGBSurface(0, 91, 55,
                    SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
                if (canvas) {
                    SDL_FillRect(canvas.get(), nullptr, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));
                    const int leftGutter = 16, bottomGutter = 14, topMargin = 2, rightMargin = 4;
                    const int usableW = 91 - leftGutter - rightMargin;
                    const int usableH = 55 - topMargin - bottomGutter;
                    // Scale up the 16x16 tile to make it readable at sidebar size.
                    float scale = std::min(static_cast<float>(usableW) / crossTile->w,
                                           static_cast<float>(usableH) / crossTile->h);
                    if (scale > 2.0f) scale = 2.0f;
                    int destW = static_cast<int>(crossTile->w * scale);
                    int destH = static_cast<int>(crossTile->h * scale);
                    int destX = leftGutter + (usableW - destW) / 2;
                    int destY = topMargin  + (usableH - destH) / 2;
                    SDL_Rect destRect = { destX, destY, destW, destH };
                    SDL_SetSurfaceBlendMode(crossTile.get(), SDL_BLENDMODE_NONE);
                    SDL_BlitScaled(crossTile.get(), nullptr, canvas.get(), &destRect);
                    auto tex = convertSurfaceToTexture(canvas.get());
                    if (tex) {
                        SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
                        smallDetailPicTex[Picture_Road] = std::move(tex);
                    }
                }
            }
        }
    }
    if (!smallDetailPicTex[Picture_Road]) {
        smallDetailPicTex[Picture_Road]        = extractSmallDetailPic("SLAB.WSA");
    }
    smallDetailPicTex[Picture_PowerLine]       = extractSmallDetailPic("SLAB.WSA");

    // Nuclear plant, police, stadium, airport build-menu icons — pull
    // first frame from their Micropolis atlases so they're recognizable.
    {
        auto makeStructDetailPic = [&](int objPicID, int frameW, int frameH) -> sdl2::texture_ptr {
            SDL_Surface* src = objPic[objPicID][HOUSE_HARKONNEN][0].get();
            if (!src) return sdl2::texture_ptr{};
            sdl2::surface_ptr cell = getSubPicture(src, 0, 0, frameW, frameH);
            if (!cell) return sdl2::texture_ptr{};
            sdl2::surface_ptr canvas{ SDL_CreateRGBSurface(0, 91, 55,
                SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
            if (!canvas) return sdl2::texture_ptr{};
            SDL_FillRect(canvas.get(), nullptr, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));
            const int leftGutter = 16, bottomGutter = 14, topMargin = 2, rightMargin = 4;
            const int usableW = 91 - leftGutter - rightMargin;
            const int usableH = 55 - topMargin - bottomGutter;
            float scale = std::min(static_cast<float>(usableW) / frameW,
                                   static_cast<float>(usableH) / frameH);
            if (scale > 1.5f) scale = 1.5f;
            int destW = static_cast<int>(frameW * scale);
            int destH = static_cast<int>(frameH * scale);
            int destX = leftGutter + (usableW - destW) / 2;
            int destY = topMargin  + (usableH - destH) / 2;
            SDL_Rect destRect = { destX, destY, destW, destH };
            SDL_SetSurfaceBlendMode(cell.get(), SDL_BLENDMODE_NONE);
            SDL_BlitScaled(cell.get(), nullptr, canvas.get(), &destRect);
            auto tex = convertSurfaceToTexture(canvas.get());
            if (tex) SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
            return tex;
        };
        auto nucTex = makeStructDetailPic(ObjPic_NuclearPlant, 3 * D2_TILESIZE, 3 * D2_TILESIZE);
        if (nucTex) smallDetailPicTex[Picture_NuclearPlant] = std::move(nucTex);
        else        smallDetailPicTex[Picture_NuclearPlant] = extractSmallDetailPic("HTEC.WSA");

        auto polTex = makeStructDetailPic(ObjPic_PoliceStation, 2 * D2_TILESIZE, 2 * D2_TILESIZE);
        if (polTex) smallDetailPicTex[Picture_PoliceStation] = std::move(polTex);
        else        smallDetailPicTex[Picture_PoliceStation] = extractSmallDetailPic("BARRAC.WSA");

        auto stadTex = makeStructDetailPic(ObjPic_Stadium, 3 * D2_TILESIZE, 3 * D2_TILESIZE);
        if (stadTex) smallDetailPicTex[Picture_Stadium] = std::move(stadTex);
        else         smallDetailPicTex[Picture_Stadium] = extractSmallDetailPic("PALACE.WSA");

        auto airTex = makeStructDetailPic(ObjPic_Airport, 3 * D2_TILESIZE, 3 * D2_TILESIZE);
        if (airTex) smallDetailPicTex[Picture_Airport] = std::move(airTex);
        else        smallDetailPicTex[Picture_Airport] = extractSmallDetailPic("STARPORT.WSA");
    }

    // Per-house DuneCity Dune2 portraits. Until an authored Icon Sprite is
    // installed, derive a readable portrait from that house's accepted
    // Compact. An icon.png beside zone.ini/building.ini takes precedence and
    // is the stable mount point used by the future ~dune2config Icon Sprite
    // workflow. The regular smallDetailPicTex remains the SimCity fallback.
    {
        constexpr int portraitW = 91;
        constexpr int portraitH = 55;
        std::array<std::array<std::filesystem::path, NUM_HOUSES>, NUM_SMALLDETAILPICS> authoredIcons{};
        std::array<std::array<std::filesystem::path, NUM_HOUSES>, NUM_SMALLDETAILPICS> representativeCompacts{};

        const auto rememberIcon = [&](const std::filesystem::path& manifestPath,
                                      int picture, int house) {
            if(picture < 0 || picture >= NUM_SMALLDETAILPICS
               || house < 0 || house >= static_cast<int>(NUM_HOUSES)) return;
            const auto iconPath = manifestPath.parent_path() / "icon.png";
            if(std::filesystem::is_regular_file(iconPath)) {
                authoredIcons[picture][house] = iconPath;
            }
        };

        if(ModManager::instance().isInitialized()) {
            const auto skinRoot = std::filesystem::path(ModManager::instance().getModPath(ModManager::instance().getActiveModName()))
                / "graphics_skins" / "Dune2";
            const auto zonesRoot = skinRoot / "zones";
            if(std::filesystem::is_directory(zonesRoot)) {
                for(const auto& entry : std::filesystem::recursive_directory_iterator(zonesRoot)) {
                    if(!entry.is_regular_file() || entry.path().filename() != "zone.ini") continue;
                    INIFile manifest(entry.path().string());
                    const int itemID = manifest.getIntValue("Zone", "ItemID", -1);
                    const int house = manifest.getIntValue("Zone", "HouseID", -1);
                    const int picture = itemID == 20 ? Picture_ZoneResidential
                        : (itemID == 21 ? Picture_ZoneCommercial
                        : (itemID == 22 ? Picture_ZoneIndustrial : -1));
                    rememberIcon(entry.path(), picture, house);
                    if(picture >= 0 && house >= 0 && house < static_cast<int>(NUM_HOUSES)) {
                        std::string compact = manifest.getStringValue("Cell.2.0.Idle", "Atlas.0", "");
                        if(compact.empty()) {
                            compact = manifest.getStringValue("Cell.0.0.Idle", "Atlas.0", "");
                        }
                        const auto compactPath = entry.path().parent_path() / compact;
                        if(!compact.empty() && std::filesystem::is_regular_file(compactPath)) {
                            representativeCompacts[picture][house] = compactPath;
                        }
                    }
                }
            }
            const auto buildingsRoot = skinRoot / "buildings";
            if(std::filesystem::is_directory(buildingsRoot)) {
                for(const auto& entry : std::filesystem::recursive_directory_iterator(buildingsRoot)) {
                    if(!entry.is_regular_file() || entry.path().filename() != "building.ini") continue;
                    INIFile manifest(entry.path().string());
                    const std::string name = manifest.getStringValue("Building", "ObjPic", "");
                    const int house = manifest.getIntValue("Building", "HouseID", -1);
                    const int picture = name == "NuclearPlant" ? Picture_NuclearPlant
                        : (name == "PoliceStation" ? Picture_PoliceStation
                        : (name == "Stadium" ? Picture_Stadium
                        : (name == "Airport" ? Picture_Airport : -1)));
                    rememberIcon(entry.path(), picture, house);
                }
            }
        }

        const auto createPortrait = [&](SDL_Surface* atlas, const SDL_Rect& source,
                                        const std::filesystem::path& authored,
                                        const std::filesystem::path& compact) -> sdl2::texture_ptr {
            if(!authored.empty()) {
                auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(authored.string().c_str(), "rb") };
                auto raw = rwops ? LoadPNG_RW(rwops.get()) : sdl2::surface_ptr{};
                if(raw) {
                    auto fitted = (raw->w == portraitW && raw->h == portraitH)
                        ? copySurface(raw.get()) : resizeSurfaceNearest(raw.get(), portraitW, portraitH);
                    if(fitted) {
                        SDL_SetColorKey(fitted.get(), SDL_FALSE, 0);
                        SDL_SetSurfaceBlendMode(fitted.get(), SDL_BLENDMODE_BLEND);
                        auto texture = convertSurfaceToTexture(fitted.get());
                        if(texture) SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
                        return texture;
                    }
                }
            }
            sdl2::surface_ptr compactSurface;
            SDL_Surface* portraitSource = atlas;
            SDL_Rect portraitRect = source;
            if(!compact.empty()) {
                auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(compact.string().c_str(), "rb") };
                compactSurface = rwops ? LoadPNG_RW(rwops.get()) : sdl2::surface_ptr{};
                if(compactSurface) {
                    SDL_SetColorKey(compactSurface.get(), SDL_FALSE, 0);
                    SDL_SetSurfaceBlendMode(compactSurface.get(), SDL_BLENDMODE_NONE);
                    portraitSource = compactSurface.get();
                    portraitRect = SDL_Rect{0, 0, compactSurface->w, compactSurface->h};
                }
            }
            if(!portraitSource || portraitRect.w <= 0 || portraitRect.h <= 0) return {};
            auto canvas = sdl2::surface_ptr{ SDL_CreateRGBSurfaceWithFormat(
                0, portraitW, portraitH, 32, SDL_PIXELFORMAT_RGBA32) };
            if(!canvas) return {};
            SDL_FillRect(canvas.get(), nullptr, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));
            const int leftGutter = 16, bottomGutter = 14, topMargin = 2, rightMargin = 4;
            const int usableW = portraitW - leftGutter - rightMargin;
            const int usableH = portraitH - topMargin - bottomGutter;
            const double scale = std::min(1.5, std::min(
                static_cast<double>(usableW) / portraitRect.w,
                static_cast<double>(usableH) / portraitRect.h));
            SDL_Rect destination{
                leftGutter + (usableW - static_cast<int>(portraitRect.w * scale)) / 2,
                topMargin + (usableH - static_cast<int>(portraitRect.h * scale)) / 2,
                static_cast<int>(portraitRect.w * scale), static_cast<int>(portraitRect.h * scale)};
            SDL_BlendMode oldMode;
            SDL_GetSurfaceBlendMode(portraitSource, &oldMode);
            SDL_SetSurfaceBlendMode(portraitSource, SDL_BLENDMODE_NONE);
            SDL_BlitScaled(portraitSource, &portraitRect, canvas.get(), &destination);
            SDL_SetSurfaceBlendMode(portraitSource, oldMode);
            auto texture = convertSurfaceToTexture(canvas.get());
            if(texture) SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
            return texture;
        };

        static const std::array<int, 3> zonePictures{
            Picture_ZoneResidential, Picture_ZoneCommercial, Picture_ZoneIndustrial};
        static const std::array<unsigned int, 3> zoneObjectPictures{
            ObjPic_ZoneResidential, ObjPic_ZoneCommercial, ObjPic_ZoneIndustrial};
        static const std::array<int, 6> buildingPictures{
            Picture_NuclearPlant, Picture_PoliceStation, Picture_Stadium,
            Picture_Airport, -1, -1};
        for(int house = 0; house < static_cast<int>(NUM_HOUSES); ++house) {
            for(std::size_t zone = 0; zone < zonePictures.size(); ++zone) {
                SDL_Surface* atlas = duneCityDune2ZonePic[zone][house][0].get();
                const int columns = std::max(1, objPicTiles[zoneObjectPictures[zone]].x);
                const int rows = std::max(1, objPicTiles[zoneObjectPictures[zone]].y);
                if(!atlas && representativeCompacts[zonePictures[zone]][house].empty()) continue;
                const int cellW = atlas ? atlas->w / columns : 0;
                const int cellH = atlas ? atlas->h / rows : 0;
                const int representativeDensity = std::min(2, columns - 1);
                SDL_Rect source{representativeDensity * cellW, 0, cellW, cellH};
                const int picture = zonePictures[zone];
                duneCityDune2DetailPicTex[picture][house] = createPortrait(
                    atlas, source, authoredIcons[picture][house],
                    representativeCompacts[picture][house]);
            }
            for(std::size_t building = 0; building < buildingPictures.size(); ++building) {
                const int picture = buildingPictures[building];
                if(picture < 0) continue;
                SDL_Surface* atlas = duneCityDune2BuildingPic[building][house][0].get();
                if(!atlas) continue;
                static const std::array<unsigned int, 6> objectPictures{
                    ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
                    ObjPic_Airport, ObjPic_Hospital, ObjPic_Church};
                const int columns = std::max(1, objPicTiles[objectPictures[building]].x);
                const int rows = std::max(1, objPicTiles[objectPictures[building]].y);
                SDL_Rect source{0, 0, atlas->w / columns, atlas->h / rows};
                duneCityDune2DetailPicTex[picture][house] = createPortrait(
                    atlas, source, authoredIcons[picture][house], {});
            }
        }
    }

    // DuneCity 1.0.506: Tornie unit portraits. The mod ships 91x55 PNG icons
    // (RocketTrikeIcon.png, FlameTankIcon.png, EliteLauncherIcon.png,
    // EliteSiegeTankIcon.png) as WSA replacements — simpler than authoring
    // 4 new WSA animations. Load via LoadPNG_RW; if missing, fall back to a
    // related vanilla portrait so the build/sidebar still has an icon.
    {
        constexpr int SmallDetailPicWidth = 91;
        constexpr int SmallDetailPicHeight = 55;

        auto loadIcon = [&](int pictureIndex, const std::string& pngName,
                            const char* fallbackWsa, bool addBlueStar = false) {
            try {
                if(auto iconAsset = openTornieAsset(pngName.c_str(), "portrait")) {
                    auto raw = LoadPNG_RW(iconAsset.get());
                    if(raw) {
                        preserveOpaqueBlackIndex(raw.get());
                        normalizeTransparentPaletteIndexes(raw.get());
                        if(raw->w != SmallDetailPicWidth || raw->h != SmallDetailPicHeight) {
                            auto resized = resizeSurfaceNearest(raw.get(), SmallDetailPicWidth, SmallDetailPicHeight);
                            if(resized) {
                                SDL_Log("GFXManager: resized portrait %s from %dx%d to %dx%d",
                                        pngName.c_str(), raw->w, raw->h, SmallDetailPicWidth, SmallDetailPicHeight);
                                raw = std::move(resized);
                            }
                        }
                        if(addBlueStar) {
                            sdl2::surface_ptr rgba{
                                SDL_ConvertSurfaceFormat(raw.get(), SDL_PIXELFORMAT_RGBA32, 0)
                            };
                            if(rgba) {
                                raw = std::move(rgba);
                                SDL_SetColorKey(raw.get(), SDL_FALSE, 0);
                                SDL_SetSurfaceBlendMode(raw.get(), SDL_BLENDMODE_BLEND);
                                static const char* starRows[] = {
                                    "....#....",
                                    "...###...",
                                    "...###...",
                                    "#########",
                                    ".#######.",
                                    "..#####..",
                                    "..##.##..",
                                    ".##...##.",
                                    "#.......#"
                                };
                                constexpr int starSize = 9;
                                const int starX = raw->w - starSize - 2;
                                const int starY = raw->h - starSize - 2;
                                const Uint32 border = SDL_MapRGBA(raw->format, 0, 32, 104, 255);
                                const Uint32 blue = SDL_MapRGBA(raw->format, 24, 152, 255, 255);
                                for(int y = 0; y < starSize; ++y) {
                                    for(int x = 0; x < starSize; ++x) {
                                        if(starRows[y][x] != '#') {
                                            continue;
                                        }
                                        for(int oy = -1; oy <= 1; ++oy) {
                                            for(int ox = -1; ox <= 1; ++ox) {
                                                SDL_Rect pixel{starX + x + ox, starY + y + oy, 1, 1};
                                                SDL_FillRect(raw.get(), &pixel, border);
                                            }
                                        }
                                    }
                                }
                                for(int y = 0; y < starSize; ++y) {
                                    for(int x = 0; x < starSize; ++x) {
                                        if(starRows[y][x] == '#') {
                                            SDL_Rect pixel{starX + x, starY + y, 1, 1};
                                            SDL_FillRect(raw.get(), &pixel, blue);
                                        }
                                    }
                                }
                            }
                        }
                        sdl2::texture_ptr tex{ SDL_CreateTextureFromSurface(renderer, raw.get()) };
                        if(tex) {
                            smallDetailPicTex[pictureIndex] = std::move(tex);
                            return;
                        }
                    }
                }
                smallDetailPicTex[pictureIndex] = extractSmallDetailPic(fallbackWsa);
            } catch(const std::exception& e) {
                SDL_Log("GFXManager: portrait %s load failed (%s) — falling back to %s",
                        pngName.c_str(), e.what(), fallbackWsa);
                smallDetailPicTex[pictureIndex] = extractSmallDetailPic(fallbackWsa);
            }
        };
        loadIcon(Picture_RocketTrike,    "RocketTrikeIcon.png",    "TRIKE.WSA");
        loadIcon(Picture_SonicTrike,     "SonicTrikeIcon.png",     "TRIKE.WSA");
        loadIcon(Picture_FlameTank,      "FlameTankIcon.png",      "HTANK.WSA");
        loadIcon(Picture_EliteLauncher,  "EliteLauncherIcon.png",  "HTANK.WSA");
        loadIcon(Picture_EliteSiegeTank, "EliteSiegeTankIcon.png", "HTANK.WSA");
        loadIcon(Picture_ChemicalSiegeTank, "ChemicalSiegeTankIcon.png", "HTANK.WSA");
        loadIcon(Picture_ChemicalCarryall, "ChemicalCarryallIcon.png", "CARRYALL.WSA");
        loadIcon(Picture_AdvancedWindTrap, "Tornie_AdvancedWindtrap_icon.png", "WINDTRAP.WSA");
        loadIcon(Picture_Worfinery,      "WorfineryIcon.png",      "WOR.WSA");
        loadIcon(Picture_TechCenter,     "TechCenterIcon.png",     "PALACE.WSA");
        loadIcon(Picture_Scoutpost,      "ScoutpostIcon.png",      "RTURRET.WSA");
        loadIcon(Picture_Flamepost,      "FlamepostIcon.png",      "RTURRET.WSA");
        loadIcon(Picture_Chemipost,      "ChemipostIcon.png",      "RTURRET.WSA");
        loadIcon(Picture_ChaosFactory,   "ChaosFactoryIcon.png",   "STARPORT.WSA");
        loadIcon(Picture_LoveFactory,     "LoveFactoryIcon.png",     "STARPORT.WSA");
        loadIcon(Picture_PalaceLightVehicles, "PalaceTrikeAndQuadIcon.png", "FREMEN.WSA");
        loadIcon(Picture_PalaceRebelsCharging, "PalaceRebelsChargingIcon.png", "FREMEN.WSA");
        loadIcon(Picture_Harvestank,     "HarvestankIcon.png",     "HARVEST.WSA");
        reloadRuntimeModPortraits();
    }

    // unused: FARTR.WSA, FHARK.WSA, FORDOS.WSA


    // Helper function to safely create tiny picture textures
    auto createTinyPictureTexture = [&](int pictureIndex, const char* name) {
        sdl2::texture_ptr texture = convertSurfaceToTexture(shapes->getPicture(pictureIndex));
        if(texture == nullptr) {
            SDL_Log("Warning: Failed to create tiny picture texture for %s (index %d)", name, pictureIndex);
        }
        return texture;
    };

    tinyPictureTex[TinyPicture_Spice] = createTinyPictureTexture(94, "Spice");
    tinyPictureTex[TinyPicture_Barracks] = createTinyPictureTexture(62, "Barracks");
    tinyPictureTex[TinyPicture_ConstructionYard] = createTinyPictureTexture(60, "ConstructionYard");
    tinyPictureTex[TinyPicture_GunTurret] = createTinyPictureTexture(67, "GunTurret");
    tinyPictureTex[TinyPicture_HeavyFactory] = createTinyPictureTexture(56, "HeavyFactory");
    tinyPictureTex[TinyPicture_HighTechFactory] = createTinyPictureTexture(57, "HighTechFactory");
    tinyPictureTex[TinyPicture_IX] = createTinyPictureTexture(58, "IX");
    tinyPictureTex[TinyPicture_LightFactory] = createTinyPictureTexture(55, "LightFactory");
    tinyPictureTex[TinyPicture_Palace] = createTinyPictureTexture(54, "Palace");
    tinyPictureTex[TinyPicture_Radar] = createTinyPictureTexture(70, "Radar");
    tinyPictureTex[TinyPicture_Refinery] = createTinyPictureTexture(64, "Refinery");
    tinyPictureTex[TinyPicture_RepairYard] = createTinyPictureTexture(65, "RepairYard");
    tinyPictureTex[TinyPicture_RocketTurret] = createTinyPictureTexture(68, "RocketTurret");
    tinyPictureTex[TinyPicture_Silo] = createTinyPictureTexture(69, "Silo");
    tinyPictureTex[TinyPicture_Slab1] = createTinyPictureTexture(53, "Slab1");
    tinyPictureTex[TinyPicture_Slab4] = createTinyPictureTexture(71, "Slab4");
    tinyPictureTex[TinyPicture_StarPort] = createTinyPictureTexture(63, "StarPort");
    tinyPictureTex[TinyPicture_Wall] = createTinyPictureTexture(66, "Wall");
    tinyPictureTex[TinyPicture_WindTrap] = createTinyPictureTexture(61, "WindTrap");
    tinyPictureTex[TinyPicture_WOR] = createTinyPictureTexture(59, "WOR");
    tinyPictureTex[TinyPicture_Carryall] = createTinyPictureTexture(77, "Carryall");
    tinyPictureTex[TinyPicture_Devastator] = createTinyPictureTexture(75, "Devastator");
    tinyPictureTex[TinyPicture_Deviator] = createTinyPictureTexture(86, "Deviator");
    tinyPictureTex[TinyPicture_Frigate] = createTinyPictureTexture(77, "Frigate");    // use carryall picture
    tinyPictureTex[TinyPicture_Harvester] = createTinyPictureTexture(88, "Harvester");
    tinyPictureTex[TinyPicture_Soldier] = createTinyPictureTexture(90, "Soldier");
    tinyPictureTex[TinyPicture_Launcher] = createTinyPictureTexture(73, "Launcher");
    tinyPictureTex[TinyPicture_MCV] = createTinyPictureTexture(89, "MCV");
    tinyPictureTex[TinyPicture_Ornithopter] = createTinyPictureTexture(85, "Ornithopter");
    tinyPictureTex[TinyPicture_Quad] = createTinyPictureTexture(74, "Quad");
    tinyPictureTex[TinyPicture_Saboteur] = createTinyPictureTexture(84, "Saboteur");
    tinyPictureTex[TinyPicture_Sandworm] = createTinyPictureTexture(93, "Sandworm");
    tinyPictureTex[TinyPicture_SiegeTank] = createTinyPictureTexture(72, "SiegeTank");
    tinyPictureTex[TinyPicture_SonicTank] = createTinyPictureTexture(79, "SonicTank");
    tinyPictureTex[TinyPicture_Tank] = createTinyPictureTexture(78, "Tank");
    tinyPictureTex[TinyPicture_Trike] = createTinyPictureTexture(80, "Trike");
    tinyPictureTex[TinyPicture_RaiderTrike] = createTinyPictureTexture(87, "RaiderTrike");
    tinyPictureTex[TinyPicture_Trooper] = createTinyPictureTexture(76, "Trooper");
    tinyPictureTex[TinyPicture_Special] = createTinyPictureTexture(75, "Special");    // use devastator picture
    tinyPictureTex[TinyPicture_Infantry] = createTinyPictureTexture(81, "Infantry");
    tinyPictureTex[TinyPicture_Troopers] = createTinyPictureTexture(91, "Troopers");

    // load UI graphics
    uiGraphic[UI_RadarAnimation][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(radar->getAnimationAsPictureRow(NUM_STATIC_ANIMATIONS_PER_ROW).get());

    uiGraphic[UI_CursorNormal][HOUSE_HARKONNEN] = mouse->getPicture(0);
    SDL_SetColorKey(uiGraphic[UI_CursorNormal][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorUp][HOUSE_HARKONNEN] = mouse->getPicture(1);
    SDL_SetColorKey(uiGraphic[UI_CursorUp][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorRight][HOUSE_HARKONNEN] = mouse->getPicture(2);
    SDL_SetColorKey(uiGraphic[UI_CursorRight][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorDown][HOUSE_HARKONNEN] = mouse->getPicture(3);
    SDL_SetColorKey(uiGraphic[UI_CursorDown][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorLeft][HOUSE_HARKONNEN] = mouse->getPicture(4);
    SDL_SetColorKey(uiGraphic[UI_CursorLeft][HOUSE_HARKONNEN].get() , SDL_TRUE, 0);

    uiGraphic[UI_CursorMove_Zoomlevel0][HOUSE_HARKONNEN] = mouse->getPicture(5);
    SDL_SetColorKey(uiGraphic[UI_CursorMove_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_CursorAttack_Zoomlevel0][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_CursorMove_Zoomlevel0][HOUSE_HARKONNEN].get(), 232, PALCOLOR_HARKONNEN);
    SDL_SetColorKey(uiGraphic[UI_CursorAttack_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CursorHeal_Zoomlevel0][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_CursorMove_Zoomlevel0][HOUSE_HARKONNEN].get(), 232, PALCOLOR_ATREIDES);
    SDL_SetColorKey(uiGraphic[UI_CursorHeal_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_CursorCapture_Zoomlevel0][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Capture.png").get());
    SDL_SetColorKey(uiGraphic[UI_CursorCapture_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_CursorCarryallDrop_Zoomlevel0][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("CarryallDrop.png").get());
    SDL_SetColorKey(uiGraphic[UI_CursorCarryallDrop_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_ReturnIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Return.png").get());
    SDL_SetColorKey(uiGraphic[UI_ReturnIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_DeployIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Deploy.png").get());
    SDL_SetColorKey(uiGraphic[UI_DeployIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_DestructIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Destruct.png").get());
    SDL_SetColorKey(uiGraphic[UI_DestructIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_SendToRepairIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("SendToRepair.png").get());
    SDL_SetColorKey(uiGraphic[UI_SendToRepairIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_CreditsDigits][HOUSE_HARKONNEN] = shapes->getPictureArray(10,1,2|TILE_NORMAL,3|TILE_NORMAL,4|TILE_NORMAL,5|TILE_NORMAL,6|TILE_NORMAL,
                                                                                7|TILE_NORMAL,8|TILE_NORMAL,9|TILE_NORMAL,10|TILE_NORMAL,11|TILE_NORMAL);
    uiGraphic[UI_SideBar][HOUSE_HARKONNEN] = PicFactory->createSideBar(false);
    uiGraphic[UI_Indicator][HOUSE_HARKONNEN] = units1->getPictureArray(3,1,8|TILE_NORMAL,9|TILE_NORMAL,10|TILE_NORMAL);
    SDL_SetColorKey(uiGraphic[UI_Indicator][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    SDL_Color indicatorTransparent = { 255, 255, 255, 48 };
    SDL_SetPaletteColors(uiGraphic[UI_Indicator][HOUSE_HARKONNEN]->format->palette, &indicatorTransparent, PALCOLOR_WHITE, 1);
    uiGraphic[UI_InvalidPlace_Zoomlevel0][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(16, PALCOLOR_LIGHTRED);
    uiGraphic[UI_InvalidPlace_Zoomlevel1][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(32, PALCOLOR_LIGHTRED);
    uiGraphic[UI_InvalidPlace_Zoomlevel2][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(48, PALCOLOR_LIGHTRED);
    uiGraphic[UI_ValidPlace_Zoomlevel0][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(16, PALCOLOR_LIGHTGREEN);
    uiGraphic[UI_ValidPlace_Zoomlevel1][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(32, PALCOLOR_LIGHTGREEN);
    uiGraphic[UI_ValidPlace_Zoomlevel2][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(48, PALCOLOR_LIGHTGREEN);
    uiGraphic[UI_GreyPlace_Zoomlevel0][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(16, PALCOLOR_LIGHTGREY);
    uiGraphic[UI_GreyPlace_Zoomlevel1][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(32, PALCOLOR_LIGHTGREY);
    uiGraphic[UI_GreyPlace_Zoomlevel2][HOUSE_HARKONNEN] = PicFactory->createPlacingGrid(48, PALCOLOR_LIGHTGREY);
    DuneStyle menuStyle(settings.video.menuPalette);
    uiGraphic[UI_MenuBackground][HOUSE_HARKONNEN] = menuStyle.createBackground(getRendererWidth(),getRendererHeight());
    uiGraphic[UI_GameStatsBackground][HOUSE_HARKONNEN] = PicFactory->createGameStatsBackground(HOUSE_HARKONNEN);
    uiGraphic[UI_GameStatsBackground][HOUSE_ATREIDES] = PicFactory->createGameStatsBackground(HOUSE_ATREIDES);
    uiGraphic[UI_GameStatsBackground][HOUSE_ORDOS] = PicFactory->createGameStatsBackground(HOUSE_ORDOS);
    uiGraphic[UI_GameStatsBackground][HOUSE_FREMEN] = PicFactory->createGameStatsBackground(HOUSE_FREMEN);
    uiGraphic[UI_GameStatsBackground][HOUSE_SARDAUKAR] = PicFactory->createGameStatsBackground(HOUSE_SARDAUKAR);
    uiGraphic[UI_GameStatsBackground][HOUSE_MERCENARY] = PicFactory->createGameStatsBackground(HOUSE_MERCENARY);
    uiGraphic[UI_SelectionBox_Zoomlevel0][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("UI_SelectionBox.png").get());
    SDL_SetColorKey(uiGraphic[UI_SelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_SelectionBox_Zoomlevel1][HOUSE_HARKONNEN] = Scaler::defaultDoubleTiledSurface(uiGraphic[UI_SelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), 1, 1);
    SDL_SetColorKey(uiGraphic[UI_SelectionBox_Zoomlevel1][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_SelectionBox_Zoomlevel2][HOUSE_HARKONNEN] = Scaler::defaultTripleTiledSurface(uiGraphic[UI_SelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), 1, 1);
    SDL_SetColorKey(uiGraphic[UI_SelectionBox_Zoomlevel2][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel0][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("UI_OtherPlayerSelectionBox.png").get());
    SDL_SetColorKey(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel1][HOUSE_HARKONNEN] = Scaler::defaultDoubleTiledSurface(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), 1, 1);
    SDL_SetColorKey(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel1][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel2][HOUSE_HARKONNEN] = Scaler::defaultTripleTiledSurface(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel0][HOUSE_HARKONNEN].get(), 1, 1);
    SDL_SetColorKey(uiGraphic[UI_OtherPlayerSelectionBox_Zoomlevel2][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_TopBar][HOUSE_HARKONNEN] = PicFactory->createTopBar();
    uiGraphic[UI_ButtonUp][HOUSE_HARKONNEN] = choam->getPicture(0);
    uiGraphic[UI_ButtonUp_Pressed][HOUSE_HARKONNEN] = choam->getPicture(1);
    uiGraphic[UI_ButtonDown][HOUSE_HARKONNEN] = choam->getPicture(2);
    uiGraphic[UI_ButtonDown_Pressed][HOUSE_HARKONNEN] = choam->getPicture(3);
    uiGraphic[UI_BuilderListUpperCap][HOUSE_HARKONNEN] = PicFactory->createBuilderListUpperCap();
    uiGraphic[UI_BuilderListLowerCap][HOUSE_HARKONNEN] = PicFactory->createBuilderListLowerCap();
    uiGraphic[UI_CustomGamePlayersArrow][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("CustomGamePlayers_Arrow.png").get());
    SDL_SetColorKey(uiGraphic[UI_CustomGamePlayersArrow][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_CustomGamePlayersArrowNeutral][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("CustomGamePlayers_ArrowNeutral.png").get());
    SDL_SetColorKey(uiGraphic[UI_CustomGamePlayersArrowNeutral][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MessageBox][HOUSE_HARKONNEN] = PicFactory->createMessageBoxBorder();

    if(bttn != nullptr) {
        uiGraphic[UI_Mentat][HOUSE_HARKONNEN] = bttn->getPicture(0);
        uiGraphic[UI_Mentat_Pressed][HOUSE_HARKONNEN] = bttn->getPicture(1);
        uiGraphic[UI_Options][HOUSE_HARKONNEN] = bttn->getPicture(2);
        uiGraphic[UI_Options_Pressed][HOUSE_HARKONNEN] = bttn->getPicture(3);
    } else {
        uiGraphic[UI_Mentat][HOUSE_HARKONNEN] = shapes->getPicture(94);
        uiGraphic[UI_Mentat_Pressed][HOUSE_HARKONNEN] = shapes->getPicture(95);
        uiGraphic[UI_Options][HOUSE_HARKONNEN] = shapes->getPicture(96);
        uiGraphic[UI_Options_Pressed][HOUSE_HARKONNEN] = shapes->getPicture(97);
    }

    uiGraphic[UI_Upgrade][HOUSE_HARKONNEN] = choam->getPicture(4);
    SDL_SetColorKey(uiGraphic[UI_Upgrade][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_Upgrade_Pressed][HOUSE_HARKONNEN] = choam->getPicture(5);
    SDL_SetColorKey(uiGraphic[UI_Upgrade_Pressed][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_Repair][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_Repair.png").get());
    uiGraphic[UI_Repair_Pressed][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_RepairPushed.png").get());
    uiGraphic[UI_Minus][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_Minus.png").get());
    uiGraphic[UI_Minus_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_Minus][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-2);
    uiGraphic[UI_Minus_Pressed][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_MinusPushed.png").get());
    uiGraphic[UI_Plus][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_Plus.png").get());
    uiGraphic[UI_Plus_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_Plus][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-2);
    uiGraphic[UI_Plus_Pressed][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Button_PlusPushed.png").get());
    for(auto pair : {std::pair{UI_Minus,UI_Minus_Pressed},std::pair{UI_Plus,UI_Plus_Pressed}}) {
        const auto* source=uiGraphic[pair.first][HOUSE_HARKONNEN].get();
        const int width=source->w,height=source->h;
        const std::string text=pair.first==UI_Plus ? "+" : "-";
        uiGraphic[pair.first][HOUSE_HARKONNEN]=menuStyle.createButtonSurface(width,height,text,false,false);
        uiGraphic[pair.second][HOUSE_HARKONNEN]=menuStyle.createButtonSurface(width,height,text,true,true);
        const auto active=pair.first==UI_Plus ? UI_Plus_Active : UI_Minus_Active;
        uiGraphic[active][HOUSE_HARKONNEN]=menuStyle.createButtonSurface(width,height,text,false,true);
    }
    uiGraphic[UI_MissionSelect][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("Menu_MissionSelect.png").get());
    PicFactory->drawFrame(uiGraphic[UI_MissionSelect][HOUSE_HARKONNEN].get(),PictureFactory::SimpleFrame,nullptr);
    SDL_SetColorKey(uiGraphic[UI_MissionSelect][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_OptionsMenu][HOUSE_HARKONNEN] = PicFactory->createOptionsMenu();
    uiGraphic[UI_LoadSaveWindow][HOUSE_HARKONNEN] = menuStyle.createBackground(440,360);
    uiGraphic[UI_NewMapWindow][HOUSE_HARKONNEN] = menuStyle.createBackground(600,440);
    uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("DuneLegacy.png").get());
    {
        const int width=uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN]->w;
        const int height=uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN]->h;
        auto banner=menuStyle.createBackground(width,height);
        auto title=pFontManager->createSurfaceWithText("Dune City",COLOR_WHITE,std::max(16,height-16));
        auto rect=calcDrawingRect(title.get(),width/2,height/2,HAlign::Center,VAlign::Center);
        SDL_BlitSurface(title.get(),nullptr,banner.get(),&rect);
        uiGraphic[UI_DuneLegacy][HOUSE_HARKONNEN]=std::move(banner);
        uiGraphic[UI_GameMenu][HOUSE_HARKONNEN]=menuStyle.createBackground(width,158);
    }

    uiGraphic[UI_PlanetBackground][HOUSE_HARKONNEN] = LoadCPS_RW(pFileManager->openFile("BIGPLAN.CPS").get());
    PicFactory->drawFrame(uiGraphic[UI_PlanetBackground][HOUSE_HARKONNEN].get(),PictureFactory::SimpleFrame);
    uiGraphic[UI_MenuButtonBorder][HOUSE_HARKONNEN] = PicFactory->createFrame(PictureFactory::DecorationFrame1,190,140,false);


    const bool tornieActive = ModManager::instance().isInitialized()
        && ModManager::instance().isTornieContentActive();
    loadMentatGraphics();

    uiGraphic[UI_MentatBackgroundBene][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATM.CPS").get()).get());
    if(uiGraphic[UI_MentatBackgroundBene][HOUSE_HARKONNEN] != nullptr) {
        benePalette.applyToSurface(uiGraphic[UI_MentatBackgroundBene][HOUSE_HARKONNEN].get());
    }

    for(int house = HOUSE_HARKONNEN; house < getNumCustomGameHouses(); ++house) {
        uiGraphic[UI_MentatHouseChoiceInfoQuestion][house] =
            PicFactory->createMentatHouseChoiceQuestion(house, benePalette);
    }

    uiGraphic[UI_MentatYes][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(0).get());
    uiGraphic[UI_MentatYes_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(1).get());
    uiGraphic[UI_MentatNo][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(2).get());
    uiGraphic[UI_MentatNo_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(3).get());
    uiGraphic[UI_MentatExit][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(4).get());
    uiGraphic[UI_MentatExit_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(5).get());
    uiGraphic[UI_MentatProcced][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(6).get());
    uiGraphic[UI_MentatProcced_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(7).get());
    uiGraphic[UI_MentatRepeat][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(8).get());
    uiGraphic[UI_MentatRepeat_Pressed][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(mentat->getPicture(9).get());

    { // Scope
        sdl2::surface_ptr pHouseChoiceBackground;
        if (pFileManager->exists("HERALD." + _("LanguageFileExtension"))) {
            pHouseChoiceBackground = LoadCPS_RW(pFileManager->openFile("HERALD." + _("LanguageFileExtension")).get());
        }
        else {
            pHouseChoiceBackground = LoadCPS_RW(pFileManager->openFile("HERALD.CPS").get());
        }

        uiGraphic[UI_HouseSelect][HOUSE_HARKONNEN] = PicFactory->createHouseSelect(pHouseChoiceBackground.get());
        uiGraphic[UI_SelectYourHouseLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(getSubPicture(pHouseChoiceBackground.get(), 0, 0, 320, 50).get());
        uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES] = getSubPicture(pHouseChoiceBackground.get(), 20, 54, 83, 91);
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_ATREIDES] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get());
        uiGraphic[UI_Herald_Colored][HOUSE_ORDOS] = getSubPicture(pHouseChoiceBackground.get(), 117, 54, 83, 91);
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_ORDOS] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get());
        uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN] = getSubPicture(pHouseChoiceBackground.get(), 215, 54, 83, 91);
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
        uiGraphic[UI_Herald_Colored][HOUSE_FREMEN] = PicFactory->createHeraldFre(uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_FREMEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_FREMEN].get());
        uiGraphic[UI_Herald_Colored][HOUSE_SARDAUKAR] = PicFactory->createHeraldSard(uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get(), uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get());
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_SARDAUKAR] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_SARDAUKAR].get());
        uiGraphic[UI_Herald_Colored][HOUSE_MERCENARY] = PicFactory->createHeraldMerc(uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get(), uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get());
        uiGraphic[UI_Herald_ColoredLarge][HOUSE_MERCENARY] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][HOUSE_MERCENARY].get());

        auto loadBonusHerald = [&](int house, const char* filename, SDL_Surface* fallback) {
            if(pFileManager->exists(filename)) {
                auto herald = LoadPNG_RW(pFileManager->openFile(filename).get());
                if(herald) {
                    if(house != HOUSE_REBELS) {
                        SDL_SetColorKey(herald.get(), SDL_TRUE, 0);
                    }
                    uiGraphic[UI_Herald_Colored][house] = std::move(herald);
                }
            }

            if(uiGraphic[UI_Herald_Colored][house] == nullptr) {
                if(house == HOUSE_REBELS) {
                    uiGraphic[UI_Herald_Colored][house] = copySurface(fallback);
                } else {
                    uiGraphic[UI_Herald_Colored][house] =
                        mapSurfaceColorRange(fallback, PALCOLOR_HARKONNEN, getHousePaletteIndex(static_cast<HOUSETYPE>(house)));
                }
            }

            SDL_Surface* heraldSurface = uiGraphic[UI_Herald_Colored][house].get();
            uiGraphic[UI_Herald_ColoredLarge][house] =
                heraldSurface->format->BytesPerPixel == 1
                    ? Scaler::defaultDoubleSurface(heraldSurface)
                    : Scaler::doubleSurfaceNN(heraldSurface);
        };

        loadBonusHerald(HOUSE_NEUTRAL, "HeraldNeu.png",
                        uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
        loadBonusHerald(HOUSE_REBELS, "HeraldRebels.png",
                        uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
    }

    uiGraphic[UI_Herald_Grey][HOUSE_HARKONNEN] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_Grey][HOUSE_ATREIDES] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_ATREIDES].get());
    uiGraphic[UI_Herald_Grey][HOUSE_ORDOS] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_ORDOS].get());
    uiGraphic[UI_Herald_Grey][HOUSE_FREMEN] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_FREMEN].get());
    uiGraphic[UI_Herald_Grey][HOUSE_SARDAUKAR] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_SARDAUKAR].get());
    uiGraphic[UI_Herald_Grey][HOUSE_MERCENARY] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_MERCENARY].get());
    uiGraphic[UI_Herald_Grey][HOUSE_NEUTRAL] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_NEUTRAL].get());
    uiGraphic[UI_Herald_Grey][HOUSE_REBELS] = PicFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][HOUSE_REBELS].get());

    loadCustomHouseHerald();

    uiGraphic[UI_Herald_ArrowLeft][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("ArrowLeft.png").get());
    uiGraphic[UI_Herald_ArrowLeftLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_ArrowLeft][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_ArrowLeftHighlight][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("ArrowLeftHighlight.png").get());
    uiGraphic[UI_Herald_ArrowLeftHighlightLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_ArrowLeftHighlight][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_ArrowRight][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("ArrowRight.png").get());
    uiGraphic[UI_Herald_ArrowRightLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_ArrowRight][HOUSE_HARKONNEN].get());
    uiGraphic[UI_Herald_ArrowRightHighlight][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("ArrowRightHighlight.png").get());
    uiGraphic[UI_Herald_ArrowRightHighlightLarge][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_ArrowRightHighlight][HOUSE_HARKONNEN].get());

    uiGraphic[UI_MapChoiceScreen][HOUSE_HARKONNEN] = PicFactory->createMapChoiceScreen(HOUSE_HARKONNEN);
    uiGraphic[UI_MapChoiceScreen][HOUSE_ATREIDES] = PicFactory->createMapChoiceScreen(HOUSE_ATREIDES);
    uiGraphic[UI_MapChoiceScreen][HOUSE_ORDOS] = PicFactory->createMapChoiceScreen(HOUSE_ORDOS);
    uiGraphic[UI_MapChoiceScreen][HOUSE_FREMEN] = PicFactory->createMapChoiceScreen(HOUSE_FREMEN);
    uiGraphic[UI_MapChoiceScreen][HOUSE_SARDAUKAR] = PicFactory->createMapChoiceScreen(HOUSE_SARDAUKAR);
    uiGraphic[UI_MapChoiceScreen][HOUSE_MERCENARY] = PicFactory->createMapChoiceScreen(HOUSE_MERCENARY);
    uiGraphic[UI_MapChoiceScreen][HOUSE_NEUTRAL] = PicFactory->createMapChoiceScreen(HOUSE_NEUTRAL);
    uiGraphic[UI_MapChoiceScreen][HOUSE_REBELS] = PicFactory->createMapChoiceScreen(HOUSE_REBELS);
    uiGraphic[UI_MapChoicePlanet][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(LoadCPS_RW(pFileManager->openFile("PLANET.CPS").get()).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoicePlanet][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceMapOnly][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(LoadCPS_RW(pFileManager->openFile("DUNEMAP.CPS").get()).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceMapOnly][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceMap][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(LoadCPS_RW(pFileManager->openFile("DUNERGN.CPS").get()).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceMap][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    // make black lines inside the map non-transparent
    {
        const auto surface = uiGraphic[UI_MapChoiceMap][HOUSE_HARKONNEN].get();

        sdl2::surface_lock lock{ surface };

        for(auto y = 48; y < 48+240; y++) {
            for(auto x = 16; x < 16 + 608; x++) {
                if(getPixel(surface, x, y) == 0) {
                    putPixel(surface, x, y, PALCOLOR_BLACK);
                }
            }
        }
    }

    uiGraphic[UI_MapChoiceClickMap][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(LoadCPS_RW(pFileManager->openFile("RGNCLK.CPS").get()).get());
    uiGraphic[UI_MapChoiceArrow_None][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(0).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_None][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_LeftUp][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(1).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_LeftUp][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_Up][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(2).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_Up][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_RightUp][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(3).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_RightUp][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_Right][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(4).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_Right][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_RightDown][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(5).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_RightDown][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_Down][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(6).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_Down][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_LeftDown][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(7).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_LeftDown][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapChoiceArrow_Left][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(arrows->getPicture(8).get());
    SDL_SetColorKey(uiGraphic[UI_MapChoiceArrow_Left][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_StructureSizeLattice][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("StructureSizeLattice.png").get());
    SDL_SetColorKey(uiGraphic[UI_StructureSizeLattice][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_StructureSizeConcrete][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("StructureSizeConcrete.png").get());
    SDL_SetColorKey(uiGraphic[UI_StructureSizeConcrete][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_MapEditor_SideBar][HOUSE_HARKONNEN] = PicFactory->createSideBar(true);
    uiGraphic[UI_MapEditor_BottomBar][HOUSE_HARKONNEN] = PicFactory->createBottomBar();

    uiGraphic[UI_MapEditor_ExitIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorExitIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ExitIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_NewIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorNewIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_NewIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_LoadIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorLoadIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_LoadIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_SaveIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorSaveIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_SaveIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_UndoIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorUndoIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_UndoIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_RedoIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorRedoIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RedoIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_PlayerIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPlayerIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_PlayerIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MapSettingsIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMapSettingsIcon.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MapSettingsIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ChoamIcon][HOUSE_HARKONNEN] = scaleSurface(getSubFrame(objPic[ObjPic_Frigate][HOUSE_HARKONNEN][0].get(),1,0,8,1).get(), 0.5);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ChoamIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ReinforcementsIcon][HOUSE_HARKONNEN] = scaleSurface(getSubFrame(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0].get(),1,0,8,2).get(), 0.66667);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ReinforcementsIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_TeamsIcon][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Troopers][HOUSE_HARKONNEN][0].get(),0,0,4,4);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_TeamsIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorNoneIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorNone.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorNoneIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorHorizontalIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorHorizontal.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorHorizontalIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorVerticalIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorVertical.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorVerticalIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorBothIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorBoth.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorBothIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_MirrorPointIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMirrorPoint.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_MirrorPointIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ArrowUp][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorArrowUp.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ArrowUp][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ArrowUp_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_ArrowUp][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    uiGraphic[UI_MapEditor_ArrowDown][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorArrowDown.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_ArrowDown][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_ArrowDown_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_ArrowDown][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    uiGraphic[UI_MapEditor_Plus][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPlus.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Plus][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_Plus_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_Plus][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    uiGraphic[UI_MapEditor_Minus][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorMinus.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Minus][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_Minus_Active][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_Minus][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    uiGraphic[UI_MapEditor_RotateLeftIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorRotateLeft.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RotateLeftIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_RotateLeftHighlightIcon][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_RotateLeftIcon][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RotateLeftHighlightIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_RotateRightIcon][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorRotateRight.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RotateRightIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_RotateRightHighlightIcon][HOUSE_HARKONNEN] = mapSurfaceColorRange(uiGraphic[UI_MapEditor_RotateRightIcon][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN-3);
    SDL_SetColorKey(uiGraphic[UI_MapEditor_RotateRightHighlightIcon][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(127).get());
    uiGraphic[UI_MapEditor_Dunes][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(159).get());
    uiGraphic[UI_MapEditor_SpecialBloom][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(209).get());
    uiGraphic[UI_MapEditor_Spice][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(191).get());
    uiGraphic[UI_MapEditor_ThickSpice][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(207).get());
    uiGraphic[UI_MapEditor_GreenSpice][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_Spice][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 24, 112, 48, 255 });
    uiGraphic[UI_MapEditor_ThickGreenSpice][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_ThickSpice][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  tornieActive
                                      ? SDL_Color{ 24, 112, 48, 255 }
                                      : SDL_Color{ 20, 84, 42, 255 });
    uiGraphic[UI_MapEditor_RedSpice][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_Spice][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 136, 48, 40, 255 });
    uiGraphic[UI_MapEditor_ThickRedSpice][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_ThickSpice][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  tornieActive
                                      ? SDL_Color{ 136, 48, 40, 255 }
                                      : SDL_Color{ 96, 32, 30, 255 });
    uiGraphic[UI_MapEditor_SpiceBloom][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(208).get());
    uiGraphic[UI_MapEditor_GreenSpiceBloom][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_SpiceBloom][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 24, 112, 48, 255 });
    uiGraphic[UI_MapEditor_RedSpiceBloom][HOUSE_HARKONNEN] =
        createTintedMapEditorIcon(uiGraphic[UI_MapEditor_SpiceBloom][HOUSE_HARKONNEN].get(),
                                  uiGraphic[UI_MapEditor_Sand][HOUSE_HARKONNEN].get(),
                                  SDL_Color{ 136, 48, 40, 255 });
    uiGraphic[UI_MapEditor_Slab][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(126).get());
    uiGraphic[UI_MapEditor_Rock][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(143).get());
    uiGraphic[UI_MapEditor_Mountain][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(icon->getPicture(175).get());

    uiGraphic[UI_MapEditor_Slab1][HOUSE_HARKONNEN] = icon->getPicture(126);
    uiGraphic[UI_MapEditor_Wall][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Wall][HOUSE_HARKONNEN][0].get(),2*D2_TILESIZE,0,D2_TILESIZE,D2_TILESIZE);
    uiGraphic[UI_MapEditor_GunTurret][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_GunTurret][HOUSE_HARKONNEN][0].get(),2*D2_TILESIZE,0,D2_TILESIZE,D2_TILESIZE);
    uiGraphic[UI_MapEditor_RocketTurret][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_RocketTurret][HOUSE_HARKONNEN][0].get(),2*D2_TILESIZE,0,D2_TILESIZE,D2_TILESIZE);
    uiGraphic[UI_MapEditor_ConstructionYard][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Windtrap][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    SDL_Color windtrapColor = { 70, 70, 70, 255};
    SDL_SetPaletteColors(uiGraphic[UI_MapEditor_Windtrap][HOUSE_HARKONNEN]->format->palette, &windtrapColor, PALCOLOR_WINDTRAP_COLORCYCLE, 1);
    auto loadMapEditorStructurePreview = [&](const char* pngName, const char* label) -> sdl2::surface_ptr {
        auto rwop = openTornieAsset(pngName, label);
        if(!rwop) {
            return nullptr;
        }

        auto raw = LoadPNG_RW(rwop.get());
        if(!raw || raw->format->BitsPerPixel != 8 || !raw->format->palette) {
            SDL_Log("GFXManager: %s editor sprite '%s' is not 8-bit indexed, using object sprite", label, pngName);
            return nullptr;
        }

        preserveOpaqueBlackIndex(raw.get());
        normalizeTransparentPaletteIndexes(raw.get());
        if(ibmPaletteLoaded) {
            if(auto remapped = remapIndexedSurfaceToPalette(raw.get(), ibmPalette.getSDLPalette())) {
                raw = std::move(remapped);
            } else {
                ibmPalette.applyToSurface(raw.get());
            }
            normalizeTransparentPaletteIndexes(raw.get());
        }
        normalizeHouseColorRangesToHarkonnen(raw.get());
        normalizeHarkonnenTeamRed(raw.get());
        SDL_SetColorKey(raw.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
        return raw;
    };

    if(auto advancedWindtrapEditor = loadMapEditorStructurePreview("Tornie_AdvancedWindtrap_gfx_editor.png",
                                                                   "Advanced Windtrap editor")) {
        uiGraphic[UI_MapEditor_AdvancedWindTrap][HOUSE_HARKONNEN] = std::move(advancedWindtrapEditor);
    } else {
        uiGraphic[UI_MapEditor_AdvancedWindTrap][HOUSE_HARKONNEN] =
            getSubPicture(objPic[ObjPic_AdvancedWindTrap][HOUSE_HARKONNEN][0].get(),
                          2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 3*D2_TILESIZE);
    }
    // Tornie: Adv Windtrap MK2 variant — same sprite as vanilla Adv Windtrap
    uiGraphic[UI_MapEditor_AdvancedWindTrapMK2][HOUSE_HARKONNEN] =
        getSubPicture(objPic[ObjPic_AdvancedWindTrap2x3][HOUSE_HARKONNEN][0].get(),
                      2*2*D2_TILESIZE, 0, 2*D2_TILESIZE, 3*D2_TILESIZE);
    uiGraphic[UI_MapEditor_AdvancedWindTrapMK3][HOUSE_HARKONNEN] =
        getSubPicture(objPic[ObjPic_AdvancedWindTrap3x2][HOUSE_HARKONNEN][0].get(),
                      2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE);
    auto applyMapEditorWindtrapColor = [&](unsigned int uiId) {
        if(uiGraphic[uiId][HOUSE_HARKONNEN] && uiGraphic[uiId][HOUSE_HARKONNEN]->format->palette) {
            SDL_SetPaletteColors(uiGraphic[uiId][HOUSE_HARKONNEN]->format->palette,
                                 &windtrapColor, PALCOLOR_WINDTRAP_COLORCYCLE, 1);
        }
    };
    applyMapEditorWindtrapColor(UI_MapEditor_AdvancedWindTrap);
    applyMapEditorWindtrapColor(UI_MapEditor_AdvancedWindTrapMK2);
    applyMapEditorWindtrapColor(UI_MapEditor_AdvancedWindTrapMK3);
    uiGraphic[UI_MapEditor_Radar][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Radar][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Silo][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Silo][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_IX][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_IX][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Barracks][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Barracks][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_WOR][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_WOR][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    // Tornie: Worfinery = 48x64 PNG, take top 2-tile-tall frame (first of 2 vertical frames).
    if(objPic[ObjPic_Worfinery][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_Worfinery][HOUSE_HARKONNEN] = getSubPicture(
            objPic[ObjPic_Worfinery][HOUSE_HARKONNEN][0].get(), 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE);
    } else {
        // Fallback to vanilla WOR sprite if Tornie Worfinery.png missing
        uiGraphic[UI_MapEditor_Worfinery][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_WOR][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    }
    uiGraphic[UI_MapEditor_LightFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_LightFactory][HOUSE_HARKONNEN][0].get(),2*2*D2_TILESIZE,0,2*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Refinery][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Refinery][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_HighTechFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_HighTechFactory][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_HeavyFactory][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_HeavyFactory][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_RepairYard][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_RepairYard][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,2*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Starport][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Starport][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,3*D2_TILESIZE);
    uiGraphic[UI_MapEditor_Palace][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Palace][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,3*D2_TILESIZE);
    // Tornie: Tech Center = 48x64 PNG, take top 2-tile-tall frame (first of 2 vertical frames).
    if(objPic[ObjPic_TechCenter][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_TechCenter][HOUSE_HARKONNEN] = getSubPicture(
            objPic[ObjPic_TechCenter][HOUSE_HARKONNEN][0].get(), 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE);
    } else {
        // Fallback to vanilla Palace sprite if Tornie TechCenter.png missing
        uiGraphic[UI_MapEditor_TechCenter][HOUSE_HARKONNEN] = getSubPicture(objPic[ObjPic_Palace][HOUSE_HARKONNEN][0].get(),2*3*D2_TILESIZE,0,3*D2_TILESIZE,3*D2_TILESIZE);
    }
    if(objPic[ObjPic_Scoutpost][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_Scoutpost][HOUSE_HARKONNEN] = getSubPicture(
            objPic[ObjPic_Scoutpost][HOUSE_HARKONNEN][0].get(), 2*D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
    } else {
        uiGraphic[UI_MapEditor_Scoutpost][HOUSE_HARKONNEN] =
            getSubPicture(objPic[ObjPic_RocketTurret][HOUSE_HARKONNEN][0].get(), 2*D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
    }

    // Custom structures are prebuilt for every visual colour slot. Install
    // their matching editor previews now so the lazy truecolour UI fallback
    // cannot copy the Harkonnen preview unchanged for another player colour.
    struct TornieEditorStructurePreview {
        unsigned int uiID;
        unsigned int objPicID;
        int x;
        int y;
        int width;
        int height;
    };
    const TornieEditorStructurePreview tornieEditorStructures[] = {
        { UI_MapEditor_AdvancedWindTrap,    ObjPic_AdvancedWindTrap,    2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 3*D2_TILESIZE },
        { UI_MapEditor_AdvancedWindTrapMK2, ObjPic_AdvancedWindTrap2x3, 2*2*D2_TILESIZE, 0, 2*D2_TILESIZE, 3*D2_TILESIZE },
        { UI_MapEditor_AdvancedWindTrapMK3, ObjPic_AdvancedWindTrap3x2, 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_Worfinery,           ObjPic_Worfinery,           2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_TechCenter,          ObjPic_TechCenter,          2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_Scoutpost,           ObjPic_Scoutpost,           2*D2_TILESIZE,   0, D2_TILESIZE,   D2_TILESIZE   },
        { UI_MapEditor_LoveFactory,         ObjPic_LoveFactory,         2*2*D2_TILESIZE, 0, 2*D2_TILESIZE, 3*D2_TILESIZE },
        { UI_MapEditor_ChaosFactory,        ObjPic_ChaosFactory,        2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE }
    };
    for(const auto& preview : tornieEditorStructures) {
        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            // Keep the dedicated Harkonnen editor image when one was loaded.
            if(colorSlot == HOUSE_HARKONNEN && uiGraphic[preview.uiID][colorSlot]) {
                continue;
            }
            SDL_Surface* atlas = objPic[preview.objPicID][colorSlot][0].get();
            if(atlas) {
                uiGraphic[preview.uiID][colorSlot] = getSubPicture(
                    atlas, preview.x, preview.y, preview.width, preview.height);
            }
        }
    }

    sdl2::surface_ptr customMapEditorStar = createCustomMapEditorStar(objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get());
    auto addMapEditorStar = [&](unsigned int uiGraphicID, bool customStar = false) {
        SDL_Surface* starSurface = (customStar && customMapEditorStar) ? customMapEditorStar.get()
                                                                       : objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get();
        if(uiGraphic[uiGraphicID][HOUSE_HARKONNEN] && starSurface) {
            uiGraphic[uiGraphicID][HOUSE_HARKONNEN] = combinePictures(
                uiGraphic[uiGraphicID][HOUSE_HARKONNEN].get(),
                starSurface,
                uiGraphic[uiGraphicID][HOUSE_HARKONNEN]->w - starSurface->w,
                uiGraphic[uiGraphicID][HOUSE_HARKONNEN]->h - starSurface->h);
        }
    };

    uiGraphic[UI_MapEditor_Soldier][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Soldier][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Trooper][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Trooper][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Harvester][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Harvester][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_RebelHarvester][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Harvester][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    addMapEditorStar(UI_MapEditor_RebelHarvester, true);
    uiGraphic[UI_MapEditor_Infantry][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Infantry][HOUSE_HARKONNEN][0].get(),0,0,4,4);
    uiGraphic[UI_MapEditor_Troopers][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Troopers][HOUSE_HARKONNEN][0].get(),0,0,4,4);
    uiGraphic[UI_MapEditor_MCV][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_MCV][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_Trike][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Trike][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Trike][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN] = combinePictures(uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN].get(), objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get(),
                                                                      uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN]->w - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->w,
                                                                      uiGraphic[UI_MapEditor_Raider][HOUSE_HARKONNEN]->h - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->h);
    uiGraphic[UI_MapEditor_Quad][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Quad][HOUSE_HARKONNEN][0].get(),0,0,8,1);
    uiGraphic[UI_MapEditor_Tank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Tank_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 0, 0);
    uiGraphic[UI_MapEditor_SiegeTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Siegetank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Siegetank_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    uiGraphic[UI_MapEditor_Launcher][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Launcher_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    uiGraphic[UI_MapEditor_Devastator][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Devastator_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Devastator_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    uiGraphic[UI_MapEditor_SonicTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Sonictank_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 1);
    auto selectEditorSprite = [&](unsigned int customSprite, unsigned int fallbackSprite) {
        return tornieActive && objPic[customSprite][HOUSE_HARKONNEN][0]
            ? customSprite
            : fallbackSprite;
    };
    const unsigned int deviatorEditorGun =
        selectEditorSprite(ObjPic_DeviatorGunTornie, ObjPic_Launcher_Gun);
    const unsigned int flameTankEditorGun =
        selectEditorSprite(ObjPic_FlameTankGunTornie, ObjPic_Launcher_Gun);
    const unsigned int eliteLauncherEditorGun =
        selectEditorSprite(ObjPic_EliteLauncherGunTornie, ObjPic_Launcher_Gun);
    const unsigned int eliteSiegeTankEditorGun =
        selectEditorSprite(ObjPic_EliteSiegeTankGunTornie, ObjPic_Siegetank_Gun);
    const unsigned int chemicalSiegeTankEditorGun =
        selectEditorSprite(ObjPic_ChemicalSiegeTankGunTornie, ObjPic_Siegetank_Gun);
    uiGraphic[UI_MapEditor_Deviator][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[deviatorEditorGun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    addMapEditorStar(UI_MapEditor_Deviator);
    // Tornie: dedicated sprites for the 3 mod units with their own .png sheets.
// Each is null-guarded so a missing PNG on a partial install doesn't crash
    // the sidebar init — the unit's button just won't have a custom icon (it
    // falls back to whatever the framework draws for an uninitialized SymbolButton).
    if (objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_RocketTrike][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_RocketTrike][HOUSE_HARKONNEN][0].get(),0,0,8,1);
        addMapEditorStar(UI_MapEditor_RocketTrike, true);
    }
    if (objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0]) {
        uiGraphic[UI_MapEditor_SonicTrike][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_SonicTrike][HOUSE_HARKONNEN][0].get(),0,0,8,1);
        addMapEditorStar(UI_MapEditor_SonicTrike, true);
    }
    uiGraphic[UI_MapEditor_FlameTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[flameTankEditorGun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    addMapEditorStar(UI_MapEditor_FlameTank, true);
    uiGraphic[UI_MapEditor_EliteLauncher][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Tank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[eliteLauncherEditorGun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 3, 0);
    addMapEditorStar(UI_MapEditor_EliteLauncher, true);
    uiGraphic[UI_MapEditor_EliteSiegeTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Siegetank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[eliteSiegeTankEditorGun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    addMapEditorStar(UI_MapEditor_EliteSiegeTank, true);
    uiGraphic[UI_MapEditor_ChemicalSiegeTank][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Siegetank_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[chemicalSiegeTankEditorGun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    addMapEditorStar(UI_MapEditor_ChemicalSiegeTank, true);

    // Compose custom vehicle previews one part at a time for every visual
    // colour. This keeps fixed Tornie cannons in their authored colour while
    // allowing the Harvestank and Elite Siege Tank turrets to follow the
    // owning player's colour. It also gives the Harvestank its actual turret
    // in the editor instead of displaying a plain vanilla Harvester.
    auto getColoredEditorFrame = [&](unsigned int objPicID, int colorSlot,
                                     int frameX, int frameY, int framesX, int framesY) -> sdl2::surface_ptr {
        // Tornie custom colour slots must be rebuilt from the indexed Harkonnen
        // atlas. Reusing an eagerly cached custom slot can preserve the authored
        // purple tint in both the map-editor button and sidebar portrait.
        const bool forceCustomRemap = tornieActive
            && (colorSlot == HOUSE_CUSTOM || isCustomHouseColorSlot(colorSlot));
        SDL_Surface* atlas = forceCustomRemap
            ? nullptr
            : objPic[objPicID][colorSlot][0].get();
        sdl2::surface_ptr remappedAtlas;

        if(!atlas) {
            SDL_Surface* harkonnenAtlas = objPic[objPicID][HOUSE_HARKONNEN][0].get();
            if(!harkonnenAtlas) {
                return nullptr;
            }

            if(colorSlot != HOUSE_HARKONNEN && harkonnenAtlas->format->BytesPerPixel == 1) {
                remappedAtlas = mapSurfaceColorRange(
                    harkonnenAtlas, PALCOLOR_HARKONNEN, getVisualRemapPaletteIndex(colorSlot));
                applyCustomVisualColorRamp(remappedAtlas.get(), colorSlot);
                applyRebelsTint(remappedAtlas.get(), colorSlot);
                normalizeTransparentPaletteIndexes(remappedAtlas.get());
                SDL_SetColorKey(remappedAtlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                atlas = remappedAtlas.get();
            } else {
                atlas = harkonnenAtlas;
            }
        }

        return getSubFrame(atlas, frameX, frameY, framesX, framesY);
    };

    auto composeEditorVehicle = [&](unsigned int baseObjPicID, int baseColorSlot,
                                    int gunObjPicID, int gunColorSlot,
                                    int gunOffsetX, int gunOffsetY) -> sdl2::surface_ptr {
        auto base = getColoredEditorFrame(baseObjPicID, baseColorSlot, 0, 0, NUM_ANGLES, 1);
        if(!base || gunObjPicID < 0) {
            return base;
        }

        auto gun = getColoredEditorFrame(static_cast<unsigned int>(gunObjPicID), gunColorSlot,
                                         0, 0, NUM_ANGLES, 1);
        if(!gun) {
            return base;
        }
        return combinePictures(base.get(), gun.get(), gunOffsetX, gunOffsetY);
    };

    auto decorateEditorVehicle = [&](sdl2::surface_ptr vehicle, bool customStar) -> sdl2::surface_ptr {
        SDL_Surface* star = (customStar && customMapEditorStar)
            ? customMapEditorStar.get()
            : objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get();
        if(!vehicle || !star) {
            return vehicle;
        }
        return combinePictures(vehicle.get(), star,
                               vehicle->w - star->w,
                               vehicle->h - star->h);
    };

    for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
        const int fixedTornieGunSlot = tornieActive ? HOUSE_HARKONNEN : colorSlot;

        auto harvestank = composeEditorVehicle(
            ObjPic_Harvester, colorSlot,
            tornieActive ? static_cast<int>(ObjPic_HarvestankGunTornie) : -1,
            colorSlot, 0, 0);
        if(harvestank) {
            uiGraphic[UI_MapEditor_RebelHarvester][colorSlot] =
                decorateEditorVehicle(std::move(harvestank), true);
        }

        auto deviator = composeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(deviatorEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(deviator) {
            uiGraphic[UI_MapEditor_Deviator][colorSlot] =
                decorateEditorVehicle(std::move(deviator), false);
        }

        auto rocketTrike = getColoredEditorFrame(ObjPic_RocketTrike, colorSlot, 0, 0, NUM_ANGLES, 1);
        if(rocketTrike) {
            uiGraphic[UI_MapEditor_RocketTrike][colorSlot] =
                decorateEditorVehicle(std::move(rocketTrike), true);
        }

        auto sonicTrike = getColoredEditorFrame(ObjPic_SonicTrike, colorSlot, 0, 0, NUM_ANGLES, 1);
        if(sonicTrike) {
            uiGraphic[UI_MapEditor_SonicTrike][colorSlot] =
                decorateEditorVehicle(std::move(sonicTrike), true);
        }

        auto flameTank = composeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(flameTankEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(flameTank) {
            uiGraphic[UI_MapEditor_FlameTank][colorSlot] =
                decorateEditorVehicle(std::move(flameTank), true);
        }

        auto eliteLauncher = composeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(eliteLauncherEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(eliteLauncher) {
            uiGraphic[UI_MapEditor_EliteLauncher][colorSlot] =
                decorateEditorVehicle(std::move(eliteLauncher), true);
        }

        auto eliteSiegeTank = composeEditorVehicle(
            ObjPic_Siegetank_Base, colorSlot,
            static_cast<int>(ObjPic_EliteSiegeTankGunTornie), colorSlot, 2, -4);
        if(eliteSiegeTank) {
            uiGraphic[UI_MapEditor_EliteSiegeTank][colorSlot] =
                decorateEditorVehicle(std::move(eliteSiegeTank), true);
        }
        auto chemicalSiegeTank = composeEditorVehicle(
            ObjPic_Siegetank_Base, colorSlot,
            static_cast<int>(ObjPic_ChemicalSiegeTankGunTornie), colorSlot, 2, -4);
        if(chemicalSiegeTank) {
            uiGraphic[UI_MapEditor_ChemicalSiegeTank][colorSlot] =
                decorateEditorVehicle(std::move(chemicalSiegeTank), true);
        }
    }

    uiGraphic[UI_MapEditor_Saboteur][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Saboteur][HOUSE_HARKONNEN][0].get(),0,0,4,3);
    uiGraphic[UI_MapEditor_Sandworm][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Sandworm][HOUSE_HARKONNEN][0].get(),0,5,1,9);
    uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN] = combinePictures(getSubFrame(objPic[ObjPic_Devastator_Base][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), getSubFrame(objPic[ObjPic_Devastator_Gun][HOUSE_HARKONNEN][0].get(),0,0,8,1).get(), 2, -4);
    uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN] = combinePictures(uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN].get(), objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get(),
                                                                  uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN]->w - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->w,
                                                                  uiGraphic[UI_MapEditor_SpecialUnit][HOUSE_HARKONNEN]->h - objPic[ObjPic_Star][HOUSE_HARKONNEN][1]->h);
    uiGraphic[UI_MapEditor_Carryall][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0].get(),0,0,8,2);
    const unsigned int chemicalCarryallEditorSprite =
        objPic[ObjPic_ChemicalCarryall][HOUSE_HARKONNEN][0]
            ? ObjPic_ChemicalCarryall
            : ObjPic_Carryall;
    uiGraphic[UI_MapEditor_ChemicalCarryall][HOUSE_HARKONNEN] = getSubFrame(objPic[chemicalCarryallEditorSprite][HOUSE_HARKONNEN][0].get(),0,0,8,2);
    addMapEditorStar(UI_MapEditor_ChemicalCarryall, true);
    rebuildModDependentEditorGraphics();
    uiGraphic[UI_MapEditor_Ornithopter][HOUSE_HARKONNEN] = getSubFrame(objPic[ObjPic_Ornithopter][HOUSE_HARKONNEN][0].get(),0,0,8,3);

    uiGraphic[UI_MapEditor_Pen1x1][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPen1x1.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Pen1x1][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_Pen3x3][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPen3x3.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Pen3x3][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    uiGraphic[UI_MapEditor_Pen5x5][HOUSE_HARKONNEN] = LoadPNG_RW(pFileManager->openFile("MapEditorPen5x5.png").get());
    SDL_SetColorKey(uiGraphic[UI_MapEditor_Pen5x5][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);

    // DuneCity: map-editor icons for SimCity-style buildings exposed when the
    // city mod is active. Zone atlases are a single 2x2-tile frame each, so
    // we just take the whole surface. Zones are RGBA truecolor (house-agnostic
    // city buildings), so we pre-fill every house slot — the lazy remap in
    // getUIGraphicSurface() assumes palette-indexed surfaces and would corrupt
    // RGBA. NuclearPlant reuses the HighTechFactory icon (same convention used
    // by the in-game detail pic — see sand.cpp) and inherits the standard
    // house-colour remap.
    {
        struct ZoneIcon { int uiID; int objPicID; };
        const ZoneIcon zoneIcons[] = {
            { UI_MapEditor_ZoneResidential, ObjPic_ZoneResidential },
            { UI_MapEditor_ZoneCommercial,  ObjPic_ZoneCommercial  },
            { UI_MapEditor_ZoneIndustrial,  ObjPic_ZoneIndustrial  },
        };
        // Use the same inhabited model as the build menu, in the static row.
        const int iconCellY = 0;
        for (const auto& z : zoneIcons) {
            const int iconCellX = (z.objPicID == ObjPic_ZoneResidential ? 5 : 3) * 2 * D2_TILESIZE;
            for (int h = 0; h < (int)NUM_HOUSES; ++h) {
                uiGraphic[z.uiID][h] = getSubPicture(objPic[z.objPicID][HOUSE_HARKONNEN][0].get(),
                                                    iconCellX, iconCellY,
                                                    2*D2_TILESIZE, 2*D2_TILESIZE);
            }
        }
    }
    // Pull the nuclear icon directly from the Micropolis nuclear-plant atlas
    // — the first 3x3 frame is the static plant. Pre-fill
    // every house slot like the zone icons (sprite is house-agnostic).
    for (int h = 0; h < (int)NUM_HOUSES; ++h) {
        if (objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0]) {
            uiGraphic[UI_MapEditor_NuclearPlant][h] = getSubPicture(objPic[ObjPic_NuclearPlant][HOUSE_HARKONNEN][0].get(), 0, 0, 3*D2_TILESIZE, 3*D2_TILESIZE);
        } else {
            // Fall back to HighTechFactory if the Micropolis PNG is missing.
            uiGraphic[UI_MapEditor_NuclearPlant][h] = getSubPicture(objPic[ObjPic_HighTechFactory][HOUSE_HARKONNEN][0].get(), 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE);
        }
    }

    // Civic atlases are RGBA. Fill every house explicitly, avoiding indexed
    // palette remapping, and take only the static frame at the logical footprint.
    struct CityEditorIcon { int uiID; int objectID; int tiles; };
    for(const auto& icon : {CityEditorIcon{UI_MapEditor_PoliceStation, ObjPic_PoliceStation, 2},
                            CityEditorIcon{UI_MapEditor_Stadium, ObjPic_Stadium, 3},
                            CityEditorIcon{UI_MapEditor_Airport, ObjPic_Airport, 3}}) {
        for(int house = 0; house < NUM_HOUSES; ++house) {
            uiGraphic[icon.uiID][house] = getSubPicture(
                objPic[icon.objectID][HOUSE_HARKONNEN][0].get(), 0, 0,
                icon.tiles * D2_TILESIZE, icon.tiles * D2_TILESIZE);
        }
    }

    // Road icon: pull frame 15 (four-way intersection) from the CityRoad
    // atlas at zoom level 1 (D2_TILESIZE-per-cell) so it matches the size of
    // other 1x1 structure icons (Slab1, Wall). Road is house-agnostic — fill
    // every house slot directly so the palette-remap helper doesn't run on
    // RGBA surfaces.
    for (int h = 0; h < (int)NUM_HOUSES; ++h) {
        if (objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][1]) {
            uiGraphic[UI_MapEditor_Road][h] = getSubPicture(objPic[ObjPic_CityRoad][HOUSE_HARKONNEN][1].get(), 15 * D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
        } else {
            // Fall back to SLAB.WSA-style if the road atlas wasn't built.
            uiGraphic[UI_MapEditor_Road][h] = getSubPicture(objPic[ObjPic_Wall][HOUSE_HARKONNEN][0].get(), 2*D2_TILESIZE, 0, D2_TILESIZE, D2_TILESIZE);
        }
    }


    // load animations
    animation[Anim_HarkonnenEyes] = menshph->getAnimation(0,4,true,true);
    animation[Anim_HarkonnenEyes]->setFrameRate(0.3);
    animation[Anim_HarkonnenMouth] = menshph->getAnimation(5,9,true,true,true);
    animation[Anim_HarkonnenMouth]->setFrameRate(5.0);
    animation[Anim_HarkonnenShoulder] = menshph->getAnimation(10,10,true,true);
    animation[Anim_HarkonnenShoulder]->setFrameRate(1.0);
    animation[Anim_AtreidesEyes] = menshpa->getAnimation(0,4,true,true);
    animation[Anim_AtreidesEyes]->setFrameRate(0.5);
    animation[Anim_AtreidesMouth] = menshpa->getAnimation(5,9,true,true,true);
    animation[Anim_AtreidesMouth]->setFrameRate(5.0);
    animation[Anim_AtreidesShoulder] = menshpa->getAnimation(10,10,true,true);
    animation[Anim_AtreidesShoulder]->setFrameRate(1.0);
    animation[Anim_AtreidesBook] = menshpa->getAnimation(11,12,true,true,true);
    animation[Anim_AtreidesBook]->setNumLoops(1);
    animation[Anim_AtreidesBook]->setFrameRate(0.2);
    animation[Anim_OrdosEyes] = menshpo->getAnimation(0,4,true,true);
    animation[Anim_OrdosEyes]->setFrameRate(0.5);
    animation[Anim_OrdosMouth] = menshpo->getAnimation(5,9,true,true,true);
    animation[Anim_OrdosMouth]->setFrameRate(5.0);
    animation[Anim_OrdosShoulder] = menshpo->getAnimation(10,10,true,true);
    animation[Anim_OrdosShoulder]->setFrameRate(1.0);
    animation[Anim_OrdosRing] = menshpo->getAnimation(11,14,true,true,true);
    animation[Anim_OrdosRing]->setNumLoops(1);
    animation[Anim_OrdosRing]->setFrameRate(6.0);
    animation[Anim_FremenEyes] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesEyes].get());
    animation[Anim_FremenMouth] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesMouth].get());
    animation[Anim_FremenShoulder] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesShoulder].get());
    animation[Anim_FremenBook] = PictureFactory::mapMentatAnimationToFremen(animation[Anim_AtreidesBook].get());
    animation[Anim_SardaukarEyes] = PictureFactory::mapMentatAnimationToSardaukar(animation[Anim_HarkonnenEyes].get());
    animation[Anim_SardaukarMouth] = PictureFactory::mapMentatAnimationToSardaukar(animation[Anim_HarkonnenMouth].get());
    animation[Anim_SardaukarShoulder] = PictureFactory::mapMentatAnimationToSardaukar(animation[Anim_HarkonnenShoulder].get());
    animation[Anim_MercenaryEyes] = PictureFactory::mapMentatAnimationToMercenary(animation[Anim_OrdosEyes].get());
    animation[Anim_MercenaryMouth] = PictureFactory::mapMentatAnimationToMercenary(animation[Anim_OrdosMouth].get());
    animation[Anim_MercenaryShoulder] = PictureFactory::mapMentatAnimationToMercenary(animation[Anim_OrdosShoulder].get());
    animation[Anim_MercenaryRing] = PictureFactory::mapMentatAnimationToMercenary(animation[Anim_OrdosRing].get());

    animation[Anim_BeneEyes] = menshpm->getAnimation(0,4,true,true);
    if(animation[Anim_BeneEyes] != nullptr) {
        animation[Anim_BeneEyes]->setPalette(benePalette);
        animation[Anim_BeneEyes]->setFrameRate(0.5);
    }
    animation[Anim_BeneMouth] = menshpm->getAnimation(5,9,true,true,true);
    if(animation[Anim_BeneMouth] != nullptr) {
        animation[Anim_BeneMouth]->setPalette(benePalette);
        animation[Anim_BeneMouth]->setFrameRate(5.0);
    }
    // the remaining animation are loaded on demand to save some loading time

    // load map choice pieces
    for(int i = 0; i < NUM_MAPCHOICEPIECES; i++) {
        mapChoicePieces[i][HOUSE_HARKONNEN] = Scaler::doubleSurfaceNN(pieces->getPicture(i).get());
        SDL_SetColorKey(mapChoicePieces[i][HOUSE_HARKONNEN].get(), SDL_TRUE, 0);
    }

    // pBackgroundSurface is separate as we never draw it but use it to construct other sprites
    pBackgroundSurface = convertSurfaceToDisplayFormat(PicFactory->createBackground().get());
}

GFXManager::~GFXManager() = default;

static std::unique_ptr<Animation> loadPngStripAnimation(const std::string& filename, int frameCount, double frameRate, bool bDoublePic, int transparentColorKey) {
    if(frameCount <= 0 || !pFileManager->exists(filename)) {
        return nullptr;
    }

    auto strip = LoadPNG_RW(pFileManager->openFile(filename).get());
    if(!strip || strip->w < frameCount || strip->h <= 0) {
        return nullptr;
    }

    const int frameWidth = strip->w / frameCount;
    if(frameWidth <= 0) {
        return nullptr;
    }

    const int extraPixels = strip->w - frameWidth * frameCount;
    int frameStride = frameWidth;
    if(frameCount > 1 && extraPixels > 0 && (extraPixels % (frameCount - 1)) == 0) {
        frameStride += extraPixels / (frameCount - 1);
    }

    auto animation = std::make_unique<Animation>();
    for(int frame = 0; frame < frameCount; frame++) {
        const int sourceX = frame * frameStride;
        if(sourceX + frameWidth > strip->w) {
            return nullptr;
        }
        auto frameSurface = getSubPicture(strip.get(), sourceX, 0, frameWidth, strip->h);
        if(transparentColorKey >= 0) {
            SDL_SetColorKey(frameSurface.get(), SDL_TRUE, static_cast<Uint32>(transparentColorKey));
        }
        animation->addFrame(std::move(frameSurface), bDoublePic, false);
    }
    animation->setFrameRate(frameRate);
    return animation;
}

static void applyRebelsTint(SDL_Surface* surface, int colorSlot) {
    if(!surface || !surface->format || !surface->format->palette
       || !isTornieRebelsColorSlot(colorSlot)) {
        return;
    }

    const int rebelsBase = getVisualRemapPaletteIndex(colorSlot);
    if(rebelsBase < 0 || rebelsBase + 7 >= surface->format->palette->ncolors) {
        return;
    }

    SDL_Color visualRamp[8];
    for(int k = 0; k < 8; ++k) {
        visualRamp[k] = getHouseColorSDL(colorSlot, k);
        visualRamp[k].a = 255;
    }
    SDL_SetPaletteColors(surface->format->palette, visualRamp, rebelsBase, 8);
}
static void applyCustomVisualColorRamp(SDL_Surface* surface, int colorSlot) {
    if(!usesPrivateVisualColorRamp(colorSlot)
       || !surface || !surface->format || !surface->format->palette) {
        return;
    }

    const int targetBase = getVisualRemapPaletteIndex(colorSlot);
    if(targetBase < 0 || targetBase + 7 >= surface->format->palette->ncolors) {
        return;
    }

    SDL_Color visualRamp[8];
    for(int shade = 0; shade < 8; ++shade) {
        visualRamp[shade] = getHouseColorSDL(colorSlot, shade);
    }
    SDL_SetPaletteColors(surface->format->palette, visualRamp, targetBase, 8);
    normalizeTransparentPaletteIndexes(surface);
}

static sdl2::surface_ptr remapTruecolorHouseColorRange(SDL_Surface* source, int colorSlot, int shadeCount) {
    if(!source || !source->format || source->format->BytesPerPixel == 1
       || !isValidHouseColorSlot(colorSlot)) {
        return nullptr;
    }

    const Palette& sourcePalette = palette;
    if(shadeCount < 1 || shadeCount > 8) {
        return nullptr;
    }
    if(sourcePalette.getNumColors() < PALCOLOR_HARKONNEN + shadeCount) {
        return nullptr;
    }

    sdl2::surface_ptr remapped{
        SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0)
    };
    if(!remapped || remapped->format->BytesPerPixel != 4) {
        return nullptr;
    }

    const bool mustLock = SDL_MUSTLOCK(remapped.get());
    if(mustLock && SDL_LockSurface(remapped.get()) != 0) {
        return remapped;
    }

    for(int y = 0; y < remapped->h; ++y) {
        auto* row = reinterpret_cast<Uint32*>(
            static_cast<Uint8*>(remapped->pixels) + y * remapped->pitch);
        for(int x = 0; x < remapped->w; ++x) {
            Uint8 r = 0;
            Uint8 g = 0;
            Uint8 b = 0;
            Uint8 a = 0;
            SDL_GetRGBA(row[x], remapped->format, &r, &g, &b, &a);
            if(a == 0) {
                continue;
            }

            for(int shade = 0; shade < shadeCount; ++shade) {
                const SDL_Color sourceColor = sourcePalette[PALCOLOR_HARKONNEN + shade];
                if(r == sourceColor.r && g == sourceColor.g && b == sourceColor.b) {
                    const SDL_Color targetColor = getHouseColorSDL(colorSlot, shade);
                    row[x] = SDL_MapRGBA(remapped->format,
                                         targetColor.r,
                                         targetColor.g,
                                         targetColor.b,
                                         a);
                    break;
                }
            }
        }
    }

    if(mustLock) {
        SDL_UnlockSurface(remapped.get());
    }
    SDL_SetColorKey(remapped.get(), SDL_FALSE, 0);
    SDL_SetSurfaceBlendMode(remapped.get(), SDL_BLENDMODE_BLEND);
    return remapped;
}
static void preserveOpaqueBlackIndex(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    if(PALCOLOR_TRANSPARENT >= palette->ncolors || PALCOLOR_BLACK >= palette->ncolors) {
        return;
    }

    const SDL_Color transparentIndexColor = palette->colors[PALCOLOR_TRANSPARENT];
    const bool opaqueBlack =
        transparentIndexColor.a >= 128
        && transparentIndexColor.r <= 8
        && transparentIndexColor.g <= 8
        && transparentIndexColor.b <= 8;
    if(!opaqueBlack) {
        return;
    }

    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            if(pixels[x] == PALCOLOR_TRANSPARENT) {
                pixels[x] = PALCOLOR_BLACK;
            }
        }
    }
}

static void normalizeTransparentPaletteIndexes(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    if(PALCOLOR_TRANSPARENT >= palette->ncolors) {
        return;
    }

    bool hasAlphaTransparentIndex = false;
    for(int i = 0; i < palette->ncolors; i++) {
        if(i != PALCOLOR_TRANSPARENT && palette->colors[i].a < 128) {
            hasAlphaTransparentIndex = true;
            break;
        }
    }

    if(hasAlphaTransparentIndex) {
        sdl2::surface_lock lock{ surface };
        for(int y = 0; y < surface->h; y++) {
            Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
            for(int x = 0; x < surface->w; x++) {
                const Uint8 index = pixels[x];
                if(index != PALCOLOR_TRANSPARENT && index < palette->ncolors && palette->colors[index].a < 128) {
                    pixels[x] = PALCOLOR_TRANSPARENT;
                }
            }
        }
    }

    for(int i = 0; i < palette->ncolors; i++) {
        palette->colors[i].a = (i == PALCOLOR_TRANSPARENT) ? 0 : SDL_ALPHA_OPAQUE;
    }
    SDL_SetColorKey(surface, SDL_TRUE, PALCOLOR_TRANSPARENT);
}

static sdl2::surface_ptr convertTornieIndexedSurfaceToRGBA(SDL_Surface* source, const char* label, int house, unsigned int zoom, bool useTextureMask) {
    if(!source || !source->format || !source->format->palette || source->format->BytesPerPixel != 1) {
        return nullptr;
    }

    sdl2::surface_ptr rgba{ SDL_CreateRGBSurfaceWithFormat(0, source->w, source->h, 32, SCREEN_FORMAT) };
    if(!rgba || !rgba->format) {
        SDL_Log("TornieGFX: rgba-convert %s house=%d zoom=%u failed: %s",
                label ? label : "Unknown",
                house,
                zoom,
                SDL_GetError());
        return nullptr;
    }

    Uint32 sourceColorKey = PALCOLOR_TRANSPARENT;
    const bool hasColorKey = SDL_GetColorKey(source, &sourceColorKey) == 0;
    const SDL_Palette* palette = source->format->palette;
    int opaquePixels = 0;
    int transparentPixels = 0;

    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(rgba.get(), SDL_BLENDMODE_NONE);
    {
        sdl2::surface_lock sourceLock{ source };
        sdl2::surface_lock rgbaLock{ rgba.get() };
        for(int y = 0; y < source->h; y++) {
            const Uint8* srcPixels = static_cast<const Uint8*>(sourceLock.pixels()) + y * source->pitch;
            auto* dstPixels = reinterpret_cast<Uint32*>(static_cast<Uint8*>(rgbaLock.pixels()) + y * rgba->pitch);
            for(int x = 0; x < source->w; x++) {
                const Uint8 index = srcPixels[x];
            // Tornie structure previews use the sprite texture as their mask. Keep
            // only keyed/palette-transparent pixels hidden; forcing the full frame
            // opaque makes empty atlas space draw as black rectangles.
            const bool transparent =
                index == PALCOLOR_TRANSPARENT
                || (hasColorKey && index == static_cast<Uint8>(sourceColorKey))
                || index >= palette->ncolors
                || palette->colors[index].a < 128;

                if(transparent) {
                    dstPixels[x] = SDL_MapRGBA(rgba->format, 0, 0, 0, 0);
                    transparentPixels++;
                } else {
                    const SDL_Color color = palette->colors[index];
                    dstPixels[x] = SDL_MapRGBA(rgba->format, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
                    opaquePixels++;
                }
            }
        }
    }

    if(zoom == 0) {
        SDL_Log("TornieGFX: rgba-convert %s house=%d zoom=%u surface=%dx%d opaque=%d transparent=%d mask=%s",
                label ? label : "Unknown",
                house,
                zoom,
                rgba->w,
                rgba->h,
                opaquePixels,
                transparentPixels,
                useTextureMask ? "texture" : "palette");
    }

    return rgba;
}

namespace {
struct TornieSurfaceFrameStats {
    int totalPixels = 0;
    int opaquePixels = 0;
    int colorKeyTransparentPixels = 0;
    int alphaTransparentPixels = 0;
};

Uint32 readSurfacePixelUnchecked(SDL_Surface* surface, int x, int y) {
    Uint8* pixel = static_cast<Uint8*>(surface->pixels) + y * surface->pitch + x * surface->format->BytesPerPixel;

    switch(surface->format->BytesPerPixel) {
        case 1:
            return *pixel;
        case 2:
            return *reinterpret_cast<Uint16*>(pixel);
        case 3:
            if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                return (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
            }
            return pixel[0] | (pixel[1] << 8) | (pixel[2] << 16);
        case 4:
            return *reinterpret_cast<Uint32*>(pixel);
        default:
            return 0;
    }
}

int getTornieDiagnosticFrameCount(SDL_Surface* surface, int frameWidth, int frameHeight) {
    if(!surface || frameWidth <= 0 || frameHeight <= 0) {
        return 0;
    }

    if(surface->w >= 2 * frameWidth && surface->h >= frameHeight && surface->h < 2 * frameHeight) {
        return std::max(1, surface->w / frameWidth);
    }

    if(surface->h >= 2 * frameHeight) {
        return std::max(1, surface->h / frameHeight);
    }

    return 1;
}

SDL_Rect getTornieDiagnosticFrameRect(SDL_Surface* surface, int frameWidth, int frameHeight, int frame) {
    if(!surface || frameWidth <= 0 || frameHeight <= 0) {
        return SDL_Rect{0, 0, 0, 0};
    }

    const bool horizontal =
        surface->w >= 2 * frameWidth
        && surface->h >= frameHeight
        && surface->h < 2 * frameHeight;
    const int frameCount = getTornieDiagnosticFrameCount(surface, frameWidth, frameHeight);
    const int clampedFrame = std::max(0, std::min(frame, std::max(0, frameCount - 1)));

    if(horizontal) {
        const int sourceX = clampedFrame * frameWidth;
        return SDL_Rect{ sourceX, 0, std::min(frameWidth, std::max(0, surface->w - sourceX)), std::min(frameHeight, surface->h) };
    }

    const int sourceY = (frameCount > 1) ? clampedFrame * frameHeight : 0;
    return SDL_Rect{ 0, sourceY, std::min(frameWidth, surface->w), std::min(frameHeight, std::max(0, surface->h - sourceY)) };
}

TornieSurfaceFrameStats collectTornieSurfaceFrameStats(SDL_Surface* surface, SDL_Rect rect) {
    TornieSurfaceFrameStats stats;
    if(!surface || !surface->format || rect.w <= 0 || rect.h <= 0) {
        return stats;
    }

    const int xStart = std::max(0, rect.x);
    const int yStart = std::max(0, rect.y);
    const int xEnd = std::min(surface->w, rect.x + rect.w);
    const int yEnd = std::min(surface->h, rect.y + rect.h);
    if(xStart >= xEnd || yStart >= yEnd) {
        return stats;
    }

    Uint32 colorKey = 0;
    const bool hasColorKey = SDL_GetColorKey(surface, &colorKey) == 0;
    SDL_Palette* palette = surface->format->palette;

    sdl2::surface_lock lock{ surface };
    for(int y = yStart; y < yEnd; y++) {
        for(int x = xStart; x < xEnd; x++) {
            const Uint32 pixel = readSurfacePixelUnchecked(surface, x, y);
            stats.totalPixels++;

            bool transparent = false;
            if(hasColorKey && pixel == colorKey) {
                stats.colorKeyTransparentPixels++;
                transparent = true;
            }

            if(palette && pixel < static_cast<Uint32>(palette->ncolors)) {
                if(palette->colors[pixel].a < 128) {
                    stats.alphaTransparentPixels++;
                    transparent = true;
                }
            } else if(!palette) {
                Uint8 r = 0;
                Uint8 g = 0;
                Uint8 b = 0;
                Uint8 a = 255;
                SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);
                if(a < 128) {
                    stats.alphaTransparentPixels++;
                    transparent = true;
                }
            }

            if(!transparent) {
                stats.opaquePixels++;
            }
        }
    }

    return stats;
}
}

static bool isTornieStructureObjPic(unsigned int id) {
    return id == ObjPic_AdvancedWindTrap
        || id == ObjPic_AdvancedWindTrap2x3
        || id == ObjPic_AdvancedWindTrap3x2
        || id == ObjPic_Worfinery
        || id == ObjPic_TechCenter
        || id == ObjPic_Scoutpost || id == ObjPic_LoveFactory;
}

static const char* getTornieStructureObjPicName(unsigned int id) {
    switch(id) {
        case ObjPic_AdvancedWindTrap:    return "AdvancedWindTrap3x3";
        case ObjPic_AdvancedWindTrap2x3: return "AdvancedWindTrap2x3";
        case ObjPic_AdvancedWindTrap3x2: return "AdvancedWindTrap3x2";
        case ObjPic_Worfinery:           return "Worfinery";
        case ObjPic_TechCenter:          return "TechCenter";
        case ObjPic_Scoutpost:           return "Scoutpost";
        case ObjPic_LoveFactory:         return "LoveFactory";
        default:                         return "Unknown";
    }
}

static void logTornieStructureSurfaceDiagnostics(const char* stage, const char* label, SDL_Surface* surface, int frameWidth, int frameHeight) {
    if(!surface) {
        SDL_Log("TornieGFX: %s %s surface=null", stage, label);
        return;
    }

    Uint32 colorKey = 0;
    const bool hasColorKey = SDL_GetColorKey(surface, &colorKey) == 0;
    SDL_BlendMode blendMode = SDL_BLENDMODE_NONE;
    SDL_GetSurfaceBlendMode(surface, &blendMode);
    const int paletteColors = (surface->format && surface->format->palette) ? surface->format->palette->ncolors : 0;
    const int frameCount = getTornieDiagnosticFrameCount(surface, frameWidth, frameHeight);

    SDL_Log("TornieGFX: %s %s surface=%dx%d bpp=%d bytes=%d paletteColors=%d colorKey=%s/%u blend=%d frameSize=%dx%d frames=%d",
            stage,
            label,
            surface->w,
            surface->h,
            surface->format ? surface->format->BitsPerPixel : 0,
            surface->format ? surface->format->BytesPerPixel : 0,
            paletteColors,
            hasColorKey ? "yes" : "no",
            hasColorKey ? colorKey : 0,
            static_cast<int>(blendMode),
            frameWidth,
            frameHeight,
            frameCount);

    if(surface->format && surface->format->palette && PALCOLOR_TRANSPARENT < surface->format->palette->ncolors) {
        const SDL_Color transparent = surface->format->palette->colors[PALCOLOR_TRANSPARENT];
        const SDL_Color opaqueBlack = (PALCOLOR_BLACK < surface->format->palette->ncolors)
            ? surface->format->palette->colors[PALCOLOR_BLACK]
            : SDL_Color{0, 0, 0, 255};
        SDL_Log("TornieGFX: %s %s palette transparent[%d]=(%u,%u,%u,%u) black[%d]=(%u,%u,%u,%u)",
                stage,
                label,
                PALCOLOR_TRANSPARENT,
                transparent.r,
                transparent.g,
                transparent.b,
                transparent.a,
                PALCOLOR_BLACK,
                opaqueBlack.r,
                opaqueBlack.g,
                opaqueBlack.b,
                opaqueBlack.a);
    }

    for(int frame = 0; frame < std::min(frameCount, 8); frame++) {
        SDL_Rect rect = getTornieDiagnosticFrameRect(surface, frameWidth, frameHeight, frame);
        TornieSurfaceFrameStats stats = collectTornieSurfaceFrameStats(surface, rect);
        SDL_Log("TornieGFX: %s %s frame=%d rect=(%d,%d,%d,%d) total=%d opaque=%d keyTransparent=%d alphaTransparent=%d",
                stage,
                label,
                frame,
                rect.x,
                rect.y,
                rect.w,
                rect.h,
                stats.totalPixels,
                stats.opaquePixels,
                stats.colorKeyTransparentPixels,
                stats.alphaTransparentPixels);

        if(stats.totalPixels == 0 || stats.opaquePixels == 0) {
            SDL_Log("TornieGFX: WARNING %s %s frame=%d is empty or fully transparent", stage, label, frame);
        }
    }
}

static int findNearestPaletteIndex(const SDL_Palette* palette, const SDL_Color color) {
    if(!palette || palette->ncolors <= 1) {
        return PALCOLOR_TRANSPARENT;
    }

    int bestIndex = std::min(PALCOLOR_BLACK, palette->ncolors - 1);
    int bestDistance = 1 << 30;
    for(int i = 1; i < palette->ncolors; i++) {
        const SDL_Color candidate = palette->colors[i];
        const int dr = static_cast<int>(candidate.r) - static_cast<int>(color.r);
        const int dg = static_cast<int>(candidate.g) - static_cast<int>(color.g);
        const int db = static_cast<int>(candidate.b) - static_cast<int>(color.b);
        const int distance = dr * dr + dg * dg + db * db;
        if(distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
            if(distance == 0) {
                break;
            }
        }
    }

    return bestIndex;
}

static sdl2::surface_ptr convertTruecolorSurfaceToPalette(SDL_Surface* source, const SDL_Palette* targetPalette, int reservedIndex, SDL_Color reservedColor) {
    if(!source || !source->format || source->format->BytesPerPixel == 1 || !targetPalette) {
        return nullptr;
    }

    sdl2::surface_ptr indexed{ SDL_CreateRGBSurface(0, source->w, source->h, 8, 0, 0, 0, 0) };
    if(!indexed || !indexed->format || !indexed->format->palette) {
        return nullptr;
    }

    const int colorCount = std::min(targetPalette->ncolors, indexed->format->palette->ncolors);
    SDL_SetPaletteColors(indexed->format->palette, targetPalette->colors, 0, colorCount);
    for(int i = 0; i < indexed->format->palette->ncolors; i++) {
        indexed->format->palette->colors[i].a =
            (i == PALCOLOR_TRANSPARENT) ? SDL_ALPHA_TRANSPARENT : SDL_ALPHA_OPAQUE;
    }

    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(indexed.get(), SDL_BLENDMODE_NONE);
    {
        sdl2::surface_lock sourceLock{ source };
        sdl2::surface_lock indexedLock{ indexed.get() };
        for(int y = 0; y < source->h; y++) {
            Uint8* destination = static_cast<Uint8*>(indexedLock.pixels()) + y * indexed->pitch;
            for(int x = 0; x < source->w; x++) {
                const Uint32 pixel = readSurfacePixelUnchecked(source, x, y);
                SDL_Color color{};
                SDL_GetRGBA(pixel, source->format, &color.r, &color.g, &color.b, &color.a);
                if(color.a < 128) {
                    destination[x] = static_cast<Uint8>(PALCOLOR_TRANSPARENT);
                } else if(reservedIndex > PALCOLOR_TRANSPARENT
                          && reservedIndex < targetPalette->ncolors
                          && color.r == reservedColor.r
                          && color.g == reservedColor.g
                          && color.b == reservedColor.b) {
                    destination[x] = static_cast<Uint8>(reservedIndex);
                } else {
                    destination[x] = static_cast<Uint8>(findNearestPaletteIndex(targetPalette, color));
                }
            }
        }
    }

    SDL_SetColorKey(indexed.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
    return indexed;
}
static sdl2::surface_ptr generateTornieWindtrapAnimationFrames(SDL_Surface* windtrapPic) {
    constexpr int sourceFrameCount = 4;
    if(!windtrapPic || !windtrapPic->format || !windtrapPic->format->palette
       || windtrapPic->format->BytesPerPixel != 1
       || windtrapPic->w <= 0 || windtrapPic->h <= 0
       || (windtrapPic->w % sourceFrameCount) != 0) {
        SDL_Log("TornieGFX: cannot generate Advanced Windtrap animation from an invalid indexed atlas");
        return nullptr;
    }

    const int frameWidth = windtrapPic->w / sourceFrameCount;
    const int frameHeight = windtrapPic->h;
    const int frameColumns = NUM_WINDTRAP_ANIMATIONS_PER_ROW;
    const int totalFrames = 2 + NUM_WINDTRAP_ANIMATIONS;
    const int frameRows = (totalFrames + frameColumns - 1) / frameColumns;
    const int sizeX = frameColumns * frameWidth;
    const int sizeY = frameRows * frameHeight;

    sdl2::surface_ptr animation{
        SDL_CreateRGBSurface(0, sizeX, sizeY, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK)
    };
    if(!animation || animation->format->BytesPerPixel < 3) {
        SDL_Log("TornieGFX: cannot allocate Advanced Windtrap animation atlas");
        return nullptr;
    }

    SDL_SetSurfaceBlendMode(animation.get(), SDL_BLENDMODE_NONE);
    SDL_FillRect(animation.get(), nullptr, SDL_MapRGBA(animation->format, 0, 0, 0, 0));

    sdl2::surface_lock sourceLock{windtrapPic};
    sdl2::surface_lock destinationLock{animation.get()};
    const auto* sourcePixels = static_cast<const Uint8*>(sourceLock.pixels());
    auto* destinationPixels = static_cast<Uint8*>(destinationLock.pixels());

    const auto writePixel = [&](int x, int y, Uint32 pixel) {
        Uint8* address = destinationPixels
                       + y * animation->pitch
                       + x * animation->format->BytesPerPixel;
        switch(animation->format->BytesPerPixel) {
            case 2:
                *reinterpret_cast<Uint16*>(address) = static_cast<Uint16>(pixel);
                break;
            case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                address[0] = static_cast<Uint8>((pixel >> 16) & 0xFF);
                address[1] = static_cast<Uint8>((pixel >> 8) & 0xFF);
                address[2] = static_cast<Uint8>(pixel & 0xFF);
#else
                address[0] = static_cast<Uint8>(pixel & 0xFF);
                address[1] = static_cast<Uint8>((pixel >> 8) & 0xFF);
                address[2] = static_cast<Uint8>((pixel >> 16) & 0xFF);
#endif
                break;
            case 4:
                *reinterpret_cast<Uint32*>(address) = pixel;
                break;
            default:
                break;
        }
    };

    const auto copyFrame = [&](int sourceFrame, int destinationFrame, const SDL_Color* cycleColor) {
        const int sourceX = sourceFrame * frameWidth;
        const int destinationX = (destinationFrame % frameColumns) * frameWidth;
        const int destinationY = (destinationFrame / frameColumns) * frameHeight;

        for(int y = 0; y < frameHeight; ++y) {
            const Uint8* sourceRow = sourcePixels + y * windtrapPic->pitch + sourceX;
            for(int x = 0; x < frameWidth; ++x) {
                const Uint8 paletteIndex = sourceRow[x];
                if(paletteIndex == PALCOLOR_TRANSPARENT
                   || paletteIndex >= windtrapPic->format->palette->ncolors) {
                    continue;
                }

                SDL_Color color = windtrapPic->format->palette->colors[paletteIndex];
                if(cycleColor != nullptr && paletteIndex == PALCOLOR_WINDTRAP_COLORCYCLE) {
                    color = *cycleColor;
                }
                const Uint32 pixel = SDL_MapRGBA(
                    animation->format, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
                writePixel(destinationX + x, destinationY + y, pixel);
            }
        }
    };

    copyFrame(0, 0, nullptr);
    copyFrame(1, 1, nullptr);

    const int colorQuantizer = 255 / std::max(1, (NUM_WINDTRAP_ANIMATIONS / 2) - 2);
    for(int i = 0; i < NUM_WINDTRAP_ANIMATIONS; ++i) {
        SDL_Color cycleColor{};
        if(i < NUM_WINDTRAP_ANIMATIONS / 2) {
            const int value = i * colorQuantizer;
            cycleColor.r = static_cast<Uint8>(std::min(80, value));
            cycleColor.g = static_cast<Uint8>(std::min(80, value));
            cycleColor.b = static_cast<Uint8>(std::min(255, value));
        } else {
            const int value = (i - NUM_WINDTRAP_ANIMATIONS / 2) * colorQuantizer;
            cycleColor.r = static_cast<Uint8>(std::max(0, 80 - value));
            cycleColor.g = static_cast<Uint8>(std::max(0, 80 - value));
            cycleColor.b = static_cast<Uint8>(std::max(0, 255 - value));
        }
        cycleColor.a = SDL_ALPHA_OPAQUE;

        const int sourceFrame = ((i / 3) % 2 == 0) ? 2 : 3;
        copyFrame(sourceFrame, 2 + i, &cycleColor);
    }

    SDL_SetSurfaceBlendMode(animation.get(), SDL_BLENDMODE_BLEND);
    return animation;
}

static sdl2::surface_ptr remapIndexedSurfaceToPalette(SDL_Surface* source, const SDL_Palette* targetPalette) {
    if(!source || !source->format || !source->format->palette || source->format->BytesPerPixel != 1 || !targetPalette) {
        return nullptr;
    }

    sdl2::surface_ptr remapped{ SDL_CreateRGBSurface(0, source->w, source->h, 8, 0, 0, 0, 0) };
    if(!remapped || !remapped->format || !remapped->format->palette) {
        return nullptr;
    }

    SDL_SetPaletteColors(remapped->format->palette,
                         targetPalette->colors,
                         0,
                         std::min(targetPalette->ncolors, remapped->format->palette->ncolors));
    for(int i = 0; i < remapped->format->palette->ncolors; i++) {
        remapped->format->palette->colors[i].a = (i == PALCOLOR_TRANSPARENT) ? 0 : SDL_ALPHA_OPAQUE;
    }

    Uint32 sourceColorKey = PALCOLOR_TRANSPARENT;
    const bool hasSourceColorKey = (SDL_GetColorKey(source, &sourceColorKey) == 0);
    Uint8 indexMap[256];
    for(int i = 0; i < 256; i++) {
        indexMap[i] = static_cast<Uint8>(PALCOLOR_TRANSPARENT);
    }

    const SDL_Palette* sourcePalette = source->format->palette;
    const int sourceColors = std::min(sourcePalette->ncolors, 256);
    for(int i = 0; i < sourceColors; i++) {
        if(i == PALCOLOR_TRANSPARENT
           || (hasSourceColorKey && i == static_cast<int>(sourceColorKey))
           || sourcePalette->colors[i].a < 128) {
            indexMap[i] = static_cast<Uint8>(PALCOLOR_TRANSPARENT);
            continue;
        }

        indexMap[i] = static_cast<Uint8>(findNearestPaletteIndex(targetPalette, sourcePalette->colors[i]));
    }

    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(remapped.get(), SDL_BLENDMODE_NONE);
    {
        sdl2::surface_lock sourceLock{ source };
        sdl2::surface_lock remappedLock{ remapped.get() };
        for(int y = 0; y < source->h; y++) {
            const Uint8* srcPixels = static_cast<const Uint8*>(sourceLock.pixels()) + y * source->pitch;
            Uint8* dstPixels = static_cast<Uint8*>(remappedLock.pixels()) + y * remapped->pitch;
            for(int x = 0; x < source->w; x++) {
                dstPixels[x] = indexMap[srcPixels[x]];
            }
        }
    }

    SDL_SetColorKey(remapped.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
    return remapped;
}

static int getNearestHarkonnenShade(const SDL_Palette* palette, int brightness) {
    if(!palette) {
        return 0;
    }

    int bestShade = 0;
    int bestDistance = 256;
    for(int shade = 0; shade < 7; ++shade) {
        const int paletteIndex = PALCOLOR_HARKONNEN + shade;
        if(paletteIndex >= palette->ncolors) {
            break;
        }

        const SDL_Color rampColor = palette->colors[paletteIndex];
        const int rampBrightness = std::max(std::max(static_cast<int>(rampColor.r),
                                                     static_cast<int>(rampColor.g)),
                                            static_cast<int>(rampColor.b));
        const int distance = std::abs(brightness - rampBrightness);
        if(distance < bestDistance) {
            bestDistance = distance;
            bestShade = shade;
        }
    }

    return bestShade;
}

static void normalizeTornieStructureTeamPaintToHarkonnen(SDL_Surface* surface, unsigned int objPicID) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            Uint8& index = pixels[x];
            if(index == PALCOLOR_TRANSPARENT || index >= palette->ncolors) {
                continue;
            }
            if(index >= PALCOLOR_HARKONNEN && index < PALCOLOR_HARKONNEN + 7) {
                continue;
            }

            const SDL_Color color = palette->colors[index];
            const int r = static_cast<int>(color.r);
            const int g = static_cast<int>(color.g);
            const int b = static_cast<int>(color.b);
            bool teamPaint = false;

            switch(objPicID) {
                case ObjPic_Worfinery:
                    // The refinery machinery uses an orange-to-brown team ramp.
                    teamPaint = r >= 56 && r >= g + 24 && r >= b + 24;
                    break;

                case ObjPic_TechCenter:
                    // Only the small red/brown Harkonnen flags are team paint.
                    teamPaint = r >= 96 && r >= g + 40 && r >= b + 32;
                    break;

                case ObjPic_Scoutpost:
                    // Preserve the green tower; recolor only its red/brown flags.
                    teamPaint = r >= 96 && r >= g + 40 && r >= b + 32;
                    break;

                case ObjPic_AdvancedWindTrap:
                    // Preserve the magenta machinery; only the Harkonnen-red flags vary.
                    teamPaint = r >= 96 && r >= g + 40 && r >= b + 32;
                    break;

                case ObjPic_AdvancedWindTrap2x3:
                case ObjPic_AdvancedWindTrap3x2:
                    // The compact variants use red/magenta team paint.
                    teamPaint = r >= 48 && r >= g + 24 && r >= b + 20;
                    break;

                default:
                    break;
            }

            if(!teamPaint) {
                continue;
            }

            const int brightness = std::max(std::max(r, g), b);
            const int shade = getNearestHarkonnenShade(palette, brightness);
            index = static_cast<Uint8>(PALCOLOR_HARKONNEN + shade);
        }
    }
}

static void normalizeHouseColorRangesToHarkonnen(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    static const int houseColorBases[] = { PALCOLOR_HARKONNEN };

    SDL_Palette* palette = surface->format->palette;
    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            Uint8& index = pixels[x];
            if(index == PALCOLOR_TRANSPARENT || index >= palette->ncolors) {
                continue;
            }

            for(const int colorBase : houseColorBases) {
                if(index >= colorBase && index < colorBase + 7) {
                    const int shade = index - colorBase;
                    index = static_cast<Uint8>(PALCOLOR_HARKONNEN + shade);
                    break;
                }
            }
        }
    }
}

static void normalizeHarkonnenTeamRed(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            Uint8& index = pixels[x];
            if(index == PALCOLOR_TRANSPARENT || index >= palette->ncolors) {
                continue;
            }
            if(index >= PALCOLOR_HARKONNEN && index < PALCOLOR_HARKONNEN + 7) {
                continue;
            }

            const SDL_Color color = palette->colors[index];
            const int r = static_cast<int>(color.r);
            const int g = static_cast<int>(color.g);
            const int b = static_cast<int>(color.b);
            const int strongestOther = std::max(g, b);
            const bool redTeamPaint = r >= 70 && r > strongestOther + 16 && g < 120 && b < 120;
            const bool darkRustTeamPaint = r >= 85 && r > g + 8 && r > b + 8 && g < 95 && b < 85;
            if(!redTeamPaint && !darkRustTeamPaint) {
                continue;
            }

            const int brightness = std::max(std::max(r, g), b);
            const int shade = getNearestHarkonnenShade(palette, brightness);
            index = static_cast<Uint8>(PALCOLOR_HARKONNEN + shade);
        }
    }
}

static void normalizeLooseTeamPaintToHarkonnen(SDL_Surface* surface) {
    if(!surface || !surface->format || !surface->format->palette || surface->format->BytesPerPixel != 1) {
        return;
    }

    SDL_Palette* palette = surface->format->palette;
    sdl2::surface_lock lock{ surface };
    for(int y = 0; y < surface->h; y++) {
        Uint8* pixels = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; x++) {
            Uint8& index = pixels[x];
            if(index == PALCOLOR_TRANSPARENT || index >= palette->ncolors) {
                continue;
            }
            if(index >= PALCOLOR_HARKONNEN && index < PALCOLOR_HARKONNEN + 7) {
                continue;
            }

            const SDL_Color color = palette->colors[index];
            const int r = static_cast<int>(color.r);
            const int g = static_cast<int>(color.g);
            const int b = static_cast<int>(color.b);
            const bool redPaint = r >= 70 && r > std::max(g, b) + 16 && g < 130 && b < 130;
            const bool greenPaint = g >= 70 && g > std::max(r, b) + 14 && r < 150 && b < 150;
            if(!redPaint && !greenPaint) {
                continue;
            }

            const int brightness = std::max(std::max(r, g), b);
            const int shade = getNearestHarkonnenShade(palette, brightness);
            index = static_cast<Uint8>(PALCOLOR_HARKONNEN + shade);
        }
    }
}

static SDL_Color createSpiceTintColor(SDL_Color base, SDL_Color tint) {
    const int brightness = std::max(std::max(static_cast<int>(base.r), static_cast<int>(base.g)),
                                    static_cast<int>(base.b));
    const int scale = std::clamp(brightness, 48, 176);

    SDL_Color color{};
    color.r = static_cast<Uint8>(std::min(220, (static_cast<int>(tint.r) * scale) / 160));
    color.g = static_cast<Uint8>(std::min(220, (static_cast<int>(tint.g) * scale) / 160));
    color.b = static_cast<Uint8>(std::min(220, (static_cast<int>(tint.b) * scale) / 160));
    color.a = base.a;
    return color;
}

static bool paletteColorsEqual(SDL_Surface* aSurface, Uint8 aIndex, SDL_Surface* bSurface, Uint8 bIndex) {
    if(!aSurface || !bSurface || !aSurface->format || !bSurface->format
       || !aSurface->format->palette || !bSurface->format->palette) {
        return aIndex == bIndex;
    }

    if(aIndex >= aSurface->format->palette->ncolors || bIndex >= bSurface->format->palette->ncolors) {
        return aIndex == bIndex;
    }

    const SDL_Color a = aSurface->format->palette->colors[aIndex];
    const SDL_Color b = bSurface->format->palette->colors[bIndex];
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static sdl2::surface_ptr createIndependentRGBACopy(SDL_Surface* source) {
    if(!source) {
        return nullptr;
    }

    sdl2::surface_ptr converted{ SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0) };
    if(!converted) {
        return copySurface(source);
    }

    SDL_BlendMode blendMode;
    if(SDL_GetSurfaceBlendMode(source, &blendMode) == 0) {
        SDL_SetSurfaceBlendMode(converted.get(), blendMode);
    }
    return converted;
}

static void collectUsedPaletteIndices(SDL_Surface* surface, bool used[256]) {
    for(int i = 0; i < 256; ++i) {
        used[i] = false;
    }

    if(!surface || !surface->format || surface->format->BytesPerPixel != 1) {
        return;
    }

    sdl2::surface_lock lock{surface};
    for(int y = 0; y < surface->h; ++y) {
        const Uint8* pixels = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch;
        for(int x = 0; x < surface->w; ++x) {
            used[pixels[x]] = true;
        }
    }
}

static int allocatePaletteIndex(SDL_Surface* surface, bool used[256]) {
    if(!surface || !surface->format || !surface->format->palette) {
        return -1;
    }

    const int colorCount = std::min(surface->format->palette->ncolors, 256);
    for(int i = colorCount - 1; i > PALCOLOR_TRANSPARENT; --i) {
        if(!used[i]) {
            used[i] = true;
            return i;
        }
    }
    return -1;
}

static void remapPixelToTint(SDL_Surface* surface, Uint8& pixel, SDL_Color tint,
                             bool used[256], int remap[256]) {
    if(!surface || !surface->format || !surface->format->palette
       || pixel == PALCOLOR_TRANSPARENT || pixel >= surface->format->palette->ncolors) {
        return;
    }

    if(remap[pixel] < 0) {
        const SDL_Color color = createSpiceTintColor(surface->format->palette->colors[pixel], tint);
        const int paletteIndex = allocatePaletteIndex(surface, used);
        if(paletteIndex >= 0) {
            SDL_SetPaletteColors(surface->format->palette, &color, paletteIndex, 1);
            remap[pixel] = paletteIndex;
        } else {
            SDL_SetPaletteColors(surface->format->palette, &color, pixel, 1);
            remap[pixel] = pixel;
        }
    }
    pixel = static_cast<Uint8>(remap[pixel]);
}

static void tintTerrainSpiceTilesIndexed(SDL_Surface* surface,
                                         int firstTile,
                                         int tileCount,
                                         SDL_Color tint,
                                         bool used[256],
                                         int remap[256]) {
    if(!surface || !surface->format || surface->format->BytesPerPixel != 1
       || surface->w <= 0 || surface->h <= 0) {
        return;
    }

    constexpr int TerrainTile_Sand = 0x03;
    const int tileWidth = surface->w / NUM_TERRAIN_TILES_X;
    const int tileHeight = surface->h / NUM_TERRAIN_TILES_Y;
    if(tileWidth <= 0 || tileHeight <= 0) {
        return;
    }

    const int sandX = (TerrainTile_Sand % NUM_TERRAIN_TILES_X) * tileWidth;
    const int sandY = (TerrainTile_Sand / NUM_TERRAIN_TILES_X) * tileHeight;
    sdl2::surface_lock lock{surface};
    for(int tileOffset = 0; tileOffset < tileCount; ++tileOffset) {
        const int tile = firstTile + tileOffset;
        const int tileX = (tile % NUM_TERRAIN_TILES_X) * tileWidth;
        const int tileY = (tile / NUM_TERRAIN_TILES_X) * tileHeight;
        for(int y = 0; y < tileHeight; ++y) {
            Uint8* row = static_cast<Uint8*>(surface->pixels) + (tileY + y) * surface->pitch;
            const Uint8* sandRow = static_cast<const Uint8*>(surface->pixels)
                                   + (sandY + y) * surface->pitch;
            for(int x = 0; x < tileWidth; ++x) {
                Uint8& pixel = row[tileX + x];
                const Uint8 sandPixel = sandRow[sandX + x];
                if(pixel != PALCOLOR_TRANSPARENT
                   && !paletteColorsEqual(surface, pixel, surface, sandPixel)) {
                    remapPixelToTint(surface, pixel, tint, used, remap);
                }
            }
        }
    }
}

static sdl2::surface_ptr createIndexedTintedTerrainSpiceSurface(
        SDL_Surface* source, SDL_Color thinTint, SDL_Color thickTint) {
    if(!source) {
        return nullptr;
    }

    auto tinted = copySurface(source);
    if(!tinted || !tinted->format || !tinted->format->palette
       || tinted->format->BytesPerPixel != 1) {
        return tinted;
    }

    bool used[256];
    collectUsedPaletteIndices(tinted.get(), used);
    int remap[256];
    std::fill(remap, remap + 256, -1);

    constexpr int TerrainTile_Spice = 0x34;
    constexpr int TerrainTile_ThickSpice = 0x44;
    constexpr int TerrainTile_SpiceBloom = 0x54;
    tintTerrainSpiceTilesIndexed(tinted.get(), TerrainTile_Spice, 16, thinTint, used, remap);
    std::fill(remap, remap + 256, -1);
    tintTerrainSpiceTilesIndexed(tinted.get(), TerrainTile_ThickSpice, 16, thickTint, used, remap);
    std::fill(remap, remap + 256, -1);
    tintTerrainSpiceTilesIndexed(tinted.get(), TerrainTile_SpiceBloom, 1, thinTint, used, remap);

    Uint32 colorKey = 0;
    if(SDL_GetColorKey(source, &colorKey) == 0) {
        SDL_SetColorKey(tinted.get(), SDL_TRUE, colorKey);
    }
    return tinted;
}

static sdl2::surface_ptr createIndexedTintedMapEditorIcon(
        SDL_Surface* source, SDL_Surface* sand, SDL_Color tint) {
    if(!source) {
        return nullptr;
    }

    auto tinted = copySurface(source);
    if(!tinted || !tinted->format || !tinted->format->palette
       || tinted->format->BytesPerPixel != 1
       || !sand || !sand->format || sand->format->BytesPerPixel != 1) {
        return tinted;
    }

    bool used[256];
    collectUsedPaletteIndices(tinted.get(), used);
    int remap[256];
    std::fill(remap, remap + 256, -1);

    sdl2::surface_lock tintedLock{tinted.get()};
    sdl2::surface_lock sandLock{sand};
    const int width = std::min(tinted->w, sand->w);
    const int height = std::min(tinted->h, sand->h);
    for(int y = 0; y < height; ++y) {
        Uint8* row = static_cast<Uint8*>(tinted->pixels) + y * tinted->pitch;
        const Uint8* sandRow = static_cast<const Uint8*>(sand->pixels) + y * sand->pitch;
        for(int x = 0; x < width; ++x) {
            Uint8& pixel = row[x];
            const Uint8 sandPixel = sandRow[x];
            if(pixel != PALCOLOR_TRANSPARENT
               && !paletteColorsEqual(tinted.get(), pixel, sand, sandPixel)) {
                remapPixelToTint(tinted.get(), pixel, tint, used, remap);
            }
        }
    }

    Uint32 colorKey = 0;
    if(SDL_GetColorKey(source, &colorKey) == 0) {
        SDL_SetColorKey(tinted.get(), SDL_TRUE, colorKey);
    }
    return tinted;
}

static void tintTerrainSpiceTilesRGBA(SDL_Surface* source,
                                      SDL_Surface* target,
                                      int firstTile,
                                      int tileCount,
                                      SDL_Color tint) {
    if(!source || !target || !source->format || !source->format->palette
       || source->format->BytesPerPixel != 1 || target->format->BytesPerPixel != 4
       || source->w <= 0 || source->h <= 0) {
        return;
    }

    constexpr int TerrainTile_Sand = 0x03;
    const int tileWidth = source->w / NUM_TERRAIN_TILES_X;
    const int tileHeight = source->h / NUM_TERRAIN_TILES_Y;
    if(tileWidth <= 0 || tileHeight <= 0) {
        return;
    }

    const int sandX = (TerrainTile_Sand % NUM_TERRAIN_TILES_X) * tileWidth;
    const int sandY = (TerrainTile_Sand / NUM_TERRAIN_TILES_X) * tileHeight;
    Uint32 sourceColorKey = 0;
    const bool hasColorKey = SDL_GetColorKey(source, &sourceColorKey) == 0;

    sdl2::surface_lock sourceLock{ source };
    sdl2::surface_lock targetLock{ target };
    for(int tileOffset = 0; tileOffset < tileCount; tileOffset++) {
        const int tile = firstTile + tileOffset;
        const int tileX = (tile % NUM_TERRAIN_TILES_X) * tileWidth;
        const int tileY = (tile / NUM_TERRAIN_TILES_X) * tileHeight;

        for(int y = 0; y < tileHeight; y++) {
            const Uint8* sourceRow = static_cast<const Uint8*>(source->pixels) + (tileY + y) * source->pitch;
            const Uint8* sandRow = static_cast<const Uint8*>(source->pixels) + (sandY + y) * source->pitch;
            Uint32* targetRow = reinterpret_cast<Uint32*>(static_cast<Uint8*>(target->pixels)
                                                          + (tileY + y) * target->pitch);
            for(int x = 0; x < tileWidth; x++) {
                const Uint8 pixel = sourceRow[tileX + x];
                const Uint8 sandPixel = sandRow[sandX + x];
                if(pixel == PALCOLOR_TRANSPARENT || (hasColorKey && pixel == sourceColorKey)
                   || pixel >= source->format->palette->ncolors
                   || paletteColorsEqual(source, pixel, source, sandPixel)) {
                    continue;
                }

                const SDL_Color color = createSpiceTintColor(source->format->palette->colors[pixel], tint);
                Uint8 oldR = 0;
                Uint8 oldG = 0;
                Uint8 oldB = 0;
                Uint8 oldA = 255;
                SDL_GetRGBA(targetRow[tileX + x], target->format, &oldR, &oldG, &oldB, &oldA);
                targetRow[tileX + x] = SDL_MapRGBA(target->format, color.r, color.g, color.b, oldA);
            }
        }
    }
}

static sdl2::surface_ptr createRGBATintedTerrainSpiceSurface(
        SDL_Surface* source, SDL_Color thinTint, SDL_Color thickTint) {
    // Thick-spice artwork already supplies its darker depth. Reusing the thin
    // tint avoids a second brightness reduction while preserving that texture.
    thickTint = thinTint;
    auto tinted = createIndependentRGBACopy(source);
    if(!tinted || !source || !source->format || source->format->BytesPerPixel != 1
       || tinted->format->BytesPerPixel != 4) {
        return tinted;
    }

    constexpr int TerrainTile_Spice = 0x34;
    constexpr int TerrainTile_ThickSpice = 0x44;
    constexpr int TerrainTile_SpiceBloom = 0x54;
    tintTerrainSpiceTilesRGBA(source, tinted.get(), TerrainTile_Spice, 16, thinTint);
    tintTerrainSpiceTilesRGBA(source, tinted.get(), TerrainTile_ThickSpice, 16, thickTint);
    tintTerrainSpiceTilesRGBA(source, tinted.get(), TerrainTile_SpiceBloom, 1, thinTint);
    return tinted;
}

static sdl2::surface_ptr createRGBATintedMapEditorIcon(
        SDL_Surface* source, SDL_Surface* sand, SDL_Color tint) {
    auto tinted = createIndependentRGBACopy(source);
    if(!tinted || !source || !source->format || !source->format->palette
       || source->format->BytesPerPixel != 1 || tinted->format->BytesPerPixel != 4
       || !sand || !sand->format || !sand->format->palette || sand->format->BytesPerPixel != 1) {
        return tinted;
    }

    Uint32 sourceColorKey = 0;
    const bool hasColorKey = SDL_GetColorKey(source, &sourceColorKey) == 0;
    sdl2::surface_lock sourceLock{ source };
    sdl2::surface_lock sandLock{ sand };
    sdl2::surface_lock targetLock{ tinted.get() };
    const int width = std::min(std::min(source->w, sand->w), tinted->w);
    const int height = std::min(std::min(source->h, sand->h), tinted->h);
    for(int y = 0; y < height; y++) {
        const Uint8* sourceRow = static_cast<const Uint8*>(source->pixels) + y * source->pitch;
        const Uint8* sandRow = static_cast<const Uint8*>(sand->pixels) + y * sand->pitch;
        Uint32* targetRow = reinterpret_cast<Uint32*>(static_cast<Uint8*>(tinted->pixels) + y * tinted->pitch);
        for(int x = 0; x < width; x++) {
            const Uint8 pixel = sourceRow[x];
            const Uint8 sandPixel = sandRow[x];
            if(pixel == PALCOLOR_TRANSPARENT || (hasColorKey && pixel == sourceColorKey)
               || pixel >= source->format->palette->ncolors
               || paletteColorsEqual(source, pixel, sand, sandPixel)) {
                continue;
            }

            const SDL_Color color = createSpiceTintColor(source->format->palette->colors[pixel], tint);
            Uint8 oldR = 0;
            Uint8 oldG = 0;
            Uint8 oldB = 0;
            Uint8 oldA = 255;
            SDL_GetRGBA(targetRow[x], tinted->format, &oldR, &oldG, &oldB, &oldA);
            targetRow[x] = SDL_MapRGBA(tinted->format, color.r, color.g, color.b, oldA);
        }
    }

    return tinted;
}

static sdl2::surface_ptr createTintedTerrainSpiceSurface(
        SDL_Surface* source, SDL_Color thinTint, SDL_Color thickTint) {
    const bool tornieActive = ModManager::instance().isInitialized()
        && ModManager::instance().isTornieContentActive();
    return tornieActive
        ? createRGBATintedTerrainSpiceSurface(source, thinTint, thickTint)
        : createIndexedTintedTerrainSpiceSurface(source, thinTint, thickTint);
}

static sdl2::surface_ptr createTintedMapEditorIcon(
        SDL_Surface* source, SDL_Surface* sand, SDL_Color tint) {
    const bool tornieActive = ModManager::instance().isInitialized()
        && ModManager::instance().isTornieContentActive();
    return tornieActive
        ? createRGBATintedMapEditorIcon(source, sand, tint)
        : createIndexedTintedMapEditorIcon(source, sand, tint);
}
static sdl2::surface_ptr resizeSurfaceNearest(SDL_Surface* source, int width, int height) {
    if(!source || width <= 0 || height <= 0) {
        return nullptr;
    }

    sdl2::surface_ptr resized;
    if(source->format->BytesPerPixel == 1) {
        resized = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0) };
        if(resized && resized->format->palette && source->format->palette) {
            SDL_SetPaletteColors(resized->format->palette,
                                 source->format->palette->colors,
                                 0,
                                 source->format->palette->ncolors);
        }
    } else {
        resized = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height,
                                                          source->format->BitsPerPixel,
                                                          source->format->Rmask,
                                                          source->format->Gmask,
                                                          source->format->Bmask,
                                                          source->format->Amask) };
    }

    if(!resized) {
        return nullptr;
    }

    SDL_BlendMode blendMode;
    SDL_GetSurfaceBlendMode(source, &blendMode);
    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(resized.get(), SDL_BLENDMODE_NONE);
    SDL_BlitScaled(source, nullptr, resized.get(), nullptr);
    SDL_SetSurfaceBlendMode(source, blendMode);
    SDL_SetSurfaceBlendMode(resized.get(), blendMode);

    Uint32 colorKey = 0;
    if(SDL_GetColorKey(source, &colorKey) == 0) {
        SDL_SetColorKey(resized.get(), SDL_TRUE, colorKey);
    }

    return resized;
}

static sdl2::surface_ptr scaleSurfaceNearest(SDL_Surface* source, int factor) {
    if(!source || factor <= 0) {
        return nullptr;
    }

    return resizeSurfaceNearest(source, source->w * factor, source->h * factor);
}

// Alpha-aware Scale2x/Scale3x for accepted DuneCity RGBA Compacts. The legacy
// Scaler implementation intentionally remains indexed/palette-only. Processing
// each atlas tile independently prevents neighbouring density/value cells from
// influencing pixels along their shared sheet boundary.
static sdl2::surface_ptr scaleDuneCitySkinEdgeAware(SDL_Surface* source, int factor,
                                                   int tilesX, int tilesY) {
    if(!source || (factor != 2 && factor != 3) || tilesX <= 0 || tilesY <= 0
       || source->w % tilesX != 0 || source->h % tilesY != 0) {
        return scaleSurfaceNearest(source, factor);
    }

    auto rgba = sdl2::surface_ptr{ SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0) };
    auto scaled = sdl2::surface_ptr{ SDL_CreateRGBSurfaceWithFormat(
        0, source->w * factor, source->h * factor, 32, SDL_PIXELFORMAT_RGBA32) };
    if(!rgba || !scaled) {
        return scaleSurfaceNearest(source, factor);
    }

    SDL_SetColorKey(rgba.get(), SDL_FALSE, 0);
    SDL_SetColorKey(scaled.get(), SDL_FALSE, 0);
    SDL_SetSurfaceBlendMode(rgba.get(), SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(scaled.get(), SDL_BLENDMODE_NONE);

    const int tileW = rgba->w / tilesX;
    const int tileH = rgba->h / tilesY;
    const Uint32 alphaMask = rgba->format->Amask;
    const auto normalize = [alphaMask](Uint32 pixel) {
        return alphaMask != 0 && (pixel & alphaMask) == 0 ? Uint32{0} : pixel;
    };

    sdl2::surface_lock sourceLock{ rgba.get() };
    sdl2::surface_lock destinationLock{ scaled.get() };
    const auto readPixel = [&](int x, int y) {
        const auto* row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(rgba->pixels) + y * rgba->pitch);
        return normalize(row[x]);
    };
    const auto writePixel = [&](int x, int y, Uint32 pixel) {
        auto* row = reinterpret_cast<Uint32*>(
            static_cast<Uint8*>(scaled->pixels) + y * scaled->pitch);
        row[x] = normalize(pixel);
    };

    for(int tileY = 0; tileY < tilesY; ++tileY) {
        for(int tileX = 0; tileX < tilesX; ++tileX) {
            const int originX = tileX * tileW;
            const int originY = tileY * tileH;
            for(int y = 0; y < tileH; ++y) {
                const int up = std::max(0, y - 1);
                const int down = std::min(tileH - 1, y + 1);
                for(int x = 0; x < tileW; ++x) {
                    const int left = std::max(0, x - 1);
                    const int right = std::min(tileW - 1, x + 1);
                    const Uint32 A = readPixel(originX + left,  originY + up);
                    const Uint32 B = readPixel(originX + x,     originY + up);
                    const Uint32 C = readPixel(originX + right, originY + up);
                    const Uint32 D = readPixel(originX + left,  originY + y);
                    const Uint32 E = readPixel(originX + x,     originY + y);
                    const Uint32 F = readPixel(originX + right, originY + y);
                    const Uint32 G = readPixel(originX + left,  originY + down);
                    const Uint32 H = readPixel(originX + x,     originY + down);
                    const Uint32 I = readPixel(originX + right, originY + down);
                    const int destinationX = (originX + x) * factor;
                    const int destinationY = (originY + y) * factor;

                    if(factor == 2) {
                        Uint32 output[4] = {E, E, E, E};
                        if(B != H && D != F) {
                            output[0] = D == B ? D : E;
                            output[1] = B == F ? F : E;
                            output[2] = D == H ? D : E;
                            output[3] = H == F ? F : E;
                        }
                        writePixel(destinationX,     destinationY,     output[0]);
                        writePixel(destinationX + 1, destinationY,     output[1]);
                        writePixel(destinationX,     destinationY + 1, output[2]);
                        writePixel(destinationX + 1, destinationY + 1, output[3]);
                    } else {
                        Uint32 output[9] = {E, E, E, E, E, E, E, E, E};
                        if(B != H && D != F) {
                            output[0] = D == B ? D : E;
                            output[1] = ((D == B && E != C) || (B == F && E != A)) ? B : E;
                            output[2] = B == F ? F : E;
                            output[3] = ((D == B && E != G) || (D == H && E != A)) ? D : E;
                            output[5] = ((B == F && E != I) || (H == F && E != C)) ? F : E;
                            output[6] = D == H ? D : E;
                            output[7] = ((D == H && E != I) || (H == F && E != G)) ? H : E;
                            output[8] = H == F ? F : E;
                        }
                        for(int outputY = 0; outputY < 3; ++outputY) {
                            for(int outputX = 0; outputX < 3; ++outputX) {
                                writePixel(destinationX + outputX, destinationY + outputY,
                                           output[outputY * 3 + outputX]);
                            }
                        }
                    }
                }
            }
        }
    }

    SDL_SetSurfaceBlendMode(scaled.get(), SDL_BLENDMODE_BLEND);
    return scaled;
}

static sdl2::surface_ptr createCustomMapEditorStar(SDL_Surface* source) {
    if(!source) {
        return nullptr;
    }

    // Keep the special-unit marker outside indexed house palettes. Its RGBA
    // pixels remain blue when the active mod or editor house changes.
    sdl2::surface_ptr star{ SDL_CreateRGBSurfaceWithFormat(
        0, source->w, source->h, 32, SDL_PIXELFORMAT_RGBA32) };
    if(!star) {
        return nullptr;
    }

    Uint32 sourceColorKey = 0;
    const bool hasSourceColorKey = SDL_GetColorKey(source, &sourceColorKey) == 0;
    const Uint32 transparent = SDL_MapRGBA(star->format, 0, 0, 0, 0);
    const Uint32 fixedBlue = SDL_MapRGBA(star->format, 24, 152, 255, 255);
    SDL_FillRect(star.get(), nullptr, transparent);

    {
        sdl2::surface_lock sourceLock{ source };
        sdl2::surface_lock starLock{ star.get() };
        for(int y = 0; y < source->h; ++y) {
            for(int x = 0; x < source->w; ++x) {
                const Uint32 sourcePixel = getPixel(source, x, y);
                Uint8 r = 0, g = 0, b = 0, a = 0;
                SDL_GetRGBA(sourcePixel, source->format, &r, &g, &b, &a);
                const bool visible = source->format->BytesPerPixel == 1
                    ? sourcePixel != PALCOLOR_TRANSPARENT
                    : (hasSourceColorKey ? sourcePixel != sourceColorKey : a != 0);
                if(visible) {
                    putPixel(star.get(), x, y, fixedBlue);
                }
            }
        }
    }

    SDL_SetSurfaceBlendMode(star.get(), SDL_BLENDMODE_BLEND);
    return star;
}

// DuneCity 1.0.487: invalidate sprite texture cache.
// Clears objPicTex + objPic (NOT uiGraphic - preserves
// mentat background and editor sidebar icons).
void GFXManager::invalidateAllSpriteTextures() {
    enhancedInfantryOverlay.clear();
    SDL_Log("GFXManager::invalidateAllSpriteTextures(): clearing textures and derived house sprite caches");
    const auto keepAllHouseSurfaces = [](int id) {
        return id == ObjPic_ZoneResidential || id == ObjPic_ZoneCommercial
               || id == ObjPic_ZoneIndustrial || id == ObjPic_CityRoad
               || id == ObjPic_NuclearPlant || id == ObjPic_PoliceStation
               || id == ObjPic_Stadium || id == ObjPic_Airport
               || id == ObjPic_Hospital || id == ObjPic_Church;
    };
    for(int id = 0; id < NUM_OBJPICS; id++) {
        for(int h = 0; h < NUM_HOUSE_COLOR_SLOTS; h++) {
            for(unsigned int z = 0; z < NUM_ZOOMLEVEL; z++) {
                objPicTex[id][h][z].reset();
                if(h != HOUSE_HARKONNEN && !keepAllHouseSurfaces(id)) {
                    objPic[id][h][z].reset();
                }
            }
        }
    }

    for(auto& hd : hdObjPicOverrides) {
        hd = HDObjPicOverride{};
    }
    enhancedUnitDefinitions.clear();
    enhancedUnitManifestsLoaded = false;
    enhancedBuildingDefinitions.clear();
    enhancedTerrainDefinitions.clear();
    enhancedWorldManifestsLoaded = false;
    duneCityBuildingDefinitions.clear();
    duneCityBuildingManifestsLoaded = false;
    if(enhancedBuildingAtlasCache) {
        enhancedBuildingAtlasCache->clear();
    }
    enhancedUnitRenderModes.clear();
    enhancedRenderModesLoaded = false;
}

void GFXManager::loadMentatGraphics() {
    for(int house = 0; house < NUM_HOUSE_COLOR_SLOTS; house++) {
        uiGraphic[UI_MentatBackground][house].reset();
        uiGraphicTex[UI_MentatBackground][house].reset();
        modMentatForeground[house].reset();
        modMentatForegroundTex[house].reset();
        modMentatEyes[house].reset();
        modMentatMouth[house].reset();
    }

    auto loadMentatBackgroundPng = [&](const std::string& filename) -> sdl2::surface_ptr {
        if(!pFileManager->exists(filename)) {
            return nullptr;
        }

        auto png = LoadPNG_RW(pFileManager->openFile(filename).get());
        if(!png) {
            return nullptr;
        }

        if(png->w <= SCREEN_MIN_WIDTH/2 && png->h <= SCREEN_MIN_HEIGHT/2) {
            return Scaler::defaultDoubleSurface(png.get());
        }

        return png;
    };

    uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATH.CPS").get()).get());
    auto vanillaAtreidesMentat = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATA.CPS").get()).get());
    uiGraphic[UI_MentatBackground][HOUSE_ATREIDES] = copySurface(vanillaAtreidesMentat.get());
    uiGraphic[UI_MentatBackground][HOUSE_ORDOS] = Scaler::defaultDoubleSurface(LoadCPS_RW(pFileManager->openFile("MENTATO.CPS").get()).get());
    uiGraphic[UI_MentatBackground][HOUSE_FREMEN] = PictureFactory::mapMentatSurfaceToFremen(vanillaAtreidesMentat.get());
    uiGraphic[UI_MentatBackground][HOUSE_SARDAUKAR] = PictureFactory::mapMentatSurfaceToSardaukar(uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN].get());
    uiGraphic[UI_MentatBackground][HOUSE_MERCENARY] = PictureFactory::mapMentatSurfaceToMercenary(uiGraphic[UI_MentatBackground][HOUSE_ORDOS].get());
    uiGraphic[UI_MentatBackground][HOUSE_NEUTRAL] = mapSurfaceColorRange(uiGraphic[UI_MentatBackground][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, houseToPaletteIndex[HOUSE_NEUTRAL]);
    uiGraphic[UI_MentatBackground][HOUSE_REBELS] =
        mapSurfaceColorRange(uiGraphic[UI_MentatBackground][HOUSE_ATREIDES].get(),
                             PALCOLOR_ATREIDES,
                             getVisualRemapPaletteIndex(HOUSE_REBELS));
    applyCustomVisualColorRamp(uiGraphic[UI_MentatBackground][HOUSE_REBELS].get(), HOUSE_REBELS);
    applyRebelsTint(uiGraphic[UI_MentatBackground][HOUSE_REBELS].get(), HOUSE_REBELS);

    ModManager& modManager = ModManager::instance();
    const int customFallback = modManager.isCustomHouseRegistered()
        ? modManager.getActiveCustomHouseInfo().fallbackHouse
        : HOUSE_HARKONNEN;
    uiGraphic[UI_MentatBackground][HOUSE_CUSTOM] = copySurface(uiGraphic[UI_MentatBackground][customFallback].get());

    for(int house = 0; house < NUM_HOUSE_COLOR_SLOTS; ++house) {
        const ModMentatInfo& info = modManager.getActiveMentatInfo(house);
        if(!info.enabled) continue;
        const int identity = modManager.getEffectiveMentatIdentity(house);
        if(identity >= 0 && identity < NUM_HOUSE_COLOR_SLOTS
           && uiGraphic[UI_MentatBackground][identity] != nullptr) {
            uiGraphic[UI_MentatBackground][house] = copySurface(uiGraphic[UI_MentatBackground][identity].get());
        }
    }

    for(int house = 0; house < NUM_HOUSE_COLOR_SLOTS; ++house) {
        const ModMentatInfo& info = modManager.getActiveMentatInfo(house);
        if(!info.enabled) continue;

        if(!info.backgroundAsset.empty()) {
            auto background = loadMentatBackgroundPng(info.backgroundAsset);
            if(background != nullptr) {
                uiGraphic[UI_MentatBackground][house] = std::move(background);
            } else {
                SDL_Log("GFXManager: Mentat %d background '%s' unavailable; using fallback",
                        house, info.backgroundAsset.c_str());
            }
        }
        if(!info.foregroundAsset.empty()) {
            auto foreground = loadMentatBackgroundPng(info.foregroundAsset);
            if(foreground != nullptr) {
                modMentatForeground[house] = std::move(foreground);
            } else {
                SDL_Log("GFXManager: Mentat %d foreground '%s' unavailable; using fallback",
                        house, info.foregroundAsset.c_str());
            }
        }
        if(!info.eyesAsset.empty()) {
            modMentatEyes[house] = loadPngStripAnimation(
                info.eyesAsset, info.eyesFrames, info.eyesFrameRate,
                info.doubleEyes, info.eyesTransparentColor);
            if(modMentatEyes[house] == nullptr) {
                SDL_Log("GFXManager: Mentat %d eyes '%s' unavailable; using fallback",
                        house, info.eyesAsset.c_str());
            }
        }
        if(!info.mouthAsset.empty()) {
            modMentatMouth[house] = loadPngStripAnimation(
                info.mouthAsset, info.mouthFrames, info.mouthFrameRate,
                info.doubleMouth, info.mouthTransparentColor);
            if(modMentatMouth[house] == nullptr) {
                SDL_Log("GFXManager: Mentat %d mouth '%s' unavailable; using fallback",
                        house, info.mouthAsset.c_str());
            }
        }
    }
}

void GFXManager::loadCustomHouseHerald() {
    constexpr unsigned int presentationIds[] = {
        UI_Herald_Colored,
        UI_Herald_ColoredLarge,
        UI_Herald_Grey
    };
    ModManager& modManager = ModManager::instance();
    auto pictureFactory = std::make_unique<PictureFactory>();

    auto loadRegisteredHerald = [&](int house) {
        for(const unsigned int id : presentationIds) {
            uiGraphic[id][house].reset();
            uiGraphicTex[id][house].reset();
        }
        if(!modManager.isCustomHouseRegistered(house)) return;

        const CustomHouseInfo& info = modManager.getCustomHouseInfo(house);
        SDL_Surface* fallback = uiGraphic[UI_Herald_Colored][info.fallbackHouse].get();
        sdl2::surface_ptr herald;

        if(!info.heraldAsset.empty()) {
            try {
                if(pFileManager->exists(info.heraldAsset)) {
                    herald = LoadPNG_RW(pFileManager->openFile(info.heraldAsset).get());
                    if(herald != nullptr) SDL_SetColorKey(herald.get(), SDL_TRUE, 0);
                }
            } catch(const std::exception& e) {
                SDL_Log("GFXManager: Custom-house herald '%s' failed (%s); using fallback",
                        info.heraldAsset.c_str(), e.what());
            }
        }
        if(herald == nullptr && fallback != nullptr) herald = copySurface(fallback);
        if(herald == nullptr) return;

        uiGraphic[UI_Herald_Colored][house] = std::move(herald);
        uiGraphic[UI_Herald_ColoredLarge][house] =
            Scaler::defaultDoubleSurface(uiGraphic[UI_Herald_Colored][house].get());
        uiGraphic[UI_Herald_Grey][house] =
            pictureFactory->createGreyHouseChoice(uiGraphic[UI_Herald_Colored][house].get());
    };

    loadRegisteredHerald(HOUSE_CUSTOM);
    loadRegisteredHerald(HOUSE_THARPIQUE);
}

void GFXManager::reloadModDependentObjectGraphics() {
    for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
        for(unsigned int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
            objPicTex[ObjPic_Scoutpost][colorSlot][zoom].reset();
            objPicTex[ObjPic_Flamepost][colorSlot][zoom].reset();
            objPicTex[ObjPic_Chemipost][colorSlot][zoom].reset();
            objPicTex[ObjPic_ChaosFactory][colorSlot][zoom].reset();
            if(scoutpostBaseGraphics[colorSlot][zoom]) {
                objPic[ObjPic_Scoutpost][colorSlot][zoom] =
                    copySurface(scoutpostBaseGraphics[colorSlot][zoom].get());
                objPic[ObjPic_Flamepost][colorSlot][zoom] =
                    copySurface(scoutpostBaseGraphics[colorSlot][zoom].get());
                objPic[ObjPic_Chemipost][colorSlot][zoom] =
                    copySurface(scoutpostBaseGraphics[colorSlot][zoom].get());
            } else {
                objPic[ObjPic_Scoutpost][colorSlot][zoom].reset();
                objPic[ObjPic_Flamepost][colorSlot][zoom].reset();
                objPic[ObjPic_Chemipost][colorSlot][zoom].reset();
            }
            if(chaosFactoryBaseGraphics[colorSlot][zoom]) {
                objPic[ObjPic_ChaosFactory][colorSlot][zoom] =
                    copySurface(chaosFactoryBaseGraphics[colorSlot][zoom].get());
            } else {
                objPic[ObjPic_ChaosFactory][colorSlot][zoom].reset();
            }
        }
    }

    const bool torniePostsActive = ModManager::instance().isInitialized()
        && ModManager::instance().isTornieContentActive();
    auto prepareRuntimeTeamGraphic = [&](SDL_Surface* source,
                                                 bool postPaint,
                                                 bool chaosPaint) -> sdl2::surface_ptr {
        if(source == nullptr) {
            return nullptr;
        }
        SDL_Surface* reference = objPic[ObjPic_Windtrap][HOUSE_HARKONNEN][0].get();
        const SDL_Palette* targetPalette =
            reference && reference->format ? reference->format->palette : nullptr;
        if(targetPalette == nullptr) {
            return nullptr;
        }

        sdl2::surface_ptr indexed;
        if(source->format && source->format->BytesPerPixel == 1
           && source->format->palette) {
            indexed = remapIndexedSurfaceToPalette(source, targetPalette);
        } else {
            indexed = convertTruecolorSurfaceToPalette(source, targetPalette);
        }
        if(!indexed || !indexed->format || !indexed->format->palette) {
            return nullptr;
        }
        preserveOpaqueBlackIndex(indexed.get());
        normalizeTransparentPaletteIndexes(indexed.get());

        SDL_Palette* colors = indexed->format->palette;
        {
            sdl2::surface_lock lock{indexed.get()};
            for(int y = 0; y < indexed->h; ++y) {
                Uint8* pixels =
                    static_cast<Uint8*>(indexed->pixels) + y * indexed->pitch;
                for(int x = 0; x < indexed->w; ++x) {
                    Uint8& pixel = pixels[x];
                    if(pixel == PALCOLOR_TRANSPARENT || pixel >= colors->ncolors) {
                        continue;
                    }
                    const SDL_Color c = colors->colors[pixel];
                    const int r = c.r;
                    const int g = c.g;
                    const int b = c.b;
                    const bool greenPost =
                        postPaint && g >= 55 && g > r + 10 && g > b + 10;
                    const bool flamePost =
                        postPaint && r >= 70 && r > b + 18 && r > g + 8;
                    const bool redFlag =
                        chaosPaint && r >= 55 && r > g + 14 && r >= b;
                    const bool purpleFlag =
                        chaosPaint && r >= 45 && b >= 45
                        && r > g + 10 && b > g + 10;
                    if(greenPost || flamePost || redFlag || purpleFlag) {
                        const int brightness = std::max(std::max(r, g), b);
                        pixel = static_cast<Uint8>(
                            PALCOLOR_HARKONNEN
                            + getNearestHarkonnenShade(colors, brightness));
                    }
                }
            }
        }
        normalizeTransparentPaletteIndexes(indexed.get());
        return indexed;
    };

    auto createRuntimeHouseGraphic = [&](SDL_Surface* source, int colorSlot,
                                         const char* label) -> sdl2::surface_ptr {
        sdl2::surface_ptr indexed;
        if(colorSlot == HOUSE_HARKONNEN) {
            indexed = copySurface(source);
        } else {
            indexed = mapSurfaceColorRange(
                source, PALCOLOR_HARKONNEN, getVisualRemapPaletteIndex(colorSlot));
            if(indexed) {
                applyCustomVisualColorRamp(indexed.get(), colorSlot);
                if(isTornieRebelsColorSlot(colorSlot)) {
                    applyRebelsTint(indexed.get(), colorSlot);
                }
            }
        }
        if(!indexed) {
            return nullptr;
        }
        normalizeTransparentPaletteIndexes(indexed.get());
        auto rgba = convertTornieIndexedSurfaceToRGBA(
            indexed.get(), label, colorSlot, 0, false);
        if(rgba) {
            SDL_SetColorKey(rgba.get(), SDL_FALSE, 0);
            SDL_SetSurfaceBlendMode(rgba.get(), SDL_BLENDMODE_BLEND);
        }
        return rgba;
    };

    auto installPostGraphic = [&](unsigned int objectGraphic,
                                  const char* filename,
                                  const char* label) {
        if(!torniePostsActive) {
            return;
        }
        try {
            if(!pFileManager->exists(filename)) {
                return;
            }
            auto raw = LoadPNG_RW(pFileManager->openFile(filename).get());
            if(!raw || raw->w != D2_TILESIZE || raw->h != 2 * D2_TILESIZE) {
                SDL_Log("GFXManager: %s must be %dx%d",
                        label, D2_TILESIZE, 2 * D2_TILESIZE);
                return;
            }
            auto source = prepareRuntimeTeamGraphic(raw.get(), false, false);
            for(int slot = 0; source && slot < NUM_HOUSE_COLOR_SLOTS; ++slot) {
                SDL_Surface* base = objPic[objectGraphic][slot][0].get();
                auto houseGraphic =
                    createRuntimeHouseGraphic(source.get(), slot, label);
                sdl2::surface_ptr atlas{
                    base ? SDL_ConvertSurfaceFormat(
                               base, SDL_PIXELFORMAT_RGBA32, 0)
                         : nullptr
                };
                if(!atlas || !houseGraphic) {
                    continue;
                }
                SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_BLEND);
                const Uint32 transparent =
                    SDL_MapRGBA(atlas->format, 0, 0, 0, 0);
                for(int frame = 0; frame < 2; ++frame) {
                    SDL_Rect src{0, frame * D2_TILESIZE,
                                 D2_TILESIZE, D2_TILESIZE};
                    SDL_Rect dst{(2 + frame) * D2_TILESIZE, 0,
                                 D2_TILESIZE, D2_TILESIZE};
                    SDL_FillRect(atlas.get(), &dst, transparent);
                    SDL_BlitSurface(houseGraphic.get(), &src, atlas.get(), &dst);
                }
                objPic[objectGraphic][slot][0] = std::move(atlas);
                objPic[objectGraphic][slot][1] =
                    scaleSurfaceNearest(objPic[objectGraphic][slot][0].get(), 2);
                objPic[objectGraphic][slot][2] =
                    scaleSurfaceNearest(objPic[objectGraphic][slot][0].get(), 3);
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: %s load failed (%s)", label, e.what());
        }
    };

    installPostGraphic(ObjPic_Scoutpost, "Green_Post.png", "Scoutpost");
    installPostGraphic(ObjPic_Flamepost, "Flamepost.png", "Flamepost");
    installPostGraphic(ObjPic_Chemipost, "Chemipost.png", "Chemipost");

    if(torniePostsActive) {
        try {
            if(pFileManager->exists("ChaosFactory.png")) {
                auto raw =
                    LoadPNG_RW(pFileManager->openFile("ChaosFactory.png").get());
                if(raw && raw->w == 3 * D2_TILESIZE
                   && raw->h == 4 * D2_TILESIZE) {
                    auto source =
                        prepareRuntimeTeamGraphic(raw.get(), false, true);
                    for(int slot = 0;
                        source && slot < NUM_HOUSE_COLOR_SLOTS; ++slot) {
                        auto houseGraphic = createRuntimeHouseGraphic(
                            source.get(), slot, "ChaosFactory");
                        if(!houseGraphic) {
                            continue;
                        }
                        sdl2::surface_ptr atlas;
                        if(objPic[ObjPic_ChaosFactory][slot][0]) {
                            atlas.reset(SDL_ConvertSurfaceFormat(
                                objPic[ObjPic_ChaosFactory][slot][0].get(),
                                SDL_PIXELFORMAT_RGBA32, 0));
                        }
                        if(!atlas) {
                            atlas.reset(SDL_CreateRGBSurfaceWithFormat(
                                0, 12 * D2_TILESIZE, 2 * D2_TILESIZE,
                                32, SDL_PIXELFORMAT_RGBA32));
                        }
                        if(!atlas) {
                            continue;
                        }
                        SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_BLEND);
                        const Uint32 transparent =
                            SDL_MapRGBA(atlas->format, 0, 0, 0, 0);
                        if(!chaosFactoryBaseGraphics[slot][0]) {
                            SDL_FillRect(atlas.get(), nullptr, transparent);
                            for(int frame = 0; frame < 2; ++frame) {
                                SDL_Rect src{0, 0, 3 * D2_TILESIZE,
                                             2 * D2_TILESIZE};
                                SDL_Rect dst{frame * 3 * D2_TILESIZE, 0,
                                             3 * D2_TILESIZE, 2 * D2_TILESIZE};
                                SDL_BlitSurface(
                                    houseGraphic.get(), &src, atlas.get(), &dst);
                            }
                        }
                        for(int frame = 0; frame < 2; ++frame) {
                            SDL_Rect src{0, frame * 2 * D2_TILESIZE,
                                         3 * D2_TILESIZE, 2 * D2_TILESIZE};
                            SDL_Rect dst{(2 + frame) * 3 * D2_TILESIZE, 0,
                                         3 * D2_TILESIZE, 2 * D2_TILESIZE};
                            SDL_FillRect(atlas.get(), &dst, transparent);
                            SDL_BlitSurface(
                                houseGraphic.get(), &src, atlas.get(), &dst);
                        }
                        objPic[ObjPic_ChaosFactory][slot][0] = std::move(atlas);
                        objPic[ObjPic_ChaosFactory][slot][1] =
                            scaleSurfaceNearest(
                                objPic[ObjPic_ChaosFactory][slot][0].get(), 2);
                        objPic[ObjPic_ChaosFactory][slot][2] =
                            scaleSurfaceNearest(
                                objPic[ObjPic_ChaosFactory][slot][0].get(), 3);
                    }
                }
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: Chaos Factory reload failed (%s)", e.what());
        }
    }

    for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
        for(unsigned int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
            objPic[ObjPic_ChemicalCarryall][colorSlot][zoom].reset();
            objPicTex[ObjPic_ChemicalCarryall][colorSlot][zoom].reset();
        }
    }

    bool chemicalCarryallLoaded = false;
    try {
        if(pFileManager->exists("ChemicalCarryall.png")) {
            auto raw = LoadPNG_RW(pFileManager->openFile("ChemicalCarryall.png").get());
            if(raw && raw->w == 192 && raw->h == 48) {
                preserveOpaqueBlackIndex(raw.get());
                normalizeTransparentPaletteIndexes(raw.get());
                sdl2::surface_ptr sourceRGBA{
                    SDL_ConvertSurfaceFormat(raw.get(), SDL_PIXELFORMAT_RGBA32, 0)
                };
                if(!sourceRGBA) {
                    throw std::runtime_error("Could not convert ChemicalCarryall.png to RGBA");
                }
                SDL_SetColorKey(sourceRGBA.get(), SDL_FALSE, 0);
                SDL_SetSurfaceBlendMode(sourceRGBA.get(), SDL_BLENDMODE_BLEND);

                for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
                    auto rgba = remapTruecolorHouseColorRange(sourceRGBA.get(), colorSlot);
                    if(!rgba) {
                        rgba = copySurface(sourceRGBA.get());
                    }
                    if(!rgba) {
                        continue;
                    }
                    SDL_SetColorKey(rgba.get(), SDL_FALSE, 0);
                    SDL_SetSurfaceBlendMode(rgba.get(), SDL_BLENDMODE_BLEND);
                    objPic[ObjPic_ChemicalCarryall][colorSlot][0] = std::move(rgba);
                    objPic[ObjPic_ChemicalCarryall][colorSlot][1] =
                        scaleSurfaceNearest(objPic[ObjPic_ChemicalCarryall][colorSlot][0].get(), 2);
                    objPic[ObjPic_ChemicalCarryall][colorSlot][2] =
                        scaleSurfaceNearest(objPic[ObjPic_ChemicalCarryall][colorSlot][0].get(), 3);
                }
                chemicalCarryallLoaded = true;
                SDL_Log("GFXManager: reloaded Chemical Carryall graphics for the active mod");
            }
        }
    } catch(const std::exception& e) {
        SDL_Log("GFXManager: Chemical Carryall reload failed (%s)", e.what());
    }

    if(!chemicalCarryallLoaded) {
        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            SDL_Surface* source = objPic[ObjPic_Carryall][colorSlot][0].get();
            const bool remapHarkonnen = source == nullptr && colorSlot != HOUSE_HARKONNEN;
            if(source == nullptr) {
                source = objPic[ObjPic_Carryall][HOUSE_HARKONNEN][0].get();
            }
            if(source == nullptr) {
                continue;
            }

            sdl2::surface_ptr fallback;
            if(remapHarkonnen && source->format->BytesPerPixel == 1) {
                fallback = mapSurfaceColorRange(
                    source, PALCOLOR_HARKONNEN, getVisualRemapPaletteIndex(colorSlot));
                applyCustomVisualColorRamp(fallback.get(), colorSlot);
                applyRebelsTint(fallback.get(), colorSlot);
                normalizeTransparentPaletteIndexes(fallback.get());
                SDL_SetColorKey(fallback.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
            } else {
                fallback = copySurface(source);
            }
            if(!fallback) {
                continue;
            }

            objPic[ObjPic_ChemicalCarryall][colorSlot][0] = std::move(fallback);
            objPic[ObjPic_ChemicalCarryall][colorSlot][1] =
                scaleSurfaceNearest(objPic[ObjPic_ChemicalCarryall][colorSlot][0].get(), 2);
            objPic[ObjPic_ChemicalCarryall][colorSlot][2] =
                scaleSurfaceNearest(objPic[ObjPic_ChemicalCarryall][colorSlot][0].get(), 3);
        }
    }
}

void GFXManager::reloadRuntimeModPortraits() {
    constexpr int portraitWidth = 91;
    constexpr int portraitHeight = 55;
    auto loadPortrait = [&](unsigned int pictureID, const char* filename,
                            const char* fallbackWsa, bool enabled) {
        smallDetailPicTex[pictureID].reset();
        try {
            if(enabled && pFileManager->exists(filename)) {
                auto raw = LoadPNG_RW(pFileManager->openFile(filename).get());
                if(raw) {
                    preserveOpaqueBlackIndex(raw.get());
                    normalizeTransparentPaletteIndexes(raw.get());
                    if(raw->w != portraitWidth || raw->h != portraitHeight) {
                        auto resized = resizeSurfaceNearest(raw.get(), portraitWidth, portraitHeight);
                        if(resized) {
                            raw = std::move(resized);
                        }
                    }
                    auto texture = convertSurfaceToTexture(raw.get());
                    if(texture) {
                        SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
                        smallDetailPicTex[pictureID] = std::move(texture);
                        return;
                    }
                }
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: runtime portrait '%s' reload failed (%s)", filename, e.what());
        }
        smallDetailPicTex[pictureID] = extractSmallDetailPic(fallbackWsa);
    };

    const bool tornieContentActive = ModManager::instance().isTornieContentActive();

    loadPortrait(Picture_RocketTrike, "RocketTrikeIcon.png", "TRIKE.WSA", tornieContentActive);
    loadPortrait(Picture_SonicTrike, "SonicTrikeIcon.png", "TRIKE.WSA", tornieContentActive);
    loadPortrait(Picture_FlameTank, "FlameTankIcon.png", "HTANK.WSA", tornieContentActive);
    loadPortrait(Picture_EliteLauncher, "EliteLauncherIcon.png", "HTANK.WSA", tornieContentActive);
    loadPortrait(Picture_EliteSiegeTank, "EliteSiegeTankIcon.png", "HTANK.WSA", tornieContentActive);
    loadPortrait(Picture_ChemicalSiegeTank, "ChemicalSiegeTankIcon.png", "HTANK.WSA", tornieContentActive);
    loadPortrait(Picture_ChemicalCarryall, "ChemicalCarryallIcon.png", "CARRYALL.WSA", tornieContentActive);
    loadPortrait(Picture_AdvancedWindTrap, "Tornie_AdvancedWindtrap_icon.png", "WINDTRAP.WSA", tornieContentActive);
    loadPortrait(Picture_Worfinery, "WorfineryIcon.png", "WOR.WSA", tornieContentActive);
    loadPortrait(Picture_TechCenter, "TechCenterIcon.png", "PALACE.WSA", tornieContentActive);
    loadPortrait(Picture_Scoutpost, "ScoutpostIcon.png", "RTURRET.WSA", tornieContentActive);
    loadPortrait(Picture_Flamepost, "FlamepostIcon.png", "RTURRET.WSA", tornieContentActive);
    loadPortrait(Picture_Chemipost, "ChemipostIcon.png", "RTURRET.WSA", tornieContentActive);
    loadPortrait(Picture_ChaosFactory, "ChaosFactoryIcon.png", "STARPORT.WSA", tornieContentActive);
    loadPortrait(Picture_LoveFactory, "LoveFactoryIcon.png", "STARPORT.WSA", tornieContentActive);
    loadPortrait(Picture_PalaceLightVehicles, "PalaceTrikeAndQuadIcon.png", "FREMEN.WSA", tornieContentActive);
    loadPortrait(Picture_PalaceRebelsCharging, "PalaceRebelsChargingIcon.png", "FREMEN.WSA",
                 tornieContentActive);
    loadPortrait(Picture_Harvestank, "HarvestankIcon.png", "HARVEST.WSA", tornieContentActive);


}

void GFXManager::rebuildModDependentEditorGraphics() {
    auto customStar = createCustomMapEditorStar(objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get());
    struct EditorStructurePreview {
        unsigned int uiID;
        unsigned int objectGraphic;
        int x;
        int y;
        int width;
        int height;
    };
    constexpr EditorStructurePreview previews[] = {
        { UI_MapEditor_AdvancedWindTrap,    ObjPic_AdvancedWindTrap,    2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 3*D2_TILESIZE },
        { UI_MapEditor_AdvancedWindTrapMK2, ObjPic_AdvancedWindTrap2x3, 2*2*D2_TILESIZE, 0, 2*D2_TILESIZE, 3*D2_TILESIZE },
        { UI_MapEditor_AdvancedWindTrapMK3, ObjPic_AdvancedWindTrap3x2, 2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_Worfinery,           ObjPic_Worfinery,           2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_TechCenter,          ObjPic_TechCenter,          2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE },
        { UI_MapEditor_Scoutpost,           ObjPic_Scoutpost,           2*D2_TILESIZE,   0, D2_TILESIZE,   D2_TILESIZE },
        { UI_MapEditor_Flamepost,           ObjPic_Flamepost,           2*D2_TILESIZE,   0, D2_TILESIZE,   D2_TILESIZE },
        { UI_MapEditor_Chemipost,           ObjPic_Chemipost,           2*D2_TILESIZE,   0, D2_TILESIZE,   D2_TILESIZE },
        { UI_MapEditor_LoveFactory,         ObjPic_LoveFactory,         2*2*D2_TILESIZE, 0, 2*D2_TILESIZE, 3*D2_TILESIZE },
        { UI_MapEditor_ChaosFactory,        ObjPic_ChaosFactory,        2*3*D2_TILESIZE, 0, 3*D2_TILESIZE, 2*D2_TILESIZE }
    };

    const bool tornieActive = ModManager::instance().isInitialized()
        && ModManager::instance().isTornieContentActive();
    auto selectEditorSprite = [&](unsigned int customSprite, unsigned int fallbackSprite) {
        return tornieActive && objPic[customSprite][HOUSE_HARKONNEN][0]
            ? customSprite
            : fallbackSprite;
    };
    const unsigned int deviatorEditorGun =
        selectEditorSprite(ObjPic_DeviatorGunTornie, ObjPic_Launcher_Gun);
    const unsigned int flameTankEditorGun =
        selectEditorSprite(ObjPic_FlameTankGunTornie, ObjPic_Launcher_Gun);
    const unsigned int eliteLauncherEditorGun =
        selectEditorSprite(ObjPic_EliteLauncherGunTornie, ObjPic_Launcher_Gun);
    const unsigned int eliteSiegeTankEditorGun =
        selectEditorSprite(ObjPic_EliteSiegeTankGunTornie, ObjPic_Siegetank_Gun);
    const unsigned int chemicalSiegeTankEditorGun =
        selectEditorSprite(ObjPic_ChemicalSiegeTankGunTornie, ObjPic_Siegetank_Gun);

    auto getRuntimeEditorFrame = [&](unsigned int objPicID, int colorSlot,
                                     int frameX, int frameY, int framesX,
                                     int framesY) -> sdl2::surface_ptr {
        const bool forceCustomRemap = tornieActive
            && (colorSlot == HOUSE_CUSTOM || isCustomHouseColorSlot(colorSlot));
        SDL_Surface* atlas = forceCustomRemap
            ? nullptr
            : objPic[objPicID][colorSlot][0].get();
        sdl2::surface_ptr remappedAtlas;

        if(!atlas) {
            SDL_Surface* harkonnenAtlas = objPic[objPicID][HOUSE_HARKONNEN][0].get();
            if(!harkonnenAtlas) {
                return nullptr;
            }

            if(colorSlot != HOUSE_HARKONNEN && harkonnenAtlas->format->BytesPerPixel == 1) {
                remappedAtlas = mapSurfaceColorRange(
                    harkonnenAtlas, PALCOLOR_HARKONNEN,
                    getVisualRemapPaletteIndex(colorSlot));
                applyCustomVisualColorRamp(remappedAtlas.get(), colorSlot);
                applyRebelsTint(remappedAtlas.get(), colorSlot);
                normalizeTransparentPaletteIndexes(remappedAtlas.get());
                SDL_SetColorKey(remappedAtlas.get(), SDL_TRUE, PALCOLOR_TRANSPARENT);
                atlas = remappedAtlas.get();
            } else {
                atlas = harkonnenAtlas;
            }
        }

        return getSubFrame(atlas, frameX, frameY, framesX, framesY);
    };

    auto composeRuntimeEditorVehicle = [&](unsigned int baseObjPicID, int baseColorSlot,
                                           int gunObjPicID, int gunColorSlot,
                                           int gunOffsetX, int gunOffsetY) -> sdl2::surface_ptr {
        auto base = getRuntimeEditorFrame(
            baseObjPicID, baseColorSlot, 0, 0, NUM_ANGLES, 1);
        if(!base || gunObjPicID < 0) {
            return base;
        }

        auto gun = getRuntimeEditorFrame(
            static_cast<unsigned int>(gunObjPicID), gunColorSlot,
            0, 0, NUM_ANGLES, 1);
        if(!gun) {
            return base;
        }
        return combinePictures(base.get(), gun.get(), gunOffsetX, gunOffsetY);
    };

    auto decorateRuntimeEditorVehicle = [&](sdl2::surface_ptr vehicle,
                                             bool useCustomStar) -> sdl2::surface_ptr {
        SDL_Surface* star = useCustomStar && customStar
            ? customStar.get()
            : objPic[ObjPic_Star][HOUSE_HARKONNEN][1].get();
        if(!vehicle || !star) {
            return vehicle;
        }
        return combinePictures(vehicle.get(), star,
                               vehicle->w - star->w,
                               vehicle->h - star->h);
    };

    constexpr unsigned int customVehiclePreviews[] = {
        UI_MapEditor_RebelHarvester,
        UI_MapEditor_Deviator,
        UI_MapEditor_RocketTrike,
        UI_MapEditor_SonicTrike,
        UI_MapEditor_FlameTank,
        UI_MapEditor_EliteLauncher,
        UI_MapEditor_EliteSiegeTank,
        UI_MapEditor_ChemicalSiegeTank
    };

    for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
        for(const auto& preview : previews) {
            uiGraphicTex[preview.uiID][colorSlot].reset();
            SDL_Surface* atlas = objPic[preview.objectGraphic][colorSlot][0].get();
            if(atlas) {
                uiGraphic[preview.uiID][colorSlot] = getSubPicture(
                    atlas, preview.x, preview.y, preview.width, preview.height);
            } else {
                uiGraphic[preview.uiID][colorSlot].reset();
            }
        }

        for(const unsigned int uiID : customVehiclePreviews) {
            uiGraphicTex[uiID][colorSlot].reset();
            uiGraphic[uiID][colorSlot].reset();
        }

        const int fixedTornieGunSlot = tornieActive ? HOUSE_HARKONNEN : colorSlot;
        auto harvestank = composeRuntimeEditorVehicle(
            ObjPic_Harvester, colorSlot,
            tornieActive ? static_cast<int>(ObjPic_HarvestankGunTornie) : -1,
            colorSlot, 0, 0);
        if(harvestank) {
            uiGraphic[UI_MapEditor_RebelHarvester][colorSlot] =
                decorateRuntimeEditorVehicle(std::move(harvestank), true);
        }

        auto deviator = composeRuntimeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(deviatorEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(deviator) {
            uiGraphic[UI_MapEditor_Deviator][colorSlot] =
                decorateRuntimeEditorVehicle(std::move(deviator), false);
        }

        auto rocketTrike = getRuntimeEditorFrame(
            ObjPic_RocketTrike, colorSlot, 0, 0, NUM_ANGLES, 1);
        if(rocketTrike) {
            uiGraphic[UI_MapEditor_RocketTrike][colorSlot] =
                decorateRuntimeEditorVehicle(std::move(rocketTrike), true);
        }

        auto sonicTrike = getRuntimeEditorFrame(
            ObjPic_SonicTrike, colorSlot, 0, 0, NUM_ANGLES, 1);
        if(sonicTrike) {
            uiGraphic[UI_MapEditor_SonicTrike][colorSlot] =
                decorateRuntimeEditorVehicle(std::move(sonicTrike), true);
        }

        auto flameTank = composeRuntimeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(flameTankEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(flameTank) {
            uiGraphic[UI_MapEditor_FlameTank][colorSlot] =
                decorateRuntimeEditorVehicle(std::move(flameTank), true);
        }

        auto eliteLauncher = composeRuntimeEditorVehicle(
            ObjPic_Tank_Base, colorSlot, static_cast<int>(eliteLauncherEditorGun),
            fixedTornieGunSlot, 3, 0);
        if(eliteLauncher) {
            uiGraphic[UI_MapEditor_EliteLauncher][colorSlot] =
                decorateRuntimeEditorVehicle(std::move(eliteLauncher), true);
        }

        auto eliteSiegeTank = composeRuntimeEditorVehicle(
            ObjPic_Siegetank_Base, colorSlot,
            static_cast<int>(eliteSiegeTankEditorGun), colorSlot, 2, -4);
        if(eliteSiegeTank) {
            uiGraphic[UI_MapEditor_EliteSiegeTank][colorSlot] =
                decorateRuntimeEditorVehicle(std::move(eliteSiegeTank), true);
        }

        auto chemicalSiegeTank = composeRuntimeEditorVehicle(
            ObjPic_Siegetank_Base, colorSlot,
            static_cast<int>(chemicalSiegeTankEditorGun), colorSlot, 2, -4);
        if(chemicalSiegeTank) {
            uiGraphic[UI_MapEditor_ChemicalSiegeTank][colorSlot] =
                decorateRuntimeEditorVehicle(std::move(chemicalSiegeTank), true);
        }

        uiGraphicTex[UI_MapEditor_ChemicalCarryall][colorSlot].reset();
        SDL_Surface* carryall = objPic[ObjPic_ChemicalCarryall][colorSlot][0].get();
        if(carryall) {
            auto preview = getSubFrame(carryall, 0, 0, 8, 2);
            if(preview && customStar) {
                preview = combinePictures(preview.get(), customStar.get(),
                                          preview->w - customStar->w,
                                          preview->h - customStar->h);
            }
            uiGraphic[UI_MapEditor_ChemicalCarryall][colorSlot] = std::move(preview);
        } else {
            uiGraphic[UI_MapEditor_ChemicalCarryall][colorSlot].reset();
        }
    }
}

void GFXManager::reloadAllObjectGraphicsForActiveMod() {
    // Build the complete object set through the newly active mod, but keep
    // this manager alive: menus retain pointers to its UI and background
    // textures while a saved game is running.
    GFXManager refreshedGraphics;

    objPic = std::move(refreshedGraphics.objPic);
    objPicTex = std::move(refreshedGraphics.objPicTex);
    scoutpostBaseGraphics = std::move(refreshedGraphics.scoutpostBaseGraphics);
    chaosFactoryBaseGraphics = std::move(refreshedGraphics.chaosFactoryBaseGraphics);
    hdObjPicOverrides = std::move(refreshedGraphics.hdObjPicOverrides);

    // Editor previews and runtime portraits depend on the freshly installed
    // object surfaces, so rebuild only those mod-sensitive UI caches.
    reloadModDependentUiGraphics();
}

void GFXManager::reloadModDependentUiGraphics() {
    // House-coloured interface borders are generated from the active palette.
    // Keep the Harkonnen master surfaces: every other house is rebuilt from them.
    for(int house = 0; house < NUM_HOUSES; ++house) {
        uiGraphicTex[UI_TopBar][house].reset();
        uiGraphicTex[UI_SideBar][house].reset();
        if(house != HOUSE_HARKONNEN) {
            uiGraphic[UI_TopBar][house].reset();
            uiGraphic[UI_SideBar][house].reset();
        }
    }

    SDL_Log("GFXManager::reloadModDependentUiGraphics(): reloading active-mod presentation");
    reloadModDependentObjectGraphics();
    for(auto& detailByHouse : houseSmallDetailPicTex) {
        for(auto& texture : detailByHouse) {
            texture.reset();
        }
    }

    for(unsigned int piece = 0; piece < NUM_MAPCHOICEPIECES; ++piece) {
        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            mapChoicePiecesTex[piece][colorSlot].reset();
            if(colorSlot != HOUSE_HARKONNEN) {
                mapChoicePieces[piece][colorSlot].reset();
            }
        }
    }

    for(unsigned int id = UI_MapChoiceArrow_None; id <= UI_MapChoiceArrow_Left; ++id) {
        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            uiGraphicTex[id][colorSlot].reset();
            if(colorSlot != HOUSE_HARKONNEN) {
                uiGraphic[id][colorSlot].reset();
            }
        }
    }

    animation[Anim_NeutralPlanet].reset();
    animation[Anim_RebelsPlanet].reset();
    // Editor previews cache house-remapped surfaces. Drop every derived slot
    // when the active mod changes so house colors cannot leak between mods.
    for(unsigned int id = UI_MapEditor_SideBar; id < NUM_UIGRAPHICS; ++id) {
        for(int colorSlot = 0; colorSlot < NUM_HOUSE_COLOR_SLOTS; ++colorSlot) {
            uiGraphicTex[id][colorSlot].reset();
            if(colorSlot != HOUSE_HARKONNEN) {
                uiGraphic[id][colorSlot].reset();
            }
        }
    }

    rebuildModDependentEditorGraphics();

    auto pictureFactory = std::make_unique<PictureFactory>();
    try {
        Palette benePalette = LoadPalette_RW(pFileManager->openFile("BENE.PAL").get());
        for(int house = HOUSE_HARKONNEN; house < getNumCustomGameHouses(); ++house) {
            uiGraphicTex[UI_MentatHouseChoiceInfoQuestion][house].reset();
            uiGraphic[UI_MentatHouseChoiceInfoQuestion][house] =
                pictureFactory->createMentatHouseChoiceQuestion(house, benePalette);
        }
    } catch(const std::exception& e) {
        SDL_Log("GFXManager: house confirmation title reload failed (%s)", e.what());
    }

    auto reloadBonusHerald = [&](int house, const char* filename) {
        constexpr unsigned int presentationIds[] = {
            UI_Herald_Colored,
            UI_Herald_ColoredLarge,
            UI_Herald_Grey
        };
        for(const unsigned int id : presentationIds) {
            uiGraphic[id][house].reset();
            uiGraphicTex[id][house].reset();
        }

        if(pFileManager->exists(filename)) {
            auto herald = LoadPNG_RW(pFileManager->openFile(filename).get());
            if(herald != nullptr) {
                if(!isHouseFaction(static_cast<HOUSETYPE>(house), HOUSE_REBELS)) {
                    SDL_SetColorKey(herald.get(), SDL_TRUE, 0);
                }
                uiGraphic[UI_Herald_Colored][house] = std::move(herald);
            }
        }

        if(uiGraphic[UI_Herald_Colored][house] == nullptr) {
            SDL_Surface* fallback = uiGraphic[UI_Herald_Colored][HOUSE_HARKONNEN].get();
            if(fallback == nullptr) {
                return;
            }
            if(isHouseFaction(static_cast<HOUSETYPE>(house), HOUSE_REBELS)) {
                uiGraphic[UI_Herald_Colored][house] = copySurface(fallback);
            } else {
                uiGraphic[UI_Herald_Colored][house] =
                    mapSurfaceColorRange(fallback, PALCOLOR_HARKONNEN,
                                         getHousePaletteIndex(static_cast<HOUSETYPE>(house)));
            }
        }

        SDL_Surface* heraldSurface = uiGraphic[UI_Herald_Colored][house].get();
        uiGraphic[UI_Herald_ColoredLarge][house] =
            heraldSurface->format->BytesPerPixel == 1
                ? Scaler::defaultDoubleSurface(heraldSurface)
                : Scaler::doubleSurfaceNN(heraldSurface);
        uiGraphic[UI_Herald_Grey][house] =
            pictureFactory->createGreyHouseChoice(heraldSurface);
    };

    auto getHeraldFilename = [](HOUSETYPE house) -> const char* {
        switch(getHouseFactionIdentity(house)) {
            case HOUSE_NEUTRAL: return "HeraldNeu.png";
            case HOUSE_REBELS: return "HeraldRebels.png";
            case HOUSE_WILDSPADE: return "HeraldWildspade.png";
            case HOUSE_KLESHMERSH: return "HeraldKleshmersh.png";
            default: return nullptr;
        }
    };
    for(const HOUSETYPE house : {
            HOUSE_NEUTRAL, HOUSE_REBELS, HOUSE_WILDSPADE, HOUSE_KLESHMERSH }) {
        const char* filename = getHeraldFilename(house);
        if(filename != nullptr && isCustomGameHouseAvailable(house)) {
            reloadBonusHerald(house, filename);
        }
    }
    reloadRuntimeModPortraits();
    loadCustomHouseHerald();
    loadMentatGraphics();
}

Animation* GFXManager::getMentatEyesAnimation(int house) {
    if(house >= 0 && house < NUM_HOUSE_COLOR_SLOTS && modMentatEyes[house] != nullptr) {
        return modMentatEyes[house].get();
    }

    switch(ModManager::instance().getEffectiveMentatIdentity(house)) {
        case HOUSE_ATREIDES: return getAnimation(Anim_AtreidesEyes);
        case HOUSE_ORDOS: return getAnimation(Anim_OrdosEyes);
        case HOUSE_FREMEN: return getAnimation(Anim_FremenEyes);
        case HOUSE_SARDAUKAR: return getAnimation(Anim_SardaukarEyes);
        case HOUSE_MERCENARY: return getAnimation(Anim_MercenaryEyes);
        default: return getAnimation(Anim_HarkonnenEyes);
    }
}

Animation* GFXManager::getMentatMouthAnimation(int house) {
    if(house >= 0 && house < NUM_HOUSE_COLOR_SLOTS && modMentatMouth[house] != nullptr) {
        return modMentatMouth[house].get();
    }

    switch(ModManager::instance().getEffectiveMentatIdentity(house)) {
        case HOUSE_ATREIDES: return getAnimation(Anim_AtreidesMouth);
        case HOUSE_ORDOS: return getAnimation(Anim_OrdosMouth);
        case HOUSE_FREMEN: return getAnimation(Anim_FremenMouth);
        case HOUSE_SARDAUKAR: return getAnimation(Anim_SardaukarMouth);
        case HOUSE_MERCENARY: return getAnimation(Anim_MercenaryMouth);
        default: return getAnimation(Anim_HarkonnenMouth);
    }
}

SDL_Texture* GFXManager::getMentatForeground(int house) {
    if(house < 0 || house >= NUM_HOUSE_COLOR_SLOTS || modMentatForeground[house] == nullptr) {
        return nullptr;
    }

    if(modMentatForegroundTex[house] == nullptr) {
        modMentatForegroundTex[house] = convertSurfaceToTexture(modMentatForeground[house].get());
    }
    return modMentatForegroundTex[house].get();
}

bool GFXManager::hasObjPic(unsigned int id, int house, unsigned int z) const {
    if(id >= NUM_OBJPICS || z >= NUM_ZOOMLEVEL) {
        return false;
    }
    house = usesSharedCityAtlas(id) ? HOUSE_HARKONNEN : getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        return false;
    }
    return objPic[id][house][z] != nullptr;
}

SDL_Texture* GFXManager::getZoomedObjPic(unsigned int id, int house, unsigned int z) {
    if(id >= NUM_OBJPICS) {
        THROW(std::invalid_argument, "GFXManager::getZoomedObjPic(): Unit Picture with ID %u is not available!", id);
    }
    house = usesSharedCityAtlas(id) ? HOUSE_HARKONNEN : getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    if(objPic[id][house][z] == nullptr) {
        // remap to this color
        if(objPic[id][HOUSE_HARKONNEN][z] == nullptr) {
            // DuneCity civic sprites: fall back to ConstructionYard instead of crashing
            static const unsigned int duneCityCivicIds[] = {
                ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
                ObjPic_Airport, ObjPic_Hospital, ObjPic_Church
            };
            // Tornie mod sprites with optional dedicated graphics: fall back to
            // their closest vanilla equivalent when the dedicated PNG is missing.
            static const unsigned int tornieModSpriteIds[] = {
                ObjPic_RocketTrike, ObjPic_SonicTrike, ObjPic_FlameTankGunTornie,
                ObjPic_EliteSiegeTankGunTornie, ObjPic_ChemicalSiegeTankGunTornie, ObjPic_DeviatorGunTornie,
                ObjPic_EliteLauncherGunTornie, ObjPic_RebelSonicTankGun,
                ObjPic_HarvestankGunTornie, ObjPic_ChemicalCarryall,
                ObjPic_AdvancedWindTrap, ObjPic_AdvancedWindTrap2x3, ObjPic_AdvancedWindTrap3x2,
                ObjPic_RebelHarvester,  // falls back to vanilla Harvester
                ObjPic_Worfinery,       // falls back to vanilla WOR
                ObjPic_TechCenter,      // falls back to vanilla Palace
                ObjPic_Scoutpost,       // falls back to vanilla Rocket Turret
                ObjPic_Flamepost,       // falls back to vanilla Rocket Turret
                ObjPic_Chemipost,       // falls back to vanilla Rocket Turret
                ObjPic_LoveFactory,     // falls back to vanilla Heavy Factory
                ObjPic_ChaosFactory     // falls back to vanilla Heavy Factory
            };
            bool isDuneCityCivic = false;
            for(auto cid : duneCityCivicIds) {
                if(id == cid) { isDuneCityCivic = true; break; }
            }
            bool isTornieModSprite = false;
            for(auto tid : tornieModSpriteIds) {
                if(id == tid) { isTornieModSprite = true; break; }
            }
            if(isDuneCityCivic && objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]) {
                SDL_Log("GFXManager::getZoomedObjPic(): DuneCity civic sprite ID %u not loaded, falling back to ConstructionYard", id);
                objPic[id][HOUSE_HARKONNEN][z] = sdl2::surface_ptr{
                    SDL_ConvertSurface(objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z].get(),
                                       objPic[ObjPic_ConstructionYard][HOUSE_HARKONNEN][z]->format, 0)
                };
            } else if(isTornieModSprite) {
                unsigned int fallbackId = ObjPic_Tank_Base;
                if(id == ObjPic_RebelHarvester) {
                    fallbackId = ObjPic_Harvester;
                } else if(id == ObjPic_SonicTrike) {
                    fallbackId = ObjPic_Trike;
                } else if(id == ObjPic_ChemicalCarryall) {
                    fallbackId = ObjPic_Carryall;
                } else if(id == ObjPic_DeviatorGunTornie
                          || id == ObjPic_FlameTankGunTornie
                          || id == ObjPic_EliteLauncherGunTornie) {
                    fallbackId = ObjPic_Launcher_Gun;
                } else if(id == ObjPic_EliteSiegeTankGunTornie
                          || id == ObjPic_ChemicalSiegeTankGunTornie) {
                    fallbackId = ObjPic_Siegetank_Gun;
                } else if(id == ObjPic_RebelSonicTankGun) {
                    fallbackId = ObjPic_Sonictank_Gun;
                } else if(id == ObjPic_HarvestankGunTornie) {
                    fallbackId = ObjPic_Siegetank_Gun;
                } else if(id == ObjPic_Worfinery) {
                    fallbackId = ObjPic_WOR;
                } else if(id == ObjPic_TechCenter) {
                    fallbackId = ObjPic_Palace;
                } else if(id == ObjPic_AdvancedWindTrap || id == ObjPic_AdvancedWindTrap2x3 || id == ObjPic_AdvancedWindTrap3x2) {
                    fallbackId = ObjPic_Windtrap;
                } else if(id == ObjPic_Scoutpost
                          || id == ObjPic_Flamepost
                          || id == ObjPic_Chemipost) {
                    fallbackId = ObjPic_RocketTurret;
                } else if(id == ObjPic_LoveFactory
                          || id == ObjPic_ChaosFactory) {
                    fallbackId = ObjPic_HeavyFactory;
                }
                if(objPic[fallbackId][HOUSE_HARKONNEN][z] == nullptr) {
                    return nullptr;
                }
                SDL_Log("GFXManager::getZoomedObjPic(): Tornie sprite ID %u not loaded, falling back to sprite ID %u", id, fallbackId);
                objPic[id][HOUSE_HARKONNEN][z] = sdl2::surface_ptr{
                    SDL_ConvertSurface(objPic[fallbackId][HOUSE_HARKONNEN][z].get(),
                                       objPic[fallbackId][HOUSE_HARKONNEN][z]->format, 0)
                };
            } else {
                THROW(std::runtime_error, "GFXManager::getZoomedObjPic(): Unit Picture with ID %u is not loaded!", id);
            }
        }

        if(objPic[id][HOUSE_HARKONNEN][z]->format->BytesPerPixel == 1) {
            objPic[id][house][z] = mapSurfaceColorRange(objPic[id][HOUSE_HARKONNEN][z].get(), PALCOLOR_HARKONNEN, getVisualRemapPaletteIndex(house));
            applyCustomVisualColorRamp(objPic[id][house][z].get(), house);
            if(isTornieRebelsColorSlot(house)) {
                applyRebelsTint(objPic[id][house][z].get(), house);
            }
            normalizeTransparentPaletteIndexes(objPic[id][house][z].get());
            if(z == 0 && isTornieStructureObjPic(id)) {
                const Coord tiles = objPicTiles[id];
                const int frameWidth = (tiles.x > 0) ? objPic[id][house][z]->w / tiles.x : objPic[id][house][z]->w;
                const int frameHeight = (tiles.y > 0) ? objPic[id][house][z]->h / tiles.y : objPic[id][house][z]->h;
                logTornieStructureSurfaceDiagnostics("house-remapped", getTornieStructureObjPicName(id), objPic[id][house][z].get(), frameWidth, frameHeight);
            }
        } else {
            objPic[id][house][z] =
                remapTruecolorHouseColorRange(objPic[id][HOUSE_HARKONNEN][z].get(), house, id == ObjPic_TechCenter ? 7 : 8);
            if(objPic[id][house][z] == nullptr) {
                objPic[id][house][z] =
                    copySurface(objPic[id][HOUSE_HARKONNEN][z].get());
            }
        }
    }

    if(objPicTex[id][house][z] == nullptr) {
        // now convert to display format
        if(id == ObjPic_Windtrap) {
            // Windtrap uses palette animation on PALCOLOR_WINDTRAP_COLORCYCLE; fake this
            objPicTex[id][house][z] = convertSurfaceToTexture(generateWindtrapAnimationFrames(objPic[id][house][z].get()));
        } else if(id == ObjPic_Bullet_SonicTemp) {
            objPicTex[id][house][z] = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, objPic[id][house][z]->w, objPic[id][house][z]->h) };
        } else if(id == ObjPic_SandwormShimmerTemp) {
            objPicTex[id][house][z] = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, objPic[id][house][z]->w, objPic[id][house][z]->h) };
        } else if(isTornieStructureObjPic(id)) {
            // The custom Tornie structure sprites are already palette-indexed and
            // normalized above. Converting them through the special RGBA mask path
            // can turn every pixel transparent, which leaves an invisible but still
            // selectable structure in-game. Use the same indexed-surface texture
            // conversion as vanilla structures so the palette colorkey is preserved.
            objPicTex[id][house][z] = convertSurfaceToTexture(objPic[id][house][z].get());
        } else {
            objPicTex[id][house][z] = convertSurfaceToTexture(objPic[id][house][z].get());
        }

        // Truecolor RGBA sprites need explicit blend mode so their alpha
        // channel is respected during rendering; without this, transparent
        // pixels appear as solid black.
        if(objPic[id][house][z]->format->BytesPerPixel != 1
           || id == ObjPic_ZoneResidential || id == ObjPic_ZoneCommercial
           || id == ObjPic_ZoneIndustrial || id == ObjPic_CityRoad
           || id == ObjPic_NuclearPlant || id == ObjPic_PoliceStation
           || id == ObjPic_Stadium || id == ObjPic_Airport
           || id == ObjPic_Hospital || id == ObjPic_Church
           || id == ObjPic_Windtrap || id == ObjPic_AdvancedWindTrap
           || id == ObjPic_AdvancedWindTrap2x3 || id == ObjPic_AdvancedWindTrap3x2
           || id == ObjPic_Worfinery || id == ObjPic_TechCenter || id == ObjPic_Scoutpost || id == ObjPic_LoveFactory
           || id == ObjPic_Star) {
            if(objPicTex[id][house][z]) {
                SDL_SetTextureBlendMode(objPicTex[id][house][z].get(), SDL_BLENDMODE_BLEND);
            }
        }

        if(z == 0 && (isTornieStructureObjPic(id) || id == ObjPic_SonicTrike)) {
            int textureWidth = 0;
            int textureHeight = 0;
            Uint32 textureFormat = 0;
            int textureAccess = 0;
            SDL_BlendMode textureBlend = SDL_BLENDMODE_NONE;
            Uint8 textureAlpha = 0;
            Uint8 textureRed = 0;
            Uint8 textureGreen = 0;
            Uint8 textureBlue = 0;
            if(objPicTex[id][house][z]) {
                SDL_QueryTexture(objPicTex[id][house][z].get(), &textureFormat, &textureAccess, &textureWidth, &textureHeight);
                SDL_GetTextureBlendMode(objPicTex[id][house][z].get(), &textureBlend);
                SDL_GetTextureAlphaMod(objPicTex[id][house][z].get(), &textureAlpha);
                SDL_GetTextureColorMod(objPicTex[id][house][z].get(), &textureRed, &textureGreen, &textureBlue);
            }
            const char* diagnosticName = (id == ObjPic_SonicTrike)
                                             ? "SonicTrike"
                                             : getTornieStructureObjPicName(id);
            SDL_Log("TornieGFX: texture-ready %s house=%d z=%u texture=%dx%d format=%u access=%d blend=%d alpha=%u color=%u,%u,%u",
                    diagnosticName,
                    house,
                    z,
                    textureWidth,
                    textureHeight,
                    textureFormat,
                    textureAccess,
                    static_cast<int>(textureBlend),
                    textureAlpha,
                    textureRed,
                    textureGreen,
                    textureBlue);
        }
    }

    return objPicTex[id][house][z].get();
}

zoomable_texture GFXManager::getObjPic(unsigned int id, int house) {
    if(id >= NUM_OBJPICS) {
        THROW(std::invalid_argument, "GFXManager::getObjPic(): Unit Picture with ID %u is not available!", id);
    }

    const int requestedHouse = house;
    int visualHouse = usesSharedCityAtlas(id) ? HOUSE_HARKONNEN : getHouseVisualHouse(requestedHouse);
    if(!isValidHouseColorSlot(visualHouse)) {
        visualHouse = HOUSE_HARKONNEN;
    }

    for(int z = 0; z < NUM_ZOOMLEVEL; z++) {
        if(objPicTex[id][visualHouse][z] == nullptr) {
            // Pass the house identity, not the already-resolved color slot.
            // getZoomedObjPic() performs the visual mapping exactly once.
            getZoomedObjPic(id, requestedHouse, z);
        }
    }

    return zoomable_texture{ objPicTex[id][visualHouse][0].get(), objPicTex[id][visualHouse][1].get(), objPicTex[id][visualHouse][2].get() };
}


SDL_Texture* GFXManager::getSmallDetailPic(unsigned int id) {
    if(id >= NUM_SMALLDETAILPICS) {
        return nullptr;
    }
    return smallDetailPicTex[id].get();
}


SDL_Texture* GFXManager::getSmallDetailPic(unsigned int id, int house) {
    if(id >= NUM_SMALLDETAILPICS) {
        return nullptr;
    }

    if(house >= 0 && house < static_cast<int>(NUM_HOUSES)
       && duneCityHouseUsesDune2[house]
       && duneCityDune2DetailPicTex[id][house]) {
        return duneCityDune2DetailPicTex[id][house].get();
    }

    const int visualHouse = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(visualHouse)
       || (visualHouse != HOUSE_CUSTOM && !isCustomHouseColorSlot(visualHouse))) {
        return smallDetailPicTex[id].get();
    }

    unsigned int editorGraphic = NUM_UIGRAPHICS;
    switch(id) {
        case Picture_Flamepost:       editorGraphic = UI_MapEditor_Flamepost;        break;
        case Picture_Carryall:         editorGraphic = UI_MapEditor_Carryall;          break;
        case Picture_Devastator:       editorGraphic = UI_MapEditor_Devastator;        break;
        case Picture_Deviator:         editorGraphic = UI_MapEditor_Deviator;          break;
        case Picture_Harvester:        editorGraphic = UI_MapEditor_Harvester;         break;
        case Picture_Harvestank:       editorGraphic = UI_MapEditor_RebelHarvester;    break;
        case Picture_Launcher:         editorGraphic = UI_MapEditor_Launcher;          break;
        case Picture_MCV:              editorGraphic = UI_MapEditor_MCV;               break;
        case Picture_Ornithopter:      editorGraphic = UI_MapEditor_Ornithopter;       break;
        case Picture_Quad:             editorGraphic = UI_MapEditor_Quad;              break;
        case Picture_RaiderTrike:      editorGraphic = UI_MapEditor_Raider;            break;
        case Picture_SiegeTank:        editorGraphic = UI_MapEditor_SiegeTank;         break;
        case Picture_SonicTank:        editorGraphic = UI_MapEditor_SonicTank;         break;
        case Picture_Tank:             editorGraphic = UI_MapEditor_Tank;              break;
        case Picture_Trike:            editorGraphic = UI_MapEditor_Trike;             break;
        case Picture_RocketTrike:      editorGraphic = UI_MapEditor_RocketTrike;       break;
        case Picture_SonicTrike:       editorGraphic = UI_MapEditor_SonicTrike;        break;
        case Picture_FlameTank:        editorGraphic = UI_MapEditor_FlameTank;         break;
        case Picture_EliteLauncher:    editorGraphic = UI_MapEditor_EliteLauncher;     break;
        case Picture_EliteSiegeTank:   editorGraphic = UI_MapEditor_EliteSiegeTank;    break;
        case Picture_ChemicalSiegeTank: editorGraphic = UI_MapEditor_ChemicalSiegeTank; break;
        default:                                                                    break;
    }

    if(editorGraphic == NUM_UIGRAPHICS) {
        return smallDetailPicTex[id].get();
    }

    auto& cachedTexture = houseSmallDetailPicTex[id][visualHouse];
    if(cachedTexture == nullptr) {
        SDL_Surface* icon = getUIGraphicSurface(editorGraphic, visualHouse);
        if(icon == nullptr || icon->w <= 0 || icon->h <= 0) {
            return smallDetailPicTex[id].get();
        }

        sdl2::surface_ptr canvas{
            SDL_CreateRGBSurface(0, 91, 55, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK)
        };
        if(canvas == nullptr) {
            return smallDetailPicTex[id].get();
        }

        SDL_SetSurfaceBlendMode(canvas.get(), SDL_BLENDMODE_BLEND);
        SDL_FillRect(canvas.get(), nullptr, SDL_MapRGBA(canvas->format, 0, 0, 0, 0));

        int width = 87;
        int height = icon->h * width / icon->w;
        if(height > 51) {
            height = 51;
            width = icon->w * height / icon->h;
        }

        SDL_Rect destination = { (91 - width) / 2, (55 - height) / 2, width, height };
        if(SDL_BlitScaled(icon, nullptr, canvas.get(), &destination) != 0) {
            return smallDetailPicTex[id].get();
        }

        cachedTexture = convertSurfaceToTexture(canvas.get());
    }

    return cachedTexture ? cachedTexture.get() : smallDetailPicTex[id].get();
}
SDL_Texture* GFXManager::getTinyPicture(unsigned int id) {
    if(id >= NUM_TINYPICTURE) {
        return nullptr;
    }
    return tinyPictureTex[id].get();
}


SDL_Surface* GFXManager::getUIGraphicSurface(unsigned int id, int house) {
    if(id >= NUM_UIGRAPHICS) {
        THROW(std::invalid_argument, "GFXManager::getUIGraphicSurface(): UI Graphic with ID %u is not available!", id);
    }
    const bool useHouseIdentity =
        id == UI_Herald_Colored
        || id == UI_Herald_ColoredLarge
        || id == UI_Herald_Grey
        || id == UI_MentatHouseChoiceInfoQuestion;
    if(!useHouseIdentity) {
        house = getHouseVisualHouse(house);
    }
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    if(uiGraphic[id][house] == nullptr) {
        // remap to this color
        if(uiGraphic[id][HOUSE_HARKONNEN] == nullptr) {
            unsigned int fallbackID = NUM_UIGRAPHICS;
            switch(id) {
                case UI_MapEditor_AdvancedWindTrap:
                case UI_MapEditor_AdvancedWindTrapMK2:
                case UI_MapEditor_AdvancedWindTrapMK3:
                    fallbackID = UI_MapEditor_Windtrap;
                    break;
                case UI_MapEditor_Worfinery:
                    fallbackID = UI_MapEditor_WOR;
                    break;
                case UI_MapEditor_TechCenter:
                    fallbackID = UI_MapEditor_Palace;
                    break;
                case UI_MapEditor_Scoutpost:
                case UI_MapEditor_Flamepost:
                case UI_MapEditor_Chemipost:
                    fallbackID = UI_MapEditor_RocketTurret;
                    break;
                case UI_MapEditor_LoveFactory:
                    fallbackID = UI_MapEditor_HighTechFactory;
                    break;
                case UI_MapEditor_ChaosFactory:
                    fallbackID = UI_MapEditor_Starport;
                    break;
                case UI_MapEditor_RebelHarvester:
                    fallbackID = UI_MapEditor_Harvester;
                    break;
                case UI_MapEditor_RocketTrike:
                case UI_MapEditor_SonicTrike:
                    fallbackID = UI_MapEditor_Raider;
                    break;
                case UI_MapEditor_FlameTank:
                case UI_MapEditor_EliteLauncher:
                    fallbackID = UI_MapEditor_Launcher;
                    break;
                case UI_MapEditor_EliteSiegeTank:
                case UI_MapEditor_ChemicalSiegeTank:
                    fallbackID = UI_MapEditor_SiegeTank;
                    break;
                case UI_MapEditor_ChemicalCarryall:
                    fallbackID = UI_MapEditor_Carryall;
                    break;
                default:
                    break;
            }
            if(fallbackID != NUM_UIGRAPHICS) {
                return getUIGraphicSurface(fallbackID, house);
            }
            THROW(std::runtime_error, "GFXManager::getUIGraphicSurface(): UI Graphic with ID %u is not loaded!", id);
        }

        SDL_Surface* base = uiGraphic[id][HOUSE_HARKONNEN].get();
        if(base->format->BytesPerPixel == 1) {
            uiGraphic[id][house] = mapSurfaceColorRange(base, PALCOLOR_HARKONNEN,
                                                       getVisualRemapPaletteIndex(house));
            applyCustomVisualColorRamp(uiGraphic[id][house].get(), house);
            if(isTornieRebelsColorSlot(house)) {
                applyRebelsTint(uiGraphic[id][house].get(), house);
            }
        } else {
            // Truecolor icons need a pixel-safe team-ramp remap. Recolour only
            // exact Harkonnen team shades and preserve every other RGBA pixel.
            uiGraphic[id][house] = remapTruecolorHouseColorRange(base, house, id == UI_MapEditor_TechCenter ? 7 : 8);
            if(uiGraphic[id][house] == nullptr) {
                uiGraphic[id][house] = copySurface(base);
            }
        }
    }

    return uiGraphic[id][house].get();
}

SDL_Texture* GFXManager::getUIGraphic(unsigned int id, int house) {
    if(id >= NUM_UIGRAPHICS) {
        THROW(std::invalid_argument, "GFXManager::getUIGraphic(): UI Graphic with ID %u is not available!", id);
    }

    const int requestedHouse = house;
    const bool useHouseIdentity =
        id == UI_Herald_Colored
        || id == UI_Herald_ColoredLarge
        || id == UI_Herald_Grey
        || id == UI_MentatHouseChoiceInfoQuestion;
    int visualHouse = useHouseIdentity ? requestedHouse : getHouseVisualHouse(requestedHouse);
    if(!isValidHouseColorSlot(visualHouse)) {
        visualHouse = HOUSE_HARKONNEN;
    }

    if(uiGraphicTex[id][visualHouse] == nullptr) {
        // Preserve the original house identity so the surface resolver maps it once.
        SDL_Surface* pSurface = getUIGraphicSurface(id, requestedHouse);

        if(id >= UI_MapChoiceArrow_None && id <= UI_MapChoiceArrow_Left) {
            uiGraphicTex[id][visualHouse] = convertSurfaceToTexture(generateMapChoiceArrowFrames(pSurface, visualHouse));
        } else {
            uiGraphicTex[id][visualHouse] = convertSurfaceToTexture(pSurface);
        }
    }

    return uiGraphicTex[id][visualHouse].get();
}

SDL_Surface* GFXManager::getMapChoicePieceSurface(unsigned int num, int house) {
    if(num >= NUM_MAPCHOICEPIECES) {
        THROW(std::invalid_argument, "GFXManager::getMapChoicePieceSurface(): Map Piece with number %u is not available!", num);
    }
    house = getHouseVisualHouse(house);
    if(!isValidHouseColorSlot(house)) {
        house = HOUSE_HARKONNEN;
    }

    if(mapChoicePieces[num][house] == nullptr) {
        // remap to this color
        if(mapChoicePieces[num][HOUSE_HARKONNEN] == nullptr) {
            THROW(std::runtime_error, "GFXManager::getMapChoicePieceSurface(): Map Piece with number %u is not loaded!", num);
        }

        mapChoicePieces[num][house] = mapSurfaceColorRange(mapChoicePieces[num][HOUSE_HARKONNEN].get(), PALCOLOR_HARKONNEN, getVisualRemapPaletteIndex(house));
        applyCustomVisualColorRamp(mapChoicePieces[num][house].get(), house);
        if(isTornieRebelsColorSlot(house)) {
            applyRebelsTint(mapChoicePieces[num][house].get(), house);
        }
    }

    return mapChoicePieces[num][house].get();
}

SDL_Texture* GFXManager::getMapChoicePiece(unsigned int num, int house) {
    if(num >= NUM_MAPCHOICEPIECES) {
        THROW(std::invalid_argument, "GFXManager::getMapChoicePiece(): Map Piece with number %u is not available!", num);
    }

    const int requestedHouse = house;
    int visualHouse = getHouseVisualHouse(requestedHouse);
    if(!isValidHouseColorSlot(visualHouse)) {
        visualHouse = HOUSE_HARKONNEN;
    }

    if(mapChoicePiecesTex[num][visualHouse] == nullptr) {
        mapChoicePiecesTex[num][visualHouse] = convertSurfaceToTexture(
            getMapChoicePieceSurface(num, requestedHouse));
    }

    return mapChoicePiecesTex[num][visualHouse].get();
}

Animation* GFXManager::getAnimation(unsigned int id) {
    if(id >= NUM_ANIMATION) {
        THROW(std::invalid_argument, "GFXManager::getAnimation(): Animation with ID %u is not available!", id);
    }

    if(animation[id] == nullptr) {
        switch(id) {
            case Anim_HarkonnenPlanet: {
                animation[Anim_HarkonnenPlanet] = loadAnimationFromWsa("FHARK.WSA");
                animation[Anim_HarkonnenPlanet]->setFrameRate(10);
            } break;

            case Anim_AtreidesPlanet: {
                animation[Anim_AtreidesPlanet] = loadAnimationFromWsa("FARTR.WSA");
                animation[Anim_AtreidesPlanet]->setFrameRate(10);
            } break;

            case Anim_OrdosPlanet: {
                animation[Anim_OrdosPlanet] = loadAnimationFromWsa("FORDOS.WSA");
                animation[Anim_OrdosPlanet]->setFrameRate(10);
            } break;

            case Anim_FremenPlanet: {
                animation[Anim_FremenPlanet] = PictureFactory::createFremenPlanet(uiGraphic[UI_Herald_ColoredLarge][HOUSE_FREMEN].get());
                animation[Anim_FremenPlanet]->setFrameRate(10);
            } break;

            case Anim_SardaukarPlanet: {
                animation[Anim_SardaukarPlanet] = PictureFactory::createSardaukarPlanet(getAnimation(Anim_OrdosPlanet), uiGraphic[UI_Herald_ColoredLarge][HOUSE_SARDAUKAR].get());
                animation[Anim_SardaukarPlanet]->setFrameRate(10);
            } break;

            case Anim_MercenaryPlanet: {
                animation[Anim_MercenaryPlanet] = PictureFactory::createMercenaryPlanet(getAnimation(Anim_AtreidesPlanet), uiGraphic[UI_Herald_ColoredLarge][HOUSE_MERCENARY].get());
                animation[Anim_MercenaryPlanet]->setFrameRate(10);
            } break;

            case Anim_NeutralPlanet: {
                animation[Anim_NeutralPlanet] = PictureFactory::createNeutralPlanet(getAnimation(Anim_HarkonnenPlanet), uiGraphic[UI_Herald_ColoredLarge][HOUSE_NEUTRAL].get());
                animation[Anim_NeutralPlanet]->setFrameRate(10);
            } break;

            case Anim_RebelsPlanet: {
                animation[Anim_RebelsPlanet] = PictureFactory::createRebelsPlanet(getAnimation(Anim_AtreidesPlanet), uiGraphic[UI_Herald_ColoredLarge][HOUSE_REBELS].get());
                animation[Anim_RebelsPlanet]->setFrameRate(10);
            } break;

            case Anim_Win1:             animation[Anim_Win1] = loadAnimationFromWsa("WIN1.WSA");                 break;
            case Anim_Win2:             animation[Anim_Win2] = loadAnimationFromWsa("WIN2.WSA");                 break;
            case Anim_Lose1:            animation[Anim_Lose1] = loadAnimationFromWsa("LOSTBILD.WSA");            break;
            case Anim_Lose2:            animation[Anim_Lose2] = loadAnimationFromWsa("LOSTVEHC.WSA");            break;
            case Anim_Barracks:         animation[Anim_Barracks] = loadAnimationFromWsa("BARRAC.WSA");           break;
            case Anim_Carryall:         animation[Anim_Carryall] = loadAnimationFromWsa("CARRYALL.WSA");         break;
            case Anim_ConstructionYard: animation[Anim_ConstructionYard] = loadAnimationFromWsa("CONSTRUC.WSA"); break;
            case Anim_Fremen:           animation[Anim_Fremen] = loadAnimationFromWsa("FREMEN.WSA");             break;
            case Anim_DeathHand:        animation[Anim_DeathHand] = loadAnimationFromWsa("GOLD-BB.WSA");         break;
            case Anim_Devastator:       animation[Anim_Devastator] = loadAnimationFromWsa("HARKTANK.WSA");       break;
            case Anim_Harvester:        animation[Anim_Harvester] = loadAnimationFromWsa("HARVEST.WSA");         break;
            case Anim_Radar:            animation[Anim_Radar] = loadAnimationFromWsa("HEADQRTS.WSA");            break;
            case Anim_HighTechFactory:  animation[Anim_HighTechFactory] = loadAnimationFromWsa("HITCFTRY.WSA");  break;
            case Anim_SiegeTank:        animation[Anim_SiegeTank] = loadAnimationFromWsa("HTANK.WSA");           break;
            case Anim_HeavyFactory:     animation[Anim_HeavyFactory] = loadAnimationFromWsa("HVYFTRY.WSA");      break;
            case Anim_Trooper:          animation[Anim_Trooper] = loadAnimationFromWsa("HYINFY.WSA");            break;
            case Anim_Infantry:         animation[Anim_Infantry] = loadAnimationFromWsa("INFANTRY.WSA");         break;
            case Anim_IX:               animation[Anim_IX] = loadAnimationFromWsa("IX.WSA");                     break;
            case Anim_LightFactory:     animation[Anim_LightFactory] = loadAnimationFromWsa("LITEFTRY.WSA");     break;
            case Anim_Tank:             animation[Anim_Tank] = loadAnimationFromWsa("LTANK.WSA");                break;
            case Anim_MCV:              animation[Anim_MCV] = loadAnimationFromWsa("MCV.WSA");                   break;
            case Anim_Deviator:         animation[Anim_Deviator] = loadAnimationFromWsa("ORDRTANK.WSA");         break;
            case Anim_Ornithopter:      animation[Anim_Ornithopter] = loadAnimationFromWsa("ORNI.WSA");          break;
            case Anim_Raider:           animation[Anim_Raider] = loadAnimationFromWsa("OTRIKE.WSA");             break;
            case Anim_Palace:           animation[Anim_Palace] = loadAnimationFromWsa("PALACE.WSA");             break;
            case Anim_Quad:             animation[Anim_Quad] = loadAnimationFromWsa("QUAD.WSA");                 break;
            case Anim_Refinery:         animation[Anim_Refinery] = loadAnimationFromWsa("REFINERY.WSA");         break;
            case Anim_RepairYard:       animation[Anim_RepairYard] = loadAnimationFromWsa("REPAIR.WSA");         break;
            case Anim_Launcher:         animation[Anim_Launcher] = loadAnimationFromWsa("RTANK.WSA");            break;
            case Anim_RocketTurret:     animation[Anim_RocketTurret] = loadAnimationFromWsa("RTURRET.WSA");      break;
            case Anim_Saboteur:         animation[Anim_Saboteur] = loadAnimationFromWsa("SABOTURE.WSA");         break;
            case Anim_Slab1:            animation[Anim_Slab1] = loadAnimationFromWsa("SLAB.WSA");                break;
            case Anim_SonicTank:        animation[Anim_SonicTank] = loadAnimationFromWsa("STANK.WSA");           break;
            case Anim_StarPort:         animation[Anim_StarPort] = loadAnimationFromWsa("STARPORT.WSA");         break;
            case Anim_Silo:             animation[Anim_Silo] = loadAnimationFromWsa("STORAGE.WSA");              break;
            case Anim_Trike:            animation[Anim_Trike] = loadAnimationFromWsa("TRIKE.WSA");               break;
            case Anim_GunTurret:        animation[Anim_GunTurret] = loadAnimationFromWsa("TURRET.WSA");          break;
            case Anim_Wall:             animation[Anim_Wall] = loadAnimationFromWsa("WALL.WSA");                 break;
            case Anim_WindTrap:         animation[Anim_WindTrap] = loadAnimationFromWsa("WINDTRAP.WSA");         break;
            case Anim_WOR:              animation[Anim_WOR] = loadAnimationFromWsa("WOR.WSA");                   break;
            case Anim_Sandworm:         animation[Anim_Sandworm] = loadAnimationFromWsa("WORM.WSA");             break;
            case Anim_Sardaukar:        animation[Anim_Sardaukar] = loadAnimationFromWsa("SARDUKAR.WSA");        break;
            case Anim_Frigate: {
                if(pFileManager->exists("FRIGATE.WSA")) {
                    animation[Anim_Frigate] = loadAnimationFromWsa("FRIGATE.WSA");
                } else {
                    // US-Version 1.07 does not contain FRIGATE.WSA
                    // We replace it with the starport
                    animation[Anim_Frigate] = loadAnimationFromWsa("STARPORT.WSA");
                }
            } break;
            case Anim_Slab4:            animation[Anim_Slab4] = loadAnimationFromWsa("4SLAB.WSA");               break;

            default: {
                THROW(std::runtime_error, "GFXManager::getAnimation(): Invalid animation ID %u", id);
            } break;
        }

        if(id >= Anim_Barracks && id <= Anim_Slab4) {
            animation[id]->setFrameRate(6);
        }
    }

    return animation[id].get();
}

std::unique_ptr<Shpfile> GFXManager::loadShpfile(const std::string& filename) const {
    try {
        return std::make_unique<Shpfile>(pFileManager->openFile(filename).get());
    } catch (std::exception &e) {
        THROW(std::runtime_error, "Error in file \"" + filename + "\":" + e.what());
    }
}

std::unique_ptr<Wsafile> GFXManager::loadWsafile(const std::string& filename) const {
    try {
        return std::make_unique<Wsafile>(pFileManager->openFile(filename).get());
    } catch (std::exception &e) {
        THROW(std::runtime_error, std::string("Error in file \"" + filename + "\":") + e.what());
    }
}

sdl2::texture_ptr GFXManager::extractSmallDetailPic(const std::string& filename) const
{
    sdl2::surface_ptr pSurface{ SDL_CreateRGBSurface(0, 91, 55, 8, 0, 0, 0, 0) };

    // create new picture surface
    if (pSurface == nullptr) {
        THROW(sdl_error, "Cannot create new surface: %s!", SDL_GetError());
    }

    { // Scope
        auto myWsafile = std::make_unique<Wsafile>(pFileManager->openFile(filename).get());

        sdl2::surface_ptr tmp{ myWsafile->getPicture(0) };
        if(tmp == nullptr) {
            THROW(std::runtime_error, "Cannot decode first frame in file '%s'!", filename);
        }

        if((tmp->w != 184) || (tmp->h != 112)) {
            THROW(std::runtime_error, "Picture '%s' is not of size 184x112!", filename);
        }

        palette.applyToSurface(pSurface.get());

        sdl2::surface_lock lock_out{ pSurface.get() };
        sdl2::surface_lock lock_in{ tmp.get() };

        char * RESTRICT const out = static_cast<char*>(lock_out.pixels());
        const char * RESTRICT const in = static_cast<const char*>(lock_in.pixels());

        //Now we can copy pixel by pixel
        for (auto y = 0; y < 55; y++) {
            for (auto x = 0; x < 91; x++) {
                out[y*pSurface->pitch + x] = in[((y * 2) + 1)*tmp->pitch + (x * 2) + 1];
            }
        }
    }

    sdl2::texture_ptr texture = convertSurfaceToTexture(pSurface.get());
    if(texture == nullptr) {
        SDL_Log("Warning: Failed to create texture for small detail pic '%s'", filename.c_str());
        // Return a nullptr texture instead of crashing
        return nullptr;
    }
    return texture;
}

std::unique_ptr<Animation> GFXManager::loadAnimationFromWsa(const std::string& filename) const {
    auto file = pFileManager->openFile(filename);
    auto wsafile = std::make_unique<Wsafile>(file.get());
    auto animation = wsafile->getAnimation(0,wsafile->getNumFrames() - 1,true,false);
    return animation;
}

sdl2::surface_ptr GFXManager::generateWindtrapAnimationFrames(SDL_Surface* windtrapPic) const {
    int windtrapColorQuantizizer = 255/((NUM_WINDTRAP_ANIMATIONS/2)-2);
    int frameWidth = windtrapPic->w / objPicTiles[ObjPic_Windtrap].x;
    int frameHeight = windtrapPic->h;
    int sizeX = NUM_WINDTRAP_ANIMATIONS_PER_ROW*frameWidth;
    int sizeY = ((2+NUM_WINDTRAP_ANIMATIONS+NUM_WINDTRAP_ANIMATIONS_PER_ROW-1)/NUM_WINDTRAP_ANIMATIONS_PER_ROW)*frameHeight;
    sdl2::surface_ptr returnPic{ SDL_CreateRGBSurface(0, sizeX, sizeY, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    SDL_SetSurfaceBlendMode(returnPic.get(), SDL_BLENDMODE_NONE);
    SDL_FillRect(returnPic.get(), nullptr, SDL_MapRGBA(returnPic->format, 0, 0, 0, 0));

    // copy building phase
    SDL_Rect src = { 0, 0, 2*frameWidth, frameHeight};
    SDL_Rect dest = src;
    SDL_BlitSurface(windtrapPic, &src, returnPic.get(), &dest);

    src.w = frameWidth;
    dest.x += dest.w;
    dest.w = frameWidth;

    for(int i = 0; i < NUM_WINDTRAP_ANIMATIONS; i++) {
        src.x = ((i/3) % 2 == 0) ? 2*frameWidth : 3*frameWidth;

        if(windtrapPic->format->palette) {
            SDL_Color windtrapColor;
            if(i < NUM_WINDTRAP_ANIMATIONS/2) {
                int val = i*windtrapColorQuantizizer;
                windtrapColor.r = static_cast<Uint8>(std::min(80, val));
                windtrapColor.g = static_cast<Uint8>(std::min(80, val));
                windtrapColor.b = static_cast<Uint8>(std::min(255, val));
                windtrapColor.a = 255;
            } else {
                int val = (i-NUM_WINDTRAP_ANIMATIONS/2)*windtrapColorQuantizizer;
                windtrapColor.r = static_cast<Uint8>(std::max(0, 80-val));
                windtrapColor.g = static_cast<Uint8>(std::max(0, 80-val));
                windtrapColor.b = static_cast<Uint8>(std::max(0, 255-val));
                windtrapColor.a = 255;
            }
            SDL_SetPaletteColors(windtrapPic->format->palette, &windtrapColor, PALCOLOR_WINDTRAP_COLORCYCLE, 1);
        }

        SDL_BlitSurface(windtrapPic, &src, returnPic.get(), &dest);

        dest.x += dest.w;
        dest.y = dest.y + dest.h * (dest.x / sizeX);
        dest.x = dest.x % sizeX;
    }

    if((returnPic->w > 2048) || (returnPic->h > 2048)) {
        SDL_Log("Warning: Size of sprite sheet for windtrap is %dx%d; may exceed hardware limits on older GPUs!", returnPic->w, returnPic->h);
    }

    return returnPic;
}


sdl2::surface_ptr GFXManager::generateMapChoiceArrowFrames(SDL_Surface* arrowPic, int house) const {
    sdl2::surface_ptr returnPic{ SDL_CreateRGBSurface(0, arrowPic->w * 4, arrowPic->h, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };

    SDL_Rect dest = {0, 0, arrowPic->w, arrowPic->h};

    for(int i = 0; i < 4; i++) {
        for(int k = 0; k < 4; k++) {
            const SDL_Color color = getHouseColorSDL(house, (i + k) % 4);
            SDL_SetPaletteColors(arrowPic->format->palette, &color, 251+k, 1);
        }

        SDL_BlitSurface(arrowPic, nullptr, returnPic.get(), &dest);
        dest.x += dest.w;
    }

    return returnPic;
}

void GFXManager::loadDuneCitySkinOverrides() {
    static const std::array<unsigned int, 3> zoneObjPics = {
        ObjPic_ZoneResidential, ObjPic_ZoneCommercial, ObjPic_ZoneIndustrial
    };
    static const std::array<unsigned int, 6> buildingObjPics = {
        ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
        ObjPic_Airport, ObjPic_Hospital, ObjPic_Church
    };

    // Start the Dune2 skin as a byte-for-byte visual clone of Stefan's native
    // SimCity atlas. Exact mounted cells are overlaid below; missing cells and
    // houses therefore retain native art instead of becoming blank.
    for(std::size_t zoneIndex = 0; zoneIndex < zoneObjPics.size(); ++zoneIndex) {
        const unsigned int id = zoneObjPics[zoneIndex];
        for(int house = 0; house < static_cast<int>(NUM_HOUSES); ++house) {
            for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
                if(objPic[id][house][zoom]) {
                    duneCitySimCityZonePic[zoneIndex][house][zoom] = copySurface(objPic[id][house][zoom].get());
                    duneCityDune2ZonePic[zoneIndex][house][zoom] = copySurface(objPic[id][house][zoom].get());
                }
            }
        }
    }
    for(std::size_t buildingIndex = 0; buildingIndex < buildingObjPics.size(); ++buildingIndex) {
        const unsigned int id = buildingObjPics[buildingIndex];
        for(int house = 0; house < static_cast<int>(NUM_HOUSES); ++house) {
            for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
                if(objPic[id][house][zoom]) {
                    duneCitySimCityBuildingPic[buildingIndex][house][zoom] = copySurface(objPic[id][house][zoom].get());
                    duneCityDune2BuildingPic[buildingIndex][house][zoom] = copySurface(objPic[id][house][zoom].get());
                }
            }
        }
    }

    if(!ModManager::instance().isInitialized()) return;
    const std::filesystem::path zonesRoot =
        std::filesystem::path(ModManager::instance().getModPath(ModManager::instance().getActiveModName()))
        / "graphics_skins" / "Dune2" / "zones";
    if(!std::filesystem::is_directory(zonesRoot)) {
        SDL_Log("GFXManager: DuneCity Dune2 skin has no mounted zones; SimCity fallback remains active");
        return;
    }

    int loadedCells = 0;
    for(const auto& entry : std::filesystem::recursive_directory_iterator(zonesRoot)) {
        if(!entry.is_regular_file() || entry.path().filename() != "zone.ini") continue;
        try {
            INIFile manifest(entry.path().string());
            const int itemID = manifest.getIntValue("Zone", "ItemID", -1);
            const int house = manifest.getIntValue("Zone", "HouseID", -1);
            const int zoneIndex = itemID == 20 ? 0 : (itemID == 21 ? 1 : (itemID == 22 ? 2 : -1));
            if(zoneIndex < 0 || house < 0 || house >= static_cast<int>(NUM_HOUSES)) {
                SDL_Log("GFXManager: Ignoring DuneCity zone manifest with ItemID=%d HouseID=%d: %s",
                        itemID, house, entry.path().string().c_str());
                continue;
            }

            SDL_Surface* target = duneCityDune2ZonePic[zoneIndex][house][0].get();
            if(!target) continue;
            const unsigned int objPicID = zoneObjPics[zoneIndex];
            const int columns = std::max(1, objPicTiles[objPicID].x);
            const int rows = std::max(1, objPicTiles[objPicID].y);
            const int cellW = target->w / columns;
            const int cellH = target->h / rows;

            for(auto section = manifest.begin(); section != manifest.end(); ++section) {
                const std::string sectionName = section->getSectionName();
                int density = -1;
                int value = -1;
                if(std::sscanf(sectionName.c_str(), "Cell.%d.%d.Idle", &density, &value) != 2
                   || density < 0 || density >= columns || value < 0 || value >= rows) continue;
                const std::string atlasName = manifest.getStringValue(sectionName, "Atlas.0", "");
                if(atlasName.empty()) continue;
                const std::filesystem::path atlasPath = entry.path().parent_path() / atlasName;
                if(!std::filesystem::is_regular_file(atlasPath)) continue;
                auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(atlasPath.string().c_str(), "rb") };
                auto cell = rwops ? LoadPNG_RW(rwops.get()) : sdl2::surface_ptr{};
                if(!cell) continue;

                // Replace the complete atlas cell, including transparent
                // pixels. Alpha blending would leave pieces of the native
                // SimCity cell visible beneath transparent Compact corners.
                // LoadPNG_RW may also preserve a PNG colour key; disable it
                // explicitly or keyed pixels are skipped even with blending
                // disabled, exposing the cloned SimCity fallback underneath.
                SDL_SetColorKey(cell.get(), SDL_FALSE, 0);
                SDL_SetSurfaceBlendMode(cell.get(), SDL_BLENDMODE_NONE);
                SDL_Rect source{0, 0,
                    std::min(cell->w, manifest.getIntValue(sectionName, "FrameWidth", cellW)),
                    std::min(cell->h, manifest.getIntValue(sectionName, "FrameHeight", cellH))};
                SDL_Rect destination{density * cellW, value * cellH, cellW, cellH};
                const Uint32 transparent = SDL_MapRGBA(target->format, 0, 0, 0, 0);
                SDL_FillRect(target, &destination, transparent);
                SDL_BlitScaled(cell.get(), &source, target, &destination);
                ++loadedCells;
            }
            duneCityDune2ZonePic[zoneIndex][house][1] =
                scaleDuneCitySkinEdgeAware(target, 2, columns, rows);
            duneCityDune2ZonePic[zoneIndex][house][2] =
                scaleDuneCitySkinEdgeAware(target, 3, columns, rows);
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: DuneCity skin manifest failed (%s): %s",
                    entry.path().string().c_str(), e.what());
        }
    }
    SDL_Log("GFXManager: Loaded %d exact DuneCity Dune2 density/value cells", loadedCells);

    const std::filesystem::path buildingsRoot =
        std::filesystem::path(ModManager::instance().getModPath(ModManager::instance().getActiveModName()))
        / "graphics_skins" / "Dune2" / "buildings";
    if(!std::filesystem::is_directory(buildingsRoot)) {
        SDL_Log("GFXManager: DuneCity Dune2 skin has no mounted special buildings; SimCity fallback remains active");
        return;
    }

    const auto buildingIndexForName = [](const std::string& name) -> int {
        if(name == "NuclearPlant") return 0;
        if(name == "PoliceStation") return 1;
        if(name == "Stadium") return 2;
        if(name == "Airport") return 3;
        if(name == "Hospital") return 4;
        if(name == "Church") return 5;
        return -1;
    };
    int loadedBuildingFrames = 0;
    for(const auto& entry : std::filesystem::recursive_directory_iterator(buildingsRoot)) {
        if(!entry.is_regular_file() || entry.path().filename() != "building.ini") continue;
        try {
            INIFile manifest(entry.path().string());
            const std::string objPicName = manifest.getStringValue("Building", "ObjPic", "");
            const int buildingIndex = buildingIndexForName(objPicName);
            const int house = manifest.getIntValue("Building", "HouseID", -1);
            if(buildingIndex < 0 || house < 0 || house >= static_cast<int>(NUM_HOUSES)) {
                SDL_Log("GFXManager: Ignoring DuneCity building manifest ObjPic=%s HouseID=%d: %s",
                        objPicName.c_str(), house, entry.path().string().c_str());
                continue;
            }

            const unsigned int objPicID = buildingObjPics[buildingIndex];
            SDL_Surface* target = duneCityDune2BuildingPic[buildingIndex][house][0].get();
            if(!target) continue;
            const int columns = std::max(1, objPicTiles[objPicID].x);
            const int rows = std::max(1, objPicTiles[objPicID].y);
            const int cellW = target->w / columns;
            const int cellH = target->h / rows;
            const std::string fallbackFrame = manifest.getStringValue("Frame.0", "File", "");

            for(int frame = 0; frame < columns * rows; ++frame) {
                const std::string sectionName = "Frame." + std::to_string(frame);
                std::string frameName = manifest.getStringValue(sectionName, "File", "");
                if(frameName.empty()) frameName = fallbackFrame;
                if(frameName.empty()) continue;
                const std::filesystem::path framePath = entry.path().parent_path() / frameName;
                if(!std::filesystem::is_regular_file(framePath)) continue;
                auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(framePath.string().c_str(), "rb") };
                auto cell = rwops ? LoadPNG_RW(rwops.get()) : sdl2::surface_ptr{};
                if(!cell) continue;
                SDL_SetColorKey(cell.get(), SDL_FALSE, 0);
                SDL_SetSurfaceBlendMode(cell.get(), SDL_BLENDMODE_NONE);
                SDL_Rect source{0, 0, cell->w, cell->h};
                SDL_Rect destination{(frame % columns) * cellW, (frame / columns) * cellH, cellW, cellH};
                const Uint32 transparent = SDL_MapRGBA(target->format, 0, 0, 0, 0);
                SDL_FillRect(target, &destination, transparent);
                SDL_BlitScaled(cell.get(), &source, target, &destination);
                ++loadedBuildingFrames;
            }
            duneCityDune2BuildingPic[buildingIndex][house][1] =
                scaleDuneCitySkinEdgeAware(target, 2, columns, rows);
            duneCityDune2BuildingPic[buildingIndex][house][2] =
                scaleDuneCitySkinEdgeAware(target, 3, columns, rows);
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: DuneCity building skin manifest failed (%s): %s",
                    entry.path().string().c_str(), e.what());
        }
    }
    SDL_Log("GFXManager: Loaded %d DuneCity Dune2 special-building frame slots", loadedBuildingFrames);
}

void GFXManager::resetDuneCityGraphicsSkins() {
    for(int house = 0; house < static_cast<int>(NUM_HOUSES); ++house) {
        setDuneCityHouseGraphicsSkin(house, false);
    }
}

void GFXManager::setDuneCityHouseGraphicsSkin(int house, bool useDune2) {
    if(house < 0 || house >= static_cast<int>(NUM_HOUSES)) return;
    duneCityHouseUsesDune2[house] = useDune2;
    static const std::array<unsigned int, 3> zoneObjPics = {
        ObjPic_ZoneResidential, ObjPic_ZoneCommercial, ObjPic_ZoneIndustrial
    };
    static const std::array<unsigned int, 6> buildingObjPics = {
        ObjPic_NuclearPlant, ObjPic_PoliceStation, ObjPic_Stadium,
        ObjPic_Airport, ObjPic_Hospital, ObjPic_Church
    };
    for(std::size_t zoneIndex = 0; zoneIndex < zoneObjPics.size(); ++zoneIndex) {
        const unsigned int id = zoneObjPics[zoneIndex];
        auto& source = useDune2 ? duneCityDune2ZonePic[zoneIndex][house]
                                : duneCitySimCityZonePic[zoneIndex][house];
        for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
            if(source[zoom]) {
                objPic[id][house][zoom] = copySurface(source[zoom].get());
                objPicTex[id][house][zoom].reset();
            }
        }
    }
    for(std::size_t buildingIndex = 0; buildingIndex < buildingObjPics.size(); ++buildingIndex) {
        const unsigned int id = buildingObjPics[buildingIndex];
        auto& source = useDune2 ? duneCityDune2BuildingPic[buildingIndex][house]
                                : duneCitySimCityBuildingPic[buildingIndex][house];
        for(int zoom = 0; zoom < NUM_ZOOMLEVEL; ++zoom) {
            if(source[zoom]) {
                objPic[id][house][zoom] = copySurface(source[zoom].get());
                objPicTex[id][house][zoom].reset();
            }
        }
    }
}

bool GFXManager::isDuneCityHouseUsingDune2(int house) const {
    return house >= 0 && house < static_cast<int>(NUM_HOUSES)
        && duneCityHouseUsesDune2[house];
}

void GFXManager::loadCompactObjPicOverrides() {
    if(!ModManager::instance().isInitialized()) {
        return;
    }

    const std::string activeMod = ModManager::instance().getActiveModName();
    if(activeMod == "vanilla") {
        return;
    }

    const std::filesystem::path overrideDir =
        std::filesystem::path(ModManager::instance().getModPath(activeMod))
        / "graphics_compact" / "objpics";

    if(!std::filesystem::is_directory(overrideDir)) {
        return;
    }

    for(unsigned int id = 0; id < NUM_OBJPICS; ++id) {
        const std::filesystem::path preferredPath = overrideDir / ("ObjPic_" + ObjPicNames[id] + ".png");
        const std::filesystem::path legacyPath = overrideDir / (ObjPicNames[id] + ".png");
        std::filesystem::path overridePath;

        if(std::filesystem::is_regular_file(preferredPath)) {
            overridePath = preferredPath;
        } else if(std::filesystem::is_regular_file(legacyPath)) {
            overridePath = legacyPath;
        } else {
            continue;
        }

        auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(overridePath.string().c_str(), "rb") };
        if(!rwops) {
            SDL_Log("GFXManager: Failed to open compact override %s: %s",
                    overridePath.string().c_str(), SDL_GetError());
            continue;
        }

        auto raw = LoadPNG_RW(rwops.get());
        if(!raw) {
            SDL_Log("GFXManager: Failed to load compact override %s",
                    overridePath.string().c_str());
            continue;
        }

        const int expectedW = objPicTiles[id].x;
        const int expectedH = objPicTiles[id].y;
        if(expectedW <= 0 || expectedH <= 0 || raw->w % expectedW != 0 || raw->h % expectedH != 0) {
            SDL_Log("GFXManager: Skipping compact override %s; size %dx%d does not match ObjPic_%s layout %dx%d",
                    overridePath.string().c_str(), raw->w, raw->h,
                    ObjPicNames[id].c_str(), expectedW, expectedH);
            continue;
        }

        sdl2::surface_ptr surface;
        if(raw->format->BytesPerPixel >= 3) {
            surface = sdl2::surface_ptr{ SDL_ConvertSurfaceFormat(raw.get(), SDL_PIXELFORMAT_RGBA32, 0) };
        } else {
            surface = sdl2::surface_ptr{ SDL_ConvertSurface(raw.get(), raw->format, 0) };
        }

        if(!surface) {
            SDL_Log("GFXManager: Failed to convert compact override %s: %s",
                    overridePath.string().c_str(), SDL_GetError());
            continue;
        }

        for(int h = 0; h < (int)NUM_HOUSES; ++h) {
            objPic[id][h][0].reset();
            objPic[id][h][1].reset();
            objPic[id][h][2].reset();
            objPicTex[id][h][0].reset();
            objPicTex[id][h][1].reset();
            objPicTex[id][h][2].reset();
        }

        objPic[id][HOUSE_HARKONNEN][0] = std::move(surface);

        for(int h = 1; h < (int)NUM_HOUSES; ++h) {
            objPic[id][h][0] = sdl2::surface_ptr{
                SDL_ConvertSurface(objPic[id][HOUSE_HARKONNEN][0].get(),
                                   objPic[id][HOUSE_HARKONNEN][0]->format, 0)
            };
        }

        SDL_Log("GFXManager: Loaded compact override ObjPic_%s from %s",
                ObjPicNames[id].c_str(), overridePath.string().c_str());
    }
}

bool GFXManager::loadHDObjPicOverride(unsigned int id) {
    if(id >= NUM_OBJPICS) {
        return false;
    }

    auto& hd = hdObjPicOverrides[id];
    if(hd.attempted) {
        return hd.loaded;
    }
    hd.attempted = true;

    if(!ModManager::instance().isInitialized()) {
        return false;
    }

    const std::string activeMod = ModManager::instance().getActiveModName();
    if(activeMod == "vanilla") {
        return false;
    }

    const std::filesystem::path overrideDir =
        std::filesystem::path(ModManager::instance().getModPath(activeMod))
        / "graphics_hd" / "objpics";

    const std::filesystem::path preferredPng = overrideDir / ("ObjPic_" + ObjPicNames[id] + ".png");
    const std::filesystem::path legacyPng = overrideDir / (ObjPicNames[id] + ".png");
    std::filesystem::path pngPath;

    if(std::filesystem::is_regular_file(preferredPng)) {
        pngPath = preferredPng;
    } else if(std::filesystem::is_regular_file(legacyPng)) {
        pngPath = legacyPng;
    } else {
        return false;
    }

    std::filesystem::path metadataPath = pngPath;
    metadataPath.replace_extension(".ini");
    if(std::filesystem::is_regular_file(metadataPath)) {
        try {
            INIFile metadata(metadataPath.string());
            hd.columns = std::max(1, metadata.getIntValue("Sprite", "Columns", 1));
            hd.rows = std::max(1, metadata.getIntValue("Sprite", "Rows", 1));
            hd.anchorX = metadata.getIntValue("Sprite", "AnchorX", -1);
            hd.anchorY = metadata.getIntValue("Sprite", "AnchorY", -1);
            hd.baseWidth = metadata.getIntValue("Render", "BaseWidth", 0);
            hd.baseHeight = metadata.getIntValue("Render", "BaseHeight", 0);
            hd.scale = metadata.getDoubleValue("Render", "Scale", 1.0);
            if(hd.scale <= 0.0) {
                hd.scale = 1.0;
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: Failed to read HD override metadata %s: %s",
                    metadataPath.string().c_str(), e.what());
        }
    }

    auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(pngPath.string().c_str(), "rb") };
    if(!rwops) {
        SDL_Log("GFXManager: Failed to open HD override %s: %s",
                pngPath.string().c_str(), SDL_GetError());
        return false;
    }

    auto raw = LoadPNG_RW(rwops.get());
    if(!raw) {
        SDL_Log("GFXManager: Failed to load HD override %s",
                pngPath.string().c_str());
        return false;
    }

    auto surface = sdl2::surface_ptr{ SDL_ConvertSurfaceFormat(raw.get(), SDL_PIXELFORMAT_RGBA32, 0) };
    if(!surface) {
        SDL_Log("GFXManager: Failed to convert HD override %s: %s",
                pngPath.string().c_str(), SDL_GetError());
        return false;
    }

    if(surface->w % hd.columns != 0 || surface->h % hd.rows != 0) {
        SDL_Log("GFXManager: Skipping HD override %s; size %dx%d does not divide into %dx%d frames",
                pngPath.string().c_str(), surface->w, surface->h, hd.columns, hd.rows);
        return false;
    }

    hd.texture[HOUSE_HARKONNEN] = convertSurfaceToTexture(surface.get());
    if(!hd.texture[HOUSE_HARKONNEN]) {
        SDL_Log("GFXManager: Failed to create HD override texture %s: %s",
                pngPath.string().c_str(), SDL_GetError());
        return false;
    }

    for(int h = 1; h < (int)NUM_HOUSES; ++h) {
        hd.texture[h] = convertSurfaceToTexture(surface.get());
    }

    hd.loaded = true;
    SDL_Log("GFXManager: Loaded HD override ObjPic_%s from %s",
            ObjPicNames[id].c_str(), pngPath.string().c_str());
    return true;
}

bool GFXManager::drawHDObjPic(unsigned int id, int house, unsigned int z,
                              int col, int numCols, int row, int numRows,
                              int x, int y) {
    if(id >= NUM_OBJPICS || z >= NUM_ZOOMLEVEL || house < 0 || house >= (int)NUM_HOUSES) {
        return false;
    }

    Uint8 blend = SDL_ALPHA_OPAQUE;
    if(ModManager::instance().isInitialized()
       && ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) == "Dune2R") {
        blend = getDune2RVisualBlend();
        if(blend == 0) {
            return false;
        }
    }

    if(!loadHDObjPicOverride(id)) {
        return false;
    }

    auto& hd = hdObjPicOverrides[id];
    SDL_Texture* texture = hd.texture[house] ? hd.texture[house].get() : hd.texture[HOUSE_HARKONNEN].get();
    if(texture == nullptr) {
        return false;
    }

    const int columns = std::max(1, hd.columns);
    const int rows = std::max(1, hd.rows);
    const int textureW = getWidth(texture);
    const int textureH = getHeight(texture);
    const int frameW = textureW / columns;
    const int frameH = textureH / rows;
    if(frameW <= 0 || frameH <= 0) {
        return false;
    }

    const int srcCol = std::clamp(col, 0, columns - 1);
    const int srcRow = std::clamp(row, 0, rows - 1);
    SDL_Rect source = { srcCol * frameW, srcRow * frameH, frameW, frameH };

    int destW = hd.baseWidth > 0 ? hd.baseWidth * (int)(z + 1) : 0;
    int destH = hd.baseHeight > 0 ? hd.baseHeight * (int)(z + 1) : 0;
    if(destW <= 0 || destH <= 0) {
        SDL_Texture* classicTexture = objPicTex[id][house][z] ? objPicTex[id][house][z].get() : objPicTex[id][HOUSE_HARKONNEN][z].get();
        if(classicTexture != nullptr) {
            destW = getWidth(classicTexture) / std::max(1, numCols);
            destH = getHeight(classicTexture) / std::max(1, numRows);
        } else {
            destW = frameW;
            destH = frameH;
        }
        destW = dune2rWorldExtent(destW);
        destH = dune2rWorldExtent(destH);
    }

    destW = std::max(1, static_cast<int>(lround(destW * hd.scale)));
    destH = std::max(1, static_cast<int>(lround(destH * hd.scale)));

    const int anchorX = hd.anchorX >= 0 ? hd.anchorX : frameW / 2;
    const int anchorY = hd.anchorY >= 0 ? hd.anchorY : frameH / 2;
    const double scaleX = static_cast<double>(destW) / static_cast<double>(frameW);
    const double scaleY = static_cast<double>(destH) / static_cast<double>(frameH);

    SDL_Rect dest = {
        x - static_cast<int>(lround(anchorX * scaleX)),
        y - static_cast<int>(lround(anchorY * scaleY)),
        destW,
        destH
    };

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, blend);
    SDL_RenderCopy(renderer, texture, &source, &dest);
    SDL_SetTextureAlphaMod(texture, SDL_ALPHA_OPAQUE);
    return true;
}

void GFXManager::loadDune2RVisualPreference() {
    if(dune2rVisualPreferenceLoaded) {
        return;
    }
    dune2rVisualPreferenceLoaded = true;
    dune2rVisualTargetEnabled = true;
    try {
        INIFile config(getConfigFilepath());
        dune2rVisualTargetEnabled = config.getBoolValue(
            "Dune2R", "Enhanced Visuals", true);
    } catch(const std::exception& e) {
        SDL_Log("GFXManager: Could not load Dune2R visual preference: %s", e.what());
    }
    dune2rVisualBlend = dune2rVisualTargetEnabled ? SDL_ALPHA_OPAQUE : 0;
}

Uint8 GFXManager::getDune2RVisualBlend() {
    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return 0;
    }
    loadDune2RVisualPreference();
    if(!dune2rVisualTransitionActive) {
        return dune2rVisualBlend;
    }

    const Uint32 elapsed = SDL_GetTicks() - dune2rVisualTransitionStartTicks;
    if(elapsed >= kDune2RVisualFadeMs) {
        dune2rVisualBlend = dune2rVisualTargetEnabled ? SDL_ALPHA_OPAQUE : 0;
        dune2rVisualTransitionActive = false;
        return dune2rVisualBlend;
    }

    const int target = dune2rVisualTargetEnabled ? SDL_ALPHA_OPAQUE : 0;
    const int start = dune2rVisualTransitionStartBlend;
    dune2rVisualBlend = static_cast<Uint8>(std::clamp(
        start + (target - start) * static_cast<int>(elapsed)
                    / static_cast<int>(kDune2RVisualFadeMs),
        0, static_cast<int>(SDL_ALPHA_OPAQUE)));
    return dune2rVisualBlend;
}

bool GFXManager::isDune2RVisualsEnabled() {
    loadDune2RVisualPreference();
    return dune2rVisualTargetEnabled;
}

void GFXManager::setDune2RVisualsEnabled(bool enabled) {
    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return;
    }
    const Uint8 currentBlend = getDune2RVisualBlend();
    if(enabled == dune2rVisualTargetEnabled && !dune2rVisualTransitionActive) {
        return;
    }
    dune2rVisualTargetEnabled = enabled;
    dune2rVisualTransitionStartBlend = currentBlend;
    dune2rVisualTransitionStartTicks = SDL_GetTicks();
    dune2rVisualTransitionActive = true;

    try {
        const std::string path = getConfigFilepath();
        INIFile config(path);
        config.setBoolValue("Dune2R", "Enhanced Visuals", enabled);
        if(!config.saveChangesTo(path)) {
            SDL_Log("GFXManager: Could not save Dune2R visual preference to %s", path.c_str());
        }
    } catch(const std::exception& e) {
        SDL_Log("GFXManager: Could not save Dune2R visual preference: %s", e.what());
    }
}

void GFXManager::toggleDune2RVisuals() {
    setDune2RVisualsEnabled(!isDune2RVisualsEnabled());
}

void GFXManager::loadEnhancedUnitManifests() {
    invalidateEnhancedUnitMountsIfChanged();
    if(enhancedUnitManifestsLoaded) {
        return;
    }
    enhancedUnitManifestsLoaded = true;
    enhancedUnitDefinitions.clear();

    if(!ModManager::instance().isInitialized()) {
        return;
    }

    const std::string activeMod = ModManager::instance().getActiveModName();
    if(!ModManager::instance().usesDune2RRemasterPresentation()) {
        return;
    }

    const std::filesystem::path unitsRoot =
        std::filesystem::path(ModManager::instance().getModPath(activeMod))
        / "graphics_hd" / "units";
    if(!std::filesystem::is_directory(unitsRoot)) {
        return;
    }

    for(const auto& entry : std::filesystem::directory_iterator(unitsRoot)) {
        if(!entry.is_directory()) {
            continue;
        }

        const std::filesystem::path manifestPath = entry.path() / "unit.ini";
        if(!std::filesystem::is_regular_file(manifestPath)) {
            continue;
        }

        try {
            INIFile manifest(manifestPath.string());
            EnhancedUnitDefinition definition;
            definition.itemID = manifest.getIntValue("Unit", "ItemID", -1);
            definition.houseID = manifest.getIntValue("Unit", "HouseID", -1);
            definition.sourceUnit = manifest.getStringValue(
                "Unit", "SourceUnit", entry.path().filename().string());
            definition.baseWidth = manifest.getIntValue("Render", "BaseWidth", 0);
            definition.baseHeight = manifest.getIntValue("Render", "BaseHeight", 0);
            definition.scale = manifest.getDoubleValue("Render", "Scale", 1.0);

            if(definition.itemID < 0 || definition.houseID < -1
               || definition.houseID >= static_cast<int>(NUM_HOUSES)
               || definition.baseWidth <= 0 || definition.baseHeight <= 0
               || definition.scale <= 0.0) {
                SDL_Log("GFXManager: Skipping invalid enhanced unit manifest %s",
                        manifestPath.string().c_str());
                continue;
            }

            for(int stateIndex = 0; stateIndex < static_cast<int>(kEnhancedStateNames.size()); ++stateIndex) {
                const auto state = static_cast<EnhancedUnitState>(stateIndex);
                for(int direction = 0; direction < kEnhancedDirectionCount; ++direction) {
                    const std::string section = std::string(kEnhancedStateNames[stateIndex])
                                                + "." + kEnhancedDirectionNames[direction];
                    const std::string atlasName = manifest.getStringValue(section, "Atlas", "");
                    if(atlasName.empty()) {
                        continue;
                    }

                    const std::filesystem::path atlasPath =
                        std::filesystem::weakly_canonical(entry.path() / atlasName);
                    if(!isPathInside(atlasPath, unitsRoot)
                       || !std::filesystem::is_regular_file(atlasPath)) {
                        SDL_Log("GFXManager: Enhanced atlas '%s' is missing or outside its mod",
                                atlasPath.string().c_str());
                        continue;
                    }

                    EnhancedUnitAnimation unitAnimation;
                    unitAnimation.atlasPath = atlasPath.string();
                    unitAnimation.columns = std::max(1, manifest.getIntValue(section, "Columns", 1));
                    unitAnimation.rows = std::max(1, manifest.getIntValue(section, "Rows", 1));
                    unitAnimation.frameCount = std::max(1, manifest.getIntValue(section, "Frames", 1));
                    unitAnimation.frameMs = std::max(1, manifest.getIntValue(section, "FrameMs", 100));
                    unitAnimation.durationsMs = parseEnhancedFrameDurations(
                        manifest.getStringValue(section, "DurationsMs", ""), unitAnimation.frameCount);
                    unitAnimation.anchorX = manifest.getIntValue(section, "AnchorX", -1);
                    unitAnimation.anchorY = manifest.getIntValue(section, "AnchorY", -1);
                    unitAnimation.loop = manifest.getBoolValue(
                        section, "Loop",
                        state != EnhancedUnitState::Combat
                        && state != EnhancedUnitState::CombatReturn
                        && state != EnhancedUnitState::DamageExploded
                        && state != EnhancedUnitState::DamageDissipation);

                    if(unitAnimation.frameCount > unitAnimation.columns * unitAnimation.rows) {
                        SDL_Log("GFXManager: Enhanced atlas '%s' declares %d frames in a %dx%d grid",
                                atlasPath.string().c_str(), unitAnimation.frameCount,
                                unitAnimation.columns, unitAnimation.rows);
                        continue;
                    }

                    definition.animations.emplace(enhancedAnimationKey(state, direction), std::move(unitAnimation));
                }
            }

            if(!definition.animations.empty()) {
                SDL_Log("GFXManager: Registered enhanced unit ItemID=%d HouseID=%d from %s",
                        definition.itemID, definition.houseID, manifestPath.string().c_str());
                enhancedUnitDefinitions.push_back(std::move(definition));
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: Failed to read enhanced unit manifest %s: %s",
                    manifestPath.string().c_str(), e.what());
        }
    }
}

void GFXManager::loadEnhancedWorldManifests() {
    invalidateEnhancedUnitMountsIfChanged();
    if(enhancedWorldManifestsLoaded) {
        return;
    }
    enhancedWorldManifestsLoaded = true;
    enhancedBuildingDefinitions.clear();
    enhancedTerrainDefinitions.clear();

    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return;
    }

    const std::filesystem::path unitsRoot =
        std::filesystem::path(ModManager::instance().getModPath(ModManager::instance().getActiveModName()))
        / "graphics_hd" / "units";
    if(!std::filesystem::is_directory(unitsRoot)) {
        return;
    }

    for(const auto& entry : std::filesystem::directory_iterator(unitsRoot)) {
        if(!entry.is_directory()) {
            continue;
        }

        const std::filesystem::path tileManifestPath = entry.path() / "tile.ini";
        if(std::filesystem::is_regular_file(tileManifestPath)) {
            try {
                INIFile manifest(tileManifestPath.string());
                EnhancedTerrainDefinition definition;
                definition.terrainType = manifest.getIntValue("Tile", "TerrainType", -1);
                definition.sourceUnit = manifest.getStringValue(
                    "Tile", "SourceUnit", entry.path().filename().string());
                const int variants = manifest.getIntValue("Tile", "Variants", 0);
                if(definition.terrainType < 0 || variants != 16) {
                    SDL_Log("GFXManager: Skipping invalid terrain manifest %s",
                            tileManifestPath.string().c_str());
                } else {
                    bool complete = true;
                    for(int variant = 0; variant < 16; ++variant) {
                        const std::string section = "Variant." + std::to_string(variant);
                        const std::string imageName = manifest.getStringValue(section, "Image", "");
                        const std::filesystem::path imagePath =
                            std::filesystem::weakly_canonical(entry.path() / imageName);
                        if(imageName.empty() || !isPathInside(imagePath, unitsRoot)
                           || !std::filesystem::is_regular_file(imagePath)) {
                            complete = false;
                            break;
                        }
                        definition.variants[variant].imagePath = imagePath.string();
                    }
                    if(complete) {
                        enhancedTerrainDefinitions.push_back(std::move(definition));
                        SDL_Log("GFXManager: Registered enhanced terrain type %d from %s",
                                enhancedTerrainDefinitions.back().terrainType,
                                tileManifestPath.string().c_str());
                    } else {
                        SDL_Log("GFXManager: Terrain manifest %s is missing a topology image",
                                tileManifestPath.string().c_str());
                    }
                }
            } catch(const std::exception& e) {
                SDL_Log("GFXManager: Failed to read terrain manifest %s: %s",
                        tileManifestPath.string().c_str(), e.what());
            }
        }

        const std::filesystem::path buildingManifestPath = entry.path() / "building.ini";
        if(!std::filesystem::is_regular_file(buildingManifestPath)) {
            continue;
        }
        try {
            INIFile manifest(buildingManifestPath.string());
            EnhancedBuildingDefinition definition;
            definition.itemID = manifest.getIntValue("Building", "ItemID", -1);
            definition.houseID = manifest.getIntValue("Building", "HouseID", -1);
            definition.sourceUnit = manifest.getStringValue(
                "Building", "SourceUnit", entry.path().filename().string());
            definition.footprintWidth = manifest.getIntValue("Building", "FootprintWidth", 0);
            definition.footprintHeight = manifest.getIntValue("Building", "FootprintHeight", 0);
            if(definition.itemID < 0 || definition.houseID < -1
               || definition.houseID >= static_cast<int>(NUM_HOUSES)
               || definition.footprintWidth <= 0 || definition.footprintHeight <= 0) {
                SDL_Log("GFXManager: Skipping invalid building manifest %s",
                        buildingManifestPath.string().c_str());
                continue;
            }

            for(int stateIndex = 0;
                stateIndex < static_cast<int>(kEnhancedBuildingStateNames.size());
                ++stateIndex) {
                const std::string section = std::string("State.")
                                            + kEnhancedBuildingStateNames[stateIndex];
                const int frameCount = manifest.getIntValue(section, "Frames", 0);
                const int atlasCount = manifest.getIntValue(section, "AtlasCount", 0);
                if(frameCount <= 0 || atlasCount <= 0 || atlasCount > 64) {
                    continue;
                }

                EnhancedBuildingAnimation animation;
                animation.frameCount = frameCount;
                animation.frameMs = std::max(1, manifest.getIntValue(section, "FrameMs", 100));
                animation.frameWidth = manifest.getIntValue(section, "FrameWidth", 0);
                animation.frameHeight = manifest.getIntValue(section, "FrameHeight", 0);
                animation.anchorX = manifest.getIntValue(section, "AnchorX", animation.frameWidth / 2);
                animation.anchorY = manifest.getIntValue(section, "AnchorY", animation.frameHeight);
                animation.loop = manifest.getBoolValue(section, "Loop", true);
                const std::string stillName = manifest.getStringValue(section, "Still", "");
                const auto stillPath = std::filesystem::weakly_canonical(entry.path() / stillName);
                animation.stillWidth = manifest.getIntValue(section, "StillWidth", animation.frameWidth);
                animation.stillHeight = manifest.getIntValue(section, "StillHeight", animation.frameHeight);
                animation.stillAnchorX = manifest.getIntValue(section, "StillAnchorX", animation.anchorX);
                animation.stillAnchorY = manifest.getIntValue(section, "StillAnchorY", animation.anchorY);
                if(!stillName.empty() && isPathInside(stillPath, unitsRoot)
                   && std::filesystem::is_regular_file(stillPath)
                   && animation.stillWidth > 0 && animation.stillWidth <= 2048
                   && animation.stillHeight > 0 && animation.stillHeight <= 2048) {
                    animation.stillPath = stillPath.string();
                }
                bool valid = animation.frameWidth > 0 && animation.frameHeight > 0;
                int coveredFrames = 0;
                for(int chunkIndex = 0; valid && chunkIndex < atlasCount; ++chunkIndex) {
                    EnhancedAtlasChunk chunk;
                    const std::string suffix = std::to_string(chunkIndex);
                    const std::string atlasName = manifest.getStringValue(
                        section, "Atlas." + suffix, "");
                    const std::filesystem::path atlasPath =
                        std::filesystem::weakly_canonical(entry.path() / atlasName);
                    chunk.firstFrame = manifest.getIntValue(
                        section, "FirstFrame." + suffix, -1);
                    chunk.frameCount = manifest.getIntValue(
                        section, "ChunkFrames." + suffix, 0);
                    chunk.columns = manifest.getIntValue(section, "Columns." + suffix, 0);
                    chunk.rows = manifest.getIntValue(section, "Rows." + suffix, 0);
                    if(atlasName.empty() || !isPathInside(atlasPath, unitsRoot)
                       || !std::filesystem::is_regular_file(atlasPath)
                       || chunk.firstFrame != coveredFrames || chunk.frameCount <= 0
                       || chunk.columns <= 0 || chunk.rows <= 0
                       || chunk.frameCount > chunk.columns * chunk.rows) {
                        valid = false;
                        break;
                    }
                    chunk.atlasPath = atlasPath.string();
                    coveredFrames += chunk.frameCount;
                    animation.chunks.push_back(std::move(chunk));
                }
                if((valid && coveredFrames == frameCount) || !animation.stillPath.empty()) {
                    if(!valid || coveredFrames != frameCount) {
                        animation.chunks.clear();
                    }
                    definition.animations.emplace(stateIndex, std::move(animation));
                } else {
                    SDL_Log("GFXManager: Skipping invalid %s section in %s",
                            section.c_str(), buildingManifestPath.string().c_str());
                }
            }

            if(!definition.animations.empty()) {
                SDL_Log("GFXManager: Registered enhanced building ItemID=%d HouseID=%d from %s",
                        definition.itemID, definition.houseID,
                        buildingManifestPath.string().c_str());
                enhancedBuildingDefinitions.push_back(std::move(definition));
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: Failed to read building manifest %s: %s",
                    buildingManifestPath.string().c_str(), e.what());
        }
    }
}

void GFXManager::loadDuneCityZoneManifests() {
    if(duneCityZoneManifestsLoaded) {
        return;
    }
    duneCityZoneManifestsLoaded = true;
    duneCityZoneDefinitions.clear();

    if(!ModManager::instance().isInitialized()
       || !ModManager::instance().isCityModeActive()) {
        return;
    }
    const std::filesystem::path zonesRoot =
        std::filesystem::path(ModManager::instance().getModPath(ModManager::instance().getActiveModName()))
        / "graphics_skins" / "Dune2" / "zones";
    if(!std::filesystem::is_directory(zonesRoot)) {
        return;
    }
    for(const auto& entry : std::filesystem::directory_iterator(zonesRoot)) {
        const auto manifestPath = entry.path() / "zone.ini";
        if(!entry.is_directory() || !std::filesystem::is_regular_file(manifestPath)) {
            continue;
        }
        try {
            INIFile manifest(manifestPath.string());
            DuneCityZoneDefinition definition;
            definition.itemID = manifest.getIntValue("Zone", "ItemID", -1);
            definition.houseID = manifest.getIntValue("Zone", "HouseID", -1);
            definition.sourceUnit = manifest.getStringValue(
                "Zone", "SourceUnit", entry.path().filename().string());
            definition.footprintWidth = manifest.getIntValue("Zone", "FootprintWidth", 2);
            definition.footprintHeight = manifest.getIntValue("Zone", "FootprintHeight", 2);
            const int densityColumns = std::clamp(
                manifest.getIntValue("Zone", "DensityColumns", 4), 1, 4);
            const int valueRows = std::clamp(
                manifest.getIntValue("Zone", "ValueTierRows", 4), 1, 4);
            if(definition.itemID < 0 || definition.houseID < -1
               || definition.houseID >= static_cast<int>(NUM_HOUSES)) {
                continue;
            }
            for(int value = 0; value < valueRows; ++value) {
                for(int density = 0; density < densityColumns; ++density) {
                    for(int activityIndex = 0;
                        activityIndex < static_cast<int>(DuneCityZoneActivity::Count);
                        ++activityIndex) {
                        const std::string section = "Cell." + std::to_string(density) + "."
                            + std::to_string(value) + "." + kDuneCityZoneActivityNames[activityIndex];
                        const int frameCount = manifest.getIntValue(section, "Frames", 0);
                        const int atlasCount = manifest.getIntValue(section, "AtlasCount", 0);
                        if(frameCount <= 0 || atlasCount <= 0 || atlasCount > 64) {
                            continue;
                        }
                        EnhancedBuildingAnimation animation;
                        animation.frameCount = frameCount;
                        animation.frameMs = std::max(1, manifest.getIntValue(section, "FrameMs", 100));
                        animation.frameWidth = manifest.getIntValue(section, "FrameWidth", 0);
                        animation.frameHeight = manifest.getIntValue(section, "FrameHeight", 0);
                        animation.anchorX = manifest.getIntValue(section, "AnchorX", animation.frameWidth / 2);
                        animation.anchorY = manifest.getIntValue(section, "AnchorY", animation.frameHeight);
                        animation.loop = manifest.getBoolValue(section, "Loop", true);
                        bool valid = animation.frameWidth > 0 && animation.frameHeight > 0;
                        int coveredFrames = 0;
                        for(int chunkIndex = 0; valid && chunkIndex < atlasCount; ++chunkIndex) {
                            EnhancedAtlasChunk chunk;
                            const std::string suffix = std::to_string(chunkIndex);
                            const std::string atlasName = manifest.getStringValue(
                                section, "Atlas." + suffix, "");
                            const auto atlasPath = std::filesystem::weakly_canonical(entry.path() / atlasName);
                            chunk.firstFrame = manifest.getIntValue(section, "FirstFrame." + suffix, -1);
                            chunk.frameCount = manifest.getIntValue(section, "ChunkFrames." + suffix, 0);
                            chunk.columns = manifest.getIntValue(section, "Columns." + suffix, 0);
                            chunk.rows = manifest.getIntValue(section, "Rows." + suffix, 0);
                            if(atlasName.empty() || !isPathInside(atlasPath, zonesRoot)
                               || !std::filesystem::is_regular_file(atlasPath)
                               || chunk.firstFrame != coveredFrames || chunk.frameCount <= 0
                               || chunk.columns <= 0 || chunk.rows <= 0
                               || chunk.frameCount > chunk.columns * chunk.rows) {
                                valid = false;
                                break;
                            }
                            chunk.atlasPath = atlasPath.string();
                            coveredFrames += chunk.frameCount;
                            animation.chunks.push_back(std::move(chunk));
                        }
                        if(valid && coveredFrames == frameCount) {
                            definition.animations.emplace(
                                duneCityZoneAnimationKey(density, value,
                                    static_cast<DuneCityZoneActivity>(activityIndex)),
                                std::move(animation));
                        }
                    }
                }
            }
            if(!definition.animations.empty()) {
                SDL_Log("GFXManager: Registered DuneCity Dune2 zone ItemID=%d HouseID=%d from %s",
                        definition.itemID, definition.houseID, manifestPath.string().c_str());
                duneCityZoneDefinitions.push_back(std::move(definition));
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: Failed to read DuneCity zone manifest %s: %s",
                    manifestPath.string().c_str(), e.what());
        }
    }
}

void GFXManager::loadDuneCityBuildingManifests() {
    if(duneCityBuildingManifestsLoaded) {
        return;
    }
    duneCityBuildingManifestsLoaded = true;
    duneCityBuildingDefinitions.clear();

    if(!ModManager::instance().isInitialized()
       || !ModManager::instance().isCityModeActive()) {
        return;
    }
    const std::filesystem::path buildingsRoot =
        std::filesystem::path(ModManager::instance().getModPath(ModManager::instance().getActiveModName()))
        / "graphics_skins" / "Dune2" / "buildings";
    if(!std::filesystem::is_directory(buildingsRoot)) {
        return;
    }
    const auto itemForObjPic = [](const std::string& name) {
        if(name == "NuclearPlant") return static_cast<int>(Structure_NuclearPlant);
        if(name == "PoliceStation") return static_cast<int>(Structure_PoliceStation);
        if(name == "Stadium") return static_cast<int>(Structure_Stadium);
        if(name == "Airport") return static_cast<int>(Structure_Airport);
        return -1;
    };

    for(const auto& entry : std::filesystem::directory_iterator(buildingsRoot)) {
        const auto manifestPath = entry.path() / "building.ini";
        if(!entry.is_directory() || !std::filesystem::is_regular_file(manifestPath)) {
            continue;
        }
        try {
            INIFile manifest(manifestPath.string());
            DuneCityBuildingDefinition definition;
            definition.itemID = itemForObjPic(
                manifest.getStringValue("Building", "ObjPic", ""));
            definition.houseID = manifest.getIntValue("Building", "HouseID", -1);
            definition.sourceUnit = manifest.getStringValue(
                "Building", "SourceUnit", entry.path().filename().string());
            const int frameCount = std::clamp(
                manifest.getIntValue("Building", "Frames", 0), 0, 64);
            if(definition.itemID < 0 || definition.houseID < 0
               || definition.houseID >= static_cast<int>(NUM_HOUSES)
               || frameCount <= 0) {
                continue;
            }
            for(int frame = 0; frame < frameCount; ++frame) {
                const std::string section = "Frame." + std::to_string(frame);
                const std::string imageName = manifest.getStringValue(section, "File", "");
                const auto imagePath = std::filesystem::weakly_canonical(entry.path() / imageName);
                if(imageName.empty() || !isPathInside(imagePath, buildingsRoot)
                   || !std::filesystem::is_regular_file(imagePath)) {
                    definition.frames.clear();
                    break;
                }
                auto input = sdl2::RWops_ptr{SDL_RWFromFile(imagePath.string().c_str(), "rb")};
                auto surface = input ? LoadPNG_RW(input.get()) : nullptr;
                if(!surface || surface->w <= 0 || surface->h <= 0
                   || surface->w > 2048 || surface->h > 2048) {
                    definition.frames.clear();
                    break;
                }
                definition.frames.push_back({imagePath.string(), surface->w, surface->h});
            }
            if(static_cast<int>(definition.frames.size()) == frameCount) {
                SDL_Log("GFXManager: Registered high-detail DuneCity building ItemID=%d HouseID=%d from %s",
                        definition.itemID, definition.houseID, manifestPath.string().c_str());
                duneCityBuildingDefinitions.push_back(std::move(definition));
            }
        } catch(const std::exception& e) {
            SDL_Log("GFXManager: Failed to read DuneCity building manifest %s: %s",
                    manifestPath.string().c_str(), e.what());
        }
    }
}

void GFXManager::loadEnhancedRenderModes() {
    if(enhancedRenderModesLoaded) {
        return;
    }
    enhancedRenderModesLoaded = true;
    enhancedUnitRenderModes.clear();

    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return;
    }

    loadEnhancedUnitManifests();
    try {
        const auto activeMod = ModManager::instance().getActiveModName();
        INIFile config(dune2rRenderPreferencesPath(activeMod,
            ModManager::instance().getModPath(activeMod), getConfigFilepath()));
        const auto section = dune2rRenderPreferencesSection(activeMod);
        for(const auto& definition : enhancedUnitDefinitions) {
            for(int stateIndex = 0; stateIndex < static_cast<int>(kEnhancedStateNames.size()); ++stateIndex) {
                const auto state = static_cast<EnhancedUnitState>(stateIndex);
                for(int direction = 0; direction < kEnhancedDirectionCount; ++direction) {
                    const std::string value = config.getStringValue(
                        section,
                        enhancedRenderModeConfigKey(definition.itemID, definition.houseID,
                                                    state, direction),
                        "full");
                    const auto mode = parseEnhancedRenderMode(value);
                    if(mode != EnhancedRenderMode::FullAnimation) {
                        enhancedUnitRenderModes.emplace(
                            enhancedRenderModeKey(definition.itemID, definition.houseID,
                                                  state, direction),
                            mode);
                    }
                }
            }
        }
    } catch(const std::exception& e) {
        SDL_Log("GFXManager: Could not load Dune2R EditoR preferences: %s", e.what());
    }
}

bool GFXManager::hasEnhancedUnitAnimation(int itemID, int house,
                                          EnhancedUnitState state,
                                          int direction) {
    loadEnhancedUnitManifests();
    if(direction < 0 || direction >= kEnhancedDirectionCount) {
        return false;
    }

    const int key = enhancedAnimationKey(state, direction);
    for(const int requestedHouse : {house, -1}) {
        for(const auto& definition : enhancedUnitDefinitions) {
            if(definition.itemID == itemID && definition.houseID == requestedHouse
               && definition.animations.find(key) != definition.animations.end()) {
                return true;
            }
        }
    }
    return false;
}

std::vector<GFXManager::EnhancedUnitEditorInfo> GFXManager::getEnhancedUnitEditorInfo() {
    loadEnhancedUnitManifests();
    std::vector<EnhancedUnitEditorInfo> result;
    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return result;
    }

    result.reserve(enhancedUnitDefinitions.size());
    for(const auto& definition : enhancedUnitDefinitions) {
        EnhancedUnitEditorInfo info;
        info.sourceUnit = definition.sourceUnit;
        info.itemID = definition.itemID;
        info.houseID = definition.houseID;
        for(int stateIndex = 0; stateIndex < static_cast<int>(kEnhancedStateNames.size()); ++stateIndex) {
            for(int direction = 0; direction < kEnhancedDirectionCount; ++direction) {
                info.available[stateIndex][direction] = definition.animations.find(
                    enhancedAnimationKey(static_cast<EnhancedUnitState>(stateIndex), direction))
                    != definition.animations.end();
            }
        }
        result.push_back(std::move(info));
    }
    return result;
}

GFXManager::EnhancedRenderMode GFXManager::getEnhancedUnitRenderMode(
    int itemID, int house, EnhancedUnitState state, int direction) {
    if(direction < 0 || direction >= kEnhancedDirectionCount
       || !ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return EnhancedRenderMode::Layered;
    }

    loadEnhancedRenderModes();
    for(const int requestedHouse : {house, -1}) {
        const auto it = enhancedUnitRenderModes.find(
            enhancedRenderModeKey(itemID, requestedHouse, state, direction));
        if(it != enhancedUnitRenderModes.end()) {
            return it->second;
        }
    }
    return EnhancedRenderMode::FullAnimation;
}

bool GFXManager::setEnhancedUnitRenderMode(int itemID, int house,
                                           EnhancedUnitState state,
                                           int direction,
                                           EnhancedRenderMode mode) {
    if(direction < 0 || direction >= kEnhancedDirectionCount
       || !ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return false;
    }

    loadEnhancedRenderModes();
    const int key = enhancedRenderModeKey(itemID, house, state, direction);
    if(mode == EnhancedRenderMode::FullAnimation) {
        enhancedUnitRenderModes.erase(key);
    } else {
        enhancedUnitRenderModes[key] = mode;
    }

    try {
        const auto activeMod = ModManager::instance().getActiveModName();
        const std::string path = dune2rRenderPreferencesPath(activeMod,
            ModManager::instance().getModPath(activeMod), getConfigFilepath());
        const auto section = dune2rRenderPreferencesSection(activeMod);
        // MSVC 19.38 rejects a conditional prvalue of the non-copyable INIFile.
        auto configFile = std::filesystem::exists(path)
            ? std::make_unique<INIFile>(path)
            : std::make_unique<INIFile>(false, std::string("Mod sprite rendering"));
        INIFile& config = *configFile;
        const std::string configKey = enhancedRenderModeConfigKey(
            itemID, house, state, direction);
        if(mode == EnhancedRenderMode::FullAnimation) {
            config.removeKey(section, configKey);
        } else {
            config.setStringValue(section, configKey,
                                  enhancedRenderModeName(mode), false);
        }
        const auto temporary = std::filesystem::path(path + ".workshop-render.tmp");
        if(!config.saveChangesTo(temporary.string())) {
            SDL_Log("GFXManager: Could not save Dune2R EditoR preferences to %s",
                    path.c_str());
            enhancedRenderModesLoaded = false;
            return false;
        }
        Workshop::replaceFile(temporary,path);
        return true;
    } catch(const std::exception& e) {
        SDL_Log("GFXManager: Could not save Dune2R EditoR preference: %s", e.what());
        enhancedRenderModesLoaded = false;
        return false;
    }
}

void GFXManager::invalidateEnhancedUnitMountsIfChanged(bool force) {
    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return;
    }

    const Uint32 now = SDL_GetTicks();
    if(!force && enhancedUnitMountLastCheck != 0
       && now - enhancedUnitMountLastCheck < 1000u) {
        return;
    }
    enhancedUnitMountLastCheck = now;

    const std::filesystem::path marker =
        std::filesystem::path(ModManager::instance().getModPath(ModManager::instance().getActiveModName()))
        / "graphics_hd" / "units" / ".mount-revision";
    std::string revision;
    if(std::ifstream input(marker); input) {
        std::getline(input, revision);
    }

    if(!force && revision == enhancedUnitMountRevision) {
        return;
    }
    enhancedUnitMountRevision = revision;
    enhancedInfantryOverlay.clear();
    if(enhancedBuildingAtlasCache) {
        enhancedBuildingAtlasCache->clear();
    }
    enhancedUnitDefinitions.clear();
    enhancedUnitManifestsLoaded = false;
    enhancedBuildingDefinitions.clear();
    enhancedTerrainDefinitions.clear();
    enhancedWorldManifestsLoaded = false;
    enhancedUnitRenderModes.clear();
    enhancedRenderModesLoaded = false;
    SDL_Log("GFXManager: Dune2R mounted-unit cache invalidated (%s)",
            revision.empty() ? "manual refresh" : revision.c_str());
}

void GFXManager::reloadEnhancedUnitMounts() {
    invalidateEnhancedUnitMountsIfChanged(true);
    loadEnhancedUnitManifests();
    loadEnhancedWorldManifests();
}

Uint32 GFXManager::getEnhancedUnitAnimationDuration(int itemID, int house,
                                                     EnhancedUnitState state,
                                                     int direction) {
    loadEnhancedUnitManifests();
    if(direction < 0 || direction >= kEnhancedDirectionCount) {
        return 0;
    }

    const int key = enhancedAnimationKey(state, direction);
    for(const int requestedHouse : {house, -1}) {
        for(const auto& definition : enhancedUnitDefinitions) {
            if(definition.itemID != itemID || definition.houseID != requestedHouse) {
                continue;
            }
            const auto animationIt = definition.animations.find(key);
            if(animationIt != definition.animations.end()) {
                return enhancedAnimationDuration(animationIt->second.frameCount,
                    animationIt->second.frameMs, animationIt->second.durationsMs);
            }
        }
    }
    return 0;
}

bool GFXManager::drawEnhancedTerrain(int terrainType, int variant,
                                     const SDL_Rect& destination) {
    const Uint8 blend = getDune2RVisualBlend();
    if(blend == 0 || variant < 0 || variant >= 16) {
        return false;
    }
    loadEnhancedWorldManifests();
    for(auto& definition : enhancedTerrainDefinitions) {
        if(definition.terrainType != terrainType) {
            continue;
        }
        auto& visual = definition.variants[variant];
        if(visual.texture == nullptr) {
            if(visual.loadAttempted) {
                return false;
            }
            visual.loadAttempted = true;
            auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(visual.imagePath.c_str(), "rb") };
            auto surface = rwops ? LoadPNG_RW(rwops.get()) : nullptr;
            if(!surface) {
                SDL_Log("GFXManager: Failed to load enhanced terrain image %s",
                        visual.imagePath.c_str());
                return false;
            }
            visual.texture = convertSurfaceToTexture(surface.get());
            if(visual.texture == nullptr) {
                return false;
            }
        }
        SDL_Texture* texture = visual.texture.get();
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(texture, blend);
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_SetTextureAlphaMod(texture, SDL_ALPHA_OPAQUE);
        return true;
    }
    return false;
}

Uint32 GFXManager::getEnhancedBuildingAnimationDuration(
    int itemID, int house, EnhancedBuildingState state) {
    loadEnhancedWorldManifests();
    for(const int requestedHouse : {house, -1}) {
        for(const auto& definition : enhancedBuildingDefinitions) {
            if(definition.itemID != itemID || definition.houseID != requestedHouse) {
                continue;
            }
            const auto found = definition.animations.find(static_cast<int>(state));
            if(found != definition.animations.end()) {
                return static_cast<Uint32>(found->second.frameCount)
                       * static_cast<Uint32>(found->second.frameMs);
            }
        }
    }
    return 0;
}

bool GFXManager::drawEnhancedBuilding(int itemID, int house, unsigned int z,
                                      EnhancedBuildingState state,
                                      Uint32 elapsedMs, int anchorX, int anchorY) {
    const Uint8 blend = getDune2RVisualBlend();
    if(blend == 0 || z >= NUM_ZOOMLEVEL) {
        return false;
    }
    loadEnhancedWorldManifests();

    EnhancedBuildingDefinition* selectedDefinition = nullptr;
    EnhancedBuildingAnimation* selectedAnimation = nullptr;
    const std::array<EnhancedBuildingState, 4> fallbacks = {
        state,
        state == EnhancedBuildingState::Working ? EnhancedBuildingState::Idle : state,
        state == EnhancedBuildingState::Repair ? EnhancedBuildingState::Idle : state,
        EnhancedBuildingState::Idle,
    };
    for(const int requestedHouse : {house, -1}) {
        for(auto& definition : enhancedBuildingDefinitions) {
            if(definition.itemID != itemID || definition.houseID != requestedHouse) {
                continue;
            }
            for(const auto candidate : fallbacks) {
                const auto found = definition.animations.find(static_cast<int>(candidate));
                if(found != definition.animations.end()) {
                    selectedDefinition = &definition;
                    selectedAnimation = &found->second;
                    break;
                }
            }
            if(selectedAnimation != nullptr) {
                break;
            }
        }
        if(selectedAnimation != nullptr) {
            break;
        }
    }
    if(selectedDefinition == nullptr || selectedAnimation == nullptr) {
        return false;
    }

    Uint32 frame = elapsedMs / static_cast<Uint32>(selectedAnimation->frameMs);
    if(selectedAnimation->loop) {
        frame %= static_cast<Uint32>(selectedAnimation->frameCount);
    } else {
        frame = std::min(frame, static_cast<Uint32>(selectedAnimation->frameCount - 1));
    }

    EnhancedAtlasChunk* selectedChunk = nullptr;
    for(auto& chunk : selectedAnimation->chunks) {
        if(static_cast<int>(frame) >= chunk.firstFrame
           && static_cast<int>(frame) < chunk.firstFrame + chunk.frameCount) {
            selectedChunk = &chunk;
            break;
        }
    }
    if(!enhancedBuildingAtlasCache) {
        enhancedBuildingAtlasCache = std::make_unique<EnhancedAtlasCache>(renderer);
    }
    SDL_Texture* texture = selectedChunk ? enhancedBuildingAtlasCache->request(
        selectedChunk->atlasPath,
        selectedChunk->columns * selectedAnimation->frameWidth,
        selectedChunk->rows * selectedAnimation->frameHeight) : nullptr;
    int frameWidth = selectedAnimation->frameWidth;
    int frameHeight = selectedAnimation->frameHeight;
    int imageAnchorX = selectedAnimation->anchorX;
    int imageAnchorY = selectedAnimation->anchorY;
    SDL_Rect source{0, 0, frameWidth, frameHeight};
    const bool animated = texture != nullptr;
    if(animated) {
        const int localFrame = static_cast<int>(frame) - selectedChunk->firstFrame;
        source.x = (localFrame % selectedChunk->columns) * frameWidth;
        source.y = (localFrame / selectedChunk->columns) * frameHeight;
    } else {
        if(!selectedAnimation->stillAttempted && !selectedAnimation->stillPath.empty()) {
            selectedAnimation->stillAttempted = true;
            auto input = sdl2::RWops_ptr{SDL_RWFromFile(selectedAnimation->stillPath.c_str(), "rb")};
            auto surface = input ? LoadPNG_RW(input.get()) : nullptr;
            if(surface && surface->w == selectedAnimation->stillWidth
               && surface->h == selectedAnimation->stillHeight) {
                selectedAnimation->stillTexture = convertSurfaceToTexture(surface.get());
            } else {
                SDL_Log("Dune2R building still failed: %s", selectedAnimation->stillPath.c_str());
            }
        }
        texture = selectedAnimation->stillTexture.get();
        if(!texture) {
            return false;
        }
        frameWidth = selectedAnimation->stillWidth;
        frameHeight = selectedAnimation->stillHeight;
        imageAnchorX = selectedAnimation->stillAnchorX;
        imageAnchorY = selectedAnimation->stillAnchorY;
        source = {0, 0, frameWidth, frameHeight};
    }
    const SDL_Rect destination = calcEnhancedBuildingDrawingRect(
        selectedDefinition->footprintWidth, z, {frameWidth, frameHeight},
        {imageAnchorX, imageAnchorY}, {anchorX, anchorY}, dune2rWorldDrawingScale);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, blend);
    SDL_RenderCopy(renderer, texture, &source, &destination);
    SDL_SetTextureAlphaMod(texture, SDL_ALPHA_OPAQUE);
    if(animated) {
        const auto nextFrame = selectedChunk->firstFrame + selectedChunk->frameCount;
        for(auto& next : selectedAnimation->chunks) {
            if(next.firstFrame == nextFrame
               || (selectedAnimation->loop && nextFrame == selectedAnimation->frameCount && next.firstFrame == 0)) {
                enhancedBuildingAtlasCache->request(next.atlasPath,
                    next.columns * selectedAnimation->frameWidth,
                    next.rows * selectedAnimation->frameHeight);
                break;
            }
        }
    }
    return true;
}

bool GFXManager::drawDuneCityZone(int itemID, int house, unsigned int z,
                                  int density, int valueTier,
                                  DuneCityZoneActivity activity,
                                  Uint32 elapsedMs, int anchorX, int anchorY, const SDL_Rect* previewBounds) {
    if(z >= NUM_ZOOMLEVEL) {
        return false;
    }
    loadDuneCityZoneManifests();
    if(!isDuneCityHouseUsingDune2(house)) {
        return false;
    }

    DuneCityZoneDefinition* selectedDefinition = nullptr;
    EnhancedBuildingAnimation* selectedAnimation = nullptr;
    const std::array<DuneCityZoneActivity, 2> fallbacks = {
        activity, DuneCityZoneActivity::Idle
    };
    for(const int requestedHouse : {house, -1}) {
        for(auto& definition : duneCityZoneDefinitions) {
            if(definition.itemID != itemID || definition.houseID != requestedHouse) {
                continue;
            }
            for(const auto candidate : fallbacks) {
                const auto found = definition.animations.find(
                    duneCityZoneAnimationKey(density, valueTier, candidate));
                if(found != definition.animations.end()) {
                    selectedDefinition = &definition;
                    selectedAnimation = &found->second;
                    break;
                }
            }
            if(selectedAnimation) break;
        }
        if(selectedAnimation) break;
    }
    if(!selectedDefinition || !selectedAnimation) {
        return false;
    }

    Uint32 frame = elapsedMs / static_cast<Uint32>(selectedAnimation->frameMs);
    if(selectedAnimation->loop) {
        frame %= static_cast<Uint32>(selectedAnimation->frameCount);
    } else {
        frame = std::min(frame, static_cast<Uint32>(selectedAnimation->frameCount - 1));
    }
    EnhancedAtlasChunk* selectedChunk = nullptr;
    for(auto& chunk : selectedAnimation->chunks) {
        if(static_cast<int>(frame) >= chunk.firstFrame
           && static_cast<int>(frame) < chunk.firstFrame + chunk.frameCount) {
            selectedChunk = &chunk;
            break;
        }
    }
    if(!selectedChunk) {
        return false;
    }
    if(!enhancedBuildingAtlasCache) {
        enhancedBuildingAtlasCache = std::make_unique<EnhancedAtlasCache>(renderer);
    }
    SDL_Texture* texture = enhancedBuildingAtlasCache->request(
        selectedChunk->atlasPath,
        selectedChunk->columns * selectedAnimation->frameWidth,
        selectedChunk->rows * selectedAnimation->frameHeight);
    if(!texture) {
        return false;
    }
    const int localFrame = static_cast<int>(frame) - selectedChunk->firstFrame;
    const SDL_Rect source{
        (localFrame % selectedChunk->columns) * selectedAnimation->frameWidth,
        (localFrame / selectedChunk->columns) * selectedAnimation->frameHeight,
        selectedAnimation->frameWidth,
        selectedAnimation->frameHeight
    };
    SDL_Rect destination = calcEnhancedBuildingDrawingRect(
        selectedDefinition->footprintWidth, z,
        {selectedAnimation->frameWidth, selectedAnimation->frameHeight},
        {selectedAnimation->anchorX, selectedAnimation->anchorY},
        {anchorX, anchorY});
    if(previewBounds) {
        const double scale = std::min(double(previewBounds->w)/source.w, double(previewBounds->h)/source.h);
        destination.w = int(source.w*scale);
        destination.h = int(source.h*scale);
        destination.x = previewBounds->x + (previewBounds->w-destination.w)/2;
        destination.y = previewBounds->y + (previewBounds->h-destination.h)/2;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, texture, &source, &destination);

    const auto nextFrame = selectedChunk->firstFrame + selectedChunk->frameCount;
    for(auto& next : selectedAnimation->chunks) {
        if(next.firstFrame == nextFrame
           || (selectedAnimation->loop && nextFrame == selectedAnimation->frameCount
               && next.firstFrame == 0)) {
            enhancedBuildingAtlasCache->request(
                next.atlasPath,
                next.columns * selectedAnimation->frameWidth,
                next.rows * selectedAnimation->frameHeight);
            break;
        }
    }
    return true;
}

bool GFXManager::drawDuneCityBuilding(int itemID, int house, int frame,
                                      const SDL_Rect& destination) {
    if(destination.w <= 0 || destination.h <= 0
       || !isDuneCityHouseUsingDune2(house)) {
        return false;
    }
    loadDuneCityBuildingManifests();
    DuneCityBuildingDefinition* selected = nullptr;
    for(auto& definition : duneCityBuildingDefinitions) {
        if(definition.itemID == itemID && definition.houseID == house) {
            selected = &definition;
            break;
        }
    }
    if(!selected || selected->frames.empty()) {
        return false;
    }
    const int frameCount = static_cast<int>(selected->frames.size());
    const int selectedFrame = std::clamp(frame, 0, frameCount - 1);
    const auto& visual = selected->frames[selectedFrame];
    if(!enhancedBuildingAtlasCache) {
        enhancedBuildingAtlasCache = std::make_unique<EnhancedAtlasCache>(renderer);
    }
    SDL_Texture* texture = enhancedBuildingAtlasCache->request(
        visual.imagePath, visual.width, visual.height);
    if(!texture) {
        return false;
    }
    const SDL_Rect source{0, 0, visual.width, visual.height};
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, texture, &source, &destination);
    return true;
}

bool GFXManager::drawEnhancedUnit(int itemID, int house, unsigned int z,
                                  EnhancedUnitState state, int direction,
                                  Uint32 elapsedMs, int x, int y) {
    const Uint8 blend = getDune2RVisualBlend();
    if(blend == 0 || z >= NUM_ZOOMLEVEL
       || direction < 0 || direction >= kEnhancedDirectionCount) {
        return false;
    }
    loadEnhancedUnitManifests();

    EnhancedUnitDefinition* selectedDefinition = nullptr;
    EnhancedUnitAnimation* selectedAnimation = nullptr;
    const int key = enhancedAnimationKey(state, direction);
    for(const int requestedHouse : {house, -1}) {
        for(auto& definition : enhancedUnitDefinitions) {
            if(definition.itemID != itemID || definition.houseID != requestedHouse) {
                continue;
            }
            const auto animationIt = definition.animations.find(key);
            if(animationIt != definition.animations.end()) {
                selectedDefinition = &definition;
                selectedAnimation = &animationIt->second;
                break;
            }
        }
        if(selectedAnimation != nullptr) {
            break;
        }
    }

    if(selectedAnimation == nullptr || selectedDefinition == nullptr) {
        return false;
    }

    if(selectedAnimation->texture == nullptr) {
        if(selectedAnimation->loadAttempted) {
            return false;
        }
        selectedAnimation->loadAttempted = true;

        auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(selectedAnimation->atlasPath.c_str(), "rb") };
        auto surface = rwops ? LoadPNG_RW(rwops.get()) : nullptr;
        if(!surface || surface->w % selectedAnimation->columns != 0
           || surface->h % selectedAnimation->rows != 0) {
            SDL_Log("GFXManager: Failed to load enhanced unit atlas %s",
                    selectedAnimation->atlasPath.c_str());
            return false;
        }
        selectedAnimation->texture = convertSurfaceToTexture(surface.get());
        if(selectedAnimation->texture == nullptr) {
            return false;
        }
    }

    SDL_Texture* texture = selectedAnimation->texture.get();
    const int frameW = getWidth(texture) / selectedAnimation->columns;
    const int frameH = getHeight(texture) / selectedAnimation->rows;
    if(frameW <= 0 || frameH <= 0) {
        return false;
    }

    const Uint32 frame = enhancedAnimationFrame(elapsedMs,
        selectedAnimation->frameCount, selectedAnimation->frameMs,
        selectedAnimation->loop, selectedAnimation->durationsMs);

    SDL_Rect source = {
        static_cast<int>(frame % selectedAnimation->columns) * frameW,
        static_cast<int>(frame / selectedAnimation->columns) * frameH,
        frameW,
        frameH
    };

    const int anchorX = selectedAnimation->anchorX >= 0 ? selectedAnimation->anchorX : frameW / 2;
    const int anchorY = selectedAnimation->anchorY >= 0 ? selectedAnimation->anchorY : frameH / 2;

    const SDL_Rect dest = calcEnhancedUnitDrawingRect(
        {selectedDefinition->baseWidth, selectedDefinition->baseHeight}, z,
        selectedDefinition->scale, {frameW, frameH}, {anchorX, anchorY}, {x, y});
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, blend);
    SDL_RenderCopy(renderer, texture, &source, &dest);
    SDL_SetTextureAlphaMod(texture, SDL_ALPHA_OPAQUE);
    return true;
}

Uint32 GFXManager::recordEnhancedInfantryFall(int itemID, int house,
                                             int direction, Uint32 nowMs) {
    if(itemID != Unit_Soldier || house != HOUSE_HARKONNEN
       || !ModManager::instance().isInitialized()
       || !ModManager::instance().usesDune2RRemasterPresentation()) {
        return 0;
    }
    // No loading, RNG or gameplay work here: destroy() still removes the unit now.
    return enhancedInfantryOverlay.record(nowMs, direction, house);
}

bool GFXManager::drawEnhancedInfantryFall(Uint32 token, int house,
                                           Uint32 nowMs, int x, int y) {
    if(!ModManager::instance().isInitialized()
       || !ModManager::instance().usesDune2RRemasterPresentation()
       || getDune2RVisualBlend() == 0) {
        return false;
    }
    // Loading may invalidate the mount cache and clear this presentation history.
    loadEnhancedUnitManifests();
    const auto* event = enhancedInfantryOverlay.find(token, nowMs, house);
    if(!event) {
        return false;
    }
    const Uint32 elapsed = nowMs - event->startMs;
    const int direction = event->direction;
    const Uint32 fallingDuration = getEnhancedUnitAnimationDuration(
        Unit_Soldier, house, EnhancedUnitState::DamageExploded, direction);
    const auto state = fallingDuration > 0 && elapsed < fallingDuration
        ? EnhancedUnitState::DamageExploded : EnhancedUnitState::DamageAftermath;
    return drawEnhancedUnit(Unit_Soldier, house, currentZoomlevel, state, direction,
        state == EnhancedUnitState::DamageExploded ? elapsed : elapsed - fallingDuration,
        x, y);
}

sdl2::surface_ptr GFXManager::generateDoubledObjPic(unsigned int id, int h) const {
    sdl2::surface_ptr pSurface;
    if(objPic[id][h][0] && objPic[id][h][0]->format->BytesPerPixel != 1) {
        pSurface = scaleSurfaceNearest(objPic[id][h][0].get(), 2);
        if((pSurface->w > 2048) || (pSurface->h > 2048)) {
            SDL_Log("Warning: Size of sprite sheet for '%s' in zoom level 1 is %dx%d; may exceed hardware limits on older GPUs!", ObjPicNames.at(id).c_str(), pSurface->w, pSurface->h);
        }
        return pSurface;
    }

    std::string filename = "Mask_2x_" + ObjPicNames.at(id) + ".png";
    if(settings.video.scaler == "ScaleHD") {
        if(pFileManager->exists(filename)) {
            pSurface = sdl2::surface_ptr{ Scaler::doubleTiledSurfaceNN(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };

            sdl2::surface_ptr pOverlay = LoadPNG_RW(pFileManager->openFile(filename).get());
            SDL_SetColorKey(pOverlay.get(), SDL_TRUE, PALCOLOR_UI_COLORCYCLE);

            // SDL_BlitSurface will silently map PALCOLOR_BLACK to PALCOLOR_TRANSPARENT as both are RGB(0,0,0,255), so make them temporarily different
            pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 1;
            pSurface->format->palette->colors[PALCOLOR_BLACK].g = 1;
            SDL_BlitSurface(pOverlay.get(), NULL, pSurface.get(), NULL);
            pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 0;
            pSurface->format->palette->colors[PALCOLOR_BLACK].g = 0;
        } else {
            SDL_Log("Warning: No HD sprite sheet for '%s' in zoom level 1!", ObjPicNames.at(id).c_str());
            pSurface = sdl2::surface_ptr{ Scaler::defaultDoubleTiledSurface(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };
        }
    } else {
        pSurface = sdl2::surface_ptr{ Scaler::defaultDoubleTiledSurface(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };
    }

    if((pSurface->w > 2048) || (pSurface->h > 2048)) {
        SDL_Log("Warning: Size of sprite sheet for '%s' in zoom level 1 is %dx%d; may exceed hardware limits on older GPUs!", ObjPicNames.at(id).c_str(), pSurface->w, pSurface->h);
    }

    return pSurface;
}

sdl2::surface_ptr GFXManager::generateTripledObjPic(unsigned int id, int h) const {
    sdl2::surface_ptr pSurface;
    if(objPic[id][h][0] && objPic[id][h][0]->format->BytesPerPixel != 1) {
        pSurface = scaleSurfaceNearest(objPic[id][h][0].get(), 3);
        if((pSurface->w > 2048) || (pSurface->h > 2048)) {
            SDL_Log("Warning: Size of sprite sheet for '%s' in zoom level 2 is %dx%d; may exceed hardware limits on older GPUs!", ObjPicNames.at(id).c_str(), pSurface->w, pSurface->h);
        }
        return pSurface;
    }

    const std::string filename = "Mask_3x_" + ObjPicNames.at(id) + ".png";
    if(settings.video.scaler == "ScaleHD") {
        if(pFileManager->exists(filename)) {
            pSurface = sdl2::surface_ptr{ Scaler::tripleTiledSurfaceNN(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };

            sdl2::surface_ptr pOverlay = LoadPNG_RW(pFileManager->openFile(filename).get());
            SDL_SetColorKey(pOverlay.get(), SDL_TRUE, PALCOLOR_UI_COLORCYCLE);

            // SDL_BlitSurface will silently map PALCOLOR_BLACK to PALCOLOR_TRANSPARENT as both are RGB(0,0,0,255), so make them temporarily different
            pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 1;
            pSurface->format->palette->colors[PALCOLOR_BLACK].g = 1;
            SDL_BlitSurface(pOverlay.get(), NULL, pSurface.get(), NULL);
            pOverlay->format->palette->colors[PALCOLOR_BLACK].g = 0;
            pSurface->format->palette->colors[PALCOLOR_BLACK].g = 0;
        } else {
            SDL_Log("Warning: No HD sprite sheet for '%s' in zoom level 2!", ObjPicNames.at(id).c_str());
            pSurface = sdl2::surface_ptr{ Scaler::defaultTripleTiledSurface(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };
        }
    } else {
        pSurface = sdl2::surface_ptr{ Scaler::defaultTripleTiledSurface(objPic[id][h][0].get(), objPicTiles[id].x, objPicTiles[id].y) };
    }


    if((pSurface->w > 2048) || (pSurface->h > 2048)) {
        SDL_Log("Warning: Size of sprite sheet for '%s' in zoom level 2 is %dx%d; may exceed hardware limits on older GPUs!", ObjPicNames.at(id).c_str(), pSurface->w, pSurface->h);
    }

    return pSurface;
}
