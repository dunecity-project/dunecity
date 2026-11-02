// Trooper Anti-Air consumer fixture. Every shot comes from UnitBase::attack(),
// every aircraft follows AirUnit's normal movement, and all damage is Map::damage.
// The diagnostic main is compiled with -fno-access-control only to read telemetry
// and set the actors' initial facing; no flight, damage or reload values are replaced.
#include <Bullet.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Network/GameStateDigest.h>
#include <misc/OMemoryStream.h>
#include <mod/ModManager.h>
#include <sand.h>
#include <units/Ornithopter.h>
#include <units/Trooper.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TrooperAAProbe {

void require(bool ok, const char* why) {
    if(!ok) {
        SDL_Log("TROOPER_AA_PROBE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
}

std::string fixtureMap() {
    std::string data = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=8\n"
                       "[MAP]\nSizeX=64\nSizeY=32\n";
    for(int row = 0; row < 32; ++row) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", row);
        data += key + std::string(64, '%') + "\n";
    }
    // The non-combat MCVs keep both real houses alive, far from the encounter.
    data += "[Player1]\nCredits=2000\nBrain=Team1\n"
            "[Player2]\nCredits=2000\nBrain=Team2\n"
            "[UNITS]\nID001=Player1,MCV,256,0,64,Area Guard\n"
            "ID002=Player2,MCV,256,2047,64,Area Guard\n";
    return data;
}

struct Result {
    std::vector<std::string> states;
    int startHP = 0;
    int endHP = 0;
    int shots = 0;
    int observedRockets = 0;
    int damageEvents = 0;
    int firstShot = -1;
    int firstDamage = -1;
    int firstCounter = -1;
    int goalUpdates = 0;
    int armedGoalUpdates = 0;
    int armedHeadingChanges = 0;
    int pathWorld = 0;
    int netXWorld = 0;
    int maxOffCourseWorld = 0;
    int ticks = 0;
    bool crossedLine = false;
    bool exited = false;
    bool killed = false;
    Uint32 finalRng = 0;
    Uint64 traceHash = 0;
};

struct RocketView {
    int heading;
    Coord goal;
};

// Deliberately excludes match options (the compared arms differ in that one
// switch). Object and projectile records, house/object digests and RNG state are
// compared byte for byte, not inferred from HP or a hash alone.
std::string simulationBytes(Game& game) {
    OMemoryStream state;
    state.open();
    const auto digest = game.computeStateDigest();
    Uint8 encoded[GameStateDigest::kEncodedSize];
    GameStateDigest::encode(digest, encoded);
    for(const auto byte : encoded) state.writeUint8(byte);
    game.getObjectManager().save(state);
    state.writeUint32(static_cast<Uint32>(bulletList.size()));
    for(const auto* bullet : bulletList) bullet->save(state);
    return std::string(state.getData(), state.getDataLength());
}

Result encounter(const std::string& mod, const std::string& mapData,
                 bool enabled, int seed, int direction, int count, bool outbound,
                 int repeat, std::ofstream& encounters, std::ofstream& frames,
                 std::ofstream& projectiles) {
    auto options = settings.gameOptions;
    options.trooperAntiAir = enabled;
    options.researchLab = false;
    options.fogOfWar = false;
    options.immortalHumanPlayer = false;
    GameInitSettings init("trooper-aa.ini", mapData, "Trooper Anti-Air", true, options);
    init.randomSeed = seed;
    const int defenderHouse = direction > 0 ? HOUSE_ATREIDES : HOUSE_HARKONNEN;
    const int attackerHouse = direction > 0 ? HOUSE_HARKONNEN : HOUSE_ATREIDES;
    GameInitSettings::HouseInfo defender(static_cast<HOUSETYPE>(defenderHouse), 1);
    defender.addPlayerInfo({"AA tester", "HumanPlayer"});
    init.addHouseInfo(defender);
    GameInitSettings::HouseInfo attacker(static_cast<HOUSETYPE>(attackerHouse), 2);
    attacker.addPlayerInfo({"Flyby", "HumanPlayer"});
    init.addHouseInfo(attacker);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // Private fixture, never a public match-analytics sample.
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    require(game->getGameInitSettings().getGameOptions().trooperAntiAir == enabled,
            "The consumer did not retain its Trooper Anti-Air option");
    const bool cityContent = ModManager::instance().isDuneCityContentActive();
    auto* owner = game->getHouse(defenderHouse);
    auto* enemy = game->getHouse(attackerHouse);
    require(owner && enemy && owner->isAlive() && enemy->isAlive(), "AA fixture houses missing");
    require(game->objectData.data[Unit_Trooper][defenderHouse].weapondamage == 5,
            "The fixture requires the unchanged stock 5-damage Trooper");
    require(game->objectData.data[Unit_Ornithopter][attackerHouse].hitpoints == 25,
            "The fixture requires the unchanged stock 25-HP Ornithopter");

    const Coord site(32, 16);
    // Start inside range for both infantry tiles; an out-of-range HOLD order is
    // discarded by the engine, so five tiles would silently exclude the rear three.
    const Coord start = site + Coord(direction * (outbound ? 3 : -4), 0);
    auto* aircraft = dynamic_cast<Ornithopter*>(enemy->createUnit(Unit_Ornithopter, true));
    require(aircraft != nullptr, "Could not create the real Ornithopter");
    aircraft->deploy(start);
    aircraft->angle = direction > 0 ? RIGHT : LEFT;
    aircraft->drawnAngle = direction > 0 ? RIGHT : LEFT;
    aircraft->setVisible(VIS_ALL, true);
    // Stop disables autonomous targeting, not the subsequent explicit move order.
    // The aircraft flies through the group to the far edge without attacking it.
    aircraft->doSetAttackMode(STOP);
    aircraft->doMove2Pos(direction > 0 ? 62 : 1, site.y, true);
    const Uint32 aircraftID = aircraft->getObjectID();
    const Coord initialPosition = aircraft->getCenterPoint();
    Coord lastPosition = initialPosition;

    std::vector<Uint32> troopers;
    std::vector<int> lastTimers;
    for(int i = 0; i < count; ++i) {
        auto* trooper = dynamic_cast<Trooper*>(owner->createUnit(Unit_Trooper, true));
        require(trooper != nullptr, "Could not create a real Trooper");
        // Five infantry fit on one tile. The remaining three in the eight-man
        // group use the adjacent tile; infantry offsets remain engine-assigned.
        const Coord spot = site + Coord(0, outbound ? 1 : i / 5);
        trooper->deploy(spot);
        trooper->setVisible(VIS_ALL, true);
        trooper->angle = destinationDrawnAngle(spot, start);
        trooper->drawnAngle = lround(trooper->angle) % NUM_ANGLES;
        trooper->doSetAttackMode(HOLD);
        trooper->doAttackObject(aircraft, true);
        require(trooper->primaryWeaponTimer == 0, "A new Trooper is not ready to fire");
        troopers.push_back(trooper->getObjectID());
        lastTimers.push_back(0);
    }

    Result result;
    result.startHP = lround(aircraft->getHealth());
    result.endHP = result.startHP;
    const int exitX = (site.x + direction * (outbound ? 22 : 10)) * TILESIZE + TILESIZE / 2;
    const int lineX = site.x * TILESIZE + TILESIZE / 2;
    const int limit = MILLI2CYCLES(2200);
    require(game->objectData.data[Unit_Trooper][defenderHouse].weaponreloadtime > limit,
            "The one-pass fixture would allow a second volley");
    const char* scenario = outbound ? "outbound_counter" : "overhead";
    const char* heading = direction > 0 ? "east" : "west";
    auto prefix = [&](std::ostream& stream) -> std::ostream& {
        return stream << mod << ',' << cityContent << ',' << enabled << ',' << scenario << ','
                      << seed << ',' << heading << ',' << count << ',' << repeat;
    };
    std::map<Uint32, RocketView> seen;
    GameStateDigest::Hasher traceHash;

    for(int tick = 0; tick < limit; ++tick) {
        game->updateGameState();
        result.ticks = tick + 1;
        auto* live = dynamic_cast<Ornithopter*>(game->getObjectManager().getObject(aircraftID));
        result.killed = live == nullptr || live->getHealth() <= 0;
        const int hp = result.killed ? 0 : lround(live->getHealth());
        if(hp < result.endHP) {
            ++result.damageEvents;
            if(result.firstDamage < 0) result.firstDamage = tick;
        }
        result.endHP = hp;
        if(live) {
            const Coord position = live->getCenterPoint();
            result.pathWorld += std::abs(position.x - lastPosition.x) + std::abs(position.y - lastPosition.y);
            result.netXWorld = position.x - initialPosition.x;
            result.maxOffCourseWorld = std::max(result.maxOffCourseWorld, std::abs(position.y - initialPosition.y));
            if(direction * (position.x - lineX) >= 0) result.crossedLine = true;
            if(direction * (position.x - exitX) >= 0 && direction * (position.x - lastPosition.x) > 0)
                result.exited = true;
            lastPosition = position;
        }
        for(std::size_t i = 0; i < troopers.size(); ++i) {
            const auto* trooper = dynamic_cast<const Trooper*>(game->getObjectManager().getObject(troopers[i]));
            require(trooper != nullptr, "A non-attacking flyby killed a Trooper");
            if(trooper->primaryWeaponTimer > lastTimers[i]) {
                ++result.shots;
                if(result.firstShot < 0) result.firstShot = tick;
            }
            lastTimers[i] = trooper->primaryWeaponTimer;
        }
        int activeRockets = 0;
        for(const auto* bullet : bulletList) {
            if(std::find(troopers.begin(), troopers.end(), bullet->shooterID) == troopers.end()) continue;
            ++activeRockets;
            require(bullet->getBulletID() == Bullet_SmallRocket && bullet->airAttack && bullet->damage == 5,
                    "The real Trooper fired a changed projectile or payload");
            auto previous = seen.find(bullet->shooterID);
            if(previous == seen.end()) {
                ++result.observedRockets;
                if(result.firstCounter < 0) result.firstCounter = bullet->detonationTimer;
                seen.emplace(bullet->shooterID, RocketView{bullet->projectileHeading, bullet->destination});
            } else {
                if(previous->second.goal != bullet->destination) {
                    ++result.goalUpdates;
                    if(bullet->detonationTimer == 0) ++result.armedGoalUpdates;
                }
                if(bullet->detonationTimer == 0 && previous->second.heading != bullet->projectileHeading)
                    ++result.armedHeadingChanges;
                previous->second = {bullet->projectileHeading, bullet->destination};
            }
            prefix(projectiles) << ',' << tick << ',' << bullet->shooterID << ',' << bullet->damage << ','
                << bullet->detonationTimer << ',' << int(bullet->projectileHeading) << ','
                << int(bullet->projectileAim) << ',' << int(bullet->projectileTurn) << ','
                << bullet->getRealX().getRawValue() << ',' << bullet->getRealY().getRawValue() << ','
                << bullet->destination.x << ',' << bullet->destination.y << ','
                << lastPosition.x << ',' << lastPosition.y << '\n';
        }
        auto bytes = simulationBytes(*game);
        for(const unsigned char byte : bytes) traceHash.mixUint8(byte);
        result.states.push_back(std::move(bytes));
        const auto digest = game->computeStateDigest();
        prefix(frames) << ',' << tick << ',' << game->getGameCycleCount() << ',' << hp << ','
            << result.shots << ',' << activeRockets << ',' << lastPosition.x << ',' << lastPosition.y << ','
            << digest.randomSeed << ',' << digest.objectHash << ',' << digest.houseHash << '\n';
        if(result.killed || result.exited) break;
    }
    result.finalRng = game->randomGen.getSeed();
    result.traceHash = traceHash.value();
    const char* outcome = result.killed ? "killed" : result.exited ? "escaped" : "timeout";
    prefix(encounters) << ',' << defenderHouse << ',' << attackerHouse << ','
        << result.startHP << ',' << result.endHP << ',' << result.startHP - result.endHP << ','
        << result.killed << ',' << result.shots << ',' << result.observedRockets << ','
        << result.damageEvents << ',' << result.firstShot << ',' << result.firstDamage << ','
        << result.firstCounter << ',' << result.goalUpdates << ',' << result.armedGoalUpdates << ','
        << result.armedHeadingChanges << ',' << result.pathWorld << ',' << result.netXWorld << ','
        << result.maxOffCourseWorld << ',' << result.crossedLine << ',' << result.exited << ','
        << result.ticks << ',' << result.finalRng << ',' << result.traceHash << ',' << outcome << '\n';
    encounters.flush();
    frames.flush();
    projectiles.flush();
    SDL_Log("TROOPER_AA_ENCOUNTER: mod=%s enabled=%d scenario=%s seed=%d heading=%s troopers=%d repeat=%d "
            "start_hp=%d end_hp=%d shots=%d goal_updates=%d armed_goal_updates=%d armed_heading_changes=%d "
            "ticks=%d outcome=%s trace_hash=%llu",
            mod.c_str(), enabled, scenario, seed, heading, count, repeat, result.startHP, result.endHP,
            result.shots, result.goalUpdates, result.armedGoalUpdates, result.armedHeadingChanges,
            result.ticks, outcome, static_cast<unsigned long long>(result.traceHash));

    require(result.killed || result.exited, "Flyby neither died nor crossed its exit plane");
    require(result.killed || result.crossedLine, "A surviving flyby never crossed the Trooper line");
    require(result.pathWorld > 0 && result.maxOffCourseWorld == 0, "The aircraft did not fly its straight pass");
    require(result.shots == count && result.observedRockets == result.shots,
            "The single-volley weapon timer and real rocket counts disagree");
    if(cityContent && enabled) {
        require(result.goalUpdates > 0, "Enabled Trooper rockets retained the old aircraft destination");
        if(outbound) {
            require(result.armedGoalUpdates > 0 && result.armedHeadingChanges > 0,
                    "An armed Trooper rocket stopped tracking when its counter reached zero");
            require(result.endHP == result.startHP - 5, "The outbound real rocket did not deliver its unchanged payload");
        } else if(count == 3) {
            require(result.endHP > 0 && result.endHP < result.startHP, "Three Troopers must damage, not one-volley kill, the aircraft");
        } else {
            require(result.killed, "Five or eight enabled Troopers failed to kill the aircraft in one pass");
        }
    }
    game.reset();
    currentGame = nullptr;
    return result;
}

} // namespace TrooperAAProbe

int runTrooperAntiAirProbe() {
    using namespace TrooperAAProbe;
    const char* modEnv = std::getenv("TROOPER_AA_PROBE_MOD");
    const char* outEnv = std::getenv("TROOPER_AA_PROBE_OUT");
    const char* seedEnv = std::getenv("TROOPER_AA_PROBE_SEEDS");
    require(modEnv && outEnv && seedEnv, "Missing Trooper Anti-Air runner environment");
    const std::string mod(modEnv), outDir(outEnv);
    require(ModManager::instance().setActiveMod(mod), "Missing Trooper Anti-Air fixture mod");
    settings.general.playerName = "AA tester";
    const bool cityContent = ModManager::instance().isDuneCityContentActive();
    std::vector<int> seeds;
    std::istringstream input(seedEnv);
    for(std::string seed; std::getline(input, seed, ',');) seeds.push_back(std::stoi(seed));
    require(!seeds.empty(), "No Trooper Anti-Air seeds supplied");
    const std::string mapData = fixtureMap();
    std::ofstream encounters(outDir + "/encounters.csv");
    std::ofstream frames(outDir + "/frames.csv");
    std::ofstream projectiles(outDir + "/projectiles.csv");
    require(encounters.good() && frames.good() && projectiles.good(), "Could not open Trooper Anti-Air telemetry");
    const char* prefix = "mod,dunecity_content,trooper_anti_air,scenario,seed,heading,troopers,repeat";
    encounters << prefix << ",defender_house,attacker_house,start_hp,end_hp,health_loss_capped,killed,shots,"
        "rockets_observed,damage_events,first_shot_tick,first_damage_tick,first_observed_counter,goal_updates,"
        "armed_goal_updates,armed_heading_changes,path_world,net_x_world,max_off_course_world,crossed_trooper_line,"
        "exit_crossed,ticks,final_rng,trace_hash,outcome\n";
    frames << prefix << ",tick,cycle,hp,shots,active_rockets,air_x,air_y,rng,object_hash,house_hash\n";
    projectiles << prefix << ",tick,shooter_id,payload,counter,heading,aim,turn,real_x_raw,real_y_raw,"
        "goal_x,goal_y,air_x,air_y\n";
    int cases = 0;
    for(const int seed : seeds) for(const int direction : {1, -1}) for(const int count : {3, 5, 8, 1}) {
        // The outbound single-shot case outlives the natural arming counter. It
        // exercises the original zero-counter cause without editing a projectile.
        const bool outbound = count == 1;
        auto off = encounter(mod, mapData, false, seed, direction, count, outbound, 0, encounters, frames, projectiles);
        auto offRepeat = encounter(mod, mapData, false, seed, direction, count, outbound, 1, encounters, frames, projectiles);
        require(off.states == offRepeat.states, "Repeated off-arm object/projectile bytes or RNG diverged");
        auto on = encounter(mod, mapData, true, seed, direction, count, outbound, 0, encounters, frames, projectiles);
        auto onRepeat = encounter(mod, mapData, true, seed, direction, count, outbound, 1, encounters, frames, projectiles);
        require(on.states == onRepeat.states, "Repeated on-arm object/projectile bytes or RNG diverged");
        if(!cityContent)
            require(off.states == on.states, "Trooper Anti-Air changed unrelated-content object/projectile bytes or RNG");
        cases += 4;
    }
    // A units probe owns its own marker, never the menu runner's result marker.
    SDL_Log("TROOPER_AA_PROBE_PASS: mod=%s dunecity_content=%d encounters=%d repeats_identical=1 unrelated_content_identical=%d",
            mod.c_str(), cityContent, cases, !cityContent);
    return -1;
}
