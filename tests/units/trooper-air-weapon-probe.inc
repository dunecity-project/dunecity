// Trooper weapon and reach against aircraft. Every shot comes from UnitBase::attack(),
// every target is found by the engine's own target search unless the case says
// "ordered", and all damage is Map::damage. The Troopers stand on a map nobody has
// explored, so the only ground their team sees is their own one-tile sight: that is
// the situation of Troopers away from a base, and the one in which they answered an
// Ornithopter only at gun distance.
//
// The diagnostic main is compiled with -fno-access-control only to read weapon timers
// and projectile fields, and to hold an aircraft in place (speed zero, as
// aircraft-aa-probe's pinned_diag does) so each case's distance is exact. No weapon,
// damage, range or reload value is replaced.
#include <Bullet.h>
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <sand.h>
#include <units/AirUnit.h>
#include <units/Trooper.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace TrooperAirWeaponProbe {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("TROOPER_AIR_WEAPON_PROBE_FAIL: %s", why.c_str());
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

// Distances are tiles east of the Trooper tile. Two are relative to the weapon range.
constexpr int kRangeEdge = -1;   // exactly the Trooper's weapon range
constexpr int kBeyondRange = -2; // one tile further

enum class Expect {
    Engages,        // both switch states: the target is shot and damaged
    EngagesWithRule,// Trooper Anti-Air on in DuneCity content: shot and damaged; otherwise untouched
    Untouched,      // both switch states: no shot, no damage
};

struct Case {
    const char* name;
    int targetItem;
    int distance;
    bool ordered;   // Player attack order on the target instead of the engine's target search.
    int troopers;   // 1 = a single Trooper, 3 = what a Troopers order deploys.
    bool raid;      // A free Ornithopter ordered onto the first Trooper, from ten tiles away.
    Expect expect;
};

struct Shot {
    int tick;
    int trooper;
    int weapon;
    bool operator==(const Shot& other) const {
        return tick == other.tick && trooper == other.trooper && weapon == other.weapon;
    }
};

struct Outcome {
    std::vector<Shot> shots;
    int startHP = 0;
    int endHP = 0;
    int firstShot = -1;
    int firstShotDistance = -1;
    int troopersLeft = 0;
    int ticks = 0;
};

Outcome run(const std::string& mod, const std::string& mapData, const Case& spec, bool enabled, std::ofstream& csv) {
    auto options = settings.gameOptions;
    options.trooperAntiAir = enabled;
    options.researchLab = false;
    options.fogOfWar = false;
    options.immortalHumanPlayer = false;
    GameInitSettings init("trooper-air-weapon.ini", mapData, "Trooper air weapon", true, options);
    init.randomSeed = 41;
    GameInitSettings::HouseInfo defender(HOUSE_ATREIDES, 1);
    defender.addPlayerInfo({"AA tester", "HumanPlayer"});
    init.addHouseInfo(defender);
    GameInitSettings::HouseInfo attacker(HOUSE_HARKONNEN, 2);
    attacker.addPlayerInfo({"Flyby", "HumanPlayer"});
    init.addHouseInfo(attacker);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // Private fixture, never a public match-analytics sample.
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    require(game->getGameInitSettings().getGameOptions().trooperAntiAir == enabled,
            "The game did not keep its Trooper Anti-Air option");
    const bool ruleActive = enabled && ModManager::instance().isDuneCityContentActive();
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    require(owner && enemy && owner->isAlive() && enemy->isAlive(), "Fixture houses missing");
    const auto& trooperData = game->objectData.data[Unit_Trooper][HOUSE_ATREIDES];
    const int fullDamage = trooperData.weapondamage;
    const int range = trooperData.weaponrange;
    require(fullDamage >= 4 && range >= 5 && trooperData.viewrange < 2,
            "The fixture needs a Trooper whose rocket outreaches its sight");

    const Coord site(32, 16);
    const bool air = isFlyingUnit(spec.targetItem);
    const bool held = air && !spec.raid;
    const int distance = spec.raid ? 10 : spec.distance == kRangeEdge ? range
                       : spec.distance == kBeyondRange ? range + 1 : spec.distance;
    if(held) game->objectData.data[spec.targetItem][HOUSE_HARKONNEN].maxspeed = 0;
    auto* target = enemy->createUnit(spec.targetItem, true);
    require(target != nullptr, "Could not create the target unit");
    target->deploy(site + Coord(distance, 0));
    if(held) static_cast<AirUnit*>(target)->currentMaxSpeed = 0;
    require(target->isAFlyingUnit() == air, "The target is not the kind of unit the case names");
    // Stop keeps a held or ground target from shooting back: the Troopers' fire is all that happens.
    if(!spec.raid) target->doSetAttackMode(STOP);
    const Uint32 targetID = target->getObjectID();
    Coord targetCenter = target->getCenterPoint();

    std::vector<Uint32> troopers;
    std::vector<int> lastTimers;
    for(int i = 0; i < spec.troopers; ++i) {
        auto* trooper = dynamic_cast<Trooper*>(owner->createUnit(Unit_Trooper, true));
        require(trooper != nullptr, "Could not create a real Trooper");
        trooper->deploy(site);
        require(trooper->getAttackMode() == GUARD, "A new Trooper does not start on Guard");
        if(spec.ordered) trooper->doAttackObject(target, true);
        troopers.push_back(trooper->getObjectID());
        lastTimers.push_back(0);
    }
    if(spec.raid) target->doAttackObject(game->getObjectManager().getObject(troopers.front()), true);
    // The point of the fixture: beyond one tile the Troopers' team sees no ground.
    if(distance >= 2)
        require(!currentGameMap->getTile(site + Coord(distance, 0))->isExploredByTeam(owner->getTeamID()),
                "The ground below the target is already explored; the case would not test the sight rule");

    Outcome outcome;
    outcome.startHP = lround(target->getHealth());
    outcome.endHP = outcome.startHP;
    const int shotsToKill = (outcome.startHP + fullDamage - 1) / fullDamage;
    const int limit = spec.raid ? MILLI2CYCLES(40000)
        : std::max(MILLI2CYCLES(12000), shotsToKill * trooperData.weaponreloadtime + MILLI2CYCLES(4000));
    std::set<const Bullet*> live;
    for(int tick = 0; tick < limit; ++tick) {
        game->updateGameState();
        outcome.ticks = tick + 1;
        const auto* liveTarget = game->getObjectManager().getObject(targetID);
        outcome.endHP = liveTarget && liveTarget->getHealth() > 0 ? lround(liveTarget->getHealth()) : 0;
        if(liveTarget) targetCenter = liveTarget->getCenterPoint();

        // Weapon of every shot: the timer restarts when a Trooper fires, and the unit keeps
        // the bullet type it really fired (a point-blank rocket can detonate within the cycle).
        outcome.troopersLeft = 0;
        for(std::size_t i = 0; i < troopers.size(); ++i) {
            const auto* trooper = dynamic_cast<const Trooper*>(game->getObjectManager().getObject(troopers[i]));
            if(trooper == nullptr) continue;
            ++outcome.troopersLeft;
            if(trooper->primaryWeaponTimer > lastTimers[i]) {
                const int shotDistance = lround(distanceFrom(trooper->getCenterPoint(), targetCenter));
                const int weapon = trooper->lastFiredBulletType;
                outcome.shots.push_back({tick, static_cast<int>(i), weapon});
                if(outcome.firstShot < 0) {
                    outcome.firstShot = tick;
                    outcome.firstShotDistance = shotDistance;
                }
                csv << mod << ',' << enabled << ',' << spec.name << ',' << tick << ',' << i << ',' << weapon << ','
                    << shotDistance << ',' << outcome.endHP << '\n';
                if(air) {
                    require(weapon == Bullet_SmallRocket,
                            std::string(spec.name) + ": a Trooper fired its gun, not its rocket, at an aircraft");
                } else if(shotDistance <= TILESIZE + TILESIZE / 2) {
                    require(weapon == Bullet_ShellSmall,
                            std::string(spec.name) + ": a Trooper did not use its gun on an adjacent ground target");
                } else if(shotDistance >= 3 * TILESIZE) {
                    require(weapon == Bullet_SmallRocket,
                            std::string(spec.name) + ": a Trooper did not use its rocket on a distant ground target");
                }
            }
            lastTimers[i] = trooper->primaryWeaponTimer;
        }
        // Payload and air flag of every projectile that outlives its first cycle.
        std::set<const Bullet*> now;
        for(const auto* bullet : bulletList) {
            now.insert(bullet);
            if(live.count(bullet) != 0
               || std::find(troopers.begin(), troopers.end(), bullet->shooterID) == troopers.end()) continue;
            if(air) {
                require(bullet->getBulletID() == Bullet_SmallRocket && bullet->airAttack && bullet->damage == fullDamage,
                        std::string(spec.name) + ": the shot at an aircraft is not a full-damage air rocket");
            } else {
                const int reduced = fullDamage - fullDamage / 4;
                require(!bullet->airAttack
                        && ((bullet->getBulletID() == Bullet_ShellSmall && bullet->damage == fullDamage)
                            || (bullet->getBulletID() == Bullet_SmallRocket && bullet->damage == reduced)),
                        std::string(spec.name) + ": the ground weapon or its payload changed");
            }
        }
        live.swap(now);
        if(outcome.endHP == 0 || outcome.troopersLeft == 0) break;
    }

    SDL_Log("TROOPER_AIR_WEAPON_CASE: mod=%s rule=%d case=%s distance=%d shots=%d first_shot_tick=%d "
            "first_shot_distance=%d start_hp=%d end_hp=%d troopers_left=%d ticks=%d",
            mod.c_str(), ruleActive, spec.name, distance, static_cast<int>(outcome.shots.size()), outcome.firstShot,
            outcome.firstShotDistance, outcome.startHP, outcome.endHP, outcome.troopersLeft, outcome.ticks);
    csv.flush();

    const std::string label = std::string(spec.name) + (enabled ? " (rule on)" : " (rule off)");
    const bool engages = spec.expect == Expect::Engages || (spec.expect == Expect::EngagesWithRule && ruleActive);
    if(spec.raid) {
        require(!outcome.shots.empty(), label + ": the squad never fired at the attacking Ornithopter");
        if(ruleActive) require(outcome.endHP < outcome.startHP, label + ": the squad's rockets never hurt the Ornithopter");
    } else if(engages) {
        require(!outcome.shots.empty(), label + ": the Troopers never fired at a target in weapon range");
        require(outcome.endHP < outcome.startHP, label + ": the Troopers' fire did not damage the target");
        if(isOrnithopterUnit(spec.targetItem))
            require(outcome.endHP == 0, label + ": the held Ornithopter survived a full engagement");
    } else {
        require(outcome.shots.empty() && outcome.endHP == outcome.startHP,
                label + ": the Troopers fired at a target they must leave alone");
    }
    game.reset();
    currentGame = nullptr;
    return outcome;
}

} // namespace TrooperAirWeaponProbe

int runTrooperAirWeaponProbe() {
    using namespace TrooperAirWeaponProbe;
    const char* modEnv = std::getenv("TROOPER_AIR_WEAPON_PROBE_MOD");
    const char* outEnv = std::getenv("TROOPER_AIR_WEAPON_PROBE_OUT");
    require(modEnv && outEnv, "Missing Trooper air weapon runner environment");
    const std::string mod(modEnv), outDir(outEnv);
    require(ModManager::instance().setActiveMod(mod), "Missing Trooper air weapon fixture mod");
    settings.general.playerName = "AA tester";
    const bool cityContent = ModManager::instance().isDuneCityContentActive();
    const std::string mapData = fixtureMap();
    std::ofstream csv(outDir + "/shots.csv");
    require(csv.good(), "Could not open Trooper air weapon telemetry");
    csv << "mod,trooper_anti_air,case,tick,trooper,weapon,distance_world,target_hp\n";

    const Case cases[] = {
        // The engine's own target search, Trooper on Guard.
        {"orni_overhead", Unit_Ornithopter, 0, false, 1, false, Expect::Engages},
        {"orni_adjacent", Unit_Ornithopter, 1, false, 1, false, Expect::Engages},
        {"orni_two_tiles", Unit_Ornithopter, 2, false, 1, false, Expect::EngagesWithRule},
        {"orni_three_tiles", Unit_Ornithopter, 3, false, 1, false, Expect::EngagesWithRule},
        {"orni_range_edge", Unit_Ornithopter, kRangeEdge, false, 1, false, Expect::EngagesWithRule},
        {"orni_beyond_range", Unit_Ornithopter, kBeyondRange, false, 1, false, Expect::Untouched},
        {"orni_adjacent_squad", Unit_Ornithopter, 1, false, 3, false, Expect::Engages},
        {"orni_range_edge_squad", Unit_Ornithopter, kRangeEdge, false, 3, false, Expect::EngagesWithRule},
        // A player's attack order never depended on sight.
        {"orni_overhead_ordered", Unit_Ornithopter, 0, true, 1, false, Expect::Engages},
        {"orni_range_edge_ordered", Unit_Ornithopter, kRangeEdge, true, 1, false, Expect::Engages},
        // A real attack run on a squad.
        {"orni_raid", Unit_Ornithopter, 10, false, 3, true, Expect::Engages},
        // Other aircraft keep the sight rule; so does every ground target.
        {"carryall_adjacent", Unit_Carryall, 1, false, 1, false, Expect::Engages},
        {"carryall_range_edge", Unit_Carryall, kRangeEdge, false, 1, false, Expect::Untouched},
        {"quad_three_tiles", Unit_Quad, 3, false, 1, false, Expect::Untouched},
        // Ground weapons: the gun up close, the reduced rocket at distance.
        {"quad_adjacent_ordered", Unit_Quad, 1, true, 1, false, Expect::Engages},
        {"quad_four_tiles_ordered", Unit_Quad, 4, true, 1, false, Expect::Engages},
    };
    int count = 0;
    for(const auto& spec : cases) {
        const auto off = run(mod, mapData, spec, false, csv);
        const auto on = run(mod, mapData, spec, true, csv);
        const bool ruleChangesCase = cityContent && (spec.expect == Expect::EngagesWithRule || spec.raid);
        if(!ruleChangesCase)
            require(off.shots == on.shots && off.endHP == on.endHP && off.ticks == on.ticks,
                    std::string(spec.name) + ": the Trooper Anti-Air switch changed a case it must not touch");
        if(spec.raid && cityContent)
            require(on.firstShot >= 0 && on.firstShot <= off.firstShot,
                    "orni_raid: with the rule on the squad opened fire later than without it");
        count += 2;
    }
    // A units probe owns its own marker, never the menu runner's result marker.
    SDL_Log("TROOPER_AIR_WEAPON_PROBE_PASS: mod=%s dunecity_content=%d runs=%d", mod.c_str(), cityContent, count);
    return -1;
}
