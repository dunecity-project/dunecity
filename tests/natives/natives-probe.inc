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
#include <units/AmbientAirplane.h>
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

/// The Natives' team: NATIVES_TEAM since save 9873, team 0 in a tree before the follow-up (1.0.852),
/// against which only the follow-up case's observations are meaningful.
#ifdef NATIVES_TEAM
constexpr int kNativesTeam = NATIVES_TEAM;
#else
constexpr int kNativesTeam = 0;
#endif

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
        nRequire(owner->getTeamID() == kNativesTeam && owner->getPlayerList().empty() && owner->getNumStructures() == 0,
                 label + ": the Natives house is off its team, has a player or a base");
        nRequire(owner->isNatives() && houseName(HOUSE_NATIVES) == "Natives", label + ": the house is not named Natives");
    }
    SDL_Log("NATIVES_PROBE: %s: %zu worms, all owned by Natives (team %d, no players)", label.c_str(), all.size(), kNativesTeam);
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
    nRequire(troops.natives->getTeamID() == kNativesTeam && troops.natives->getPlayerList().empty(), "The Natives house is off its team or has a player");
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

// ---------------------------------------------------------------------------------------------
// Release 1.0.852 feature interactions: the Natives house against the spectated game end, the
// city airplane's Death Hand crash, match analytics, campaign win/lose and lockstep peers.
// ---------------------------------------------------------------------------------------------

/// Destroys every object of @a house except @a keep, round after round (destroyed buildings can
/// release a soldier), until only the kept objects are left. The house stays alive through them.
void stripTo(Game& game, int houseID, const std::set<Uint32>& keep) {
    House* house = game.getHouse(houseID);
    nRequire(house != nullptr && house->isAlive(), "Cannot strip house " + houseName(houseID));
    for(int round = 0; round < 64; ++round) {
        std::vector<Uint32> doomed;
        for(const StructureBase* s : structureList)
            if(s->getOwner() == house && !keep.count(s->getObjectID())) doomed.push_back(s->getObjectID());
        for(const UnitBase* u : unitList)
            if((u->getOwner() == house || u->getOriginalHouseID() == houseID) && !keep.count(u->getObjectID()))
                doomed.push_back(u->getObjectID());
        if(doomed.empty()) break;
        for(Uint32 id : doomed) if(ObjectBase* object = game.getObjectManager().getObject(id)) object->destroy();
        game.updateGameState();
    }
    int left = 0;
    for(const StructureBase* s : structureList) if(s->getOwner() == house) ++left;
    for(const UnitBase* u : unitList) if(u->getOwner() == house) ++left;
    nRequire(left == static_cast<int>(keep.size()) && house->isAlive() && !game.finished,
             "House " + houseName(houseID) + " was not stripped to its kept objects");
}

/// A fresh unit or structure of @a house near @a centre; a structure releases no soldier when it
/// falls. Callers drop it to 1 HP once the rest of the house is gone (one hit then decides it).
ObjectBase* lastStand(Game& game, House* house, int itemID, Coord centre) {
    ObjectBase* object = nullptr;
    if(isStructure(itemID)) {
        for(int ring = 0; ring < 30 && !object; ++ring)
            for(int dy = -ring; dy <= ring && !object; ++dy)
                for(int dx = -ring; dx <= ring && !object; ++dx) {
                    if(std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                    const int x = centre.x + dx, y = centre.y + dy;
                    if(!currentGameMap->tileExists(x, y) || !currentGameMap->tileExists(x + 1, y + 1)) continue;
                    object = house->placeStructure(NONE_ID, itemID, x, y, true, true);
                }
        game.objectData.data[itemID][house->getHouseID()].infspawnprop = 0;
    } else {
        object = placeNear(house, itemID, centre);
    }
    nRequire(object != nullptr, "Could not place the last " + getItemNameByID(itemID) + " of " + houseName(house->getHouseID()));
    object->setVisible(VIS_ALL, true);
    return object;
}

/// A city airplane of @a owner hovering (speed 0) over @a at.
AmbientAirplane* hoveringPlane(Game& game, House* owner, Coord at) {
    game.objectData.data[Unit_AmbientAirplane][owner->getHouseID()].maxspeed = 0;
    auto* plane = dynamic_cast<AmbientAirplane*>(owner->createUnit(Unit_AmbientAirplane, true));
    nRequire(plane != nullptr, "Could not create a city airplane for " + houseName(owner->getHouseID()));
    plane->deploy(at);
    plane->currentMaxSpeed = 0;
    plane->setVisible(VIS_ALL, true);
    return plane;
}

/// Natives troopers beside @a target, all ordered to attack it.
std::vector<Uint32> nativesAttack(Game& game, const ObjectBase* target, int count, Coord near) {
    House* owner = game.getOrCreateNativesHouse();
    std::vector<Uint32> ids;
    for(int i = 0; i < count; ++i) {
        UnitBase* trooper = placeNear(owner, Unit_Trooper, near);
        trooper->setVisible(VIS_ALL, true);
        nRequire(trooper->canAttack(target), "A Natives trooper may not attack the " + getItemNameByID(target->getItemID()));
        trooper->doAttackObject(target, true);
        ids.push_back(trooper->getObjectID());
    }
    return ids;
}

/// Steps until @a house is out; returns that cycle.
Uint32 untilOut(Game& game, const House* house, Uint32 budget, const std::string& what) {
    const Uint32 limit = game.getGameCycleCount() + budget;
    while(house->isAlive() && game.getGameCycleCount() < limit) game.updateGameState();
    nRequire(!house->isAlive(), what + ": " + houseName(house->getHouseID()) + " still alive after " + std::to_string(budget) + " cycles");
    return game.getGameCycleCount();
}

/// Steps until the object is gone (or dead); returns that cycle.
Uint32 untilGone(Game& game, Uint32 id, Uint32 budget, const std::string& what) {
    const Uint32 limit = game.getGameCycleCount() + budget;
    auto alive = [&]() { const ObjectBase* o = game.getObjectManager().getObject(id); return o && o->getHealth() > 0; };
    while(alive() && game.getGameCycleCount() < limit) game.updateGameState();
    if(alive()) {
        const ObjectBase* o = game.getObjectManager().getObject(id);
        SDL_Log("NATIVES_PROBE: %s: object %u item %d at %d,%d health %.1f", what.c_str(), id, o->getItemID(),
                o->getLocation().x, o->getLocation().y, o->getHealth().toDouble());
        for(const UnitBase* u : unitList)
            if(u->getOwner()->isNatives() && u->getItemID() == Unit_Trooper)
                SDL_Log("NATIVES_PROBE:   Natives trooper %u at %d,%d health %.1f target %u mode %d range %d", u->getObjectID(),
                        u->getLocation().x, u->getLocation().y, u->getHealth().toDouble(), u->getTarget() ? u->getTarget()->getObjectID() : 0,
                        static_cast<int>(u->getAttackMode()), u->getWeaponRange());
    }
    nRequire(!alive(), what + " did not happen within " + std::to_string(budget) + " cycles");
    return game.getGameCycleCount();
}

/// The spectated game ended at @a decided naming @a winnerHouse's team (-1: nobody), never Natives.
void expectWatchedEnd(Game& game, const std::string& label, Uint32 decided, int winnerHouse) {
    nRequire(game.finished, label + ": the spectated game did not end when it was decided");
    nRequire(game.finishedLevelCycle + 1 >= decided && game.finishedLevelCycle <= decided + 1,
             label + ": ended at cycle " + std::to_string(game.finishedLevelCycle) + ", decided at " + std::to_string(decided));
    const int team = winnerHouse < 0 ? 0 : game.getHouse(winnerHouse)->getTeamID();
    nRequire(game.getWatchedWinnerTeam() == team, label + ": watched winner team " + std::to_string(game.getWatchedWinnerTeam())
             + ", expected " + std::to_string(team));
    const std::string text = game.watchedResultText();
    nRequire(text.find("Natives") == std::string::npos, label + ": the end line names the Natives: " + text);
    if(winnerHouse >= 0) nRequire(text.find(houseName(winnerHouse)) != std::string::npos, label + ": the end line '" + text + "' misses the winner");
    else nRequire(text == "No house is left standing", label + ": the end line is '" + text + "'");
    nRequire(natives(game) != nullptr && natives(game)->isAlive(), label + ": no Natives house is left to test against");
    SDL_Log("NATIVES_PROBE: %s: ended at cycle %u (decided %u): '%s' with %d Natives units alive", label.c_str(),
            game.finishedLevelCycle, decided, text.c_str(), natives(game)->getNumUnits());
}

/// The analytics end payload: one row per player, Natives nowhere, the right winner (-1: none).
void expectAnalytics(Game& game, const std::string& label, int winnerHouse) {
    const std::string payload = game.buildMatchAnalyticsPayload(true);
    nRequire(payload.find("\"house_name\":\"Natives\"") == std::string::npos, label + ": the analytics payload names the Natives");
    nRequire(payload.find("\"house_slot\":" + std::to_string(HOUSE_NATIVES) + ",") == std::string::npos,
             label + ": the analytics payload has a Natives row");
    nRequire(payload.find("\"winning_house\":" + std::to_string(winnerHouse) + ",") != std::string::npos,
             label + ": the analytics winner is not house " + std::to_string(winnerHouse));
    size_t rows = 0, players = 0;
    for(size_t at = payload.find("\"house_slot\":"); at != std::string::npos; at = payload.find("\"house_slot\":", at + 1)) ++rows;
    for(int h = 0; h < NUM_HOUSES; ++h) if(game.getHouse(h)) players += game.getHouse(h)->getPlayerList().size();
    nRequire(rows == players, label + ": " + std::to_string(rows) + " analytics rows for " + std::to_string(players) + " players");
    SDL_Log("NATIVES_PROBE: %s: analytics %zu player rows, winning_house=%d, no Natives", label.c_str(), rows, winnerHouse);
}

Coord mapCentre() { return Coord(currentGameMap->getSizeX() / 2, currentGameMap->getSizeY() / 2); }

/// Spectated DuneCity free-for-all (the view's house and three QuantBots): the deciding blow comes
/// from the Natives - a crime trooper kills the last unit, a crime trooper brings down a city
/// airplane whose Death Hand takes the last building, the same crash takes both last teams at once.
void runSpectatedInteractions() {
    const Params p = skirmishParams("dunecity", 3, true);
    const Result result = generateFor(p);
    std::vector<int> enemies;
    for(const HouseSetup& h : result.houses) if(h.role == Role::Enemy) enemies.push_back(h.house);
    nRequire(enemies.size() == 3, "The spectated free-for-all does not have three enemies");
    const auto prepare = [&](std::unique_ptr<Game>& game) {
        game = startSkirmish(p, result);
        nRequire(game->isObserving() && game->isCitySimEnabled(), "Spectate or the city did not reach the match");
        runCycles(*game, 150);
        // Natives units well away from the fight, so the house has something left at the end.
        placeNear(game->getOrCreateNativesHouse(), Unit_Sandworm, Coord(4, 4));
        eliminate(*game, HOUSE_ORDOS);
        eliminate(*game, enemies[0]);
        nRequire(!game->finished, "The spectated game ended with two houses left");
    };
    {
        // A crime trooper kills the last unit of the second-to-last house.
        std::unique_ptr<Game> game;
        prepare(game);
        House* loser = game->getHouse(enemies[1]);
        ObjectBase* last = lastStand(*game, loser, Unit_Quad, mapCentre());
        stripTo(*game, enemies[1], {last->getObjectID()});
        last->setHealth(1);
        const int nativeKills = natives(*game)->getNumDestroyedUnits();
        nativesAttack(*game, last, 3, last->getLocation());
        const Uint32 decided = untilOut(*game, loser, MILLI2CYCLES(60 * 1000), "trooper kill");
        nRequire(natives(*game)->getNumDestroyedUnits() == nativeKills + 1, "The last unit's kill is not the Natives'");
        expectWatchedEnd(*game, "spectated trooper kill", decided, enemies[2]);
        expectAnalytics(*game, "spectated trooper kill", enemies[2]);
        while(!game->finishedLevel) game->updateGameState();
        nRequire(game->whatNext() == GAME_CUSTOM_GAME_STATS, "The spectated game does not go on to its statistics");
        expectHidden(*game, "spectated trooper kill");
        nEnd(game);
    }
    {
        // A crime trooper shoots down the winner's airplane over the loser's last building.
        std::unique_ptr<Game> game;
        prepare(game);
        House* loser = game->getHouse(enemies[1]);
        ObjectBase* last = lastStand(*game, loser, Structure_WindTrap, mapCentre());
        stripTo(*game, enemies[1], {last->getObjectID()});
        runCycles(*game, 60);      // the stripped buildings' explosions die down first
        last->setHealth(20);       // the crash's Death Hand takes it; nothing else does meanwhile
        AmbientAirplane* plane = hoveringPlane(*game, game->getHouse(enemies[2]), last->getLocation() + Coord(1, 1));
        plane->setHealth(10);      // two trooper rockets bring it down
        const Uint32 planeID = plane->getObjectID();
        const int unitKills = natives(*game)->getNumDestroyedUnits(), structureKills = natives(*game)->getNumDestroyedStructures();
        nativesAttack(*game, plane, 2, plane->getLocation() + Coord(0, 4));
        untilGone(*game, planeID, MILLI2CYCLES(60 * 1000), "the Natives shooting down the plane");
        const Uint32 decided = untilOut(*game, loser, 20, "plane crash");
        nRequire(natives(*game)->getNumDestroyedStructures() == structureKills + 1 && natives(*game)->getNumDestroyedUnits() > unitKills,
                 "The crash's kills are not credited to the Natives");
        expectWatchedEnd(*game, "spectated plane crash", decided, enemies[2]);
        expectAnalytics(*game, "spectated plane crash", enemies[2]);
        nEnd(game);
    }
    {
        // The crash takes the last building of both remaining houses: nobody has won, and the
        // Natives' units on the map keep nothing running.
        std::unique_ptr<Game> game;
        prepare(game);
        ObjectBase* lastA = lastStand(*game, game->getHouse(enemies[1]), Structure_WindTrap, mapCentre());
        ObjectBase* lastB = lastStand(*game, game->getHouse(enemies[2]), Structure_WindTrap, lastA->getLocation() + Coord(2, 0));
        stripTo(*game, enemies[1], {lastA->getObjectID()});
        stripTo(*game, enemies[2], {lastB->getObjectID()});
        runCycles(*game, 60);
        lastA->setHealth(20);
        lastB->setHealth(20);
        nRequire(!game->finished, "The double-wipe game ended before the crash");
        AmbientAirplane* plane = hoveringPlane(*game, game->getHouse(enemies[2]), lastA->getLocation() + Coord(2, 1));
        plane->setHealth(10);
        const Uint32 planeID = plane->getObjectID();
        const Coord atA = lastA->getLocation(), atB = lastB->getLocation();
        nativesAttack(*game, plane, 2, plane->getLocation() + Coord(0, 4));
        untilGone(*game, planeID, MILLI2CYCLES(60 * 1000), "the Natives shooting down the plane");
        // Both last buildings fall to the one crash, in the same cycle (each is removed in its own
        // update, so one house's defeat comes before the other's within that cycle).
        const House* houseA = game->getHouse(enemies[1]);
        const House* houseB = game->getHouse(enemies[2]);
        const Uint32 limit = game->getGameCycleCount() + 20;
        while(houseA->isAlive() && houseB->isAlive() && game->getGameCycleCount() < limit) game->updateGameState();
        nRequire(!houseA->isAlive() && !houseB->isAlive(), "The crash did not take both last buildings in one cycle");
        const Uint32 decided = game->getGameCycleCount();
        SDL_Log("NATIVES_PROBE: double wipe: plane over %s's windtrap at %d,%d and %s's at %d,%d; both out by cycle %u",
                houseName(enemies[1]).c_str(), atA.x, atA.y, houseName(enemies[2]).c_str(), atB.x, atB.y, decided);
        expectWatchedEnd(*game, "spectated double wipe", decided, -1);
        expectAnalytics(*game, "spectated double wipe", -1);
        while(!game->finishedLevel) game->updateGameState();
        nRequire(game->whatNext() == GAME_CUSTOM_GAME_STATS, "The wiped-out game does not go on to its statistics");
        nEnd(game);
    }
}

/// Played DuneCity 1v1: a crime trooper killing the human's last unit loses the game, killing the
/// enemy's last unit wins it - the original rules, whoever fired.
void runPlayedInteractions() {
    const Params p = skirmishParams("dunecity", 1, false);
    const Result result = generateFor(p);
    int enemy = -1;
    for(const HouseSetup& h : result.houses) if(h.role == Role::Enemy) enemy = h.house;
    for(const bool humanFalls : {true, false}) {
        auto game = startSkirmish(p, result);
        runCycles(*game, 150);
        const int victim = humanFalls ? static_cast<int>(HOUSE_ORDOS) : enemy;
        ObjectBase* last = lastStand(*game, game->getHouse(victim), Unit_Quad, mapCentre());
        stripTo(*game, victim, {last->getObjectID()});
        last->setHealth(1);
        nativesAttack(*game, last, 3, last->getLocation());
        const Uint32 decided = untilOut(*game, game->getHouse(victim), MILLI2CYCLES(60 * 1000), "played trooper kill");
        SDL_Log("NATIVES_PROBE: played %s: decided %u finished=%d won=%d finished_at=%u", humanFalls ? "loss" : "win", decided,
                game->finished ? 1 : 0, game->won ? 1 : 0, game->finishedLevelCycle);
        nRequire(game->finished && game->won == !humanFalls && game->finishedLevelCycle + 1 >= decided && game->finishedLevelCycle <= decided,
                 std::string("The played game did not end as a ") + (humanFalls ? "loss" : "win") + " on the Natives' kill");
        expectAnalytics(*game, humanFalls ? "played loss" : "played win", humanFalls ? enemy : static_cast<int>(HOUSE_ORDOS));
        SDL_Log("NATIVES_PROBE: played %s at cycle %u on a crime trooper's kill", humanFalls ? "loss" : "win", decided);
        nEnd(game);
    }
}

/// A DuneCity campaign mission won by conquest (WinFlags has "no enemy buildings"; the spice-quota
/// missions are decided by the quota, not by any crash): Natives troopers bring down an enemy
/// airplane over its base (no crash, the Natives credited, the mission goes on and the campaign AI
/// is not provoked); a chained crash takes a second plane; then a crash decides the mission both ways.
void runCampaignInteractions() {
    int mission = 0;
    const auto start = [&](int candidate) {
        nRequire(RandomSkirmish::activateMod("dunecity"), "Mod dunecity did not activate");
        GameInitSettings init(HOUSE_ATREIDES, effectiveGameOptions, missionNumberToLevelNumber(candidate));
        init.mission = candidate;
        init.filename = GameInitSettings::getScenarioFilename(HOUSE_ATREIDES, candidate);
        for(int h = 0; h < NUM_HOUSES; ++h)
            if(isHouseAvailable(static_cast<HOUSETYPE>(h))) {
                GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h), h == HOUSE_ATREIDES ? 1 : 2);
                if(h == HOUSE_ATREIDES) info.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
                else info.addPlayerInfo({houseName(h), "qBotMedium"});
                init.addHouseInfo(info);
            }
        init.randomSeed = 20261024;
        auto game = nStart(init);
        nRequire(game->gameType == GameType::Campaign && game->isCitySimEnabled(), "Not a DuneCity campaign mission");
        runCycles(*game, 150);
        return game;
    };
    for(int candidate : {4, 5, 6, 7, 8, 9, 10}) {
        auto game = start(candidate);
        SDL_Log("NATIVES_PROBE: campaign mission %d: win_flags=%d lose_flags=%d", candidate, game->winFlags, game->loseFlags);
        const bool conquest = (game->winFlags & WINLOSEFLAGS_AI_NO_BUILDINGS) != 0;
        nEnd(game);
        if(conquest) { mission = candidate; break; }
    }
    nRequire(mission != 0, "No tried campaign mission is won by conquest");
    const auto startMission = [&]() { return start(mission); };
    std::vector<int> enemies;
    {
        auto game = startMission();
        for(int h = 0; h < NUM_HOUSES; ++h)
            if(game->getHouse(h) && h != HOUSE_ATREIDES && !game->getHouse(h)->isNatives() && game->getHouse(h)->isAlive()
               && game->getHouse(h)->getNumStructures() > 0) enemies.push_back(h);
        nRequire(!enemies.empty(), "The campaign mission has no enemy base");
        House* enemy = game->getHouse(enemies.front());
        const StructureBase* target = nullptr;
        for(const StructureBase* s : structureList) if(s->getOwner() == enemy && (!target || s->getItemID() == Structure_ConstructionYard)) target = s;
        const std::string targetName = getItemNameByID(target->getItemID());
        std::vector<QuantBot*> bots;
        for(int h : enemies) for(const auto& player : game->getHouse(h)->getPlayerList())
            if(auto* bot = dynamic_cast<QuantBot*>(player.get())) bots.push_back(bot);
        nRequire(!bots.empty(), "The campaign enemies have no QuantBot");
        for(QuantBot* bot : bots) nRequire(!bot->campaignAIAttackFlag, "The campaign AI was already provoked");
        // Both planes are the enemy's own, so its anti-air leaves them alone and only the Natives
        // shoot. The first has taken damage already (two trooper rockets bring it down), so the
        // troopers need not outlast the base defence that turns on them.
        AmbientAirplane* plane = hoveringPlane(*game, enemy, target->getLocation() + Coord(1, 1));
        AmbientAirplane* second = hoveringPlane(*game, enemy, target->getLocation() + Coord(2, 1));
        plane->setHealth(10);
        const Uint32 planeID = plane->getObjectID(), secondID = second->getObjectID();
        House* nativesHouse = game->getOrCreateNativesHouse();
        const int nativeKills = nativesHouse->getNumDestroyedUnits() + nativesHouse->getNumDestroyedStructures();
        const int planeLosses = enemy->getNumLostItems(Unit_AmbientAirplane);
        nativesAttack(*game, plane, 3, plane->getLocation() + Coord(0, 4));
        untilGone(*game, planeID, MILLI2CYCLES(60 * 1000), "the Natives shooting down the campaign plane");
        untilGone(*game, secondID, 4, "the chained crash");
        runCycles(*game, 300);
        nRequire(!game->finished, "The campaign mission ended on a crash that decided nothing");
        nRequire(nativesHouse->getNumDestroyedUnits() + nativesHouse->getNumDestroyedStructures() >= nativeKills + 2,
                 "The crash and its chain are not credited to the Natives");
        nRequire(enemy->getNumLostItems(Unit_AmbientAirplane) == planeLosses + 2, "The enemy did not lose both planes");
        for(QuantBot* bot : bots) nRequire(!bot->campaignAIAttackFlag, "A Natives crash provoked the campaign AI's offensive");
        SDL_Log("NATIVES_PROBE: campaign mission %d: Natives shot down %s's plane over its %s, chained crash, %d Natives kills, "
                "mission running, campaign AI not provoked", mission, houseName(enemy->getHouseID()).c_str(),
                targetName.c_str(), nativesHouse->getNumDestroyedUnits() + nativesHouse->getNumDestroyedStructures());
        nEnd(game);
    }
    for(const bool playerFalls : {false, true}) {
        // The crash takes the last enemy building (the player's trooper shot down the enemy's
        // plane): won. Or the Natives bring the player's plane down on the player's last building: lost.
        auto game = startMission();
        House* atreides = game->getHouse(HOUSE_ATREIDES);
        const int last = playerFalls ? static_cast<int>(HOUSE_ATREIDES) : enemies.back();
        if(!playerFalls)
            for(int h = 0; h < NUM_HOUSES; ++h)
                if(h != last && h != HOUSE_ATREIDES && game->getHouse(h) && !game->getHouse(h)->isNatives()
                   && game->getHouse(h)->getTeamID() != 0 && game->getHouse(h)->isAlive()) eliminate(*game, h);
        nRequire(!game->finished, "The mission ended before its deciding crash");
        ObjectBase* building = lastStand(*game, game->getHouse(last), Structure_WindTrap, mapCentre());
        stripTo(*game, last, {building->getObjectID()});
        building->setHealth(20);   // the crash's Death Hand takes it; nothing else does meanwhile
        AmbientAirplane* plane = hoveringPlane(*game, game->getHouse(playerFalls ? static_cast<int>(HOUSE_ATREIDES) : enemies.back()),
                                               building->getLocation() + Coord(1, 1));
        plane->setHealth(10);      // two trooper rockets bring it down
        const Uint32 planeID = plane->getObjectID();
        if(playerFalls) {
            nativesAttack(*game, plane, 2, plane->getLocation() + Coord(0, 4));
        } else {
            UnitBase* shooter = placeNear(atreides, Unit_Trooper, plane->getLocation() + Coord(0, 4));
            shooter->setVisible(VIS_ALL, true);
            nRequire(shooter->canAttack(plane), "The player's trooper may not shoot the enemy plane");
            shooter->doAttackObject(plane, true);
        }
        untilGone(*game, planeID, MILLI2CYCLES(60 * 1000), "shooting down the deciding plane");
        const Uint32 decided = untilOut(*game, game->getHouse(last), 20, "deciding crash");
        runCycles(*game, 2);
        SDL_Log("NATIVES_PROBE: campaign mission %d deciding crash: decided %u finished=%d won=%d finished_at=%u win_flags=%d lose_flags=%d",
                mission, decided, game->finished ? 1 : 0, game->won ? 1 : 0, game->finishedLevelCycle, game->winFlags, game->loseFlags);
        for(int h = 0; h < NUM_HOUSES; ++h)
            if(game->getHouse(h) && game->getHouse(h)->isAlive())
                SDL_Log("NATIVES_PROBE:   alive house %s team %d units %d structures %d", houseName(h).c_str(),
                        game->getHouse(h)->getTeamID(), game->getHouse(h)->getNumUnits(), game->getHouse(h)->getNumStructures());
        nRequire(game->finished && game->won == !playerFalls,
                 std::string("The campaign mission was not ") + (playerFalls ? "lost" : "won") + " on the deciding crash");
        SDL_Log("NATIVES_PROBE: campaign mission %d %s at cycle %u (decided %u) by a crash", mission, playerFalls ? "lost" : "won",
                game->finishedLevelCycle, decided);
        nEnd(game);
    }
}

/// Lockstep: a networked host and guest and a spectator run the same DuneCity simulation with worms,
/// crime troopers and a plane the Natives bring down; their digests agree at every 750-cycle
/// checkpoint and at the deciding cycle, and the spectator's game ends with the guest's win.
void runLockstepInteractions() {
    Params p = skirmishParams("dunecity", 3, false);
    const Result result = generateFor(p);
    GameInitSettings init(result.name, result.ini, "natives-probe", false, effectiveGameOptions);
    std::vector<int> ai;
    int guestHouse = -1;
    for(const HouseSetup& h : result.houses) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h.house), h.team);
        if(h.role == Role::Human) info.addPlayerInfo(GameInitSettings::PlayerInfo("Host", HUMANPLAYERCLASS));
        else if(guestHouse < 0) { guestHouse = h.house; info.addPlayerInfo(GameInitSettings::PlayerInfo("Guest", HUMANPLAYERCLASS)); }
        else { ai.push_back(h.house); info.addPlayerInfo(GameInitSettings::PlayerInfo(houseName(h.house), "qBotMedium")); }
        init.addHouseInfo(info);
    }
    init.randomSeed = p.seed;
    struct Peer { std::string name; bool observer; std::vector<std::string> digests; Uint32 finishedAt = 0; bool won = false; int watched = -1; };
    std::vector<Peer> peers{{"Host", false}, {"Guest", false}, {"Viewer", true}};
    for(Peer& peer : peers) {
        settings.general.playerName = peer.name;
        auto game = std::make_unique<Game>();
        currentGame = game.get();
        game->localObserver = peer.observer;
        game->bReplay = true;
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        nRequire(game->isObserving() == peer.observer && game->isCitySimEnabled(), peer.name + " did not start as intended");
        const auto step = [&](Uint32 cycles) {
            for(Uint32 i = 0; i < cycles; ++i) {
                if(game->getGameCycleCount() % 750 == 0)
                    peer.digests.push_back(std::to_string(game->getGameCycleCount()) + ":" + GameStateDigest::describe(game->computeStateDigest()));
                game->updateGameState();
            }
        };
        step(150);
        // The same injections at the same cycle on every peer: Natives worms, crime troopers at the
        // first AI's base, the host's plane over the second AI's base brought down by a Natives trooper.
        House* nativesHouse = game->getOrCreateNativesHouse();
        placeNear(nativesHouse, Unit_Sandworm, mapCentre());
        placeNear(nativesHouse, Unit_Sandworm, mapCentre() + Coord(6, 6));
        const auto baseOf = [&](int h) {
            const StructureBase* found = nullptr;
            for(const StructureBase* s : structureList) if(s->getOwner()->getHouseID() == h && (!found || s->getItemID() == Structure_ConstructionYard)) found = s;
            nRequire(found != nullptr, houseName(h) + " has no building");
            return found;
        };
        const auto troops = game->getCitySimulation()->raiseCrimeTroopers(baseOf(ai[0]), 3);
        nRequire(!troops.units.empty() && troops.natives == nativesHouse, peer.name + ": crime raised no Natives troopers");
        AmbientAirplane* plane = hoveringPlane(*game, game->getHouse(HOUSE_ORDOS), baseOf(ai[1])->getLocation() + Coord(1, 1));
        const Uint32 planeID = plane->getObjectID();
        nativesAttack(*game, plane, 2, baseOf(ai[1])->getLocation() + Coord(4, 2));
        for(int i = 0; i < 6000 && game->getObjectManager().getObject(planeID); ++i) step(1);
        nRequire(game->getObjectManager().getObject(planeID) == nullptr, peer.name + ": the plane was not shot down");
        step(MILLI2CYCLES(120 * 1000));
        eliminate(*game, ai[0]);
        step(60);
        eliminate(*game, HOUSE_ORDOS);
        step(60);
        const Uint32 decided = eliminate(*game, ai[1]);
        peer.digests.push_back("decided " + std::to_string(decided) + ":" + GameStateDigest::describe(game->computeStateDigest()));
        peer.finishedAt = game->finished ? game->finishedLevelCycle : 0;
        peer.won = game->won;
        peer.watched = game->getWatchedWinnerTeam();
        SDL_Log("NATIVES_PROBE: lockstep peer=%s checkpoints=%zu decided=%u finished_at=%u won=%d watched_winner=%d natives_units=%d last=%s",
                peer.name.c_str(), peer.digests.size(), decided, peer.finishedAt, peer.won ? 1 : 0, peer.watched,
                nativesHouse->getNumUnits(), peer.digests.back().c_str());
        if(peer.observer) expectWatchedEnd(*game, "lockstep spectator", decided, guestHouse);
        nEnd(game);
    }
    settings.general.playerName = "Natives tester";
    for(size_t i = 0; i < peers[0].digests.size() || i < peers[2].digests.size(); ++i) {
        const auto at = [&](const Peer& peer) { return i < peer.digests.size() ? peer.digests[i] : std::string("missing"); };
        nRequire(at(peers[0]) == at(peers[1]) && at(peers[1]) == at(peers[2]),
                 "Peers diverge at checkpoint " + std::to_string(i) + ": " + at(peers[0]) + " / " + at(peers[1]) + " / " + at(peers[2]));
    }
    nRequire(peers[1].won && peers[1].finishedAt == peers[2].finishedAt, "The guest's win and the spectator's end are not the same cycle");
    SDL_Log("NATIVES_PROBE: lockstep: %zu identical checkpoints on host, guest and spectator; guest won and spectator ended at %u",
            peers[0].digests.size(), peers[2].finishedAt);
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
    // Two free sand tiles beside the worm (infantry of different houses never share a tile).
    std::vector<Coord> sand;
    for(int ring = 1; ring < 20 && sand.size() < 2; ++ring)
        for(int dx = -ring; dx <= ring && sand.size() < 2; ++dx) {
            const Coord c = worm->getLocation() + Coord(dx, ring);
            if(currentGameMap->tileExists(c) && currentGameMap->getTile(c)->isSand() && !currentGameMap->getTile(c)->hasAnObject()) sand.push_back(c);
        }
    nRequire(sand.size() == 2, "No free sand beside the worm");
    UnitBase* own = ordos->placeUnit(Unit_Trooper, sand[0].x, sand[0].y, true);
    UnitBase* foe = game->getHouse(enemy)->placeUnit(Unit_Trooper, sand[1].x, sand[1].y, true);
    nRequire(own && foe, "Could not place the two troopers");
    own->setVisible(VIS_ALL, true); foe->setVisible(VIS_ALL, true);
#ifdef NATIVES_TEAM
    worm->setSummoner(HOUSE_ORDOS);
#endif
    const bool eatsOwn = worm->canAttack(own), eatsFoe = worm->canAttack(foe);
    const Uint32 ownId = own->getObjectID(), foeId = foe->getObjectID();
    // The bite a worm deals with its mouth over each tile (Sandworm::update -> Map::damage).
    for(const Coord& bite : sand)
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
    else if(kind == "spectated-interactions") runSpectatedInteractions();
    else if(kind == "played-interactions") runPlayedInteractions();
    else if(kind == "campaign-interactions") runCampaignInteractions();
    else if(kind == "lockstep-interactions") runLockstepInteractions();
    else if(kind == "followup") runFollowup();
    else if(kind == "loadsave") {
        // Diagnostic, not a ctest case: load NATIVES_LOAD_SAVE in whatever profile DUNECITY_USERDIR
        // names (run the --keep binary by hand), e.g. to see a pruned ws-<hash> copy reinstalled.
        const char* path = std::getenv("NATIVES_LOAD_SAVE");
        nRequire(path != nullptr, "NATIVES_LOAD_SAVE is not set");
        auto game = nStart(GameInitSettings(std::string(path)));
        SDL_Log("NATIVES_PROBE: loaded %s (save %u) as mod %s, %zu units", path, game->getLoadedSavegameVersion(),
                ModManager::instance().getActiveModName().c_str(), unitList.size());
        runCycles(*game, 100);
        nEnd(game);
    }
#endif
    else nRequire(false, "Unknown case " + kind);
    SDL_Log("NATIVES_PROBE_PASS: %s", kind.c_str());
    return MENU_QUIT_DEFAULT;
}
