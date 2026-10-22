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

#ifndef GLOBALS_H
#define GLOBALS_H

#include <DataTypes.h>
#include <Definitions.h>
#include <Colors.h>
#include <FileClasses/Palette.h>
#include <data.h>
#include <misc/RobustList.h>
#include <misc/DrawingRectHelper.h>

#include <misc/SDL2pp.h>

#include <array>
#include <memory>

#define _(msgid) pTextManager->getLocalized(msgid)

// forward declarations
class SoundPlayer;
class MusicPlayer;

class FileManager;
class GFXManager;
class SFXManager;
class FontManager;
class TextManager;
class NetworkManager;

class Game;
class Map;
class ScreenBorder;
class House;
class HumanPlayer;
class UnitBase;
class StructureBase;
class Bullet;

#ifndef SKIP_EXTERN_DEFINITION
 #define EXTERN extern
#else
 #define EXTERN
#endif


// SDL stuff
EXTERN SDL_Window*          window;                     ///< the window
EXTERN SDL_Renderer*        renderer;                   ///< the renderer
EXTERN SDL_Texture*         screenTexture;              ///< the texture
EXTERN Palette              palette;                    ///< the palette for the screen
EXTERN Palette              customPalette;              ///< optional Custom_IBM.PAL for extra visual color ramps
EXTERN bool                 customPaletteLoaded;        ///< true when Custom_IBM.PAL is available
EXTERN std::array<SDL_Color, 8> rebelsColorRamp;         ///< private Rebels ramp; never written into IBM.PAL
EXTERN int                  drawnMouseX;                ///< the current mouse position (x coordinate)
EXTERN int                  drawnMouseY;                ///< the current mouse position (y coordinate)
EXTERN int                  currentZoomlevel;           ///< 0 = the smallest zoom level, 1 = medium zoom level, 2 = maximum zoom level


// abstraction layers
EXTERN std::unique_ptr<SoundPlayer>         soundPlayer;                ///< manager for playing sfx and voice
EXTERN std::unique_ptr<MusicPlayer>         musicPlayer;                ///< manager for playing background music

EXTERN std::unique_ptr<FileManager>         pFileManager;               ///< manager for loading files from PAKs
EXTERN std::unique_ptr<GFXManager>          pGFXManager;                ///< manager for loading and managing graphics
EXTERN std::unique_ptr<SFXManager>          pSFXManager;                ///< manager for loading and managing sounds
EXTERN std::unique_ptr<FontManager>         pFontManager;               ///< manager for loading and managing fonts
EXTERN std::unique_ptr<TextManager>         pTextManager;               ///< manager for loading and managing texts and providing localization
EXTERN std::unique_ptr<NetworkManager>      pNetworkManager;            ///< manager for all network events (nullptr if not in multiplayer game)

// game stuff
EXTERN Game*                currentGame;                ///< the current running game
EXTERN ScreenBorder*        screenborder;               ///< the screen border for the current running game
EXTERN Map*                 currentGameMap;             ///< the map for the current running game
EXTERN House*               pLocalHouse;                ///< the house of the human player that is playing the current running game on this computer
EXTERN HumanPlayer*         pLocalPlayer;               ///< the player that is playing the current running game on this computer

EXTERN RobustList<UnitBase*>       unitList;            ///< the list of all units
EXTERN RobustList<StructureBase*>  structureList;       ///< the list of all structures
EXTERN RobustList<Bullet*>         bulletList;          ///< the list of all bullets


// misc
EXTERN SettingsClass    settings;                       ///< the settings read from the settings file
EXTERN SettingsClass::GameOptionsClass effectiveGameOptions;  ///< effective game options (settings + mod overrides)

EXTERN bool debug;                                      ///< is set for debugging purposes
EXTERN std::array<int, NUM_HOUSES> houseToVisualHouse;  ///< runtime visual color slot for each house


// constants
static const int houseToPaletteIndex[NUM_HOUSES] = {
    PALCOLOR_HARKONNEN, PALCOLOR_ATREIDES, PALCOLOR_ORDOS, PALCOLOR_FREMEN,
    PALCOLOR_SARDAUKAR, PALCOLOR_MERCENARY, PALCOLOR_NEUTRAL, PALCOLOR_REBELS,
    PALCOLOR_HARKONNEN, PALCOLOR_NEUTRAL, PALCOLOR_REBELS, PALCOLOR_MERCENARY,
    PALCOLOR_HARKONNEN  // Natives: remapped through its private ramp (HOUSECOLOR_NATIVES)
};    ///< the base colors for the different house identities
static const int houseColorToPaletteIndex[NUM_HOUSE_COLOR_SLOTS] = {
    PALCOLOR_HARKONNEN, PALCOLOR_ATREIDES, PALCOLOR_ORDOS, PALCOLOR_FREMEN,
    PALCOLOR_SARDAUKAR, PALCOLOR_MERCENARY, PALCOLOR_NEUTRAL, PALCOLOR_REBELS, PALCOLOR_HARKONNEN,
    PALCOLOR_HARKONNEN, PALCOLOR_ATREIDES, PALCOLOR_ORDOS, PALCOLOR_FREMEN,
    PALCOLOR_SARDAUKAR, PALCOLOR_MERCENARY,
    PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN, PALCOLOR_HARKONNEN,
    PALCOLOR_HARKONNEN  // HOUSECOLOR_NATIVES: private ramp
};    ///< palette ramp used by house colors, including custom visual-only colors
/// ObjectData "(X)" suffix letters. Natives has none ('?' never matches a key), so it always
/// gets the plain values of every [Unit] section.
static const char houseChar[] = { 'H', 'A', 'O', 'F', 'S', 'M', 'N', 'R', 'C', 'W', 'K', 'T', '?' };
static_assert(sizeof(houseChar) == NUM_HOUSES, "houseChar needs one letter per house");

int getHousePaletteIndex(HOUSETYPE house);
int getHouseColorPaletteIndexFromSlot(int colorSlot);

inline bool isValidHouseColorSlot(int colorSlot) {
    return colorSlot >= 0 && colorSlot < NUM_HOUSE_COLOR_SLOTS;
}

inline bool isCustomHouseColorSlot(int colorSlot) {
    return colorSlot >= NUM_BASE_HOUSE_COLOR_SLOTS && colorSlot < HOUSECOLOR_GUEST_1;
}

inline int getHouseVisualHouse(int house) {
    if(house >= 0 && house < NUM_HOUSES) {
        const int visualHouse = houseToVisualHouse[house];
        if(isValidHouseColorSlot(visualHouse)) {
            return visualHouse;
        }
    }

    return house;
}

inline int getHouseColorPaletteIndex(int house) {
    const int visualHouse = getHouseVisualHouse(house);
    if(isValidHouseColorSlot(visualHouse)) {
        return getHouseColorPaletteIndexFromSlot(visualHouse);
    }

    return PALCOLOR_HARKONNEN;
}

bool isTornieGuestHouseColorSlot(int colorSlot);
bool isTornieRebelsColorSlot(int colorSlot);
bool isVanillaRebelsColorSlot(int colorSlot);

inline const Palette& getPaletteForHouseColorSlot(int colorSlot) {
    return (colorSlot == HOUSE_CUSTOM || isCustomHouseColorSlot(colorSlot) || isTornieGuestHouseColorSlot(colorSlot)) && customPaletteLoaded
        ? customPalette
        : palette;
}

bool isDuneCityHouseColorSlot(int colorSlot);
SDL_Color getHouseColorSDL(int colorSlot, int shadeOffset = 3);
Uint32 getHouseColorRGB(int colorSlot, int shadeOffset = 3);

void loadCustomPalette();

class INIFile;
/// Config section holding the player's own game option choices for the active
/// mod (empty for vanilla, which keeps them in [Game Options] directly).
std::string userGameOptionsSection();
/// Writes every game option key into the given config section.
void writeGameOptionsToConfig(INIFile& config, const std::string& section, const SettingsClass::GameOptionsClass& options);
/// Overlays the keys present in the given config section onto options.
void applyGameOptionsFromConfig(const INIFile& config, const std::string& section, SettingsClass::GameOptionsClass& options);
/// Makes options the new defaults: updates settings, the config file (base
/// section and the per-mod override) and the effective options.
void saveGameOptionsAsDefaults(const SettingsClass::GameOptionsClass& options);
void applyCustomPaletteRuntimeHouseRamps();
bool isHouseAvailable(HOUSETYPE house);
int getNumAvailableHouses();
bool isCustomGameHouseAvailable(HOUSETYPE house);
int getNumCustomGameHouses();
HOUSETYPE getHouseFactionIdentity(HOUSETYPE house);
HOUSETYPE getRuntimeHouseForIdentity(HOUSETYPE identity);
bool isHouseFaction(HOUSETYPE house, HOUSETYPE identity);
int getDefaultHouseColorSlot(HOUSETYPE house);
char getHouseScenarioLetter(HOUSETYPE house);
std::string getHouseRegionPrefix(HOUSETYPE house);
HOUSETYPE getHouseFallbackHouse(HOUSETYPE house);

void resetHouseVisualHouseMapping();
void setHouseVisualHouse(HOUSETYPE house, int visualHouse);

#endif //GLOBALS_H
