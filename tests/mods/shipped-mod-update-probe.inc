// Shipped-mod update probe. Only compiled by run-shipped-mod-update-probe.py, never by the shipped game.
//
// One profile is started by three "builds" that differ only in the rules they ship (the runner
// gives each its own data directory; the program version is the same, as between staging builds):
//
//   old       the shipped Dune City before the update: seeds the profile, starts a Random Skirmish
//             through the menu's Start handler and saves it. With SHIPPED_MOD_PROBE_SNAPSHOT it then
//             leaves the profile as older builds left it: the automatic `ws-<hash>` snapshot of
//             that shipped mod is the active mod.
//   quantbot  only `QuantBot Config.ini.default` changed. The installed Dune City must follow it,
//             stay the approved shipped mod and never be snapshotted by a new game.
//   current   the current shipped rules, started on the profile whose active mod is the old
//             snapshot. New games (without and with a menu choosing the mod) run the current
//             shipped data; the save, and a new game pinned as a replay pins it, still get the
//             old revision.
//
// Each stage prints SHIPPED_MOD_PROBE_PASS or throws after SHIPPED_MOD_PROBE_FAIL.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <FileClasses/INIFile.h>
#include <Menu/RandomSkirmishMenu.h>
#include <Network/OnlineModPolicy.h>
#include <Network/WorkshopGameContent.h>
#include <misc/FileSystem.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <players/QuantBotConfig.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace ShippedModProbe {

void require(bool ok, const std::string& why) {
    if(ok) return;
    SDL_Log("SHIPPED_MOD_PROBE_FAIL: %s", why.c_str());
    throw std::runtime_error(why);
}

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::string activeMod() { return ModManager::instance().getActiveModName(); }

/// What the next start of the game will read.
std::string activeModFile() {
    std::ifstream file(std::filesystem::path(ModManager::instance().getModsBasePath()) / "active_mod.txt");
    std::string name;
    std::getline(file, name);
    return name;
}

std::vector<std::string> snapshotFolders() {
    std::vector<std::string> found;
    for(const auto& entry : std::filesystem::directory_iterator(ModManager::instance().getModsBasePath())) {
        const auto name = entry.path().filename().string();
        if(entry.is_directory() && isWorkshopSnapshotMod(name)) found.push_back(name);
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::string joined(const std::vector<std::string>& names) {
    std::string text;
    for(const auto& name : names) text += (text.empty() ? "" : ", ") + name;
    return text.empty() ? "none" : text;
}

/// The rules a started game runs with.
struct Rules {
    std::string mod;            ///< the mod the game names
    std::string revision;       ///< the revision it pins
    int soldierPrice = 0;       ///< from the game's own object data
    bool buildUpToEnemy = false;///< the QuantBot setting the houses read
    std::string quantBotFile;   ///< where that setting was read from
    bool quantBotFileHasNewKey = false;
};

std::string describe(const Rules& rules) {
    return "mod=" + rules.mod + " soldier=" + std::to_string(rules.soldierPrice)
        + " buildUpToEnemy=" + std::to_string(rules.buildUpToEnemy)
        + " quantbot=" + rules.quantBotFile + (rules.quantBotFileHasNewKey ? " (has PalaceWorm)" : " (no PalaceWorm)");
}

Rules rulesOf(Game& game) {
    Rules rules;
    rules.mod = game.getGameInitSettings().getModName();
    rules.revision = game.getGameInitSettings().getModRevisionHash();
    rules.soldierPrice = game.objectData.data[Unit_Soldier][HOUSE_ATREIDES].price;
    rules.buildUpToEnemy = getQuantBotConfig().force.buildUpToEnemy;
    rules.quantBotFile = getQuantBotConfigFilepath();
    rules.quantBotFileHasNewKey = INIFile(rules.quantBotFile).hasKey("Force Concentration", "PalaceWorm");
    return rules;
}

struct Started {
    std::unique_ptr<Game> game;
    Rules rules;
    ~Started() {
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
    }
};

Started start(const GameInitSettings& init) {
    Started started;
    started.game = std::make_unique<Game>();
    currentGame = started.game.get();
    started.game->initGame(init);
    started.rules = rulesOf(*started.game);
    return started;
}

/// The Random Skirmish screen's Start handler up to the game itself: it reads the choices of the
/// profile's [Random Skirmish] (Dune City), activates that mod, generates the map and builds the
/// game setup that startSinglePlayerGame() then hands to Game::initGame.
Started startRandomSkirmish() {
    GameInitSettings init;
    {
        RandomSkirmishMenu menu;
        require(menu.prepareStart(init), "Random Skirmish Start did not produce a game");
    }
    return start(init);
}

/// A new game whose screen does not pick a mod (Skirmish, a campaign mission): it runs the mod
/// that is active when the screen builds its setup.
GameInitSettings unpickedGame() {
    std::ifstream mapFile(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(mapFile)), {});
    require(!mapData.empty(), "Missing fixture map");
    GameInitSettings init("shipped-mod-probe.ini", mapData, false, effectiveGameOptions);
    init.setRandomSeed(4242);
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
    init.addHouseInfo(human);
    GameInitSettings::HouseInfo bot(HOUSE_ORDOS, 2);
    bot.addPlayerInfo({"Hard bot", "qBotHard"});
    init.addHouseInfo(bot);
    return init;
}

void requireShippedDuneCity(const Rules& rules, int soldierPrice, bool buildUpToEnemy, const std::string& what) {
    const std::string seen = what + ": " + describe(rules) + "; active mod " + activeMod();
    require(rules.mod == "dunecity" && activeMod() == "dunecity", "not the shipped Dune City mod. " + seen);
    require(activeModFile() == "dunecity", "the next start would not use the shipped Dune City mod (active_mod.txt="
            + activeModFile() + "). " + seen);
    require(rules.soldierPrice == soldierPrice,
            "Soldier price is not the shipped " + std::to_string(soldierPrice) + ". " + seen);
    const auto shippedFile = std::filesystem::path(ModManager::instance().getModPath("dunecity")) / "QuantBot Config.ini";
    require(std::filesystem::path(rules.quantBotFile) == shippedFile,
            "QuantBot settings were not read from the shipped mod. " + seen);
    require(rules.buildUpToEnemy == buildUpToEnemy, "QuantBot BuildUpToEnemy is not the shipped value. " + seen);
    require(OnlineModPolicy::approved("dunecity"), "the installed Dune City is not the build's shipped mod. " + seen);
}

void old() {
    const int oldPrice = std::stoi(env("SHIPPED_MOD_PROBE_OLD_PRICE"));
    auto started = startRandomSkirmish();
    requireShippedDuneCity(started.rules, oldPrice, false, "old build's Random Skirmish");
    require(!started.rules.quantBotFileHasNewKey, "the old shipped QuantBot settings already have PalaceWorm");
    require(snapshotFolders().empty(), "the shipped mod was snapshotted: " + joined(snapshotFolders()));
    require(!started.rules.revision.empty(), "the new game pinned no mod revision");
    started.game->initializeGameLoop();
    require(started.game->saveGame(env("SHIPPED_MOD_PROBE_SAVE")), "the old build's game was not saved");
    if(!env("SHIPPED_MOD_PROBE_SNAPSHOT").empty()) {
        require(Workshop::activateModRevision(started.rules.revision), "cannot install the old automatic snapshot");
        require(activeModFile() == "ws-" + started.rules.revision, "the old snapshot is not the stored active mod");
    }
    SDL_Log("SHIPPED_MOD_PROBE_OLD: revision=%s price=%d", started.rules.revision.c_str(), started.rules.soldierPrice);
}

void quantbot() {
    const int oldPrice = std::stoi(env("SHIPPED_MOD_PROBE_OLD_PRICE"));
    require(activeMod() == "dunecity", "the profile did not start on Dune City: " + activeMod());
    auto started = startRandomSkirmish();
    requireShippedDuneCity(started.rules, oldPrice, true, "Random Skirmish after a QuantBot-only update");
    require(started.rules.quantBotFileHasNewKey, "the installed QuantBot settings lack the shipped PalaceWorm key");
    require(snapshotFolders().empty(), "a new game on the shipped mod was snapshotted: " + joined(snapshotFolders()));
}

void current() {
    const int oldPrice = std::stoi(env("SHIPPED_MOD_PROBE_OLD_PRICE"));
    const int newPrice = std::stoi(env("SHIPPED_MOD_PROBE_NEW_PRICE"));
    const std::string oldRevision = env("SHIPPED_MOD_PROBE_OLD_REVISION");
    const std::string snapshot = "ws-" + oldRevision;
    auto& mods = ModManager::instance();
    require(mods.modExists(snapshot), "the profile lost the old snapshot folder");

    // 1. The start itself: before any menu, the player is back on the shipped mod.
    require(activeMod() == "dunecity", "the game started on " + activeMod() + ", not the shipped Dune City");
    require(activeModFile() == "dunecity", "active_mod.txt still names " + activeModFile());

    // 2. A new game from a screen that does not pick a mod.
    {
        const GameInitSettings init = unpickedGame();
        require(init.getModName() == "dunecity", "a new game is set up for " + init.getModName());
        auto started = start(init);
        requireShippedDuneCity(started.rules, newPrice, true, "new game without a mod choice");
        require(started.rules.quantBotFileHasNewKey, "the shipped QuantBot settings lack the PalaceWorm key");
        require(started.rules.revision != oldRevision, "the new game pins the old revision");
    }

    // 3. A new Random Skirmish, as the player starts one.
    {
        auto started = startRandomSkirmish();
        requireShippedDuneCity(started.rules, newPrice, true, "new Random Skirmish");
        require(started.rules.revision != oldRevision, "the new Random Skirmish pins the old revision");
    }
    require(snapshotFolders() == std::vector<std::string>{snapshot},
            "new games on the shipped mod installed snapshots: " + joined(snapshotFolders()));

    // 4. The old build's save still loads the revision it was saved with.
    {
        auto started = start(GameInitSettings(env("SHIPPED_MOD_PROBE_SAVE")));
        require(started.rules.revision == oldRevision, "the save lost its pinned revision: " + started.rules.revision);
        require(activeMod() == snapshot, "the save did not load its pinned revision; active mod " + activeMod());
        require(INIFile(mods.getActiveObjectDataPath()).getIntValue("Soldier", "Price", -1) == oldPrice,
                "the save's mod does not have the old Soldier price");
        require(started.rules.soldierPrice == oldPrice, "the loaded game has Soldier price "
                + std::to_string(started.rules.soldierPrice));
    }

    // 5. A replay names its revision the same way and takes the same route (Game::initGame).
    require(mods.setActiveMod("dunecity"), "cannot return to Dune City");
    {
        GameInitSettings recorded = unpickedGame();
        recorded.setModRevision(oldRevision, 1);
        auto started = start(recorded);
        require(activeMod() == snapshot && started.rules.soldierPrice == oldPrice,
                "a game pinned to the old revision ran " + describe(started.rules) + "; active mod " + activeMod());
    }

    // 6. After such a load, the next new game is on the shipped mod again.
    {
        auto started = startRandomSkirmish();
        require(started.rules.mod == "dunecity" && activeMod() == "dunecity" && started.rules.soldierPrice == newPrice,
                "a new game after loading the old save ran " + describe(started.rules) + "; active mod " + activeMod());
    }
}

}

int runShippedModUpdateProbe() {
    using namespace ShippedModProbe;
    settings.general.playerName = "Shipped mod tester";
    const std::string stage = env("SHIPPED_MOD_PROBE_STAGE");
    SDL_Log("SHIPPED_MOD_PROBE_START: stage=%s active=%s file=%s snapshots=%s", stage.c_str(), activeMod().c_str(),
            activeModFile().c_str(), joined(snapshotFolders()).c_str());
    if(stage == "old") old();
    else if(stage == "quantbot") quantbot();
    else if(stage == "current") current();
    else require(false, "unknown stage " + stage);
    SDL_Log("SHIPPED_MOD_PROBE_PASS: stage=%s active=%s", stage.c_str(), activeMod().c_str());
    return MENU_QUIT_DEFAULT;
}
