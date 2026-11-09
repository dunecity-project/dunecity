// Included only by run-carryall-repair-probe.py, never by the shipped game.
//
// Repair trips by carryall, end to end, in real matches advanced only by Game::updateGameState():
// a damaged ground unit, a free carryall parked at the house's repair yard and that yard well over
// twenty tiles away (Stefan's 2026-11-09 campaign save "nocrryall" had its Harkonnen yard twenty
// tiles from the fighting). The carryall must collect the unit before it drives off, fly it into the
// bay, and the yard must repair and release it. Each trigger the game has for a repair trip reaches
// the carryall request through a different caller, so each one is a case:
//   human_auto     the engine's own rule for a human house (GroundUnit::checkPos, below half health)
//   human_order    the player's Repair command (CMD_UNIT_SENDTOREPAIR, the sidebar/hotkey order)
//   partner        a human house's QuantBot partner sending the unit on damage (QuantBot::onDamage)
//   ai_auto        an AI QuantBot house, the engine's rule
//   ai_damage      an AI QuantBot house, the AI's own repair order on damage
// Without a free carryall the unit drives to the yard and is repaired there:
//   no_carryall    the house owns no carryall (the "nocrryall" save: its partner's orders walk)
//   busy_carryall  the house's only carryall is flying other cargo
// The map is bare rock, so routes and flights are always open and no worm can intervene.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <players/QuantBot.h>
#include <structures/RepairYard.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <sand.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kMapSize = 64;
const Coord kYardSpot(6, 6);                 ///< Repair yard top-left corner.
const Coord kParking(7, 11);                 ///< Where the idle carryall waits, beside the yard.
const Coord kDamagedSpot(34, 36);            ///< The damaged unit, 28 tiles from the yard.
const Coord kEnemySpot(56, 56);              ///< The enemy's own corner.
constexpr Uint32 kBookWithin = 60;           ///< Cycles: a free carryall is booked at once.
constexpr Uint32 kTripCycles = 12000;        ///< Cycles: the whole trip, including the repair.
constexpr int kMaxGroundTiles = 2;           ///< A unit waiting for its lift stays where it is.

void crRequire(bool ok, const std::string& what) {
    if(!ok) throw std::runtime_error(what);
}

/// Both houses own their buildings from the first cycle, so neither is ever without objects (a
/// house without any is out of the match at once). Credits pay for every repair point.
std::string crMap() {
    const auto position = [](const Coord& at) { return at.y * kMapSize + at.x; };
    std::ostringstream map;
    map << "[BASIC]\nVersion=2\nTechLevel=8\nWinFlags=3\nLoseFlags=1\n\n[MAP]\nSizeX=" << kMapSize
        << "\nSizeY=" << kMapSize << "\n\n";
    for(int y = 0; y < kMapSize; ++y) {
        char key[8];
        std::snprintf(key, sizeof(key), "%03d=", y);
        map << key << std::string(kMapSize, '%') << '\n';
    }
    map << "\n[Atreides]\nQuota=0\nCredits=5000\n\n[Harkonnen]\nQuota=0\nCredits=0\n\n"
        << "[STRUCTURES]\nID000=Atreides,Repair,256," << position(kYardSpot)
        << "\nID001=Harkonnen,Windtrap,256," << position(kEnemySpot) << "\n\n"
        << "[UNITS]\nID000=Harkonnen,Tank,256," << position(kEnemySpot + Coord(-3, 0)) << ",0,Guard\n";
    return map.str();
}

/// One match per case: Atreides (the house under test) against a Harkonnen house in the far
/// corner that only has to stay alive and own the unit that deals the damage.
struct Match {
    std::unique_ptr<Game> game;
    House* home = nullptr;
    House* enemy = nullptr;
    RepairYard* yard = nullptr;
    UnitBase* attacker = nullptr;
};

Match crStart(const std::string& mod, const std::vector<GameInitSettings::PlayerInfo>& homePlayers) {
    const std::string tester = settings.general.playerName;
    const bool homeHasLocal = std::any_of(homePlayers.begin(), homePlayers.end(),
        [&](const auto& player) { return player.playerName == tester; });
    Match match;
    match.game = std::make_unique<Game>();
    currentGame = match.game.get();
    GameInitSettings init("carryall-repair.ini", crMap(), false, settings.gameOptions);
    init.randomSeed = 20261109;
    crRequire(init.getModName() == mod, mod + ": the fixture selected mod " + init.getModName());
    // Outside a network game the map loader makes each human player it creates the local one in
    // turn, so the enemy comes first: the home house's human player, when it has one, is the
    // local player whose Repair orders this probe issues.
    GameInitSettings::HouseInfo enemy(HOUSE_HARKONNEN, 2);
    enemy.addPlayerInfo(GameInitSettings::PlayerInfo(homeHasLocal ? "Enemy" : tester, HUMANPLAYERCLASS));
    init.addHouseInfo(enemy);
    GameInitSettings::HouseInfo home(HOUSE_ATREIDES, 1);
    for(const auto& player : homePlayers) home.addPlayerInfo(player);
    init.addHouseInfo(home);
    // A live game, not a replay: the player's Repair order is a real command through the command
    // manager, which a replay initialisation would make read-only.
    match.game->initGame(init);
    match.game->initializeGameLoop();
    match.game->bPause = false;

    match.home = match.game->getHouse(HOUSE_ATREIDES);
    match.enemy = match.game->getHouse(HOUSE_HARKONNEN);
    crRequire(match.home && match.enemy, mod + ": the fixture houses are missing");
    crRequire(pLocalHouse == (homeHasLocal ? match.home : match.enemy) && pLocalPlayer != nullptr,
              mod + ": the fixture's local player is in the wrong house");
    for(StructureBase* structure : structureList)
        if(structure->getOwner() == match.home && structure->getItemID() == Structure_RepairYard)
            match.yard = static_cast<RepairYard*>(structure);
    for(UnitBase* unit : unitList)
        if(unit->getOwner() == match.enemy && unit->getItemID() == Unit_Tank) match.attacker = unit;
    crRequire(match.yard != nullptr && match.yard->getLocation() == kYardSpot, mod + ": the fixture repair yard is missing");
    crRequire(match.attacker != nullptr, mod + ": the fixture enemy tank is missing");
    crRequire(!match.game->finished, mod + ": the fixture match was decided before it started");
    return match;
}

void crEnd(Match& match) {
    match.game.reset();
    currentGame = nullptr;
    pLocalHouse = nullptr;
    pLocalPlayer = nullptr;
}

GroundUnit* crTank(House* house, const Coord& at) {
    auto* tank = static_cast<GroundUnit*>(house->createUnit(Unit_Tank));
    crRequire(tank != nullptr, "cannot create the damaged tank");
    tank->deploy(at);
    return tank;
}

Carryall* crCarryall(House* house, const Coord& at) {
    auto* carry = static_cast<Carryall*>(house->createUnit(Unit_Carryall));
    crRequire(carry != nullptr, "cannot create the carryall");
    carry->deploy(at);
    return carry;
}

/// The whole trip of one unit, in cycles from the moment it was damaged (0 = never happened).
struct Trip {
    Uint32 booked = 0, pickedUp = 0, inBay = 0, released = 0;
    Uint32 cycles = 0;     ///< How long it was followed.
    Uint32 carrier = NONE_ID;
    int ground = 0;        ///< Farthest the unit got from its start on its own, before any lift.
    int closest = 1 << 20; ///< Closest it got to the yard on its own, before any lift.
};

/// Plays the match until the unit is released from the yard, `until` holds or `cycles` ran out.
Trip crFollow(Game& game, GroundUnit* unit, RepairYard* yard, Uint32 cycles = kTripCycles,
              const std::function<bool()>& until = {}) {
    Trip trip;
    const Coord start = unit->getLocation();
    // CARRYALL_REPAIR_TRACE=1 logs every change of the unit's and the carryalls' transport state.
    const bool trace = std::getenv("CARRYALL_REPAIR_TRACE") != nullptr;
    std::string last;
    for(Uint32 t = 1; t <= cycles && !(until && until()); ++t) {
        game.updateGameState();
        trip.cycles = t;
        crRequire(!game.finished, "the fixture match was decided at cycle " + std::to_string(game.getGameCycleCount())
                  + " (home " + (game.getHouse(HOUSE_ATREIDES)->isAlive() ? "alive" : "dead") + ", enemy "
                  + (game.getHouse(HOUSE_HARKONNEN)->isAlive() ? "alive" : "dead") + ")");
        if(trace) {
            std::ostringstream state;
            state << "unit@" << unit->getLocation().x << ',' << unit->getLocation().y << " mode=" << unit->getAttackMode()
                  << " target=" << (unit->getTarget() ? getItemNameByID(unit->getTarget()->getItemID()) : "-")
                  << " ry=" << unit->goingToRepairYard << " awaiting=" << unit->isAwaitingPickup()
                  << " picked=" << unit->isPickedUp() << " active=" << unit->isActive() << " forced=" << unit->wasForced()
                  << " bay=" << (yard->getContainedRepairUnit() == unit) << " bookings=" << yard->getNumBookings();
            for(const UnitBase* u : unitList)
                if(isCarryallUnit(u->getItemID())) {
                    const auto* c = static_cast<const Carryall*>(u);
                    state << " | carryall " << c->getObjectID() << '@' << c->getLocation().x << ',' << c->getLocation().y
                          << " target=" << (c->getTarget() ? getItemNameByID(c->getTarget()->getItemID()) : "-")
                          << " cargo=" << c->getCargoIds().size() << " dest=" << c->getDestination().x << ','
                          << c->getDestination().y;
                }
            if(state.str() != last) {
                SDL_Log("CARRYALL_REPAIR_TRACE: t=%u %s", t, state.str().c_str());
                last = state.str();
            }
        }
        if(!trip.booked && unit->isAwaitingPickup() && unit->hasBookedCarrier()) {
            trip.booked = t;
            trip.carrier = unit->getCarrier()->getObjectID();
        }
        if(!trip.pickedUp && !trip.inBay && unit->isPickedUp()) trip.pickedUp = t;
        if(!trip.pickedUp && !trip.inBay && unit->isActive()) {
            trip.ground = std::max(trip.ground, blockDistance(start, unit->getLocation()).lround());
            trip.closest = std::min(trip.closest, blockDistance(unit->getLocation(),
                                                                yard->getClosestPoint(unit->getLocation())).lround());
        }
        if(!trip.inBay && yard->getContainedRepairUnit() == unit) trip.inBay = t;
        if(trip.inBay && yard->getContainedRepairUnit() != unit && unit->isActive() && !unit->isPickedUp()) {
            trip.released = t;
            break;
        }
    }
    return trip;
}

std::string crDescribe(const GroundUnit* unit, const Trip& trip) {
    std::ostringstream text;
    text << " (booked=" << trip.booked << " pickedUp=" << trip.pickedUp << " inBay=" << trip.inBay
         << " released=" << trip.released << " driven=" << trip.ground << " closest=" << trip.closest
         << " health=" << unit->getHealth().lround() << '/' << unit->getMaxHealth()
         << " mode=" << unit->getAttackMode() << " goingToRepairYard=" << unit->goingToRepairYard
         << " awaiting=" << unit->isAwaitingPickup() << " forced=" << unit->wasForced() << ')';
    return text.str();
}

/// The trip a free carryall must give: booked at once, collected where the unit stood, flown into
/// the bay, repaired to full health and released.
void crExpectLift(const std::string& label, const GroundUnit* unit, const std::vector<const Carryall*>& fleet,
                  const Trip& trip) {
    const std::string why = crDescribe(unit, trip);
    crRequire(trip.booked != 0 && trip.booked <= kBookWithin,
              label + ": no free carryall was booked for the repair trip" + why);
    crRequire(std::any_of(fleet.begin(), fleet.end(), [&](const Carryall* c) { return c->getObjectID() == trip.carrier; }),
              label + ": the repair trip booked an aircraft that is not the house's own carryall" + why);
    crRequire(trip.pickedUp != 0, label + ": the carryall never collected the damaged unit" + why);
    crRequire(trip.ground <= kMaxGroundTiles,
              label + ": the unit drove towards the repair yard instead of waiting for its carryall" + why);
    crRequire(trip.inBay != 0, label + ": the carryall never delivered the unit into the repair bay" + why);
    crRequire(trip.released != 0, label + ": the repair yard never released the unit" + why);
    crRequire(unit->getHealth() == unit->getMaxHealth(), label + ": the unit was released unrepaired" + why);
    SDL_Log("CARRYALL_REPAIR_PROBE_LIFT: %s booked=%u picked_up=%u in_bay=%u released=%u driven=%d",
            label.c_str(), trip.booked, trip.pickedUp, trip.inBay, trip.released, trip.ground);
}

/// The trip without a free carryall: the unit sets off on its own and is repaired on arrival.
void crExpectDrive(const std::string& label, const GroundUnit* unit, int startDistance, const Trip& trip,
                   bool mayBeLiftedLater) {
    const std::string why = crDescribe(unit, trip);
    crRequire(trip.ground >= 5, label + ": without a free carryall the unit did not drive to the repair yard" + why);
    crRequire(trip.closest < startDistance - 5, label + ": the unit did not head for the repair yard" + why);
    crRequire(mayBeLiftedLater || trip.pickedUp == 0, label + ": a unit with no carryall was picked up" + why);
    crRequire(trip.inBay != 0, label + ": the unit never entered the repair bay" + why);
    crRequire(trip.released != 0, label + ": the repair yard never released the unit" + why);
    crRequire(unit->getHealth() == unit->getMaxHealth(), label + ": the unit was released unrepaired" + why);
    SDL_Log("CARRYALL_REPAIR_PROBE_DRIVE: %s driven=%d closest=%d picked_up=%u in_bay=%u released=%u",
            label.c_str(), trip.ground, trip.closest, trip.pickedUp, trip.inBay, trip.released);
}

const std::vector<GameInitSettings::PlayerInfo> crHuman() {
    return {GameInitSettings::PlayerInfo(settings.general.playerName, HUMANPLAYERCLASS)};
}

/// Damage dealt by the enemy's tank, so every player of the house hears about it (onDamage).
void crHit(Match& match, GroundUnit* unit, const FixPoint& healthAfter) {
    const int damage = (unit->getHealth() - healthAfter).lround();
    crRequire(damage > 0, "the hit fixture needs a positive damage");
    unit->handleDamage(damage, match.attacker->getObjectID(), match.enemy);
}

void crLiftCase(const std::string& mod, const std::string& name) {
    const std::string label = mod + "/" + name;
    std::vector<GameInitSettings::PlayerInfo> players = crHuman();
    if(name == "partner") players.emplace_back("Atreides partner", "qBotBrutal");
    if(name == "ai_auto" || name == "ai_damage") players = {GameInitSettings::PlayerInfo("Atreides AI", "qBotHard")};
    Match match = crStart(mod, players);
    auto* carry = crCarryall(match.home, kParking);
    auto* tank = crTank(match.home, kDamagedSpot);
    const int distance = blockDistance(tank->getLocation(), match.yard->getClosestPoint(tank->getLocation())).lround();
    crRequire(distance >= 20, label + ": the fixture yard is too close for a repair flight");
    for(int settle = 0; settle < 10; ++settle) match.game->updateGameState();
    crRequire(!tank->goingToRepairYard && !carry->isBooked() && tank->isActive(),
              label + ": the fixture started with a repair trip or a booked carryall");

    if(name == "human_auto" || name == "ai_auto") {
        // Below half health: the engine sends the unit by itself.
        tank->setHealth(FixPoint(tank->getMaxHealth()) / 3);
    } else if(name == "human_order") {
        // Above half health the engine would leave it, so only the player's order sends it.
        tank->setHealth(FixPoint(tank->getMaxHealth()) * 7 / 10);
        tank->handleSendToRepairClick();
    } else {
        // The QuantBot's own rule: a hit that leaves a vehicle below 60 % (above half health, so
        // the engine's rule cannot be what sends it).
        tank->setHealth(FixPoint(tank->getMaxHealth()) * 58 / 100);
        crHit(match, tank, FixPoint(tank->getMaxHealth()) * 55 / 100);
        crRequire(tank->goingToRepairYard, label + ": the QuantBot did not send the damaged unit for repairs"
                  + crDescribe(tank, Trip()));
    }
    const Trip trip = crFollow(*match.game, tank, match.yard);
    crExpectLift(label, tank, {carry}, trip);
    crRequire(match.yard->getNumBookings() == 0 && !carry->hasCargo() && !tank->isAwaitingPickup(),
              label + ": the completed repair left a transport or yard booking");
    crEnd(match);
}

void crNoCarryallCase(const std::string& mod, bool partner) {
    const std::string label = mod + (partner ? "/partner_no_carryall" : "/no_carryall");
    auto players = crHuman();
    if(partner) players.emplace_back("Atreides partner", "qBotBrutal");
    Match match = crStart(mod, players);
    auto* tank = crTank(match.home, kDamagedSpot);
    for(int settle = 0; settle < 10; ++settle) match.game->updateGameState();
    crRequire(!match.home->hasCarryalls(), label + ": the fixture house owns a carryall");
    const int distance = blockDistance(tank->getLocation(), match.yard->getClosestPoint(tank->getLocation())).lround();
    crRequire(distance >= 20, label + ": the fixture repair yard is too close");
    if(partner) {
        // As in the reported campaign save: the partner orders a long repair walk even though
        // the house has no carryall. Above half health rules out the engine's auto-repair.
        tank->setHealth(FixPoint(tank->getMaxHealth()) * 58 / 100);
        crHit(match, tank, FixPoint(tank->getMaxHealth()) * 55 / 100);
        crRequire(tank->goingToRepairYard, label + ": the partner did not order the repair trip");
    } else {
        tank->setHealth(FixPoint(tank->getMaxHealth()) / 3);
        tank->handleSendToRepairClick();
    }
    const Trip trip = crFollow(*match.game, tank, match.yard);
    crExpectDrive(label, tank, distance, trip, false);
    crRequire(match.yard->getNumBookings() == 0, label + ": the completed repair left a yard booking");
    crEnd(match);
}

void crBusyCarryallCase(const std::string& mod) {
    const std::string label = mod + "/busy_carryall";
    Match match = crStart(mod, crHuman());
    auto* carry = crCarryall(match.home, kParking);
    auto* cargo = crTank(match.home, kParking + Coord(2, 0));
    auto* tank = crTank(match.home, kDamagedSpot);
    for(int settle = 0; settle < 10; ++settle) match.game->updateGameState();
    // The only carryall takes other cargo on a long flight to the far edge of the map.
    carry->giveCargo(cargo);
    carry->setDestination(Coord(kMapSize - 2, 2));
    crRequire(carry->isBooked(), label + ": the busy carryall fixture is not booked");
    tank->setHealth(FixPoint(tank->getMaxHealth()) / 3);
    tank->handleSendToRepairClick();
    // While its only carryall is away, the unit must already be on its way, not waiting for it.
    const Trip early = crFollow(*match.game, tank, match.yard, kTripCycles, [&] { return !carry->hasCargo(); });
    crRequire(!carry->hasCargo(), label + ": the busy carryall never delivered its cargo");
    crRequire(early.booked == 0 && early.pickedUp == 0 && early.ground >= 2,
              label + ": with its only carryall busy the unit waited instead of driving" + crDescribe(tank, early));
    // It may still be collected once that carryall is free again; either way it ends repaired.
    const Trip trip = crFollow(*match.game, tank, match.yard);
    crRequire(trip.inBay != 0 && trip.released != 0 && tank->getHealth() == tank->getMaxHealth(),
              label + ": the unit never got its repair" + crDescribe(tank, trip));
    SDL_Log("CARRYALL_REPAIR_PROBE_DRIVE: %s driven_while_busy=%d busy_cycles=%u picked_up_later=%u in_bay=%u released=%u",
            label.c_str(), early.ground, early.cycles, trip.pickedUp, trip.inBay, trip.released);
    crEnd(match);
}

}

int runCarryallRepairProbe() {
    const std::string mod = std::getenv("CARRYALL_REPAIR_MOD") ? std::getenv("CARRYALL_REPAIR_MOD") : "";
    settings.general.playerName = "Carryall tester";
    settings.network.metaServer = "http://127.0.0.1:1/";
    std::string current;
    try {
        crRequire(ModManager::instance().setActiveMod(mod), "Missing carryall repair fixture mod " + mod);
        for(const char* name : {"human_auto", "human_order", "partner", "ai_auto", "ai_damage"}) {
            current = name;
            crLiftCase(mod, name);
        }
        current = "no_carryall";
        crNoCarryallCase(mod, false);
        current = "partner_no_carryall";
        crNoCarryallCase(mod, true);
        current = "busy_carryall";
        crBusyCarryallCase(mod);
    } catch(const std::exception& e) {
        SDL_Log("CARRYALL_REPAIR_PROBE_FAIL: %s: %s", current.c_str(), e.what());
        return MENU_QUIT_DEFAULT;
    }
    SDL_Log("CARRYALL_REPAIR_PROBE_PASS: %s", mod.c_str());
    return MENU_QUIT_DEFAULT;
}
