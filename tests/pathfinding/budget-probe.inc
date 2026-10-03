// Included only by run-budget-probe.py, never by the shipped game.
//
// Exercises the strict per-cycle pathfinding node budget against the production
// scheduler on a real city save: slice/whole equality, the hard budget, the
// cross-tick input policy, queue fairness and memory bounds, observer
// continuation captured mid-suspension, ordinary mid-suspension load, and
// teardown while searches are suspended.
#include <AStarSearch.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <PathWorkerPool.h>
#include <ScreenBorder.h>
#include <Network/GameStateDigest.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <players/AIDecisionLog.h>
#include <players/HumanPlayer.h>
#include <structures/StructureBase.h>
#include <Network/NetworkManager.h>
#include <mod/ModManager.h>
#include <units/Harvester.h>
#include <units/RebelHarvester.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

int gChecks = 0;
std::string gOutput;

void require(bool ok, const char* why) {
    if(!ok) {
        SDL_Log("BUDGET_PROBE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
    ++gChecks;
}

std::unique_ptr<Game> loadFixture() {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;  // No interactive briefing or public match analytics.
    game->initGame(GameInitSettings(std::string(std::getenv("BUDGET_PROBE_SAVE"))));
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    return game;
}

std::string digestOf(Game& game) {
    return GameStateDigest::describe(game.computeStateDigest());
}

// The request-lifecycle invariant, checked after every real cycle: a pending id is
// either queued or active, never both, never neither, and never twice.
void requireLifecycle(Game& game, const char* when) {
    static std::string detail;
    auto fail = [&](const char* what) {
        detail = std::string(when) + ": " + what;
        return detail.c_str();
    };
    std::set<Uint32> active, queued;
    for(const auto& job : game.activePathJobs) {
        require(active.insert(job.objectId).second, fail("active ids are unique"));
        require(game.pendingPathRequestIds.count(job.objectId) == 1, fail("an active id is pending"));
        auto* unit = dynamic_cast<UnitBase*>(game.getObjectManager().getObject(job.objectId));
        if(unit == nullptr) {
            // A unit can be destroyed later in the cycle, after the path phase, so
            // a slot whose unit has gone is legitimate until the next phase cancels
            // it. The duplicate and pending invariants above still had to hold.
            continue;
        }
        if(!unit->pathRequestQueued) {
            size_t queueCopies = 0;
            for(const auto& entry : game.pathRequestQueue) if(entry.objectId == job.objectId) ++queueCopies;
            SDL_Log("BUDGET_PROBE_DIAG: id=%u item=%d timer=%d flag=%d queued_copies=%zu charged=%zu "
                    "restarts=%u reasons=%u requeue=%d path=%zu active=%zu",
                job.objectId, unit->getItemID(), unit->recalculatePathTimer, unit->pathRequestQueued ? 1 : 0,
                queueCopies, job.chargedNodes, job.restarts, job.restartReasons, job.requeue ? 1 : 0,
                unit->getPlannedPath().size(), game.activePathJobs.size());
        }
        require(unit->pathRequestQueued, fail("an active job's unit still holds its pending flag"));
    }
    for(const auto& request : game.pathRequestQueue) {
        require(queued.insert(request.objectId).second, fail("no duplicate queue entries"));
        require(game.pendingPathRequestIds.count(request.objectId) == 1, fail("a queued id is pending"));
        require(active.count(request.objectId) == 0, fail("no id is both queued and active"));
    }
    require(game.pendingPathRequestIds.size() == active.size() + queued.size(),
        fail("pending ids are exactly queued plus active"));
    // Conservation: every started request is applied once, cancelled, requeued or
    // still running. A second application for one request would break this.
    require(game.pathRequestsStarted
            == game.pathResultsApplied + game.pathRequestsCancelled
             + game.pathRequestsRequeued + game.activePathJobs.size(),
        fail("started requests equal applied plus cancelled plus requeued plus active"));
}

// Ground units with a real destination, in object-id order so every phase picks
// the same ones.
std::vector<UnitBase*> movableUnits(size_t limit) {
    std::vector<UnitBase*> units;
    for(UnitBase* unit : unitList) {
        if(unit->isAFlyingUnit() || !unit->isActive()) continue;
        if(!currentGameMap->tileExists(unit->getLocation())) continue;
        units.push_back(unit);
    }
    std::sort(units.begin(), units.end(), [](const UnitBase* a, const UnitBase* b) {
        return a->getObjectID() < b->getObjectID();
    });
    if(units.size() > limit) units.resize(limit);
    return units;
}

// ---------------------------------------------------------------------------
// 1. A sliced search must return exactly what the whole search returns.
// ---------------------------------------------------------------------------
void checkSliceEqualsWhole() {
    auto game = loadFixture();
    const auto units = movableUnits(24);
    require(units.size() >= 8, "Fixture provides ground units for slice comparison");
    const int width = currentGameMap->getSizeX(), height = currentGameMap->getSizeY();
    size_t compared = 0, resumed = 0;
    size_t totalNodes = 0;

    for(UnitBase* unit : units) {
        const Coord start = unit->getLocation();
        const Coord destinations[] = {
            Coord(width - 1, height - 1), Coord(0, 0), Coord(width / 2, height / 2),
            Coord((unit->getObjectID() * 37u) % width, (unit->getObjectID() * 71u) % height)
        };
        for(const Coord& destination : destinations) {
            if(!currentGameMap->tileExists(destination)) continue;

            std::list<Coord> whole;
            int wholeNodes = 0;
            {
                AStarSearch search(currentGameMap, unit, start, destination);
                whole = search.getFoundPath();
                wholeNodes = search.getNodesChecked();
            }
            totalNodes += static_cast<size_t>(wholeNodes);

            for(const size_t quota : {size_t(1), size_t(3), size_t(17), size_t(251), size_t(5000)}) {
                AStarSearch sliced(currentGameMap, unit, start, destination, AStarSearch::Resumable{});
                size_t slices = 0, charged = 0;
                while(sliced.step(currentGameMap, unit, quota, quota + 4096) == AStarSearch::Status::Running) {
                    const size_t closed = sliced.getNodesClosedLastStep();
                    require(closed <= quota, "A slice never closes more nodes than its quota");
                    charged += closed;
                    if(++slices > 400000) { require(false, "A sliced search made progress and terminated"); }
                }
                charged += sliced.getNodesClosedLastStep();
                require(sliced.getFoundPath() == whole, "A sliced search returns the whole search's route");
                require(sliced.getNodesChecked() == wholeNodes, "A sliced search closes the whole search's nodes");
                require(charged == static_cast<size_t>(wholeNodes), "Charged slices sum to the node count");
                if(slices > 1) ++resumed;
                ++compared;
            }
        }
    }
    require(resumed > 0, "The comparison actually exercised multi-slice searches");
    SDL_Log("BUDGET_PROBE_SLICES: comparisons=%zu multi_slice=%zu whole_nodes=%zu", compared, resumed, totalNodes);
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
}

// ---------------------------------------------------------------------------
// 2. Hard budget under a deliberate request storm, plus fairness and bounds.
// ---------------------------------------------------------------------------
void checkStrictBudget() {
    auto game = loadFixture();
    require(pNetworkManager == nullptr, "The probe runs single-player, where carry-over applies");

    // Dropping every cached route makes all ground units eligible at once, which
    // is the load the strict budget has to hold.
    size_t cleared = 0;
    for(UnitBase* unit : unitList) {
        if(unit->isAFlyingUnit()) continue;
        unit->clearPath();
        ++cleared;
    }
    game->negotiatedBudget = 5000;
    game->carryOverTokens = 0;

    // A sample whose movement proves routes are actually being delivered and used.
    std::vector<std::pair<Uint32,Coord>> watched;
    for(UnitBase* unit : movableUnits(300)) watched.emplace_back(unit->getObjectID(), unit->getLocation());

    const int cycles = 400;
    size_t worstUsed = 0, worstQueue = 0, worstSuspended = 0, totalUsed = 0, completions = 0;
    size_t cyclesAtCap = 0, lateCompletions = 0;
    for(int i = 0; i < cycles; ++i) {
        const size_t carryBefore = game->carryOverTokens;
        const size_t effective = std::min<size_t>(game->negotiatedBudget + carryBefore, Game::kHardCap);
        const size_t processedBefore = game->frameTiming.totalPathsProcessed;

        game->updateGameState();

        const size_t used = game->frameTiming.pathTokensThisCycle;
        require(used <= effective, "A cycle never spends more nodes than its effective budget");
        require(game->activePathJobs.size() <= Game::kPathBatchSize, "Active searches stay within the batch bound");
        for(const auto& job : game->activePathJobs) {
            require(job.search != nullptr, "An active slot always holds a live search");
        }
        // The pending-id set dedupes, so the backlog can never exceed the units
        // that could be asking. A strict budget does mean a storm leaves a
        // backlog: that is throughput being bounded, which is the point.
        require(game->pathRequestQueue.size() <= unitList.size(), "The queue stays bounded by the unit count");
        requireLifecycle(*game, "Request lifecycle holds every cycle of the storm");
        worstUsed = std::max(worstUsed, used);
        worstQueue = std::max(worstQueue, game->pathRequestQueue.size());
        worstSuspended = std::max(worstSuspended, game->activePathJobs.size());
        totalUsed += used;
        const size_t done = game->frameTiming.totalPathsProcessed - processedBefore;
        completions += done;
        if(i >= cycles * 3 / 4) lateCompletions += done;
        if(used == effective) ++cyclesAtCap;
        AITelemetry::log().flushPerformance(game->getGameCycleCount());
        if((i + 1) % 50 == 0) {
            std::string charges;
            for(const auto& job : game->activePathJobs) {
                charges += " " + std::to_string(job.chargedNodes) + "/" + std::to_string(job.restarts)
                         + "/reasons=" + std::to_string(job.restartReasons);
            }
            SDL_Log("BUDGET_PROBE_TRACE: cycle=%d used=%zu queue=%zu suspended=%zu completions=%zu charges=%s",
                i + 1, used, game->pathRequestQueue.size(), game->activePathJobs.size(), completions, charges.c_str());
        }
    }

    require(completions > 0, "The storm actually completed searches");
    require(cyclesAtCap > 0, "The budget was actually the binding constraint");
    // Progress has to be sustained, not just an initial burst: a stalled or
    // starved scheduler would stop completing requests while the queue stayed full.
    require(lateCompletions > 0, "Requests keep completing late in the storm");

    // Routes have to be delivered and used, not merely computed.
    size_t moved = 0, routed = 0;
    for(const auto& entry : watched) {
        auto* unit = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(entry.first));
        if(unit == nullptr) continue;
        if(unit->getLocation() != entry.second) ++moved;
        if(!unit->getPlannedPath().empty()) ++routed;
    }
    require(moved > watched.size() / 20, "A meaningful share of watched units actually moved");
    require(routed > 0, "Watched units are holding delivered routes");
    require(game->pathResultsApplied > 0, "Results were applied to units");
    // Buffers in use must be bounded by the batch, not by the queue.
    const auto pool = AStarSearch::getPoolUsageStats();
    require(pool.buffersInUse <= Game::kPathBatchSize, "Scratch buffers in use stay within the batch bound");
    SDL_Log("BUDGET_PROBE_BUDGET: cycles=%d cleared=%zu budget=5000 max_used=%zu cycles_at_cap=%zu "
            "total_used=%zu completions=%zu late_completions=%zu max_queue=%zu final_queue=%zu "
            "max_suspended=%zu buffers=%zu started=%zu applied=%zu requeued=%zu cancelled=%zu "
            "watched=%zu moved=%zu routed=%zu",
        cycles, cleared, worstUsed, cyclesAtCap, totalUsed, completions, lateCompletions,
        worstQueue, game->pathRequestQueue.size(), worstSuspended, pool.buffersInUse,
        game->pathRequestsStarted, game->pathResultsApplied, game->pathRequestsRequeued,
        game->pathRequestsCancelled, watched.size(), moved, routed);
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
}

// ---------------------------------------------------------------------------
// 3. Cross-tick input policy: unrelated movement must not restart a search,
//    but the unit's own inputs changing must.
// ---------------------------------------------------------------------------
#include "cross-tick-phase.inc"

// Advances a freshly loaded fixture until suspended searches exist, using a
// small budget and a storm of cleared routes. Returns the cycle reached.
Uint32 advanceToSuspension(Game& game, int limit) {
    for(UnitBase* unit : unitList) {
        if(!unit->isAFlyingUnit()) unit->clearPath();
    }
    // Stays inside the negotiable range: the observer stream validates the budget
    // it restores, and the storm above is enough to suspend searches at 5000.
    game.negotiatedBudget = Game::kMinBudget;
    game.carryOverTokens = 0;
    for(int i = 0; i < limit; ++i) {
        game.updateGameState();
        if(!game.activePathJobs.empty()) break;
    }
    return game.getGameCycleCount();
}

#include "observer-phase.inc"
#include "dead-slot-phase.inc"

// ---------------------------------------------------------------------------
// 4b. Unit traffic versus long-lived geometry, and the cost of a goal this
//     unit cannot enter. Uses steerable()/pinOrder()/resetScheduler() above.
// ---------------------------------------------------------------------------
#include "traffic-phase.inc"

// ---------------------------------------------------------------------------
// 5. An ordinary save taken mid-suspension keeps the established reset
//    semantics, and the affected units must requeue promptly rather than
//    waiting out a stale cooldown.
// ---------------------------------------------------------------------------
void checkOrdinaryMidSuspensionLoad() {
    const std::string midSave = gOutput + "/mid-suspension.dls";
    std::vector<Uint32> suspendedUnits;
    {
        auto game = loadFixture();
        advanceToSuspension(*game, 60);
        require(!game->activePathJobs.empty(), "The ordinary save is taken while searches are suspended");
        for(const auto& job : game->activePathJobs) {
            suspendedUnits.push_back(job.objectId);
            auto* unit = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(job.objectId));
            require(unit != nullptr, "The suspended unit exists");
            // The cooldown is only set when a search finishes, so a suspended unit
            // carries no positive countdown that could defer its requeue. The value
            // is the pre-existing -1 "queued work" sentinel that enqueuePathRequest
            // sets and that UnitBase::load normalises to 0 on an ordinary load.
            require(unit->recalculatePathTimer <= 0,
                "A suspended search leaves no positive cooldown to wait out");
            require(unit->pathRequestQueued, "A suspended search keeps the unit's pending flag set");
        }
        require(game->saveGame(midSave.c_str()), "Saved mid-suspension");
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }

    {
        auto game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;
        game->initGame(GameInitSettings(midSave));
        game->bReplay = false;
        game->initializeGameLoop();
        game->bPause = false;
        require(game->activePathJobs.empty(), "An ordinary load starts with no suspended searches");
        for(const Uint32 id : suspendedUnits) {
            auto* unit = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(id));
            if(unit == nullptr) continue;
            require(!unit->pathRequestQueued, "An ordinary load reset the pending flag");
            require(unit->recalculatePathTimer == 0, "An ordinary load carries no stale cooldown");
        }
        int requeueCycle = -1;
        for(int i = 0; i < 25 && requeueCycle < 0; ++i) {
            game->updateGameState();
            for(const Uint32 id : suspendedUnits) {
                auto* unit = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(id));
                if(unit != nullptr && (unit->pathRequestQueued || !unit->pathList.empty())) {
                    requeueCycle = i + 1;
                    break;
                }
            }
        }
        require(requeueCycle >= 0, "A previously suspended unit requeues promptly after an ordinary load");
        SDL_Log("BUDGET_PROBE_ORDINARY_LOAD: units=%zu requeued_within_cycles=%d",
            suspendedUnits.size(), requeueCycle);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }
}

// ---------------------------------------------------------------------------
// 6. Tearing the match down mid-suspension must release everything.
// ---------------------------------------------------------------------------
void checkCleanupDuringSuspension() {
    size_t suspended = 0;
    {
        auto game = loadFixture();
        advanceToSuspension(*game, 60);
        suspended = game->activePathJobs.size();
        require(suspended > 0, "Teardown happens with searches suspended");
        require(AStarSearch::getPoolUsageStats().buffersInUse >= 1, "Suspended searches hold scratch buffers");
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }
    require(AStarSearch::getPoolUsageStats().buffersInUse == 0, "Teardown released every scratch buffer");
    SDL_Log("BUDGET_PROBE_CLEANUP: suspended_at_teardown=%zu buffers_after=%zu",
        suspended, AStarSearch::getPoolUsageStats().buffersInUse);
}

} // namespace

int runBudgetProbe() {
    ModManager::instance().setActiveMod("dunecity");
    gOutput = std::getenv("BUDGET_PROBE_OUTPUT");
    // All phases unless a comma-separated subset is named, which keeps iteration
    // on one phase cheap without weakening the default run.
    const char* requested = std::getenv("BUDGET_PROBE_PHASES");
    const std::string phases = (requested != nullptr && *requested != '\0') ? requested : "slice,budget,inputs,traffic,control,observer,parser,deadslot,ordinary,cleanup";
    auto wanted = [&phases](const char* name) { return phases.find(name) != std::string::npos; };

    if(wanted("slice")) checkSliceEqualsWhole();
    if(wanted("budget")) checkStrictBudget();
    if(wanted("inputs")) checkCrossTickInputs();
    if(wanted("traffic")) {
        checkTrafficVersusGeometry();
        checkOccupiedGoalApproach();
        checkHarvesterPendingGuard();
    }
    if(wanted("control")) checkObserverRestoreControl();
    if(wanted("observer")) checkObserverContinuation();
    if(wanted("parser")) checkMalformedContinuation();
    if(wanted("deadslot")) checkDeadActiveSlotCheckpoint();
    if(wanted("ordinary")) checkOrdinaryMidSuspensionLoad();
    if(wanted("cleanup")) checkCleanupDuringSuspension();
    SDL_Log("BUDGET_PROBE_PHASES: %s", phases.c_str());

    SDL_Log("BUDGET_PROBE_PASS: checks=%d", gChecks);
    return MENU_QUIT_DEFAULT;
}
