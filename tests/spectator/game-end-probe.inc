// Included only by run-game-end-probe.py, never by the shipped game. Plays real matches in the
// real engine and checks how they end for a view that has no side left in them: the offline
// Spectate of Random Skirmish and Custom Game, an observer that watches the same simulation as
// two networked players, and a human whose team is out on a map that lets it keep watching.
// Houses are eliminated through the engine's own objects (destroy() until House::isAlive() is
// false, which is the engine's defeat rule), so House::lose() and every win/lose check run as
// they do in play. GAME_END_BASELINE builds the same cases against a tree without the fix and
// only reports what happens (no new API, no assertions on the new behaviour).
#include <Menu/RandomSkirmishMenu.h>
#include <Menu/CustomGameStatsMenu.h>
#include <MapEditor/SkirmishGenerator.h>
#include <players/PlayerFactory.h>
#include <players/HumanPlayer.h>
#include <FileClasses/LoadSavePNG.h>
#include <Game.h>
#include <House.h>
#include <ScreenBorder.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <mod/ModManager.h>
#include <misc/FileSystem.h>
#include <misc/draw_util.h>
#include <misc/string_util.h>
#include <sand.h>

#include <cstdlib>
#include <functional>
#include <set>

namespace {

using namespace SkirmishGenerator;

void endRequire(bool ok, const std::string& what) {
    if(!ok) { SDL_Log("GAME_END_PROBE_FAIL: %s", what.c_str()); throw std::runtime_error(what); }
}

std::string endOut(const std::string& name) { return std::string(std::getenv("GAME_END_PROBE_OUT")) + "/" + name; }

std::unique_ptr<Game> startEndMatch(const GameInitSettings& init, bool networkObserver = false) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    // A network spectator's Game: the players' simulation, watched through the detached view
    // identity. isObserving() is the predicate the end of the game reads; a real spectator
    // gets it from its network manager, this one from the same flag Spectate sets.
    game->localObserver = networkObserver;
    game->bReplay = true;   // no briefing / analytics at setup
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    return game;
}

void endEndMatch(std::unique_ptr<Game>& game) {
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
}

std::string houseName(int h) { return getHouseNameByNumber(static_cast<HOUSETYPE>(h)); }

void endCapture(Game& game, const std::string& name) {
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    endRequire(image != nullptr, "Could not read back the game view");
    endRequire(SavePNG(image.get(), endOut(name + ".png").c_str()) == 0, "Could not write " + name);
    SDL_Log("GAME_END_PROBE: screenshot %s.png", name.c_str());
}

void endCaptureWindow(Window& window, const std::string& name) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    window.draw();
    auto image = renderReadSurface(renderer);
    endRequire(image != nullptr, "Could not read back the window");
    endRequire(SavePNG(image.get(), endOut(name + ".png").c_str()) == 0, "Could not write " + name);
    SDL_Log("GAME_END_PROBE: screenshot %s.png", name.c_str());
}

/// Every unit and structure of the house, until the engine counts it as defeated. Destroyed
/// buildings can leave a surviving soldier behind, so it takes a few rounds of real cycles.
/// Returns the cycle at which the house was out.
Uint32 eliminate(Game& game, int houseID) {
    House* house = game.getHouse(houseID);
    endRequire(house != nullptr && house->isAlive(), "Cannot eliminate house " + houseName(houseID));
    for(int round = 0; round < 64 && house->isAlive(); ++round) {
        std::vector<Uint32> doomed;
        for(const StructureBase* s : structureList) if(s->getOwner() == house) doomed.push_back(s->getObjectID());
        for(const UnitBase* u : unitList)
            if(u->getOwner() == house || u->originalHouseID == houseID) doomed.push_back(u->getObjectID());
        for(Uint32 id : doomed) if(ObjectBase* object = game.getObjectManager().getObject(id)) object->destroy();
        if(house->isAlive()) game.updateGameState();
    }
    endRequire(!house->isAlive(), "House " + houseName(houseID) + " survived its elimination");
    SDL_Log("GAME_END_PROBE: cycle=%u eliminated %s (team %d) finished=%d", game.getGameCycleCount(),
            houseName(houseID).c_str(), house->getTeamID(), game.finished ? 1 : 0);
    return game.getGameCycleCount();
}

void runCycles(Game& game, Uint32 cycles) {
    const Uint32 end = game.getGameCycleCount() + cycles;
    while(game.getGameCycleCount() < end && !game.finishedLevel) game.updateGameState();
}

/// The text a watched game must show for this winning team, built independently of the engine.
std::string expectedWinner(Game& game, int team) {
    std::vector<int> members;
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(game.getHouse(h) && game.getHouse(h)->getTeamID() == team && !game.getHouse(h)->getPlayerList().empty()) members.push_back(h);
    if(members.size() == 1) return "House " + houseName(members.front()) + " has won";
    std::string names;
    for(size_t i = 0; i < members.size(); ++i)
        names += (i == 0 ? "" : i + 1 == members.size() ? " and " : ", ") + houseName(members[i]);
    return "Team " + std::to_string(team) + " has won: " + names;
}

/**
    The match must already be decided. Checks that the local view's game ended at once with the
    right winner, the in-game line and the statistics screen name it in its colour, the end
    message runs its usual six seconds and the game then leaves through the statistics to the menu.
    In a baseline build: reports whether the game ended at all within a minute.
*/
void expectWatchedEnd(Game& game, const std::string& label, Uint32 decidedCycle, int winnerTeam, bool timeUp = false) {
#ifdef GAME_END_BASELINE
    (void) winnerTeam; (void) timeUp;
    const Uint32 limit = decidedCycle + MILLI2CYCLES(60 * 1000);
    while(!game.finished && game.getGameCycleCount() < limit) game.updateGameState();
    SDL_Log("GAME_END_BASELINE: %s decided at cycle %u; finished=%d won=%d at cycle %u (%u cycles later)", label.c_str(),
            decidedCycle, game.finished ? 1 : 0, game.won ? 1 : 0, game.getGameCycleCount(), game.getGameCycleCount() - decidedCycle);
#else
    endRequire(game.finished, label + ": the game did not end when it was decided");
    endRequire(game.finishedLevelCycle >= decidedCycle && game.finishedLevelCycle - decidedCycle <= 2,
               label + ": the game ended " + std::to_string(game.finishedLevelCycle - decidedCycle) + " cycles after it was decided");
    endRequire(!game.won, label + ": a view without a side was told it won");
    endRequire(game.getWatchedWinnerTeam() == winnerTeam, label + ": winner team " + std::to_string(game.getWatchedWinnerTeam())
               + ", expected " + std::to_string(winnerTeam));
    const std::string expected = winnerTeam > 0 ? expectedWinner(game, winnerTeam)
                                                : std::string(timeUp ? "Time is up - no winner" : "No house is left standing");
    endRequire(game.watchedResultText() == expected, label + ": end line '" + game.watchedResultText() + "', expected '" + expected + "'");
    Uint32 color = COLOR_WHITE;
    if(winnerTeam > 0)
        for(int h = NUM_HOUSES - 1; h >= 0; --h)
            if(game.getHouse(h) && game.getHouse(h)->getTeamID() == winnerTeam && !game.getHouse(h)->getPlayerList().empty())
                color = getHouseColorRGB(getHouseVisualHouse(h), 3);
    endRequire(game.watchedResultColor() == color, label + ": the end line is not in the winner's colour");
    SDL_Log("GAME_END_PROBE: %s ended at cycle %u (decided %u): '%s' colour=%08x", label.c_str(), game.finishedLevelCycle,
            decidedCycle, expected.c_str(), color);
    endCapture(game, "ended-" + label);
    const Uint32 wait = MILLI2CYCLES(END_WAIT_TIME);
    while(!game.finishedLevel && game.getGameCycleCount() <= game.finishedLevelCycle + wait + 2) game.updateGameState();
    endRequire(game.finishedLevel, label + ": the end message did not lead out of the game");
    endRequire(game.getGameCycleCount() - game.finishedLevelCycle <= wait + 2, label + ": the end message ran too long");
    endRequire(game.whatNext() == GAME_CUSTOM_GAME_STATS, label + ": the game does not go on to its statistics");
    {
        CustomGameStatsMenu stats;
        endRequire(stats.resultLabel.getText() == expected, label + ": the statistics say '" + stats.resultLabel.getText() + "'");
        endRequire(stats.resultLabel.textcolor == color, label + ": the statistics' winner line is not in the winner's colour");
        endCaptureWindow(stats, "stats-" + label);
    }
    endRequire(game.whatNext() == GAME_RETURN_TO_MENU, label + ": the statistics do not return to the menu");
    SDL_Log("GAME_END_PROBE: %s left at cycle %u through the statistics to the menu", label.c_str(), game.getGameCycleCount());
#endif
}

void expectRunning(Game& game, const std::string& what) {
    SDL_Log("GAME_END_PROBE: cycle=%u %s: finished=%d", game.getGameCycleCount(), what.c_str(), game.finished ? 1 : 0);
    endRequire(!game.finished, "The game ended although it is not decided: " + what);
}

Params skirmishParams(const std::string& mod, int enemies, bool teams, bool spectate) {
    Params p;
    p.house = HOUSE_ORDOS;
    p.seed = 20261022;
    p.techLevel = 8;
    p.mod = mod;
    p.mode = RandomSkirmish::modeForMod(mod);
    p.mapSize = p.mode == Mode::DuneCity ? 96 : 64;
    p.allies = teams ? 1 : 0;
    p.enemies = enemies;
    p.enemiesAllied = teams;
    const int city = p.mode == Mode::DuneCity ? 1 : 0;
    p.allySide = Side{"qBotEasy", BaseSize::Small, 1, city};
    p.enemySide = Side{"qBotEasy", BaseSize::Small, 1, city};
    p.playerSide = Side{"", BaseSize::Small, 1, city};
    p.spectate = spectate;
    p.spectateClass = "qBotMedium";
    return p;
}

Result generateFor(const Params& p) {
    endRequire(RandomSkirmish::activateMod(p.mod), "Mod " + p.mod + " did not activate");
    Result result = SkirmishGenerator::generate(p, RandomSkirmish::loadCatalogue(p.mode));
    for(const HouseSetup& h : result.houses) endRequire(h.skipped == 0, "The generator could not place everything");
    return result;
}

std::unique_ptr<Game> startSkirmish(const Params& p, const Result& result) {
    GameInitSettings init = RandomSkirmish::makeInit(result, effectiveGameOptions, p);
    init.randomSeed = p.seed;
    auto game = startEndMatch(init);
    endRequire(game->isObserving() == p.spectate, "Spectate did not reach the match");
    std::string roster;
    for(const HouseSetup& h : result.houses) {
        const House* house = game->getHouse(h.house);
        endRequire(house != nullptr, "A generated house is missing");
        roster += " " + houseName(h.house) + "/team" + std::to_string(house->getTeamID()) + "/" + house->getPlayerList().front()->getPlayerclass();
    }
    SDL_Log("GAME_END_PROBE: %s %s, view anchored on %s:%s", p.mod.c_str(), p.spectate ? "spectated" : "played",
            pLocalHouse ? houseName(pLocalHouse->getHouseID()).c_str() : "none", roster.c_str());
    return game;
}

/**
    Spectated Random Skirmish free-for-all: the watched house goes out first (the match is not
    decided: the game goes on), then all but one enemy - which is not the view anchor - so the
    winner is somebody the view has no tie to.
*/
void runSpectatedFfa(const std::string& mod) {
    const Params p = skirmishParams(mod, 3, false, true);
    const Result result = generateFor(p);
    auto game = startSkirmish(p, result);
    runCycles(*game, 150);
    std::vector<int> enemies;
    for(const HouseSetup& h : result.houses) if(h.role == Role::Enemy) enemies.push_back(h.house);
    int winner = -1;
    for(int h : enemies) if(winner < 0 && h != pLocalHouse->getHouseID()) winner = h;
    endRequire(winner >= 0, "No enemy to win");
    eliminate(*game, HOUSE_ORDOS);
    expectRunning(*game, "the watched house is out, three houses remain");
    runCycles(*game, 60);
    Uint32 decided = 0;
    std::vector<int> rest;
    for(int h : enemies) if(h != winner) rest.push_back(h);
    for(size_t i = 0; i < rest.size(); ++i) {
        decided = eliminate(*game, rest[i]);
        if(i + 1 < rest.size()) { expectRunning(*game, "two houses remain"); runCycles(*game, 60); }
    }
    expectWatchedEnd(*game, mod + "-ffa", decided, game->getHouse(winner)->getTeamID());
    endEndMatch(game);
}

/// Spectated 2v2: the watched house goes out but its ally fights on; one enemy goes out; the
/// ally goes out last, so the enemy team - one house of it already dead - has won.
void runSpectatedTeams(const std::string& mod) {
    const Params p = skirmishParams(mod, 2, true, true);
    const Result result = generateFor(p);
    auto game = startSkirmish(p, result);
    runCycles(*game, 150);
    int ally = -1;
    std::vector<int> enemies;
    for(const HouseSetup& h : result.houses) {
        if(h.role == Role::Ally) ally = h.house;
        if(h.role == Role::Enemy) enemies.push_back(h.house);
    }
    endRequire(ally >= 0 && enemies.size() == 2, "The 2v2 roster is not one ally and two enemies");
    const int enemyTeam = game->getHouse(enemies[0])->getTeamID();
    endRequire(game->getHouse(enemies[1])->getTeamID() == enemyTeam && game->getHouse(ally)->getTeamID() == game->getHouse(HOUSE_ORDOS)->getTeamID()
               && enemyTeam != game->getHouse(ally)->getTeamID(), "The 2v2 teams are not two against two");
    eliminate(*game, HOUSE_ORDOS);
    expectRunning(*game, "the watched house is out, its ally fights on");
    runCycles(*game, 60);
    eliminate(*game, enemies[0]);
    expectRunning(*game, "one house of each team is out");
    runCycles(*game, 60);
    const Uint32 decided = eliminate(*game, ally);
    expectWatchedEnd(*game, mod + "-teams", decided, enemyTeam);
    endEndMatch(game);
}

/// Control: a played Random Skirmish keeps the original rules. Winning: the game ends when the
/// last enemy is out, as a win. Losing: it ends as soon as the player's own house is out.
void runPlayedControl() {
    const Params p = skirmishParams("vanilla", 3, false, false);
    const Result result = generateFor(p);
    std::vector<int> enemies;
    for(const HouseSetup& h : result.houses) if(h.role == Role::Enemy) enemies.push_back(h.house);
    {
        auto game = startSkirmish(p, result);
        endRequire(!game->isObserving() && pLocalHouse == game->getHouse(HOUSE_ORDOS), "The player does not play Ordos");
        runCycles(*game, 150);
        Uint32 decided = 0;
        for(size_t i = 0; i < enemies.size(); ++i) {
            decided = eliminate(*game, enemies[i]);
            if(i + 1 < enemies.size()) { expectRunning(*game, "the player fights on"); runCycles(*game, 60); }
        }
        endRequire(game->finished && game->won && game->finishedLevelCycle == decided, "The player did not win when the last enemy went out");
#ifndef GAME_END_BASELINE
        endRequire(game->getWatchedWinnerTeam() == -1, "A played win was turned into a watched end");
#endif
        SDL_Log("GAME_END_PROBE: played win at cycle %u, digest %s", decided, GameStateDigest::describe(game->computeStateDigest()).c_str());
        endCapture(*game, "ended-played-win");
        while(!game->finishedLevel) game->updateGameState();
        endRequire(game->getGameCycleCount() - decided == MILLI2CYCLES(END_WAIT_TIME) + 1, "The played end message did not run its usual time");
        endRequire(game->whatNext() == GAME_CUSTOM_GAME_STATS && game->whatNext() == GAME_RETURN_TO_MENU, "A played win does not leave as before");
        endEndMatch(game);
    }
    {
        auto game = startSkirmish(p, result);
        runCycles(*game, 150);
        eliminate(*game, enemies[0]);
        expectRunning(*game, "one enemy is out");
        runCycles(*game, 60);
        const Uint32 out = eliminate(*game, HOUSE_ORDOS);
        endRequire(game->finished && !game->won && game->finishedLevelCycle == out, "The player did not lose when its house went out");
#ifndef GAME_END_BASELINE
        endRequire(game->getWatchedWinnerTeam() == -1, "A played defeat was turned into a watched end");
#endif
        SDL_Log("GAME_END_PROBE: played defeat at cycle %u, digest %s", out, GameStateDigest::describe(game->computeStateDigest()).c_str());
        endEndMatch(game);
    }
}

GameInitSettings bundledMap(const std::string& mod, const std::string& map, const std::vector<std::pair<int, std::string>>& houses,
                            bool spectate) {
    endRequire(RandomSkirmish::activateMod(mod), "Mod " + mod + " did not activate");
    const std::string path = std::string(std::getenv("GAME_END_SOURCE_DIR")) + "/data/maps/multiplayer/" + map + ".ini";
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

/// A player whose house is out on a map without "the human has buildings" in its win rules
/// (Equilibrium: WinFlags=1) keeps watching - and must be shown the end when it is decided.
void runEliminatedWatcher() {
    auto game = startEndMatch(bundledMap("vanilla", "4P - 128x128 - Equilibrium",
        {{HOUSE_ATREIDES, HUMANPLAYERCLASS}, {HOUSE_HARKONNEN, "qBotEasy"}, {HOUSE_ORDOS, "qBotEasy"}, {HOUSE_SARDAUKAR, "qBotEasy"}}, false));
    endRequire(game->winFlags == 1 && pLocalHouse == game->getHouse(HOUSE_ATREIDES) && !game->isObserving(), "Equilibrium is not a played WinFlags=1 match");
    runCycles(*game, 150);
    eliminate(*game, HOUSE_ATREIDES);
    expectRunning(*game, "the player's house is out and the map lets it watch");
    runCycles(*game, 60);
    eliminate(*game, HOUSE_HARKONNEN);
    expectRunning(*game, "two AI houses remain");
    runCycles(*game, 60);
    const Uint32 decided = eliminate(*game, HOUSE_ORDOS);
    expectWatchedEnd(*game, "eliminated-watcher", decided, game->getHouse(HOUSE_SARDAUKAR)->getTeamID());
    endEndMatch(game);
}

/// Silicon Valley wins on a 10,000 quota (WinFlags=7, LoseFlags=5). Spectated, any house that
/// banks its quota wins; played, only the player's own house's quota ends the game (as before).
void runQuota() {
    const std::vector<std::pair<int, std::string>> ai{{HOUSE_HARKONNEN, "qBotEasy"}, {HOUSE_ATREIDES, "qBotEasy"},
                                                       {HOUSE_ORDOS, "qBotEasy"}, {HOUSE_FREMEN, "qBotEasy"}};
    const auto bank = [](Game& game, int h) {
        House* house = game.getHouse(h);
        endRequire(house->getQuota() > 0, "The map gave " + houseName(h) + " no quota");
        // Room to store it (silos), then the last credit of it as refinery income.
        house->capacity = 2 * house->getQuota();
        house->storedCredits = FixPoint(house->getQuota() - 1);
        house->addCredits(FixPoint(1), true);   // the engine's own quota check runs on income
        SDL_Log("GAME_END_PROBE: cycle=%u %s banked its quota of %d: finished=%d", game.getGameCycleCount(), houseName(h).c_str(),
                house->getQuota(), game.finished ? 1 : 0);
        return game.getGameCycleCount();
    };
    {
        auto game = startEndMatch(bundledMap("vanilla", "4P - 64x64 - Silicon Valley", ai, true));
        endRequire(game->isObserving() && game->winFlags == 7, "Silicon Valley is not a spectated quota match");
        runCycles(*game, 150);
        const int winner = pLocalHouse->getHouseID() == HOUSE_ORDOS ? HOUSE_FREMEN : HOUSE_ORDOS;
        const Uint32 decided = bank(*game, winner);
        expectWatchedEnd(*game, "spectated-quota", decided, game->getHouse(winner)->getTeamID());
        endEndMatch(game);
    }
    {
        auto played = ai;
        played[0].second = HUMANPLAYERCLASS;
        auto game = startEndMatch(bundledMap("vanilla", "4P - 64x64 - Silicon Valley", played, false));
        endRequire(pLocalHouse == game->getHouse(HOUSE_HARKONNEN), "The player does not play Harkonnen");
        runCycles(*game, 150);
        bank(*game, HOUSE_ORDOS);
        expectRunning(*game, "another house banked its quota (the player's game only ends on its own)");
        const Uint32 own = bank(*game, HOUSE_HARKONNEN);
        endRequire(game->finished && game->won && game->finishedLevelCycle == own, "The player's own quota did not win");
#ifndef GAME_END_BASELINE
        endRequire(game->getWatchedWinnerTeam() == -1, "A played quota win was turned into a watched end");
#endif
        endEndMatch(game);
    }
}

/// A one-minute time limit (WinFlags 1|2|8) on a spectated free-for-all: the time is up with
/// four teams alive, so the game ends without a winner.
void runTimeout() {
    Params p = skirmishParams("vanilla", 3, false, true);
    Result result = generateFor(p);
    const std::string flags = "WinFlags=3\n";
    const auto at = result.ini.find(flags);
    endRequire(at != std::string::npos, "The generated map has no WinFlags=3 line");
    result.ini.replace(at, flags.size(), "WinFlags=11\nTimeOut=1\n");
    auto game = startSkirmish(p, result);
    const Uint32 limit = MILLI2CYCLES(60 * 1000);
    while(!game->finished && game->getGameCycleCount() < limit + 10) game->updateGameState();
    SDL_Log("GAME_END_PROBE: time limit at cycle %u: finished=%d at cycle %u", limit, game->finished ? 1 : 0, game->getGameCycleCount());
    expectWatchedEnd(*game, "timeout", limit, 0, true);
    endEndMatch(game);
}

/// Not in the default set (minutes of play): a spectated vanilla 1v1, Brutal against an Easy
/// house with a small base, played by the AIs themselves until one house is left (40 minutes at most).
void runNatural() {
    Params p = skirmishParams("vanilla", 1, false, true);
    p.spectateClass = "qBotBrutal";
    p.playerSide = Side{"", BaseSize::Medium, 3, 0};
    p.enemySide = Side{"qBotEasy", BaseSize::Small, 0, 0};
    const Result result = generateFor(p);
    auto game = startSkirmish(p, result);
    const Uint32 limit = MILLI2CYCLES(40 * 60 * 1000);
    Uint32 decided = 0;
    while(game->getGameCycleCount() < limit && decided == 0) {
        // The cycle being simulated is gameCycleCount until updateGameState() advances it.
        const Uint32 cycle = game->getGameCycleCount();
        game->updateGameState();
        std::set<int> teams;
        for(int h = 0; h < NUM_HOUSES; ++h)
            if(game->getHouse(h) && game->getHouse(h)->getTeamID() != 0 && game->getHouse(h)->isAlive()) teams.insert(game->getHouse(h)->getTeamID());
        if(teams.size() <= 1) decided = cycle;
    }
    endRequire(decided != 0, "Nobody won the natural 1v1 within 40 minutes");
    SDL_Log("GAME_END_PROBE: natural 1v1 decided at cycle %u (%.1f game minutes)", decided, decided / double(MILLI2CYCLES(60000)));
    int winnerTeam = 0;
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(game->getHouse(h) && game->getHouse(h)->getTeamID() != 0 && game->getHouse(h)->isAlive()) winnerTeam = game->getHouse(h)->getTeamID();
    expectWatchedEnd(*game, "natural-1v1", decided, winnerTeam);
    endEndMatch(game);
}

/**
    Lockstep: two networked players and a spectator run the same simulation (the same init and
    the same eliminations at the same cycles). The host's house goes out first: the host's game
    ends as a loss (original rules) and the spectator keeps watching. When the guest's last
    enemy goes out, the guest wins and the spectator's game ends at the very same cycle, and
    all three agree on the game state there: the end of a watched game changes nothing simulated.
*/
void runMultiplayer() {
    Params p = skirmishParams("vanilla", 3, false, false);
    const Result result = generateFor(p);
    GameInitSettings init(result.name, result.ini, "game-end-probe", false, effectiveGameOptions);
    std::vector<int> ai;
    int guestHouse = -1;
    for(const HouseSetup& h : result.houses) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h.house), h.team);
        if(h.role == Role::Human) info.addPlayerInfo(GameInitSettings::PlayerInfo("Host", HUMANPLAYERCLASS));
        else if(guestHouse < 0) { guestHouse = h.house; info.addPlayerInfo(GameInitSettings::PlayerInfo("Guest", HUMANPLAYERCLASS)); }
        else { ai.push_back(h.house); info.addPlayerInfo(GameInitSettings::PlayerInfo(houseName(h.house), "qBotEasy")); }
        init.addHouseInfo(info);
    }
    init.randomSeed = p.seed;
    struct Peer { std::string name; bool observer; Uint32 hostOut = 0, decided = 0, finishedAt = 0; bool hostFinished = false, won = false;
                  int winner = -1; std::string digest; };
    std::vector<Peer> peers{{"Host", false}, {"Guest", false}, {"Viewer", true}};
    for(Peer& peer : peers) {
        settings.general.playerName = peer.name;
        auto game = startEndMatch(init, peer.observer);
        endRequire(game->isObserving() == peer.observer && pLocalHouse != nullptr, peer.name + " did not start as intended");
        endRequire(peer.observer || pLocalHouse->getHouseID() == (peer.name == "Host" ? HOUSE_ORDOS : guestHouse), peer.name + " plays the wrong house");
        runCycles(*game, 150);
        eliminate(*game, ai[0]);
        endRequire(!game->finished, peer.name + ": ended after the first AI went out");
        runCycles(*game, 60);
        peer.hostOut = eliminate(*game, HOUSE_ORDOS);
        peer.hostFinished = game->finished;
        if(peer.name == "Host") endRequire(game->finished && !game->won, "The host did not lose when its house went out");
        else endRequire(!game->finished, peer.name + ": ended when the host's house went out, with three houses left");
        // The host leaves after its end message in a real match; here it keeps stepping so all
        // three can be compared at the deciding cycle.
        for(int i = 0; i < 60; ++i) game->updateGameState();
        peer.decided = eliminate(*game, ai[1]);
        peer.won = game->won;
        peer.finishedAt = game->finished ? game->finishedLevelCycle : 0;
#ifndef GAME_END_BASELINE
        peer.winner = game->getWatchedWinnerTeam();
#endif
        peer.digest = GameStateDigest::describe(game->computeStateDigest());
        SDL_Log("GAME_END_PROBE: peer=%s host_out=%u decided=%u finished_at=%u won=%d watched_winner=%d digest=%s", peer.name.c_str(),
                peer.hostOut, peer.decided, peer.finishedAt, peer.won ? 1 : 0, peer.winner, peer.digest.c_str());
        if(peer.observer) {
#ifdef GAME_END_BASELINE
            expectWatchedEnd(*game, "multiplayer-observer", peer.decided, 0);
#else
            expectWatchedEnd(*game, "multiplayer-observer", peer.decided, game->getHouse(guestHouse)->getTeamID());
#endif
        }
        endEndMatch(game);
    }
    settings.general.playerName = "Game end tester";
    const Peer &host = peers[0], &guest = peers[1], &viewer = peers[2];
    endRequire(host.digest == guest.digest && guest.digest == viewer.digest, "The peers disagree about the game state");
    endRequire(host.decided == guest.decided && guest.decided == viewer.decided, "The peers decided the match at different cycles");
    endRequire(guest.won && guest.finishedAt == guest.decided, "The guest did not win when its last enemy went out");
    endRequire(host.finishedAt == host.hostOut, "The host's game did not end when its house went out");
#ifndef GAME_END_BASELINE
    endRequire(viewer.finishedAt == guest.finishedAt, "The spectator's game did not end at the guest's winning cycle");
#endif
    SDL_Log("GAME_END_PROBE: lockstep host lost at %u, guest won at %u, spectator ended at %u, one digest", host.finishedAt,
            guest.finishedAt, viewer.finishedAt);
}

}

int runGameEndProbe() {
    settings.general.playerName = "Game end tester";
    settings.network.metaServer = "http://127.0.0.1:1/";
    const std::string kind = std::getenv("GAME_END_PROBE_CASE") ? std::getenv("GAME_END_PROBE_CASE") : "";
    if(kind == "vanilla-ffa") runSpectatedFfa("vanilla");
    else if(kind == "dunecity-ffa") runSpectatedFfa("dunecity");
    else if(kind == "vanilla-teams") runSpectatedTeams("vanilla");
    else if(kind == "dunecity-teams") runSpectatedTeams("dunecity");
    else if(kind == "played") runPlayedControl();
    else if(kind == "eliminated-watcher") runEliminatedWatcher();
    else if(kind == "quota") runQuota();
    else if(kind == "timeout") runTimeout();
    else if(kind == "multiplayer") runMultiplayer();
    else if(kind == "natural-1v1") runNatural();
    else endRequire(false, "Unknown case " + kind);
    SDL_Log("GAME_END_PROBE_PASS: %s", kind.c_str());
    return MENU_QUIT_DEFAULT;
}
