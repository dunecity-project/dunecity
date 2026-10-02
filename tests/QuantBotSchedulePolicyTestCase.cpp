/*
 *  QuantBotSchedulePolicyTestCase.cpp
 *
 *  Pins the AI update schedule. These call the production predicates in
 *  include/players/QuantBotSchedulePolicy.h, the same ones QuantBot::update()
 *  runs, so a divergence here is a divergence in the game.
 *
 *  Two contracts:
 *    - every game type other than CustomGame reproduces the original gate,
 *      (cycle + houseID) % 50 == 0, exactly;
 *    - CustomGame splits that pass into two phases at the same 50-cycle cadence,
 *      derived purely from (cycle, houseID), with no stored state.
 */

#include <catch2/catch_all.hpp>
#include <players/QuantBotSchedulePolicy.h>

#include <algorithm>
#include <set>
#include <vector>

using namespace QuantBotSchedulePolicy;

namespace {
constexpr int kInterval = 50;      // AIUPDATEINTERVAL
constexpr int kSlots    = static_cast<int>(NUM_HOUSES);
constexpr uint32_t kCycles = 1200; // > 1000 cycles, 24 full intervals

// The original predicate, written out independently of the header.
bool originalGate(uint32_t cycle, int houseID) {
    return ((cycle + static_cast<uint32_t>(houseID)) % 50u) == 0u;
}

std::vector<uint32_t> dueCycles(bool (*predicate)(uint32_t,int,int,int),
                                int houseID, uint32_t from, uint32_t to) {
    std::vector<uint32_t> out;
    for (uint32_t c = from; c < to; ++c)
        if (predicate(c, houseID, kInterval, kSlots)) out.push_back(c);
    return out;
}

bool exactCadence(const std::vector<uint32_t>& cycles, int interval) {
    if (cycles.size() < 2) return false;
    for (size_t i = 1; i < cycles.size(); ++i)
        if (cycles[i] - cycles[i-1] != static_cast<uint32_t>(interval)) return false;
    return true;
}
} // namespace

TEST_CASE("Schedule: only CustomGame is phased", "[ai-schedule][gate]") {
    REQUIRE(phasedSchedule(GameType::CustomGame));
    // Every other named enumerator keeps the original timing.
    REQUIRE_FALSE(phasedSchedule(GameType::Invalid));
    REQUIRE_FALSE(phasedSchedule(GameType::LoadSavegame));
    REQUIRE_FALSE(phasedSchedule(GameType::Campaign));
    REQUIRE_FALSE(phasedSchedule(GameType::Skirmish));
    REQUIRE_FALSE(phasedSchedule(GameType::CustomMultiplayer));
    REQUIRE_FALSE(phasedSchedule(GameType::LoadMultiplayer));
    REQUIRE_FALSE(phasedSchedule(GameType::CampaignCoop));
    REQUIRE_FALSE(phasedSchedule(GameType::SkirmishCoop));
    REQUIRE_FALSE(phasedSchedule(GameType::LoadCoop));
    // Nothing that is a network type may ever be phased.
    for (int raw = -1; raw <= 8; ++raw) {
        const auto type = static_cast<GameType>(raw);
        if (isNetworkGameType(type)) REQUIRE_FALSE(phasedSchedule(type));
    }
}

TEST_CASE("Schedule: legacy modes reproduce the original gate", "[ai-schedule][legacy]") {
    for (int houseID = 0; houseID < kSlots; ++houseID)
        for (uint32_t cycle = 0; cycle < kCycles; ++cycle)
            REQUIRE(legacyDue(cycle, houseID, kInterval) == originalGate(cycle, houseID));
}

TEST_CASE("Schedule: a custom save loaded into a network session stays legacy", "[ai-schedule][gate]") {
    // The world type comes from the save; the launch type selects the session.
    for (GameType launch : {GameType::CustomMultiplayer, GameType::LoadMultiplayer,
                            GameType::CampaignCoop, GameType::SkirmishCoop, GameType::LoadCoop}) {
        REQUIRE_FALSE(phasedSchedule(GameType::CustomGame, launch));
    }
    REQUIRE(phasedSchedule(GameType::CustomGame, GameType::CustomGame));
    REQUIRE(phasedSchedule(GameType::CustomGame, GameType::LoadSavegame));
}

TEST_CASE("Schedule: each phase keeps the exact 50-cycle cadence", "[ai-schedule][cadence]") {
    for (int houseID = 0; houseID < kSlots; ++houseID) {
        const auto units  = dueCycles(unitPhaseDue,  houseID, 0, kCycles);
        const auto builds = dueCycles(buildPhaseDue, houseID, 0, kCycles);
        // 1200 cycles / 50 = 24 firings per phase, evenly spaced.
        REQUIRE(units.size()  == kCycles / kInterval);
        REQUIRE(builds.size() == kCycles / kInterval);
        REQUIRE(exactCadence(units,  kInterval));
        REQUIRE(exactCadence(builds, kInterval));
        // The legacy gate fires the same number of times, so no timer action is
        // missed or duplicated across the interval.
        const auto legacy = [&] {
            std::vector<uint32_t> out;
            for (uint32_t c = 0; c < kCycles; ++c) if (legacyDue(c, houseID, kInterval)) out.push_back(c);
            return out;
        }();
        REQUIRE(legacy.size() == units.size());
    }
}

TEST_CASE("Schedule: unit and build never coincide within a house", "[ai-schedule][cadence]") {
    for (int houseID = 0; houseID < kSlots; ++houseID) {
        REQUIRE(unitPhase(houseID, kInterval, kSlots) != buildPhase(houseID, kInterval, kSlots));
        // Half an interval apart, in both directions around the ring.
        const int gap = (buildPhase(houseID, kInterval, kSlots)
                         - unitPhase(houseID, kInterval, kSlots) + kInterval) % kInterval;
        REQUIRE(gap == kInterval / 2);
        for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
            const bool both = unitPhaseDue(cycle, houseID, kInterval, kSlots)
                           && buildPhaseDue(cycle, houseID, kInterval, kSlots);
            REQUIRE_FALSE(both);
        }
    }
}

TEST_CASE("Schedule: houses 0 and 4 are no longer 4 cycles apart", "[ai-schedule][spread]") {
    // The measured profile is dominated by houses 0 and 4. Under the original
    // gate they land 4 cycles apart, so both stalls hit nearly the same frame.
    const int oldGap = std::min((0 - 4 + kInterval) % kInterval, (4 - 0 + kInterval) % kInterval);
    REQUIRE(oldGap == 4);

    const int p0 = unitPhase(0, kInterval, kSlots);
    const int p4 = unitPhase(4, kInterval, kSlots);
    const int newGap = std::min((p0 - p4 + kInterval) % kInterval, (p4 - p0 + kInterval) % kInterval);
    REQUIRE(newGap > oldGap);
    REQUIRE(newGap == 16);

    // Every distinct pair of house slots is at least an even share apart.
    for (int a = 0; a < kSlots; ++a)
        for (int b = a + 1; b < kSlots; ++b) {
            const int pa = unitPhase(a, kInterval, kSlots), pb = unitPhase(b, kInterval, kSlots);
            REQUIRE(pa != pb);
            const int gap = std::min((pa - pb + kInterval) % kInterval, (pb - pa + kInterval) % kInterval);
            REQUIRE(gap >= kInterval / kSlots);
        }
}

TEST_CASE("Schedule: phases are stateless across frame chunking", "[ai-schedule][stateless]") {
    // The main loop runs a variable number of cycles per rendered frame. Because
    // the predicates read only (cycle, houseID), any chunking must produce the
    // same due-cycle list. 1, 4 and 7 cycles per frame are all exercised.
    for (int houseID = 0; houseID < kSlots; ++houseID) {
        const auto reference = dueCycles(unitPhaseDue, houseID, 0, kCycles);
        const auto refBuild  = dueCycles(buildPhaseDue, houseID, 0, kCycles);
        for (uint32_t chunk : {1u, 4u, 7u}) {
            std::vector<uint32_t> units, builds;
            for (uint32_t start = 0; start < kCycles; start += chunk)
                for (uint32_t c = start; c < std::min(start + chunk, kCycles); ++c) {
                    if (unitPhaseDue(c, houseID, kInterval, kSlots)) units.push_back(c);
                    if (buildPhaseDue(c, houseID, kInterval, kSlots)) builds.push_back(c);
                }
            REQUIRE(units == reference);
            REQUIRE(builds == refBuild);
        }
    }
}

TEST_CASE("Schedule: a saved buildTimer keeps its cadence, never doubled", "[ai-schedule][timer]") {
    // The build phase owns the WHOLE original timer block. Replaying the block
    // once per build phase must consume a saved timer at AIUPDATEINTERVAL per
    // interval and fire build() exactly when it reaches zero — the same number of
    // productions the legacy combined pass would have run over the same cycles.
    for (int savedTimer : {0, 50, 100}) {
        for (int houseID = 0; houseID < kSlots; ++houseID) {
            int phasedTimer = savedTimer, legacyTimer = savedTimer;
            int phasedBuilds = 0, legacyBuilds = 0;
            for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
                if (buildPhaseDue(cycle, houseID, kInterval, kSlots)) {
                    if (phasedTimer <= 0) {
                        ++phasedBuilds;
                        phasedTimer = 5 + houseID % 10; // build() resets this timer.
                    } else phasedTimer -= kInterval;
                }
                if (legacyDue(cycle, houseID, kInterval)) {
                    if (legacyTimer <= 0) {
                        ++legacyBuilds;
                        legacyTimer = 5 + houseID % 10;
                    } else legacyTimer -= kInterval;
                }
            }
            // Same production count: not doubled, not halved, not skipped.
            REQUIRE(phasedBuilds == legacyBuilds);
            REQUIRE(phasedTimer == legacyTimer);
            // Positive reset values make production fire every other interval.
            REQUIRE(phasedBuilds == (static_cast<int>(kCycles / kInterval) - savedTimer / kInterval + 1) / 2);
        }
    }
}

TEST_CASE("Schedule: degenerate inputs do not divide by zero", "[ai-schedule][guard]") {
    REQUIRE(unitPhase(3, 0, kSlots) == 0);
    REQUIRE(unitPhase(3, kInterval, 0) == 0);
    REQUIRE(buildPhase(3, 0, kSlots) == 0);
    REQUIRE_FALSE(legacyDue(0, 0, 0));
    REQUIRE_FALSE(unitPhaseDue(0, 0, 0, kSlots));
    REQUIRE_FALSE(buildPhaseDue(0, 0, 0, kSlots));
    // Negative house ids fold into a valid slot rather than indexing backwards.
    REQUIRE(unitPhase(-1, kInterval, kSlots) == unitPhase(kSlots - 1, kInterval, kSlots));
}
