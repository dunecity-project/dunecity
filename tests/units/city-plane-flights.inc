// City airplane flights in a whole game: what planes do over real cities with real defences.
//
// One Custom Game per process on a bundled city map, every house played by QuantBot Brutal and
// watched by a spectator, in the Dune City mod's own options. This is Stefan's game of
// 2026-11-23 (4P - 192x192 - Metropolis, 148 to 289 Rocket Turrets a house) and his earlier
// two-player game (2P - 192x192 - City). The runner plays the same map and seed under different
// states of three match rules: City Plane Carpet Bombing, City Plane Bombing Runs and City Plane
// Purchase (City Plane Defence and City Plane Bombing stay on, the Ornithopter patrol off).
//
// Recorded from the live objects, cycle by cycle:
//   flights.csv    one row per plane: when it flew, whether its centre crossed an enemy building
//                  and an enemy zone, bombs released, and what its bombs did.
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
#include <sand.h>
#include <structures/Airport.h>
#include <structures/StructureBase.h>
#include <units/AmbientAirplane.h>
#include <dunecity/CityAircraftPolicy.h>

#include <cstdlib>
#include <fstream>
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

    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    // What a Custom Game with the Dune City mod plays: the INI overlaid by the mod's GameOptions.ini.
    auto options = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    require(options.cityPlaneDefence && options.cityPlaneBombing && !options.airportOrnithopterPatrol
                && options.cityPlaneCarpetBombing && options.cityPlaneBombingRuns && options.cityPlanePurchase,
            "The mod's options do not play the default city plane rules");
    require(options.startWithExploredMap && !options.fogOfWar, "The mod's options are not explored and fog-free");
    options.cityPlaneCarpetBombing = rules[0] == '1';
    options.cityPlaneBombingRuns = rules[1] == '1';
    options.cityPlanePurchase = rules[2] == '1';

    const std::string mapPath = env("CITY_PLANE_FLIGHTS_MAP");
    std::ifstream mapFile(mapPath);
    std::stringstream mapData;
    mapData << mapFile.rdbuf();
    require(!mapData.str().empty(), "Missing map " + mapPath);
    GameInitSettings init(mapPath.substr(mapPath.find_last_of('/') + 1), mapData.str(), false, options);
    init.randomSeed = static_cast<Uint32>(std::atoi(env("CITY_PLANE_FLIGHTS_SEED").c_str()));
    init.setLocalSpectator(true);
    for(int i = 0; i < players; ++i) {
        // Random house, as the Custom Game screen's default; every house its own team.
        GameInitSettings::HouseInfo info(HOUSE_INVALID, i + 1);
        info.addPlayerInfo({"QuantBot Brutal", "qBotBrutal"});
        init.addHouseInfo(info);
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
            SDL_Log("CITY_PLANE_FLIGHTS_START: house=%d rocket_turrets=%d airports=%d buildings=%d credits=%d", h,
                    house->getNumItems(Structure_RocketTurret), house->getNumItems(Structure_Airport),
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
    flightsOut << "object,house,first,last,crossed_building,crossed_zone,bombs,on_building,damage,destroyed,credits,last_health\n";
    for(const auto& [object, flight] : flights) {
        flightsOut << object << ',' << flight.house << ',' << flight.first << ',' << flight.last << ','
                   << flight.crossedBuilding << ',' << flight.crossedZone << ',' << flight.bombs << ','
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
