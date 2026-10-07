// Included only by run-pathfinding-probe.py, never by the shipped game.
#include <AStarSearch.h>
#include "ReferenceAStarSearch.h"
#include <Game.h>
#include <GameInitSettings.h>
#include <Map.h>
#include <units/UnitBase.h>
#include <mod/ModManager.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <set>

int runPathfindingProbe() {
    ModManager::instance().setActiveMod("dunecity");
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true; // No interactive briefing or public match analytics.
    game->initGame(GameInitSettings(std::string(std::getenv("PATH_PROBE_SAVE"))));
    const int width = currentGameMap->getSizeX(), height = currentGameMap->getSizeY();
    struct Query { UnitBase* unit; Coord start, destination; };
    std::vector<Query> queries;
    std::set<int> types;
    for (auto* unit : unitList) {
        if (unit->isAFlyingUnit() || !unit->isActive()
                || !currentGameMap->tileExists(unit->getLocation())) continue;
        const Coord start = unit->getLocation();
        types.insert(unit->getItemID());
        const Coord targets[] = {
            start, Coord(std::min(start.x + 1, width - 1), start.y),
            unit->resolvePathDestination(), Coord(0, 0), Coord(width - 1, height - 1),
            Coord(width / 2, height / 2),
            Coord((unit->getObjectID() * 37u) % width, (unit->getObjectID() * 71u) % height)
        };
        for (const auto& destination : targets)
            if (currentGameMap->tileExists(destination)) queries.push_back({unit, start, destination});
    }
    if (queries.size() < 20) throw std::runtime_error("Save needs at least 20 ground-unit path queries");
    using Clock = std::chrono::steady_clock;
    // Reuse the scratch pool after an obstacle change, as happens when a
    // building is placed or a unit moves between synchronous searches.
    Tile* changedTile = nullptr;
    for (const auto& q : queries) {
        const Coord next(std::min(q.start.x + 1, width - 1), q.start.y);
        auto* tile = currentGameMap->getTile(next);
        if (!tile->hasAGroundObject() && !tile->isMountain()) { changedTile = tile; break; }
    }
    if (!changedTile) throw std::runtime_error("Missing obstacle-change fixture");
    const int originalTerrain = changedTile->getType();
    int capped = 0, empty = 0, blockedGoals = 0;
    std::size_t nodes = 0, savedNodes = 0;
    double referenceMs = 0, candidateMs = 0;
    const int rounds = 3;
    for (int round = 0; round < rounds; ++round) {
        if (round == 1) changedTile->setType(Terrain_Mountain);
        if (round == 2) changedTile->setType(originalTerrain);
        double roundReferenceMs = 0, roundCandidateMs = 0;
        for (std::size_t i = 0; i < queries.size(); ++i) {
            const auto& q = queries[i];
            std::list<Coord> expected, actual;
            int expectedNodes = 0, actualNodes = 0;
            auto reference = [&] {
                const auto begin = Clock::now();
                {
                    ReferenceAStarSearch search(currentGameMap, q.unit, q.start, q.destination);
                    expected = search.getFoundPath(); expectedNodes = search.getNodesChecked();
                }
                roundReferenceMs += std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
            };
            auto candidate = [&] {
                const auto begin = Clock::now();
                {
                    AStarSearch search(currentGameMap, q.unit, q.start, q.destination);
                    actual = search.getFoundPath(); actualNodes = search.getNodesChecked();
                }
                roundCandidateMs += std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
            };
            // Alternate order to avoid giving one version consistently warm caches.
            if ((i + round) % 2) { reference(); candidate(); }
            else { candidate(); reference(); }
            // A goal this unit may enter must still produce byte-identical work:
            // that is the regression guard. A goal it may not enter is now
            // approached from an adjacent tile instead of being proved
            // unreachable, so for those the contract is the approach itself --
            // no more work than the proof cost, a passable route, and never a
            // step onto the goal.
            const bool goalEnterable = q.unit->canPass(q.destination.x, q.destination.y);
            bool ok = goalEnterable ? (expected == actual && expectedNodes == actualNodes)
                                    : (actualNodes <= expectedNodes);
            if (ok && !goalEnterable) {
                for (const Coord& step : actual) {
                    if (step == q.destination || !q.unit->canPass(step.x, step.y)) { ok = false; break; }
                }
                if (ok && !actual.empty() && actual != expected) {
                    const Coord last = actual.back();
                    ok = std::max(std::abs(last.x-q.destination.x), std::abs(last.y-q.destination.y)) <= 1;
                }
            }
            if (!ok) {
                SDL_Log("PATH_PROBE_FAIL: round=%d query=%zu unit=%u item=%d start=%d,%d dest=%d,%d "
                        "enterable=%d nodes=%d/%d len=%zu/%zu",
                    round,i,q.unit->getObjectID(),q.unit->getItemID(),q.start.x,q.start.y,
                    q.destination.x,q.destination.y,goalEnterable?1:0,expectedNodes,actualNodes,
                    expected.size(),actual.size());
                throw std::runtime_error(goalEnterable
                    ? "Pathfinding route or expansion count changed for an enterable goal"
                    : "Occupied-goal approach is not a bounded, passable route next to the goal");
            }
            if (round == 0) {
                capped += expectedNodes >= 16384; empty += expected.empty(); nodes += expectedNodes;
                if (!goalEnterable) { blockedGoals++; savedNodes += expectedNodes - actualNodes; }
            }
        }
        referenceMs += roundReferenceMs; candidateMs += roundCandidateMs;
        SDL_Log("PATH_PROBE_ROUND: round=%d queries=%zu reference_ms=%.3f candidate_ms=%.3f",
            round,queries.size(),roundReferenceMs,roundCandidateMs);
    }
    SDL_Log("PATH_PROBE_PASS: queries=%zu rounds=%d unit_types=%zu nodes=%zu capped=%d empty=%d "
            "blocked_goals=%d nodes_saved_on_blocked_goals=%zu "
            "reference_ms=%.3f candidate_ms=%.3f speedup=%.3f",
        queries.size(),rounds,types.size(),nodes,capped,empty,blockedGoals,savedNodes,
        referenceMs,candidateMs,referenceMs/candidateMs);
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    return MENU_QUIT_DEFAULT;
}
