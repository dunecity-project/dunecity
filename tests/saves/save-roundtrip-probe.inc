// Included only by run-save-roundtrip-probe.py, never by the shipped game. Two cases:
//   make-save  (SAVE_PROBE_MAKER, linked against an earlier release's tree; only API that release
//              already had): plays a vanilla Gamma Sector FFA and a DuneCity Habbanya-Penny 1v1 for
//              SAVE_PROBE_MINUTES game minutes and saves both mid-game, worms on the map
//   load       (this release): SAVE_PROBE_SAVE loads, runs 600 cycles, is re-saved as the current
//              savegame version and loads again; every stage reports mod, pinned revision, saving
//              release, houses, units, structures and who owns the sandworms (the Natives)
#include <Menu/RandomSkirmishMenu.h>
#include <players/HumanPlayer.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <mod/ModManager.h>
#include <misc/FileSystem.h>
#include <misc/string_util.h>
#include <sand.h>
#ifndef SAVE_PROBE_MAKER
#include <Network/WorkshopGameContent.h>
#endif

#include <cstdlib>
#include <map>

namespace {

void sRequire(bool ok, const std::string& what) {
    if(!ok) throw std::runtime_error(what);
}

std::string sEnv(const char* name) { const char* value = std::getenv(name); return value ? value : ""; }

std::unique_ptr<Game> sStart(const GameInitSettings& init) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;   // no briefing / analytics at setup
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    return game;
}

void sEnd(std::unique_ptr<Game>& game) {
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
}

void sRun(Game& game, Uint32 cycles) {
    const Uint32 end = game.getGameCycleCount() + cycles;
    while(game.getGameCycleCount() < end && !game.finishedLevel) game.updateGameState();
}

std::string sHouse(int h) { return getHouseNameByNumber(static_cast<HOUSETYPE>(h)); }

/// Houses (name: units/structures, players), worm owners. Returns the worm owners.
std::map<int, int> sReport(Game& game, const std::string& label) {
    std::map<int, int> units, structures, worms;
    for(const UnitBase* unit : unitList) {
        ++units[unit->getOwner()->getHouseID()];
        if(unit->getItemID() == Unit_Sandworm) ++worms[unit->getOwner()->getHouseID()];
    }
    for(const StructureBase* structure : structureList) ++structures[structure->getOwner()->getHouseID()];
    std::string houses, wormOwners;
    int count = 0;
    for(int h = 0; h < NUM_HOUSES; ++h) {
        const House* house = game.getHouse(h);
        if(!house) continue;
        ++count;
        houses += " " + sHouse(h) + "(team " + std::to_string(house->getTeamID()) + ", players "
                  + std::to_string(house->getPlayerList().size()) + ", " + std::to_string(units[h]) + "u/"
                  + std::to_string(structures[h]) + "s)";
    }
    for(const auto& [h, n] : worms) wormOwners += " " + sHouse(h) + "=" + std::to_string(n);
    const auto& init = game.getGameInitSettings();
#ifdef SAVE_PROBE_MAKER
    const std::string savedBy = "-";
#else
    const std::string savedBy = init.getSavedGameVersion().empty() ? "-" : init.getSavedGameVersion();
#endif
    SDL_Log("SAVE_PROBE: %s: version %s, savegame %u, saved by %s, mod '%s' (save names '%s'), revision %s, cycle %u, "
            "%d houses, %zu units, %zu structures, worms:%s; houses:%s",
            label.c_str(), VERSION, game.getLoadedSavegameVersion(), savedBy.c_str(),
            ModManager::instance().getActiveModName().c_str(), init.getModName().c_str(),
            init.getModRevisionHash().empty() ? "-" : init.getModRevisionHash().substr(0, 16).c_str(),
            game.getGameCycleCount(), count, unitList.size(), structureList.size(),
            wormOwners.empty() ? " none" : wormOwners.c_str(), houses.c_str());
    return worms;
}

#ifdef SAVE_PROBE_MAKER

GameInitSettings sMatch(const std::string& mod, const std::string& map, const std::vector<int>& houses) {
    sRequire(RandomSkirmish::activateMod(mod), "Mod " + mod + " did not activate");
    const std::string path = sEnv("SAVE_PROBE_SOURCE_DIR") + "/data/maps/multiplayer/" + map + ".ini";
    GameInitSettings init(getBasename(path, true), readCompleteFile(path), false, effectiveGameOptions);
    int team = 1;
    for(const int house : houses) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(house), team++);
        const bool human = team == 2;
        info.addPlayerInfo(GameInitSettings::PlayerInfo(human ? settings.general.playerName : sHouse(house),
                                                        human ? HUMANPLAYERCLASS : "qBotMedium"));
        init.addHouseInfo(info);
    }
    init.randomSeed = 20261024;
    return init;
}

void makeSave(const std::string& mod, const std::string& map, const std::vector<int>& houses) {
    const int minutes = std::max(1, std::atoi(sEnv("SAVE_PROBE_MINUTES").c_str()));
    auto game = sStart(sMatch(mod, map, houses));
    sRun(*game, MILLI2CYCLES(minutes * 60000));
    sRequire(!game->finishedLevel, mod + ": the match ended before the save");
    const auto worms = sReport(*game, "made " + mod);
    sRequire(!worms.empty(), mod + ": no worm on the map at the save");
    const std::string path = sEnv("SAVE_PROBE_OUT") + "/" + sEnv("SAVE_PROBE_LABEL") + "-" + mod + ".dls";
    sRequire(game->saveGame(path), "Could not write " + path);
    SDL_Log("SAVE_PROBE: wrote %s", path.c_str());
    sEnd(game);
}

#else

/// Every worm belongs to the Natives house, which has no players.
void expectNativeWorms(Game& game, const std::map<int, int>& worms, const std::string& label) {
    for(const auto& [h, n] : worms) sRequire(h == HOUSE_NATIVES, label + ": " + std::to_string(n) + " worms owned by " + sHouse(h));
    if(!worms.empty()) sRequire(game.getHouse(HOUSE_NATIVES) && game.getHouse(HOUSE_NATIVES)->getPlayerList().empty(),
                                label + ": no player-less Natives house");
}

void loadSave() {
    const std::string path = sEnv("SAVE_PROBE_SAVE"), resaved = sEnv("SAVE_PROBE_OUT") + "/" + sEnv("SAVE_PROBE_LABEL") + "-resaved.dls";
    {
        // The load itself would open the Workshop download dialog for a revision this profile lacks.
        const auto data = readCompleteFile(path);
        IMemoryStream stream(data.data(), data.size());
        GameInitSettings::HouseInfoList houses;
        auto saved = GameInitSettings::readSaveSetup(stream, houses);
        SDL_Log("SAVE_PROBE: header: mod '%s', revision %s, saved by %s", saved.getModName().c_str(),
                saved.getModRevisionHash().c_str(), saved.getSavedGameVersion().c_str());
        try { WorkshopGameContent::resolveMod(saved, false); }
        catch(const std::exception& e) {
            sRequire(false, "mod revision " + saved.getModRevisionHash().substr(0, 16) + " of '" + saved.getModName()
                     + "' (saved by " + saved.getSavedGameVersion() + ") is unavailable here: " + e.what());
        }
    }
    auto game = sStart(GameInitSettings(path));
    const bool old = game->getLoadedSavegameVersion() < SAVEGAMEVERSION;
    expectNativeWorms(*game, sReport(*game, "loaded"), "loaded");
    sRun(*game, 600);
    sRequire(!game->finishedLevel || sEnv("SAVE_PROBE_ALLOW_END") == "1", "the game ended within 600 cycles");
    const auto worms = sReport(*game, "after 600 cycles");
    expectNativeWorms(*game, worms, "after 600 cycles");
    const size_t units = unitList.size(), structures = structureList.size();
    const Uint32 cycle = game->getGameCycleCount();
    sRequire(game->saveGame(resaved), "Could not re-save");
    sEnd(game);
    game = sStart(GameInitSettings(resaved));
    sRequire(game->getLoadedSavegameVersion() == SAVEGAMEVERSION, "the re-save is not the current savegame version");
    expectNativeWorms(*game, sReport(*game, "re-saved and loaded"), "re-saved and loaded");
    sRequire(unitList.size() == units && structureList.size() == structures && game->getGameCycleCount() == cycle,
             "the re-saved game differs from the game that was saved");
    sRun(*game, 60);
    sEnd(game);
    SDL_Log("SAVE_PROBE: round trip of a %s save: ok", old ? "previous-version" : "current-version");
}

#endif

}

int runSaveRoundtripProbe() {
    settings.general.playerName = "Save tester";
    settings.network.metaServer = "http://127.0.0.1:1/";
    const std::string kind = sEnv("SAVE_PROBE_CASE");
    try {
#ifdef SAVE_PROBE_MAKER
        if(kind == "make-save") {
            makeSave("vanilla", "4P - 128x64 - Gamma Sector", {HOUSE_ATREIDES, HOUSE_HARKONNEN, HOUSE_ORDOS, HOUSE_SARDAUKAR});
            makeSave("dunecity", "2P - 128x128 - 1v1 - Habbanya-Penny", {HOUSE_ATREIDES, HOUSE_HARKONNEN});
        }
#else
        if(kind == "load") loadSave();
#endif
        else sRequire(false, "Unknown case " + kind);
    } catch(const std::exception& e) {
        SDL_Log("SAVE_PROBE_FAIL: %s", e.what());
        return MENU_QUIT_DEFAULT;
    }
    SDL_Log("SAVE_PROBE_PASS: %s", kind.c_str());
    return MENU_QUIT_DEFAULT;
}
