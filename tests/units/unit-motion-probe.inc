// Headless integration fixture (tests/units/run-unit-motion-probe.py); linked with the production
// game objects, never part of the shipped game.
//
// Smooth unit motion at Stefan's 2 ms game speed (HANDOVER "Unit jitter"): ground units of several
// types and both houses shuttle across open sand while the interactive main loop
// (Game::runMainLoop) runs at 2 ms per cycle with its real frame pacing, so a rendered frame runs
// several cycles. Every cycle and every rendered frame the positions the renderer draws
// (world2screen(realX, realY) inside UnitBase::DrawnStepScope, as Game::drawScreen) are checked
// against the Dynasty step each unit is on:
//   - a moving unit advances at most two cycles' worth of its step per cycle (one if the step
//     changes on that cycle), so it never covers a tile in one cycle;
//   - a moving unit mid-step never stands still for two cycles (a step chained onto the previous
//     one on the same cycle keeps justStoppedMoving, and GroundUnit::checkPos holds the simulated
//     unit on its tile centre for half a step, then moves it a whole tile; 1.0.760-1.0.850 drew it
//     there);
//   - between two rendered frames a unit moves at most the cycles run in between times its step
//     speed (in pixels at the frame's zoom), and never a whole tile unless that speed covers one.
// The simulation is not changed (protocol 88): held_cycles counts the cycles a moving unit was held
// on its tile centre, i.e. drawn somewhere else than simulated.
// The scenario has to chain steps: at least ten chained steps are required.
// A step chains onto the previous one on the same cycle when it is a whole number of route periods
// long and starts on a route tick that is a multiple of 3 (UnitBase::beginDynastyStep): Launcher,
// Deviator and Sonic Tank cross a sand tile in 20 movement ticks, so shuttles are sent back only on
// cycles whose first route tick is such a multiple. Rendering here costs almost nothing, so each
// frame waits a few milliseconds (a repeating 3/9/20 ms pattern) to give the uneven 1-10 cycles per
// frame of Stefan's DuneCity-Performance.log.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <mod/ModManager.h>
#include <units/GroundUnit.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace UnitMotionProbe {

constexpr Uint32 kCycles = 4000;
constexpr double kSlack = 0.05;                           // FixPoint rounding, world units
constexpr Uint32 kFrameDelayMs[] = {3, 9, 20};

/// The first route tick UnitBase::navigate gives a unit ordered to move before @a cycle.
inline Uint64 firstRouteTick(int item, Uint32 cycle) {
    const Uint64 now = static_cast<Uint64>(cycle) * (GAMESPEED_DEFAULT * 3);
    return ((now + 249) / 250) * 5 + ((item == Unit_Harvester || item == Unit_MCV) ? 0 : 5);
}

struct Sample {
    double x = 0, y = 0;
    bool moving = false;
    Uint64 stepStart = 0, stepEnd = 0;
    double speed = 0;          // world units per cycle of the current step, 0 when not on one
    double frameBound = 0;     // sum of per-cycle bounds since the last rendered frame
    double frameX = 0, frameY = 0;
    bool framed = false;
    int stillMidStep = 0;
};

struct State {
    Game* game = nullptr;
    Uint32 firstCycle = 0, stopAt = 0;
    std::map<Uint32, Sample> units;
    std::vector<Uint32> shuttles;
    std::map<Uint32, Coord> homes;         // the other end of each shuttle's lane
    std::set<Uint32> lost;                 // shuttles destroyed during the run
    int heldCycles = 0;                    // cycles a moving unit was held on its tile centre
    int cycles = 0, frames = 0, frameCycles = 0, chained = 0;
    int cycleViolations = 0, stillViolations = 0, frameViolations = 0, frameTileJumps = 0;
    double maxCycleMove = 0, maxFramePx = 0, maxFrameRatio = 0;
    double tilePx = 0;
    std::ofstream cyclesCsv, framesCsv;
    std::string firstFailure;
};
inline State state;

inline void fail(int& counter, const std::string& what) {
    ++counter;
    if (state.firstFailure.empty()) state.firstFailure = what;
}

/// World units per cycle of the Dynasty step @a u is on (UnitBase::moveDynastyStep).
inline double stepSpeed(const UnitBase* u) {
    if (!u->moving || u->dynastyStepEnd <= u->dynastyStepStart) return 0;
    const double length = std::hypot(u->dynastyEndpoint.x - u->dynastyStartX.toDouble(),
                                     u->dynastyEndpoint.y - u->dynastyStartY.toDouble());
    return length * (GAMESPEED_DEFAULT * 3) / static_cast<double>(u->dynastyStepEnd - u->dynastyStepStart);
}

/// Checks the positions read now, before @a g runs its next cycle (onCycle reads them inside the
/// renderer's UnitBase::DrawnStepScope).
inline void measure(Game& g) {
    State& s = state;
    const Uint32 cycle = g.getGameCycleCount();
    const bool frameStart = g.frameTiming.gameCyclesThisFrame == 0;
    if (frameStart) ++s.frames;
    ++s.frameCycles;
    for (UnitBase* u : unitList) {
        if (!u->isAGroundUnit() || !u->usesDynastyGroundTiming() || !u->isActive()) continue;
        const double x = u->realX.toDouble(), y = u->realY.toDouble();
        const double speed = stepSpeed(u);
        auto [it, inserted] = s.units.try_emplace(u->getObjectID());
        Sample& p = it->second;
        if (!inserted) {
            const double moved = std::hypot(x - p.x, y - p.y);
            const bool newStep = u->moving && (!p.moving || u->dynastyStepStart != p.stepStart);
            if (u->moving && p.moving && u->dynastyStepStart != p.stepStart) ++s.chained;
            // Moved during the last cycle: up to one cycle of its step plus the lag of a step that
            // started before the cycle boundary (< 1 cycle); a step change adds the previous
            // step's last cycle.
            const double bound = 2 * std::max(speed, p.speed) + kSlack;
            if (p.moving || u->moving) {
                s.maxCycleMove = std::max(s.maxCycleMove, moved);
                if (moved > bound)
                    fail(s.cycleViolations, "cycle " + std::to_string(cycle) + ": unit " + std::to_string(u->getObjectID())
                         + " (item " + std::to_string(u->getItemID()) + ") moved " + std::to_string(moved)
                         + " world units in one cycle, step speed " + std::to_string(std::max(speed, p.speed)));
                p.frameBound += bound;
            }
            // Mid-step means strictly inside it: started a cycle ago or more and not yet due to end.
            const Uint64 now = static_cast<Uint64>(cycle) * (GAMESPEED_DEFAULT * 3);
            const bool midStep = u->moving && !newStep && now > u->dynastyStepStart + GAMESPEED_DEFAULT * 3
                                 && now < u->dynastyStepEnd;
            p.stillMidStep = midStep && moved == 0 ? p.stillMidStep + 1 : 0;
            if (midStep && u->justStoppedMoving) ++s.heldCycles;   // simulated on its tile centre
            if (p.stillMidStep == 2)
                fail(s.stillViolations, "cycle " + std::to_string(cycle) + ": unit " + std::to_string(u->getObjectID())
                     + " (item " + std::to_string(u->getItemID()) + ") stands still mid-step at "
                     + std::to_string(x) + "," + std::to_string(y));
            if (moved != 0)
                s.cyclesCsv << cycle << ',' << u->getObjectID() << ',' << u->getItemID() << ',' << x << ',' << y << ','
                            << moved << ',' << bound << ',' << u->moving << ',' << u->justStoppedMoving << '\n';
        }
        if (frameStart) {
            // The frame just rendered drew this position (renderFrame runs before the frame's cycles).
            if (p.framed) {
                const double world = std::hypot(x - p.frameX, y - p.frameY);
                const double px = world * s.tilePx / TILESIZE, boundPx = p.frameBound * s.tilePx / TILESIZE;
                s.maxFramePx = std::max(s.maxFramePx, px);
                if (p.frameBound > 0) s.maxFrameRatio = std::max(s.maxFrameRatio, world / p.frameBound);
                if (world > p.frameBound + kSlack)
                    fail(s.frameViolations, "frame at cycle " + std::to_string(cycle) + ": unit " + std::to_string(u->getObjectID())
                         + " drawn " + std::to_string(px) + " px from its previous frame, bound " + std::to_string(boundPx) + " px");
                if (world >= TILESIZE && p.frameBound < TILESIZE)
                    fail(s.frameTileJumps, "frame at cycle " + std::to_string(cycle) + ": unit " + std::to_string(u->getObjectID())
                         + " jumped a tile between two frames");
                if (world != 0)
                    s.framesCsv << cycle << ',' << s.frameCycles << ',' << u->getObjectID() << ',' << u->getItemID() << ','
                                << x << ',' << y << ',' << px << ',' << boundPx << '\n';
            }
            p.framed = true; p.frameX = x; p.frameY = y; p.frameBound = 0;
        }
        p.x = x; p.y = y; p.moving = u->moving; p.stepStart = u->dynastyStepStart; p.stepEnd = u->dynastyStepEnd; p.speed = speed;
    }
    if (frameStart) s.frameCycles = 1;
    ++s.cycles;
}

inline void onCycle(Game& g) {
    State& s = state;
    {
        // What Game::drawScreen would draw now; the simulation's positions come back at the brace.
        const UnitBase::DrawnStepScope drawn;
        measure(g);
    }
    // Shuttles turn round at either end of their lane, on a cycle whose first route tick is a
    // multiple of 3 (see the top). Looked up by ID: a destroyed unit is gone from the object manager.
    const Uint32 cycle = g.getGameCycleCount();
    for (const Uint32 id : s.shuttles) {
        auto* u = dynamic_cast<UnitBase*>(g.getObjectManager().getObject(id));
        if (u == nullptr) {
            if (s.lost.insert(id).second) SDL_Log("UNIT_MOTION_PROBE_LOST: unit %u at cycle %u", id, cycle);
            continue;
        }
        if (!u->moving && u->location == u->destination && firstRouteTick(u->getItemID(), cycle) % 3 == 0) {
            const Coord back = s.homes[id];
            s.homes[id] = u->location;
            u->doMove2Pos(back.x, back.y, true);
        }
    }
}

} // namespace UnitMotionProbe

int runUnitMotionProbe() {
    using namespace UnitMotionProbe;
    auto require = [](bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); };
    const std::string out = std::getenv("UNIT_MOTION_PROBE_OUT");
    require(ModManager::instance().setActiveMod("dunecity"), "Missing dunecity mod");
    settings.general.playerName = "Motion tester";
    // Without the map's two sandworms: on all-sand they would eat the shuttles.
    std::ifstream mapFile(PROBE_MAP_PATH);
    std::string mapData;
    for (std::string line; std::getline(mapFile, line);)
        if (line.find(",Sandworm,") == std::string::npos) mapData += line + '\n';
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    // A single-player custom game, as Stefan's: its speed is the player's setting (Game::getGameSpeed)
    // and its frames are paced (FramePacing); a network game type would run at the match's speed.
    GameInitSettings init("motion.ini", mapData, true, settings.gameOptions);
    init.randomSeed = 92;
    GameInitSettings::HouseInfo atreides(HOUSE_ATREIDES, 1);
    atreides.addPlayerInfo({"Motion tester", "HumanPlayer"});
    init.addHouseInfo(atreides);
    // Same team: the shuttles share the map and must not shoot each other.
    GameInitSettings::HouseInfo harkonnen(HOUSE_HARKONNEN, 1);
    harkonnen.addPlayerInfo({"Other", "HumanPlayer"});
    init.addHouseInfo(harkonnen);
    game->initGame(init);
    House* houses[2] = {game->getHouse(HOUSE_ATREIDES), game->getHouse(HOUSE_HARKONNEN)};
    require(houses[0] && houses[1], "Motion fixture houses missing");

    // Open sand everywhere: no bumps on rock, no spice, no obstacles but the units themselves.
    for (int y = 0; y < currentGameMap->getSizeY(); ++y)
        for (int x = 0; x < currentGameMap->getSizeX(); ++x) {
            Tile* tile = currentGameMap->getTile(x, y);
            tile->setType(Terrain_Sand);
            tile->setRoad(false);
        }

    State& s = state;
    s.game = game.get();
    s.cyclesCsv.open(out + "/unit-motion-cycles.csv");
    s.cyclesCsv << "cycle,id,item,x,y,moved,bound,moving,jsm\n";
    s.framesCsv.open(out + "/unit-motion-frames.csv");
    s.framesCsv << "cycle,cycles,id,item,x,y,px,bound_px\n";
    const int items[] = {Unit_Tank, Unit_Launcher, Unit_SiegeTank, Unit_Quad, Unit_Trike, Unit_Harvester,
                         Unit_Devastator, Unit_SonicTank, Unit_Deviator, Unit_MCV};
    // Horizontal lanes on every other row of the left part of the map, both houses.
    int lane = 0;
    for (int y = 1; y < currentGameMap->getSizeY() - 1; y += 2, ++lane) {
        House* owner = houses[lane % 2];
        auto* unit = owner->createUnit(items[lane % std::size(items)], true);
        require(unit != nullptr, "Could not create a motion fixture unit");
        const Coord home(1, y), far(30, y);
        unit->deploy(home);
        unit->doMove2Pos(far.x, far.y, true);
        s.homes[unit->getObjectID()] = home;
        s.shuttles.push_back(unit->getObjectID());
    }
    // Parallel diagonals on the right part.
    for (int k = 0; k < 3; ++k, ++lane) {
        auto* unit = houses[k % 2]->createUnit(items[(k * 3) % std::size(items)], true);
        require(unit != nullptr, "Could not create a diagonal motion fixture unit");
        const Coord home(33, 1 + 6 * k), far(33 + 12, 13 + 6 * k);
        unit->deploy(home);
        unit->doMove2Pos(far.x, far.y, true);
        s.homes[unit->getObjectID()] = home;
        s.shuttles.push_back(unit->getObjectID());
    }

    // Stefan's game speed; the interactive loop, its frame pacing and the renderer (dummy driver).
    settings.gameOptions.gameSpeed = 2;
    s.firstCycle = game->getGameCycleCount();
    s.stopAt = s.firstCycle + kCycles;
    game->beforeCycleHook = [](Game& g) {
        if (state.tilePx == 0) state.tilePx = screenborder->world2screenX(TILESIZE) - screenborder->world2screenX(0);
        const bool frameStart = g.frameTiming.gameCyclesThisFrame == 0;
        onCycle(g);
        // This frame's render/AI cost (see the top): it delays the frame's remaining cycles and
        // the next frame, as in the real game.
        if (frameStart) {
            // Spin rather than sleep: SDL_Delay(20) in this headless process took up to ~150 ms.
            const Uint64 until = SDL_GetPerformanceCounter()
                + SDL_GetPerformanceFrequency() * kFrameDelayMs[state.frames % std::size(kFrameDelayMs)] / 1000;
            while (SDL_GetPerformanceCounter() < until) {}
        }
    };
    const SDL_TimerID timer = SDL_AddTimer(20, [](Uint32 interval, void*) -> Uint32 {
        if (currentGame != nullptr && currentGame->getGameCycleCount() >= state.stopAt) currentGame->bQuitGame = true;
        return interval;
    }, nullptr);
    game->runMainLoop();
    SDL_RemoveTimer(timer);
    game->beforeCycleHook = nullptr;
    s.cyclesCsv.close();
    s.framesCsv.close();

    const double cyclesPerFrame = s.frames ? static_cast<double>(s.cycles) / s.frames : 0;
    SDL_Log("UNIT_MOTION_PROBE: cycles=%d frames=%d cycles_per_frame=%.2f tile_px=%.0f chained_steps=%d "
            "held_cycles=%d max_cycle_move=%.2f max_frame_px=%.2f max_frame_ratio=%.3f "
            "cycle_violations=%d still_mid_step=%d frame_violations=%d frame_tile_jumps=%d lost_shuttles=%zu",
            s.cycles, s.frames, cyclesPerFrame, s.tilePx, s.chained, s.heldCycles, s.maxCycleMove, s.maxFramePx,
            s.maxFrameRatio, s.cycleViolations, s.stillViolations, s.frameViolations, s.frameTileJumps, s.lost.size());
    if (!s.firstFailure.empty()) SDL_Log("UNIT_MOTION_PROBE_FIRST_FAILURE: %s", s.firstFailure.c_str());
    require(s.cycles >= static_cast<int>(kCycles) - 1, "The main loop ended early");
    require(cyclesPerFrame >= 2.0, "2 ms game speed should run several cycles per rendered frame");
    require(s.lost.empty(), "A shuttle was destroyed during the run");
    require(s.chained >= 10, "The scenario no longer chains steps on the cycle the previous one ended");
    require(s.cycleViolations == 0, "A moving unit covered more than its step speed in one cycle: " + s.firstFailure);
    require(s.stillViolations == 0, "A moving unit stood still mid-step: " + s.firstFailure);
    require(s.frameViolations == 0 && s.frameTileJumps == 0,
            "A unit was drawn further from its previous frame than its speed allows: " + s.firstFailure);
    SDL_Log("UNIT_MOTION_PROBE_PASS");
    game.reset();
    currentGame = nullptr;
    return -1;
}
