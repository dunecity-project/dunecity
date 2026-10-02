/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PATHWORKERPOOL_H
#define PATHWORKERPOOL_H

#include <cstddef>
#include <functional>
#include <vector>

/**
    A bounded, persistent pool for the one thing in pathfinding that is pure:
    advancing an already-constructed A* frontier by a fixed node quota.

    The pool deliberately offers only a batch barrier. The caller selects the
    batch, fixes every quota and the application order before dispatching, waits
    for the whole batch, and then applies results itself in that order. Nothing
    here can therefore influence a gameplay decision: a different worker count
    changes only wall time. No task outlives the batch, so nothing survives the
    mutation-quiet path phase.

    Worker count comes from DUNECITY_PATH_WORKERS (1 runs every task inline on
    the calling thread, which is the Stage 1b serial path verbatim). It is a
    developer and test control, not a product setting, and the default is
    conservative. If threads cannot be created the pool falls back to inline
    execution with no joinable threads left behind.

    DUNECITY_PATH_WORKER_DELAY_US adds a deterministic per-task delay so tests
    can force completion order to differ from dispatch order. It is derived from
    the task index only, never from a clock.
 */
class PathWorkerPool {
public:
    static PathWorkerPool& instance();

    /// Workers actually in use: 1 means inline, >1 means that many threads.
    size_t workerCount();
    /// Runs every task exactly once and returns only when all have finished.
    /// A task's exception is rethrown after the whole batch has been collected.
    void runBatch(const std::vector<std::function<void()>>& tasks);
    /// Joins and stops the threads. Safe to call repeatedly and with no batch running.
    void shutdown();

    PathWorkerPool(const PathWorkerPool&) = delete;
    PathWorkerPool& operator=(const PathWorkerPool&) = delete;

private:
    PathWorkerPool() = default;
    ~PathWorkerPool();

    struct Impl;
    Impl* impl = nullptr;
    /// Worker count whose startup already failed, so it is not retried per batch.
    size_t failedCount = 0;

    void ensureStarted();
    void runInline(const std::vector<std::function<void()>>& tasks);
};

#endif // PATHWORKERPOOL_H
