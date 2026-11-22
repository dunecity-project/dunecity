// A Palace-released Saboteur goes for the nearest enemy vehicle, whoever owns the Palace.
//
// Before this probe existed the Saboteur's search was the general one: the nearest thing it
// may attack, structure or vehicle alike, so a Wind Trap five tiles away was blown up while
// tanks stood further off, and a human-owned Saboteur was released in Guard with a weapon
// range of 0 and never moved. Each case below runs the real Palace special weapon and
// Game::updateGameState until the Saboteur is gone, then checks what was destroyed.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <mod/ModManager.h>
#include <structures/Palace.h>
#include <structures/StructureBase.h>
#include <units/Saboteur.h>
#include <units/UnitBase.h>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace SaboteurTargetProbe {

inline void require(bool ok, const std::string& why) {
    if(!ok) throw std::runtime_error("SABOTEUR_TARGET_PROBE_FAIL: " + why);
}

struct Watched {
    std::string name;
    Uint32 objectID;
};

/// One 64x64 open-rock match: Ordos (team 1) owns the Palace at (30,30), Harkonnen (team 2)
/// is the enemy. OWNERCLASS is the Ordos player class, so the same cases run for a human and
/// for a QuantBot house.
struct Fixture {
    std::unique_ptr<Game> game;
    House* owner = nullptr;
    House* enemy = nullptr;
    Palace* palace = nullptr;
    std::vector<Watched> watched;
    std::string label;

    Fixture(const std::string& ownerClass, const std::string& caseName) : label(ownerClass + "/" + caseName) {
        std::ostringstream map;
        map << "[BASIC]\nVersion=2\nTechLevel=8\n[MAP]\nSizeX=64\nSizeY=64\n";
        for(int y = 0; y < 64; ++y) map << std::setw(3) << std::setfill('0') << y << '=' << std::string(64, '%') << '\n';
        map << "[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n";
        const bool humanOwner = ownerClass == "HumanPlayer";
        // The local player is the Saboteur's owner in the human cases and its victim in the AI cases.
        settings.general.playerName = humanOwner ? "Owner" : "Enemy";
        game = std::make_unique<Game>();
        currentGame = game.get();
        GameInitSettings init("saboteur-target.ini", map.str(), false, settings.gameOptions);
        init.randomSeed = 71;
        GameInitSettings::HouseInfo own(HOUSE_ORDOS, 1);
        own.addPlayerInfo({"Owner", ownerClass});
        init.addHouseInfo(own);
        GameInitSettings::HouseInfo foe(HOUSE_HARKONNEN, 2);
        foe.addPlayerInfo({"Enemy", "HumanPlayer"});
        init.addHouseInfo(foe);
        game->initGame(init);
        game->initializeGameLoop();
        owner = game->getHouse(HOUSE_ORDOS);
        enemy = game->getHouse(HOUSE_HARKONNEN);
        require(owner != nullptr && enemy != nullptr, label + ": fixture houses missing");
        require(owner->isAI() == !humanOwner, label + ": owner is not the requested kind of player");
        palace = static_cast<Palace*>(owner->placeStructure(NONE_ID, Structure_Palace, 30, 30, true, true));
        require(palace != nullptr, label + ": cannot place the Palace");
    }

    ~Fixture() {
        game.reset();
        currentGame = nullptr;
    }

    const UnitBase* enemyUnit(const std::string& name, int itemID, int x, int y) {
        auto* unit = enemy->createUnit(itemID, true);
        require(unit != nullptr, label + ": cannot create " + name);
        unit->deploy(Coord(x, y));
        unit->doSetAttackMode(STOP);    // The victims neither move nor shoot.
        watched.push_back({name, unit->getObjectID()});
        return unit;
    }

    const StructureBase* enemyStructure(const std::string& name, int itemID, int x, int y) {
        const auto* structure = enemy->placeStructure(NONE_ID, itemID, x, y, true, true);
        require(structure != nullptr, label + ": cannot place " + name);
        watched.push_back({name, structure->getObjectID()});
        return structure;
    }

    /// The real special weapon: Palace::doSpecialWeapon() -> Palace::spawnSaboteur().
    UnitBase* release() {
        for(int i = 0; i < 5; ++i) game->updateGameState();
        palace->specialWeaponTimer = 0;
        palace->doSpecialWeapon();
        UnitBase* saboteur = nullptr;
        for(auto* unit : unitList)
            if(unit->getItemID() == Unit_Saboteur && unit->getOwner() == owner) saboteur = unit;
        require(saboteur != nullptr, label + ": the Palace released no Saboteur");
        require(saboteur->getAttackMode() == HUNT, label + ": the released Saboteur is not hunting");
        return saboteur;
    }

    bool alive(Uint32 objectID) const { return game->getObjectManager().getObject(objectID) != nullptr; }

    /// Runs until the Saboteur is gone or SECONDS of game time passed. Returns the game seconds used.
    int runUntilGone(Uint32 saboteurID, int seconds) {
        const Uint32 start = game->getGameCycleCount();
        const Uint32 deadline = start + MILLI2CYCLES(seconds * 1000);
        while(game->getGameCycleCount() < deadline && alive(saboteurID)) game->updateGameState();
        return static_cast<int>((game->getGameCycleCount() - start) * GAMESPEED_DEFAULT / 1000);
    }

    /// Exactly VICTIM is destroyed; the Saboteur blew itself up doing it.
    void requireOnlyVictim(Uint32 saboteurID, Uint32 victimID) {
        require(!alive(saboteurID), label + ": the Saboteur never reached a target");
        for(const auto& entry : watched) {
            const bool shouldDie = entry.objectID == victimID;
            require(alive(entry.objectID) != shouldDie,
                    label + ": " + entry.name + (shouldDie ? " survived" : " was destroyed instead"));
        }
    }
};

/// The enemy vehicle a Saboteur standing at FROM must pick: the nearest ground unit that is
/// neither infantry nor a sandworm, lower object ID first on a tie.
inline const UnitBase* nearestVehicle(const House* enemy, const Coord& from) {
    const UnitBase* best = nullptr;
    FixPoint bestDistance = FixPt_MAX;
    for(const auto* unit : unitList) {
        if(unit->getOwner() != enemy || !unit->isAGroundUnit() || unit->isInfantry()
           || unit->getItemID() == Unit_Sandworm) continue;
        const FixPoint distance = blockDistance(from, unit->getLocation());
        if(distance < bestDistance || (distance == bestDistance && unit->getObjectID() < best->getObjectID())) {
            best = unit;
            bestDistance = distance;
        }
    }
    return best;
}

/// A nearer structure and a nearer infantry unit are passed by for the nearest of several
/// vehicles; an aircraft overhead is no vehicle.
inline void vehicleBeforeStructure(const std::string& ownerClass) {
    Fixture f(ownerClass, "vehicle-before-structure");
    f.enemyStructure("Wind Trap 5 tiles off", Structure_WindTrap, 30, 38);
    f.enemyUnit("Soldier 4 tiles off", Unit_Soldier, 35, 36);
    f.enemyUnit("Carryall overhead", Unit_Carryall, 33, 35);
    const auto* near = f.enemyUnit("Tank 13 tiles off", Unit_Tank, 31, 46);
    f.enemyUnit("Quad 19 tiles off", Unit_Quad, 52, 31);
    f.enemyUnit("Tank 22 tiles off", Unit_Tank, 8, 31);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    const auto* expected = nearestVehicle(f.enemy, saboteur->getLocation());
    require(expected == near, f.label + ": the fixture's nearest vehicle is not the 13-tile tank");
    const Uint32 victimID = expected->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() == expected,
            f.label + ": the released Saboteur did not pick the nearest vehicle");
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s destroyed the nearest vehicle after %d s; Wind Trap, Soldier, Carryall and farther vehicles untouched",
            f.label.c_str(), seconds);
}

/// Harvesters and MCVs are vehicles: the nearest one goes, whatever its kind.
inline void workersAreVehicles(const std::string& ownerClass) {
    Fixture f(ownerClass, "harvester-and-mcv");
    const auto* harvester = f.enemyUnit("Harvester 10 tiles off", Unit_Harvester, 44, 31);
    f.enemyUnit("MCV 16 tiles off", Unit_MCV, 31, 50);
    f.enemyUnit("Tank 24 tiles off", Unit_Tank, 6, 31);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    const auto* expected = nearestVehicle(f.enemy, saboteur->getLocation());
    require(expected == harvester, f.label + ": the fixture's nearest vehicle is not the Harvester");
    const Uint32 victimID = expected->getObjectID();
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s destroyed the Harvester after %d s; MCV and Tank untouched",
            f.label.c_str(), seconds);
}

/// No enemy vehicle anywhere: the old rule still applies and the nearest structure goes.
inline void structureWithoutVehicles(const std::string& ownerClass) {
    Fixture f(ownerClass, "no-vehicle");
    const auto* trap = f.enemyStructure("Wind Trap 5 tiles off", Structure_WindTrap, 30, 38);
    f.enemyStructure("Refinery 12 tiles off", Structure_Refinery, 45, 30);
    f.enemyUnit("Soldier 4 tiles off", Unit_Soldier, 35, 36);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    const Uint32 victimID = trap->getObjectID();
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s had no vehicle to go for and destroyed the nearest structure after %d s",
            f.label.c_str(), seconds);
}

/// Nothing a Saboteur may attack: it stops hunting and waits in Guard, alive, as every
/// hunter with an empty target set does.
inline void nothingToSabotage(const std::string& ownerClass) {
    Fixture f(ownerClass, "nothing-to-attack");
    f.enemyUnit("Soldier 4 tiles off", Unit_Soldier, 35, 36);
    f.enemyUnit("Carryall overhead", Unit_Carryall, 33, 35);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    f.runUntilGone(saboteurID, 20);
    require(f.alive(saboteurID), f.label + ": the Saboteur blew up with nothing to sabotage");
    require(saboteur->getAttackMode() == GUARD && saboteur->getTarget() == nullptr,
            f.label + ": a Saboteur with nothing to sabotage did not return to Guard");
    for(const auto& entry : f.watched) require(f.alive(entry.objectID), f.label + ": " + entry.name + " was destroyed");
    SDL_Log("SABOTEUR_TARGET_CASE: %s had nothing to sabotage and waits in Guard", f.label.c_str());
}

/// The owner's explicit order still wins: sent at a structure, the Saboteur leaves the vehicles.
inline void explicitOrderKept() {
    Fixture f("HumanPlayer", "explicit-order");
    const auto* refinery = f.enemyStructure("Refinery 12 tiles off", Structure_Refinery, 45, 30);
    f.enemyUnit("Tank 13 tiles off", Unit_Tank, 31, 46);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() != nullptr && saboteur->getTarget()->getItemID() == Unit_Tank,
            f.label + ": the released Saboteur did not start for the Tank");
    saboteur->doAttackObject(refinery, true);
    const Uint32 victimID = refinery->getObjectID();
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s was ordered onto the Refinery and destroyed it after %d s; the Tank is untouched",
            f.label.c_str(), seconds);
}

/// Area Guard (a Saboteur its owner set to it): a vehicle inside the 10-tile guard range is
/// taken before a nearer structure.
inline void areaGuardPrefersVehicle() {
    Fixture f("HumanPlayer", "area-guard");
    f.enemyStructure("Wind Trap 3 tiles off", Structure_WindTrap, 20, 13);
    const auto* tank = f.enemyUnit("Tank 8 tiles off", Unit_Tank, 28, 10);
    auto* saboteur = f.owner->createUnit(Unit_Saboteur, false);
    require(saboteur != nullptr, f.label + ": cannot create the Saboteur");
    saboteur->deploy(Coord(20, 10));
    const Uint32 saboteurID = saboteur->getObjectID();
    saboteur->doSetAttackMode(AREAGUARD);
    const Uint32 victimID = tank->getObjectID();
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s took the Tank inside its guard range after %d s; the nearer Wind Trap is untouched",
            f.label.c_str(), seconds);
}

} // namespace SaboteurTargetProbe

int runSaboteurTargetProbe() {
    using namespace SaboteurTargetProbe;
    const std::string mod = std::getenv("SABOTEUR_TARGET_PROBE_MOD");
    require(ModManager::instance().setActiveMod(mod), "missing fixture mod " + mod);
    for(const char* ownerClass : {"HumanPlayer", "qBotHard"}) {
        vehicleBeforeStructure(ownerClass);
        workersAreVehicles(ownerClass);
        structureWithoutVehicles(ownerClass);
        nothingToSabotage(ownerClass);
    }
    explicitOrderKept();
    areaGuardPrefersVehicle();
    SDL_Log("SABOTEUR_TARGET_PROBE_PASS: mod=%s human and QuantBot owners, 10 cases", mod.c_str());
    return -1;
}
