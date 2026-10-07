#include <Menu/PlaySetup.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/Dune2RReadinessMenu.h>
#include <Menu/MenuBase.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/TextManager.h>
#include <GUI/MsgBox.h>
#include <GUI/dune/LoadSaveWindow.h>
#include <misc/FileSystem.h>
#include <misc/IMemoryStream.h>
#include <misc/fnkdat.h>
#include <misc/string_util.h>
#include <misc/WebRuntime.h>
#include <mod/ModManager.h>
#include <Network/WorkshopGameContent.h>
#include <globals.h>
#include <main.h>
#include <sand.h>
#include <algorithm>
#include <utility>

namespace {
std::string userDirectory(const char* name) {
    char path[FILENAME_MAX];
    return fnkdat(name, path, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) >= 0 ? path : "";
}

std::vector<std::string> maps() {
    std::vector<std::string> result;
    for(const char* kind : {"singleplayer", "multiplayer"}) {
        std::string dir = getDuneLegacyDataDir() + "/maps/" + kind + "/";
        if(getFileNamesList(dir, "ini", true).empty()) dir = getDuneLegacyDataDir() + "/data/maps/" + kind + "/";
        for(const auto& source : {dir, userDirectory((std::string("maps/") + kind + "/").c_str())}) {
            if(source.empty()) continue;
            for(const auto& file : getFileNamesList(source, "ini", true)) result.push_back(source + file);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return strToLower(getBasename(a)) < strToLower(getBasename(b));
    });
    return result;
}

// Use the existing validated save header parser. A file's folder is not its game type.
GameInitSettings readSetup(const std::string& path, GameInitSettings::HouseInfoList& houses) {
    const auto bytes = readCompleteFile(path);
    IMemoryStream stream(bytes.data(), bytes.size());
    return GameInitSettings::readSaveSetup(stream, houses);
}

class PlayError final : public MenuBase {
public:
    explicit PlayError(const std::string& message) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
        openWindow(MsgBox::create(message));
    }
    void onChildWindowClose(Window*) override { quit(); }
};

class GameLibrary final : public MenuBase {
public:
    explicit GameLibrary(bool replay) : replay(replay) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
    }
    int showMenu() override {
        const auto saves = userDirectory("save/");
        const auto shared = userDirectory("mpsave/");
        const auto recordings = userDirectory("replay/");
        if(replay) openWindow(LoadSaveWindow::create(false, _("Replays"), recordings, "rpl"));
        else openWindow(LoadSaveWindow::create(false, _("Load Game"),
            std::vector<std::string>{saves, shared}, std::vector<std::string>{_("Saved games"), _("Online saves")}, "dls"));
        return MenuBase::showMenu();
    }
    void onChildWindowClose(Window* child) override {
        auto* picker = dynamic_cast<LoadSaveWindow*>(child);
        if(!picker) { quit(); return; }
        const auto path = picker->getFilename();
        if(path.empty()) { quit(); return; }
        // Run after the modal has been removed, rather than starting a nested game from its destructor.
        selected = path;
    }
    void update() override {
        if(selected.empty()) return;
        const auto path = std::exchange(selected, {});
        try {
            if(replay) startReplay(path);
            else {
                GameInitSettings::HouseInfoList houses;
                auto saved = readSetup(path, houses);
                WorkshopGameContent::resolveMod(saved);
                if(isNetworkGameType(saved.getGameType())) {
                    auto& mods = ModManager::instance();

                    GameInitSettings init(getBasename(path), readCompleteFile(path), settings.general.playerName + "'s saved game");
                    init.configureCoopSave(saved, houses);
                    if(isCoopGameType(saved.getGameType())) {
                        init.enableCoop(true, settings.general.playerName + "'s co-op campaign");
                    }
                    CrossplayMenu(init, true).showMenu();
                } else startSinglePlayerGame(GameInitSettings(path));
            }
            quit();
        } catch(const std::exception& error) { openWindow(MsgBox::create(error.what())); }
    }
private:
    bool replay;
    std::string selected;
};

std::string recentGame() {
    const auto dir = userDirectory("save/");
    if(dir.empty()) return {};
    const auto installed = ModManager::instance().listMods();
    for(const auto& file : getFileNamesList(dir, "dls", true, FileListOrder_ModifyDate_Dsc)) {
        try {
            GameInitSettings::HouseInfoList houses;
            const auto saved = readSetup(dir + file, houses);
            if(!isNetworkGameType(saved.getGameType())
               && (!saved.getModRevisionHash().empty() || std::any_of(installed.begin(), installed.end(), [&](const auto& mod) { return mod.name == saved.getModName(); }))) return dir + file;
        } catch(const std::exception&) { /* Skip corrupt, newer or unavailable saves. */ }
    }
    return {};
}
}

void rememberPlayMode(PlayModeScope scope, bool online) {
    const bool campaign = scope == PlayModeScope::Campaign;
    bool& remembered = campaign ? settings.general.campaignOnline : settings.general.customGameOnline;
    if(remembered == online) return;
    remembered = online;
    INIFile config(getConfigFilepath());
    config.setBoolValue("General", campaign ? "Campaign Online" : "Custom Game Online", online);
    if(!config.saveChangesTo(getConfigFilepath())) {
        SDL_Log("Warning: could not save the Offline/Online choice to the configuration file");
        return;
    }
    WebRuntime::syncPersistentFiles();
}

void rememberCustomGameMod(const std::string& modName) {
    if(modName.empty() || settings.general.customGameMod == modName) return;
    settings.general.customGameMod = modName;
    INIFile config(getConfigFilepath());
    config.setStringValue("General", "Custom Game Mod", modName);
    if(!config.saveChangesTo(getConfigFilepath())) {
        SDL_Log("Warning: could not save the custom game mod choice to the configuration file");
        return;
    }
    WebRuntime::syncPersistentFiles();
}

void showGameLibrary(bool replays) { GameLibrary(replays).showMenu(); }
bool hasRecentGame() { return !recentGame().empty(); }
void continueRecentGame() {
    const auto path = recentGame();
    if(!path.empty()) startSinglePlayerGame(GameInitSettings(path));
    else showGameLibrary();
}

SettingsClass::GameOptionsClass initialCustomGameRules() {
    // Saves, joins and map previews can activate content without updating the
    // menu's cached defaults. A NEW game must obtain rules from its actual mod,
    // even when that mod is already active. Back/Next keeps setup.rules instead.
    auto& mods = ModManager::instance();
    const auto& preferred = settings.general.customGameMod;
    if(!preferred.empty() && preferred != mods.getActiveModName() && mods.modExists(preferred))
        mods.setActiveMod(preferred);
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    return effectiveGameOptions;
}

void playCustomGame(bool online) {
    CustomPlaySetup setup;
    setup.rules = initialCustomGameRules();
    setup.maps = maps();
    setup.mods = ModManager::instance().listModChoices();
    setup.online = online;
    // One controller per house is the legible default; advanced sharing stays available.
    setup.sharedHouse = false;
    for(size_t i = 0; i < setup.mods.size(); ++i)
        if(setup.mods[i].matchesSelectionName(ModManager::instance().getActiveModName())) setup.mod = static_cast<int>(i);
    if(setup.maps.empty()) { PlayError(_("No custom maps found. Create a map in Editors > Map Editor, then return here.")).showMenu(); return; }
    bool chooseMap = true;
    for(;;) {
        if(chooseMap) {
            CustomGameMenu browser(false, false, &setup);
            if(browser.showMenu() != MENU_SETUP_PLAYERS) return;
            chooseMap = false;
        }
        auto& mods = ModManager::instance();
        const auto oldMod = mods.getActiveModName();
        if(!setup.mods.empty() && setup.mods[setup.mod].name != oldMod) {
            if(mods.setActiveMod(setup.mods[setup.mod].name)) setup.rules = effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
            else for(size_t i = 0; i < setup.mods.size(); ++i) if(setup.mods[i].name == oldMod) setup.mod = static_cast<int>(i);
        }
        // The canonical Dune2R artwork is completed and verified here, before this game
        // captures a mod version - also when the mod was switched in the player setup,
        // which returns to this loop. A cancelled or failed download returns to the map
        // chooser rather than starting with partial art. An already verified, unchanged
        // target costs nothing.
        if(setup.mod >= 0 && setup.mod < static_cast<int>(setup.mods.size())) {
            const auto artwork = ensureDune2RArtworkReady(setup.mods[setup.mod].name);
            if(!artwork.ready) {
                if(!artwork.message.empty()) PlayError(artwork.message).showMenu();
                chooseMap = true;
                continue;
            }
        }
        const auto path = setup.maps[setup.map];
        GameInitSettings init(getBasename(path, true), readCompleteFile(path), setup.sharedHouse, setup.rules);
        // A downloaded map's authored revision is reused when the selected mod is the one
        // it was authored with. With another mod chosen, or after the working map changed,
        // these bytes are captured as a new revision of the selected mod when the game starts.
        if(existsFile(path + ".workshop.ini")) {
            try { WorkshopGameContent::applyMapRevisionForSelectedMod(path, init); }
            catch(const std::exception& error) { PlayError(error.what()).showMenu(); chooseMap = true; continue; }
        }
        int result;
        {
            CustomGamePlayers menu(init, true, false, &setup);
            result = menu.showMenu();
        }
        if(result == MENU_SETUP_CHANGED) continue;
        if(result == MENU_SETUP_MAP || result == MENU_QUIT_DEFAULT) { chooseMap = true; continue; }
        if(result != MENU_SETUP_HOST) return;
        GameInitSettings networkInit(getBasename(path, true), readCompleteFile(path), settings.general.playerName + "'s custom game", setup.sharedHouse, setup.rules);
        networkInit.setModRevision(init.getModRevisionHash(), init.getModRevisionVersion());
        networkInit.setMapRevision(init.getMapRevisionHash(), init.getMapRevisionVersion(), init.getMapRevisionManifest());
        if(CrossplayMenu(networkInit, setup.publicGame, setup.players, setup.allowJoinAfterStart).showMenu() == MENU_QUIT_GAME_FINISHED) return;
    }
}
