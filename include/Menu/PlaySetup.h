#ifndef PLAYSETUP_H
#define PLAYSETUP_H

#include <GameInitSettings.h>
#include <Network/ChangeEventList.h>
#include <mod/ModInfo.h>
#include <string>
#include <vector>

// Local setup only: no connection is opened until the player chooses Create Lobby.
struct CustomPlaySetup {
    std::vector<std::string> maps;
    std::vector<ModInfo> mods;
    int map = 0;
    int mapCategory = 4;
    int mod = 0;
    bool online = false;
    bool publicGame = true;
    bool allowJoinAfterStart = true;
    bool sharedHouse = false;
    // Offline only: leave every faction to an AI and watch. Survives the map/mod/rules
    // rebuilds because it lives here rather than in the rebuilt GameInitSettings, and is
    // cleared whenever `online` is chosen so a host can never become a local observer.
    bool spectate = false;
    SettingsClass::GameOptionsClass rules;
    ChangeEventList players;
};

constexpr int MENU_SETUP_CHANGED = -20;
constexpr int MENU_SETUP_HOST = -21;
constexpr int MENU_SETUP_MAP = -22;
constexpr int MENU_SETUP_PLAYERS = -23;
// The Offline/Online choice is remembered separately for the campaign and for custom
// games; the two menus are entered for different reasons and share no other state.
enum class PlayModeScope { Campaign, CustomGame };
void rememberPlayMode(PlayModeScope scope, bool online);
// The mod a player picks for a custom game is remembered across maps, menus and
// restarts. Mods activated automatically (a save, a join, a campaign) do not change it.
void rememberCustomGameMod(const std::string& modName);
SettingsClass::GameOptionsClass initialCustomGameRules();
void playCustomGame(bool online);
void showGameLibrary(bool replays = false);
bool hasRecentGame();
void continueRecentGame();

#endif
