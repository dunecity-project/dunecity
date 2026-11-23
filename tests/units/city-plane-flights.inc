// City airplane flights in a whole game: what planes do over real cities with real defences.
//
// One Custom Game per process on a bundled city map, started the way a player starts it: the
// Custom Game screen (CustomGameMenu) with the profile's saved mod and the shipped default
// options, its Next handler's GameInitSettings, the lobby (CustomGamePlayers) seating QuantBot
// Brutal, and the lobby's Start. This is Stefan's game of 2026-11-23 (4P - 192x192 -
// Metropolis, every house an AI and watched, 148 to 289 Rocket Turrets a house) and his
// two-player game (2P - 192x192 - City, the local human against one AI). The runner plays the
// same map and seed under different states of three match rules, set as the Game Rules window
// would set them: City Plane Carpet Bombing, City Plane Bombing Runs and City Plane Purchase
// (City Plane Defence and City Plane Bombing stay on, the Ornithopter patrol off).
//
// Recorded from the live objects, cycle by cycle:
//   flights.csv    one row per plane: when it flew, whether its centre crossed an enemy building
//                  and an enemy zone, the enemy building tiles that came under its bomb sight
//                  and how many of them got a bomb, bombs released, and what its bombs did.
//   purchases.csv  one row per plane an Airport sold: the buyer's credits afterwards.
//   houses.csv     every house at the end: credits, buildings, planes bought.
// The engine's own CITY_PLANE_* log lines (launch, purchase, shot down) are merged by the runner.
//
// The diagnostic main is compiled with -fno-access-control only to read bomb owners, bomb
// loads and Airport cooldowns. Nothing in the game is replaced or steered.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Bullet.h>
#include <mod/ModManager.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/PlaySetup.h>
#include <players/PlayerFactory.h>
#include <misc/FileSystem.h>
#include <sand.h>
#include <structures/Airport.h>
#include <structures/StructureBase.h>
#include <units/AmbientAirplane.h>
#include <dunecity/CityAircraftPolicy.h>

#include <cstdlib>
#include <fstream>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace CityPlaneFlights {

namespace CA = DuneCity::CityAircraft;

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("CITY_PLANE_FLIGHTS_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

std::string env(const char* name) {
    const char* value = std::getenv(name);
    require(value != nullptr, std::string("Missing ") + name);
    return value;
}

struct Flight {
    int house = -1;
    Uint32 first = 0, last = 0;
    bool crossedBuilding = false, crossedZone = false;
    int bombs = 0, onBuilding = 0, damage = 0, destroyed = 0, credits = 0;
    int lastHealth = 0;
    // The bomb sight: enemy building tiles that came under it, and those that got a bomb.
    int sightTiles = 0, bombedTiles = 0;
    Coord sightTile = Coord::Invalid();
    bool sightTileEnemy = false, sightTileBombed = false;
    Uint32 released = 0;
};

struct Falling {
    Uint32 plane, target, dropped;
    int item, before, price;
};

bool isZone(int item) {
    return item == Structure_ZoneResidential || item == Structure_ZoneCommercial || item == Structure_ZoneIndustrial;
}

} // namespace CityPlaneFlights

int runCityPlaneFlights() {
    using namespace CityPlaneFlights;
    const std::string out = env("CITY_PLANE_FLIGHTS_OUT");
    const std::string rules = env("CITY_PLANE_FLIGHTS_RULES");   // carpet, runs, purchase: e.g. "110"
    const int players = std::atoi(env("CITY_PLANE_FLIGHTS_PLAYERS").c_str());
    const Uint32 cycles = static_cast<Uint32>(std::atoi(env("CITY_PLANE_FLIGHTS_CYCLES").c_str()));
    require(rules.size() == 3 && players >= 2 && cycles > 0, "Bad city plane flights runner environment");

    const std::string seats = env("CITY_PLANE_FLIGHTS_SEATS");   // "human": the local player and AIs; "spectate": AIs only
    const std::string mapPath = env("CITY_PLANE_FLIGHTS_MAP");
    // playCustomGame() (src/Menu/PlaySetup.cpp), the main menu's Custom Game: the saved mod
    // (Custom Game Mod = dunecity) is activated and its effective options become the rules.
    CustomPlaySetup setup;
    setup.rules = initialCustomGameRules();
    require(ModManager::instance().getActiveModName() == "dunecity", "Custom Game did not activate the saved dunecity mod");
    setup.maps = {mapPath};
    setup.map = 0;
    setup.mods = ModManager::instance().listModChoices();
    for(size_t i = 0; i < setup.mods.size(); ++i)
        if(setup.mods[i].matchesSelectionName(ModManager::instance().getActiveModName())) setup.mod = static_cast<int>(i);
    setup.online = false;
    setup.sharedHouse = false;
    setup.spectate = seats == "spectate";
    {
        // The map and rules screen as the player opens it.
        CustomGameMenu browser(false, false, &setup);
        const auto& options = browser.currentGameOptions;
        require(options == setup.rules, "The Custom Game screen does not show the setup's rules");
        require(options.cityPlaneDefence && options.cityPlaneBombing && !options.airportOrnithopterPatrol
                    && options.cityPlaneCarpetBombing && options.cityPlaneBombingRuns && options.cityPlanePurchase,
                "The Custom Game screen does not offer the default city plane rules");
        require(options.startWithExploredMap && !options.fogOfWar, "The mod's options are not explored and fog-free");
    }
    // The Game Rules window's result for this arm.
    setup.rules.cityPlaneCarpetBombing = rules[0] == '1';
    setup.rules.cityPlaneBombingRuns = rules[1] == '1';
    setup.rules.cityPlanePurchase = rules[2] == '1';
    GameInitSettings init(getBasename(mapPath, true), readCompleteFile(mapPath), setup.multiplePlayersPerHouse(), setup.rules);
    {
        CustomGamePlayers lobby(init, true, false, &setup);
        lobby.update();
        require(lobby.numHouses == players, "The lobby does not seat the map's houses");
        require(lobby.spectating == setup.spectate, "The lobby did not take the Spectate choice");
        const int ai = PlayerFactory::getIndexByPlayerClass("qBotBrutal");
        require(ai >= 0, "Missing QuantBot Brutal");
        int humans = 0;
        for(int row = 0; row < lobby.numHouses; ++row) {
            if(lobby.getSeatPlayer(2 * row) == 0) { ++humans; continue; }   // PLAYER_HUMAN keeps its seat
            require(lobby.selectSeatPlayer(2 * row, ai), "The lobby cannot seat QuantBot Brutal");
            lobby.onChangePlayerDropDownBoxes(true, 2 * row);
        }
        require(humans == (setup.spectate ? 0 : 1), "The lobby did not seat exactly the local human");
        lobby.addAllPlayersToGameInitSettings();   // the Start button's last step before the game
        init = lobby.gameInitSettings;
        require(init.isLocalSpectator() == setup.spectate, "The started game lost the Spectate choice");
    }
    init.randomSeed = static_cast<Uint32>(std::atoi(env("CITY_PLANE_FLIGHTS_SEED").c_str()));
    // The settings travel through their stream as they do into a save or to a peer.
    {
        OMemoryStream saved;
        saved.open();
        init.save(saved);
        IMemoryStream stream(saved.getData(), saved.getDataLength());
        const GameInitSettings restored(stream);
        const auto& a = restored.getGameOptions();
        const auto& b = init.getGameOptions();
        require(a.cityPlaneDefence == b.cityPlaneDefence && a.cityPlaneBombing == b.cityPlaneBombing
                    && a.airportOrnithopterPatrol == b.airportOrnithopterPatrol
                    && a.cityPlaneCarpetBombing == b.cityPlaneCarpetBombing
                    && a.cityPlaneBombingRuns == b.cityPlaneBombingRuns && a.cityPlanePurchase == b.cityPlanePurchase,
                "The city plane rules did not survive the settings stream");
    }

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // Private fixture, never a public match-analytics sample.
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    const auto& played = game->getGameInitSettings().getGameOptions();
    require(played.cityPlaneCarpetBombing == (rules[0] == '1') && played.cityPlaneBombingRuns == (rules[1] == '1')
                && played.cityPlanePurchase == (rules[2] == '1') && played.cityPlaneBombing && played.cityPlaneDefence,
            "The game did not keep the city plane rules of its arm");
    require(game->isCitySimEnabled(), "The map is not a city game");
    for(int h = 0; h < NUM_HOUSES; ++h) {
        if(const House* house = game->getHouse(h)) {
            if(house->getNumStructures() == 0) continue;   // the Natives hold no city
            SDL_Log("CITY_PLANE_FLIGHTS_START: house=%d human=%d rocket_turrets=%d airports=%d buildings=%d credits=%d", h,
                    int(!house->isAI()), house->getNumItems(Structure_RocketTurret), house->getNumItems(Structure_Airport),
                    house->getNumStructures(), house->getCredits());
        }
    }

    std::map<Uint32, Flight> flights;
    std::map<const Bullet*, Falling> falling;
    std::map<Uint32, Sint32> cooldown;      // Airport -> purchase cooldown last cycle
    int bought[NUM_HOUSES] = {};
    int mostPlanes = 0;
    std::ofstream purchases(out + "/purchases.csv");
    purchases << "cycle,house,credits_after,planes\n";

    while(!game->finished && game->getGameCycleCount() < cycles) {
        game->updateGameState();
        const Uint32 cycle = game->getGameCycleCount();

        int planes[NUM_HOUSES] = {};
        for(const AmbientAirplane* plane : AmbientAirplane::all()) {
            if(plane->getHealth() <= 0) continue;
            const int house = plane->getOwner()->getHouseID();
            mostPlanes = std::max(mostPlanes, ++planes[house]);
            auto [it, fresh] = flights.try_emplace(plane->getObjectID());
            Flight& flight = it->second;
            if(fresh) { flight.house = house; flight.first = cycle; }
            flight.last = cycle;
            flight.lastHealth = plane->getHealth().lround();
            if(plane->takeoffFrame == 0) {
                // The tile under the bomb sight: closed and counted when the sight moves on.
                const Coord aim = plane->bombSight();
                const Coord tileAt = (aim.x < 0 || aim.y < 0) ? Coord::Invalid() : Coord(aim.x / TILESIZE, aim.y / TILESIZE);
                const bool bombedNow = plane->getBombsReleased() > flight.released;
                flight.released = plane->getBombsReleased();
                if(!(tileAt == flight.sightTile)) {
                    if(flight.sightTileEnemy) {
                        ++flight.sightTiles;
                        if(flight.sightTileBombed || bombedNow) ++flight.bombedTiles;
                    }
                    flight.sightTile = tileAt;
                    flight.sightTileBombed = false;
                    flight.sightTileEnemy = false;
                    if(tileAt.isValid() && currentGameMap->tileExists(tileAt)) {
                        const Tile* under = currentGameMap->getTile(tileAt);
                        flight.sightTileEnemy = under->hasAGroundObject() && under->getGroundObject() != nullptr
                            && under->getGroundObject()->isAStructure() && under->getGroundObject()->getHealth() > 0
                            && under->getGroundObject()->getOwner()->getTeamID() != plane->getOwner()->getTeamID();
                    }
                }
                if(bombedNow) flight.sightTileBombed = true;
            }
            const Coord at = plane->getCenterPoint();
            if(at.x < 0 || at.y < 0 || !currentGameMap->tileExists(at.x / TILESIZE, at.y / TILESIZE)) continue;
            const Tile* tile = currentGameMap->getTile(at.x / TILESIZE, at.y / TILESIZE);
            if(!tile->hasAGroundObject() || tile->getGroundObject() == nullptr || !tile->getGroundObject()->isAStructure()) continue;
            const ObjectBase* building = tile->getGroundObject();
            if(building->getOwner()->getTeamID() == plane->getOwner()->getTeamID()) continue;
            flight.crossedBuilding = true;
            if(isZone(building->getItemID())) flight.crossedZone = true;
        }

        std::map<const Bullet*, Falling> now;
        for(const Bullet* bullet : bulletList) {
            if(bullet->getBulletID() != Bullet_Bomb) continue;
            const auto known = falling.find(bullet);
            const ObjectBase* target = bullet->getTarget();
            if(known != falling.end()) {
                now[bullet] = known->second;
                if(target != nullptr) now[bullet].before = target->getHealth().lround();
                continue;
            }
            Falling bomb{bullet->shooterID, target ? target->getObjectID() : Uint32(NONE_ID), cycle,
                         target ? int(target->getItemID()) : -1, target ? int(target->getHealth().lround()) : 0,
                         target ? currentGame->objectData.data[target->getItemID()][target->getOriginalHouseID()].price : 0};
            now[bullet] = bomb;
            Flight& flight = flights[bomb.plane];
            ++flight.bombs;
            if(target != nullptr) ++flight.onBuilding;
        }
        for(const auto& [bullet, bomb] : falling) {
            if(now.count(bullet) || bomb.target == NONE_ID || bomb.before <= 0) continue;
            const ObjectBase* target = game->getObjectManager().getObject(bomb.target);
            const int after = target != nullptr ? std::max(0, int(target->getHealth().lround())) : 0;
            Flight& flight = flights[bomb.plane];
            flight.damage += std::max(0, bomb.before - after);
            if(after <= 0) { ++flight.destroyed; flight.credits += bomb.price; }
        }
        falling.swap(now);

        for(const StructureBase* structure : structureList) {
            if(structure->getItemID() != Structure_Airport) continue;
            const auto* airport = static_cast<const Airport*>(structure);
            Sint32& before = cooldown[airport->getObjectID()];
            if(airport->purchaseCooldown > before) {
                const House* house = airport->getOwner();
                ++bought[house->getHouseID()];
                purchases << cycle << ',' << house->getHouseID() << ',' << house->getCredits() << ','
                          << house->getNumItems(Unit_AmbientAirplane) << '\n';
            }
            before = airport->purchaseCooldown;
        }
    }

    std::ofstream flightsOut(out + "/flights.csv");
    flightsOut << "object,house,first,last,crossed_building,crossed_zone,sight_tiles,bombed_tiles,bombs,on_building,damage,destroyed,credits,last_health\n";
    for(const auto& [object, flight] : flights) {
        flightsOut << object << ',' << flight.house << ',' << flight.first << ',' << flight.last << ','
                   << flight.crossedBuilding << ',' << flight.crossedZone << ',' << flight.sightTiles << ','
                   << flight.bombedTiles << ',' << flight.bombs << ','
                   << flight.onBuilding << ',' << flight.damage << ',' << flight.destroyed << ',' << flight.credits << ','
                   << flight.lastHealth << '\n';
    }
    std::ofstream houses(out + "/houses.csv");
    houses << "house,credits,buildings,planes_bought\n";
    for(int h = 0; h < NUM_HOUSES; ++h) {
        if(const House* house = game->getHouse(h)) {
            houses << h << ',' << house->getCredits() << ',' << house->getNumStructures() << ',' << bought[h] << '\n';
        }
    }
    require(mostPlanes <= CA::kMaxPlanesPerHouse, "A house had more than three planes in the air");
    // A units probe owns its own marker, never the menu runner's result marker.
    SDL_Log("CITY_PLANE_FLIGHTS_PASS: cycles=%u flights=%zu most_planes=%d", game->getGameCycleCount(), flights.size(),
            mostPlanes);
    return -1;
}
