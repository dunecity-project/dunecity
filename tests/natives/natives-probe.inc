// Included only by run-natives-probe.py, never by the shipped game. Starts real matches in the
// real engine and checks the Natives house (HOUSE_NATIVES): every sandworm and every trooper that
// city crime raises belongs to it, it is never listed or counted as a house, and win/lose ignores
// it. NATIVES_BASELINE builds only the case that writes the 9868 save fixture, against a tree
// without the Natives house (no new API is used there).
#include <Menu/RandomSkirmishMenu.h>
#include <Menu/CustomGameStatsMenu.h>
#include <MapEditor/SkirmishGenerator.h>
#include <players/PlayerFactory.h>
#include <players/HumanPlayer.h>
#include <players/QuantBot.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <units/SandWorm.h>
#include <Trigger/ReinforcementTrigger.h>
#include <mod/ModManager.h>
#include <misc/FileSystem.h>
#include <misc/string_util.h>
#include <sand.h>
#ifndef NATIVES_BASELINE
#include <GUI/dune/ObservedHouseSelector.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>
#include <Network/GameInitSettingsPolicy.h>
#include <dunecity/CitySimulation.h>
#endif

#include <cstdlib>
#include <set>

namespace {

using namespace SkirmishGenerator;

void nRequire(bool ok, const std::string& what) {
    if(!ok) { SDL_Log("NATIVES_PROBE_FAIL: %s", what.c_str()); throw std::runtime_error(what); }
}

std::string nOut(const std::string& name) { return std::string(std::getenv("NATIVES_PROBE_OUT")) + "/" + name; }

std::string houseName(int h) { return getHouseNameByNumber(static_cast<HOUSETYPE>(h)); }

std::unique_ptr<Game> nStart(const GameInitSettings& init) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;   // no briefing / analytics at setup
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    return game;
}

void nEnd(std::unique_ptr<Game>& game) {
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
}

void runCycles(Game& game, Uint32 cycles) {
    const Uint32 end = game.getGameCycleCount() + cycles;
    while(game.getGameCycleCount() < end && !game.finishedLevel) game.updateGameState();
}

std::vector<UnitBase*> worms() {
    std::vector<UnitBase*> found;
    for(UnitBase* unit : unitList) if(unit->getItemID() == Unit_Sandworm) found.push_back(unit);
    return found;
}

GameInitSettings bundledMap(const std::string& mod, const std::string& map, const std::vector<std::pair<int, std::string>>& houses,
                            bool spectate) {
    nRequire(RandomSkirmish::activateMod(mod), "Mod " + mod + " did not activate");
    const std::string path = std::string(std::getenv("NATIVES_SOURCE_DIR")) + "/data/maps/multiplayer/" + map + ".ini";
    GameInitSettings init(getBasename(path, true), readCompleteFile(path), false, effectiveGameOptions);
    init.setLocalSpectator(spectate);
    int team = 1;
    for(const auto& [house, playerClass] : houses) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(house), team++);
        info.addPlayerInfo(GameInitSettings::PlayerInfo(playerClass == HUMANPLAYERCLASS ? settings.general.playerName : houseName(house), playerClass));
        init.addHouseInfo(info);
    }
    init.randomSeed = 20261022;
    return init;
}

#ifdef NATIVES_BASELINE

/// The 9868 fixture: Gamma Sector names Player1 as the owner of its 87 worms, so in the old engine
/// those worms belong to whichever played house took that slot.
void makeSave() {
    auto game = nStart(bundledMap("vanilla", "4P - 128x64 - Gamma Sector",
        {{HOUSE_ATREIDES, HUMANPLAYERCLASS}, {HOUSE_HARKONNEN, "qBotEasy"}, {HOUSE_ORDOS, "qBotEasy"}, {HOUSE_SARDAUKAR, "qBotEasy"}}, false));
    runCycles(*game, 150);
    std::map<int, int> owners;
    for(const UnitBase* worm : worms()) ++owners[worm->getOwner()->getHouseID()];
    bool playerOwned = false;
    for(const auto& [house, count] : owners) {
        SDL_Log("NATIVES_PROBE: 9868 worms owned by %s: %d (players %zu)", houseName(house).c_str(), count,
                game->getHouse(house)->getPlayerList().size());
        playerOwned = playerOwned || !game->getHouse(house)->getPlayerList().empty();
    }
    nRequire(playerOwned, "No worm of the fixture belongs to a played house");
    nRequire(game->saveGame(nOut("worms-9868.dls")), "Could not write the 9868 save");
    nEnd(game);
}

#else

House* natives(Game& game) { return game.getHouse(HOUSE_NATIVES); }

/// Every worm is the Natives house's, the counts follow it, and no other house counts a worm.
void expectWormsNative(Game& game, const std::string& label, size_t minimum) {
    const auto all = worms();
    nRequire(all.size() >= minimum, label + ": " + std::to_string(all.size()) + " worms, expected at least " + std::to_string(minimum));
    House* owner = natives(game);
    nRequire(all.empty() || owner != nullptr, label + ": worms but no Natives house");
    for(const UnitBase* worm : all) {
        nRequire(worm->getOwner() == owner, label + ": worm " + std::to_string(worm->getObjectID()) + " belongs to "
                 + houseName(worm->getOwner()->getHouseID()));
        nRequire(worm->getOriginalHouseID() == HOUSE_NATIVES, label + ": a worm's original house is not Natives");
    }
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(const House* house = game.getHouse(h); house && h != HOUSE_NATIVES)
            nRequire(house->getNumItems(Unit_Sandworm) == 0, label + ": " + houseName(h) + " still counts a worm");
    if(owner) {
        nRequire(owner->getNumItems(Unit_Sandworm) == static_cast<int>(all.size()), label + ": the Natives' worm count is wrong");
        nRequire(owner->getTeamID() == NATIVES_TEAM && owner->getPlayerList().empty() && owner->getNumStructures() == 0,
                 label + ": the Natives house is off its team, has a player or a base");
        nRequire(owner->isNatives() && houseName(HOUSE_NATIVES) == "Natives", label + ": the house is not named Natives");
    }
    SDL_Log("NATIVES_PROBE: %s: %zu worms, all owned by Natives (team %d, no players)", label.c_str(), all.size(), NATIVES_TEAM);
}

/// Natives is no house anybody chooses, browses or ranks.
void expectHidden(Game& game, const std::string& label) {
    nRequire(!isHouseAvailable(HOUSE_NATIVES) && !isCustomGameHouseAvailable(HOUSE_NATIVES), label + ": Natives is selectable");
    nRequire(getNumCustomGameHouses() <= HOUSE_NATIVES && !isPlayerHouseID(HOUSE_NATIVES), label + ": Natives counts as a player house");
    nRequire(getHouseByName("Natives") == HOUSE_INVALID, label + ": a map can name Natives");
    nRequire(!GameInitSettingsPolicy::isKnownHouse(HOUSE_NATIVES), label + ": a lobby packet may name Natives");
    for(int h : ObservedHouse::browsable()) nRequire(h != HOUSE_NATIVES, label + ": the observed-house selector offers Natives");
    {
        CustomGameStatsMenu stats;
        nRequire(stats.houseStat[HOUSE_NATIVES].houseName.getText().empty(), label + ": the statistics have a Natives row");
        for(int h = 0; h < NUM_HOUSES; ++h)
            nRequire(stats.houseStat[h].houseName.getText().find("Natives") == std::string::npos, label + ": a statistics row names Natives");
    }
    SDL_Log("NATIVES_PROBE: %s: Natives absent from house choices, observed-house selector and statistics", label.c_str());
}

/// What a player sees of a Natives unit: the inspect panel names Natives, and the radar draws it in
/// the Natives' own colour (worms stay white, as before).
void expectShownAsNatives(Game& game, const UnitBase* unit, const std::string& label) {
    auto* panel = DefaultObjectInterface::create(unit->getObjectID());
    const std::string shown = panel->getOwnerName();
    delete panel;
    nRequire(shown == "Natives", label + ": the inspect panel names '" + shown + "'");
    const Uint32 nativesRadar = getHouseRadarColor(HOUSE_NATIVES);
    nRequire(nativesRadar == getHouseColorRGB(HOUSECOLOR_NATIVES, 0), label + ": the Natives' radar colour is not their own");
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(game.getHouse(h) && h != HOUSE_NATIVES)
            nRequire(getHouseRadarColor(static_cast<HOUSETYPE>(h)) != nativesRadar, label + ": " + houseName(h) + " shares the Natives' radar colour");
    if(game.isObserving() && unit->isActive()) {
        Tile* tile = currentGameMap->getTile(unit->getLocation());
        const Uint32 expected = unit->getItemID() == Unit_Sandworm ? COLOR_WHITE : nativesRadar;
        nRequire(tile->getRadarColor(pLocalHouse, true) == expected, label + ": the radar shows the unit in another colour");
    }
    SDL_Log("NATIVES_PROBE: %s: inspect panel says '%s', radar colour %08x", label.c_str(), shown.c_str(), nativesRadar);
}

/// Places @a itemID for @a house on the nearest free tile around @a centre (sand only for worms).
UnitBase* placeNear(House* house, int itemID, Coord centre) {
    for(int ring = 0; ring < 40; ++ring)
        for(int dy = -ring; dy <= ring; ++dy)
            for(int dx = -ring; dx <= ring; ++dx) {
                if(std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                const int x = centre.x + dx, y = centre.y + dy;
                if(!currentGameMap->tileExists(x, y)) continue;
                const Tile* tile = currentGameMap->getTile(x, y);
                if(itemID == Unit_Sandworm ? !tile->isSand() : (tile->isMountain() || tile->hasAGroundObject())) continue;
                if(UnitBase* unit = house->placeUnit(itemID, x, y)) return unit;
            }
    nRequire(false, "No room to place " + getItemNameByID(itemID));
    return nullptr;
}

Params skirmishParams(const std::string& mod, int enemies, bool spectate) {
    Params p;
    p.house = HOUSE_ORDOS;
    p.seed = 20261022;
    p.techLevel = 8;
    p.mod = mod;
    p.mode = RandomSkirmish::modeForMod(mod);
    p.mapSize = p.mode == Mode::DuneCity ? 96 : 64;
    p.allies = 0;
    p.enemies = enemies;
    p.enemiesAllied = false;
    const int city = p.mode == Mode::DuneCity ? 1 : 0;
    p.enemySide = Side{"qBotEasy", BaseSize::Small, 1, city};
    p.playerSide = Side{"", BaseSize::Small, 1, city};
    p.spectate = spectate;
    p.spectateClass = "qBotMedium";
    return p;
}

Result generateFor(const Params& p) {
    nRequire(RandomSkirmish::activateMod(p.mod), "Mod " + p.mod + " did not activate");
    Result result = SkirmishGenerator::generate(p, RandomSkirmish::loadCatalogue(p.mode));
    for(const HouseSetup& h : result.houses) nRequire(h.skipped == 0, "The generator could not place everything");
    return result;
}

std::unique_ptr<Game> startSkirmish(const Params& p, const Result& result) {
    GameInitSettings init = RandomSkirmish::makeInit(result, effectiveGameOptions, p);
    init.randomSeed = p.seed;
    return nStart(init);
}

/// The house slot every peer's simulation names in the map: the generated INI's own house name.
std::string iniHouse(int h) { return houseName(h); }

/// Worms on bundled maps (a vanilla 4P map whose worms a played house owned, a DuneCity 1v1 whose
/// worms named an absent house), a generated Random Skirmish with a worm in its INI, worms spawned
/// later by any house, and a campaign mission.
void runMaps() {
    {
        // Equilibrium: 135 worms, every one written as Harkonnen's - here a played QuantBot house.
        auto game = nStart(bundledMap("vanilla", "4P - 128x128 - Equilibrium",
            {{HOUSE_ATREIDES, "qBotEasy"}, {HOUSE_HARKONNEN, "qBotEasy"}, {HOUSE_ORDOS, "qBotEasy"}, {HOUSE_SARDAUKAR, "qBotEasy"}}, true));
        nRequire(!game->getHouse(HOUSE_HARKONNEN)->getPlayerList().empty(), "Harkonnen does not play Equilibrium");
        expectWormsNative(*game, "vanilla Equilibrium", 100);
        runCycles(*game, 300);
        expectWormsNative(*game, "vanilla Equilibrium after 300 cycles", 100);
        expectHidden(*game, "vanilla Equilibrium");
        for(const UnitBase* worm : worms()) if(worm->isActive()) { expectShownAsNatives(*game, worm, "Equilibrium worm"); break; }
        nEnd(game);
    }
    {
        // DuneCity mod, a 1v1 whose worms name Fremen, who does not play: no anonymous Fremen house.
        auto game = nStart(bundledMap("dunecity", "2P - 128x128 - 1v1 - Habbanya-Penny",
            {{HOUSE_ORDOS, "qBotEasy"}, {HOUSE_HARKONNEN, "qBotEasy"}}, true));
        expectWormsNative(*game, "DuneCity Habbanya-Penny", 10);
        nRequire(game->getHouse(HOUSE_FREMEN) == nullptr, "A Fremen house was created for the map's worms");
        expectHidden(*game, "DuneCity Habbanya-Penny");
        nEnd(game);
    }
    {
        // Random Skirmish: a worm written into the generated map as the player's own house.
        const Params p = skirmishParams("vanilla", 1, false);
        Result result = generateFor(p);
        int size = result.map.getSizeX(), pos = -1;
        for(int y = size / 2 - 8; y < size / 2 + 8 && pos < 0; ++y)
            for(int x = size / 2 - 8; x < size / 2 + 8 && pos < 0; ++x)
                if(result.map(x, y) == Terrain_Sand) pos = y * size + x;
        nRequire(pos >= 0, "No sand in the middle of the generated map");
        const std::string section = "[UNITS]\n";
        const auto at = result.ini.find(section);
        nRequire(at != std::string::npos, "The generated map has no [UNITS] section");
        result.ini.insert(at + section.size(), "ID9001=" + iniHouse(HOUSE_ORDOS) + ",Sandworm,256," + std::to_string(pos) + ",64,Area Guard\n");
        auto game = startSkirmish(p, result);
        expectWormsNative(*game, "Random Skirmish INI worm", 1);
        // Later worms: whichever house asks for one (a reinforcement, a mod) gets a Natives worm.
        House* ordos = game->getHouse(HOUSE_ORDOS);
        const int ordosUnits = ordos->getNumUnits();
        UnitBase* spawned = placeNear(ordos, Unit_Sandworm, Coord(size / 2, size / 2));
        nRequire(spawned->getOwner() == natives(*game) && ordos->getNumUnits() == ordosUnits,
                 "A worm Ordos created belongs to Ordos or counts as its unit");
        expectWormsNative(*game, "Random Skirmish spawned worm", 2);
        expectShownAsNatives(*game, spawned, "spawned worm");
        runCycles(*game, 200);
        expectWormsNative(*game, "Random Skirmish after 200 cycles", 1);
        nEnd(game);
    }
    {
        // Campaign: the first Atreides mission (of a few) whose scenario places worms.
        bool found = false;
        for(int mission : {5, 8, 11, 2, 14, 17, 20, 3, 6, 9, 12}) {
            nRequire(RandomSkirmish::activateMod("vanilla"), "Mod vanilla did not activate");
            GameInitSettings init(HOUSE_ATREIDES, effectiveGameOptions, missionNumberToLevelNumber(mission));
            init.mission = mission;
            init.filename = GameInitSettings::getScenarioFilename(HOUSE_ATREIDES, mission);
            for(int h = 0; h < NUM_HOUSES; ++h)
                if(isHouseAvailable(static_cast<HOUSETYPE>(h))) {
                    GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h), h == HOUSE_ATREIDES ? 1 : 2);
                    if(h == HOUSE_ATREIDES) info.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
                    info.addPlayerInfo({houseName(h), "qBotMedium"});
                    init.addHouseInfo(info);
                }
            init.randomSeed = 20261022;
            auto game = nStart(init);
            if(!worms().empty()) {
                expectWormsNative(*game, "campaign mission " + std::to_string(mission), 1);
                expectHidden(*game, "campaign mission " + std::to_string(mission));
                found = true;
            } else {
                SDL_Log("NATIVES_PROBE: campaign mission %d places no worms", mission);
            }
            nEnd(game);
            if(found) break;
        }
        nRequire(found, "No tried campaign mission places a worm");
    }
}

/// City crime: a DuneCity match where the troopers rise in a QuantBot house's base. They are the
/// Natives', hunt the building whose district broke into unrest, are no army in that house's threat
/// estimate, are fought off by its defence, and survive a save and a reload as the Natives'.
void runCrime() {
    const Params p = skirmishParams("dunecity", 1, true);
    const Result result = generateFor(p);
    auto game = startSkirmish(p, result);
    runCycles(*game, 150);
    nRequire(natives(*game) == nullptr, "A Natives house exists although nothing needed one");
    int enemy = -1;
    for(const HouseSetup& h : result.houses) if(h.role == Role::Enemy) enemy = h.house;
    House* victim = game->getHouse(enemy);
    QuantBot* bot = nullptr;
    for(const auto& player : victim->getPlayerList()) if(auto* b = dynamic_cast<QuantBot*>(player.get())) bot = b;
    nRequire(bot != nullptr, "The enemy house has no QuantBot");
    const StructureBase* origin = nullptr;
    for(const StructureBase* s : structureList)
        if(s->getOwner() == victim && (!origin || s->getItemID() == Structure_ConstructionYard)) origin = s;
    nRequire(origin != nullptr, "The enemy house has no building");
    // More Natives worms than the house's unit limit (Equilibrium has 135): worms are no army, so
    // they must not use up the limit the crime troopers rise under.
    House* nativesHouse = game->getOrCreateNativesHouse();
    nativesHouse->maxUnits = 20;    // this skirmish has no unit limit; a map-size one, as on most maps
    const Coord middle(currentGameMap->getSizeX() / 2, currentGameMap->getSizeY() / 2);
    std::vector<Uint32> wormIds;
    while(static_cast<int>(wormIds.size()) < nativesHouse->getMaxUnits() + 5)
        wormIds.push_back(placeNear(nativesHouse, Unit_Sandworm, middle)->getObjectID());
    SDL_Log("NATIVES_PROBE: %zu Natives worms against a unit limit of %d", wormIds.size(), nativesHouse->getMaxUnits());
    bot->updateEnemySightings();
    const int64_t threatBefore = bot->enemyThreatEstimate();
    const int victimTroopers = victim->getNumItems(Unit_Trooper);
    const auto troops = game->getCitySimulation()->raiseCrimeTroopers(origin, 3);
    nRequire(troops.natives != nullptr && troops.natives == natives(*game), "Crime raised no troopers or not for Natives");
    nRequire(troops.units.size() == 3, "Crime raised " + std::to_string(troops.units.size()) + " troopers, expected 3");
    for(const UnitBase* unit : troops.units) {
        nRequire(unit->getItemID() == Unit_Trooper && unit->getOwner() == troops.natives && unit->getOriginalHouseID() == HOUSE_NATIVES,
                 "A crime trooper is not a Natives trooper");
        nRequire(unit->getAttackMode() == HUNT && unit->getTarget() == origin, "A crime trooper does not hunt the unrest's building");
    }
    nRequire(victim->getNumItems(Unit_Trooper) == victimTroopers && troops.natives->getNumItems(Unit_Trooper) == 3,
             "The crime troopers are counted by the wrong house");
    nRequire(troops.natives->getTeamID() == NATIVES_TEAM && troops.natives->getPlayerList().empty(), "The Natives house is off its team or has a player");
    bot->updateEnemySightings();
    for(const auto& [id, sighting] : bot->enemySightings)
        nRequire(!game->getObjectManager().getObject(id) || !game->getObjectManager().getObject(id)->getOwner()->isNatives(),
                 "QuantBot remembers a crime trooper as enemy army");
    nRequire(bot->enemyThreatEstimate() == threatBefore, "Crime troopers changed QuantBot's enemy threat estimate");
    for(Uint32 id : wormIds) if(ObjectBase* worm = game->getObjectManager().getObject(id)) worm->destroy();
    expectShownAsNatives(*game, troops.units.front(), "crime trooper");
    expectHidden(*game, "DuneCity crime");
    SDL_Log("NATIVES_PROBE: crime raised 3 Natives troopers at %s's %s; threat estimate %lld unchanged",
            houseName(enemy).c_str(), getItemNameByID(origin->getItemID()).c_str(), static_cast<long long>(threatBefore));

    // Save and reload: the Natives house and its troopers come back as they were.
    const std::string save = nOut("crime-9871.dls");
    nRequire(game->saveGame(save), "Could not save the crime match");
    std::vector<Uint32> ids;
    for(const UnitBase* unit : troops.units) ids.push_back(unit->getObjectID());
    nEnd(game);
    game = nStart(GameInitSettings(save));
    nRequire(game->getLoadedSavegameVersion() == SAVEGAMEVERSION, "The crime save is not the current version");
    nRequire(natives(*game) != nullptr && natives(*game)->getNumItems(Unit_Trooper) == 3, "The Natives house did not survive the reload");
    for(Uint32 id : ids) {
        const ObjectBase* unit = game->getObjectManager().getObject(id);
        nRequire(unit && unit->getOwner() == natives(*game), "A reloaded crime trooper is not the Natives'");
    }

    // The house defends against them as it always did: within two game minutes they are hurt or dead.
    auto natHealth = [&]() {
        FixPoint total = 0;
        for(Uint32 id : ids) if(const auto* u = game->getObjectManager().getObject(id)) total += u->getHealth();
        return total;
    };
    const FixPoint start = natHealth();
    const Uint32 limit = game->getGameCycleCount() + MILLI2CYCLES(120 * 1000);
    while(game->getGameCycleCount() < limit && natHealth() == start) game->updateGameState();
    nRequire(natHealth() < start, "Nobody fought the crime troopers in two game minutes");
    SDL_Log("NATIVES_PROBE: crime troopers engaged by cycle %u (health %.0f -> %.0f)", game->getGameCycleCount(),
            start.toDouble(), natHealth().toDouble());
    nEnd(game);
}

/// Every unit and structure of the house, until the engine counts it as defeated.
Uint32 eliminate(Game& game, int houseID) {
    House* house = game.getHouse(houseID);
    nRequire(house != nullptr && house->isAlive(), "Cannot eliminate house " + houseName(houseID));
    for(int round = 0; round < 64 && house->isAlive(); ++round) {
        std::vector<Uint32> doomed;
        for(const StructureBase* s : structureList) if(s->getOwner() == house) doomed.push_back(s->getObjectID());
        for(const UnitBase* u : unitList)
            if(u->getOwner() == house || u->getOriginalHouseID() == houseID) doomed.push_back(u->getObjectID());
        for(Uint32 id : doomed) if(ObjectBase* object = game.getObjectManager().getObject(id)) object->destroy();
        if(house->isAlive()) game.updateGameState();
    }
    nRequire(!house->isAlive(), "House " + houseName(houseID) + " survived its elimination");
    return game.getGameCycleCount();
}

/// Natives units on the map (troopers and a worm) away from every base.
void addNatives(Game& game) {
    House* owner = game.getOrCreateNativesHouse();
    const Coord centre(currentGameMap->getSizeX() / 2, currentGameMap->getSizeY() / 2);
    for(int i = 0; i < 3; ++i) placeNear(owner, Unit_Trooper, centre);
    placeNear(owner, Unit_Sandworm, centre);
    nRequire(owner->getNumUnits() >= 4, "The Natives units were not placed");
}

/// Win/lose ignores the Natives: a played game is won on the last enemy with Natives units alive, a
/// spectated one ends naming the last real team, and a Natives house with no units left loses nothing.
void runWinLose() {
    const Params played = skirmishParams("vanilla", 3, false);
    const Result result = generateFor(played);
    std::vector<int> enemies;
    for(const HouseSetup& h : result.houses) if(h.role == Role::Enemy) enemies.push_back(h.house);
    {
        auto game = startSkirmish(played, result);
        runCycles(*game, 60);
        addNatives(*game);
        Uint32 decided = 0;
        for(size_t i = 0; i < enemies.size(); ++i) {
            decided = eliminate(*game, enemies[i]);
            if(i + 1 < enemies.size()) nRequire(!game->finished, "The game ended with enemies left");
        }
        nRequire(natives(*game)->getNumUnits() > 0, "No Natives unit is left to test with");
        nRequire(game->finished && game->won && game->finishedLevelCycle == decided,
                 "The player did not win on the last enemy while Natives units are alive");
        SDL_Log("NATIVES_PROBE: played win at cycle %u with %d Natives units on the map", decided, natives(*game)->getNumUnits());
        while(!game->finishedLevel) game->updateGameState();
        nRequire(game->whatNext() == GAME_CUSTOM_GAME_STATS, "The won game does not go on to its statistics");
        expectHidden(*game, "played win");
        nEnd(game);
    }
    {
        // The Natives losing every unit is no defeat and no end.
        auto game = startSkirmish(played, result);
        runCycles(*game, 60);
        addNatives(*game);
        std::vector<Uint32> doomed;
        for(const UnitBase* u : unitList) if(u->getOwner() == natives(*game)) doomed.push_back(u->getObjectID());
        for(Uint32 id : doomed) if(ObjectBase* object = game->getObjectManager().getObject(id)) object->destroy();
        runCycles(*game, 30);
        nRequire(natives(*game)->getNumUnits() == 0 && natives(*game)->isAlive(), "The Natives house is not 'alive' without units");
        nRequire(!game->finished, "The game ended when the Natives lost their units");
        nEnd(game);
    }
    {
        // Spectated free-for-all: the view's house and two enemies go out; the last enemy has won,
        // although Natives troopers and a worm are still on the map.
        const Params watched = skirmishParams("vanilla", 3, true);
        const Result watchedResult = generateFor(watched);
        std::vector<int> others;
        for(const HouseSetup& h : watchedResult.houses) if(h.role == Role::Enemy) others.push_back(h.house);
        auto game = startSkirmish(watched, watchedResult);
        nRequire(game->isObserving(), "Spectate did not reach the match");
        runCycles(*game, 60);
        addNatives(*game);
        const int winner = others.back();
        eliminate(*game, HOUSE_ORDOS);
        nRequire(!game->finished, "The spectated game ended with three houses left");
        Uint32 decided = 0;
        for(size_t i = 0; i + 1 < others.size(); ++i) decided = eliminate(*game, others[i]);
        nRequire(game->finished && game->finishedLevelCycle - decided <= 2, "The spectated game did not end on the last real team");
        nRequire(game->getWatchedWinnerTeam() == game->getHouse(winner)->getTeamID() && game->soleSurvivingTeam() == game->getWatchedWinnerTeam(),
                 "The watched winner is not the last real team");
        nRequire(game->watchedResultText().find("Natives") == std::string::npos, "The end line names the Natives");
        SDL_Log("NATIVES_PROBE: spectated end at cycle %u: '%s' with %d Natives units on the map", game->finishedLevelCycle,
                game->watchedResultText().c_str(), natives(*game)->getNumUnits());
        expectHidden(*game, "spectated end");
        nEnd(game);
    }
}

/// A 9868 save (Gamma Sector, worms owned by a played house): its worms are the Natives' on load,
/// a 9871 save of it keeps them so, and both continue.
void runSave9868() {
    nRequire(RandomSkirmish::activateMod("vanilla"), "Mod vanilla did not activate");
    auto game = nStart(GameInitSettings(std::string(std::getenv("NATIVES_SAVE_9868"))));
    nRequire(game->getLoadedSavegameVersion() == 9868, "The fixture is not a 9868 save");
    expectWormsNative(*game, "9868 save", 40);
    expectHidden(*game, "9868 save");
    const size_t count = worms().size();
    runCycles(*game, 100);
    expectWormsNative(*game, "9868 save after 100 cycles", 40);
    const std::string save = nOut("worms-9871.dls");
    nRequire(game->saveGame(save), "Could not save the converted game");
    nEnd(game);
    game = nStart(GameInitSettings(save));
    nRequire(game->getLoadedSavegameVersion() == SAVEGAMEVERSION, "The re-saved game is not the current version");
    expectWormsNative(*game, "re-saved 9871", 40);
    nRequire(worms().size() == count, "The re-saved game has a different number of worms");
    runCycles(*game, 100);
    nEnd(game);
}

/// The 1.0.853 follow-up. A custom map's unlisted unit owner becomes an anonymous team-0 house: the
/// Natives fight it, and neither it nor the Natives decide the match. A worm a reinforcement brings
/// for a house never eats that house's side, also after a save and a reload. Built against a tree
/// without the follow-up (no NATIVES_TEAM), the case only logs what that tree does.
void runFollowup() {
    const Params played = skirmishParams("vanilla", 1, false);
    Result result = generateFor(played);
    int enemy = -1;
    std::set<int> playedHouses;
    for(const HouseSetup& h : result.houses) { playedHouses.insert(h.house); if(h.role == Role::Enemy) enemy = h.house; }
    const int anonymous = playedHouses.count(HOUSE_MERCENARY) ? HOUSE_FREMEN : HOUSE_MERCENARY;
    nRequire(!playedHouses.count(anonymous), "Both candidate anonymous houses play");
    // Two Trooper lines for the unlisted house in the middle of the map, as a custom map may write.
    const int size = result.map.getSizeX();
    std::vector<int> spots;
    for(int y = size / 2 - 10; y < size / 2 + 10 && spots.size() < 2; ++y)
        for(int x = size / 2 - 10; x < size / 2 + 10 && spots.size() < 2; ++x)
            if(result.map(x, y) == Terrain_Sand && (spots.empty() || spots.back() != y * size + x - 1)) spots.push_back(y * size + x);
    nRequire(spots.size() == 2, "No sand in the middle of the generated map");
    const std::string section = "[UNITS]\n";
    const auto at = result.ini.find(section);
    nRequire(at != std::string::npos, "The generated map has no [UNITS] section");
    for(size_t i = 0; i < spots.size(); ++i)
        result.ini.insert(at + section.size(), "ID" + std::to_string(9101 + i) + "=" + iniHouse(anonymous) + ",Trooper,256,"
                          + std::to_string(spots[i]) + ",64,Guard\n");
    auto game = startSkirmish(played, result);
    House* anon = game->getHouse(anonymous);
    nRequire(anon != nullptr && anon->getTeamID() == 0 && anon->getPlayerList().empty(), "The map loader made no anonymous team-0 house");
    UnitBase* anonTrooper = nullptr;
    for(UnitBase* u : unitList) if(u->getOwner() == anon && u->getItemID() == Unit_Trooper) anonTrooper = u;
    nRequire(anonTrooper != nullptr, "The anonymous house's troopers were not placed");
    House* nativesHouse = game->getOrCreateNativesHouse();
    UnitBase* nativeTrooper = placeNear(nativesHouse, Unit_Trooper, anonTrooper->getLocation() + Coord(1, 0));
    nativeTrooper->setVisible(VIS_ALL, true);
    const bool nativeShoots = nativeTrooper->canAttack(anonTrooper), anonShoots = anonTrooper->canAttack(nativeTrooper);
    SDL_Log("NATIVES_PROBE_OBS: natives_team=%d anonymous_team=%d natives_fire_on_anonymous=%d anonymous_fire_on_natives=%d",
            nativesHouse->getTeamID(), anon->getTeamID(), nativeShoots, anonShoots);
    // Run until either side is hurt (two game minutes at most).
    const auto health = [&](Uint32 id) { const ObjectBase* o = game->getObjectManager().getObject(id); return o ? o->getHealth() : FixPoint(0); };
    const Uint32 nativeId = nativeTrooper->getObjectID(), anonId = anonTrooper->getObjectID();
    const FixPoint nativeStart = health(nativeId), anonStart = health(anonId);
    const Uint32 limit = game->getGameCycleCount() + MILLI2CYCLES(120 * 1000);
    while(game->getGameCycleCount() < limit && health(nativeId) == nativeStart && health(anonId) == anonStart) game->updateGameState();
    const bool fought = health(nativeId) < nativeStart || health(anonId) < anonStart;
    SDL_Log("NATIVES_PROBE_OBS: natives_v_anonymous fought=%d by cycle %u (natives %.0f -> %.0f, anonymous %.0f -> %.0f)", fought,
            game->getGameCycleCount(), nativeStart.toDouble(), health(nativeId).toDouble(), anonStart.toDouble(), health(anonId).toDouble());

    // A worm Ordos's reinforcement brings, and Ordos and enemy troopers sharing a tile with its bite.
    House* ordos = game->getHouse(HOUSE_ORDOS);
    const Coord middle(size / 2, size / 2);
    auto* worm = dynamic_cast<Sandworm*>(placeNear(nativesHouse, Unit_Sandworm, middle));
    nRequire(worm != nullptr && worm->getOwner() == nativesHouse, "The worm is not the Natives'");
    Coord bite = Coord::Invalid();
    for(int ring = 1; ring < 20 && bite.isInvalid(); ++ring)
        for(int dx = -ring; dx <= ring && bite.isInvalid(); ++dx) {
            const Coord c = worm->getLocation() + Coord(dx, ring);
            if(currentGameMap->tileExists(c) && currentGameMap->getTile(c)->isSand() && !currentGameMap->getTile(c)->hasAnObject()) bite = c;
        }
    nRequire(bite.isValid(), "No free sand beside the worm");
    UnitBase* own = ordos->placeUnit(Unit_Trooper, bite.x, bite.y, true);
    UnitBase* foe = game->getHouse(enemy)->placeUnit(Unit_Trooper, bite.x, bite.y, true);
    nRequire(own && foe, "Could not put both troopers on one tile");
    own->setVisible(VIS_ALL, true); foe->setVisible(VIS_ALL, true);
#ifdef NATIVES_TEAM
    worm->setSummoner(HOUSE_ORDOS);
#endif
    const bool eatsOwn = worm->canAttack(own), eatsFoe = worm->canAttack(foe);
    const Uint32 ownId = own->getObjectID(), foeId = foe->getObjectID();
    currentGameMap->damage(worm->getObjectID(), nativesHouse, Coord(bite.x * TILESIZE + TILESIZE / 2, bite.y * TILESIZE + TILESIZE / 2),
                           Bullet_Sandworm, 5000, NONE_ID, false);
    const bool ownSurvives = health(ownId) > 0, foeSurvives = health(foeId) > 0;
    SDL_Log("NATIVES_PROBE_OBS: summoned worm targets_own=%d targets_enemy=%d; bite: own_survives=%d enemy_survives=%d",
            eatsOwn, eatsFoe, ownSurvives, foeSurvives);

#ifdef NATIVES_TEAM
    nRequire(nativesHouse->getTeamID() == NATIVES_TEAM, "The Natives house is not on NATIVES_TEAM");
    nRequire(nativeShoots && anonShoots, "The Natives and an anonymous team-0 house do not fight each other");
    nRequire(fought, "Neither the Natives trooper nor the anonymous trooper was hurt in two game minutes");
    nRequire(!eatsOwn && eatsFoe, "The summoned worm targets its summoner's side or spares the enemy");
    nRequire(ownSurvives && !foeSurvives, "The summoned worm's bite took its summoner's trooper or spared the enemy's");

    // The reinforcement trigger itself names the summoner: Ordos's Homebase worm comes by carryall.
    std::set<Uint32> before;
    for(const UnitBase* u : unitList) before.insert(u->getObjectID());
    ReinforcementTrigger(HOUSE_ORDOS, Unit_Sandworm, Drop_Homebase, false, game->getGameCycleCount()).trigger();
    const Sandworm* delivered = nullptr;
    for(const UnitBase* u : unitList)
        if(!before.count(u->getObjectID())) if(const auto* w = dynamic_cast<const Sandworm*>(u)) delivered = w;
    nRequire(delivered && delivered->getOwner() == nativesHouse && delivered->getSummoner() == HOUSE_ORDOS,
             "Ordos's reinforcement worm is not a Natives worm summoned by Ordos");

    // Save and reload: the summoner, the teams and the spared trooper come back.
    const Uint32 wormId = worm->getObjectID(), deliveredId = delivered->getObjectID();
    const std::string save = nOut("followup-9873.dls");
    nRequire(game->saveGame(save), "Could not save the follow-up match");
    nEnd(game);
    game = nStart(GameInitSettings(save));
    nRequire(game->getLoadedSavegameVersion() == SAVEGAMEVERSION && SAVEGAMEVERSION == SAVEGAMEVERSION_NATIVES_TEAM,
             "The follow-up save is not the current version");
    nRequire(natives(*game)->getTeamID() == NATIVES_TEAM && game->getHouse(anonymous)->getTeamID() == 0, "The reload changed a team");
    const auto* reloaded = dynamic_cast<const Sandworm*>(game->getObjectManager().getObject(wormId));
    const auto* reloadedOwn = game->getObjectManager().getObject(ownId);
    const auto* reloadedDelivered = dynamic_cast<const Sandworm*>(game->getObjectManager().getObject(deliveredId));
    nRequire(reloaded && reloaded->getSummoner() == HOUSE_ORDOS && reloadedDelivered && reloadedDelivered->getSummoner() == HOUSE_ORDOS,
             "A reloaded worm forgot its summoner");
    nRequire(reloadedOwn && !reloaded->canAttack(reloadedOwn), "The reloaded worm targets its summoner's trooper");
    nRequire(reloadedOwn->isVisible(NATIVES_TEAM), "The reloaded trooper lost its Natives visibility slot");

    // Win/lose: the enemy's last unit decides the game; the anonymous house's and the Natives' units do not count.
    nRequire(game->getHouse(anonymous)->getNumUnits() > 0 && natives(*game)->getNumUnits() > 0,
             "No anonymous or Natives unit is left to test the win with");
    const Uint32 decided = eliminate(*game, enemy);
    nRequire(game->finished && game->won && game->finishedLevelCycle == decided,
             "The player did not win on the last enemy while anonymous and Natives units are alive");
    SDL_Log("NATIVES_PROBE: follow-up: Natives team %d fight anonymous team 0; Ordos's worm spares Ordos; win at cycle %u",
            NATIVES_TEAM, decided);
#endif
    nEnd(game);
}

#endif

}

int runNativesProbe() {
    settings.general.playerName = "Natives tester";
    settings.network.metaServer = "http://127.0.0.1:1/";
    const std::string kind = std::getenv("NATIVES_PROBE_CASE") ? std::getenv("NATIVES_PROBE_CASE") : "";
#ifdef NATIVES_BASELINE
    if(kind == "make-save") makeSave();
#else
    if(kind == "maps") runMaps();
    else if(kind == "crime") runCrime();
    else if(kind == "winlose") runWinLose();
    else if(kind == "save9868") runSave9868();
    else if(kind == "followup") runFollowup();
#endif
    else nRequire(false, "Unknown case " + kind);
    SDL_Log("NATIVES_PROBE_PASS: %s", kind.c_str());
    return MENU_QUIT_DEFAULT;
}
