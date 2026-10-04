/*
 *  globals_test_stub.cpp - Minimal test stub for globals needed by CitySimulation tests.
 *
 *  Provides null pointers and minimal stubs so that CitySimulation.cpp and sounds.cpp
 *  compile and link in the test executable without the full Dune Legacy game
 *  infrastructure.
 */

#include <globals.h>
#include <mod/ModManager.h>

#include <string>
#include <memory>

// QuantBotConfig's two path helpers ask the mod manager where the active mod
// keeps its files. The army-recovery test drives the real config object for its
// multiplayer hash, but never its file paths, so the mod lookup is stubbed here
// rather than linking the whole mod subsystem into the unit test target.
std::string ModManager::getActiveObjectDataPath() const { return {}; }
std::string ModManager::getActiveQuantBotConfigPath() const { return {}; }

// Forward-declared minimal stubs for manager classes (satisfy header method signatures)
class SoundPlayer {};
class MusicPlayer {};
class FileManager {};
class GFXManager {};
class SFXManager {};
class FontManager {};
class TextManager {
public:
    std::string getLocalized(const std::istream&) const { return ""; }
    std::string getLocalized(const std::string& msgid) const { return msgid; }
    std::string postProcessString(const std::string& s) const { return s; }
};
class NetworkManager {};

// Null-initialized global manager pointers (test context: no game running)
std::unique_ptr<SoundPlayer>   soundPlayer;
std::unique_ptr<MusicPlayer>   musicPlayer;
std::unique_ptr<FileManager>    pFileManager;
std::unique_ptr<GFXManager>     pGFXManager;
std::unique_ptr<SFXManager>     pSFXManager;
std::unique_ptr<FontManager>    pFontManager;
std::unique_ptr<TextManager>    pTextManager{new TextManager};
std::unique_ptr<NetworkManager> pNetworkManager;

// Game globals — null in test context (currentGame guard protects NewsTicker calls)
Game*        currentGame     = nullptr;
Map*         currentGameMap  = nullptr;
ScreenBorder* screenborder   = nullptr;

// Minimal SDL stubs (satisfy linker)
SDL_Window*  window         = nullptr;
SDL_Renderer* renderer      = nullptr;
SDL_Texture* screenTexture  = nullptr;
Palette      palette;
int          drawnMouseX    = 0;
int          drawnMouseY    = 0;
int          currentZoomlevel = 0;
bool         debug          = false;

// Settings stubs (empty structs, test context)
SettingsClass settings;
SettingsClass::GameOptionsClass effectiveGameOptions;

HOUSETYPE getHouseFactionIdentity(HOUSETYPE house) {
    return house;
}
