// Included only by run-turret-front-probe.py, never by the shipped game.
//
// Rocket Turrets at the front (QuantBotTurretFront.cpp, TurretFrontPolicy.h): each of the four
// QuantBot switches, in the situation it is for, ON and OFF, with the cases where it must not act.
// CLAUDE.md, "AI behaviours: prove the switch actually does its job before its score counts".
//
// Every case is a fresh custom game on a bare rock map: Atreides is a Brutal QuantBot with a base
// in the west, Harkonnen an idle human-controlled house with a base in the east. The game is played
// by Game::updateGameState() alone; the QuantBot builds and places its own turrets. Activation is
// read from the bot's own order counts (ordered / placed at the site / engaged a unit), the effect
// from the map: the turret stands, its target is an enemy unit, the enemy unit lost health, the
// belt's turrets stand further toward the enemy than the base ever did, the retired turrets are
// gone from the rear only.
//
//   reinforce-on    enemy Quads shoot an own Refinery: FrontTurrets orders turrets beside it that
//                   stand and fire on the Quads
//   reinforce-off   the same with the switch off: no front order
//   reinforce-far   switch on, Quads parked 20 tiles from every own building: no front order
//   advance-on      own Combat Tanks stand east of the base: AdvancingTurrets builds a line of eight
//                   or more turrets, at least three side by side, its furthest at least six tiles
//                   east of the base's first eastern edge, none within reach of the enemy turret
//   advance-off     the same with the switch off: no advance order
//   advance-noarmy  switch on, no army at all: the line still forms and moves forward (it needs no
//                   army; the first version's army-lead trigger kept it from moving)
//   creep-on        an own outpost 9 tiles from Quads guarding the enemy base: TurretCreep places
//                   a turret within range of them, which stands and fires on them
//   creep-off       the same with the switch off: no creep order
//   creep-covered   switch on, two enemy Rocket Turrets cover the Quads: no creep order
//   retire-on       20 own turrets deep in the rear, 4 at the front, 2 by the Palace: rear turrets
//                   go, at most four a review, front and Palace turrets stay
//   retire-off      the same with the switch off: every turret stays
//   drill-on        an enemy base 18 tiles from the own base's edge, guarded by Quads: TurretDrill
//                   builds a dense mass toward and into it (turrets touching, within range of the
//                   enemy buildings) that hurts the Quads; with turrets firing on units only (the
//                   default) no own turret ever targets an enemy building
//   drill-buildings the same with Turrets Can Fire On Buildings on: own turrets also fire on the
//                   enemy buildings they reach
//   drill-off       the same with the switch off: no drill order
//   drill-far       switch on, the enemy base 90 tiles away: no drill order
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <ObjectManager.h>
#include <mod/ModManager.h>
#include <players/Player.h>
#include <players/QuantBot.h>
#include <players/QuantBotConfig.h>
#include <players/TurretFrontPolicy.h>
#include <structures/BuilderBase.h>
#include <structures/StructureBase.h>
#include <structures/TurretBase.h>
#include <units/UnitBase.h>
#include <sand.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace TurretFrontProbe {

constexpr int kMapX = 128, kMapY = 64;
std::string gOut;
SettingsClass::GameOptionsClass gModOptions;
std::ofstream gChecks;

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("TURRET_FRONT_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

void record(const std::string& check, const std::string& detail) {
    gChecks << check << ",\"" << detail << "\"\n";
    gChecks.flush();
    SDL_Log("TURRET_FRONT_PROBE_CHECK: %s %s", check.c_str(), detail.c_str());
}

std::string fixtureMap() {
    std::string data = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTimeOut=0\nTechLevel=8\n[MAP]\nSizeX="
                       + std::to_string(kMapX) + "\nSizeY=" + std::to_string(kMapY) + "\n";
    for(int row = 0; row < kMapY; ++row) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", row);
        data += key + std::string(kMapX, '%') + "\n";
    }
    // The idle human house keeps an MCV in its far corner; the QuantBot gets only what each case
    // places, so it never deploys a second yard.
    data += "[Player1]\nCredits=30000\nQuota=0\n[Player2]\nCredits=0\nQuota=0\n"
            "[UNITS]\nID001=Player2,MCV,256," + std::to_string(kMapX * kMapY - 1) + ",64,Area Guard\n";
    return data;
}

struct Fixture {
    std::unique_ptr<Game> game;
    House* own = nullptr;
    House* enemy = nullptr;
    QuantBot* bot = nullptr;
    std::shared_ptr<QuantBotConfig> config;
};

/// Atreides: a Brutal QuantBot with 30,000 credits; Harkonnen: the idle human tester's house.
Fixture newGame(bool front, bool advance, bool creep, bool retire, bool drill = false, bool fireOnBuildings = false) {
    Fixture f;
    auto options = gModOptions;
    options.turretsFireOnBuildings = fireOnBuildings;
    options.immortalHumanPlayer = false;
    options.instantBuild = true;          // the probe tests where turrets go, not how fast they build
    options.fogOfWar = false;
    options.startWithExploredMap = true;
    GameInitSettings init("turret-front.ini", fixtureMap(), "Turret front", true, options);
    init.randomSeed = 20261127;
    GameInitSettings::HouseInfo bot(HOUSE_ATREIDES, 1);
    bot.addPlayerInfo({"Turret bot", "qBotBrutal"});
    init.addHouseInfo(bot);
    GameInitSettings::HouseInfo human(HOUSE_HARKONNEN, 2);
    human.addPlayerInfo({"Turret tester", "HumanPlayer"});
    init.addHouseInfo(human);

    f.game = std::make_unique<Game>();
    currentGame = f.game.get();
    f.game->bReplay = true; // Private fixture, never a public match-analytics sample.
    f.game->initGame(init);
    f.game->bReplay = false;
    f.game->initializeGameLoop();
    f.own = f.game->getHouse(HOUSE_ATREIDES);
    f.enemy = f.game->getHouse(HOUSE_HARKONNEN);
    require(f.own && f.enemy, "The fixture houses are missing");
    for(const auto& player : f.own->getPlayerList())
        if(auto* q = dynamic_cast<QuantBot*>(player.get())) f.bot = q;
    require(f.bot != nullptr, "Atreides has no QuantBot");
    f.config = std::make_shared<QuantBotConfig>(f.bot->botConfig());
    f.config->force.frontTurrets = front;
    f.config->force.advancingTurrets = advance;
    f.config->force.turretCreep = creep;
    f.config->force.rearTurretRetire = retire;
    f.config->force.turretDrill = drill;
    f.bot->configOverride = f.config;
    require(f.game->getGameInitSettings().getGameOptions().turretsFireOnBuildings == fireOnBuildings,
            "The game did not keep its Turrets Can Fire On Buildings rule");
    require(f.bot->frontTurretsEnabled() == front && f.bot->advancingTurretsEnabled() == advance
            && f.bot->turretCreepEnabled() == creep && f.bot->rearTurretRetireEnabled() == retire
            && f.bot->turretDrillEnabled() == drill,
            "The switches do not read the bot's config (a DUNECITY_TEST_* override is set?)");
    return f;
}

StructureBase* place(House* house, int item, int x, int y) {
    auto* structure = house->placeStructure(NONE_ID, item, x, y, true, true);
    require(structure != nullptr, "Could not place item " + std::to_string(item) + " at "
                                  + std::to_string(x) + "," + std::to_string(y));
    return structure;
}

UnitBase* unit(House* house, int item, int x, int y) {
    auto* u = house->createUnit(item, true);
    require(u != nullptr, "Could not create unit " + std::to_string(item));
    u->deploy(Coord(x, y));
    return u;
}

/// The own base every case starts from: a Construction Yard, a Radar, a Refinery and power, in the
/// west (x 6..18, y 26..37).
void ownBase(Fixture& f) {
    // A fully upgraded yard, as in a mid-game base: Rocket Turrets need the upgrade.
    auto* yard = static_cast<BuilderBase*>(place(f.own, Structure_ConstructionYard, 10, 28));
    yard->curUpgradeLev = yard->getMaxUpgradeLevel();
    yard->updateBuildList();
    place(f.own, Structure_Radar, 13, 28);
    place(f.own, Structure_Refinery, 14, 32);
    for(int i = 0; i < 6; ++i) place(f.own, Structure_WindTrap, 6 + 2 * (i % 2), 26 + 2 * (i / 2) + (i / 2));
}

/// The enemy base in the east: a Construction Yard and power, at \a x.
void enemyBase(Fixture& f, int x) {
    place(f.enemy, Structure_ConstructionYard, x, 30);
    place(f.enemy, Structure_WindTrap, x + 3, 30);
    place(f.enemy, Structure_WindTrap, x + 3, 33);
}

const QuantBot::TurretFrontCounts& counts(const Fixture& f, QuantBot::TurretFrontMode mode) {
    return f.bot->turretFrontCounts[size_t(mode)];
}

int totalOrders(const Fixture& f) {
    int n = 0;
    for(const auto& c : f.bot->turretFrontCounts) n += c.ordered;
    return n;
}

std::string countText(const QuantBot::TurretFrontCounts& c) {
    return "ordered " + std::to_string(c.ordered) + ", placed " + std::to_string(c.placed) + ", engaged "
           + std::to_string(c.engaged) + ", missed " + std::to_string(c.missed);
}

/// What the bot's turret front and yards stood at, logged with every failure.
void diagnose(const Fixture& f) {
    const auto& p = f.bot->turretFrontPlan;
    SDL_Log("TURRET_FRONT_PROBE_DIAG: plan active=%d ordered=%d mode=%d site=%d,%d edge=%d scan_cycle=%u blocked=%d/%d/%d/%d/%d/%d "
            "credits=%d power=%d/%d threats=%zu cycle=%u",
            int(p.active), int(p.ordered), int(p.mode), p.site.x, p.site.y, p.edge, f.bot->turretFrontScanCycle,
            f.bot->turretFrontBlocked[0], f.bot->turretFrontBlocked[1], f.bot->turretFrontBlocked[2],
            f.bot->turretFrontBlocked[3], f.bot->turretFrontBlocked[4], f.bot->turretFrontBlocked[5],
            f.own->getCredits(), f.own->getProducedPower(), f.own->getPowerRequirement(),
            f.bot->visibleGroundTurretThreats.size(), currentGame->getGameCycleCount());
    for(const StructureBase* s : structureList)
        if(s->getOwner() == f.own && s->isABuilder()) {
            const auto* b = static_cast<const BuilderBase*>(s);
            SDL_Log("TURRET_FRONT_PROBE_DIAG: builder item=%u queue=%d producing=%u upgrade=%d/%d", s->getItemID(),
                    b->getProductionQueueSize(), b->getCurrentProducedItem(), b->getCurrentUpgradeLevel(), b->getMaxUpgradeLevel());
        }
}

std::vector<const StructureBase*> rocketTurrets(House* house) {
    std::vector<const StructureBase*> out;
    for(const StructureBase* s : structureList)
        if(s->getOwner() == house && s->getItemID() == Structure_RocketTurret && s->getHealth() > 0) out.push_back(s);
    return out;
}

/// Placed turrets of \a mode, by position, sampled from the bot's order tracker (an order stays
/// tracked until its turret engages a unit or is lost).
void collectPlaced(const Fixture& f, QuantBot::TurretFrontMode mode, std::set<std::pair<int, int>>& out) {
    for(const auto& order : f.bot->turretFrontOrders)
        if(order.mode == mode && order.turret != NONE_ID) out.insert({order.site.x, order.site.y});
}

/// Own Rocket Turrets whose target is an enemy building, and those whose target is a unit.
void sampleTargets(const Fixture& f, int& onBuildings, int& onUnits) {
    for(const StructureBase* s : structureList) {
        if(s->getOwner() != f.own || s->getItemID() != Structure_RocketTurret) continue;
        const auto* t = dynamic_cast<const TurretBase*>(s);
        const ObjectBase* target = t ? t->getTarget() : nullptr;
        if(target == nullptr || target->getOwner() == f.own) continue;
        if(target->isAStructure()) ++onBuildings; else ++onUnits;
    }
}

int health(Uint32 id) {
    const auto* object = currentGame->getObjectManager().getObject(id);
    return object ? int(lround(object->getHealth())) : 0;
}

/// Plays until \a done or \a cycles; returns the cycles played. The bot's cash is topped up to
/// 5,000 whenever it falls below (a mid-game income; the fixture has no harvesting economy, and the
/// bot otherwise spends its starting credits on its base before the case's situation matters).
template<typename Done> int play(Fixture& f, int cycles, Done done) {
    for(int c = 0; c < cycles; ++c) {
        if(f.own->getCredits() < 5000) f.own->addCredits(FixPoint(5000 - f.own->getCredits()));
        f.game->updateGameState();
        if(c % 64 == 0 && done()) return c;
    }
    return cycles;
}

// --- (a) FrontTurrets ---------------------------------------------------------------------------

void reinforce(bool on, bool far) {
    const std::string name = std::string("reinforce-") + (far ? "far" : on ? "on" : "off");
    Fixture f = newGame(on, false, false, false);
    ownBase(f);
    enemyBase(f, 110);
    // Six Quads beside the Refinery's east side (or, far, 20 tiles from every own building).
    std::vector<Uint32> quads;
    const int qx = far ? 40 : 19;
    for(int i = 0; i < 6; ++i) {
        UnitBase* q = unit(f.enemy, Unit_Quad, qx + i % 2, 31 + i / 2);
        q->doSetAttackMode(GUARD);
        quads.push_back(q->getObjectID());
    }
    auto quadHealth = [&] { int sum = 0; for(Uint32 id : quads) sum += health(id); return sum; };
    const int startHealth = quadHealth();
    const auto& c = counts(f, QuantBot::TurretFrontMode::Reinforce);
    const int played = play(f, 9000, [&] { return c.engaged > 0 && quadHealth() < startHealth; });
    if(!on) {
        require(totalOrders(f) == 0, name + ": front orders with the switch off");
        record(name, "no front order in " + std::to_string(played) + " cycles");
        return;
    }
    if(far) {
        require(c.ordered == 0, name + ": reinforcement ordered for Quads 20 tiles from every own building");
        record(name, "no reinforcement in " + std::to_string(played) + " cycles");
        return;
    }
    if(c.ordered == 0) diagnose(f);
    require(c.ordered > 0, name + ": no reinforcement ordered under attack (" + countText(c) + ")");
    require(c.placed > 0, name + ": no ordered turret stood at its site (" + countText(c) + ")");
    require(c.engaged > 0, name + ": no reinforcement turret fired on a unit (" + countText(c) + ")");
    require(quadHealth() < startHealth, name + ": the Quads lost no health");
    // The reinforcement stands beside the attacked Refinery (within kReinforceSiteTiles of it).
    int beside = 0;
    for(const StructureBase* t : rocketTurrets(f.own))
        beside += int(TurretFrontPolicy::tileDistance(t->getLocation().x, t->getLocation().y, 15, 33)
                      <= TurretFrontPolicy::kReinforceSiteTiles + 1);
    require(beside > 0, name + ": no turret stands beside the Refinery");
    record(name, countText(c) + "; quads " + std::to_string(startHealth) + "->" + std::to_string(quadHealth())
                 + "; " + std::to_string(beside) + " turrets beside the Refinery; cycles " + std::to_string(played));
}

// --- (b) AdvancingTurrets -----------------------------------------------------------------------

void advance(bool on, bool army) {
    const std::string name = std::string("advance-") + (!army ? "noarmy" : on ? "on" : "off");
    Fixture f = newGame(false, on, false, false);
    ownBase(f);
    enemyBase(f, 112);
    // An enemy Rocket Turret on the belt's way, north-east: no belt turret within its reach.
    const Coord enemyTurret(70, 14);
    place(f.enemy, Structure_WindTrap, 72, 12);
    place(f.enemy, Structure_RocketTurret, enemyTurret.x, enemyTurret.y);
    const int baseEast = 18;   // the Refinery's east edge: the base's first enemy-facing edge
    std::vector<Uint32> tanks;
    if(army)
        for(int i = 0; i < 8; ++i) tanks.push_back(unit(f.own, Unit_Tank, 34 + i % 2, 28 + i / 2)->getObjectID());
    const auto& c = counts(f, QuantBot::TurretFrontMode::Advance);
    // The belt's own turrets (placed by AdvancingTurrets orders), not every own turret.
    std::set<std::pair<int, int>> belt;
    auto furthestEast = [&] {
        int x = 0;
        for(const auto& at : belt) x = std::max(x, at.first);
        return x;
    };
    const int played = play(f, 12000, [&] {
        collectPlaced(f, QuantBot::TurretFrontMode::Advance, belt);
        return belt.size() >= 8 && furthestEast() >= baseEast + 6;
    });
    collectPlaced(f, QuantBot::TurretFrontMode::Advance, belt);
    if(!on) {
        require(totalOrders(f) == 0, name + ": front orders with the switch off");
        record(name, "no advance order in " + std::to_string(played) + " cycles");
        return;
    }
    SDL_Log("TURRET_FRONT_PROBE_DIAG: bot=%p own=%p counts advance=%s", static_cast<void*>(f.bot),
            static_cast<void*>(f.own), countText(counts(f, QuantBot::TurretFrontMode::Advance)).c_str());
    for(const auto& player : f.own->getPlayerList())
        SDL_Log("TURRET_FRONT_PROBE_DIAG: player %p", static_cast<void*>(player.get()));
    require(c.ordered > 0, name + ": no belt ordered with the army ahead (" + countText(c) + ")");
    require(belt.size() >= 8, name + ": the line did not stand, eight belt turrets wanted, "
                              + std::to_string(belt.size()) + " (" + countText(c) + ")");
    require(furthestEast() >= baseEast + 6, name + ": the line did not move forward, furthest belt turret at x="
                                                + std::to_string(furthestEast()));
    // The line is a line: at least three belt turrets stand at the same distance from the enemy.
    auto enemyDistance = [&](int x, int y) {
        int d = 1000;
        for(const StructureBase* s : structureList) {
            if(s->getOwner() != f.enemy) continue;
            const Coord l = s->getLocation(), z = s->getStructureSize();
            d = std::min(d, std::max(std::max({0, l.x - x, x - (l.x + z.x - 1)}), std::max({0, l.y - y, y - (l.y + z.y - 1)})));
        }
        return d;
    };
    std::map<int, int> depth;
    for(const auto& at : belt) ++depth[enemyDistance(at.first, at.second)];
    int widest = 0;
    for(const auto& entry : depth) widest = std::max(widest, entry.second);
    require(widest >= 3, name + ": the belt is a spear, not a line (" + std::to_string(widest) + " side by side)");
    for(const auto& at : belt)
        require(TurretFrontPolicy::tileDistance(at.first, at.second, enemyTurret.x, enemyTurret.y)
                    > TurretFrontPolicy::kEnemyTurretReachTiles,
                name + ": a belt turret stands within the enemy turret's reach");
    record(name, countText(c) + "; " + std::to_string(belt.size()) + " belt turrets, " + std::to_string(widest)
                 + " side by side, furthest x=" + std::to_string(furthestEast()) + " (base edge x="
                 + std::to_string(baseEast) + "); cycles " + std::to_string(played));
}

// --- (c) TurretCreep ----------------------------------------------------------------------------

void creep(bool on, bool covered) {
    const std::string name = std::string("creep-") + (covered ? "covered" : on ? "on" : "off");
    Fixture f = newGame(false, false, on, false);
    ownBase(f);
    enemyBase(f, 70);
    // An own outpost (a Radar and its power) 9 tiles west of the Quads guarding the enemy base: its
    // build range reaches tiles within a turret's range of them.
    place(f.own, Structure_WindTrap, 52, 30);
    place(f.own, Structure_Radar, 54, 30);
    std::vector<Uint32> quads;
    for(int i = 0; i < 4; ++i) {
        UnitBase* q = unit(f.enemy, Unit_Quad, 64 + i % 2, 30 + i / 2);
        q->doSetAttackMode(GUARD);
        quads.push_back(q->getObjectID());
    }
    if(covered) {
        // Every tile the outpost can build on within range of the Quads is within reach of both.
        place(f.enemy, Structure_WindTrap, 66, 24);
        place(f.enemy, Structure_RocketTurret, 61, 28);
        place(f.enemy, Structure_RocketTurret, 61, 33);
    }
    auto quadHealth = [&] { int sum = 0; for(Uint32 id : quads) sum += health(id); return sum; };
    const int startHealth = quadHealth();
    const auto& c = counts(f, QuantBot::TurretFrontMode::Creep);
    const int played = play(f, 9000, [&] { return c.engaged > 0 && quadHealth() < startHealth; });
    if(!on) {
        require(totalOrders(f) == 0, name + ": front orders with the switch off");
        record(name, "no creep order in " + std::to_string(played) + " cycles");
        return;
    }
    if(covered) {
        require(c.ordered == 0, name + ": creep ordered where two enemy turrets reach (" + countText(c) + ")");
        record(name, "no creep order in " + std::to_string(played) + " cycles");
        return;
    }
    require(c.ordered > 0, name + ": no creep order toward the defenders (" + countText(c) + ")");
    require(c.placed > 0, name + ": no creep turret stood at its site (" + countText(c) + ")");
    require(c.engaged > 0, name + ": no creep turret fired on a defender (" + countText(c) + ")");
    require(quadHealth() < startHealth, name + ": the defenders lost no health");
    int inRange = 0;
    for(const StructureBase* t : rocketTurrets(f.own))
        inRange += int(t->getLocation().x >= 56);
    require(inRange > 0, name + ": no own turret stands within reach of the defenders");
    record(name, countText(c) + "; defenders " + std::to_string(startHealth) + "->" + std::to_string(quadHealth())
                 + "; cycles " + std::to_string(played));
}

// --- (d) RearTurretRetire -----------------------------------------------------------------------

void retire(bool on) {
    const std::string name = std::string("retire-") + (on ? "on" : "off");
    Fixture f = newGame(false, false, false, on);
    // The base's front is at x 30..40 (Construction Yard, Refinery, power, 4 turrets); its rear,
    // x 2..12, holds 20 turrets and a Palace with 2 beside it. The enemy is at x 118.
    auto* yard = static_cast<BuilderBase*>(place(f.own, Structure_ConstructionYard, 34, 28));
    yard->curUpgradeLev = yard->getMaxUpgradeLevel();
    yard->updateBuildList();
    place(f.own, Structure_Refinery, 37, 32);
    for(int i = 0; i < 8; ++i) place(f.own, Structure_WindTrap, 30 + 2 * (i % 2), 22 + 3 * (i / 2));
    place(f.own, Structure_Palace, 4, 50);
    enemyBase(f, 118);
    std::set<Uint32> rear, front, palace;
    for(int i = 0; i < 20; ++i) rear.insert(place(f.own, Structure_RocketTurret, 2 + 2 * (i % 5), 8 + 3 * (i / 5))->getObjectID());
    for(int i = 0; i < 4; ++i) front.insert(place(f.own, Structure_RocketTurret, 42, 26 + 3 * i)->getObjectID());
    palace.insert(place(f.own, Structure_RocketTurret, 8, 50)->getObjectID());
    palace.insert(place(f.own, Structure_RocketTurret, 8, 53)->getObjectID());
    auto standing = [&](const std::set<Uint32>& ids) {
        int n = 0;
        for(Uint32 id : ids) n += int(health(id) > 0);
        return n;
    };
    // Two reviews (kRetirePeriodCycles each) and a margin.
    const int cycles = int(2 * TurretFrontPolicy::kRetirePeriodCycles) + 600;
    play(f, cycles, [] { return false; });
    const int rearLeft = standing(rear), frontLeft = standing(front), palaceLeft = standing(palace);
    if(!on) {
        require(rearLeft == 20 && frontLeft == 4 && palaceLeft == 2 && f.bot->turretRetired == 0,
                name + ": a turret was retired with the switch off");
        record(name, "all 26 turrets stand after " + std::to_string(cycles) + " cycles");
        return;
    }
    require(f.bot->turretRetired > 0, name + ": no rear turret retired");
    require(f.bot->turretRetired <= 3 * TurretFrontPolicy::kRetirePerReview,
            name + ": more than four turrets retired a review");
    require(20 - rearLeft == f.bot->turretRetired, name + ": the retired turrets are not the rear ones");
    require(frontLeft == 4, name + ": a front turret was retired");
    require(palaceLeft == 2, name + ": a turret beside the Palace was retired");
    record(name, "retired " + std::to_string(f.bot->turretRetired) + " of 20 rear turrets in " + std::to_string(cycles)
                 + " cycles; front 4/4 and Palace 2/2 stand");
}

// --- (e) TurretDrill ---------------------------------------------------------------------------

void drill(bool on, bool fireOnBuildings, bool far) {
    const std::string name = std::string("drill-") + (far ? "far" : !on ? "off" : fireOnBuildings ? "buildings" : "on");
    Fixture f = newGame(false, false, false, false, on, fireOnBuildings);
    ownBase(f);
    const int ex = far ? 110 : 34;
    enemyBase(f, ex);
    std::vector<Uint32> enemyBuildings;
    for(const StructureBase* s : structureList)
        if(s->getOwner() == f.enemy) enemyBuildings.push_back(s->getObjectID());
    std::vector<Uint32> quads;
    for(int i = 0; i < 4; ++i) {
        UnitBase* q = unit(f.enemy, Unit_Quad, ex - 2, 28 + 2 * i);
        q->doSetAttackMode(GUARD);
        quads.push_back(q->getObjectID());
    }
    auto sum = [&](const std::vector<Uint32>& ids) { int total = 0; for(Uint32 id : ids) total += health(id); return total; };
    const int quadStart = sum(quads), buildingStart = sum(enemyBuildings);
    const auto& c = counts(f, QuantBot::TurretFrontMode::Drill);
    // The nearest any own turret stands to an enemy building tile, and how many touch another.
    auto mass = [&](int& nearest, int& touching) {
        nearest = 1000; touching = 0;
        const auto turrets = rocketTurrets(f.own);
        for(const StructureBase* t : turrets) {
            const Coord at = t->getLocation();
            for(Uint32 id : enemyBuildings) {
                const auto* b = dynamic_cast<const StructureBase*>(currentGame->getObjectManager().getObject(id));
                if(b == nullptr) continue;
                const Coord bl = b->getLocation(), bs = b->getStructureSize();
                const int dx = std::max({0, bl.x - at.x, at.x - (bl.x + bs.x - 1)});
                const int dy = std::max({0, bl.y - at.y, at.y - (bl.y + bs.y - 1)});
                nearest = std::min(nearest, std::max(dx, dy));
            }
            for(const StructureBase* o : turrets)
                if(o != t && TurretFrontPolicy::tileDistance(at.x, at.y, o->getLocation().x, o->getLocation().y) == 1) {
                    ++touching;
                    break;
                }
        }
    };
    int nearest = 1000, touching = 0, onBuildings = 0, onUnits = 0;
    const int played = play(f, 12000, [&] {
        mass(nearest, touching);
        sampleTargets(f, onBuildings, onUnits);
        const bool reached = nearest <= 7 && touching >= 4 && c.placed >= 8;
        return reached && sum(quads) == 0 && (!fireOnBuildings || onBuildings > 0);
    });
    mass(nearest, touching);
    if(!on) {
        require(totalOrders(f) == 0, name + ": front orders with the switch off");
        record(name, "no drill order in " + std::to_string(played) + " cycles");
        return;
    }
    if(far) {
        require(c.ordered == 0, name + ": the drill started with the enemy base 90 tiles away");
        record(name, "no drill order in " + std::to_string(played) + " cycles");
        return;
    }
    require(c.ordered >= 8 && c.placed >= 8, name + ": the drill did not build a mass, eight wanted (" + countText(c) + ")");
    require(nearest <= 7, name + ": the mass did not reach the enemy base, nearest turret " + std::to_string(nearest)
                          + " tiles from an enemy building");
    require(touching >= 4, name + ": the mass is not dense, " + std::to_string(touching) + " turrets touch another");
    require(c.engaged > 0, name + ": no drill turret fired on a unit (" + countText(c) + ")");
    require(sum(quads) < quadStart, name + ": the defenders lost no health");
    // The rule, on the turrets themselves (the bot's army may hit buildings either way).
    if(fireOnBuildings)
        require(onBuildings > 0, name + ": no own turret fired on an enemy building with Turrets Can Fire On Buildings on");
    else
        require(onBuildings == 0, name + ": an own turret targeted an enemy building with the rule off");
    record(name, countText(c) + "; nearest " + std::to_string(nearest) + " tiles, " + std::to_string(touching)
                 + " touching; defenders " + std::to_string(quadStart) + "->" + std::to_string(sum(quads))
                 + "; enemy buildings " + std::to_string(buildingStart) + "->" + std::to_string(sum(enemyBuildings))
                 + "; turret targets: " + std::to_string(onUnits) + " units, " + std::to_string(onBuildings) + " buildings"
                 + "; cycles " + std::to_string(played));
}

} // namespace TurretFrontProbe

int runTurretFrontProbe() {
    using namespace TurretFrontProbe;
    const char* outEnv = std::getenv("TURRET_FRONT_OUT");
    require(outEnv != nullptr, "Missing TURRET_FRONT_OUT");
    gOut = outEnv;
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    settings.general.playerName = "Turret tester";
    gModOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    const char* caseEnv = std::getenv("TURRET_FRONT_CASE");
    const std::string only = caseEnv ? caseEnv : "";
    gChecks.open(gOut + "/checks.csv");
    gChecks << "check,detail\n";
    auto want = [&](const char* name) { return only.empty() || only == name; };
    if(want("reinforce-on")) reinforce(true, false);
    if(want("reinforce-off")) reinforce(false, false);
    if(want("reinforce-far")) reinforce(true, true);
    if(want("advance-on")) advance(true, true);
    if(want("advance-off")) advance(false, true);
    if(want("advance-noarmy")) advance(true, false);
    if(want("creep-on")) creep(true, false);
    if(want("creep-off")) creep(false, false);
    if(want("creep-covered")) creep(true, true);
    if(want("retire-on")) retire(true);
    if(want("retire-off")) retire(false);
    if(want("drill-on")) drill(true, false, false);
    if(want("drill-buildings")) drill(true, true, false);
    if(want("drill-off")) drill(false, false, false);
    if(want("drill-far")) drill(true, false, true);
    gChecks.close();
    SDL_Log("TURRET_FRONT_PROBE_PASS");
    return -1;
}
