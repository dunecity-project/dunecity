// A Palace-released Saboteur goes for the nearest thing it may blow up, enemy building or vehicle
// alike, whoever owns the Palace.
//
// Up to 1.0.876 a human-owned Saboteur was released in Guard with a weapon range of 0 and never
// moved, and a hunting one only blew up after it had stopped next to its target. Each case below
// runs the real Palace special weapon and Game::updateGameState until the Saboteur is gone, then
// checks what was destroyed.
//
// A Saboteur sees one tile and goes off half a step before it stands next to its target, so its
// owner saw the target for 0.4 s before the blast, and a Saboteur that was shot blew up exactly
// like one that had sabotaged something: both looked like a Saboteur blowing up by itself. The last cases check that its owner is shown the target on the approach and that a
// shot Saboteur falls like a foot soldier.
#include <Explosion.h>
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

/// A building is as good as a vehicle: the nearest thing it may blow up is taken, here a Wind
/// Trap, while a nearer Soldier (infantry is no target) and three vehicles further off are left.
inline void nearestIsABuilding(const std::string& ownerClass) {
    Fixture f(ownerClass, "nearest-is-a-building");
    const auto* trap = f.enemyStructure("Wind Trap 7 tiles north", Structure_WindTrap, 30, 22);
    f.enemyUnit("Soldier 6 tiles north", Unit_Soldier, 27, 24);
    f.enemyUnit("Tank 13 tiles east", Unit_Tank, 45, 31);
    f.enemyUnit("Quad 20 tiles south", Unit_Quad, 31, 52);
    f.enemyUnit("Tank 22 tiles west", Unit_Tank, 8, 31);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    const Uint32 victimID = trap->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() == trap, f.label + ": the released Saboteur did not pick the nearest building");
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s destroyed the nearest building after %d s; the nearer Soldier and the farther vehicles untouched",
            f.label.c_str(), seconds);
}

/// The nearest of several vehicles is taken when it is nearer than every building.
inline void nearestIsAVehicle(const std::string& ownerClass) {
    Fixture f(ownerClass, "nearest-is-a-vehicle");
    f.enemyStructure("Wind Trap 14 tiles north", Structure_WindTrap, 30, 14);
    f.enemyStructure("Refinery 24 tiles south-west", Structure_Refinery, 10, 50);
    f.enemyUnit("Soldier 6 tiles north", Unit_Soldier, 27, 24);
    const auto* near = f.enemyUnit("Tank 8 tiles east", Unit_Tank, 40, 31);
    f.enemyUnit("Quad 20 tiles south", Unit_Quad, 31, 52);
    f.enemyUnit("Tank 22 tiles west", Unit_Tank, 8, 31);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    const auto* expected = nearestVehicle(f.enemy, saboteur->getLocation());
    require(expected == near, f.label + ": the fixture's nearest vehicle is not the 8-tile tank");
    const Uint32 victimID = expected->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() == expected, f.label + ": the released Saboteur did not pick the nearest vehicle");
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s destroyed the nearest vehicle after %d s; the farther buildings and vehicles untouched",
            f.label.c_str(), seconds);
}

/// Something nearer turns up beside it on the way: a vehicle within two tiles sees the Saboteur,
/// and it blows that up instead of walking on under fire to the one it had picked.
inline void nearerThingTurnsUp(const std::string& ownerClass) {
    Fixture f(ownerClass, "nearer-thing-turns-up");
    f.enemyUnit("Tank 13 tiles south", Unit_Tank, 31, 46);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    const Coord released = saboteur->getLocation();
    for(int cycle = 0; cycle < 2000 && f.alive(saboteurID)
        && (blockDistance(released, saboteur->getLocation()) < 4 || saboteur->moving); ++cycle) {
        f.game->updateGameState();
    }
    require(f.alive(saboteurID) && saboteur->getTarget() != nullptr && saboteur->getTarget()->getItemID() == Unit_Tank,
            f.label + ": the Saboteur was not on its way to the Tank");
    Coord beside = Coord::Invalid();
    for(const Coord& offset : {Coord(2, 0), Coord(-2, 0), Coord(2, -1), Coord(-2, -1)}) {
        const Coord tile = saboteur->getLocation() + offset;
        if(beside.isInvalid() && currentGameMap->tileExists(tile) && !currentGameMap->getTile(tile)->hasAGroundObject()) {
            beside = tile;
        }
    }
    require(beside.isValid(), f.label + ": no free tile beside the Saboteur");
    const auto* quad = f.enemyUnit("Quad that drove up beside it", Unit_Quad, beside.x, beside.y);
    const Uint32 victimID = quad->getObjectID();
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s met a Quad beside it on the way and destroyed it %d s later; the Tank it had picked is untouched",
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

/// No enemy vehicle anywhere: the nearest structure goes, the nearer Soldier is no target.
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
    // An AI house's Saboteur looks again every ten seconds, so it may be caught mid-search.
    require(saboteur->getTarget() == nullptr && (saboteur->getAttackMode() == GUARD
                || (f.owner->isAI() && saboteur->getAttackMode() == HUNT)),
            f.label + ": a Saboteur with nothing to sabotage did not return to Guard");
    for(const auto& entry : f.watched) require(f.alive(entry.objectID), f.label + ": " + entry.name + " was destroyed");
    SDL_Log("SABOTEUR_TARGET_CASE: %s had nothing to sabotage and waits in Guard", f.label.c_str());
}

/// The owner's explicit order still wins: sent at the far Refinery, the Saboteur walks past the
/// nearer Wind Trap it had picked itself, although that building sees it on the way.
inline void explicitOrderKept() {
    Fixture f("HumanPlayer", "explicit-order");
    const auto* refinery = f.enemyStructure("Refinery 12 tiles east", Structure_Refinery, 45, 30);
    f.enemyStructure("Wind Trap on the way", Structure_WindTrap, 38, 30);
    f.enemyUnit("Tank 13 tiles south", Unit_Tank, 31, 48);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    f.game->updateGameState();
    require(saboteur->getTarget() != nullptr && saboteur->getTarget() != refinery,
            f.label + ": the released Saboteur did not start for something nearer than the Refinery");
    saboteur->doAttackObject(refinery, true);
    const Uint32 victimID = refinery->getObjectID();
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s was ordered onto the Refinery and destroyed it after %d s; the Wind Trap it passed and the Tank are untouched",
            f.label.c_str(), seconds);
}

/// Area Guard (a Saboteur its owner set to it): the nearest thing inside the 10-tile guard range
/// is taken, and what stands beyond the range is left. Outside Hunt every unit only picks what
/// stands on ground its team has explored, so the owner has seen the map here.
inline void areaGuardTakesNearest() {
    Fixture f("HumanPlayer", "area-guard");
    for(int y = 0; y < currentGameMap->getSizeY(); ++y)
        for(int x = 0; x < currentGameMap->getSizeX(); ++x)
            currentGameMap->getTile(x, y)->setExplored(f.owner->getHouseID(), f.game->getGameCycleCount());
    f.enemyStructure("Wind Trap 14 tiles off", Structure_WindTrap, 20, 24);
    const auto* tank = f.enemyUnit("Tank 8 tiles off", Unit_Tank, 28, 10);
    auto* saboteur = f.owner->createUnit(Unit_Saboteur, false);
    require(saboteur != nullptr, f.label + ": cannot create the Saboteur");
    saboteur->deploy(Coord(20, 10));
    const Uint32 saboteurID = saboteur->getObjectID();
    saboteur->doSetAttackMode(AREAGUARD);
    const Uint32 victimID = tank->getObjectID();
    const int seconds = f.runUntilGone(saboteurID, 120);
    f.requireOnlyVictim(saboteurID, victimID);
    SDL_Log("SABOTEUR_TARGET_CASE: %s took the Tank inside its guard range after %d s; the Wind Trap beyond it is untouched",
            f.label.c_str(), seconds);
}

/// Its owner must see what it is about to blow up. The target stands in ground the owner never
/// explored; the Saboteur itself sees one tile. From four tiles out the target is shown to the
/// owner, so there is a second or more to see it before the blast, and the blast is shown too.
inline void targetShownBeforeBlast(const std::string& ownerClass, bool building) {
    Fixture f(ownerClass, building ? "building-shown-before-blast" : "vehicle-shown-before-blast");
    const ObjectBase* victim = building
        ? static_cast<const ObjectBase*>(f.enemyStructure("Refinery 12 tiles east", Structure_Refinery, 45, 30))
        : static_cast<const ObjectBase*>(f.enemyUnit("Tank 12 tiles east", Unit_Tank, 44, 31));
    const Uint32 victimID = victim->getObjectID();
    const int house = f.owner->getHouseID();
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    require(!currentGameMap->getTile(victim->getClosestPoint(saboteur->getLocation()))->isExploredByHouse(house),
            f.label + ": the fixture's target stands on ground the owner has already explored");
    Uint32 shownCycle = 0;
    int shownDistance = -1;
    const Uint32 deadline = f.game->getGameCycleCount() + MILLI2CYCLES(120 * 1000);
    while(f.game->getGameCycleCount() < deadline && f.alive(saboteurID)) {
        const Coord nearest = victim->getClosestPoint(saboteur->getLocation());
        if(shownCycle == 0 && currentGameMap->getTile(nearest)->isExploredByHouse(house)) {
            shownCycle = f.game->getGameCycleCount();
            shownDistance = blockDistance(saboteur->getLocation(), nearest).lround();
        }
        f.game->updateGameState();
    }
    f.requireOnlyVictim(saboteurID, victimID);
    const int shownMs = shownCycle == 0 ? 0
        : static_cast<int>((f.game->getGameCycleCount() - shownCycle) * GAMESPEED_DEFAULT);
    SDL_Log("SABOTEUR_TARGET_SHOWN: %s target shown to its owner %d ms before the blast, from %d tiles",
            f.label.c_str(), shownMs, shownDistance);
    require(shownMs >= 1000, f.label + ": the owner saw the target for " + std::to_string(shownMs)
            + " ms before the blast (at least 1000 required)");
    require(!f.game->getExplosionList().empty(), f.label + ": the sabotage left no blast");
    SDL_Log("SABOTEUR_TARGET_CASE: %s showed its owner the %s for %d ms before blowing it up",
            f.label.c_str(), building ? "Refinery" : "Tank", shownMs);
}

/// A Saboteur that is shot has sabotaged nothing and must not look as if it had: it falls like
/// any foot soldier (a body on the ground), with no blast.
inline void shotSaboteurFalls(const std::string& ownerClass) {
    Fixture f(ownerClass, "shot-on-the-way");
    const auto* tank = f.enemyUnit("Tank 20 tiles east", Unit_Tank, 52, 31);
    auto* saboteur = f.release();
    const Uint32 saboteurID = saboteur->getObjectID();
    for(int cycle = 0; cycle < MILLI2CYCLES(3 * 1000); ++cycle) f.game->updateGameState();
    require(f.alive(saboteurID) && saboteur->getTarget() == tank, f.label + ": the Saboteur was not on its way to the Tank");
    require(f.game->getExplosionList().empty(), f.label + ": the fixture already shows an explosion");
    const Coord where = saboteur->getLocation();
    saboteur->handleDamage(saboteur->getHealth().lround() + 1, tank->getObjectID(), f.enemy);
    for(int cycle = 0; cycle < 3 && f.alive(saboteurID); ++cycle) f.game->updateGameState();
    require(!f.alive(saboteurID), f.label + ": the shot Saboteur is still alive");
    require(f.alive(tank->getObjectID()), f.label + ": the Tank was destroyed by a Saboteur that never reached it");
    require(f.game->getExplosionList().empty(), f.label + ": a shot Saboteur blew up as if it had sabotaged something");
    require(currentGameMap->getTile(where)->hasDeadUnits(), f.label + ": a shot Saboteur left no body");
    SDL_Log("SABOTEUR_TARGET_CASE: %s was shot on the way and fell like a foot soldier, with no blast", f.label.c_str());
}

} // namespace SaboteurTargetProbe

int runSaboteurTargetProbe() {
    using namespace SaboteurTargetProbe;
    const std::string mod = std::getenv("SABOTEUR_TARGET_PROBE_MOD");
    require(ModManager::instance().setActiveMod(mod), "missing fixture mod " + mod);
    for(const char* ownerClass : {"HumanPlayer", "qBotHard"}) {
        nearestIsABuilding(ownerClass);
        nearestIsAVehicle(ownerClass);
        nearerThingTurnsUp(ownerClass);
        workersAreVehicles(ownerClass);
        structureWithoutVehicles(ownerClass);
        nothingToSabotage(ownerClass);
        targetShownBeforeBlast(ownerClass, false);
        targetShownBeforeBlast(ownerClass, true);
        shotSaboteurFalls(ownerClass);
    }
    explicitOrderKept();
    areaGuardTakesNearest();
    SDL_Log("SABOTEUR_TARGET_PROBE_PASS: mod=%s human and QuantBot owners, 20 cases", mod.c_str());
    return -1;
}
