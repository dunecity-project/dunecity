#include <catch2/catch_test_macros.hpp>

#include <players/BlobWavePolicy.h>
#include <players/QuantBotConfig.h>

#include <algorithm>
#include <cstdlib>

// QuantBot hunt parties (docs/formations.md, "Blob waves"): the pure decisions behind gather,
// the leg march, release, reform and the tactical fall-back. The real-engine behaviour is
// covered by the quantbot_blob_waves probe (tests/ai/blob-wave-probe.inc).

using namespace BlobWavePolicy;

namespace {
constexpr uint32_t kCyclesPerSecond = 62;   // MILLI2CYCLES(1000) at 16 ms per cycle
uint32_t seconds(int s) { return uint32_t(s) * kCyclesPerSecond; }
int chebyshev(std::pair<int, int> v) { return std::max(std::abs(v.first), std::abs(v.second)); }
}

TEST_CASE("Blob waves: the blob radius and the leg grow with the party", "[ai][blob]") {
    REQUIRE(blobRadius(1) == 2);
    REQUIRE(blobRadius(8) == 2);
    REQUIRE(blobRadius(9) == 3);
    REQUIRE(blobRadius(72) == 6);
    // Two members per squared tile always fit.
    for (int members = 1; members <= 400; ++members) {
        const int r = blobRadius(members);
        REQUIRE(r * r * 2 >= members);
        REQUIRE(legTiles(members) >= 8);
        REQUIRE(legTiles(members) >= 2 * r);
    }
}

TEST_CASE("Blob waves: a gather waits for the party, with a stall rule and a timeout", "[ai][blob]") {
    const uint32_t maxCycles = seconds(kGatherMaxMs / 1000), stall = seconds(kGatherStallMs / 1000);
    // All members collected: done at once.
    REQUIRE(gatherComplete(20, 20, 0, 0, maxCycles, stall, true));
    // 90% is the quorum.
    REQUIRE(gatherComplete(20, 18, seconds(1), 0, maxCycles, stall, true));
    REQUIRE_FALSE(gatherComplete(20, 17, seconds(1), 0, maxCycles, stall, true));
    // Arrivals stopped, the rest stand still (blocked) and at least 60% there: go without them.
    REQUIRE_FALSE(gatherComplete(20, 12, seconds(10), seconds(7), maxCycles, stall, false));
    REQUIRE(gatherComplete(20, 12, seconds(10), stall, maxCycles, stall, false));
    REQUIRE_FALSE(gatherComplete(20, 11, seconds(10), stall, maxCycles, stall, false));
    // A slow member still walking in is waited for (until the timeout).
    REQUIRE_FALSE(gatherComplete(20, 12, seconds(10), stall, maxCycles, stall, true));
    // The timeout: a straggler or a blocked member never stalls the offensive.
    REQUIRE(gatherComplete(20, 0, maxCycles, 0, maxCycles, stall, true));
    REQUIRE_FALSE(gatherComplete(20, 0, maxCycles - 1, 0, maxCycles, stall, true));
    REQUIRE(gatherComplete(0, 0, 0, 0, maxCycles, stall, true));
}

TEST_CASE("Blob waves: a leg waits for the slowest member, never for stragglers", "[ai][blob]") {
    // Quorum of the members that are not stragglers.
    REQUIRE(legComplete(10, 9, 0, 1000));
    REQUIRE_FALSE(legComplete(10, 8, 0, 1000));
    REQUIRE(legComplete(10, 0, 1000, 1000));
    REQUIRE(legComplete(0, 0, 0, 1000));
    // A straggler is more than two legs plus the radius from the leg's end.
    REQUIRE_FALSE(straggler(2 * 12 + 7, 12, 7));
    REQUIRE(straggler(2 * 12 + 8, 12, 7));
    // The time limit is the slowest member's travel time with slack, clamped to the phase cap.
    const int64_t soldier = 267, tank = 80;   // cycles per tile (TILESIZE / reference speed)
    const uint32_t slow = travelLimit(12 * soldier, kCyclesPerSecond, kLegMaxMs);
    const uint32_t fast = travelLimit(12 * tank, kCyclesPerSecond, kLegMaxMs);
    REQUIRE(slow > fast);
    REQUIRE(slow >= uint32_t(12 * soldier));                      // long enough for the soldier
    REQUIRE(fast >= uint32_t(kTravelMinMs) * kCyclesPerSecond / 1000);
    REQUIRE(travelLimit(0, kCyclesPerSecond, kGatherMaxMs) == uint32_t(kTravelMinMs) * kCyclesPerSecond / 1000);
    REQUIRE(travelLimit(12 * 100000, kCyclesPerSecond, kLegMaxMs) == uint32_t(kLegMaxMs) * kCyclesPerSecond / 1000);
    REQUIRE(travelLimit(12 * 100000, kCyclesPerSecond, kGatherMaxMs) == uint32_t(kGatherMaxMs) * kCyclesPerSecond / 1000);
}

TEST_CASE("Blob waves: a fast member is never more than a leg and the blob ahead of the slowest", "[ai][blob]") {
    // The march model: every leg ends where the anchor was plus one leg towards the goal; the
    // fastest member waits there, the slowest needs the whole leg. At the moment the next leg
    // starts the slowest member (not a straggler) is within two legs plus the radius of the old
    // leg end, and the fastest is at most the radius past it, so the fastest leads by at most
    // three legs plus two radii in the worst case and by about one leg in the normal case.
    for (int members : {3, 12, 40, 72, 150}) {
        const int leg = legTiles(members), r = blobRadius(members) + 1;
        const int worstLead = r + 2 * leg + r;
        REQUIRE(worstLead <= 3 * leg + 2 * r);
        // The leg end is one leg from the anchor, never past the goal.
        REQUIRE(chebyshev(legOffset(0, 0, 100, 37, leg)) == leg);
        REQUIRE(legOffset(0, 0, 3, -2, leg) == std::pair<int, int>{3, -2});
    }
}

TEST_CASE("Blob waves: the rally point lies towards the objective, a short way out", "[ai][blob]") {
    const auto offset = gatherOffset(10, 10, 90, 70);
    REQUIRE(chebyshev(offset) == kGatherAdvanceTiles);
    REQUIRE(offset.first > 0);
    REQUIRE(offset.second > 0);
    // Never more than a third of the way to a close objective.
    REQUIRE(chebyshev(gatherOffset(10, 10, 16, 10)) == 2);
    REQUIRE(gatherOffset(5, 5, 5, 5) == std::pair<int, int>{0, 0});
}

TEST_CASE("Blob waves: the tactical fall-back goes a few tiles away from the threat, towards home", "[ai][blob]") {
    // Threat to the east, home to the west: straight back west by the fall-back distance.
    auto back = fallbackOffset(50, 50, 60, 50, true, 10, 50, true, kFallbackTiles);
    REQUIRE(back == std::pair<int, int>{-kFallbackTiles, 0});
    // Threat to the east, home to the north: away from the threat counts twice.
    back = fallbackOffset(50, 50, 60, 50, true, 50, 10, true, kFallbackTiles);
    REQUIRE(chebyshev(back) == kFallbackTiles);
    REQUIRE(back.first < 0);
    REQUIRE(back.second < 0);
    REQUIRE(-back.first > -back.second);
    // Even with home behind the threat it never falls back into the threat.
    back = fallbackOffset(50, 50, 60, 50, true, 90, 50, true, kFallbackTiles);
    REQUIRE(back.first < 0);
    // No threat known: home. Nothing known: no move.
    REQUIRE(fallbackOffset(50, 50, 0, 0, false, 50, 90, true, kFallbackTiles) == std::pair<int, int>{0, kFallbackTiles});
    REQUIRE(fallbackOffset(50, 50, 0, 0, false, 0, 0, false, kFallbackTiles) == std::pair<int, int>{0, 0});
    // Done when 70% stand there or after the limit.
    REQUIRE(fallbackComplete(10, 7, 0, 100));
    REQUIRE_FALSE(fallbackComplete(10, 6, 99, 100));
    REQUIRE(fallbackComplete(10, 0, 100, 100));
}

TEST_CASE("Blob waves: local outmatch uses the posture policy's ratios", "[ai][blob]") {
    REQUIRE(outmatched(1000, 1300, kOutmatchBps));
    REQUIRE_FALSE(outmatched(1000, 1299, kOutmatchBps));
    REQUIRE(outmatched(1000, 2000, kSevereBps));
    REQUIRE_FALSE(outmatched(1000, 1999, kSevereBps));
    REQUIRE_FALSE(outmatched(1000, 0, kOutmatchBps));
    // No friends at all: any enemy of at least 1.3 power units outmatches.
    REQUIRE(outmatched(0, 2, kOutmatchBps));
    REQUIRE_FALSE(outmatched(0, 1, kOutmatchBps));
}

TEST_CASE("Blob waves: integer steps are symmetric and exact", "[ai][blob]") {
    REQUIRE(scaled(0, 0, 8) == std::pair<int, int>{0, 0});
    REQUIRE(scaled(10, 5, 8) == std::pair<int, int>{8, 4});
    REQUIRE(scaled(-10, -5, 8) == std::pair<int, int>{-8, -4});
    REQUIRE(scaled(3, -1, 8) == std::pair<int, int>{8, -3});
    for (int dx = -9; dx <= 9; ++dx)
        for (int dy = -9; dy <= 9; ++dy) {
            const auto a = scaled(dx, dy, 7), b = scaled(-dx, -dy, 7);
            REQUIRE(a.first == -b.first);
            REQUIRE(a.second == -b.second);
            if (dx || dy) REQUIRE(chebyshev(a) == 7);
        }
}

TEST_CASE("Blob waves: Blob is a WaveFormation value but never a planner shape", "[ai][blob][config]") {
    REQUIRE(QuantBotConfig::parseWaveFormation("Blob", INT32_MIN) == QuantBotConfig::kWaveFormationBlob);
    REQUIRE(QuantBotConfig::parseWaveFormation(" blob ", INT32_MIN) == QuantBotConfig::kWaveFormationBlob);
    REQUIRE(std::string(QuantBotConfig::waveFormationName(QuantBotConfig::kWaveFormationBlob)) == "Blob");
    REQUIRE(QuantBotConfig::parseWaveFormation(QuantBotConfig::waveFormationName(QuantBotConfig::kWaveFormationBlob), 0)
            == QuantBotConfig::kWaveFormationBlob);
    // Every other AI group moves as None with Blob; shapes and None pass through.
    REQUIRE(QuantBotConfig::formationShapeOf(QuantBotConfig::kWaveFormationBlob) == QuantBotConfig::kWaveFormationNone);
    REQUIRE(QuantBotConfig::formationShapeOf(QuantBotConfig::kWaveFormationNone) == QuantBotConfig::kWaveFormationNone);
    REQUIRE(QuantBotConfig::formationShapeOf(0) == 0);
    // It reaches the multiplayer config hash.
    QuantBotConfig none, blob;
    blob.waveFormation = QuantBotConfig::kWaveFormationBlob;
    REQUIRE(none.getConfigHash() != blob.getConfigHash());
    REQUIRE(validPhase(unsigned(Phase::Fallback)));
    REQUIRE_FALSE(validPhase(unsigned(Phase::Fallback) + 1));
}
