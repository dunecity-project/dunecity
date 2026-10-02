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

#include <PathWorkerPool.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <string>

// Browser builds without pthreads run the identical serial algorithm: the pool
// simply never starts a thread, so there is no new isolation or hosting
// requirement for the web target.
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#define DUNECITY_PATH_THREADS_AVAILABLE 1
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>
#else
#define DUNECITY_PATH_THREADS_AVAILABLE 0
#endif

namespace {

constexpr size_t kMaxWorkers = 8;

size_t readEnvCount(const char* name, size_t fallback, size_t cap) {
    const char* value = std::getenv(name);
    if(value == nullptr || *value == '\0') {
        return fallback;
    }
    try {
        const long parsed = std::stol(value);
        if(parsed <= 0) return fallback;
        return std::min(static_cast<size_t>(parsed), cap);
    } catch(const std::exception&) {
        return fallback;
    }
}

size_t configuredWorkers() {
    // Four independent slices measurably reduce simulation time on Apple silicon.
    // Cap the default at four and use fewer threads on smaller/unknown machines.
#if DUNECITY_PATH_THREADS_AVAILABLE
    const size_t defaultCount = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
#else
    const size_t defaultCount = 1;
#endif
    return readEnvCount("DUNECITY_PATH_WORKERS", defaultCount, kMaxWorkers);
}

unsigned long configuredDelayMicroseconds() {
    return static_cast<unsigned long>(readEnvCount("DUNECITY_PATH_WORKER_DELAY_US", 0, 100000));
}

// Derived from the task index alone, never from a clock, so a delayed run is
// still reproducible and still cannot leak timing into a decision.
void applyTestDelay(size_t index, unsigned long window) {
#if DUNECITY_PATH_THREADS_AVAILABLE
    if(window == 0) return;
    const unsigned long jitter = static_cast<unsigned long>((index * 7919u + 13u) % 7u);
    std::this_thread::sleep_for(std::chrono::microseconds(window * (1 + jitter)));
#else
    (void)index; (void)window;
#endif
}

} // namespace

#if DUNECITY_PATH_THREADS_AVAILABLE
struct PathWorkerPool::Impl {
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable workReady;
    std::condition_variable batchDone;
    const std::vector<std::function<void()>>* batch = nullptr;
    size_t nextIndex = 0;
    size_t outstanding = 0;
    unsigned long delayWindow = 0;
    bool stopping = false;
    std::exception_ptr firstError;

    void workerLoop() {
        for(;;) {
            const std::vector<std::function<void()>>* tasks = nullptr;
            size_t index = 0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                workReady.wait(lock, [this] { return stopping || (batch != nullptr && nextIndex < batch->size()); });
                if(stopping) return;
                tasks = batch;
                index = nextIndex++;
            }

            applyTestDelay(index, delayWindow);
            std::exception_ptr failure;
            try {
                (*tasks)[index]();
            } catch(...) {
                failure = std::current_exception();
            }

            {
                std::unique_lock<std::mutex> lock(mutex);
                // Kept so the batch can still be collected in full before the
                // exception is propagated; a worker never aborts its siblings.
                if(failure && !firstError) firstError = failure;
                if(--outstanding == 0) batchDone.notify_all();
            }
        }
    }
};
#else
struct PathWorkerPool::Impl {};
#endif

PathWorkerPool& PathWorkerPool::instance() {
    static PathWorkerPool pool;
    return pool;
}

PathWorkerPool::~PathWorkerPool() {
    shutdown();
}

size_t PathWorkerPool::workerCount() {
#if DUNECITY_PATH_THREADS_AVAILABLE
    ensureStarted();
    if(impl == nullptr) return 1;
    return impl->threads.empty() ? 1 : impl->threads.size();
#else
    return 1;
#endif
}

void PathWorkerPool::runInline(const std::vector<std::function<void()>>& tasks) {
    // Same contract as the threaded path: every task in the batch runs, and only
    // then is the first failure propagated. A half-collected batch would leave the
    // caller's fixed-order application pass reading stale slots.
    std::exception_ptr failure;
    for(size_t i = 0; i < tasks.size(); ++i) {
        try {
            tasks[i]();
        } catch(...) {
            if(!failure) failure = std::current_exception();
        }
    }
    if(failure) std::rethrow_exception(failure);
}

#if DUNECITY_PATH_THREADS_AVAILABLE
void PathWorkerPool::ensureStarted() {
    const size_t wanted = configuredWorkers();
    if(wanted <= 1) {
        // Configuring one worker really does mean inline: a pool left over from an
        // earlier setting in the same process is stopped, not quietly reused.
        shutdown();
        failedCount = 0;
        return;
    }
    if(impl != nullptr && impl->threads.size() == wanted) {
        return;
    }
    if(failedCount == wanted) {
        return;   // Already tried and failed for this count; do not retry per batch.
    }
    shutdown();
    impl = new Impl();
    impl->delayWindow = configuredDelayMicroseconds();
    try {
        for(size_t i = 0; i < wanted; ++i) {
            impl->threads.emplace_back([this] { impl->workerLoop(); });
        }
    } catch(...) {
        // Any startup failure, not just std::system_error: join whatever did start
        // so nothing joinable leaks, then fall back to inline for this count.
        shutdown();
        failedCount = wanted;
    }
}

void PathWorkerPool::runBatch(const std::vector<std::function<void()>>& tasks) {
    if(tasks.empty()) {
        return;
    }
    ensureStarted();
    if(impl == nullptr || impl->threads.empty()) {
        runInline(tasks);
        return;
    }

    std::exception_ptr failure;
    {
        std::unique_lock<std::mutex> lock(impl->mutex);
        impl->batch = &tasks;
        impl->nextIndex = 0;
        impl->outstanding = tasks.size();
        impl->firstError = nullptr;
        impl->workReady.notify_all();
        impl->batchDone.wait(lock, [this] { return impl->outstanding == 0; });
        impl->batch = nullptr;
        failure = impl->firstError;
        impl->firstError = nullptr;
    }
    if(failure) {
        std::rethrow_exception(failure);
    }
}

void PathWorkerPool::shutdown() {
    if(impl == nullptr) {
        return;
    }
    {
        std::unique_lock<std::mutex> lock(impl->mutex);
        impl->stopping = true;
        impl->workReady.notify_all();
    }
    for(std::thread& worker : impl->threads) {
        if(worker.joinable()) worker.join();
    }
    delete impl;
    impl = nullptr;
}
#else
void PathWorkerPool::ensureStarted() { }

void PathWorkerPool::runBatch(const std::vector<std::function<void()>>& tasks) {
    runInline(tasks);
}

void PathWorkerPool::shutdown() { }
#endif
