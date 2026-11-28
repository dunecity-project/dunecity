// What do Troopers do against Ornithopters in a player's own settings? The Dune City mod's
// effective game options (the map starts explored, no Fog of War, so every team sees all
// ground), every stance a player or QuantBot leaves a Trooper in, and three kinds of
// Ornithopter behaviour: attacking the Troopers, attacking a Harvester three tiles away, and
// flying past two tiles off. A Launcher and a Rocket Turret stand in the same spot as
// yardsticks. Every case is played with [Game Options] "Trooper Ornithopter Reach" off (the
// rules up to 1.0.876) and on. The runner tabulates and checks the result.
//
// The diagnostic main is compiled with -fno-access-control only to read targets, timers and
// projectile fields. No weapon, damage, range, speed or reload value is replaced.
#include <Bullet.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <sand.h>
#include <structures/RocketTurret.h>
#include <units/Launcher.h>
#include <units/Ornithopter.h>
#include <units/Trooper.h>
#include <units/UnitOrderQueue.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TrooperStanceProbe {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("TROOPER_STANCE_PROBE_FAIL: %s", why.c_str());
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
    data += "[Player1]\nCredits=2000\nBrain=Team1\n[Player2]\nCredits=2000\nBrain=Team2\n"
            "[UNITS]\nID001=Player1,MCV,256,0,64,Area Guard\nID002=Player2,MCV,256,"
            + std::to_string(kMapSize * kMapSize - 1) + ",64,Area Guard\n";
    return data;
}

enum Shooter { kTrooper, kTroopers, kLauncher, kRocketTurret };
enum Stance { kGuard, kAreaGuard, kAmbush, kHunt, kMoving, kAttackMove };
enum Raid { kAttacksThem, kAttacksNearby, kFliesPast };

const char* shooterName(int v) { return v == kTrooper ? "trooper" : v == kTroopers ? "troopers3" : v == kLauncher ? "launcher" : "rocket_turret"; }
const char* stanceName(int v) { return v == kGuard ? "guard" : v == kAreaGuard ? "area_guard" : v == kAmbush ? "ambush" : v == kHunt ? "hunt" : v == kMoving ? "moving" : "attack_move"; }
const char* raidName(int v) { return v == kAttacksThem ? "attacks_them" : v == kAttacksNearby ? "attacks_nearby" : "flies_past"; }

struct Row {
    int picked = 0;             // a shooter ever held the Ornithopter as its target
    int firstPickTick = -1;
    int firstPickDistance = -1; // world units between centres
    int firstShotTick = -1;
    int firstShotDistance = -1;
    int shots = 0;
    int gunShots = 0;           // Bullet_ShellSmall fired by a Trooper while the Ornithopter was its target
    int otherBullets = 0;       // anything that is neither a rocket nor the gun
    int damageEvents = 0;
    int damage = 0;
    int killed = 0;
    int killTick = -1;
    int shootersLost = 0;
    int inRangeTicks = 0;       // Ornithopter alive and inside the weapon range of a live shooter
    // Shooter-ticks while the Ornithopter was in that shooter's range:
    int noTarget = 0, otherTarget = 0, reloading = 0, turning = 0, moving = 0, ready = 0;
    int ticks = 0;
    int weaponRange = 0;
    int reload = 0;
};

Row play(const std::string& mapData, int shooterKind, int stance, int raid, int seed, bool reach) {
    auto options = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    options.garrisoningAndCapturing = false;
    options.immortalHumanPlayer = false;
    require(options.startWithExploredMap && !options.fogOfWar && options.trooperAntiAir
            && options.trooperOrnithopterReach, "These are not the Dune City mod's effective options");
    options.trooperOrnithopterReach = reach;
    GameInitSettings init("trooper-stance.ini", mapData, "Trooper stance", true, options);
    init.randomSeed = seed;
    GameInitSettings::HouseInfo defender(HOUSE_ATREIDES, 1);
    defender.addPlayerInfo({"AA tester", "HumanPlayer"});
    init.addHouseInfo(defender);
    GameInitSettings::HouseInfo attacker(HOUSE_ORDOS, 2);
    attacker.addPlayerInfo({"Raider", "HumanPlayer"});
    init.addHouseInfo(attacker);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_ORDOS);
    require(owner && enemy, "Fixture houses missing");

    std::mt19937 jitter(static_cast<std::uint32_t>(seed) * 2654435761u + 11u);
    auto shift = [&](int span) { return static_cast<int>(jitter() % static_cast<std::uint32_t>(2 * span + 1)) - span; };
    const bool walks = stance == kMoving || stance == kAttackMove;
    const Coord site = walks ? Coord(8, kMapSize / 2) : Coord(kMapSize / 2, kMapSize / 2);
    require(currentGameMap->getTile(site + Coord(5, 0))->isExploredByTeam(owner->getTeamID()), "The map is not explored");

    Row row;
    std::vector<Uint32> shooters;
    std::vector<int> lastTimers;
    const int item = shooterKind == kLauncher ? Unit_Launcher : shooterKind == kRocketTurret ? Structure_RocketTurret : Unit_Trooper;
    row.weaponRange = game->objectData.data[item][HOUSE_ATREIDES].weaponrange;
    row.reload = game->objectData.data[item][HOUSE_ATREIDES].weaponreloadtime;
    if(shooterKind == kRocketTurret) {
        // Rocket-Turrets Need Power in these options: a Wind Trap away from the encounter feeds it.
        require(owner->placeStructure(NONE_ID, Structure_WindTrap, site.x - 12, site.y - 12, true, true) != nullptr, "No Wind Trap");
        auto* turret = owner->placeStructure(NONE_ID, Structure_RocketTurret, site.x, site.y, true, true);
        require(turret != nullptr, "No Rocket Turret");
        require(owner->getProducedPower() >= owner->getPowerRequirement(), "The Rocket Turret has no power");
        shooters.push_back(turret->getObjectID());
        lastTimers.push_back(0);
    } else {
        const int count = shooterKind == kTroopers ? 3 : 1;
        for(int i = 0; i < count; ++i) {
            auto* unit = owner->createUnit(item, true);
            require(unit != nullptr, "Could not create the shooter");
            unit->deploy(site);
            if(stance == kAreaGuard) unit->doSetAttackMode(AREAGUARD);
            else if(stance == kAmbush) unit->doSetAttackMode(AMBUSH);
            else if(stance == kHunt) unit->doSetAttackMode(HUNT);
            else if(stance == kMoving) unit->doMove2Pos(kMapSize - 8, site.y, true);
            else if(stance == kAttackMove)
                unit->doIssueOrder(UnitOrder::at(UnitOrderType::AttackMove, Coord(kMapSize - 8, site.y)), false);
            shooters.push_back(unit->getObjectID());
            lastTimers.push_back(0);
        }
    }

    Uint32 decoy = NONE_ID;
    if(raid == kAttacksNearby) {
        auto* quad = owner->createUnit(Unit_Harvester, true);
        require(quad != nullptr, "Could not create the nearby victim");
        quad->deploy(site + Coord(1, 3));
        quad->doSetAttackMode(STOP);
        decoy = quad->getObjectID();
    }

    auto* aircraft = dynamic_cast<Ornithopter*>(enemy->createUnit(Unit_Ornithopter, true));
    require(aircraft != nullptr, "Could not create a real Ornithopter");
    const int lane = shift(1);
    Coord flightGoal = Coord::Invalid();
    if(raid == kFliesPast) {
        const Coord from(std::max(1, site.x - 14), site.y + 2 + lane);
        flightGoal = Coord(std::min(kMapSize - 2, site.x + 22), site.y + 2 + lane);
        aircraft->deploy(from);
        aircraft->doSetAttackMode(STOP);
        aircraft->doMove2Pos(flightGoal.x, flightGoal.y, true);
    } else {
        aircraft->deploy(site + Coord(14, lane));
        aircraft->doAttackObject(game->getObjectManager().getObject(raid == kAttacksNearby ? decoy : shooters.front()), true);
    }
    const Uint32 aircraftID = aircraft->getObjectID();
    const int startHP = lround(aircraft->getHealth());
    int lastHP = startHP;

    std::set<const Bullet*> live;
    const int limit = MILLI2CYCLES(raid == kFliesPast ? 30000 : 60000);
    for(int tick = 0; tick < limit; ++tick) {
        game->updateGameState();
        row.ticks = tick + 1;
        auto* orni = dynamic_cast<Ornithopter*>(game->getObjectManager().getObject(aircraftID));
        const bool alive = orni != nullptr && orni->getHealth() > 0;
        const int hp = alive ? static_cast<int>(lround(orni->getHealth())) : 0;
        if(hp < lastHP) { ++row.damageEvents; row.damage += lastHP - hp; }
        lastHP = hp;
        if(!alive && row.killTick < 0) { row.killed = 1; row.killTick = tick; }

        int shootersAlive = 0;
        bool anyInRange = false;
        for(std::size_t i = 0; i < shooters.size(); ++i) {
            auto* object = game->getObjectManager().getObject(shooters[i]);
            if(object == nullptr || object->getHealth() <= 0) continue;
            ++shootersAlive;
            auto* unit = dynamic_cast<UnitBase*>(object);
            const bool targetsIt = alive && object->target.getObjPointer() == orni;
            const int far = alive ? static_cast<int>(lround(distanceFrom(object->getCenterPoint(), orni->getCenterPoint()))) : -1;
            if(targetsIt && !row.picked) { row.picked = 1; row.firstPickTick = tick; row.firstPickDistance = far; }
            if(unit != nullptr) {
                if(unit->primaryWeaponTimer > lastTimers[i]) {
                    ++row.shots;
                    if(row.firstShotTick < 0) { row.firstShotTick = tick; row.firstShotDistance = far; }
                    if(unit->lastFiredBulletType == Bullet_ShellSmall) ++row.gunShots;
                    else if(unit->lastFiredBulletType != Bullet_SmallRocket && unit->lastFiredBulletType != Bullet_Rocket)
                        ++row.otherBullets;
                }
                lastTimers[i] = unit->primaryWeaponTimer;
            }
            if(alive && blockDistance(object->getLocation(), orni->getLocation()) <= object->getWeaponRange()) {
                anyInRange = true;
                if(unit != nullptr) {
                    const int wanted = destinationDrawnAngle(unit->getLocation(), orni->getLocation());
                    if(!targetsIt) ++(object->target.getObjPointer() != nullptr ? row.otherTarget : row.noTarget);
                    else if(unit->primaryWeaponTimer > 0) ++row.reloading;
                    else if(unit->moving) ++row.moving;
                    else if(unit->getCurrentAttackAngle() != wanted) ++row.turning;
                    else ++row.ready;
                }
            }
        }
        if(anyInRange) ++row.inRangeTicks;
        row.shootersLost = static_cast<int>(shooters.size()) - shootersAlive;

        // A structure has no unit weapon timer: count its projectiles as they appear.
        std::set<const Bullet*> now;
        for(const auto* bullet : bulletList) {
            now.insert(bullet);
            if(shooterKind != kRocketTurret || live.count(bullet) != 0
               || std::find(shooters.begin(), shooters.end(), bullet->shooterID) == shooters.end()) continue;
            ++row.shots;
            if(row.firstShotTick < 0 && alive) {
                const auto* turret = game->getObjectManager().getObject(bullet->shooterID);
                row.firstShotTick = tick;
                row.firstShotDistance = turret ? static_cast<int>(lround(distanceFrom(turret->getCenterPoint(), orni->getCenterPoint()))) : -1;
            }
            if(bullet->getBulletID() != Bullet_TurretRocket) ++row.otherBullets;
        }
        live.swap(now);

        if(!alive || shootersAlive == 0) break;
        if(raid == kFliesPast) {
            if(blockDistance(orni->getLocation(), flightGoal) <= 1) break;
        } else if(orni->target.getObjPointer() == nullptr) {
            // The raid goes on: back onto its victim, or the nearest shooter still standing.
            const ObjectBase* next = raid == kAttacksNearby ? game->getObjectManager().getObject(decoy) : nullptr;
            if(next == nullptr || next->getHealth() <= 0) {
                next = nullptr;
                FixPoint best = FixPt_MAX;
                for(const Uint32 id : shooters) {
                    const auto* object = game->getObjectManager().getObject(id);
                    if(object == nullptr || object->getHealth() <= 0) continue;
                    const FixPoint far = distanceFrom(orni->getCenterPoint(), object->getCenterPoint());
                    if(far < best) { best = far; next = object; }
                }
                if(raid == kAttacksNearby) break; // The victim is gone; the case is over.
            }
            if(next != nullptr) orni->doAttackObject(next, true);
        }
    }
    game.reset();
    currentGame = nullptr;
    return row;
}

} // namespace TrooperStanceProbe

int runTrooperStanceProbe() {
    using namespace TrooperStanceProbe;
    const char* outEnv = std::getenv("TROOPER_STANCE_PROBE_OUT");
    const char* seedEnv = std::getenv("TROOPER_STANCE_PROBE_SEEDS");
    require(outEnv && seedEnv, "Missing Trooper stance runner environment");
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    settings.general.playerName = "AA tester";
    const std::string mapData = fixtureMap();
    std::ofstream csv(std::string(outEnv) + "/stances.csv");
    csv << "shooter,stance,raid,seed,reach,weapon_range,reload,picked,first_pick_tick,first_pick_distance,first_shot_tick,"
           "first_shot_distance,shots,gun_shots,other_bullets,damage_events,damage,killed,kill_tick,shooters_lost,"
           "in_range_ticks,no_target,other_target,reloading,turning,moving,ready,ticks\n";
    std::vector<int> seeds;
    std::istringstream input(seedEnv);
    for(std::string item; std::getline(input, item, ',');) seeds.push_back(std::stoi(item));
    int cases = 0;
    for(const int shooter : {kTrooper, kTroopers, kLauncher, kRocketTurret})
    for(const int stance : {kGuard, kAreaGuard, kAmbush, kHunt, kMoving, kAttackMove}) {
        if(shooter == kRocketTurret && stance != kGuard) continue;
        for(const int raid : {kAttacksThem, kAttacksNearby, kFliesPast})
        for(const int seed : seeds)
        for(const bool reach : {false, true}) {
            // The rule is about Troopers: the yardsticks are played once.
            if(!reach && shooter != kTrooper && shooter != kTroopers) continue;
            const Row r = play(mapData, shooter, stance, raid, seed, reach);
            csv << shooterName(shooter) << ',' << stanceName(stance) << ',' << raidName(raid) << ',' << seed << ','
                << reach << ',' << r.weaponRange << ',' << r.reload << ',' << r.picked << ',' << r.firstPickTick << ','
                << r.firstPickDistance << ',' << r.firstShotTick << ',' << r.firstShotDistance << ',' << r.shots << ','
                << r.gunShots << ',' << r.otherBullets << ',' << r.damageEvents << ',' << r.damage << ',' << r.killed << ','
                << r.killTick << ',' << r.shootersLost << ',' << r.inRangeTicks << ',' << r.noTarget << ','
                << r.otherTarget << ',' << r.reloading << ',' << r.turning << ',' << r.moving << ',' << r.ready << ','
                << r.ticks << '\n';
            csv.flush();
            ++cases;
        }
    }
    SDL_Log("TROOPER_STANCE_PROBE_PASS: cases=%d", cases);
    return -1;
}
