#include <catch2/catch_test_macros.hpp>

#include <FramePacing.h>

#include <algorithm>
#include <vector>

// Game::runMainLoop's catch-up loop, reduced to its pacing: per frame, cycles run while game time
// is owed, at most ten, and (single player) while FramePacing allows; the frame's own duration
// then becomes owed time, capped as the loop caps it. Cycle k costs costs[k] ms.
namespace {
struct Frames { std::vector<int> cycles; std::vector<double> catchUp; };

Frames play(const std::vector<double>& costs, bool network, double renderMs, int speedMs) {
    Frames frames;
    size_t next = 0;
    double owed = 0;
    while (next < costs.size()) {
        int ran = 0;
        double spent = 0;
        while (owed > speedMs && ran < 10 && next < costs.size()
               && FramePacing::mayRunAnotherCycle(ran, spent, network)) {
            spent += costs[next++];
            owed -= speedMs;
            ++ran;
        }
        frames.cycles.push_back(ran);
        frames.catchUp.push_back(spent);
        owed = std::min(owed + renderMs + spent, std::max(speedMs * 3.0, 24.0));
    }
    return frames;
}
} // namespace

TEST_CASE("Frame pacing bounds a single-player frame's catch-up and keeps every cycle", "[frame_pacing]") {
    // Stefan's 8-house save at 2 ms on his Air in Low Power Mode: 3 ms cycles, every 50th a
    // 25 ms AI pass.
    std::vector<double> costs;
    for (int k = 0; k < 5000; ++k) costs.push_back(k % 50 == 0 ? 25.0 : 3.0);
    const Frames single = play(costs, false, 6.0, 2);
    const Frames network = play(costs, true, 6.0, 2);
    long total = 0;
    double worstSingle = 0, worstNetwork = 0;
    for (size_t f = 0; f < single.cycles.size(); ++f) {
        total += single.cycles[f];
        REQUIRE(single.cycles[f] <= 10);
        worstSingle = std::max(worstSingle, single.catchUp[f]);
    }
    for (double spent : network.catchUp) worstNetwork = std::max(worstNetwork, spent);
    // Every cycle runs, in order, whatever the pacing: only the cycles per frame differ.
    REQUIRE(total == static_cast<long>(costs.size()));
    // A frame stops catching up once 16 ms are used: at most the budget plus one cycle.
    REQUIRE(worstSingle < FramePacing::kCatchUpBudgetMs + 25.0);
    // Network games keep the ten-cycle cap alone: fewer, longer frames for the same cycles.
    REQUIRE(worstNetwork >= worstSingle);
    REQUIRE(network.cycles.size() < single.cycles.size());
}

TEST_CASE("Frame pacing always runs a frame's first due cycle", "[frame_pacing]") {
    REQUIRE(FramePacing::mayRunAnotherCycle(0, 1000.0, false));
    REQUIRE(FramePacing::mayRunAnotherCycle(3, FramePacing::kCatchUpBudgetMs - 0.1, false));
    REQUIRE_FALSE(FramePacing::mayRunAnotherCycle(3, FramePacing::kCatchUpBudgetMs, false));
    REQUIRE(FramePacing::mayRunAnotherCycle(9, 1000.0, true));
}
