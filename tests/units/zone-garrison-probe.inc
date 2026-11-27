// Zone Garrisons (docs/zone-garrisons.md): Soldiers and Troopers sent with the capture order occupy
// Residential, Commercial and Industrial zones. Each case below runs the real capture command and
// Game::updateGameState on a 64x64 Dune City map and checks what changed:
//
// own zone      an own Soldier walks in; the zone keeps its owner; four fit and a fifth is refused
// take          an own Trooper walks into an empty enemy zone: the zone, its tiles, its density and
//               its power draw become ours, the enemy's count drops
// fire out      a garrison shoots an enemy Soldier standing in its range, crediting the Trooper
// assault       three Troopers ordered to capture a zone one enemy Soldier holds shoot it out and take it
// evacuate      the owner's Evacuate command brings every soldier out beside the zone; an enemy's
//               command is refused
// destroyed     soldiers come out of a zone that is destroyed around them, each losing half its
//               maximum hit points
// return        a captured zone goes back to its owner (count, power draw, tiles) when its garrison is
//               evacuated or killed; an own zone stays
// low health    below the evacuation threshold the garrison leaves on its own and a captured zone goes
//               back; nobody enters or takes a zone that damaged
// ally          an ally's zone is refused (it is never taken or shared)
// containment   transport recovery passes do not pull soldiers out of a zone
// save          a captured, garrisoned zone saves and loads with its owner, original owner and
//               occupants; they come out after the load and the zone goes back
// rule off      with Zone Garrisons off, a capture order on an own zone does nothing and an enemy zone
//               is not entered
// picture       garrison.png: a garrisoned zone beside an empty one, for a person to look at
//
// The player's own path (CLAUDE.md, "Player features"): units selected as a click selects them, the
// unit panel's real Capture button pressed (and the C key), the zone clicked with a real SDL mouse
// event through Game::doInput, game cycles run, and the zone panel's real Evacuate button pressed:
// player own zone    one Soldier: button, cursor, click, inside, panel count, Evacuate, out
// player take        a Soldier and a Trooper selected together take an empty enemy zone with the C key;
//                    the panel says captured; Evacuate hands the zone back
// player refused     a full own zone, an ally's zone (the cursor refuses the click) and a zone below the
//                    threshold are not entered; an enemy's garrisoned zone shows Evacuate disabled
// garrison button    the unit panel's Garrison button (and the I key) for Soldiers and Troopers, hidden for
//                    a tank and with the rule off; the cursor shows the Garrison symbol over an own or enemy
//                    zone and a pointer over sand; a click on sand is refused, on an own zone garrisons it
// reinforce          a Trooper joins a Soldier's own garrison and a captured zone's garrison (mixed)
// trooper air/tank   a garrisoned Trooper hits an Ornithopter and a tank with its own rockets
// no pull            a soldier on its way in keeps its order against every other controller's order
//                    (move, hunt, attack, formation, repair, Carryall, attack mode); the garrison
//                    controller's own order is obeyed
// price              the Soldier costs 30 credits in the Barracks build list
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <Command.h>
#include <ScreenBorder.h>
#include <GameInterface.h>
#include <GUI/ObjectInterfaces/MultiUnitInterface.h>
#include <GUI/ObjectInterfaces/UnitInterface.h>
#include <GUI/ObjectInterfaces/ZoneStructureInterface.h>
#include <FileClasses/LoadSavePNG.h>
#include <mod/ModManager.h>
#include <players/Player.h>
#include <structures/ZoneStructure.h>
#include <structures/BuilderBase.h>
#include <units/InfantryBase.h>
#include <units/UnitBase.h>
#include <dunecity/ZoneGarrisonPolicy.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ZoneGarrisonProbe {

inline void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("ZONE_GARRISON_PROBE_FAIL: %s", why.c_str());
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
    // The MCVs keep the houses alive, in far corners.
    data += "[Player1]\nCredits=2000\nBrain=Team1\n[Player2]\nCredits=2000\nBrain=Team2\n"
            "[Player3]\nCredits=2000\nBrain=Team1\n"
            "[UNITS]\nID001=Player1,MCV,256,0,64,Guard\nID002=Player2,MCV,256,"
            + std::to_string(kMapSize * kMapSize - 1) + ",64,Guard\nID003=Player3,MCV,256,"
            + std::to_string(kMapSize - 1) + ",64,Guard\n";
    return data;
}

/// Atreides (local, team 1) and its ally Ordos (team 1) against Harkonnen (team 2).
struct Fixture {
    std::unique_ptr<Game> game;
    House* own = nullptr;
    House* enemy = nullptr;
    House* ally = nullptr;
    std::string label;

    Fixture(const std::string& name, bool rule = true) : label(name) {
        auto options = gModOptions;
        options.zoneGarrisons = rule;
        options.immortalHumanPlayer = false;
        GameInitSettings init("zone-garrison.ini", fixtureMap(), "Zone garrison", true, options);
        init.randomSeed = 17;
        GameInitSettings::HouseInfo a(HOUSE_ATREIDES, 1);
        a.addPlayerInfo({"Garrison tester", "HumanPlayer"});
        init.addHouseInfo(a);
        GameInitSettings::HouseInfo h(HOUSE_HARKONNEN, 2);
        h.addPlayerInfo({"Enemy", "HumanPlayer"});
        init.addHouseInfo(h);
        GameInitSettings::HouseInfo o(HOUSE_ORDOS, 1);
        o.addPlayerInfo({"Ally", "HumanPlayer"});
        init.addHouseInfo(o);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;   // Private fixture, never a public match-analytics sample.
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        own = game->getHouse(HOUSE_ATREIDES);
        enemy = game->getHouse(HOUSE_HARKONNEN);
        ally = game->getHouse(HOUSE_ORDOS);
        require(own && enemy && ally, label + ": fixture houses missing");
        require(game->isCitySimEnabled(), label + ": the fixture is not a city game");
        require(game->getGameInitSettings().getGameOptions().zoneGarrisons == rule,
                label + ": the game did not keep the Zone Garrisons rule of its arm");
    }

    ~Fixture() {
        game.reset();
        currentGame = nullptr;
    }

    ZoneStructure* zone(House* house, int item, int x, int y, int level = 2) {
        auto* z = dynamic_cast<ZoneStructure*>(house->placeStructure(NONE_ID, item, x, y, true, true));
        require(z != nullptr, label + ": cannot place a zone at " + std::to_string(x) + "," + std::to_string(y));
        z->setStartingLevel(level);
        z->setVisible(VIS_ALL, true);
        return z;
    }

    UnitBase* unit(House* house, int item, int x, int y, ATTACKMODE mode = GUARD) {
        auto* u = house->createUnit(item, true);
        require(u != nullptr, label + ": cannot create a unit");
        u->deploy(Coord(x, y));
        u->setVisible(VIS_ALL, true);
        u->doSetAttackMode(mode);
        return u;
    }

    Uint8 playerOf(const House* house) const {
        return house->getPlayerList().front()->getPlayerID();
    }

    void capture(const UnitBase* soldier, const ZoneStructure* z) {
        Command(playerOf(soldier->getOwner()), CMD_INFANTRY_CAPTURE, soldier->getObjectID(), z->getObjectID())
            .executeCommand();
    }

    bool alive(Uint32 id) const { return game->getObjectManager().getObject(id) != nullptr; }

    void run(int seconds) {
        const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(seconds * 1000);
        while(game->getGameCycleCount() < end) game->updateGameState();
    }

    /// Runs until DONE or SECONDS of game time; returns whether DONE happened.
    template<class F> bool runUntil(int seconds, F done) {
        const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(seconds * 1000);
        while(game->getGameCycleCount() < end) {
            if(done()) return true;
            game->updateGameState();
        }
        return done();
    }
};

void record(const std::string& name, const std::string& detail) {
    SDL_Log("ZONE_GARRISON_CASE: %s %s", name.c_str(), detail.c_str());
}

bool hidden(const UnitBase* u) {
    return !u->isActive() && u->isPickedUp() && !u->isVisible() && !u->isRespondable();
}

bool onMapBeside(const UnitBase* u, const ZoneStructure* z, int reach) {
    const Coord at = u->getLocation();
    return u->isActive() && !u->isPickedUp() && currentGameMap->tileExists(at)
        && blockDistance(at, z->getClosestPoint(at)) <= reach;
}

void checkOwnZone() {
    Fixture f("own zone");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 20, 20);
    std::vector<UnitBase*> soldiers;
    for(int i = 0; i < 5; ++i) soldiers.push_back(f.unit(f.own, i % 2 ? Unit_Trooper : Unit_Soldier, 14, 18 + i));
    f.capture(soldiers[0], z);
    require(soldiers[0]->getAttackMode() == CAPTURE, "own zone: the capture order did not take");
    require(f.runUntil(30, [&] { return z->getGarrisonSize() == 1; }), "own zone: the Soldier never entered");
    require(hidden(soldiers[0]), "own zone: the Soldier inside is still on the map");
    require(z->getOwner() == f.own && z->getGarrisonHouse() == f.own, "own zone: the zone changed hands");
    for(int i = 1; i < 4; ++i) f.capture(soldiers[i], z);
    require(f.runUntil(40, [&] { return z->getGarrisonSize() == 4; }), "own zone: four soldiers did not fit");
    f.capture(soldiers[4], z);
    require(soldiers[4]->getAttackMode() != CAPTURE, "own zone: a capture order on a full zone was taken");
    f.run(10);
    require(z->getGarrisonSize() == DuneCity::ZoneGarrison::kCapacity && soldiers[4]->isActive(),
            "own zone: a fifth soldier entered");
    record("own_zone", "4 entered, the 5th refused, owner kept");
}

void checkTake() {
    Fixture f("take");
    auto* z = f.zone(f.enemy, Structure_ZoneCommercial, 40, 20, 2);
    // Beside the zone and standing still, so the capture command takes it in this very cycle: the
    // city simulation may grow or shrink an enemy lot while a soldier walks over.
    auto* trooper = f.unit(f.own, Unit_Trooper, 39, 20);
    f.run(1);
    const int enemyZones = f.enemy->getNumItems(Structure_ZoneCommercial);
    const int ownZones = f.own->getNumItems(Structure_ZoneCommercial);
    const int draw = z->getZonePowerDraw();
    const int enemyRequirement = f.enemy->getPowerRequirement();
    const int ownRequirement = f.own->getPowerRequirement();
    const int density = currentGameMap->getTile(40, 20)->getCityZoneDensity();
    const FixPoint health = z->getHealth();
    f.capture(trooper, z);
    require(z->getOwner() == f.own, "take: the empty enemy zone was not taken by a Trooper beside it");
    require(z->getGarrisonSize() == 1 && hidden(trooper), "take: the Trooper is not inside");
    require(f.enemy->getNumItems(Structure_ZoneCommercial) == enemyZones - 1
                && f.own->getNumItems(Structure_ZoneCommercial) == ownZones + 1,
            "take: the zone counts did not move between the houses");
    for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
        require(currentGameMap->getTile(40 + dx, 20 + dy)->getOwner() == HOUSE_ATREIDES, "take: a tile kept its owner");
    require(currentGameMap->getTile(40, 20)->getCityZoneDensity() == density, "take: the zone lost its density");
    require(z->getZonePowerDraw() == draw && f.enemy->getPowerRequirement() == enemyRequirement - draw
                && f.own->getPowerRequirement() == ownRequirement + draw,
            "take: the power draw did not move with the zone");
    require(z->getHealth() == health, "take: the zone was damaged by its capture");
    // And a Trooper that has to walk over takes one too.
    auto* far = f.zone(f.enemy, Structure_ZoneIndustrial, 40, 30, 1);
    auto* walker = f.unit(f.own, Unit_Trooper, 33, 30);
    f.capture(walker, far);
    require(f.runUntil(30, [&] { return far->getOwner() == f.own; }), "take: a walking Trooper never took the zone");
    record("take", "density " + std::to_string(density) + ", draw " + std::to_string(draw)
           + " moved with the zone, counts moved; a walking Trooper took a second zone");
}

void checkFireOut() {
    Fixture f("fire out");
    auto* z = f.zone(f.own, Structure_ZoneIndustrial, 20, 30);
    auto* a = f.unit(f.own, Unit_Trooper, 19, 29);
    auto* b = f.unit(f.own, Unit_Trooper, 22, 29);
    f.capture(a, z);
    f.capture(b, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 2; }), "fire out: the Troopers did not enter");
    auto* target = f.unit(f.enemy, Unit_Soldier, 25, 31, STOP);
    const Uint32 targetID = target->getObjectID();
    const int seconds0 = f.game->getGameCycleCount();
    require(f.runUntil(40, [&] { return !f.alive(targetID); }), "fire out: the garrison never killed the Soldier in its range");
    const auto& reward = f.own->getCombatReward(Unit_Trooper);
    require(reward.kills >= 1 && reward.hpRemovedMilli > 0, "fire out: the kill was not credited to the Troopers");
    require(z->getGarrisonSize() == 2, "fire out: a Trooper left or died");
    record("fire_out", "killed after " + std::to_string((f.game->getGameCycleCount() - seconds0) * GAMESPEED_DEFAULT / 1000)
           + " s, credited to Trooper");
}

void checkAssault() {
    Fixture f("assault");
    auto* z = f.zone(f.enemy, Structure_ZoneResidential, 40, 40);
    auto* defender = f.unit(f.enemy, Unit_Soldier, 39, 39);
    f.capture(defender, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), "assault: the defender did not enter");
    const Uint32 defenderID = defender->getObjectID();
    std::vector<UnitBase*> attackers;
    for(int i = 0; i < 3; ++i) attackers.push_back(f.unit(f.own, Unit_Trooper, 33, 39 + i));
    std::vector<Uint32> attackerIDs;
    for(auto* a : attackers) {
        f.capture(a, z);
        require(a->getAttackMode() == CAPTURE, "assault: a capture order on a held zone was refused");
        attackerIDs.push_back(a->getObjectID());
    }
    // While the zone is held nobody may walk in.
    f.run(2);
    require(z->getOwner() == f.enemy, "assault: the zone changed hands while held");
    require(f.runUntil(120, [&] { return z->getOwner() == f.own; }), "assault: the attackers never took the held zone");
    require(!f.alive(defenderID), "assault: the defender survived the assault");
    require(z->getGarrisonHouse() == f.own, "assault: the zone is not held by the attackers");
    int inside = 0;
    for(const Uint32 id : attackerIDs) {
        const auto* a = dynamic_cast<const UnitBase*>(f.game->getObjectManager().getObject(id));
        if(a != nullptr && hidden(a)) ++inside;
    }
    record("assault", "zone taken, defender dead, " + std::to_string(inside) + " of 3 Troopers inside, zone health "
           + std::to_string(z->getHealth().lround()) + "/" + std::to_string(z->getMaxHealth()));
}

void checkEvacuate() {
    Fixture f("evacuate");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 30, 30);
    std::vector<UnitBase*> soldiers;
    for(int i = 0; i < 3; ++i) {
        soldiers.push_back(f.unit(f.own, Unit_Soldier, 29 + i, 29));
        f.capture(soldiers.back(), z);
    }
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 3; }), "evacuate: the soldiers did not enter");
    Command(f.playerOf(f.enemy), CMD_ZONE_EVACUATE, z->getObjectID()).executeCommand();
    require(z->getGarrisonSize() == 3, "evacuate: an enemy's Evacuate command was obeyed");
    Command(f.playerOf(f.own), CMD_ZONE_EVACUATE, z->getObjectID()).executeCommand();
    require(!z->isGarrisoned() && z->getOwner() == f.own, "evacuate: the zone is not empty and ours");
    for(auto* s : soldiers) require(onMapBeside(s, z, 2) && s->isRespondable() && s->isVisible(),
                                    "evacuate: a soldier did not come out beside the zone");
    f.run(3);
    for(auto* s : soldiers) require(s->isActive() && s->getHealth() > 0, "evacuate: a soldier did not survive coming out");
    record("evacuate", "3 out beside the zone, the enemy's command refused");
}

void checkDestroyed() {
    Fixture f("destroyed");
    auto* z = f.zone(f.own, Structure_ZoneCommercial, 30, 40);
    std::vector<UnitBase*> soldiers;
    for(int i = 0; i < 2; ++i) {
        soldiers.push_back(f.unit(f.own, Unit_Soldier, 29 + i, 39));
        f.capture(soldiers.back(), z);
    }
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 2; }), "destroyed: the soldiers did not enter");
    const Uint32 zoneID = z->getObjectID();
    std::vector<Uint32> ids;
    std::vector<int> before;
    for(auto* s : soldiers) {
        ids.push_back(s->getObjectID());
        before.push_back(s->getHealth().lround());
    }
    // The building's own damage path, without the garrison's cover split: a hit this large would
    // also kill the soldier at the front (cover, not immunity), and this case is about coming out.
    z->StructureBase::handleDamage(100000, NONE_ID, f.enemy);
    require(f.runUntil(5, [&] { return !f.alive(zoneID); }), "destroyed: the zone did not go");
    std::string hurt;
    for(size_t i = 0; i < ids.size(); ++i) {
        auto* s = dynamic_cast<UnitBase*>(f.game->getObjectManager().getObject(ids[i]));
        require(s != nullptr && s->isActive() && !s->isPickedUp() && currentGameMap->tileExists(s->getLocation()),
                "destroyed: a soldier did not come out of the ruin");
        require(s->getHealth().lround() == DuneCity::ZoneGarrison::healthAfterEjection(before[i], s->getMaxHealth())
                    && s->getHealth() > 0 && s->getHealth().lround() < before[i],
                "destroyed: a soldier thrown out of the ruin did not lose half its hit points");
        hurt += " " + std::to_string(before[i]) + "->" + std::to_string(s->getHealth().lround());
    }
    record("destroyed", "both soldiers out of the ruin, hurt:" + hurt);
}

/// A captured zone goes back to the house it was taken from once its garrison is gone: by the
/// Evacuate command, or when the last soldier inside is killed. An own zone stays.
void checkReturn() {
    Fixture f("return");
    auto* z = f.zone(f.enemy, Structure_ZoneCommercial, 40, 20, 2);
    auto* trooper = f.unit(f.own, Unit_Trooper, 39, 20);
    f.run(1);
    const int enemyZones = f.enemy->getNumItems(Structure_ZoneCommercial);
    const int enemyRequirement = f.enemy->getPowerRequirement();
    f.capture(trooper, z);
    require(z->getOwner() == f.own && z->getGarrisonOriginalOwner() == HOUSE_HARKONNEN,
            "return: the zone was not taken, or does not know whom it was taken from");
    Command(f.playerOf(f.own), CMD_ZONE_EVACUATE, z->getObjectID()).executeCommand();
    require(!z->isGarrisoned() && onMapBeside(trooper, z, 2), "return: the Trooper did not come out");
    require(z->getOwner() == f.enemy && z->getGarrisonOriginalOwner() < 0,
            "return: the evacuated captured zone did not go back to its owner");
    require(f.enemy->getNumItems(Structure_ZoneCommercial) == enemyZones
                && f.enemy->getPowerRequirement() == enemyRequirement,
            "return: the zone count or power draw did not come back with the zone");
    for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
        require(currentGameMap->getTile(40 + dx, 20 + dy)->getOwner() == HOUSE_HARKONNEN, "return: a tile kept the holder");
    // Taken again, and this time the soldier inside is killed.
    auto* soldier = f.unit(f.own, Unit_Soldier, 39, 21);
    f.run(1);
    f.capture(soldier, z);
    require(z->getOwner() == f.own && z->getGarrisonSize() == 1, "return: the zone was not taken a second time");
    soldier->ObjectBase::handleDamage(1000, NONE_ID, f.enemy);
    f.run(1);
    require(!z->isGarrisoned() && z->getOwner() == f.enemy, "return: a captured zone whose garrison died did not go back");
    // An own zone the garrison leaves stays its owner's.
    auto* mine = f.zone(f.own, Structure_ZoneResidential, 20, 20);
    auto* guard = f.unit(f.own, Unit_Soldier, 19, 20);
    f.capture(guard, mine);
    require(f.runUntil(20, [&] { return mine->getGarrisonSize() == 1; }), "return: the soldier did not enter its own zone");
    Command(f.playerOf(f.own), CMD_ZONE_EVACUATE, mine->getObjectID()).executeCommand();
    require(mine->getOwner() == f.own && !mine->isGarrisoned(), "return: an own zone changed hands when its garrison left");
    record("return", "evacuated and killed garrisons hand captured zones back (count, power, tiles); an own zone stays");
}

/// Below the evacuation threshold the garrison leaves on its own (a captured zone goes back as it
/// does), and nobody may enter or take a zone that damaged.
void checkLowHealth() {
    Fixture f("low health");
    const int percent = ZoneStructure::evacuateHealthPercent();
    require(percent == DuneCity::ZoneGarrison::kEvacuateHealthPercent, "low health: the threshold is overridden");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 20, 30);
    std::vector<UnitBase*> soldiers;
    for(int i = 0; i < 2; ++i) {
        soldiers.push_back(f.unit(f.own, i ? Unit_Trooper : Unit_Soldier, 19, 29 + i));
        f.capture(soldiers.back(), z);
    }
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 2; }), "low health: the soldiers did not enter");
    const int max = z->getMaxHealth();
    const int atLine = (max * percent + 99) / 100;   // the lowest health still held
    z->setHealth(atLine);
    f.run(1);
    require(z->getGarrisonSize() == 2, "low health: the garrison left at " + std::to_string(atLine) + "/" + std::to_string(max));
    z->setHealth(atLine - 1);
    f.run(1);
    require(!z->isGarrisoned() && z->getOwner() == f.own, "low health: the garrison stayed in a zone below the threshold");
    for(auto* s : soldiers) require(onMapBeside(s, z, 2) && s->getHealth() > 0, "low health: a soldier did not come out");
    f.capture(soldiers[0], z);
    f.run(5);
    require(!z->isGarrisoned(), "low health: a soldier entered a zone below the threshold");
    // A captured zone driven below the threshold goes back to its owner, and cannot be taken again.
    auto* foe = f.zone(f.enemy, Structure_ZoneIndustrial, 40, 30, 2);
    auto* trooper = f.unit(f.own, Unit_Trooper, 39, 30);
    f.run(1);
    f.capture(trooper, foe);
    require(foe->getOwner() == f.own, "low health: the enemy zone was not taken");
    foe->StructureBase::handleDamage(foe->getHealth().lround() - atLine + 1, NONE_ID, f.enemy);
    f.run(1);
    require(!foe->isGarrisoned() && foe->getOwner() == f.enemy && onMapBeside(trooper, foe, 2),
            "low health: a captured zone below the threshold was not left and handed back");
    f.capture(trooper, foe);
    f.run(5);
    require(foe->getOwner() == f.enemy && !foe->isGarrisoned(), "low health: a zone below the threshold was taken");
    record("low_health", "held at " + std::to_string(atLine) + "/" + std::to_string(max) + " (" + std::to_string(percent)
           + "%), left below it; a captured zone went back; no entry below it");
}

void checkAlly() {
    Fixture f("ally");
    auto* z = f.zone(f.ally, Structure_ZoneResidential, 50, 10);
    auto* s = f.unit(f.own, Unit_Soldier, 47, 10);
    f.capture(s, z);
    f.run(15);
    require(!z->isGarrisoned() && z->getOwner() == f.ally && s->isActive(), "ally: an ally's zone was entered or taken");
    record("ally", "refused");
}

void checkContainment() {
    Fixture f("containment");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 20, 44);
    auto* s = f.unit(f.own, Unit_Soldier, 19, 43);
    f.capture(s, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), "containment: the soldier did not enter");
    f.run(3 * Game::transportContainmentIntervalCycles * GAMESPEED_DEFAULT / 1000 + 5);
    require(z->getGarrisonSize() == 1 && hidden(s), "containment: a recovery pass pulled the soldier out of the zone");
    record("containment", "3 recovery passes, still inside");
}

void checkSave() {
    const std::string savePath = gOut + "/zone-garrison.dls";
    Uint32 zoneID = NONE_ID;
    std::vector<Uint32> occupants;
    {
        Fixture f("save");
        auto* z = f.zone(f.enemy, Structure_ZoneIndustrial, 44, 30);
        auto* a = f.unit(f.own, Unit_Trooper, 43, 29);
        auto* b = f.unit(f.own, Unit_Soldier, 46, 29);
        f.capture(a, z);
        require(f.runUntil(20, [&] { return z->getOwner() == f.own; }), "save: the zone was not taken");
        f.capture(b, z);
        require(f.runUntil(20, [&] { return z->getGarrisonSize() == 2; }), "save: the second soldier did not enter");
        zoneID = z->getObjectID();
        occupants = z->getGarrison();
        require(f.game->saveGame(savePath), "save: cannot save");
    }
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(savePath));
    game->bReplay = false;
    game->initializeGameLoop();
    require(game->getLoadedSavegameVersion() >= SAVEGAMEVERSION_ZONE_GARRISON, "save: the save predates Zone Garrisons");
    auto* z = dynamic_cast<ZoneStructure*>(game->getObjectManager().getObject(zoneID));
    require(z != nullptr, "save: the zone is missing after the load");
    require(z->getOwner() == game->getHouse(HOUSE_ATREIDES), "save: the captured zone lost its new owner");
    require(z->getGarrison() == occupants, "save: the garrison changed in the load");
    require(z->getGarrisonOriginalOwner() == HOUSE_HARKONNEN, "save: the captured zone forgot its original owner");
    for(const Uint32 id : occupants) {
        auto* u = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(id));
        require(u != nullptr && hidden(u), "save: a loaded occupant is on the map");
    }
    for(int i = 0; i < MILLI2CYCLES(3000); ++i) game->updateGameState();
    require(z->getGarrison() == occupants, "save: the loaded garrison did not hold");
    require(z->evacuateGarrison() == 2, "save: the loaded garrison did not come out");
    require(z->getOwner() == game->getHouse(HOUSE_HARKONNEN) && z->getGarrisonOriginalOwner() < 0,
            "save: the loaded captured zone did not go back to its owner when its garrison left");
    for(const Uint32 id : occupants) {
        auto* u = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(id));
        require(u != nullptr && onMapBeside(u, z, 2), "save: a loaded occupant did not come out beside the zone");
    }
    game.reset();
    currentGame = nullptr;
    record("save", "owner, original owner, 2 occupants and their exit (the zone goes back) survive save and load");
}

void checkRuleOff() {
    Fixture f("rule off", false);
    auto* own = f.zone(f.own, Structure_ZoneResidential, 20, 20);
    auto* foe = f.zone(f.enemy, Structure_ZoneResidential, 30, 20);
    auto* s = f.unit(f.own, Unit_Soldier, 17, 20);
    auto* t = f.unit(f.own, Unit_Soldier, 27, 18);
    f.capture(s, own);
    require(s->getAttackMode() != CAPTURE, "rule off: a capture order on an own zone was taken");
    f.capture(t, foe);
    f.run(20);
    require(!own->isGarrisoned() && !foe->isGarrisoned() && foe->getOwner() == f.enemy,
            "rule off: a zone was garrisoned or an undamaged enemy zone taken");
    record("rule_off", "own zone refused, enemy zone not entered");
}

void checkPicture() {
    Fixture f("picture");
    auto* held = f.zone(f.own, Structure_ZoneResidential, 30, 30, 3);
    f.zone(f.own, Structure_ZoneResidential, 33, 30, 3);
    auto* taken = f.zone(f.enemy, Structure_ZoneCommercial, 30, 33, 2);
    f.zone(f.enemy, Structure_ZoneCommercial, 33, 33, 2);
    for(int i = 0; i < 3; ++i) {
        auto* s = f.unit(f.own, i ? Unit_Trooper : Unit_Soldier, 29, 29 + i);
        f.capture(s, held);
    }
    auto* t = f.unit(f.own, Unit_Trooper, 29, 35);
    f.capture(t, taken);
    require(f.runUntil(30, [&] { return held->getGarrisonSize() == 3 && taken->getOwner() == f.own; }),
            "picture: the garrisons did not form");
    f.run(1);
    for(int zoom = 0; zoom < 3; ++zoom) {
        currentZoomlevel = zoom;
        screenborder->adjustScreenBorderToMapsize(kMapSize, kMapSize);
        screenborder->setNewScreenCenter(Coord(32 * TILESIZE, 32 * TILESIZE));
        SDL_SetRenderTarget(renderer, screenTexture);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        f.game->drawScreen();
        auto image = renderReadSurface(renderer);
        SDL_SetRenderTarget(renderer, nullptr);
        require(image != nullptr, "picture: could not read back the rendered garrison");
        const std::string path = gOut + "/garrison-zoom" + std::to_string(zoom) + ".png";
        SavePNG(image.get(), path.c_str());
    }
    currentZoomlevel = 1;
    record("picture", "garrison-zoom0..2.png: 3 soldiers in an own R zone (left), 1 in a taken C zone (below)");
}


// --- The player's own path -------------------------------------------------------------------

int queued(Game& game, CMDTYPE type) {
    int count = 0;
    for(const auto& [cycle, slot] : game.cmdManager.timeslot)
        for(const auto& command : slot) if(command.getCommandID() == type) ++count;
    return count;
}

/// Selects UNITS as a selection click does and opens their real side panel.
ObjectInterface* selectUnits(Fixture& f, const std::vector<UnitBase*>& units) {
    Game& game = *f.game;
    require(game.pInterface != nullptr, f.label + ": the game has no interface");
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    for(UnitBase* u : units) {
        game.selectedList.insert(u->getObjectID());
        u->setSelected(true);
    }
    game.selectionChanged();
    game.setCursorMode(Game::CursorMode_Normal);
    game.pInterface->updateObjectInterface();
    return game.pInterface->pObjectContainer;
}

/// The Capture button of the unit panel the player sees for the selection.
SymbolButton* captureButtonOf(ObjectInterface* panel) {
    if(auto* one = dynamic_cast<UnitInterface*>(panel)) return &one->captureButton;
    if(auto* many = dynamic_cast<MultiUnitInterface*>(panel)) return &many->captureButton;
    return nullptr;
}

/// A real left-button press and release on the widget, as the mouse sends it.
void press(Widget& button) {
    button.handleMouseLeft(5, 5, true);
    button.handleMouseLeft(5, 5, false);
}

/// A real SDL mouse click on TILE of the map view, through Game::doInput.
void clickMap(Fixture& f, Coord tile, Uint8 button = SDL_BUTTON_LEFT) {
    currentGameMap->viewMap(f.own->getHouseID(), tile, 6);
    screenborder->setNewScreenCenter(Coord(tile.x * TILESIZE + TILESIZE / 2, tile.y * TILESIZE + TILESIZE / 2));
    const int x = screenborder->world2screenX(tile.x * TILESIZE + TILESIZE / 2);
    const int y = screenborder->world2screenY(tile.y * TILESIZE + TILESIZE / 2);
    require(screenborder->isScreenCoordInsideMap(x, y), f.label + ": the click point is outside the map view");
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_Event event{};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = button;
    event.button.x = x;
    event.button.y = y;
    require(SDL_PushEvent(&event) == 1, f.label + ": the mouse press was rejected");
    event.type = SDL_MOUSEBUTTONUP;
    require(SDL_PushEvent(&event) == 1, f.label + ": the mouse release was rejected");
    f.game->doInput();
}

/// What a unit and a zone are doing, for a failure message.
std::string describe(const std::vector<UnitBase*>& units, const ZoneStructure* z) {
    std::string text = "zone owner " + std::to_string(z->getOwner()->getHouseID()) + " inside "
        + std::to_string(z->getGarrisonSize()) + " health " + std::to_string(z->getHealth().lround())
        + " original " + std::to_string(z->getGarrisonOriginalOwner()) + ";";
    for(const UnitBase* u : units) {
        const Coord at = u->getLocation();
        text += " unit " + std::to_string(u->getObjectID()) + (hidden(u) ? " inside" : "") + " at "
            + std::to_string(at.x) + "," + std::to_string(at.y) + " mode " + std::to_string(int(u->getAttackMode()))
            + " target " + std::to_string(u->getTarget() ? u->getTarget()->getObjectID() : 0)
            + (u->isMoving() ? " moving" : " still") + " health " + std::to_string(u->getHealth().lround()) + ";";
    }
    return text;
}

/// Presses Capture on the selection's panel (or the C key) and clicks ZONE; returns the capture
/// commands the click queued. ACCEPTED: the click is on a target the cursor takes (an own zone or an
/// enemy's); a click on anything else leaves the Capture cursor open, as for every order cursor.
int captureByPlayer(Fixture& f, const std::vector<UnitBase*>& units, const ZoneStructure* zone, bool key = false,
                    bool accepted = true) {
    Game& game = *f.game;
    ObjectInterface* panel = selectUnits(f, units);
    SymbolButton* button = captureButtonOf(panel);
    require(button != nullptr, f.label + ": the selection has no unit panel with a Capture button");
    require(button->isVisible() && button->isEnabled(), f.label + ": the Capture button is not shown and enabled");
    require(button->getTooltipText().find(_("Capture")) != std::string::npos,
            f.label + ": the Capture button's tooltip does not say Capture");
    if(key) {
        SDL_KeyboardEvent keyEvent{};
        keyEvent.type = SDL_KEYDOWN;
        keyEvent.keysym.sym = SDLK_c;
        game.handleKeyInput(keyEvent);
        require(game.currentCursorMode == Game::CursorMode_Capture, f.label + ": the C key did not open the Capture cursor");
    } else {
        press(*button);
        require(game.currentCursorMode == Game::CursorMode_Capture, f.label + ": the Capture button did not open its cursor");
        panel->update();
        require(button->getToggleState(), f.label + ": the Capture button does not show it is active");
    }
    const int before = queued(game, CMD_INFANTRY_CAPTURE);
    clickMap(f, zone->getLocation());
    if(accepted) {
        require(game.currentCursorMode == Game::CursorMode_Normal, f.label + ": the Capture cursor stayed open after the click");
    } else {
        require(game.currentCursorMode == Game::CursorMode_Capture && queued(game, CMD_INFANTRY_CAPTURE) == before,
                f.label + ": a click on a target the cursor refuses queued an order or closed the cursor");
        game.setCursorMode(Game::CursorMode_Normal);   // as the player's right click cancels it
    }
    return queued(game, CMD_INFANTRY_CAPTURE) - before;
}

/// Selects ZONE with a real click on it and returns its real panel.
ZoneStructureInterface* selectZone(Fixture& f, const ZoneStructure* zone) {
    Game& game = *f.game;
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.setCursorMode(Game::CursorMode_Normal);
    clickMap(f, zone->getLocation());
    require(game.selectedList.size() == 1 && game.selectedList.count(zone->getObjectID()) == 1,
            f.label + ": a click on the zone did not select it");
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<ZoneStructureInterface*>(game.pInterface->pObjectContainer);
    require(panel != nullptr && panel->update(), f.label + ": selecting the zone did not open its panel");
    return panel;
}

void checkPlayerOwnZone() {
    Fixture f("player own zone");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 24, 24);
    auto* soldier = f.unit(f.own, Unit_Soldier, 20, 25);
    f.run(1);
    require(captureByPlayer(f, {soldier}, z) == 1, "player own zone: the click on an own zone queued no capture order");
    require(f.runUntil(30, [&] { return z->getGarrisonSize() == 1; }), "player own zone: the Soldier never went inside");
    require(hidden(soldier) && z->getOwner() == f.own, "player own zone: the Soldier is not hidden inside an own zone");
    auto* panel = selectZone(f, z);
    require(panel->garrisonLabel.isVisible() && panel->garrisonLabel.getText().find("1/4") != std::string::npos
                && panel->garrisonLabel.getText().find(_("(captured)")) == std::string::npos,
            "player own zone: the zone panel does not show Garrison 1/4: " + panel->garrisonLabel.getText());
    require(panel->evacuateButton.isVisible() && panel->evacuateButton.isEnabled(),
            "player own zone: the Evacuate button is not shown and enabled");
    const int before = queued(*f.game, CMD_ZONE_EVACUATE);
    press(panel->evacuateButton);
    require(queued(*f.game, CMD_ZONE_EVACUATE) == before + 1, "player own zone: Evacuate queued no command");
    require(f.runUntil(5, [&] { return !z->isGarrisoned(); }), "player own zone: Evacuate did not empty the zone");
    require(onMapBeside(soldier, z, 2) && soldier->isRespondable() && z->getOwner() == f.own,
            "player own zone: the Soldier did not come out beside its own zone");
    panel->update();
    require(!panel->evacuateButton.isVisible(), "player own zone: Evacuate is still shown on an empty zone");
    record("player_own_zone", "Capture button, map click, inside, panel 1/4, Evacuate button, out; zone kept");
}

void checkPlayerTake() {
    Fixture f("player take");
    auto* z = f.zone(f.enemy, Structure_ZoneCommercial, 40, 24, 2);
    auto* soldier = f.unit(f.own, Unit_Soldier, 35, 24);
    auto* trooper = f.unit(f.own, Unit_Trooper, 35, 25);
    f.run(1);
    require(captureByPlayer(f, {soldier, trooper}, z, true) == 2,
            "player take: the click on an empty enemy zone did not order both selected soldiers");
    const FixPoint health = z->getHealth();
    require(f.runUntil(40, [&] { return z->getOwner() == f.own && z->getGarrisonSize() == 2; }),
            "player take: the selected soldiers did not take the enemy zone and both go in: "
            + describe({soldier, trooper}, z));
    require(z->getHealth() == health, "player take: the soldiers shot the zone they walked into: "
            + describe({soldier, trooper}, z));
    auto* panel = selectZone(f, z);
    require(panel->garrisonLabel.getText().find(_("(captured)")) != std::string::npos,
            "player take: the zone panel does not say the zone is captured: " + panel->garrisonLabel.getText());
    require(panel->evacuateButton.isVisible() && panel->evacuateButton.isEnabled(),
            "player take: Evacuate is not offered on the captured zone");
    press(panel->evacuateButton);
    require(f.runUntil(5, [&] { return !z->isGarrisoned(); }), "player take: Evacuate did not empty the captured zone");
    require(z->getOwner() == f.enemy, "player take: the evacuated captured zone did not go back to its owner");
    require(onMapBeside(soldier, z, 2) && onMapBeside(trooper, z, 2), "player take: the soldiers did not come out");
    record("player_take", "Soldier + Trooper, C key, map click: zone taken, panel says captured, Evacuate hands it back");
}

void checkPlayerRefused() {
    Fixture f("player refused");
    // A full own zone.
    auto* full = f.zone(f.own, Structure_ZoneResidential, 20, 20);
    for(int i = 0; i < DuneCity::ZoneGarrison::kCapacity; ++i) {
        auto* s = f.unit(f.own, Unit_Soldier, 19, 19 + i);
        f.capture(s, full);
    }
    require(f.runUntil(40, [&] { return full->getGarrisonSize() == DuneCity::ZoneGarrison::kCapacity; }),
            "player refused: the zone did not fill");
    auto* fifth = f.unit(f.own, Unit_Soldier, 16, 20);
    f.run(1);
    captureByPlayer(f, {fifth}, full);
    f.run(10);
    require(fifth->isActive() && !hidden(fifth) && full->getGarrisonSize() == DuneCity::ZoneGarrison::kCapacity,
            "player refused: a fifth soldier went into a full zone");
    // An ally's zone.
    auto* allied = f.zone(f.ally, Structure_ZoneResidential, 50, 12);
    auto* visitor = f.unit(f.own, Unit_Soldier, 46, 12);
    f.run(1);
    captureByPlayer(f, {visitor}, allied, false, false);
    f.run(15);
    require(!allied->isGarrisoned() && allied->getOwner() == f.ally && visitor->isActive(),
            "player refused: an ally's zone was entered or taken");
    // A zone below the threshold.
    auto* damaged = f.zone(f.own, Structure_ZoneIndustrial, 30, 40);
    damaged->setHealth(damaged->getMaxHealth() * ZoneStructure::evacuateHealthPercent() / 100 - 1);
    auto* late = f.unit(f.own, Unit_Soldier, 27, 40);
    f.run(1);
    captureByPlayer(f, {late}, damaged);
    f.run(15);
    require(!damaged->isGarrisoned() && late->isActive(), "player refused: a soldier went into a zone below the threshold");
    // An enemy's garrisoned zone: its panel shows the garrison but not an Evacuate we may press.
    auto* held = f.zone(f.enemy, Structure_ZoneResidential, 44, 44);
    auto* defender = f.unit(f.enemy, Unit_Soldier, 43, 43);
    f.capture(defender, held);
    require(f.runUntil(20, [&] { return held->getGarrisonSize() == 1; }), "player refused: the enemy soldier did not go in");
    // A click selects the enemy's zone; the panel the player gets offers no Evacuate they could press.
    f.game->unselectAll(f.game->selectedList);
    f.game->selectedList.clear();
    clickMap(f, held->getLocation());
    f.game->pInterface->updateObjectInterface();
    if(auto* panel = dynamic_cast<ZoneStructureInterface*>(f.game->pInterface->pObjectContainer)) {
        panel->update();
        require(!panel->evacuateButton.isEnabled(), "player refused: Evacuate is enabled on an enemy's zone");
    }
    record("player_refused", "full own zone, ally's zone and a damaged zone not entered; enemy's Evacuate disabled");
}

// --- Garrison button, reinforcement, garrison weapons, no pulling away, price -----------------------

void checkGarrisonButton() {
    Fixture f("garrison button");
    Game& game = *f.game;
    auto* z = f.zone(f.own, Structure_ZoneResidential, 24, 24);
    auto* foe = f.zone(f.enemy, Structure_ZoneCommercial, 40, 24, 2);
    auto* soldier = f.unit(f.own, Unit_Soldier, 20, 25);
    auto* trooper = f.unit(f.own, Unit_Trooper, 20, 27);
    auto* tank = f.unit(f.own, Unit_Tank, 16, 30);
    f.run(1);
    // One Soldier: the panel shows Garrison, with its tooltip, and Capture says it occupies zones.
    auto* one = dynamic_cast<UnitInterface*>(selectUnits(f, {soldier}));
    require(one != nullptr, "garrison button: one Soldier selected opened no unit panel");
    require(one->garrisonButton.isVisible() && one->garrisonButton.isEnabled(),
            "garrison button: the Garrison button is not shown for a Soldier");
    require(one->garrisonButton.getTooltipText().find(_("Garrison")) != std::string::npos
                && one->garrisonButton.getTooltipText().find("(I)") != std::string::npos,
            "garrison button: the tooltip does not name Garrison and its I key: " + one->garrisonButton.getTooltipText());
    require(one->captureButton.getTooltipText().find(_("zone")) != std::string::npos,
            "garrison button: the Capture tooltip does not mention zones: " + one->captureButton.getTooltipText());
    // A tank alone: no Garrison button; a tank with a Soldier: the group panel shows it.
    auto* tankPanel = dynamic_cast<UnitInterface*>(selectUnits(f, {tank}));
    require(tankPanel != nullptr && !tankPanel->garrisonButton.isVisible(), "garrison button: shown for a tank");
    auto* many = dynamic_cast<MultiUnitInterface*>(selectUnits(f, {tank, soldier}));
    require(many != nullptr, "garrison button: a tank and a Soldier opened no group panel");
    many->update();
    require(many->garrisonButton.isVisible(), "garrison button: the group panel hides Garrison with a Soldier in it");
    // Press it: the Garrison cursor; its symbol over an own and an enemy zone, a pointer over sand.
    one = dynamic_cast<UnitInterface*>(selectUnits(f, {soldier}));
    press(one->garrisonButton);
    require(game.currentCursorMode == Game::CursorMode_Garrison, "garrison button: pressing it did not open the Garrison cursor");
    one->update();
    require(one->garrisonButton.getToggleState(), "garrison button: the button does not show it is active");
    using Action = CursorAppearance::Action;
    require(game.orderCursorAction(Game::CursorMode_Garrison, 24, 24) == Action::Garrison,
            "garrison button: the cursor over an own zone is not the Garrison symbol");
    require(game.orderCursorAction(Game::CursorMode_Garrison, 41, 25) == Action::Garrison,
            "garrison button: the cursor over an enemy zone is not the Garrison symbol");
    require(game.orderCursorAction(Game::CursorMode_Garrison, 30, 30) == Action::Pointer,
            "garrison button: the cursor over sand claims a target");
    require(game.orderCursorAction(Game::CursorMode_Capture, 25, 25) == Action::Capture,
            "garrison button: the Capture cursor over an own zone does not show it is a target");
    // A click on sand is refused and keeps the cursor; a click on the own zone garrisons it.
    const int before = queued(game, CMD_INFANTRY_CAPTURE);
    clickMap(f, Coord(30, 30));
    require(game.currentCursorMode == Game::CursorMode_Garrison && queued(game, CMD_INFANTRY_CAPTURE) == before,
            "garrison button: a click on sand ordered something or closed the cursor");
    clickMap(f, z->getLocation());
    require(queued(game, CMD_INFANTRY_CAPTURE) == before + 1 && game.currentCursorMode == Game::CursorMode_Normal,
            "garrison button: the click on an own zone queued no garrison order");
    require(f.runUntil(30, [&] { return z->getGarrisonSize() == 1 && hidden(soldier); }),
            "garrison button: the Soldier never went into the zone it was sent to");
    // The I key with a Trooper selected opens the same cursor; the Trooper takes the enemy zone.
    selectUnits(f, {trooper});
    SDL_KeyboardEvent key{};
    key.type = SDL_KEYDOWN;
    key.keysym.sym = SDLK_i;
    game.handleKeyInput(key);
    require(game.currentCursorMode == Game::CursorMode_Garrison, "garrison button: the I key did not open the Garrison cursor");
    clickMap(f, foe->getLocation());
    require(f.runUntil(40, [&] { return foe->getOwner() == f.own && foe->getGarrisonSize() == 1; }),
            "garrison button: the Trooper sent with the I key did not take the enemy zone");
    // A tank alone: the I key does nothing.
    selectUnits(f, {tank});
    game.handleKeyInput(key);
    require(game.currentCursorMode == Game::CursorMode_Normal, "garrison button: the I key opened the cursor for a tank");
    record("garrison_button", "shown for Soldier/Trooper and a mixed group, hidden for a tank; tooltip names I; "
           "symbol over own/enemy zone, pointer over sand; sand refused; own zone garrisoned; I key took an enemy zone");
}

void checkGarrisonButtonRuleOff() {
    Fixture f("garrison button rule off", false);
    auto* soldier = f.unit(f.own, Unit_Soldier, 20, 25);
    f.run(1);
    auto* one = dynamic_cast<UnitInterface*>(selectUnits(f, {soldier}));
    require(one != nullptr && !one->garrisonButton.isVisible(), "garrison button rule off: the button is shown with the rule off");
    SDL_KeyboardEvent key{};
    key.type = SDL_KEYDOWN;
    key.keysym.sym = SDLK_i;
    f.game->handleKeyInput(key);
    require(f.game->currentCursorMode != Game::CursorMode_Garrison, "garrison button rule off: the I key opened the cursor");
    record("garrison_button_rule_off", "no button, no cursor");
}

void checkReinforce() {
    Fixture f("reinforce");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 24, 24);
    auto* soldier = f.unit(f.own, Unit_Soldier, 22, 23);
    f.run(1);
    captureByPlayer(f, {soldier}, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), "reinforce: the Soldier did not go in");
    auto* trooper = f.unit(f.own, Unit_Trooper, 20, 26);
    f.run(1);
    require(captureByPlayer(f, {trooper}, z) == 1, "reinforce: the Trooper's order on the held own zone was not queued");
    require(f.runUntil(30, [&] { return z->getGarrisonSize() == 2; }), "reinforce: the Trooper did not join the Soldier");
    require(z->getGarrison()[0] == soldier->getObjectID() && z->getGarrison()[1] == trooper->getObjectID(),
            "reinforce: the mixed garrison is not the Soldier then the Trooper");
    // A captured zone: a Trooper takes it, another reinforces; the zone stays captured.
    auto* foe = f.zone(f.enemy, Structure_ZoneIndustrial, 40, 30, 2);
    auto* first = f.unit(f.own, Unit_Trooper, 39, 30);
    f.run(1);
    f.capture(first, foe);
    require(foe->getOwner() == f.own && foe->getGarrisonSize() == 1, "reinforce: the enemy zone was not taken");
    auto* second = f.unit(f.own, Unit_Soldier, 36, 31);
    f.run(1);
    captureByPlayer(f, {second}, foe);
    require(f.runUntil(30, [&] { return foe->getGarrisonSize() == 2; }), "reinforce: nobody joined the captured zone's garrison");
    require(foe->getGarrisonOriginalOwner() == HOUSE_HARKONNEN, "reinforce: the captured zone forgot whom it was taken from");
    record("reinforce", "a Trooper joined a Soldier's own zone (mixed), a Soldier joined a captured zone");
}

void checkTrooperAir() {
    Fixture f("trooper air");
    require(f.game->getGameInitSettings().getGameOptions().trooperAntiAir, "trooper air: Trooper Anti-Air is off in the fixture");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 30, 30);
    auto* trooper = f.unit(f.own, Unit_Trooper, 29, 29);
    f.capture(trooper, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), "trooper air: the Trooper did not go in");
    auto* bird = f.unit(f.enemy, Unit_Ornithopter, 34, 30);
    const Uint32 birdID = bird->getObjectID();
    const int birdHealth = bird->getHealth().lround();
    bird->doAttackObject(z, true);
    const auto before = f.own->getCombatReward(Unit_Trooper).hpRemovedMilli;
    require(f.runUntil(40, [&] {
                const auto* b = dynamic_cast<const UnitBase*>(f.game->getObjectManager().getObject(birdID));
                return b == nullptr || b->getHealth().lround() < birdHealth; }),
            "trooper air: the garrisoned Trooper never hit the Ornithopter attacking its zone");
    require(f.own->getCombatReward(Unit_Trooper).hpRemovedMilli > before, "trooper air: the hit was not credited to the Trooper");
    require(z->getGarrisonSize() == 1 || z->getHealth() <= 0 || !z->isGarrisoned(), "trooper air: the garrison changed");
    record("trooper_air", "a garrisoned Trooper hit an Ornithopter (credited to the Trooper)");
}

void checkTrooperTank() {
    Fixture f("trooper tank");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 30, 30);
    auto* trooper = f.unit(f.own, Unit_Trooper, 29, 29);
    f.capture(trooper, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), "trooper tank: the Trooper did not go in");
    auto* tank = f.unit(f.enemy, Unit_Tank, 35, 31, STOP);
    const Uint32 tankID = tank->getObjectID();
    const int tankHealth = tank->getHealth().lround();
    const auto before = f.own->getCombatReward(Unit_Trooper).hpRemovedMilli;
    require(f.runUntil(30, [&] {
                const auto* t = dynamic_cast<const UnitBase*>(f.game->getObjectManager().getObject(tankID));
                return t == nullptr || t->getHealth().lround() < tankHealth; }),
            "trooper tank: the garrisoned Trooper never hit the tank in its rocket range");
    const auto removed = f.own->getCombatReward(Unit_Trooper).hpRemovedMilli - before;
    require(removed > 0, "trooper tank: the tank's damage was not credited to the Trooper");
    record("trooper_tank", "a garrisoned Trooper's rockets hit a tank 5 tiles out (" + std::to_string(removed / 1000)
           + " hp credited)");
}

void checkNoPull() {
    Fixture f("no pull");
    auto* z = f.zone(f.own, Structure_ZoneResidential, 40, 40);
    auto* soldier = f.unit(f.own, Unit_Soldier, 30, 40);
    auto* tank = f.unit(f.enemy, Unit_Tank, 50, 50, STOP);
    f.run(1);
    f.capture(soldier, z);
    require(soldier->getAttackMode() == CAPTURE, "no pull: the capture order did not take");
    Player* player = f.own->getPlayerList().front().get();
    // Every controller's order as an attack wave, a defence pull, a formation, a retreat, a repair
    // trip, an airlift or an idle sweep would give it.
    player->doMove2Pos(soldier, 20, 20, true, "retreat");
    player->doHuntToward(soldier, 50, 50, "attackWave");
    player->doAttackObject(soldier, tank, true, "defencePull");
    player->doAttackPos(soldier, 50, 50, true, "idleSweep");
    player->doMove2Object(soldier, tank, "escort");
    player->doSetAttackMode(soldier, AREAGUARD, "formation");
    player->doFormationMove({soldier}, 20, 20, true, 0, "waveFormation");
    player->doRepair(soldier, "repairTrip");
    player->doRequestCarryallDrop(static_cast<const GroundUnit*>(static_cast<const UnitBase*>(soldier)), "airlift");
    require(soldier->getAttackMode() == CAPTURE && soldier->getTarget() == z,
            "no pull: another controller's order re-routed the soldier on its way in");
    require(f.runUntil(40, [&] { return z->getGarrisonSize() == 1; }), "no pull: the soldier did not reach its zone");
    // The garrison controller's own order is obeyed.
    auto* other = f.unit(f.own, Unit_Soldier, 30, 44);
    f.run(1);
    f.capture(other, z);
    player->doSetAttackMode(other, AREAGUARD, "updateZoneGarrisons");
    require(other->getAttackMode() == AREAGUARD, "no pull: the garrison controller could not end its own order");
    record("no_pull", "9 orders from other controllers refused; the soldier went in; the controller's own order obeyed");
}

void checkPrice() {
    Fixture f("price");
    require(f.game->objectData.data[Unit_Soldier][HOUSE_ATREIDES].price == 30, "price: the Soldier's price is not 30");
    f.own->placeStructure(NONE_ID, Structure_WindTrap, 10, 10, true, true);
    f.own->placeStructure(NONE_ID, Structure_Radar, 13, 10, true, true);
    auto* barracks = dynamic_cast<BuilderBase*>(f.own->placeStructure(NONE_ID, Structure_Barracks, 10, 14, true, true));
    require(barracks != nullptr, "price: cannot place a Barracks");
    f.run(2);
    barracks->updateBuildList();
    int price = -1;
    for(const BuildItem& item : barracks->getBuildList()) if(item.itemID == Unit_Soldier) price = static_cast<int>(item.price);
    require(price == 30, "price: the Barracks build list prices the Soldier at " + std::to_string(price));
    record("price", "Soldier 30 credits in the Barracks build list");
}

} // namespace ZoneGarrisonProbe

int runZoneGarrisonProbe() {
    using namespace ZoneGarrisonProbe;
    const char* outEnv = std::getenv("ZONE_GARRISON_OUT");
    require(outEnv != nullptr, "Missing ZONE_GARRISON_OUT");
    gOut = outEnv;
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    settings.general.playerName = "Garrison tester";
    gModOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    require(gModOptions.zoneGarrisons, "Zone Garrisons is not on by default in the Dune City mod");
    // Every case runs even after one fails, so a run shows all failures; any failure fails the probe.
    int failed = 0;
    for(auto check : {checkOwnZone, checkTake, checkFireOut, checkAssault, checkEvacuate, checkDestroyed,
                      checkReturn, checkLowHealth, checkAlly, checkContainment, checkSave, checkRuleOff,
                      checkPlayerOwnZone, checkPlayerTake, checkPlayerRefused, checkGarrisonButton,
                      checkGarrisonButtonRuleOff, checkReinforce, checkTrooperAir, checkTrooperTank, checkNoPull,
                      checkPrice, checkPicture}) {
        try {
            check();
        } catch(const std::exception&) {
            ++failed;   // require() logged ZONE_GARRISON_PROBE_FAIL with the reason.
            currentGame = nullptr;
        }
    }
    if(failed == 0) SDL_Log("ZONE_GARRISON_PROBE_PASS: 23 cases");
    else SDL_Log("ZONE_GARRISON_PROBE_SUMMARY: %d of 23 cases failed", failed);
    return -1;
}
