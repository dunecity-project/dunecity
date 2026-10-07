#include <dunecity/HouseColors.h>
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

#include <globals.h>
#include <mod/ModManager.h>
#include <main.h>
#include <FileClasses/INIFile.h>

#include <SoundPlayer.h>
#include <FileClasses/music/MusicPlayer.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/Palfile.h>
#include <Network/NetworkManager.h>

// Explicit definitions of global variables (instead of relying on EXTERN macro)
// SDL stuff
SDL_Window*          window = nullptr;
SDL_Renderer*        renderer = nullptr;
SDL_Texture*         screenTexture = nullptr;
Palette              palette;
Palette              customPalette;
bool                 customPaletteLoaded = false;
std::array<SDL_Color, 8> rebelsColorRamp{{
    SDL_Color{ 82, 82, 82, 255 },
    SDL_Color{ 72, 72, 72, 255 },
    SDL_Color{ 62, 62, 62, 255 },
    SDL_Color{ 52, 52, 52, 255 },
    SDL_Color{ 42, 42, 42, 255 },
    SDL_Color{ 34, 34, 34, 255 },
    SDL_Color{ 27, 27, 27, 255 },
    SDL_Color{ 20, 20, 20, 255 }
}};
int                  drawnMouseX = 0;
int                  drawnMouseY = 0;
int                  currentZoomlevel = 0;

// abstraction layers
std::unique_ptr<SoundPlayer>         soundPlayer;
std::unique_ptr<MusicPlayer>         musicPlayer;
std::unique_ptr<FileManager>         pFileManager;
std::unique_ptr<GFXManager>          pGFXManager;
std::unique_ptr<SFXManager>          pSFXManager;
std::unique_ptr<FontManager>         pFontManager;
std::unique_ptr<TextManager>         pTextManager;
std::unique_ptr<NetworkManager>      pNetworkManager;

// game stuff
Game*                currentGame = nullptr;
ScreenBorder*        screenborder = nullptr;
Map*                 currentGameMap = nullptr;
House*               pLocalHouse = nullptr;
HumanPlayer*         pLocalPlayer = nullptr;

RobustList<UnitBase*>       unitList;
RobustList<StructureBase*>  structureList;
RobustList<Bullet*>         bulletList;

// misc
SettingsClass    settings;
SettingsClass::GameOptionsClass effectiveGameOptions;
bool debug = false;
std::array<int, NUM_HOUSES> houseToVisualHouse = {
    HOUSE_HARKONNEN,
    HOUSE_ATREIDES,
    HOUSE_ORDOS,
    HOUSE_FREMEN,
    HOUSE_SARDAUKAR,
    HOUSE_MERCENARY,
    HOUSE_NEUTRAL,
    HOUSE_REBELS,
    HOUSE_CUSTOM,
    HOUSECOLOR_GUEST_1,
    HOUSECOLOR_GUEST_2,
    HOUSECOLOR_GUEST_3
};

namespace {

bool tornieGuestHousesActive() {
    return ModManager::instance().isInitialized()
        && ModManager::instance().isTornieContentActive();
}

const std::array<SDL_Color, 8> wildspadeRamp{{
    {168, 20, 96, 255}, {148, 12, 84, 255}, {128, 12, 72, 255}, {108, 8, 60, 255},
    {88, 4, 48, 255}, {68, 4, 40, 255}, {48, 0, 28, 255}, {28, 0, 16, 255}
}};
const std::array<SDL_Color, 8> kleshmershOrangeRamp{{
    {255, 126, 91, 255}, {255, 96, 58, 255}, {248, 65, 24, 255}, {233, 45, 0, 255},
    {196, 35, 0, 255}, {158, 27, 0, 255}, {120, 20, 0, 255}, {82, 13, 0, 255}
}};
const std::array<SDL_Color, 8> kleshmershBrownRamp{{
    {112, 84, 64, 255}, {100, 76, 56, 255}, {88, 64, 52, 255}, {76, 56, 44, 255},
    {64, 48, 36, 255}, {52, 36, 28, 255}, {40, 28, 24, 255}, {28, 20, 16, 255}
}};
const std::array<SDL_Color, 8> tharpiqueRamp{{
    {108, 176, 228, 255}, {72, 156, 212, 255}, {48, 136, 196, 255}, {36, 120, 180, 255},
    {20, 96, 152, 255}, {12, 76, 120, 255}, {8, 52, 88, 255}, {4, 32, 52, 255}
}};
const std::array<SDL_Color, 8> darkGreyRamp{{
    {82, 82, 82, 255}, {72, 72, 72, 255}, {62, 62, 62, 255}, {52, 52, 52, 255},
    {42, 42, 42, 255}, {34, 34, 34, 255}, {27, 27, 27, 255}, {20, 20, 20, 255}
}};

}

std::string userGameOptionsSection() {
    const ModManager& mods = ModManager::instance();
    if(!mods.isInitialized() || mods.getActiveModName() == "vanilla") {
        return std::string();
    }
    return "Game Options " + mods.getActiveModName();
}

void writeGameOptionsToConfig(INIFile& config, const std::string& section, const SettingsClass::GameOptionsClass& options) {
    config.removeKey(section, "Structures Degrade On Concrete"); // obsolete option
    config.setIntValue(section, "Game Speed", options.gameSpeed);
    config.setBoolValue(section, "Concrete Required", options.concreteRequired);
    config.setBoolValue(section, "Fog of War", options.fogOfWar);
    config.setBoolValue(section, "Start with Explored Map", options.startWithExploredMap);
    config.setBoolValue(section, "Instant Build", options.instantBuild);
    config.setBoolValue(section, "Only One Palace", options.onlyOnePalace);
    config.setBoolValue(section, "Rocket-Turrets Need Power", options.rocketTurretsNeedPower);
    config.setBoolValue(section, "Sandworms Respawn", options.sandwormsRespawn);
    config.setBoolValue(section, "Killed Sandworms Drop Spice", options.killedSandwormsDropSpice);
    config.setBoolValue(section, "Manual Carryall Drops", options.manualCarryallDrops);
    config.setIntValue(section, "Maximum Number of Units Override", options.maximumNumberOfUnitsOverride);
    config.setIntValue(section, "Maximum Number of Harvesters Override", options.maximumNumberOfHarvestersOverride);
    config.setIntValue(section, "Maximum Number of Construction Yards Override", options.maximumNumberOfConstructionYardsOverride);
    config.setBoolValue(section, "Immortal Human Player", options.immortalHumanPlayer);
    config.setBoolValue(section, "City Effects", options.cityEffects);
    config.setBoolValue(section, "Original Unit Damage", options.originalUnitDamage);
}

void applyGameOptionsFromConfig(const INIFile& config, const std::string& section, SettingsClass::GameOptionsClass& options) {
    if(section.empty() || !config.hasSection(section)) {
        return;
    }
    auto readBool = [&](const char* key, bool& field) {
        if(config.hasKey(section, key)) field = config.getBoolValue(section, key, field);
    };
    auto readInt = [&](const char* key, int& field) {
        if(config.hasKey(section, key)) field = config.getIntValue(section, key, field);
    };
    readInt("Game Speed", options.gameSpeed);
    readBool("Concrete Required", options.concreteRequired);
    readBool("Fog of War", options.fogOfWar);
    readBool("Start with Explored Map", options.startWithExploredMap);
    readBool("Instant Build", options.instantBuild);
    readBool("Only One Palace", options.onlyOnePalace);
    readBool("Rocket-Turrets Need Power", options.rocketTurretsNeedPower);
    readBool("Sandworms Respawn", options.sandwormsRespawn);
    readBool("Killed Sandworms Drop Spice", options.killedSandwormsDropSpice);
    readBool("Manual Carryall Drops", options.manualCarryallDrops);
    readInt("Maximum Number of Units Override", options.maximumNumberOfUnitsOverride);
    readInt("Maximum Number of Harvesters Override", options.maximumNumberOfHarvestersOverride);
    readInt("Maximum Number of Construction Yards Override", options.maximumNumberOfConstructionYardsOverride);
    readBool("Immortal Human Player", options.immortalHumanPlayer);
    readBool("City Effects", options.cityEffects);
    readBool("Original Unit Damage", options.originalUnitDamage);
}

void saveGameOptionsAsDefaults(const SettingsClass::GameOptionsClass& options) {
    INIFile config(getConfigFilepath());
    const std::string section = userGameOptionsSection();
    if(section.empty()) {
        // Vanilla has no per-mod section, so its choices are the global defaults.
        settings.gameOptions = options;
        writeGameOptionsToConfig(config, "Game Options", options);
    } else {
        // The mod's own GameOptions.ini stays untouched: it is hashed for
        // multiplayer config checks. The player's choices live here instead
        // and are layered over the mod's defaults.
        //
        // The global section is deliberately NOT written from here. What is being saved is the
        // effective rule set, which already has the active mod's defaults folded into it, so
        // copying it to "Game Options" exported those mod defaults to vanilla and every other mod:
        // changing any single option while Dune City was active used to carry Dune City's whole
        // rule set - now including its carryall default - into vanilla play.
        writeGameOptionsToConfig(config, section, options);
    }
    if(!config.saveChangesTo(getConfigFilepath())) {
        SDL_Log("Warning: could not save the game option defaults to the configuration file");
    }

    effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    SDL_Log("Game options saved as defaults%s%s", section.empty() ? "" : " for mod ",
            section.empty() ? "" : ModManager::instance().getActiveModName().c_str());
}

void loadCustomPalette() {
    customPaletteLoaded = false;

    if(pFileManager == nullptr || !pFileManager->exists("Custom_IBM.PAL")) {
        return;
    }

    try {
        customPalette = LoadPalette_RW(pFileManager->openFile("Custom_IBM.PAL").get());
        customPaletteLoaded = customPalette.getNumColors() >= 256;

    if(customPaletteLoaded && tornieGuestHousesActive()) {
        const int darkVioletBase = houseColorToPaletteIndex[HOUSECOLOR_CUSTOM_DARK_VIOLET];
        for(int i = 0; i < 8; ++i) {
            SDL_Color& color = customPalette[darkVioletBase + i];
            color.r = static_cast<Uint8>((static_cast<unsigned int>(color.r) * 9U) / 10U);
            color.g = static_cast<Uint8>((static_cast<unsigned int>(color.g) * 9U) / 10U);
            color.b = static_cast<Uint8>((static_cast<unsigned int>(color.b) * 9U) / 10U);
        }
    }
    } catch(const std::exception& e) {
        SDL_Log("Warning: Could not load Custom_IBM.PAL: %s", e.what());
        customPalette = Palette();
        customPaletteLoaded = false;
    }
}

bool isTornieGuestHouseColorSlot(int colorSlot) {
    return tornieGuestHousesActive()
        && colorSlot >= HOUSECOLOR_GUEST_1 && colorSlot <= HOUSECOLOR_GUEST_3;
}

bool isTornieRebelsColorSlot(int colorSlot) {
    if(!ModManager::instance().isInitialized()) return colorSlot == HOUSE_REBELS;
    const std::string activeMod = ModManager::instance().getActiveModName();
    return ModManager::instance().getContentBase(activeMod) == "Tornie" && colorSlot == HOUSE_REBELS;
}

bool isVanillaRebelsColorSlot(int colorSlot) {
    return colorSlot == HOUSE_REBELS
        && ModManager::instance().isInitialized()
        && ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) == "vanilla";
}

int getHouseColorPaletteIndexFromSlot(int colorSlot) {
    if(!isValidHouseColorSlot(colorSlot)) {
        return PALCOLOR_HARKONNEN;
    }

    const bool tornieMainActive = ModManager::instance().isInitialized()
        && ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) == "Tornie";
    if(tornieMainActive && colorSlot == HOUSECOLOR_CUSTOM_BRIGHT_YELLOW) {
        return PALCOLOR_NEUTRAL;
    }

    if(colorSlot >= HOUSECOLOR_GUEST_1 && colorSlot <= HOUSECOLOR_GUEST_3) {
        if(!tornieGuestHousesActive()) {
            return PALCOLOR_HARKONNEN;
        }
        static const int tornieGuestPalette[3] = {
            PALCOLOR_NEUTRAL, PALCOLOR_REBELS, 136
        };
        return tornieGuestPalette[colorSlot - HOUSECOLOR_GUEST_1];
    }

    return colorSlot == HOUSE_CUSTOM
        ? getHousePaletteIndex(HOUSE_CUSTOM)
        : houseColorToPaletteIndex[colorSlot];
}

bool isDuneCityHouseColorSlot(int colorSlot) {
    return colorSlot >= HOUSE_HARKONNEN && colorSlot <= HOUSE_REBELS
        && ModManager::instance().isInitialized()
        && ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) == "dunecity";
}

SDL_Color getHouseColorSDL(int colorSlot, int shadeOffset) {
    if(!isValidHouseColorSlot(colorSlot) || shadeOffset < 0 || shadeOffset >= 8) {
        return SDL_Color{ 0, 0, 0, 255 };
    }

    if(isDuneCityHouseColorSlot(colorSlot)) {
        return DuneCity::houseColorShade(colorSlot, shadeOffset);
    }

    if(isVanillaRebelsColorSlot(colorSlot)) {
        return darkGreyRamp[shadeOffset];
    }

    if(colorSlot == HOUSECOLOR_CUSTOM_APPLE_GREEN && tornieGuestHousesActive()) {
        return darkGreyRamp[shadeOffset];
    }

    if(colorSlot == HOUSECOLOR_CUSTOM_BRIGHT_YELLOW
       && ModManager::instance().isInitialized()
       && ModManager::instance().isTornieContentActive()) {
        return kleshmershBrownRamp[shadeOffset];
    }

    if(isTornieRebelsColorSlot(colorSlot)) {
        return rebelsColorRamp[shadeOffset];
    }

    if(colorSlot >= HOUSECOLOR_GUEST_1 && colorSlot <= HOUSECOLOR_GUEST_3
       && tornieGuestHousesActive()) {
        if(colorSlot == HOUSECOLOR_GUEST_1) return wildspadeRamp[shadeOffset];
        if(colorSlot == HOUSECOLOR_GUEST_2) return kleshmershOrangeRamp[shadeOffset];
        return tharpiqueRamp[shadeOffset];
    }

    const Palette& sourcePalette = getPaletteForHouseColorSlot(colorSlot);
    const int paletteIndex = getHouseColorPaletteIndexFromSlot(colorSlot) + shadeOffset;
    if(paletteIndex >= 0 && paletteIndex < sourcePalette.getNumColors()) {
        SDL_Color color = sourcePalette[paletteIndex];
        return color;
    }
    return SDL_Color{ 0, 0, 0, 255 };
}

Uint32 getHouseColorRGB(int colorSlot, int shadeOffset) {
    return SDL2RGB(getHouseColorSDL(colorSlot, shadeOffset));
}

void applyCustomPaletteRuntimeHouseRamps() {
    static const Uint8 rebelsGreyRamp[8] = { 82, 72, 62, 52, 42, 34, 27, 20 };
    const bool tornieMainActive =
        ModManager::instance().isInitialized()
        && ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) == "Tornie";
    const bool tornieHouseColorsActive =
        tornieMainActive
        && customPaletteLoaded
        && customPalette.getNumColors() >= PALCOLOR_SARDAUKAR + 8;

    if(!tornieMainActive) {
        if(palette.getNumColors() < 256) {
            return;
        }
        for(int k = 0; k < 8; ++k) {
            SDL_Color& color = palette[PALCOLOR_REBELS + k];
            color = { rebelsGreyRamp[k], rebelsGreyRamp[k], rebelsGreyRamp[k], 255 };
            rebelsColorRamp[k] = color;
        }
        return;
    }

    for(int k = 0; k < 8; ++k) {
        const SDL_Color greyColor{
            rebelsGreyRamp[k], rebelsGreyRamp[k], rebelsGreyRamp[k], 255
        };
        SDL_Color rebelsColor = greyColor;

        if(tornieHouseColorsActive) {
            const SDL_Color customRebelsColor = customPalette[PALCOLOR_SARDAUKAR + k];
            const bool customSlotAlreadySwapped = customRebelsColor.r == greyColor.r
                && customRebelsColor.g == greyColor.g
                && customRebelsColor.b == greyColor.b;
            rebelsColor = customSlotAlreadySwapped ? rebelsColorRamp[k] : customRebelsColor;
            rebelsColor.a = 255;
            if(tornieMainActive) {
                customPalette[PALCOLOR_SARDAUKAR + k] = greyColor;
            }
        }

        rebelsColorRamp[k] = rebelsColor;
    }
}
bool isHouseAvailable(HOUSETYPE house) {
    if(house >= HOUSE_HARKONNEN && house < NUM_LEGACY_HOUSES) return true;
    return house == HOUSE_CUSTOM
        && ModManager::instance().isInitialized()
        && ModManager::instance().isCustomHouseRegistered();
}

int getNumAvailableHouses() {
    return NUM_LEGACY_HOUSES + (isHouseAvailable(HOUSE_CUSTOM) ? 1 : 0);
}

bool isCustomGameHouseAvailable(HOUSETYPE house) {
    if(isHouseAvailable(house)) return true;
    return tornieGuestHousesActive()
        && house >= HOUSE_WILDSPADE && house <= HOUSE_THARPIQUE;
}

int getNumCustomGameHouses() {
    return tornieGuestHousesActive() ? NUM_HOUSES : getNumAvailableHouses();
}

HOUSETYPE getHouseFactionIdentity(HOUSETYPE house) {
    return house;
}

HOUSETYPE getRuntimeHouseForIdentity(HOUSETYPE identity) {
    return identity;
}

bool isHouseFaction(HOUSETYPE house, HOUSETYPE identity) {
    return getHouseFactionIdentity(house) == identity;
}

int getDefaultHouseColorSlot(HOUSETYPE house) {
    if(tornieGuestHousesActive()
       && house >= HOUSE_WILDSPADE && house <= HOUSE_THARPIQUE) {
        return HOUSECOLOR_GUEST_1 + (house - HOUSE_WILDSPADE);
    }
    return house >= HOUSE_HARKONNEN && house < NUM_CAMPAIGN_HOUSES
        ? static_cast<int>(house)
        : HOUSE_HARKONNEN;
}

char getHouseScenarioLetter(HOUSETYPE house) {
    const HOUSETYPE identity = getHouseFactionIdentity(house);
    if(identity == HOUSE_CUSTOM) {
        const CustomHouseInfo& info = ModManager::instance().getActiveCustomHouseInfo();
        return isHouseAvailable(identity) ? info.scenarioLetter : '?';
    }
    if(identity >= HOUSE_WILDSPADE && identity <= HOUSE_THARPIQUE) {
        return tornieGuestHousesActive() ? houseChar[identity] : '?';
    }
    switch(identity) {
        case HOUSE_HARKONNEN: return 'H';
        case HOUSE_ATREIDES: return 'A';
        case HOUSE_ORDOS: return 'O';
        case HOUSE_FREMEN: return 'F';
        case HOUSE_SARDAUKAR: return 'S';
        case HOUSE_MERCENARY: return 'M';
        case HOUSE_NEUTRAL: return 'N';
        case HOUSE_REBELS: return 'R';
        default: return '?';
    }
}

std::string getHouseRegionPrefix(HOUSETYPE house) {
    const HOUSETYPE identity = getHouseFactionIdentity(house);
    if(identity == HOUSE_CUSTOM) {
        const CustomHouseInfo& info = ModManager::instance().getActiveCustomHouseInfo();
        return isHouseAvailable(identity) ? info.regionPrefix : std::string();
    }
    if(identity >= HOUSE_WILDSPADE && identity <= HOUSE_THARPIQUE) {
        static const char* const guestPrefixes[] = { "WIL", "KLE", "THA" };
        return tornieGuestHousesActive()
            ? guestPrefixes[identity - HOUSE_WILDSPADE]
            : std::string();
    }
    static const char* const prefixes[NUM_LEGACY_HOUSES] = {
        "HAR", "ATR", "ORD", "FRE", "SAR", "MER", "NEU", "REB"
    };
    return identity >= HOUSE_HARKONNEN && identity < NUM_LEGACY_HOUSES
        ? prefixes[identity]
        : std::string();
}

int getHousePaletteIndex(HOUSETYPE house) {
    const HOUSETYPE identity = getHouseFactionIdentity(house);
    if(identity >= HOUSE_WILDSPADE && identity <= HOUSE_THARPIQUE
       && !tornieGuestHousesActive()) {
        return PALCOLOR_HARKONNEN;
    }
    if(identity == HOUSE_CUSTOM || identity == HOUSE_THARPIQUE) {
        const CustomHouseInfo& info = ModManager::instance().getCustomHouseInfo(house);
        return info.enabled ? info.paletteIndex : PALCOLOR_HARKONNEN;
    }
    return (identity >= HOUSE_HARKONNEN && identity < NUM_HOUSES)
        ? houseToPaletteIndex[identity]
        : PALCOLOR_HARKONNEN;
}

HOUSETYPE getHouseFallbackHouse(HOUSETYPE house) {
    const HOUSETYPE identity = getHouseFactionIdentity(house);
    if(identity == HOUSE_CUSTOM || identity == HOUSE_THARPIQUE) {
        const CustomHouseInfo& info = ModManager::instance().getCustomHouseInfo(house);
        return info.enabled ? static_cast<HOUSETYPE>(info.fallbackHouse) : HOUSE_HARKONNEN;
    }
    if(identity == HOUSE_WILDSPADE) return tornieGuestHousesActive() ? HOUSE_NEUTRAL : HOUSE_HARKONNEN;
    if(identity == HOUSE_KLESHMERSH) return tornieGuestHousesActive() ? HOUSE_REBELS : HOUSE_HARKONNEN;
    if(identity == HOUSE_THARPIQUE && !tornieGuestHousesActive()) return HOUSE_HARKONNEN;
    return identity;
}

void resetHouseVisualHouseMapping() {
    for(int house = 0; house < NUM_HOUSES; ++house) {
        houseToVisualHouse[house] = getDefaultHouseColorSlot(static_cast<HOUSETYPE>(house));
    }
}

void setHouseVisualHouse(HOUSETYPE house, int visualHouse) {
    if(house < 0 || house >= NUM_HOUSES) {
        return;
    }

    if(!isValidHouseColorSlot(visualHouse)) {
        houseToVisualHouse[house] = getDefaultHouseColorSlot(house);
        return;
    }

    houseToVisualHouse[house] = visualHouse;
}
