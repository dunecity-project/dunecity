// Headless integration fixture for the Stage 1a result-preserving changes;
// linked with the production game objects. Covers the three things that are
// new state rather than removed work: the active-corpse tile registry, the A*
// scratch generation stamp (including its wrap), and the split build of the
// city placement distance fields.
#include <AStarSearch.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <chrono>
#include <sand.h>
#include <mod/ModManager.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <units/GroundUnit.h>
#include <fstream>
#include <limits>
#include <memory>
#include <set>

namespace {

int gChecks = 0;

void require(bool ok, const char* why) {
    if(!ok) {
        SDL_Log("ACTIVE_TILE_PROBE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
    ++gChecks;
}

// The property the registry has to keep: a tile holding a timer is always in
// the list, so the per-cycle sweep can never skip one. Checked by scanning the
// whole map, which is exactly the work the sweep no longer does.
void requireRegistryCoversMap(const char* when) {
    size_t withTimers = 0, registered = 0;
    for(int x = 0; x < currentGameMap->getSizeX(); ++x) {
        for(int y = 0; y < currentGameMap->getSizeY(); ++y) {
            const Tile* tile = currentGameMap->getTile(x,y);
            if(tile->hasDeadUnits()) {
                ++withTimers;
                require(tile->isDeadUnitRegistered(), when);
            }
            if(tile->isDeadUnitRegistered()) ++registered;
        }
    }
    require(registered == currentGameMap->getActiveDeadUnitTileCount(), when);
    require(withTimers <= registered, when);
}

} // namespace

int runActiveTileProbe() {
    require(ModManager::instance().setActiveMod("dunecity"), "Missing fixture mod");
    settings.general.playerName = "Active tile tester";
    std::ifstream map(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(map)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("active-tile.ini", mapData, "Active tiles", true, settings.gameOptions);
    init.randomSeed = 67;
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Active tile tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    game->initializeGameLoop();
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    require(owner && owner->isAlive(), "Fixture house missing");

    // ---------- 1. registry lifecycle ----------
    require(currentGameMap->getActiveDeadUnitTileCount() == 0, "Fresh map has no corpse tiles");
    requireRegistryCoversMap("Fresh map registry");

    const Coord corpses[] = {Coord(5,5), Coord(6,5), Coord(20,12)};
    for(const auto& at : corpses) {
        currentGameMap->getTile(at)->assignDeadUnit(DeadUnit_Infantry, HOUSE_ATREIDES, at*TILESIZE);
    }
    require(currentGameMap->getActiveDeadUnitTileCount() == 3, "Three corpse tiles registered");
    requireRegistryCoversMap("After assigning corpses");

    // A second corpse on an already registered tile must not duplicate it.
    currentGameMap->getTile(corpses[0])->assignDeadUnit(DeadUnit_Infantry, HOUSE_ATREIDES, corpses[0]*TILESIZE);
    require(currentGameMap->getActiveDeadUnitTileCount() == 3, "Repeat corpse does not duplicate the entry");

    // The timer is 2000 and update() decrements before erasing, so a corpse
    // survives exactly 2001 sweeps. Verify the boundary rather than assuming it.
    for(int cycle = 0; cycle < 2000; ++cycle) currentGameMap->updateActiveDeadUnitTiles();
    require(currentGameMap->getActiveDeadUnitTileCount() == 3, "Corpses still live one sweep before expiry");
    require(currentGameMap->getTile(corpses[2])->hasDeadUnits(), "Timer not yet expired");
    currentGameMap->updateActiveDeadUnitTiles();
    require(!currentGameMap->getTile(corpses[2])->hasDeadUnits(), "Timer expired on the expected sweep");
    require(currentGameMap->getActiveDeadUnitTileCount() == 0, "Expired tiles leave the registry");
    requireRegistryCoversMap("After expiry");

    // ---------- 2. stale entries from clearTerrain()/setRoad() ----------
    currentGameMap->getTile(corpses[0])->assignDeadUnit(DeadUnit_Infantry, HOUSE_ATREIDES, corpses[0]*TILESIZE);
    currentGameMap->getTile(corpses[1])->assignDeadUnit(DeadUnit_Infantry, HOUSE_ATREIDES, corpses[1]*TILESIZE);
    require(currentGameMap->getActiveDeadUnitTileCount() == 2, "Two corpse tiles for the clearing case");
    currentGameMap->getTile(corpses[0])->clearTerrain();
    currentGameMap->getTile(corpses[1])->setRoad(true);
    require(currentGameMap->getActiveDeadUnitTileCount() == 2, "Clearing leaves a stale entry until the next sweep");
    currentGameMap->updateActiveDeadUnitTiles();
    require(currentGameMap->getActiveDeadUnitTileCount() == 0, "The sweep drops stale entries");
    requireRegistryCoversMap("After clearTerrain and setRoad");
    currentGameMap->getTile(corpses[1])->setRoad(false);

    // ---------- 3. the real Map::save/load round trip ----------
    currentGameMap->getTile(corpses[2])->assignDeadUnit(DeadUnit_Carrall, HOUSE_ATREIDES, corpses[2]*TILESIZE);
    currentGameMap->getTile(corpses[0])->assignDeadUnit(DeadUnit_Ornithopter, HOUSE_ATREIDES, corpses[0]*TILESIZE);
    for(int cycle = 0; cycle < 7; ++cycle) currentGameMap->updateActiveDeadUnitTiles();
    OMemoryStream saved;
    saved.open();
    currentGameMap->save(saved);
    const std::string bytes(saved.getData(), saved.getDataLength());

    // Tile::load branches on the loaded savegame version, and a fixture game
    // that was never loaded from a file leaves it at 0 while Map::save writes
    // current-format fields. Declaring the current format for this round trip is
    // a fixture concern only; production version handling is untouched. Reaching
    // the member relies on the -fno-access-control this probe is compiled with.
    const Uint32 fixtureVersion = game->loadedSavegameVersion;
    game->loadedSavegameVersion = SAVEGAMEVERSION;
    {
        IMemoryStream restoring(bytes.data(), bytes.size());
        currentGameMap->load(restoring);
    }
    require(currentGameMap->getActiveDeadUnitTileCount() == 2, "Map::load rebuilds the registry from the saved tiles");
    requireRegistryCoversMap("After Map::load");

    // Re-saving the loaded map must reproduce the same bytes: the registry is
    // derived state and must stay out of the save.
    OMemoryStream again;
    again.open();
    currentGameMap->save(again);
    require(std::string(again.getData(), again.getDataLength()) == bytes, "Map save bytes unchanged by the registry");
    game->loadedSavegameVersion = fixtureVersion;

    // The restored timers must carry on from where they were saved, not restart
    // at a fresh 2000: 2000 - 7 already elapsed leaves 1994 sweeps.
    for(int cycle = 0; cycle < 1993; ++cycle) currentGameMap->updateActiveDeadUnitTiles();
    require(currentGameMap->getActiveDeadUnitTileCount() == 2, "Restored timers kept their remaining life");
    currentGameMap->updateActiveDeadUnitTiles();
    require(currentGameMap->getActiveDeadUnitTileCount() == 0, "Restored timers expire on the expected sweep");

    // Rebuilding a map with no timers clears every flag.
    currentGameMap->rebuildActiveDeadUnitTiles();
    require(currentGameMap->getActiveDeadUnitTileCount() == 0, "Rebuild on a clean map registers nothing");
    requireRegistryCoversMap("After clean rebuild");

    // ---------- 4. A* scratch generation stamp and its wrap ----------
    auto* unit = owner->createUnit(Unit_Tank, true);
    require(unit != nullptr, "Fixture unit missing");
    unit->deploy(Coord(10,10));
    const Coord start = unit->getLocation();
    const Coord destination(30,20);
    require(currentGameMap->tileExists(destination), "Fixture destination exists");

    std::list<Coord> expected;
    int expectedNodes = 0;
    {
        AStarSearch search(currentGameMap, unit, start, destination);
        expected = search.getFoundPath();
        expectedNodes = search.getNodesChecked();
    }
    require(!expected.empty(), "Fixture path is non-empty");

    // Ordinary reuse: the same search over a recycled buffer must agree.
    for(int repeat = 0; repeat < 3; ++repeat) {
        AStarSearch search(currentGameMap, unit, start, destination);
        require(search.getFoundPath() == expected, "Reused scratch returns the same route");
        require(search.getNodesChecked() == expectedNodes, "Reused scratch expands the same nodes");
    }

    // Force every pooled buffer to the last stamp so the next acquisition wraps
    // and takes the full-reset branch. The result must be unchanged.
    for(auto& entry : AStarSearch::tilePool) {
        if(entry.buffer != nullptr) entry.generation = std::numeric_limits<Uint32>::max();
    }
    {
        AStarSearch search(currentGameMap, unit, start, destination);
        require(search.getFoundPath() == expected, "Generation wrap returns the same route");
        require(search.getNodesChecked() == expectedNodes, "Generation wrap expands the same nodes");
    }
    // The search straight after a wrap reuses stamp-1 buffers; check it too.
    {
        AStarSearch search(currentGameMap, unit, start, destination);
        require(search.getFoundPath() == expected, "Search after a wrap returns the same route");
        require(search.getNodesChecked() == expectedNodes, "Search after a wrap expands the same nodes");
    }

    // A changed obstacle must still be seen through a recycled buffer: this is
    // the stale-scratch failure the stamp could have introduced.
    Tile* blocked = currentGameMap->getTile(start.x+1, start.y);
    const int originalTerrain = blocked->getType();
    blocked->setType(Terrain_Mountain);
    std::list<Coord> around;
    {
        AStarSearch search(currentGameMap, unit, start, destination);
        around = search.getFoundPath();
    }
    blocked->setType(originalTerrain);
    {
        AStarSearch search(currentGameMap, unit, start, destination);
        require(search.getFoundPath() == expected, "Restoring the obstacle restores the route");
    }
    require(around != expected, "The obstacle actually changed the route");

    // The placement distance-field cache is covered against the production
    // QuantBot::findPlaceLocation in tests/performance/ai-cache-probe.inc, which
    // needs a real city save; a synthetic field comparison here would only have
    // re-run the same algorithm twice.

    // ---------- 5. the sweep A/B, both forms in one process ----------
    // Form (a) is the per-cycle loop as it stood at da5a8f1f; form (b) is the
    // registry sweep that replaced it. Equal work, so the difference is the
    // traversal itself. Cost is linear in tiles, so scale by tile count to read
    // across to a larger map; this fixture map is deliberately small.
    {
        const int tiles = currentGameMap->getSizeX()*currentGameMap->getSizeY();
        const Coord living[] = {Coord(3,3), Coord(8,9), Coord(14,4), Coord(22,17),
                                Coord(31,8), Coord(40,21), Coord(45,12), Coord(18,25)};
        for(const auto& at : living) {
            require(currentGameMap->tileExists(at), "Benchmark corpse tile exists");
            currentGameMap->getTile(at)->assignDeadUnit(DeadUnit_Infantry, HOUSE_ATREIDES, at*TILESIZE);
        }
        const size_t active = currentGameMap->getActiveDeadUnitTileCount();
        require(active == 8, "Benchmark registered eight corpse tiles");

        using Clock = std::chrono::steady_clock;
        const int sweeps = 400;
        double fullMapMs = 0, registryMs = 0;
        for(int repeat = 0; repeat < 2; ++repeat) {
            const auto fullStart = Clock::now();
            for(int sweep = 0; sweep < sweeps; ++sweep)
                for(int y = 0; y < currentGameMap->getSizeY(); y++)
                    for(int x = 0; x < currentGameMap->getSizeX(); x++)
                        currentGameMap->getTile(x,y)->update();
            fullMapMs += std::chrono::duration<double,std::milli>(Clock::now()-fullStart).count();

            const auto registryStart = Clock::now();
            for(int sweep = 0; sweep < sweeps; ++sweep) currentGameMap->updateActiveDeadUnitTiles();
            registryMs += std::chrono::duration<double,std::milli>(Clock::now()-registryStart).count();
        }
        require(currentGameMap->getActiveDeadUnitTileCount() == active, "Benchmark corpses outlived the benchmark");
        SDL_Log("ACTIVE_TILE_PROBE_SWEEP: tiles=%d active=%zu sweeps=%d full_map_us=%.3f registry_us=%.3f",
            tiles, active, sweeps*2, fullMapMs*1000.0/(sweeps*2), registryMs*1000.0/(sweeps*2));
    }

    SDL_Log("ACTIVE_TILE_PROBE_PASS: checks=%d path_nodes=%d", gChecks, expectedNodes);
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    return MENU_QUIT_DEFAULT;
}
