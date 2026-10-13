#include <catch2/catch_all.hpp>

#include <structures/PlacementReservationPolicy.h>

#include <cstdint>
#include <list>
#include <vector>

using namespace PlacementReservation;

namespace {

constexpr int kBuildRange = 2;   // Definitions.h BUILDRANGE

/// The fields ConstructionYard's ProductionQueueItem carries for a reservation.
struct Entry {
    std::uint32_t itemID;
    std::uint32_t reservationID;
    int plotX;
    int plotY;
    bool reservationFailed;
};

Entry reserved(std::uint32_t item, std::uint32_t id, int x, int y) { return {item, id, x, y, false}; }
Entry unreserved(std::uint32_t item) { return {item, 0, 0, 0, false}; }

/// Own tiles: everything left of column 10 counts as within build range.
const auto ownRangeLeftOf10 = [](int x, int) { return x < 10; };

} // namespace

TEST_CASE("Pre-placement: a plot needs real or reserved build range", "[preplace][validity]") {
    const std::vector<Footprint> none;
    REQUIRE(validate({8, 5, 2, 2}, true, ownRangeLeftOf10, none, kBuildRange) == Verdict::Valid);
    // Only one footprint tile needs to be in range, as in Map::okayToPlaceStructure.
    REQUIRE(validate({9, 5, 3, 2}, true, ownRangeLeftOf10, none, kBuildRange) == Verdict::Valid);
    REQUIRE(validate({10, 5, 2, 2}, true, ownRangeLeftOf10, none, kBuildRange) == Verdict::OutOfRange);
    // Terrain, structures and the map edge refuse a plot however close it is.
    REQUIRE(validate({8, 5, 2, 2}, false, ownRangeLeftOf10, none, kBuildRange) == Verdict::Blocked);
    REQUIRE(validate({8, 5, 0, 2}, true, ownRangeLeftOf10, none, kBuildRange) == Verdict::Blocked);
}

TEST_CASE("Pre-placement: earlier reservations of the house chain the range rule", "[preplace][validity][chain]") {
    // A: a 2x2 windtrap reserved at the edge of the real range.
    const std::vector<Footprint> chainA = {{8, 5, 2, 2}};
    // A's last column is x=9, so B may start BUILDRANGE tiles further, at x=11.
    REQUIRE(validate({11, 5, 2, 2}, true, ownRangeLeftOf10, chainA, kBuildRange) == Verdict::Valid);
    // One tile further is not: the chain grants exactly BUILDRANGE, like a real building.
    REQUIRE(validate({12, 5, 2, 2}, true, ownRangeLeftOf10, chainA, kBuildRange) == Verdict::OutOfRange);
    // Diagonal reach follows Map::isWithinBuildRange (both axes within range): A's last
    // row is y=6, so y=8 is the furthest row.
    REQUIRE(validate({11, 8, 2, 2}, true, ownRangeLeftOf10, chainA, kBuildRange) == Verdict::Valid);
    REQUIRE(validate({11, 9, 2, 2}, true, ownRangeLeftOf10, chainA, kBuildRange) == Verdict::OutOfRange);

    // A chain of chains: C leans on B (last column x=12), which leans on A.
    const std::vector<Footprint> chainAB = {{8, 5, 2, 2}, {11, 5, 2, 2}};
    REQUIRE(validate({14, 5, 3, 2}, true, ownRangeLeftOf10, chainAB, kBuildRange) == Verdict::Valid);
    REQUIRE(validate({14, 5, 3, 2}, true, ownRangeLeftOf10, chainA, kBuildRange) == Verdict::OutOfRange);
    // Chain range does not rescue a blocked plot.
    REQUIRE(validate({11, 5, 2, 2}, false, ownRangeLeftOf10, chainA, kBuildRange) == Verdict::Blocked);
}

TEST_CASE("Pre-placement: reserved plots never overlap", "[preplace][validity][overlap]") {
    const std::vector<Footprint> palace = {{4, 4, 3, 3}};
    REQUIRE(validate({6, 6, 2, 2}, true, ownRangeLeftOf10, palace, kBuildRange) == Verdict::Overlap);
    REQUIRE(validate({4, 4, 1, 1}, true, ownRangeLeftOf10, palace, kBuildRange) == Verdict::Overlap);
    // Touching edges is not overlapping.
    REQUIRE(validate({7, 4, 2, 2}, true, ownRangeLeftOf10, palace, kBuildRange) == Verdict::Valid);
    REQUIRE(validate({4, 7, 3, 3}, true, ownRangeLeftOf10, palace, kBuildRange) == Verdict::Valid);
    REQUIRE_FALSE(overlaps({0, 0, 2, 2}, {2, 0, 2, 2}));
    REQUIRE(overlaps({0, 0, 3, 2}, {2, 1, 2, 2}));
}

TEST_CASE("Pre-placement: a finished reservation places, waits for units, or falls back",
          "[preplace][completion]") {
    const std::uint32_t wait = 5000 / 16;   // MILLI2CYCLES(5000) at GAMESPEED_DEFAULT 16
    REQUIRE(decideCompletion(true, false, 0, wait) == Completion::Place);
    // A plot that becomes valid during the wait is placed at once.
    REQUIRE(decideCompletion(true, true, wait - 1, wait) == Completion::Place);

    // Only units in the way: keep asking them to move for the whole budget...
    REQUIRE(decideCompletion(false, true, 0, wait) == Completion::WaitForUnits);
    REQUIRE(decideCompletion(false, true, wait - 1, wait) == Completion::WaitForUnits);
    // ...and not one cycle longer.
    REQUIRE(decideCompletion(false, true, wait, wait) == Completion::Fallback);

    // A structure on the plot, or lost adjacency, falls back without waiting.
    REQUIRE(decideCompletion(false, false, 0, wait) == Completion::Fallback);
}

TEST_CASE("Pre-placement: cancelling refunds only the order under construction", "[preplace][cancel][refund]") {
    std::list<Entry> queue = {reserved(18, 1, 5, 5), reserved(20, 2, 9, 5), unreserved(19)};
    const int progress = 137;

    // A queued, unstarted reservation costs nothing to cancel and keeps the others in order.
    CancelResult result = eraseReservation(queue, 2);
    REQUIRE(result.found);
    REQUIRE_FALSE(result.wasInProduction);
    REQUIRE(result.itemID == 20);
    REQUIRE(refundOnCancel(result, progress) == 0);
    REQUIRE(queue.size() == 2);
    REQUIRE(queue.front().reservationID == 1);
    REQUIRE(queue.back().itemID == 19);

    // The front one is the one money was spent on: all of it comes back.
    result = eraseReservation(queue, 1);
    REQUIRE(result.found);
    REQUIRE(result.wasInProduction);
    REQUIRE(refundOnCancel(result, progress) == progress);
    REQUIRE(queue.size() == 1);
    REQUIRE(queue.front().reservationID == 0);

    // Unknown ids, 0 ("not reserved") and repeats change nothing and refund nothing.
    for(std::uint32_t id : {0u, 1u, 77u}) {
        result = eraseReservation(queue, id);
        REQUIRE_FALSE(result.found);
        REQUIRE(refundOnCancel(result, progress) == 0);
        REQUIRE(queue.size() == 1);
    }
}

TEST_CASE("Pre-placement: reserved and unreserved orders keep one queue order", "[preplace][ordering]") {
    std::list<Entry> queue = {reserved(18, 4, 5, 5), unreserved(19), reserved(20, 5, 9, 5), unreserved(21)};

    const std::vector<Ghost> ghosts = ghostsInQueueOrder(queue);
    REQUIRE(ghosts.size() == 2);
    REQUIRE(ghosts[0].number == 1);
    REQUIRE(ghosts[0].inProduction);
    REQUIRE(ghosts[0].reservationID == 4);
    REQUIRE(ghosts[0].x == 5);
    // The unreserved order between them keeps its place in the count.
    REQUIRE(ghosts[1].number == 3);
    REQUIRE_FALSE(ghosts[1].inProduction);
    REQUIRE(ghosts[1].itemID == 20);

    // The reserved front order is placed by the simulation...
    REQUIRE(frontAwaitsReservedPlacement(queue));
    queue.pop_front();
    // ...the unreserved one behind it waits for the player as today...
    REQUIRE_FALSE(frontAwaitsReservedPlacement(queue));
    queue.pop_front();
    REQUIRE(frontAwaitsReservedPlacement(queue));
    REQUIRE(ghostsInQueueOrder(queue).front().number == 1);
    // ...and a reservation that fell back is the player's again, drawn red.
    queue.front().reservationFailed = true;
    REQUIRE_FALSE(frontAwaitsReservedPlacement(queue));
    REQUIRE(ghostsInQueueOrder(queue).front().failed);
}

TEST_CASE("Pre-placement: reservation ids are stable and never 0", "[preplace][ids]") {
    REQUIRE(nextReservationID(1) == 2);
    REQUIRE(nextReservationID(41) == 42);
    REQUIRE(nextReservationID(0xFFFFFFFFu) == 1);
}

TEST_CASE("Pre-placement: orders spread over the house's Construction Yards", "[preplace][yards]") {
    // No yard can take the order.
    REQUIRE(chooseYard({}, 7) == 0);
    // The least remaining work wins, wherever the building was picked from.
    REQUIRE(chooseYard({{7, 900}, {9, 0}, {12, 300}}, 7) == 9);
    // On a tie the yard the player picked from keeps the order...
    REQUIRE(chooseYard({{5, 0}, {7, 0}, {9, 0}}, 7) == 7);
    // ...otherwise the lowest object id, whatever the listing order.
    REQUIRE(chooseYard({{9, 0}, {5, 0}, {12, 400}}, 7) == 5);
    // Successive orders alternate between two idle yards as each one's work grows.
    std::vector<YardLoad> yards{{3, 0}, {4, 0}};
    std::vector<std::uint32_t> picks;
    for(int order = 0; order < 4; ++order) {
        const std::uint32_t chosen = chooseYard(yards, 3);
        picks.push_back(chosen);
        for(YardLoad& yard : yards) if(yard.yardID == chosen) yard.remainingWork += 500;
    }
    REQUIRE(picks == std::vector<std::uint32_t>{3, 4, 3, 4});
}
