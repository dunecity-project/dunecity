// The city airplane against Rocket Turrets, its bombs, and the Airport without Ornithopters:
// the same encounter played with each match rule off (the game up to 1.0.876) and on, in one
// binary, in the Dune City mod's own options (explored map, no fog, turrets need power).
//
//   passes  An enemy plane crosses a site defended by 1, 2, 4 or 6 Rocket Turrets, alone on open
//           ground or around a base, on three headings and five lateral offsets. City Plane
//           Defence off and on; bombing is off in both arms, so what the defender loses is the
//           crash. Per turret and cycle the probe records why the turret is not firing.
//   runs    A plane crosses an undefended enemy base on three headings and thirteen offsets.
//           Bombing off, on with one aimed bomb per building tile (the first rule), and on
//           with the stick (City Plane Carpet Bombing): bombs, buildings and credits per run.
//   checks  Fixed cases with exact expectations: what one run does to a Wind Trap, a
//           Construction Yard and a Palace, that own and allied buildings are never hit, the bomb
//           load and its return, the saved bomb load, and what an Airport launches.
//
// Planes fly AmbientAirplane's own update, turrets TurretBase's, rockets and bombs Bullet's and
// damage Map::damage. The diagnostic main is compiled with -fno-access-control only to read
// targets and timers and to put a plane on a chosen course. No weapon, damage, range, speed or
// reload value is replaced.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Bullet.h>
#include <mod/ModManager.h>
#include <sand.h>
#include <structures/Airport.h>
#include <ScreenBorder.h>
#include <misc/draw_util.h>
#include <FileClasses/LoadSavePNG.h>
#include <Command.h>
#include <players/Player.h>
#include <units/Tank.h>
#include <Explosion.h>
#include <players/DeviationReward.h>
#include <structures/RocketTurret.h>
#include <units/AmbientAirplane.h>
#include <dunecity/CityAircraftPolicy.h>

#include <set>
#include <map>
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

namespace CityPlaneEffect {

namespace CA = DuneCity::CityAircraft;

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("CITY_PLANE_EFFECT_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

constexpr int kMapSize = 64;
std::string gOut;
SettingsClass::GameOptionsClass gModOptions;

std::string fixtureMap() {
    std::string data = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=8\n[MAP]\nSizeX="
                       + std::to_string(kMapSize) + "\nSizeY=" + std::to_string(kMapSize) + "\n";
    for(int row = 0; row < kMapSize; ++row) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", row);
        data += key + std::string(kMapSize, '%') + "\n";
    }
    // The non-combat MCVs keep the houses alive, far from the encounter.
    data += "[Player1]\nCredits=2000\nBrain=Team1\n[Player2]\nCredits=2000\nBrain=Team2\n"
            "[Player3]\nCredits=2000\nBrain=Team2\n"
            "[UNITS]\nID001=Player1,MCV,256,0,64,Area Guard\nID002=Player2,MCV,256,"
            + std::to_string(kMapSize * kMapSize - 1) + ",64,Area Guard\nID003=Player3,MCV,256,"
            + std::to_string(kMapSize - 1) + ",64,Area Guard\n";
    return data;
}

struct Rules {
    bool defence;
    bool bombing;
    bool patrol;
    bool carpet = false;    // City Plane Carpet Bombing: the stick instead of one aimed bomb a tile
    bool purchase = false;  // City Plane Purchase
};

// Atreides defends; Harkonnen owns the plane; Ordos is Harkonnen's ally.
std::unique_ptr<Game> newGame(const std::string& mapData, int seed, const Rules& rules) {
    auto options = gModOptions;
    options.cityPlaneDefence = rules.defence;
    options.cityPlaneBombing = rules.bombing;
    options.airportOrnithopterPatrol = rules.patrol;
    options.cityPlaneCarpetBombing = rules.carpet;
    options.cityPlanePurchase = rules.purchase;
    // Every plane here flies a course the probe gives it, never a bombing run of its own choice.
    options.cityPlaneBombingRuns = false;
    options.immortalHumanPlayer = false;
    GameInitSettings init("city-plane-effect.ini", mapData, "City plane effect", true, options);
    init.randomSeed = seed;
    GameInitSettings::HouseInfo defender(HOUSE_ATREIDES, 1);
    defender.addPlayerInfo({"AA tester", "HumanPlayer"});
    init.addHouseInfo(defender);
    GameInitSettings::HouseInfo attacker(HOUSE_HARKONNEN, 2);
    attacker.addPlayerInfo({"Raider", "HumanPlayer"});
    init.addHouseInfo(attacker);
    GameInitSettings::HouseInfo ally(HOUSE_ORDOS, 2);
    ally.addPlayerInfo({"Raider ally", "HumanPlayer"});
    init.addHouseInfo(ally);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // Private fixture, never a public match-analytics sample.
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    const auto& played = game->getGameInitSettings().getGameOptions();
    require(played.cityPlaneDefence == rules.defence && played.cityPlaneBombing == rules.bombing
                && played.airportOrnithopterPatrol == rules.patrol,
            "The game did not keep the city plane rules of its arm");
    require(played.startWithExploredMap && !played.fogOfWar && played.rocketTurretsNeedPower,
            "The game does not play the Dune City mod's options");
    require(game->isCitySimEnabled(), "The fixture is not a city game");
    return game;
}

StructureBase* place(House* house, int item, Coord at) {
    auto* structure = house->placeStructure(NONE_ID, item, at.x, at.y, true, true);
    require(structure != nullptr, "Could not place " + getItemNameByID(item));
    structure->setVisible(VIS_ALL, true);
    return structure;
}

// A cruising plane at `start`, flying straight for `end` (which may lie outside the map).
AmbientAirplane* fly(House* house, Coord start, Coord end, int heading) {
    auto* plane = dynamic_cast<AmbientAirplane*>(house->createUnit(Unit_AmbientAirplane, true));
    require(plane != nullptr, "Could not create the city airplane");
    start.x = std::clamp(start.x, 1, kMapSize - 2);
    start.y = std::clamp(start.y, 1, kMapSize - 2);
    plane->deploy(start);
    plane->setVisible(VIS_ALL, true);
    plane->takeoffFrame = 0;
    plane->angle = FixPoint(heading);
    plane->showHeading();
    plane->setFlightDestination(end);
    return plane;
}

void course(int direction, const Coord& site, int offset, Coord& start, Coord& end, int& heading) {
    const int far = 26;
    if(direction == 0) { start = site + Coord(-far, offset); end = site + Coord(far + 24, offset); heading = RIGHT; }
    else if(direction == 1) { start = site + Coord(offset, -far); end = site + Coord(offset, far + 24); heading = DOWN; }
    else { start = site + Coord(-far + offset, -far - offset); end = site + Coord(far + 24 + offset, far + 24 - offset); heading = RIGHTDOWN; }
}

bool insideMap(const Coord& world) {
    return world.x > TILESIZE && world.y > TILESIZE && world.x < (kMapSize - 1) * TILESIZE
        && world.y < (kMapSize - 1) * TILESIZE;
}

// ---------------------------------------------------------------------------------------------
// passes: Rocket Turrets against a crossing plane
// ---------------------------------------------------------------------------------------------

struct PassSpec {
    int layout;   // 0 turrets alone on open ground, 1 turrets around a base
    int turrets;
    int direction;
    int offset;
    int seed;
};

struct PassOutcome {
    int inRange = 0, hold = 0, noTarget = 0, turning = 0, reloading = 0;
    int shots = 0, firstShotDistance = -1, planeDamage = 0, killed = 0, crashDistance = -1;
    int ownDamage = 0, ownDestroyed = 0, ticks = 0;
};

const FixPoint kFireTolerance = 15.0_fix / 45.0_fix; // TurretBase.cpp, internal linkage there

PassOutcome playPass(const std::string& mapData, const PassSpec& spec, bool defence) {
    auto game = newGame(mapData, spec.seed, Rules{defence, false, false});
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);

    // The seed moves the site and the turrets' first facing (TurretBase draws it), identically in both arms.
    std::mt19937 jitter(static_cast<std::uint32_t>(spec.seed) * 2654435761u + 97u);
    auto shift = [&](int span) { return static_cast<int>(jitter() % static_cast<std::uint32_t>(2 * span + 1)) - span; };
    const Coord site(kMapSize / 2 + shift(2), kMapSize / 2 + shift(2));

    std::vector<Uint32> structures;
    auto build = [&](int item, Coord at) {
        auto* structure = place(owner, item, at);
        structures.push_back(structure->getObjectID());
        return structure;
    };
    if(spec.layout == 0) {
        for(int i = 0; i < 3; ++i) build(Structure_WindTrap, Coord(2, 2 + 3 * i)); // power, far away
    } else {
        build(Structure_ConstructionYard, site + Coord(-1, -1));
        build(Structure_WindTrap, site + Coord(-4, -1));
        build(Structure_WindTrap, site + Coord(2, -1));
        build(Structure_WindTrap, site + Coord(-1, -4));
        build(Structure_Refinery, site + Coord(-1, 2));
        build(Structure_Radar, site + Coord(2, 2));
        build(Structure_LightFactory, site + Coord(-4, 2));
    }
    static const Coord perimeter[6] = {Coord(-6, -6), Coord(5, 5), Coord(5, -6), Coord(-6, 5), Coord(0, -7), Coord(0, 6)};
    static const Coord cluster[6] = {Coord(0, 0), Coord(2, 0), Coord(0, 2), Coord(2, 2), Coord(-2, 0), Coord(0, -2)};
    std::vector<Uint32> turrets;
    std::vector<int> lastTimers;
    for(int i = 0; i < spec.turrets; ++i) {
        auto* turret = dynamic_cast<RocketTurret*>(
            build(Structure_RocketTurret, site + (spec.layout == 0 ? cluster[i] : perimeter[i])));
        require(turret != nullptr, "Could not place a Rocket Turret");
        turrets.push_back(turret->getObjectID());
        lastTimers.push_back(0);
    }
    require(owner->hasPower(), "The defender is not powered");
    for(int i = 0; i < 5; ++i) game->updateGameState();
    int startHP = 0;
    for(const Uint32 id : structures) startHP += lround(game->getObjectManager().getObject(id)->getHealth());

    Coord start, end;
    int heading = 0;
    course(spec.direction, site, spec.offset, start, end, heading);
    auto* plane = fly(enemy, start, end, heading);
    const Uint32 planeID = plane->getObjectID();
    const int planeHP = lround(plane->getHealth());
    const int range = game->objectData.data[Structure_RocketTurret][HOUSE_ATREIDES].weaponrange;

    PassOutcome outcome;
    Coord last = plane->getCenterPoint();
    int lastHealth = planeHP;
    for(int tick = 0; tick < 1500; ++tick) {
        game->updateGameState();
        outcome.ticks = tick + 1;
        auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
        if(live == nullptr || live->getHealth() <= 0) {
            outcome.killed = (live != nullptr || insideMap(last)) ? 1 : 0;
            if(outcome.killed) {
                lastHealth = 0;
                outcome.crashDistance = lround(distanceFrom(last, Coord(site.x * TILESIZE + TILESIZE / 2,
                                                                         site.y * TILESIZE + TILESIZE / 2)));
            }
            break;
        }
        last = live->getCenterPoint();
        lastHealth = lround(live->getHealth());
        for(std::size_t i = 0; i < turrets.size(); ++i) {
            auto* turret = dynamic_cast<RocketTurret*>(game->getObjectManager().getObject(turrets[i]));
            if(turret == nullptr) continue;
            if(turret->weaponTimer > lastTimers[i]) {
                ++outcome.shots;
                if(outcome.firstShotDistance < 0)
                    outcome.firstShotDistance = lround(distanceFrom(turret->getCenterPoint(), live->getCenterPoint()));
            }
            lastTimers[i] = turret->weaponTimer;
            // The same yardstick in both arms: the turret's own weapon range in tiles.
            if(blockDistance(turret->getLocation(), live->getLocation()) > range) continue;
            ++outcome.inRange;
            if(!turret->canAttack(live)) { ++outcome.hold; continue; }
            if(turret->target.getObjPointer() != live) { ++outcome.noTarget; continue; }
            const int wanted = destinationDrawnAngle(turret->getLocation(), live->getLocation());
            FixPoint delta = FixPoint::abs(turret->angle - FixPoint(wanted));
            if(delta > 4) delta = FixPoint(8) - delta;
            if(delta > kFireTolerance) { ++outcome.turning; continue; }
            if(turret->weaponTimer > 0) ++outcome.reloading;
        }
    }
    outcome.planeDamage = planeHP - lastHealth;
    for(int i = 0; i < 80; ++i) game->updateGameState(); // the crash blast and rockets still in the air
    int endHP = 0;
    for(const Uint32 id : structures) {
        const auto* structure = game->getObjectManager().getObject(id);
        if(structure == nullptr || structure->getHealth() <= 0) { ++outcome.ownDestroyed; continue; }
        endHP += lround(structure->getHealth());
    }
    outcome.ownDamage = startHP - endHP;
    game.reset();
    currentGame = nullptr;
    return outcome;
}

int runPasses(const std::string& mapData, const std::vector<int>& seeds) {
    std::ofstream csv(gOut + "/passes.csv");
    csv << "layout,turrets,direction,offset,seed,defence,in_range,hold,no_target,turning,reloading,shots,"
           "first_shot_distance,plane_damage,killed,crash_distance,own_damage,own_destroyed,ticks\n";
    int passes = 0;
    for(const int layout : {0, 1})
    for(const int turrets : {1, 2, 4, 6})
    for(const int direction : {0, 1, 2})
    for(const int offset : {0, 2, 4, 6, 9})
    for(const int seed : seeds) {
        const PassSpec spec{layout, turrets, direction, offset, seed};
        for(const bool defence : {false, true}) {
            const auto o = playPass(mapData, spec, defence);
            csv << (layout == 0 ? "open" : "base") << ',' << turrets << ',' << direction << ',' << offset << ',' << seed
                << ',' << defence << ',' << o.inRange << ',' << o.hold << ',' << o.noTarget << ',' << o.turning << ','
                << o.reloading << ',' << o.shots << ',' << o.firstShotDistance << ',' << o.planeDamage << ','
                << o.killed << ',' << o.crashDistance << ',' << o.ownDamage << ',' << o.ownDestroyed << ',' << o.ticks << '\n';
            ++passes;
        }
        csv.flush();
    }
    return passes;
}

// ---------------------------------------------------------------------------------------------
// runs: a bombing run over an undefended base
// ---------------------------------------------------------------------------------------------

struct Building {
    int item;
    Coord at;
};

// A compact base of the mod's ordinary buildings, 14 tiles across.
const std::vector<Building>& baseLayout() {
    static const std::vector<Building> layout = {
        {Structure_ConstructionYard, Coord(-1, -1)}, {Structure_WindTrap, Coord(-4, -1)},
        {Structure_WindTrap, Coord(2, -1)},          {Structure_WindTrap, Coord(-1, -4)},
        {Structure_WindTrap, Coord(2, -4)},          {Structure_Refinery, Coord(-1, 2)},
        {Structure_Radar, Coord(2, 2)},              {Structure_LightFactory, Coord(-4, 2)},
        {Structure_Silo, Coord(-4, -4)},             {Structure_HeavyFactory, Coord(5, -1)},
        {Structure_Barracks, Coord(-7, -1)},         {Structure_RocketTurret, Coord(-7, -4)},
        {Structure_RocketTurret, Coord(5, 5)},       {Structure_GunTurret, Coord(-1, 5)},
        {Structure_ZoneResidential, Coord(-7, 2)},   {Structure_ZoneCommercial, Coord(-7, 5)},
        {Structure_ZoneIndustrial, Coord(-4, 5)},    {Structure_Palace, Coord(5, 2)},
        {Structure_Wall, Coord(1, -5)},              {Structure_Wall, Coord(1, -4)},
        {Structure_Wall, Coord(1, -3)},              {Structure_Wall, Coord(4, 1)},
    };
    return layout;
}

struct RunOutcome {
    int bombs = 0, damage = 0, destroyed = 0, credits = 0, damaged = 0, yardLost = 0, palaceLost = 0, ticks = 0;
    int planeHealth = 0;
    std::string lost;
};

// rule 0: no bombing; 1: one aimed bomb per building tile; 2: the stick.
RunOutcome playRun(const std::string& mapData, int direction, int offset, int seed, int rule) {
    auto game = newGame(mapData, seed, Rules{true, rule > 0, false, rule == 2});
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    std::mt19937 jitter(static_cast<std::uint32_t>(seed) * 2654435761u + 131u);
    auto shift = [&](int span) { return static_cast<int>(jitter() % static_cast<std::uint32_t>(2 * span + 1)) - span; };
    const Coord site(kMapSize / 2 + shift(2), kMapSize / 2 + shift(2));

    struct Tracked { Uint32 id; int item; int health; };
    std::vector<Tracked> buildings;
    for(const auto& building : baseLayout()) {
        auto* structure = place(owner, building.item, site + building.at);
        // The fixture watches bombs, not turrets: its turrets stand unarmed.
        if(auto* turret = dynamic_cast<TurretBase*>(structure)) turret->attackMode = STOP;
        buildings.push_back({structure->getObjectID(), building.item, static_cast<int>(lround(structure->getHealth()))});
    }
    for(int i = 0; i < 5; ++i) game->updateGameState();
    for(auto& building : buildings)
        building.health = lround(game->getObjectManager().getObject(building.id)->getHealth());

    Coord start, end;
    int heading = 0;
    course(direction, site, offset, start, end, heading);
    auto* plane = fly(enemy, start, end, heading);
    const Uint32 planeID = plane->getObjectID();
    require(plane->getBombs() == CA::kBombLoad, "A new plane does not carry a full load of aimed bombs");

    RunOutcome outcome;
    for(int tick = 0; tick < 1500; ++tick) {
        game->updateGameState();
        outcome.ticks = tick + 1;
        auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
        if(live == nullptr) break;
        outcome.bombs = static_cast<int>(live->getBombsReleased());
        outcome.planeHealth = lround(live->getHealth());
    }
    // No turret fires here: whatever the plane lost, it lost to its own bombs.
    require(outcome.planeHealth == 100, "A plane lost hit points on a run over an undefended base");
    for(int i = 0; i < 40; ++i) game->updateGameState();
    for(const auto& building : buildings) {
        const auto* structure = game->getObjectManager().getObject(building.id);
        const int health = (structure == nullptr || structure->getHealth() <= 0) ? 0 : lround(structure->getHealth());
        if(health >= building.health) continue;
        ++outcome.damaged;
        outcome.damage += building.health - health;
        if(health > 0) continue;
        ++outcome.destroyed;
        outcome.credits += game->objectData.data[building.item][HOUSE_ATREIDES].price;
        outcome.yardLost += building.item == Structure_ConstructionYard;
        outcome.palaceLost += building.item == Structure_Palace;
        outcome.lost += (outcome.lost.empty() ? "" : "+") + getItemNameByID(building.item);
    }
    game.reset();
    currentGame = nullptr;
    return outcome;
}

int runRuns(const std::string& mapData, const std::vector<int>& seeds) {
    std::ofstream csv(gOut + "/runs.csv");
    csv << "direction,offset,seed,bombing,bombs,damage,damaged,destroyed,credits,yard_lost,palace_lost,lost,ticks\n";
    int runs = 0;
    for(const int direction : {0, 1, 2})
    for(int offset = -6; offset <= 6; ++offset)
    for(const int seed : seeds) {
        for(const int bombing : {0, 1, 2}) {
            const auto o = playRun(mapData, direction, offset, seed, bombing);
            // The aimed bombs come from a rack of six; the stick has none.
            require(bombing == 2 || o.bombs <= CA::kBombLoad, "A run dropped more than one load of aimed bombs");
            require(bombing || (o.bombs == 0 && o.damage == 0), "A plane bombed with City Plane Bombing off");
            csv << direction << ',' << offset << ',' << seed << ',' << bombing << ',' << o.bombs << ',' << o.damage << ','
                << o.damaged << ',' << o.destroyed << ',' << o.credits << ',' << o.yardLost << ',' << o.palaceLost << ','
                << (o.lost.empty() ? "-" : o.lost) << ',' << o.ticks << '\n';
            ++runs;
        }
        csv.flush();
    }
    return runs;
}

// ---------------------------------------------------------------------------------------------
// checks: exact cases
// ---------------------------------------------------------------------------------------------

std::ofstream gChecks;
void record(const std::string& name, const std::string& detail) {
    gChecks << name << ',' << detail << '\n';
    gChecks.flush();
    SDL_Log("CITY_PLANE_EFFECT check %s: %s", name.c_str(), detail.c_str());
}

int healthOf(Game& game, Uint32 id) {
    const auto* object = game.getObjectManager().getObject(id);
    return (object == nullptr || object->getHealth() <= 0) ? 0 : lround(object->getHealth());
}

// One west-to-east run along `row` over whatever `setup` built; returns the bombs dropped.
template <typename Setup>
int flyRow(const std::string& mapData, int seed, bool bombing, Setup setup, std::vector<int>& health,
           bool carpet = false) {
    auto game = newGame(mapData, seed, Rules{true, bombing, false, carpet});
    std::vector<Uint32> ids;
    const int row = 30;
    setup(*game, row, ids);
    for(int i = 0; i < 5; ++i) game->updateGameState();
    auto* plane = fly(game->getHouse(HOUSE_HARKONNEN), Coord(4, row), Coord(kMapSize + 20, row), RIGHT);
    const Uint32 planeID = plane->getObjectID();
    int dropped = 0;
    for(int tick = 0; tick < 700; ++tick) {
        game->updateGameState();
        auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
        if(live == nullptr) break;
        dropped = static_cast<int>(live->getBombsReleased());
        require(lround(live->getHealth()) == 100, "A plane lost hit points to its own bombs");
    }
    for(int i = 0; i < 2 * CA::kBombFallCycles; ++i) game->updateGameState();
    health.clear();
    for(const Uint32 id : ids) health.push_back(healthOf(*game, id));
    game.reset();
    currentGame = nullptr;
    return dropped;
}

void checkBombTargets(const std::string& mapData) {
    std::vector<int> health;
    // A Wind Trap (200), then a Construction Yard (400), then a Palace (1000) under the course,
    // far enough apart that each gets a full load: the fixture hands the plane a new load in between.
    for(const bool bombing : {false, true}) {
        for(const int item : {Structure_WindTrap, Structure_ConstructionYard, Structure_Palace}) {
            const int dropped = flyRow(mapData, 3, bombing, [&](Game& game, int row, std::vector<Uint32>& ids) {
                ids.push_back(place(game.getHouse(HOUSE_ATREIDES), item, Coord(30, row))->getObjectID());
            }, health);
            const std::string name = getItemNameByID(item);
            record("one_run", name + " bombing=" + std::to_string(bombing) + " bombs=" + std::to_string(dropped)
                                  + " health_after=" + std::to_string(health[0]));
            if(!bombing) {
                require(dropped == 0, "A plane bombed a " + name + " with City Plane Bombing off");
            } else if(item == Structure_WindTrap) {
                require(dropped == 2 && health[0] == 0, "One run over a Wind Trap did not destroy it with two bombs");
            } else if(item == Structure_ConstructionYard) {
                require(dropped == 2 && health[0] == 200, "One run over a Construction Yard did not leave it at 200 of 400");
            } else {
                require(dropped == 3 && health[0] == 700, "One run over a Palace did not leave it at 700 of 1000");
            }
        }
    }

    // Own and allied buildings under the course are never aimed at; the enemy one beyond them is.
    const int dropped = flyRow(mapData, 4, true, [&](Game& game, int row, std::vector<Uint32>& ids) {
        ids.push_back(place(game.getHouse(HOUSE_HARKONNEN), Structure_WindTrap, Coord(20, row))->getObjectID());
        ids.push_back(place(game.getHouse(HOUSE_ORDOS), Structure_WindTrap, Coord(26, row))->getObjectID());
        ids.push_back(place(game.getHouse(HOUSE_ATREIDES), Structure_WindTrap, Coord(32, row))->getObjectID());
        // An own wall directly beside the enemy building: the nearest an own building can be.
        ids.push_back(place(game.getHouse(HOUSE_HARKONNEN), Structure_Wall, Coord(34, row))->getObjectID());
        ids.push_back(place(game.getHouse(HOUSE_ORDOS), Structure_Wall, Coord(31, row))->getObjectID());
    }, health);
    record("own_side", "bombs=" + std::to_string(dropped) + " own_trap=" + std::to_string(health[0]) + " allied_trap="
                           + std::to_string(health[1]) + " enemy_trap=" + std::to_string(health[2]) + " own_wall="
                           + std::to_string(health[3]) + " allied_wall=" + std::to_string(health[4]));
    require(health[0] == 200 && health[1] == 200 && health[3] == 50 && health[4] == 50,
            "A bomb damaged the plane's own or an allied building");
    require(dropped == 2 && health[2] == 0, "The enemy Wind Trap between own buildings was not destroyed");
}

void checkBombLoad(const std::string& mapData) {
    // Twelve enemy walls in a row: six bombs, six walls, and the other six stand.
    std::vector<int> health;
    const int dropped = flyRow(mapData, 5, true, [&](Game& game, int row, std::vector<Uint32>& ids) {
        for(int i = 0; i < 12; ++i)
            ids.push_back(place(game.getHouse(HOUSE_ATREIDES), Structure_Wall, Coord(24 + i, row))->getObjectID());
    }, health);
    const int standing = static_cast<int>(std::count_if(health.begin(), health.end(), [](int h) { return h > 0; }));
    record("load", "walls=12 bombs=" + std::to_string(dropped) + " standing=" + std::to_string(standing));
    require(dropped == CA::kBombLoad && standing == 12 - CA::kBombLoad, "A run did not stop at one bomb load");

    // The rack: one bomb back per kBombRearmTicks, never above a full load.
    int bombs = 0, timer = 0, ticks = 0;
    while(bombs < CA::kBombLoad) { bombs = CA::rearmBombs(bombs, timer); ++ticks; }
    require(ticks == CA::kBombLoad * CA::kBombRearmTicks, "The bomb rack does not refill at one bomb per rearm time");
    bombs = CA::rearmBombs(bombs, timer);
    require(bombs == CA::kBombLoad && timer == 0, "A full bomb rack kept counting");
    record("rearm", "ticks_per_bomb=" + std::to_string(CA::kBombRearmTicks) + " full_load_ticks=" + std::to_string(ticks));
}

void checkSavedBombLoad(const std::string& mapData, bool carpet) {
    auto game = newGame(mapData, 6, Rules{true, true, false, carpet, carpet});
    auto* plane = fly(game->getHouse(HOUSE_HARKONNEN), Coord(10, 20), Coord(kMapSize + 20, 20), RIGHT);
    plane->bombs = carpet ? 5 : 2;
    plane->bombTimer = carpet ? 17 : 77;
    plane->stickGap = 1;
    plane->stickPhase = carpet ? 11 : 3;   // the stick's tail bomb falls due seven cycles on
    plane->setBought(carpet);
    auto* sold = dynamic_cast<Airport*>(place(game->getHouse(HOUSE_ATREIDES), Structure_Airport, Coord(40, 40)));
    require(sold != nullptr, "Could not place the Airport for the save");
    sold->purchaseCooldown = 1234;
    const Uint32 airportID = sold->getObjectID();
    const Uint32 planeID = plane->getObjectID();
    for(int i = 0; i < 10; ++i) game->updateGameState();
    plane = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    require(plane != nullptr, "The plane left before the save");
    const int bombs = plane->bombs, timer = plane->bombTimer, gap = plane->stickGap, phase = plane->stickPhase;
    const int cooldown = sold->purchaseCooldown;
    require((!carpet || (gap == CA::kStickTailBombs + 1 && bombs == 5)) && cooldown == 1224, "The stick gap counter or the purchase cooldown did not run before the save");
    const std::string path = gOut + "/city-plane-effect.dls";
    require(game->saveGame(path), "Could not write the save");
    game.reset();
    currentGame = nullptr;

    game = std::make_unique<Game>();
    currentGame = game.get();
    game->initGame(GameInitSettings(path));
    game->initializeGameLoop();
    auto* loaded = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
    require(loaded != nullptr, "The saved plane did not come back");
    require(loaded->bombs == bombs && loaded->bombTimer == timer, "The plane lost its bomb load in the save");
    // The aimed bombs follow the sight tile, the stick its gap counter.
    require(carpet || loaded->bombSightTile.isValid(), "The plane lost its bomb sight tile in the save");
    require(loaded->stickGap == gap && loaded->stickPhase == phase, "The plane lost its stick counters in the save");
    require(loaded->isBought() == carpet, "The plane lost whether it was bought in the save");
    const auto* airport = dynamic_cast<Airport*>(game->getObjectManager().getObject(airportID));
    require(airport != nullptr && airport->getPurchaseCooldown() == cooldown, "The Airport lost its purchase cooldown in the save");
    require(AmbientAirplane::all().size() == 1 && AmbientAirplane::all().front() == loaded,
            "The loaded plane is not the one city airplane the game knows");
    const auto& options = game->getGameInitSettings().getGameOptions();
    require(options.cityPlaneDefence && options.cityPlaneBombing && !options.airportOrnithopterPatrol
                && options.cityPlaneCarpetBombing == carpet && !options.cityPlaneBombingRuns
                && options.cityPlanePurchase == carpet,
            "The save lost the city plane rules");
    record("save", std::string(carpet ? "stick " : "aimed ") + "bombs=" + std::to_string(bombs) + " timer=" + std::to_string(timer) + " stick_gap="
                       + std::to_string(gap) + " stick_phase=" + std::to_string(phase) + " purchase_cooldown=" + std::to_string(cooldown) + " restored=1");
    game.reset();
    currentGame = nullptr;
    require(AmbientAirplane::all().empty(), "A finished game left a city airplane registered");
}

void checkAirport(const std::string& mapData) {
    // Eleven game minutes: longer than the ten-minute patrol timer the Airport took from the Palace.
    const int cycles = MILLI2CYCLES(11 * 60 * 1000);
    for(const bool patrol : {false, true}) {
        auto game = newGame(mapData, 7, Rules{true, true, patrol});
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        for(int i = 0; i < 4; ++i) place(owner, Structure_WindTrap, Coord(2, 2 + 3 * i));
        auto* airport = dynamic_cast<Airport*>(place(owner, Structure_Airport, Coord(30, 30)));
        require(airport != nullptr && owner->hasPower(), "Could not place a powered Airport");
        require(airport->getMaxSpawnTimer() < cycles, "The fixture is shorter than the patrol timer");
        const int timerAtStart = airport->getSpawnTimer();
        int ornithopters = 0, planes = 0;
        std::vector<Uint32> seen;
        for(int cycle = 0; cycle < cycles; ++cycle) {
            game->updateGameState();
            ornithopters = std::max(ornithopters, owner->getNumItems(Unit_Ornithopter));
            for(const AmbientAirplane* plane : AmbientAirplane::all()) {
                if(plane->getOwner() != owner
                   || std::find(seen.begin(), seen.end(), plane->getObjectID()) != seen.end()) continue;
                seen.push_back(plane->getObjectID());
                ++planes;
            }
        }
        record("airport", "patrol=" + std::to_string(patrol) + " ornithopters=" + std::to_string(ornithopters)
                              + " city_planes_launched=" + std::to_string(planes) + " patrol_timer="
                              + std::to_string(timerAtStart) + "->" + std::to_string(airport->getSpawnTimer()));
        // One free plane every five minutes, the time a Palace takes to make a Saboteur: at 5:00
        // and at 10:00 in these eleven minutes.
        require(planes == 2, "The Airport did not launch its free plane at five and at ten minutes");
        if(patrol) {
            require(ornithopters == 2, "With the patrol rule on the Airport did not send out its pair of Ornithopters");
        } else {
            require(ornithopters == 0, "The Airport released an Ornithopter");
            require(airport->getSpawnTimer() == timerAtStart, "The patrol timer ran with the patrol rule off");
        }
        game.reset();
        currentGame = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// checks: the stick (City Plane Carpet Bombing)
// ---------------------------------------------------------------------------------------------

// Stefan, 2026-11-23: "when you build an airport you should get one". An Airport that stands when
// the game begins charges for five minutes first (checkAirport: launches at 5:00 and 10:00); one
// placed during the game has its free plane ready and launches it at its next five-second tick.
void checkAirportBuiltPlane(const std::string& mapData) {
    auto game = newGame(mapData, 19, Rules{true, true, false, true, true});
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    for(int i = 0; i < 3; ++i) place(owner, Structure_WindTrap, Coord(2, 2 + 3 * i));
    for(int i = 0; i < 10; ++i) game->updateGameState();
    auto* airport = dynamic_cast<Airport*>(place(owner, Structure_Airport, Coord(30, 30)));
    require(airport != nullptr && airport->getFreePlaneTimer() == 0, "An Airport built during the game has no free plane ready");
    int launchedAfter = -1;
    for(int cycle = 0; cycle < MILLI2CYCLES(10000) && launchedAfter < 0; ++cycle) {
        game->updateGameState();
        if(owner->getNumItems(Unit_AmbientAirplane) > 0) launchedAfter = cycle;
    }
    record("airport_built", "free plane launched " + std::to_string(launchedAfter) + " cycles after the Airport was placed; next in "
                                + std::to_string(airport->getFreePlaneTimer()) + " cycles");
    require(launchedAfter >= 0, "An Airport built during the game did not launch its free plane within ten seconds");
    require(airport->getFreePlaneTimer() > MILLI2CYCLES(4 * 60 * 1000), "The launch did not restart the five-minute clock");
    game.reset();
    currentGame = nullptr;
}

void checkStick(const std::string& mapData) {
    std::vector<int> health;
    // One run each over a Wind Trap (200), a Rocket Turret (200, one tile), a Construction Yard
    // (400), a Refinery (450, three tiles) and a Palace (1000, three tiles): 250 a bomb, one bomb
    // every two tiles, the first half a tile inside the building.
    for(const int item : {Structure_WindTrap, Structure_RocketTurret, Structure_ConstructionYard, Structure_Refinery,
                          Structure_Palace}) {
        const int dropped = flyRow(mapData, 3, true, [&](Game& game, int row, std::vector<Uint32>& ids) {
            auto* structure = place(game.getHouse(HOUSE_ATREIDES), item, Coord(30, row));
            if(auto* turret = dynamic_cast<TurretBase*>(structure)) turret->attackMode = STOP;
            ids.push_back(structure->getObjectID());
        }, health, true);
        const std::string name = getItemNameByID(item);
        record("stick_run", name + " bombs=" + std::to_string(dropped) + " health_after=" + std::to_string(health[0]));
        // One hit destroys the 200-point buildings; the Construction Yard (two tiles) takes one
        // hit, the Refinery and the Palace (three tiles) two.
        const int expected = item == Structure_ConstructionYard ? 150 : item == Structure_Palace ? 500 : 0;
        require(health[0] == expected, "One stick over a " + name + " did not leave it at " + std::to_string(expected));
    }

    // The stick runs on in step across a one-tile road between two buildings: one bomb each, one
    // on the road between them and one past the last.
    int dropped = flyRow(mapData, 3, true, [&](Game& game, int row, std::vector<Uint32>& ids) {
        ids.push_back(place(game.getHouse(HOUSE_ATREIDES), Structure_WindTrap, Coord(28, row))->getObjectID());
        ids.push_back(place(game.getHouse(HOUSE_ATREIDES), Structure_WindTrap, Coord(31, row))->getObjectID());
    }, health, true);
    record("stick_gap", "two Wind Traps one tile apart: bombs=" + std::to_string(dropped) + " health_after="
                            + std::to_string(health[0]) + " and " + std::to_string(health[1]));
    require(dropped == 4 && health[0] == 0 && health[1] == 0,
            "The stick did not run on over two Wind Traps a tile apart with four bombs");

    // Own and allied buildings: nothing is released over them, also not as the tail of a stick.
    dropped = flyRow(mapData, 4, true, [&](Game& game, int row, std::vector<Uint32>& ids) {
        ids.push_back(place(game.getHouse(HOUSE_HARKONNEN), Structure_WindTrap, Coord(20, row))->getObjectID());
        ids.push_back(place(game.getHouse(HOUSE_ORDOS), Structure_WindTrap, Coord(26, row))->getObjectID());
        ids.push_back(place(game.getHouse(HOUSE_ATREIDES), Structure_WindTrap, Coord(32, row))->getObjectID());
        ids.push_back(place(game.getHouse(HOUSE_HARKONNEN), Structure_Wall, Coord(34, row))->getObjectID());
        ids.push_back(place(game.getHouse(HOUSE_ORDOS), Structure_Wall, Coord(31, row))->getObjectID());
    }, health, true);
    record("stick_own_side", "bombs=" + std::to_string(dropped) + " own_trap=" + std::to_string(health[0]) + " allied_trap="
                                 + std::to_string(health[1]) + " enemy_trap=" + std::to_string(health[2]) + " own_wall="
                                 + std::to_string(health[3]) + " allied_wall=" + std::to_string(health[4]));
    require(health[0] == 200 && health[1] == 200 && health[3] == 50 && health[4] == 50,
            "A stick bomb damaged the plane's own or an allied building");
    require(dropped == 1 && health[2] == 0, "The enemy Wind Trap between own buildings did not get exactly its one bomb");

    // The rack does not empty: twenty enemy Wind Traps side by side (forty tiles) all fall, one
    // bomb each and the tail.
    dropped = flyRow(mapData, 5, true, [&](Game& game, int row, std::vector<Uint32>& ids) {
        for(int i = 0; i < 20; ++i)
            ids.push_back(place(game.getHouse(HOUSE_ATREIDES), Structure_WindTrap, Coord(14 + 2 * i, row))->getObjectID());
    }, health, true);
    const int standing = static_cast<int>(std::count_if(health.begin(), health.end(), [](int h) { return h > 0; }));
    record("stick_rack", "wind_traps=20 bombs=" + std::to_string(dropped) + " standing=" + std::to_string(standing));
    require(dropped == 20 + CA::kStickTailBombs && standing == 0, "The stick stopped before the end of twenty Wind Traps");

    // Units under the stick take the ordinary splash, whoever owns them: a tank on the road
    // between two enemy Wind Traps, once the defender's and once the plane's own.
    for(const int house : {HOUSE_ATREIDES, HOUSE_HARKONNEN}) {
        int tankHealth = 0, tankFull = 0;
        flyRow(mapData, 3, true, [&](Game& game, int row, std::vector<Uint32>& ids) {
            ids.push_back(place(game.getHouse(HOUSE_ATREIDES), Structure_WindTrap, Coord(28, row))->getObjectID());
            ids.push_back(place(game.getHouse(HOUSE_ATREIDES), Structure_WindTrap, Coord(31, row))->getObjectID());
            auto* tank = game.getHouse(house)->createUnit(Unit_Tank, true);
            require(tank != nullptr, "Could not create the tank under the stick");
            tank->deploy(Coord(30, row));
            tank->doSetAttackMode(STOP);
            tankFull = lround(tank->getHealth());
            ids.push_back(tank->getObjectID());
        }, health, true);
        tankHealth = health[2];
        record("stick_units", std::string(house == HOUSE_ATREIDES ? "enemy" : "own") + " tank on the road under the stick: health "
                                  + std::to_string(tankFull) + "->" + std::to_string(tankHealth));
        require(tankHealth < tankFull, "A tank on the road under the stick took no splash");
    }
}

// Stefan, 2026-11-23: "also get planes to drop bombs on enemy units as well". Three enemy tanks in
// a row in the open, no building near: the stick starts over them and its blast, a tile and a
// half against units, hurts every one. The same three tanks of the plane's own side get no bomb.
void checkBombsOnUnits(const std::string& mapData) {
    for(const int house : {HOUSE_ATREIDES, HOUSE_HARKONNEN}) {
        std::vector<int> health;
        const int dropped = flyRow(mapData, 7, true, [&](Game& game, int row, std::vector<Uint32>& ids) {
            for(int i = 0; i < 3; ++i) {
                auto* tank = game.getHouse(house)->createUnit(Unit_Tank, true);
                require(tank != nullptr, "Could not create a tank for the stick");
                tank->deploy(Coord(28 + 2 * i, row));
                tank->doSetAttackMode(STOP);
                ids.push_back(tank->getObjectID());
            }
        }, health, true);
        record("stick_on_units", std::string(house == HOUSE_ATREIDES ? "enemy" : "own") + " tanks in the open: bombs=" + std::to_string(dropped)
                                     + " health_after=" + std::to_string(health[0]) + "/" + std::to_string(health[1]) + "/"
                                     + std::to_string(health[2]));
        if(house == HOUSE_ATREIDES) {
            require(dropped >= 2, "The plane dropped no stick on enemy tanks in the open");
            require(health[0] < 200 && health[1] < 200 && health[2] < 200, "A tank under the stick was not damaged");
        } else {
            require(dropped == 0 && health[0] == 200 && health[1] == 200 && health[2] == 200,
                    "The plane bombed tanks of its own side");
        }
    }
    require(CA::stickUnitDamage(250, 0, TILESIZE) == 250 && CA::stickUnitDamage(250, TILESIZE / 2, TILESIZE) == 125
                && CA::stickUnitDamage(250, TILESIZE, TILESIZE) == 62 && CA::stickUnitDamage(250, TILESIZE * 3 / 2, TILESIZE) == 31,
            "The heavy bomb's damage to units by distance is not 250 / 125 / 62 / 31");
}

// Stefan's game of 2026-11-23: the building is repairing (every AI building below half health
// is, and a human's with auto-repair), so it gets a hit point back between two bombs. The first
// rule's 2 x 100 then leaves a 200-point Wind Trap standing on 1; the stick destroys it.
void checkRepairingBuilding(const std::string& mapData) {
    for(const bool carpet : {false, true}) {
        auto game = newGame(mapData, 9, Rules{true, true, false, carpet});
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        owner->setAutoRepairEnabled(true);
        const int row = 30;
        const Uint32 id = place(owner, Structure_WindTrap, Coord(30, row))->getObjectID();
        for(int i = 0; i < 5; ++i) game->updateGameState();
        fly(game->getHouse(HOUSE_HARKONNEN), Coord(4, row), Coord(kMapSize + 20, row), RIGHT);
        int lowest = healthOf(*game, id);
        for(int tick = 0; tick < 500 && lowest > 0; ++tick) {
            game->updateGameState();
            lowest = std::min(lowest, healthOf(*game, id));
        }
        record("repairing", std::string(carpet ? "stick" : "aimed bombs") + " on a repairing Wind Trap: lowest_health="
                                + std::to_string(lowest));
        if(carpet) require(lowest == 0, "The stick did not destroy a repairing Wind Trap");
        else require(lowest == 1, "Two aimed bombs did not leave a repairing Wind Trap on 1 hit point");
        game.reset();
        currentGame = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// checks: buying a plane (City Plane Purchase)
// ---------------------------------------------------------------------------------------------

void checkPurchase(const std::string& mapData) {
    for(const bool purchase : {false, true}) {
        auto game = newGame(mapData, 10, Rules{true, true, false, true, purchase});
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        for(int i = 0; i < 8; ++i) place(owner, Structure_WindTrap, Coord(2 + 3 * (i / 4), 2 + 3 * (i % 4)));
        std::vector<Airport*> airports;
        for(int i = 0; i < 4; ++i) {
            airports.push_back(dynamic_cast<Airport*>(place(owner, Structure_Airport, Coord(20, 10 + 6 * i))));
            require(airports.back() != nullptr, "Could not place an Airport");
        }
        owner->startingCredits += 10000;   // addCredits() stops at the house's storage capacity
        // No cycle is played before the purchases: the Airports' own roll could launch a free plane.
        require(owner->hasPower(), "The Airports are not powered");
        const int before = owner->getCredits();
        const Uint32 ownPlayer = owner->getPlayerList().front()->getPlayerID();
        const Uint32 otherPlayer = game->getHouse(HOUSE_HARKONNEN)->getPlayerList().front()->getPlayerID();
        const auto planes = [&] { return owner->getNumItems(Unit_AmbientAirplane); };
        if(!purchase) {
            require(!airports[0]->canBuyAirplane() && !airports[0]->buyAirplane(), "An Airport sold a plane with the rule off");
            Command(ownPlayer, CMD_AIRPORT_BUY_AIRPLANE, airports[0]->getObjectID()).executeCommand();
            require(planes() == 0 && owner->getCredits() == before, "The Buy command launched a plane with the rule off");
            record("purchase", "rule=0 planes=0 credits=" + std::to_string(before) + "->" + std::to_string(owner->getCredits()));
            game.reset();
            currentGame = nullptr;
            continue;
        }
        // Another house's player may not buy at this Airport.
        Command(otherPlayer, CMD_AIRPORT_BUY_AIRPLANE, airports[0]->getObjectID()).executeCommand();
        require(planes() == 0 && owner->getCredits() == before, "Another house bought a plane at this Airport");
        // The owner's Buy button.
        Command(ownPlayer, CMD_AIRPORT_BUY_AIRPLANE, airports[0]->getObjectID()).executeCommand();
        require(planes() == 1 && owner->getCredits() == before - CA::kPlanePrice, "The Buy command did not launch one plane for 1500");
        // QuantBot paces itself by this clock; a player is not held by it.
        require(airports[0]->getPurchaseCooldown() == MILLI2CYCLES(CA::kPurchaseCooldownMs),
                "A purchase did not start the Airport's purchase clock");
        // Stefan, 2026-11-23: "i should be able to buy more planes, up to 10". The same Airport sells
        // the next nine at once, and no Airport sells an eleventh.
        owner->startingCredits += 20000;
        const int rich = owner->getCredits();
        for(int i = 1; i < CA::kMaxPlanesPerHouse; ++i) {
            require(airports[0]->canBuyAirplane(), "An Airport made a player wait before plane " + std::to_string(i + 1));
            Command(ownPlayer, CMD_AIRPORT_BUY_AIRPLANE, airports[0]->getObjectID()).executeCommand();
            require(planes() == i + 1, "The Buy command did not launch plane " + std::to_string(i + 1));
        }
        const int afterTen = owner->getCredits();
        require(planes() == 10 && afterTen == rich - 9 * CA::kPlanePrice, "Ten planes did not cost 15000");
        for(Airport* airport : airports)
            require(!airport->canBuyAirplane() && !airport->buyAirplane(), "A house with ten planes in the air bought an eleventh");
        Command(ownPlayer, CMD_AIRPORT_BUY_AIRPLANE, airports[1]->getObjectID()).executeCommand();
        require(planes() == 10 && owner->getCredits() == afterTen, "The Buy command launched an eleventh plane");
        // One plane fewer in the air: a house that cannot pay is refused, one that can buys again.
        for(UnitBase* unit : unitList) {
            if(unit->getItemID() == Unit_AmbientAirplane && unit->getOwner() == owner) {
                static_cast<AmbientAirplane*>(unit)->leaveMap();
                break;
            }
        }
        for(int i = 0; i < 3 && planes() == 10; ++i) game->updateGameState();
        const int flying = planes();
        require(flying < 10, "A plane that left the map still counts as in the air");
        owner->takeCredits(owner->getCredits() - (CA::kPlanePrice - 100));
        require(owner->getCredits() < CA::kPlanePrice && !airports[2]->canBuyAirplane() && !airports[2]->buyAirplane()
                    && planes() == flying, "A house bought a plane it could not pay for");
        owner->startingCredits += CA::kPlanePrice;
        const int paid = owner->getCredits();
        require(airports[2]->buyAirplane() && planes() == flying + 1 && owner->getCredits() == paid - CA::kPlanePrice,
                "A house that could pay again could not buy a plane");
        record("purchase", "rule=1 price=" + std::to_string(CA::kPlanePrice) + " ten_planes_from_one_airport_at_once=1 nine_more_credits="
                               + std::to_string(rich) + "->" + std::to_string(afterTen) + " eleventh_refused=1 other_house_refused=1"
                               + " ai_pace_cycles=" + std::to_string(MILLI2CYCLES(CA::kPurchaseCooldownMs)) + " unaffordable_refused=1");
        game.reset();
        currentGame = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// checks: the fall, and a plane's own bombs
// ---------------------------------------------------------------------------------------------

// Stefan, 2026-11-23: "the planes own bombs shouldn't hurt it, and they should take a while to
// drop ... so that the plane has already passed the bombs".
void checkFall(const std::string& mapData) {
    for(const bool carpet : {false, true}) {
        auto game = newGame(mapData, 12, Rules{true, true, false, carpet});
        const int row = 30;
        auto* trap = place(game->getHouse(HOUSE_ATREIDES), Structure_WindTrap, Coord(30, row));
        const Uint32 trapID = trap->getObjectID();
        for(int i = 0; i < 5; ++i) game->updateGameState();
        auto* plane = fly(game->getHouse(HOUSE_HARKONNEN), Coord(4, row), Coord(kMapSize + 20, row), RIGHT);
        const Uint32 planeID = plane->getObjectID();
        int released = -1, landed = -1, planeAhead = 0;
        Coord impact = Coord::Invalid(), aimed = Coord::Invalid();
        for(int tick = 0; tick < 700 && landed < 0; ++tick) {
            game->updateGameState();
            auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
            require(live != nullptr && lround(live->getHealth()) == 100, "The plane was lost or hurt before its first bomb landed");
            if(released < 0 && live->getBombsReleased() > 0) {
                released = tick;
                for(const Bullet* bullet : bulletList)
                    if(bullet->getBulletID() == Bullet_Bomb) aimed = bullet->destination;
            }
            if(released >= 0 && !game->getExplosionList().empty()) {
                landed = tick;
                impact = game->getExplosionList().front()->getPosition();
                planeAhead = live->getCenterPoint().x - impact.x;
            }
        }
        require(released >= 0 && landed >= 0, "No bomb was released and landed");
        const int fall = landed - released;
        const auto* hit = currentGameMap->getTile(impact.x / TILESIZE, impact.y / TILESIZE)->getGroundObject();
        record("fall", std::string(carpet ? "stick" : "aimed") + " fall_cycles=" + std::to_string(fall) + " impact_tile="
                           + std::to_string(impact.x / TILESIZE) + "/" + std::to_string(impact.y / TILESIZE) + " on_target="
                           + std::to_string(hit != nullptr && hit->getObjectID() == trapID) + " plane_tiles_past_impact="
                           + std::to_string(planeAhead / TILESIZE));
        // Seen from outside the bullet: the release shows a cycle late and the bomb bursts the cycle it comes
        // within one step of its target.
        require(fall >= CA::kBombFallCycles - 3 && fall <= CA::kBombFallCycles + 1, "A bomb did not fall for about 75 cycles");
        require(hit != nullptr && hit->getObjectID() == trapID && impact.x / TILESIZE == aimed.x / TILESIZE
                    && impact.y / TILESIZE == aimed.y / TILESIZE,
                "A bomb did not land on the building tile it was released at");
        require(planeAhead >= 8 * TILESIZE, "The plane had not passed its bomb by eight tiles when it went off");
        game.reset();
        currentGame = nullptr;
    }

    // The rule, whatever the geometry: a bomb's blast with aircraft right at its centre. The
    // dropping plane, another plane of its house, an allied plane, an enemy plane and an enemy
    // Ornithopter all keep their health; a tank there (the control) does not, and the same blast
    // as an Ornithopter's rocket aimed at aircraft (the second control) does hurt a plane.
    auto game = newGame(mapData, 13, Rules{true, true, false, true});
    const Coord spot(30, 30);
    const Coord centre(spot.x * TILESIZE + TILESIZE / 2, spot.y * TILESIZE + TILESIZE / 2);
    auto* harkonnen = game->getHouse(HOUSE_HARKONNEN);
    auto* own = fly(harkonnen, spot, spot + Coord(20, 0), RIGHT);
    auto* second = fly(harkonnen, spot, spot + Coord(20, 0), RIGHT);
    auto* allied = fly(game->getHouse(HOUSE_ORDOS), spot, spot + Coord(20, 0), RIGHT);
    auto* enemy = fly(game->getHouse(HOUSE_ATREIDES), spot, spot + Coord(20, 0), RIGHT);
    auto* ornithopter = game->getHouse(HOUSE_ATREIDES)->createUnit(Unit_Ornithopter, true);
    auto* tank = game->getHouse(HOUSE_ATREIDES)->createUnit(Unit_Tank, true);
    require(ornithopter != nullptr && tank != nullptr, "Could not create the units under the bomb");
    ornithopter->deploy(spot);
    tank->deploy(spot);
    const std::vector<ObjectBase*> aircraft = {own, second, allied, enemy, ornithopter};
    std::vector<int> before;
    for(const ObjectBase* object : aircraft) before.push_back(lround(object->getHealth()));
    const int tankBefore = lround(tank->getHealth());
    for(const bool air : {false, true})
        currentGameMap->damage(own->getObjectID(), harkonnen, centre, Bullet_Bomb, CA::kStickDamage, TILESIZE / 2, air, true,
                               NONE_ID, DeviationReward::Provenance());
    for(size_t i = 0; i < aircraft.size(); ++i)
        require(lround(aircraft[i]->getHealth()) == before[i], "A bomb damaged an aircraft at the centre of its blast");
    require(lround(tank->getHealth()) < tankBefore, "A bomb did not damage a tank at the centre of its blast");
    std::string after;
    for(const ObjectBase* object : aircraft) after += (after.empty() ? "" : "/") + std::to_string(lround(object->getHealth()));
    const int tankAfter = lround(tank->getHealth());
    currentGameMap->damage(own->getObjectID(), harkonnen, centre, Bullet_SmallRocket, CA::kStickDamage, TILESIZE, true, true,
                           NONE_ID, DeviationReward::Provenance());
    require(lround(enemy->getHealth()) < before[3], "The control blast aimed at aircraft did not damage a plane");
    record("own_bombs", "two bomb blasts centred on five aircraft (own plane/second own/allied/enemy plane/enemy Ornithopter): "
                            "health after " + after + " unchanged; tank there " + std::to_string(tankBefore) + "->"
                            + std::to_string(tankAfter) + "; control rocket blast then takes the enemy plane to "
                            + std::to_string(lround(enemy->getHealth())));
    game.reset();
    currentGame = nullptr;
}

// Stefan, 2026-11-23: "missiles chasing a plane just keep going forever. there needs to be a
// distance cutoff". A turret rocket is 1.13 times as fast as the plane, so after a plane flying
// away it flew nine times the gap between them. Now it goes off once it is 3 x WeaponRange (24
// tiles) from where it was fired, and a turret takes no plane it cannot meet inside that.
void checkRocketChase(const std::string& mapData) {
    const int row = 30;
    const Coord launchTile(4, row);
    const Coord launch(launchTile.x * TILESIZE + TILESIZE / 2, row * TILESIZE + TILESIZE / 2);
    // One rocket from `launch` at a plane: how far from `launch` it got, in tenths of a tile.
    struct Flight { int farthest10; int planeHealth; int cycles; };
    const auto chase = [&](int rangeTiles, int planeX, int heading, int turnAfter) {
        auto game = newGame(mapData, 14, Rules{true, false, false});
        // The cut-off is 3 x the Rocket Turret's WeaponRange: the control arm sets a range no rocket reaches.
        for(int h = 0; h < NUM_HOUSES; ++h)
            game->objectData.data[Structure_RocketTurret][h].weaponrange = rangeTiles / CA::kRocketChaseRangeFactor;
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        for(int i = 0; i < 3; ++i) place(owner, Structure_WindTrap, Coord(2, 2 + 3 * i));
        auto* turret = place(owner, Structure_RocketTurret, launchTile);
        static_cast<TurretBase*>(turret)->attackMode = STOP;   // the one rocket is fired by hand
        for(int i = 0; i < 5; ++i) game->updateGameState();
        auto* plane = fly(game->getHouse(HOUSE_HARKONNEN), Coord(planeX, row),
                          heading == RIGHT ? Coord(kMapSize + 20, row) : Coord(-20, row), heading);
        const Uint32 planeID = plane->getObjectID();
        Coord from = launch, at = plane->getCenterPoint();
        bulletList.push_back(new Bullet(turret->getObjectID(), &from, &at, Bullet_TurretRocket, 30, true, plane));
        Flight flight{0, 100, 0};
        for(int tick = 0; tick < 900; ++tick) {
            if(tick == turnAfter) {
                auto* live = dynamic_cast<AmbientAirplane*>(game->getObjectManager().getObject(planeID));
                require(live != nullptr, "The plane was gone before it turned away");
                live->angle = FixPoint(static_cast<int>(RIGHT));
                live->showHeading();
                live->setFlightDestination(Coord(kMapSize + 20, row));
            }
            game->updateGameState();
            bool flying = false;
            for(const Bullet* bullet : bulletList) {
                if(bullet->getBulletID() != Bullet_TurretRocket) continue;
                flying = true;
                const Coord position(lround(bullet->realX), lround(bullet->realY));
                flight.farthest10 = std::max<int>(flight.farthest10, lround(distanceFrom(launch, position) * 10 / TILESIZE));
            }
            if(!flying) { flight.cycles = tick + 1; break; }
        }
        require(flight.cycles > 0, "A turret rocket was still flying after 900 cycles");
        flight.planeHealth = healthOf(*game, planeID);
        game.reset();
        currentGame = nullptr;
        return flight;
    };
    const int reach = 24;
    // A plane five tiles away and flying away: the meeting point is 45 tiles off.
    const Flight unlimited = chase(240, 9, RIGHT, -1);
    const Flight away = chase(reach, 9, RIGHT, -1);
    // A plane coming head-on from 26 tiles is met half-way; when it turns away after 20 cycles the rocket gives up.
    const Flight headOn = chase(reach, 30, LEFT, -1);
    const Flight turned = chase(reach, 30, LEFT, 20);
    record("rocket_chase", "plane flying away from 5 tiles: without a cut-off the rocket flew " + std::to_string(unlimited.farthest10)
                               + " tenths of a tile in " + std::to_string(unlimited.cycles) + " cycles (plane health "
                               + std::to_string(unlimited.planeHealth) + "); with the 24-tile cut-off " + std::to_string(away.farthest10)
                               + " in " + std::to_string(away.cycles) + " cycles; head-on from 26 tiles " + std::to_string(headOn.farthest10)
                               + " (plane health " + std::to_string(headOn.planeHealth) + "); plane turning away after 20 cycles "
                               + std::to_string(turned.farthest10) + " in " + std::to_string(turned.cycles) + " cycles");
    require(unlimited.farthest10 > 400, "The control rocket did not chase the plane beyond forty tiles");
    require(away.farthest10 <= reach * 10 + 6 && turned.farthest10 <= reach * 10 + 6,
            "A rocket chasing a plane flew farther than 24 tiles from its turret");
    require(headOn.planeHealth == 70 && headOn.farthest10 <= reach * 10, "A rocket no longer hits a plane coming head-on");

    // Stefan, 2026-11-23: "rockets fired at them seem to go right through them". A rocket fired at
    // one plane goes off on another enemy plane it touches on the way: two planes come head-on in
    // line, the rocket is fired at the far one and the near one takes it.
    {
        auto game = newGame(mapData, 17, Rules{true, false, false});
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        for(int i = 0; i < 3; ++i) place(owner, Structure_WindTrap, Coord(2, 2 + 3 * i));
        auto* turret = place(owner, Structure_RocketTurret, launchTile);
        static_cast<TurretBase*>(turret)->attackMode = STOP;
        for(int i = 0; i < 5; ++i) game->updateGameState();
        auto* harkonnen = game->getHouse(HOUSE_HARKONNEN);
        auto* nearPlane = fly(harkonnen, Coord(18, row), Coord(-20, row), LEFT);
        auto* farPlane = fly(harkonnen, Coord(34, row), Coord(-20, row), LEFT);
        const Uint32 nearID = nearPlane->getObjectID(), farID = farPlane->getObjectID();
        Coord from = launch, at = farPlane->getCenterPoint();
        bulletList.push_back(new Bullet(turret->getObjectID(), &from, &at, Bullet_TurretRocket, 30, true, farPlane));
        for(int tick = 0; tick < 300 && !bulletList.empty(); ++tick) game->updateGameState();
        record("rocket_chase", "rocket fired at the far of two planes in line: near plane health=" + std::to_string(healthOf(*game, nearID))
                                   + " far plane health=" + std::to_string(healthOf(*game, farID)));
        require(healthOf(*game, nearID) == 70 && healthOf(*game, farID) == 100,
                "A rocket passed through an enemy plane on the way to its target");
        game.reset();
        currentGame = nullptr;
    }
    // A Launcher's rocket flies to where it meets the plane and goes off on touching it; it used
    // to fly to where the plane had been and had no test against aircraft.
    {
        auto game = newGame(mapData, 18, Rules{true, false, false});
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        auto* launcher = owner->createUnit(Unit_Launcher, true);
        require(launcher != nullptr, "Could not create the Launcher");
        launcher->deploy(launchTile);
        launcher->doSetAttackMode(STOP);
        auto* plane = fly(game->getHouse(HOUSE_HARKONNEN), Coord(26, row), Coord(-20, row), LEFT);
        const Uint32 planeID = plane->getObjectID();
        for(int i = 0; i < 2; ++i) game->updateGameState();
        const bool takes = launcher->canAttack(plane);
        Coord from = launch, at = plane->getCenterPoint();
        const int damage = game->objectData.data[Unit_Launcher][HOUSE_ATREIDES].weapondamage;
        bulletList.push_back(new Bullet(launcher->getObjectID(), &from, &at, Bullet_Rocket, damage, true, plane));
        for(int tick = 0; tick < 300 && !bulletList.empty(); ++tick) game->updateGameState();
        record("rocket_chase", "Launcher may attack a plane coming over it=" + std::to_string(takes) + "; its rocket (" + std::to_string(damage)
                                   + ") leaves the plane at health=" + std::to_string(healthOf(*game, planeID)));
        require(takes, "A Launcher holds its fire at an enemy city airplane");
        require(healthOf(*game, planeID) == std::max(0, 100 - damage), "A Launcher's rocket did not hit the plane it was fired at");
        game.reset();
        currentGame = nullptr;
    }

    // The turret's side of the rule: it takes a plane it can meet inside 24 tiles and no other.
    auto game = newGame(mapData, 15, Rules{true, false, false});
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    for(int i = 0; i < 3; ++i) place(owner, Structure_WindTrap, Coord(2, 2 + 3 * i));
    auto* turret = dynamic_cast<RocketTurret*>(place(owner, Structure_RocketTurret, launchTile));
    auto* harkonnen = game->getHouse(HOUSE_HARKONNEN);
    turret->attackMode = STOP;
    auto* nearPlane = fly(harkonnen, Coord(6, row), Coord(kMapSize + 20, row), RIGHT);
    auto* farPlane = fly(harkonnen, Coord(10, row), Coord(kMapSize + 20, row), RIGHT);
    auto* comingPlane = fly(harkonnen, Coord(26, row), Coord(-20, row), LEFT);
    for(int i = 0; i < 2; ++i) game->updateGameState();   // a plane has a velocity once it has moved
    const bool near = turret->canAttack(nearPlane);
    const bool far = turret->canAttack(farPlane);
    const bool coming = turret->canAttack(comingPlane);
    record("rocket_chase", "turret takes a plane flying away from 2 tiles=" + std::to_string(near) + " from 6 tiles="
                               + std::to_string(far) + " coming head-on from 22 tiles=" + std::to_string(coming));
    require(near && !far && coming, "A turret takes a plane its rocket cannot meet inside 24 tiles, or refuses one it can");
    game.reset();
    currentGame = nullptr;
}

// Pictures for a person to look at (no assertion beyond "it rendered"): a plane crossing a row
// of zones and Wind Traps, drawn 14 cycles after its first bomb lands, with the aimed bombs
// (aimed.png) and with the stick (stick.png). Below the row stand a 4x4 Airport and a compact 3x3 one.
void checkPictures(const std::string& mapData) {
    for(const bool carpet : {false, true}) {
        auto game = newGame(mapData, 11, Rules{true, true, false, carpet});
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        const int row = 30;
        const int items[] = {Structure_ZoneResidential, Structure_WindTrap, Structure_ZoneCommercial, Structure_WindTrap,
                             Structure_ZoneIndustrial, Structure_ZoneResidential};
        for(int i = 0; i < 6; ++i) place(owner, items[i], Coord(22 + 3 * i, row));
        // Off the plane's course, for the eye: the 4x4 Airport and, beside it, a compact 3x3 one
        // as an old save or a crowded map has it (the same sprite cell scaled into three tiles).
        place(owner, Structure_Airport, Coord(23, row + 5));
        {
            Airport::CompactPlacement compact;
            place(owner, Structure_Airport, Coord(30, row + 5));
        }
        for(int i = 0; i < 5; ++i) game->updateGameState();
        fly(game->getHouse(HOUSE_HARKONNEN), Coord(4, row), Coord(kMapSize + 20, row), RIGHT);
        int sinceFirst = -1, explosions = 0;
        for(int tick = 0; tick < 400 && sinceFirst < 14; ++tick) {
            game->updateGameState();
            if(sinceFirst >= 0 || !game->getExplosionList().empty()) ++sinceFirst;
        }
        explosions = static_cast<int>(game->getExplosionList().size());
        screenborder->setNewScreenCenter(Coord(30 * TILESIZE, row * TILESIZE));
        SDL_SetRenderTarget(renderer, screenTexture);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        game->drawScreen();
        auto image = renderReadSurface(renderer);
        SDL_SetRenderTarget(renderer, nullptr);
        require(image != nullptr, "Could not read back the rendered bombing run");
        const std::string path = gOut + (carpet ? "/stick.png" : "/aimed.png");
        SavePNG(image.get(), path.c_str());
        record("picture", std::string(carpet ? "stick.png" : "aimed.png") + " explosions_on_screen=" + std::to_string(explosions));
        game.reset();
        currentGame = nullptr;
    }
}

// The off state is the game up to 1.0.876: a turret holds every shot at a plane over its own wall
// and sees it only inside weapon range. On, it fires the shots that cannot bring it down.
void checkHoldFire(const std::string& mapData) {
    for(const bool defence : {false, true}) {
        auto game = newGame(mapData, 8, Rules{defence, false, false});
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        for(int i = 0; i < 3; ++i) place(owner, Structure_WindTrap, Coord(2, 2 + 3 * i));
        const Coord at(30, 30);
        auto* turret = dynamic_cast<RocketTurret*>(place(owner, Structure_RocketTurret, at));
        place(owner, Structure_Wall, at + Coord(8, 0));
        auto* plane = fly(game->getHouse(HOUSE_HARKONNEN), at + Coord(6, 0), at + Coord(60, 0), RIGHT);
        plane->currentMaxSpeed = 0; // held in place over the wall's blast reach, as city_aircraft_probe does
        const bool full = turret->canAttack(plane);
        plane->setHealth(turret->rocketDamage());
        const bool last = turret->canAttack(plane);
        record("hold_fire", "defence=" + std::to_string(defence) + " full_health_shot=" + std::to_string(full)
                                + " killing_shot=" + std::to_string(last));
        // Stefan, 2026-11-23: "yes they can crash it in their own city". With the rule on nothing is
        // held; with it off (the game up to 1.0.876) every shot over the turret's own wall is.
        require(last == defence, defence ? "A turret still holds the shot that brings the plane down onto its own wall"
                                         : "With City Plane Defence off a turret fires the killing shot over its own wall");
        require(full == defence, defence ? "A turret holds a shot that cannot bring the plane down"
                                         : "With City Plane Defence off a turret fires at a plane over its own wall");
        game.reset();
        currentGame = nullptr;
    }
    // The meeting point: head-on at twenty tiles the two meet about half-way, within a quarter tile.
    const auto meet = CA::intercept(Coord(0, 0), Coord(20 * 256, 0), -655, 0, 737);
    require(meet.possible && std::abs(meet.point.x - 20 * 256 * 737 / (737 + 655)) <= 64 && meet.point.y == 0,
            "The meeting point of a head-on rocket is wrong");
    require(!CA::intercept(Coord(0, 0), Coord(256, 0), 737, 0, 737).possible, "A rocket catches a target as fast as itself");
    record("intercept", "head_on_20_tiles_x=" + std::to_string(meet.point.x) + " cycles16=" + std::to_string(meet.cycles16));
}

// Diagnosis, not a test (run-city-plane-effect.py --replay-save): load a saved game, play it on and
// report what the anti-air does about enemy city airplanes and how each plane ends. Written for
// Stefan's save of 2026-11-23, "rocket turrets are no longer attacking planes, neither are
// launchers. the planes seem to die when they turn from their own bombs".
void replaySave(const std::string& path, int cycles) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->initGame(GameInitSettings(path));
    game->initializeGameLoop();
    std::ofstream out(gOut + "/replay.csv");
    out << "cycle,planes,turret_pairs,turret_may_fire,turret_held_crash,turret_held_reach,turret_unpowered,turret_other,"
           "turrets_targeting_plane,launcher_pairs,launcher_may_fire,launcher_held_crash,launcher_other,rockets_at_planes\n";
    std::ofstream ends(gOut + "/replay-planes.csv");
    ends << "object,house,bought,first_cycle,last_cycle,last_x,last_y,last_health,edge_tiles\n";
    struct Seen { int house; bool bought; Uint32 first, last; Coord at; int health; };
    std::map<Uint32, Seen> seen;
    std::set<const Bullet*> known;
    long firedTurret = 0, firedOther = 0, pairs = 0, may = 0, heldCrash = 0, heldReach = 0, unpowered = 0, other = 0;
    long launcherPairs = 0, launcherMay = 0, launcherCrash = 0, launcherOther = 0;
    int ended = 0, endedAtEdge = 0, endedHurt = 0;
    const int mapW = currentGameMap->getSizeX(), mapH = currentGameMap->getSizeY();
    const auto standing = [] {
        int buildings = 0, turrets = 0;
        for(const StructureBase* structure : structureList) {
            if(structure->getHealth() <= 0) continue;
            ++buildings;
            if(structure->getItemID() == Structure_RocketTurret) ++turrets;
        }
        return std::pair<int, int>(buildings, turrets);
    };
    const auto before = standing();
    for(int tick = 0; tick < cycles && !game->finished; ++tick) {
        game->updateGameState();
        const Uint32 cycle = game->getGameCycleCount();
        std::set<Uint32> alive;
        for(const AmbientAirplane* plane : AmbientAirplane::all()) {
            if(plane->getHealth() <= 0) continue;
            alive.insert(plane->getObjectID());
            auto [it, fresh] = seen.try_emplace(plane->getObjectID(),
                Seen{plane->getOwner()->getHouseID(), plane->isBought(), cycle, cycle, Coord(), 0});
            it->second.last = cycle;
            it->second.at = Coord(plane->getCenterPoint().x / TILESIZE, plane->getCenterPoint().y / TILESIZE);
            it->second.health = lround(plane->getHealth());
        }
        for(auto it = seen.begin(); it != seen.end();) {
            if(alive.count(it->first)) { ++it; continue; }
            const Seen& s = it->second;
            const int edge = std::min({s.at.x, s.at.y, mapW - 1 - s.at.x, mapH - 1 - s.at.y});
            ends << it->first << ',' << s.house << ',' << s.bought << ',' << s.first << ',' << s.last << ',' << s.at.x << ','
                 << s.at.y << ',' << s.health << ',' << edge << '\n';
            ++ended;
            if(edge <= 3) ++endedAtEdge;
            if(s.health < 100) ++endedHurt;
            it = seen.erase(it);
        }
        std::set<const Bullet*> now;
        int inFlight = 0;
        for(const Bullet* bullet : bulletList) {
            const ObjectBase* target = bullet->getTarget();
            if(target == nullptr || target->getItemID() != Unit_AmbientAirplane) continue;
            now.insert(bullet);
            ++inFlight;
            if(!known.count(bullet)) (bullet->getBulletID() == Bullet_TurretRocket ? firedTurret : firedOther)++;
        }
        known.swap(now);
        if(tick % 25 != 0) continue;
        int p = 0, m = 0, hc = 0, hr = 0, up = 0, ot = 0, targeting = 0, lp = 0, lm = 0, lc = 0, lo = 0;
        for(const StructureBase* structure : structureList) {
            if(structure->getItemID() != Structure_RocketTurret || structure->getHealth() <= 0) continue;
            const auto* turret = static_cast<const RocketTurret*>(structure);
            const ObjectBase* aimed = turret->target.getObjPointer();
            if(aimed != nullptr && aimed->getItemID() == Unit_AmbientAirplane) ++targeting;
            const int reach = turret->getWeaponRange() * 3 * TILESIZE;
            for(const AmbientAirplane* plane : AmbientAirplane::all()) {
                if(plane->getHealth() <= 0 || plane->getOwner()->getTeamID() == turret->getOwner()->getTeamID()) continue;
                if(distanceFrom(turret->getCenterPoint(), plane->getCenterPoint()) > reach) continue;
                ++p;
                if(!turret->getOwner()->hasPower()) ++up;
                else if(turret->canAttack(plane)) ++m;
                else if(AmbientAirplane::crashEndangers(*turret, *plane)) ++hc;
                else if(!turret->canMeetCityPlane(plane)) ++hr;
                else ++ot;
            }
        }
        for(const UnitBase* unit : unitList) {
            if((unit->getItemID() != Unit_Launcher && unit->getItemID() != Unit_EliteLauncher) || unit->getHealth() <= 0) continue;
            const int range = unit->getWeaponRange() * TILESIZE;
            for(const AmbientAirplane* plane : AmbientAirplane::all()) {
                if(plane->getHealth() <= 0 || plane->getOwner()->getTeamID() == unit->getOwner()->getTeamID()) continue;
                if(distanceFrom(unit->getCenterPoint(), plane->getCenterPoint()) > range) continue;
                ++lp;
                if(unit->canAttack(plane)) ++lm;
                else if(AmbientAirplane::crashEndangers(*unit, *plane)) ++lc;
                else ++lo;
            }
        }
        out << cycle << ',' << alive.size() << ',' << p << ',' << m << ',' << hc << ',' << hr << ',' << up << ',' << ot << ','
            << targeting << ',' << lp << ',' << lm << ',' << lc << ',' << lo << ',' << inFlight << '\n';
        pairs += p; may += m; heldCrash += hc; heldReach += hr; unpowered += up; other += ot;
        launcherPairs += lp; launcherMay += lm; launcherCrash += lc; launcherOther += lo;
    }
    SDL_Log("CITY_PLANE_REPLAY: cycles=%d turret_plane_pairs_in_reach=%ld may_fire=%ld held_for_crash=%ld held_out_of_rocket_reach=%ld "
            "unpowered=%ld other=%ld rockets_fired_by_turrets=%ld rockets_fired_by_others=%ld",
            cycles, pairs, may, heldCrash, heldReach, unpowered, other, firedTurret, firedOther);
    SDL_Log("CITY_PLANE_REPLAY: launcher_plane_pairs_in_range=%ld may_fire=%ld held_for_crash=%ld other=%ld", launcherPairs,
            launcherMay, launcherCrash, launcherOther);
    SDL_Log("CITY_PLANE_REPLAY: planes_ended=%d within_three_tiles_of_the_edge=%d ended_below_full_health=%d still_flying=%zu",
            ended, endedAtEdge, endedHurt, seen.size());
    const auto after = standing();
    SDL_Log("CITY_PLANE_REPLAY: buildings %d->%d rocket_turrets %d->%d (all houses, every cause, new ones included)",
            before.first, after.first, before.second, after.second);
    game.reset();
    currentGame = nullptr;
}

std::vector<int> numbers(const char* text) {
    std::vector<int> values;
    std::istringstream input(text);
    for(std::string item; std::getline(input, item, ',');) values.push_back(std::stoi(item));
    return values;
}

} // namespace CityPlaneEffect

int runCityPlaneEffect() {
    using namespace CityPlaneEffect;
    const char* outEnv = std::getenv("CITY_PLANE_EFFECT_OUT");
    const char* seedEnv = std::getenv("CITY_PLANE_EFFECT_SEEDS");
    const char* runSeedEnv = std::getenv("CITY_PLANE_EFFECT_RUN_SEEDS");
    require(outEnv && seedEnv && runSeedEnv, "Missing city plane effect runner environment");
    gOut = outEnv;
    if(const char* replay = std::getenv("CITY_PLANE_EFFECT_REPLAY")) {
        const char* cycles = std::getenv("CITY_PLANE_EFFECT_REPLAY_CYCLES");
        replaySave(replay, cycles ? std::atoi(cycles) : 3750);
        return -1;
    }
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    settings.general.playerName = "AA tester";
    // What a Custom Game with the Dune City mod plays: the INI overlaid by the mod's GameOptions.ini.
    gModOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    require(gModOptions.cityPlaneDefence && gModOptions.cityPlaneBombing && !gModOptions.airportOrnithopterPatrol
                && gModOptions.cityPlaneCarpetBombing && gModOptions.cityPlaneBombingRuns && gModOptions.cityPlanePurchase,
            "The mod's options do not play the default city plane rules");
    const std::string mapData = fixtureMap();

    gChecks.open(gOut + "/checks.csv");
    gChecks << "check,detail\n";
    checkBombTargets(mapData);
    checkBombLoad(mapData);
    checkSavedBombLoad(mapData, false);
    checkSavedBombLoad(mapData, true);
    checkAirport(mapData);
    checkAirportBuiltPlane(mapData);
    checkHoldFire(mapData);
    checkRocketChase(mapData);
    checkStick(mapData);
    checkRepairingBuilding(mapData);
    checkBombsOnUnits(mapData);
    checkPurchase(mapData);
    checkPictures(mapData);
    checkFall(mapData);
    gChecks.close();

    const int runs = runRuns(mapData, numbers(runSeedEnv));
    const int passes = runPasses(mapData, numbers(seedEnv));
    // A units probe owns its own marker, never the menu runner's result marker.
    SDL_Log("CITY_PLANE_EFFECT_PASS: passes=%d runs=%d", passes, runs);
    return -1;
}
