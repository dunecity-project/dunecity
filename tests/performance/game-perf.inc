// Included only by campaign-balance-probe.inc (run-campaign-balance.py), never by the shipped game.
//
// Game frame-cost measurement (docs/game-performance.md, tests/performance/game-perf.py).
// Plays the loaded match cycle by cycle exactly as the probe's ordinary loop does and records,
// for every cycle: the whole updateGameState() wall time, every house's own update time (the
// engine's "house.update" timing, read back from the AI decision log's in-memory counters), and
// the engine's city simulation, unit, structure and path-search timers. Optionally it renders
// a frame every BALANCE_PERF_RENDER_EVERY cycles through Game::renderFrame() (the dummy video
// driver's software renderer, so it tracks render work, not GPU speed).
//
// The simulation is not touched: only clocks and counters are read, so a perf run plays the
// same game as the ordinary probe loop (same PROBE_DIGEST lines).
//
// Kept to APIs that exist since 1.0.833 so the same file measures older releases
// (game-perf.py --source copies it into a throwaway tag worktree).
#include <players/AIDecisionLog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace GamePerf {

using Clock = std::chrono::steady_clock;

inline double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// This thread's CPU time in ms: unlike wall time it does not grow while the thread waits for a
/// core on a loaded machine (it still grows on a slower core).
inline double threadCpuMs() {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return now.tv_sec * 1000.0 + now.tv_nsec / 1.0e6;
}

/// A fixed reference workload (integer hashing over a 4 MiB table: arithmetic plus cache misses,
/// like the simulation), timed in thread CPU ms; the fastest of five runs. Ratios of the game's
/// timings to it travel between machines and loads far better than raw milliseconds.
inline double calibrationMs() {
    std::vector<uint32_t> table(1 << 20);
    double best = 1e9;
    for (int run = 0; run < 5; ++run) {
        const double start = threadCpuMs();
        uint32_t state = 2463534242u, sum = 0;
        for (uint32_t i = 0; i < (1u << 22); ++i) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            uint32_t& slot = table[state & (table.size() - 1)];
            slot += state;
            sum += slot >> 3;
        }
        volatile uint32_t sink = sum;
        (void)sink;
        best = std::min(best, threadCpuMs() - start);
    }
    return best;
}

/// Microseconds accumulated so far by one telemetry timing (scope, house), 0 if none yet.
inline long long telemetrySum(const std::string& scope, int house) {
    auto& metrics = AITelemetry::log().performanceMetrics;   // test-only: -fno-access-control
    const auto found = metrics.find(std::make_tuple(scope, house, -1, true));
    return found == metrics.end() ? 0 : static_cast<long long>(found->second.sum);
}

struct Cycle {
    float sim = 0, cpu = 0, ai = 0, city = 0, units = 0, structures = 0, path = 0, render = -1;
    float house[NUM_HOUSES] = {};
    int unitCount = 0;
};

inline double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = std::min(values.size() - 1, static_cast<size_t>(p * (values.size() - 1) + 0.5));
    return values[index];
}

inline std::string stats(const std::vector<double>& values) {
    double sum = 0, peak = 0;
    for (double v : values) { sum += v; peak = std::max(peak, v); }
    char text[200];
    std::snprintf(text, sizeof(text), "{\"mean\":%.3f,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f}",
                  values.empty() ? 0.0 : sum / values.size(), percentile(values, 0.50), percentile(values, 0.95),
                  percentile(values, 0.99), peak);
    return text;
}

/// The interactive game itself: Game::runMainLoop() with the real renderer (or the dummy
/// driver's), the configured game speed and the engine's own frame pacing, until @a cycles more
/// cycles have run. The engine's own per-frame figures are read back afterwards (frame count,
/// cumulative AI/unit/structure/path/render time); game-perf.py adds the decision log's
/// five-second performance windows (frame and per-house timings, frames over 33/100 ms).
inline void runMainLoop(Game& game, Uint32 cycles) {
    static Uint32 stopAt = 0;
    // The interactive game paces cycles by the player's game speed (settings, not the match):
    // Stefan plays at 2 ms, where the engine runs up to 10 cycles per displayed frame.
    settings.gameOptions.gameSpeed = std::getenv("BALANCE_PERF_GAME_SPEED") ? std::atoi(std::getenv("BALANCE_PERF_GAME_SPEED")) : 2;
    stopAt = game.getGameCycleCount() + cycles;
    // The engine has no cycle limit; a timer ends the loop (bQuitGame is only read by the loop).
    const SDL_TimerID timer = SDL_AddTimer(20, [](Uint32 interval, void*) -> Uint32 {
        if (currentGame != nullptr && currentGame->getGameCycleCount() >= stopAt) currentGame->bQuitGame = true;
        return interval;
    }, nullptr);
    const double calibrationBefore = calibrationMs();
    const auto start = Clock::now();
    const double cpuStart = threadCpuMs();
    game.runMainLoop();
    const double wallMs = msSince(start), cpuMs = threadCpuMs() - cpuStart;
    SDL_RemoveTimer(timer);
    const auto& t = game.frameTiming;
    const double frames = std::max(1, t.frameCount);
    std::printf("GAME_PERF_MAINLOOP: {\"cycles\":%d,\"frames\":%d,\"wall_ms\":%.0f,\"main_thread_cpu_ms\":%.0f,"
                "\"fps\":%.2f,\"cycles_per_frame\":%.2f,\"game_speed_ms\":%d,\"frame_ms_mean\":%.2f,\"frame_ms_max\":%.1f,"
                "\"ai_ms_per_frame\":%.2f,\"units_ms_per_frame\":%.2f,\"structures_ms_per_frame\":%.2f,"
                "\"path_ms_per_frame\":%.2f,\"render_ms_per_frame\":%.2f,\"render_ms_max\":%.1f,\"calibration_ms\":%.2f,"
                "\"units_end\":%zu}\n",
                t.totalGameCycles, t.frameCount, wallMs, cpuMs, t.frameCount * 1000.0 / std::max(1.0, t.totalMs),
                t.totalGameCycles / frames, game.getGameSpeed(), t.totalMs / frames, t.maxTotalMs, t.aiMs / frames,
                t.unitsMs / frames, t.structuresMs / frames, t.pathfindingMs / frames, t.renderingMs / frames,
                t.maxRenderingMs, calibrationBefore, unitList.size());
    std::fflush(stdout);
}

/// Plays @a cycles game cycles (or to the end of the match) and prints one GAME_PERF_RESULT line.
/// @a perCycle runs before every cycle (the probe's digest hook); it is not timed.
inline void run(Game& game, Uint32 cycles, const std::function<void()>& perCycle) {
    if (std::getenv("BALANCE_PERF_MAINLOOP")) { runMainLoop(game, cycles); return; }
    const int renderEvery = std::getenv("BALANCE_PERF_RENDER_EVERY") ? std::atoi(std::getenv("BALANCE_PERF_RENDER_EVERY")) : 0;
    // Cycles per displayed frame for the frame model: Stefan plays at game speed 2 ms, where the
    // engine's guardrail runs 10 cycles per frame (Game::runMainLoop kMaxCyclesPerFrame).
    const int frameCycles = std::max(1, std::getenv("BALANCE_PERF_FRAME_CYCLES") ? std::atoi(std::getenv("BALANCE_PERF_FRAME_CYCLES")) : 10);
    const char* csvPath = std::getenv("BALANCE_PERF_CSV");
    if (!AITelemetry::log().enabled())
        SDL_Log("GAME_PERF_WARNING: AI decision log is off, so per-house timings are zero");
    std::vector<int> houses;
    for (int h = 0; h < NUM_HOUSES; ++h) if (game.getHouse(h) != nullptr) houses.push_back(h);
    std::vector<Cycle> samples;
    samples.reserve(cycles);
    long long before[NUM_HOUSES] = {};
    auto& timing = game.frameTiming;
    const Uint32 first = game.getGameCycleCount();
    const auto loopStart = Clock::now();
    while (!game.finished && game.getGameCycleCount() - first < cycles) {
        if (perCycle) perCycle();
        for (int h : houses) before[h] = telemetrySum("house.update", h);
        timing.aiMsThisFrame = timing.citySimMsThisFrame = timing.unitsMsThisFrame = 0.0;
        timing.structuresMsThisFrame = timing.pathfindingMsThisFrame = timing.renderingMsThisFrame = 0.0;
        const auto start = Clock::now();
        const double cpuStart = threadCpuMs();
        game.updateGameState();
        Cycle sample;
        sample.cpu = static_cast<float>(threadCpuMs() - cpuStart);
        sample.sim = static_cast<float>(msSince(start));
        sample.ai = static_cast<float>(timing.aiMsThisFrame);
        sample.city = static_cast<float>(timing.citySimMsThisFrame);
        sample.units = static_cast<float>(timing.unitsMsThisFrame);
        sample.structures = static_cast<float>(timing.structuresMsThisFrame);
        sample.path = static_cast<float>(timing.pathfindingMsThisFrame);
        for (int h : houses) sample.house[h] = static_cast<float>((telemetrySum("house.update", h) - before[h]) / 1000.0);
        sample.unitCount = static_cast<int>(unitList.size());
        if (renderEvery > 0 && (game.getGameCycleCount() - first) % renderEvery == 0) {
            const auto renderStart = Clock::now();
            game.renderFrame();
            sample.render = static_cast<float>(msSince(renderStart));
        }
        samples.push_back(sample);
    }
    const double loopMs = msSince(loopStart);
    const double calibration = calibrationMs();

    std::vector<double> sim, cpu, ai, city, units, structures, path, render, frames;
    double cpuTotal = 0;
    for (const Cycle& s : samples) {
        sim.push_back(s.sim); cpu.push_back(s.cpu); ai.push_back(s.ai); city.push_back(s.city); units.push_back(s.units);
        structures.push_back(s.structures); path.push_back(s.path);
        if (s.render >= 0) render.push_back(s.render);
        cpuTotal += s.cpu;
    }
    double renderMean = 0;
    for (double r : render) renderMean += r;
    renderMean = render.empty() ? 0.0 : renderMean / render.size();
    // A displayed frame = frameCycles consecutive cycles of simulation (+ the mean render time
    // when rendering was measured), as the game's main loop runs them at 2 ms game speed.
    int over33 = 0, over50 = 0, over100 = 0;
    for (size_t i = 0; i + frameCycles <= samples.size(); i += frameCycles) {
        double frame = renderMean;
        for (int k = 0; k < frameCycles; ++k) frame += samples[i + k].sim;
        frames.push_back(frame);
        over33 += frame > 33.3; over50 += frame > 50.0; over100 += frame > 100.0;
    }
    double frameMean = 0;
    for (double f : frames) frameMean += f;
    frameMean = frames.empty() ? 0.0 : frameMean / frames.size();

    std::string houseJson;
    for (int h : houses) {
        std::vector<double> values;
        values.reserve(samples.size());
        for (const Cycle& s : samples) values.push_back(s.house[h]);
        houseJson += (houseJson.empty() ? "" : ",") + std::string("\"") + std::to_string(h) + "\":" + stats(values);
    }
    // Inclusive AI scope totals (ms over the whole run), biggest first: where the AI time went.
    std::vector<std::pair<long long, std::string>> scopes;
    for (const auto& entry : AITelemetry::log().performanceMetrics) {
        const auto& [scope, house, item, duration] = entry.first;
        if (!duration || item != -1 || house < 0 || scope.rfind("ai.", 0) != 0) continue;
        scopes.emplace_back(entry.second.sum, scope + "/h" + std::to_string(house));
    }
    std::sort(scopes.rbegin(), scopes.rend());
    std::string scopeJson;
    for (size_t i = 0; i < scopes.size() && i < 25; ++i)
        scopeJson += (scopeJson.empty() ? "" : ",") + std::string("\"") + scopes[i].second + "\":" + std::to_string(scopes[i].first / 1000);
    int unitsEnd = samples.empty() ? 0 : samples.back().unitCount;
    std::printf("GAME_PERF_RESULT: {\"first_cycle\":%u,\"cycles\":%zu,\"loop_ms\":%.0f,\"units_end\":%d,"
                "\"frame_cycles\":%d,\"sim\":%s,\"cpu\":%s,\"cpu_total_ms\":%.0f,\"calibration_ms\":%.3f,"
                "\"ai\":%s,\"city\":%s,\"units\":%s,\"structures\":%s,\"path\":%s,"
                "\"render\":%s,\"frame\":%s,\"frames_over_33ms\":%d,\"frames_over_50ms\":%d,\"frames_over_100ms\":%d,"
                "\"frames\":%zu,\"est_fps\":%.2f,\"house\":{%s},\"ai_scopes_ms\":{%s}}\n",
                first, samples.size(), loopMs, unitsEnd, frameCycles, stats(sim).c_str(), stats(cpu).c_str(), cpuTotal,
                calibration, stats(ai).c_str(),
                stats(city).c_str(), stats(units).c_str(), stats(structures).c_str(), stats(path).c_str(),
                stats(render).c_str(), stats(frames).c_str(), over33, over50, over100, frames.size(),
                frameMean > 0 ? 1000.0 / frameMean : 0.0, houseJson.c_str(), scopeJson.c_str());
    std::fflush(stdout);
    if (csvPath != nullptr && csvPath[0] != '\0') {
        std::ofstream csv(csvPath);
        csv << "cycle,sim,ai,city,units,structures,path,render,unit_count";
        for (int h : houses) csv << ",house" << h;
        csv << '\n';
        for (size_t i = 0; i < samples.size(); ++i) {
            const Cycle& s = samples[i];
            csv << first + i << ',' << s.sim << ',' << s.ai << ',' << s.city << ',' << s.units << ','
                << s.structures << ',' << s.path << ',' << s.render << ',' << s.unitCount;
            for (int h : houses) csv << ',' << s.house[h];
            csv << '\n';
        }
    }
}

} // namespace GamePerf
