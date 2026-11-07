#include <catch2/catch_test_macros.hpp>

#include <PathWorkerPool.h>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

void setWorkers(const char* count) {
#ifdef _WIN32
    _putenv_s("DUNECITY_PATH_WORKERS", count == nullptr ? "" : count);
#else
    if(count == nullptr) {
        unsetenv("DUNECITY_PATH_WORKERS");
    } else {
        setenv("DUNECITY_PATH_WORKERS", count, 1);
    }
#endif
}

std::vector<std::function<void()>> countingBatch(std::atomic<int>& ran, size_t size) {
    std::vector<std::function<void()>> tasks;
    for(size_t i = 0; i < size; ++i) tasks.push_back([&ran] { ++ran; });
    return tasks;
}

} // namespace

TEST_CASE("PathWorkerPool: one worker really runs inline", "[pathfinding][workers]") {
    setWorkers("1");
    REQUIRE(PathWorkerPool::instance().workerCount() == 1);
    std::atomic<int> ran{0};
    const auto tasks = countingBatch(ran, 4);
    PathWorkerPool::instance().runBatch(tasks);
    REQUIRE(ran.load() == 4);
    PathWorkerPool::instance().shutdown();
    setWorkers(nullptr);
}

TEST_CASE("PathWorkerPool: switching the count takes effect in one process", "[pathfinding][workers]") {
    // The switch down to 1 has to stop the pool, not keep dispatching on the old
    // threads, and the switch back up has to build the new count.
    setWorkers("2");
    REQUIRE(PathWorkerPool::instance().workerCount() == 2);
    setWorkers("1");
    REQUIRE(PathWorkerPool::instance().workerCount() == 1);
    setWorkers("4");
    REQUIRE(PathWorkerPool::instance().workerCount() == 4);

    std::atomic<int> ran{0};
    const auto tasks = countingBatch(ran, 16);
    PathWorkerPool::instance().runBatch(tasks);
    REQUIRE(ran.load() == 16);

    setWorkers("1");
    REQUIRE(PathWorkerPool::instance().workerCount() == 1);
    std::atomic<int> inlineRan{0};
    const auto inlineTasks = countingBatch(inlineRan, 3);
    PathWorkerPool::instance().runBatch(inlineTasks);
    REQUIRE(inlineRan.load() == 3);
    PathWorkerPool::instance().shutdown();
    setWorkers(nullptr);
}

TEST_CASE("PathWorkerPool: every task runs exactly once", "[pathfinding][workers]") {
    for(const char* count : {"1", "2", "4"}) {
        setWorkers(count);
        const size_t size = 32;
        std::vector<std::atomic<int>> seen(size);
        for(auto& entry : seen) entry.store(0);
        std::vector<std::function<void()>> tasks;
        for(size_t i = 0; i < size; ++i) tasks.push_back([&seen, i] { ++seen[i]; });
        PathWorkerPool::instance().runBatch(tasks);
        for(size_t i = 0; i < size; ++i) REQUIRE(seen[i].load() == 1);
    }
    PathWorkerPool::instance().shutdown();
    setWorkers(nullptr);
}

TEST_CASE("PathWorkerPool: a failing task does not abandon the batch", "[pathfinding][workers]") {
    // Both modes promise the same thing: the whole batch is collected, and only
    // then is the first failure propagated. Otherwise the caller's fixed-order
    // application pass would read slots that were never advanced.
    for(const char* count : {"1", "2", "4"}) {
        setWorkers(count);
        std::atomic<int> completed{0};
        std::vector<std::function<void()>> tasks;
        for(size_t i = 0; i < 8; ++i) {
            if(i == 2 || i == 5) {
                tasks.push_back([] { throw std::runtime_error("task failed"); });
            } else {
                tasks.push_back([&completed] { ++completed; });
            }
        }
        REQUIRE_THROWS_AS(PathWorkerPool::instance().runBatch(tasks), std::runtime_error);
        REQUIRE(completed.load() == 6);

        // The pool stays usable: nothing was left outstanding by the failure.
        std::atomic<int> ran{0};
        const auto next = countingBatch(ran, 4);
        PathWorkerPool::instance().runBatch(next);
        REQUIRE(ran.load() == 4);
    }
    PathWorkerPool::instance().shutdown();
    setWorkers(nullptr);
}

TEST_CASE("PathWorkerPool: repeated shutdown and empty batches are safe", "[pathfinding][workers]") {
    setWorkers("2");
    std::atomic<int> ran{0};
    const auto tasks = countingBatch(ran, 2);
    PathWorkerPool::instance().runBatch(tasks);
    REQUIRE(ran.load() == 2);
    PathWorkerPool::instance().shutdown();
    PathWorkerPool::instance().shutdown();
    const std::vector<std::function<void()>> none;
    PathWorkerPool::instance().runBatch(none);
    setWorkers(nullptr);
    PathWorkerPool::instance().shutdown();
}
