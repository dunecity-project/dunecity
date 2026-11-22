// Do Troopers fight Ornithopters better with the reach rule? Controlled engagements, the
// same encounter played with [Game Options] "Trooper Ornithopter Reach" off (the rule up to
// 1.0.876: a Trooper's own search only takes an Ornithopter over ground its team sees) and
// on, in one binary. Trooper Anti-Air is on in both arms, so the rocket tracks in both and
// the only difference is the reach.
//
// Each encounter is a raid: Ornithopters fly in from a given side and distance, each ordered
// onto a Trooper, and are sent onto the nearest surviving Trooper whenever they have no
// target. Troopers stand on Guard and are given no orders: what they shoot at is the
// engine's own target search. Shots are UnitBase::attack(), flight is AirUnit's, damage is
// Map::damage. The seed moves every start tile a little, the same way in both arms, so
// seeds are different encounters and not replays.
//
// The diagnostic main is compiled with -fno-access-control only to read targets and timers.
// No weapon, damage, range, speed or reload value is replaced.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <sand.h>
#include <units/Ornithopter.h>
#include <units/Trooper.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TrooperOrnithopterEffect {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("TROOPER_ORNITHOPTER_EFFECT_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

constexpr int kMapSize = 64;

std::string fixtureMap() {
    std::string data = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=8\n[MAP]\nSizeX="
                       + std::to_string(kMapSize) + "\nSizeY=" + std::to_string(kMapSize) + "\n";
    for(int row = 0; row < kMapSize; ++row) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", row);
        data += key + std::string(kMapSize, '%') + "\n";
    }
    // The non-combat MCVs keep both real houses alive, far from the encounter.
    data += "[Player1]\nCredits=2000\nBrain=Team1\n[Player2]\nCredits=2000\nBrain=Team2\n"
            "[UNITS]\nID001=Player1,MCV,256,0,64,Area Guard\nID002=Player2,MCV,256,"
            + std::to_string(kMapSize * kMapSize - 1) + ",64,Area Guard\n";
    return data;
}

enum Terrain {
    kUnexplored, // nobody has seen the ground: Troopers in the field
    kFogged,     // Start with Explored Map and Fog of War: ground is seen only next to own units
    kExplored,   // Start with Explored Map, no fog: the team sees all ground. The rule cannot matter.
};

const char* terrainName(int terrain) {
    return terrain == kUnexplored ? "unexplored" : terrain == kFogged ? "fogged" : "explored";
}

struct Spec {
    int terrain;
    int troopers;    // 1, or 3 per Troopers order: 3 = one squad, 6 = two squads.
    int ornithopters;
    int approach;    // 0 one side, 1 diagonal, 2 from different sides
    int distance;    // start distance in tiles
    int seed;
};

const char* approachName(int approach) {
    return approach == 0 ? "side" : approach == 1 ? "diagonal" : "split";
}

struct Outcome {
    int ornithoptersDown = 0;
    int troopersLost = 0;
    int firstKillTick = -1;
    int firstShotTick = -1;
    int firstShotDistance = -1; // world units (64 per tile) to the nearest Ornithopter
    int shots = 0;
    int ornithopterDamage = 0;
    int ticks = 0;
};

Outcome play(const std::string& mapData, const Spec& spec, bool reach) {
    auto options = settings.gameOptions;
    options.trooperAntiAir = true;
    options.trooperOrnithopterReach = reach;
    options.researchLab = false;
    options.fogOfWar = spec.terrain == kFogged;
    options.startWithExploredMap = spec.terrain != kUnexplored;
    options.immortalHumanPlayer = false;
    GameInitSettings init("trooper-ornithopter-effect.ini", mapData, "Trooper Ornithopter effect", true, options);
    init.randomSeed = spec.seed;
    GameInitSettings::HouseInfo defender(HOUSE_ATREIDES, 1);
    defender.addPlayerInfo({"AA tester", "HumanPlayer"});
    init.addHouseInfo(defender);
    GameInitSettings::HouseInfo attacker(HOUSE_HARKONNEN, 2);
    attacker.addPlayerInfo({"Raider", "HumanPlayer"});
    init.addHouseInfo(attacker);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // Private fixture, never a public match-analytics sample.
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    const auto& played = game->getGameInitSettings().getGameOptions();
    require(played.trooperAntiAir && played.trooperOrnithopterReach == reach,
            "The game did not keep the Trooper rules of its arm");
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    require(owner && enemy && owner->isAlive() && enemy->isAlive(), "Fixture houses missing");

    // The seed moves the start tiles, identically in both arms.
    std::mt19937 jitter(static_cast<std::uint32_t>(spec.seed) * 2654435761u + 97u);
    auto shift = [&](int span) { return static_cast<int>(jitter() % static_cast<std::uint32_t>(2 * span + 1)) - span; };
    const Coord site(kMapSize / 2 + shift(3), kMapSize / 2 + shift(3));
    require(currentGameMap->getTile(site + Coord(4, 0))->isExploredByTeam(owner->getTeamID())
                == (spec.terrain != kUnexplored),
            "The ground around the Troopers is not in the state the case names");

    std::vector<Uint32> troopers;
    std::vector<int> lastTimers;
    for(int i = 0; i < spec.troopers; ++i) {
        auto* trooper = dynamic_cast<Trooper*>(owner->createUnit(Unit_Trooper, true));
        require(trooper != nullptr, "Could not create a real Trooper");
        // A Troopers order deploys three Troopers; each squad stands on its own tile.
        trooper->deploy(site + Coord(i / 3, 0));
        require(trooper->getAttackMode() == GUARD, "A new Trooper does not start on Guard");
        troopers.push_back(trooper->getObjectID());
        lastTimers.push_back(0);
    }

    std::vector<Uint32> ornithopters;
    std::vector<int> startHP;
    for(int k = 0; k < spec.ornithopters; ++k) {
        const int d = spec.distance + shift(1);
        const int lane = k - (spec.ornithopters - 1) / 2 + shift(1);
        Coord start;
        if(spec.approach == 0) start = site + Coord(d, lane);
        else if(spec.approach == 1) start = site + Coord(-d + lane, -d - lane);
        else start = site + (k % 3 == 0 ? Coord(lane, -d) : k % 3 == 1 ? Coord(lane, d) : Coord(-d, lane));
        start.x = std::clamp(start.x, 1, kMapSize - 2);
        start.y = std::clamp(start.y, 1, kMapSize - 2);
        auto* aircraft = dynamic_cast<Ornithopter*>(enemy->createUnit(Unit_Ornithopter, true));
        require(aircraft != nullptr, "Could not create a real Ornithopter");
        aircraft->deploy(start);
        aircraft->doAttackObject(game->getObjectManager().getObject(troopers[k % troopers.size()]), true);
        ornithopters.push_back(aircraft->getObjectID());
        startHP.push_back(lround(aircraft->getHealth()));
    }

    Outcome outcome;
    const int limit = MILLI2CYCLES(90000);
    for(int tick = 0; tick < limit; ++tick) {
        game->updateGameState();
        outcome.ticks = tick + 1;

        int troopersAlive = 0;
        for(std::size_t i = 0; i < troopers.size(); ++i) {
            const auto* trooper = dynamic_cast<const Trooper*>(game->getObjectManager().getObject(troopers[i]));
            if(trooper == nullptr || trooper->getHealth() <= 0) continue;
            ++troopersAlive;
            if(trooper->primaryWeaponTimer > lastTimers[i]) {
                ++outcome.shots;
                if(outcome.firstShotTick < 0) {
                    outcome.firstShotTick = tick;
                    for(const Uint32 id : ornithopters) {
                        const auto* aircraft = game->getObjectManager().getObject(id);
                        if(aircraft == nullptr) continue;
                        const int far = lround(distanceFrom(trooper->getCenterPoint(), aircraft->getCenterPoint()));
                        if(outcome.firstShotDistance < 0 || far < outcome.firstShotDistance) outcome.firstShotDistance = far;
                    }
                }
            }
            lastTimers[i] = trooper->primaryWeaponTimer;
        }
        outcome.troopersLost = spec.troopers - troopersAlive;

        int ornithoptersAlive = 0;
        outcome.ornithopterDamage = 0;
        for(std::size_t k = 0; k < ornithopters.size(); ++k) {
            auto* aircraft = dynamic_cast<Ornithopter*>(game->getObjectManager().getObject(ornithopters[k]));
            if(aircraft == nullptr || aircraft->getHealth() <= 0) {
                outcome.ornithopterDamage += startHP[k];
                continue;
            }
            ++ornithoptersAlive;
            outcome.ornithopterDamage += startHP[k] - static_cast<int>(lround(aircraft->getHealth()));
            if(troopersAlive > 0 && aircraft->target.getObjPointer() == nullptr) {
                // The raid goes on: onto the nearest Trooper still standing.
                const ObjectBase* nearest = nullptr;
                FixPoint best = FixPt_MAX;
                for(const Uint32 id : troopers) {
                    const auto* trooper = game->getObjectManager().getObject(id);
                    if(trooper == nullptr || trooper->getHealth() <= 0) continue;
                    const FixPoint far = distanceFrom(aircraft->getCenterPoint(), trooper->getCenterPoint());
                    if(far < best) { best = far; nearest = trooper; }
                }
                if(nearest != nullptr) aircraft->doAttackObject(nearest, true);
            }
        }
        outcome.ornithoptersDown = spec.ornithopters - ornithoptersAlive;
        if(outcome.ornithoptersDown > 0 && outcome.firstKillTick < 0) outcome.firstKillTick = tick;
        if(ornithoptersAlive == 0 || troopersAlive == 0) break;
    }
    game.reset();
    currentGame = nullptr;
    return outcome;
}

std::vector<int> numbers(const char* text) {
    std::vector<int> values;
    std::istringstream input(text);
    for(std::string item; std::getline(input, item, ',');) values.push_back(std::stoi(item));
    return values;
}

} // namespace TrooperOrnithopterEffect

int runTrooperOrnithopterEffect() {
    using namespace TrooperOrnithopterEffect;
    const char* outEnv = std::getenv("TROOPER_ORNITHOPTER_EFFECT_OUT");
    const char* seedEnv = std::getenv("TROOPER_ORNITHOPTER_EFFECT_SEEDS");
    const char* fogSeedEnv = std::getenv("TROOPER_ORNITHOPTER_EFFECT_FOG_SEEDS");
    const char* exploredSeedEnv = std::getenv("TROOPER_ORNITHOPTER_EFFECT_EXPLORED_SEEDS");
    require(outEnv && seedEnv && fogSeedEnv && exploredSeedEnv, "Missing Trooper Ornithopter effect runner environment");
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    require(ModManager::instance().isDuneCityContentActive(), "The reach rule only exists in DuneCity content");
    settings.general.playerName = "AA tester";
    const std::string mapData = fixtureMap();
    std::ofstream csv(std::string(outEnv) + "/encounters.csv");
    require(csv.good(), "Could not open Trooper Ornithopter effect telemetry");
    csv << "terrain,troopers,ornithopters,approach,distance,seed,reach,ornithopters_down,troopers_lost,"
           "first_kill_tick,first_shot_tick,first_shot_distance,shots,ornithopter_damage,ticks\n";
    int encounters = 0;
    for(const int terrain : {kUnexplored, kFogged, kExplored})
    for(const int seed : numbers(terrain == kUnexplored ? seedEnv : terrain == kFogged ? fogSeedEnv : exploredSeedEnv))
    for(const int troopers : {1, 3, 6})
    for(const int ornithopters : {1, 2, 3})
    for(const int approach : {0, 1, 2})
    for(const int distance : {8, 14}) {
        const Spec spec{terrain, troopers, ornithopters, approach, distance, seed};
        for(const bool reach : {false, true}) {
            const auto outcome = play(mapData, spec, reach);
            csv << terrainName(terrain) << ',' << troopers << ',' << ornithopters << ','
                << approachName(approach) << ',' << distance << ',' << seed << ',' << reach << ','
                << outcome.ornithoptersDown << ',' << outcome.troopersLost << ',' << outcome.firstKillTick << ','
                << outcome.firstShotTick << ',' << outcome.firstShotDistance << ',' << outcome.shots << ',' << outcome.ornithopterDamage << ','
                << outcome.ticks << '\n';
            ++encounters;
        }
        csv.flush();
    }
    // A units probe owns its own marker, never the menu runner's result marker.
    SDL_Log("TROOPER_ORNITHOPTER_EFFECT_PASS: encounters=%d", encounters);
    return -1;
}
