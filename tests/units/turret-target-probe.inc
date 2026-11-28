// Included only by run-turret-target-probe.py, never by the shipped game.
//
// Turrets Can Fire On Buildings (protocol 147, default off): Rocket and Gun Turrets fire on units
// only, as the original game's turret script did, never on buildings or city zones. Each check is a fresh
// city game on a bare rock map, played by Game::updateGameState() alone; the attack order goes
// through the command manager as a player's click does (CMD_TURRET_ATTACKOBJECT).
//
//   default (off) an enemy Wind Trap and an enemy Residential zone three tiles from an own Rocket Turret and
//         Gun Turret take no damage, by the turrets' own search or by an attack order on each; an
//         enemy Quad driven up beside them is shot
//   on    with the option on, the same turrets damage the enemy building or zone (the protocol 146
//         behaviour)
#include <Command.h>
#include <FileClasses/INIFile.h>
#include <globals.h>
#include <CommandManager.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <mod/ModManager.h>
#include <players/Player.h>
#include <structures/StructureBase.h>
#include <structures/TurretBase.h>
#include <units/GroundUnit.h>
#include <sand.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace TurretTargetProbe {

constexpr int kMapSize = 64;
std::string gOut;
SettingsClass::GameOptionsClass gModOptions;
std::ofstream gChecks;

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("TURRET_TARGET_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

void record(const std::string& check, const std::string& detail) {
    gChecks << check << ",\"" << detail << "\"\n";
    gChecks.flush();
    SDL_Log("TURRET_TARGET_PROBE_CHECK: %s %s", check.c_str(), detail.c_str());
}

std::string fixtureMap() {
    std::string data = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTimeOut=0\nTechLevel=8\n[MAP]\nSizeX="
                       + std::to_string(kMapSize) + "\nSizeY=" + std::to_string(kMapSize) + "\n";
    for(int row = 0; row < kMapSize; ++row) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", row);
        data += key + std::string(kMapSize, '%') + "\n";
    }
    // MCVs keep the houses alive, far from everything.
    data += "[Player1]\nCredits=5000\nQuota=0\n[Player2]\nCredits=0\nQuota=0\n"
            "[UNITS]\nID001=Player1,MCV,256,0,64,Area Guard\nID002=Player2,MCV,256,"
            + std::to_string(kMapSize * kMapSize - 1) + ",64,Area Guard\n";
    return data;
}

/// Atreides is the human tester with the turrets; Harkonnen an AI Player house with no credits
/// (it cannot repair what is hit).
std::unique_ptr<Game> newGame(const std::string& mapData, bool unitsOnly) {
    auto options = gModOptions;
    options.turretsFireOnBuildings = !unitsOnly;
    options.immortalHumanPlayer = false;
    GameInitSettings init("turret-target.ini", mapData, "Turret target", true, options);
    init.randomSeed = 4711;
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.addPlayerInfo({"Turret tester", "HumanPlayer"});
    init.addHouseInfo(human);
    GameInitSettings::HouseInfo ai(HOUSE_HARKONNEN, 2);
    ai.addPlayerInfo({"Turret AI", "AIPlayer:hard"});
    init.addHouseInfo(ai);

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // Private fixture, never a public match-analytics sample.
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    require(game->getGameInitSettings().getGameOptions().turretsFireOnBuildings == !unitsOnly,
            "The game did not keep its Turrets Can Fire On Buildings rule");
    return game;
}

StructureBase* place(House* house, int item, Coord at) {
    auto* structure = house->placeStructure(NONE_ID, item, at.x, at.y, true, true);
    require(structure != nullptr, "Could not place item " + std::to_string(item));
    structure->setVisible(VIS_ALL, true);
    return structure;
}

int health(Uint32 id) {
    const auto* object = currentGame->getObjectManager().getObject(id);
    return object ? int(lround(object->getHealth())) : 0;
}

struct Fixture {
    std::unique_ptr<Game> game;
    Uint32 rocket = NONE_ID, gun = NONE_ID, trap = NONE_ID, zone = NONE_ID;
};

Fixture build(const std::string& mapData, bool unitsOnly) {
    Fixture f;
    f.game = newGame(mapData, unitsOnly);
    House* own = f.game->getHouse(HOUSE_ATREIDES);
    House* enemy = f.game->getHouse(HOUSE_HARKONNEN);
    for(int i = 0; i < 3; ++i) place(own, Structure_WindTrap, Coord(2, 2 + 3 * i));
    f.rocket = place(own, Structure_RocketTurret, Coord(30, 30))->getObjectID();
    f.gun = place(own, Structure_GunTurret, Coord(30, 34))->getObjectID();
    f.trap = place(enemy, Structure_WindTrap, Coord(33, 31))->getObjectID();
    f.zone = place(enemy, Structure_ZoneResidential, Coord(26, 32))->getObjectID();
    require(own->hasPower(), "The turrets are not powered");
    for(int i = 0; i < 5; ++i) f.game->updateGameState();
    return f;
}

/// Plays \a cycles cycles and returns the lowest health each object had.
std::vector<int> play(Fixture& f, const std::vector<Uint32>& watched, int cycles) {
    std::vector<int> lowest;
    for(const Uint32 id : watched) lowest.push_back(health(id));
    int held[2] = {0, 0};
    for(int c = 0; c < cycles; ++c) {
        f.game->updateGameState();
        for(size_t i = 0; i < watched.size(); ++i) lowest[i] = std::min(lowest[i], health(watched[i]));
        const Uint32 turrets[2] = {f.rocket, f.gun};
        for(int t = 0; t < 2; ++t) {
            const auto* turret = dynamic_cast<const TurretBase*>(f.game->getObjectManager().getObject(turrets[t]));
            require(turret != nullptr, "A turret was lost");
            const auto* target = turret->getTarget();
            // An attack order sets the target; the turret's next update must drop a building.
            held[t] = target != nullptr && target->isAStructure() ? held[t] + 1 : 0;
            if(!f.game->getGameInitSettings().getGameOptions().turretsFireOnBuildings)
                require(held[t] <= 2, "A turret kept a building as its target");
        }
    }
    return lowest;
}

void checkOn(const std::string& mapData) {
    Fixture f = build(mapData, true);
    const int trap = health(f.trap), zone = health(f.zone);
    auto lowest = play(f, {f.trap, f.zone}, 600);
    require(lowest[0] == trap && lowest[1] == zone, "A turret fired on a building by its own search");
    // A player's attack order on each building: the turret drops it.
    auto& commands = f.game->getCommandManager();
    commands.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_TURRET_ATTACKOBJECT, f.rocket, f.trap));
    commands.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_TURRET_ATTACKOBJECT, f.gun, f.zone));
    lowest = play(f, {f.trap, f.zone}, 600);
    require(lowest[0] == trap && lowest[1] == zone, "A turret fired on a building it was ordered to attack");
    // An enemy Quad beside both turrets is shot.
    House* enemy = f.game->getHouse(HOUSE_HARKONNEN);
    auto* quad = dynamic_cast<GroundUnit*>(enemy->createUnit(Unit_Quad, true));
    require(quad != nullptr, "Could not create the Quad");
    quad->deploy(Coord(32, 33));
    quad->setVisible(VIS_ALL, true);
    quad->doSetAttackMode(GUARD);
    const Uint32 quadID = quad->getObjectID();
    const int quadHealth = health(quadID);
    lowest = play(f, {quadID, f.trap, f.zone}, 600);
    require(lowest[0] < quadHealth, "The turrets did not fire on the enemy Quad");
    require(lowest[1] == trap && lowest[2] == zone, "A building was hit while the turrets shot the Quad");
    record("on", "building " + std::to_string(trap) + "->" + std::to_string(lowest[1]) + ", zone "
                 + std::to_string(zone) + "->" + std::to_string(lowest[2]) + ", quad "
                 + std::to_string(quadHealth) + "->" + std::to_string(lowest[0]));
}

void checkOff(const std::string& mapData) {
    Fixture f = build(mapData, false);
    const int trap = health(f.trap), zone = health(f.zone);
    const auto lowest = play(f, {f.trap, f.zone}, 600);
    require(lowest[0] < trap || lowest[1] < zone, "With the option on no turret fired on a building");
    record("off", "building " + std::to_string(trap) + "->" + std::to_string(lowest[0]) + ", zone "
                  + std::to_string(zone) + "->" + std::to_string(lowest[1]));
}

/// The options INI: the current key round-trips, an INI with only the earlier inverted key
/// "Turrets Target Units Only" keeps working, the current key wins when both are present, and an
/// INI with neither leaves the rule alone.
void checkIni() {
    const std::string section = "Game Options";
    for(const bool value : {false, true}) {
        SettingsClass::GameOptionsClass written;
        written.turretsFireOnBuildings = value;
        INIFile config(false, std::string());
        writeGameOptionsToConfig(config, section, written);
        require(config.hasKey(section, "Turrets Can Fire On Buildings")
                && !config.hasKey(section, "Turrets Target Units Only"), "The options INI does not write the current key");
        SettingsClass::GameOptionsClass read;
        read.turretsFireOnBuildings = !value;
        applyGameOptionsFromConfig(config, section, read);
        require(read.turretsFireOnBuildings == value && read.getHash() == written.getHash(),
                "Turrets Can Fire On Buildings did not round-trip through the options INI");
    }
    for(const bool unitsOnly : {false, true}) {
        INIFile config(false, std::string());
        config.setBoolValue(section, "Turrets Target Units Only", unitsOnly);
        SettingsClass::GameOptionsClass read;
        read.turretsFireOnBuildings = unitsOnly;   // the opposite of what the old key means
        applyGameOptionsFromConfig(config, section, read);
        require(read.turretsFireOnBuildings == !unitsOnly, "The earlier key Turrets Target Units Only was not read, inverted");
    }
    {
        INIFile config(false, std::string());
        config.setBoolValue(section, "Turrets Target Units Only", false);
        config.setBoolValue(section, "Turrets Can Fire On Buildings", false);
        SettingsClass::GameOptionsClass read;
        read.turretsFireOnBuildings = true;
        applyGameOptionsFromConfig(config, section, read);
        require(!read.turretsFireOnBuildings, "The earlier key won over Turrets Can Fire On Buildings");
    }
    {
        INIFile config(false, std::string());
        config.setBoolValue(section, "Hospital Care", true);
        SettingsClass::GameOptionsClass read;
        applyGameOptionsFromConfig(config, section, read);
        require(!read.turretsFireOnBuildings, "An INI without either key changed the turret rule");
    }
    record("ini", "current key round-trips; earlier key read inverted; current key wins; neither leaves the default");
}

} // namespace TurretTargetProbe

int runTurretTargetProbe() {
    using namespace TurretTargetProbe;
    const char* outEnv = std::getenv("TURRET_TARGET_OUT");
    require(outEnv != nullptr, "Missing TURRET_TARGET_OUT");
    gOut = outEnv;
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    settings.general.playerName = "Turret tester";
    gModOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    gModOptions.garrisoningAndCapturing = false;
    require(!gModOptions.turretsFireOnBuildings, "Turrets Can Fire On Buildings is not off by default");
    const std::string mapData = fixtureMap();
    gChecks.open(gOut + "/checks.csv");
    gChecks << "check,detail\n";
    checkIni();
    checkOn(mapData);
    checkOff(mapData);
    gChecks.close();
    SDL_Log("TURRET_TARGET_PROBE_PASS");
    return -1;
}
