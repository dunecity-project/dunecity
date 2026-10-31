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
    REQUIRE(decideCompletion(true, false) == Completion::Place);
    REQUIRE(decideCompletion(true, true) == Completion::Place);
    // Only units in the way: wait (own units move off, enemies are waited out), never give up.
    REQUIRE(decideCompletion(false, true) == Completion::WaitForUnits);
    // A structure on the plot, or lost build range, falls back.
    REQUIRE(decideCompletion(false, false) == Completion::Fallback);
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

TEST_CASE("Pre-placement: an idle yard takes the next order of the shared queue", "[preplace][yards][shared]") {
    // Nothing waiting: only fronts, which stay where their money and progress are.
    REQUIRE(pickSharedOrder({}) == -1);
    REQUIRE(pickSharedOrder({{5, 0, 0}, {9, 0, 1}}) == -1);
    // The order planned first wins, wherever it waits...
    REQUIRE(pickSharedOrder({{40, 1, 0}, {30, 3, 2}, {35, 2, 1}}) == 1);
    // ...a front never moves, however early it was planned...
    REQUIRE(pickSharedOrder({{10, 0, 0}, {30, 1, 1}}) == 1);
    // ...and orders loaded from a save without a plan order (0) go by queue position, then by
    // the earlier yard.
    REQUIRE(pickSharedOrder({{0, 3, 0}, {0, 1, 2}, {0, 2, 1}}) == 1);
    REQUIRE(pickSharedOrder({{0, 1, 2}, {0, 1, 1}}) == 1);
    // Ghost numbers: plan order, even after a yard took an order planned later than another
    // yard's waiting one. Yard 0 holds plans 10 and 30, yard 1 holds 20 and 40.
    std::vector<SharedSlot> shared = {{10, 0, 0}, {30, 1, 0}, {20, 0, 1}, {40, 1, 1}};
    std::stable_sort(shared.begin(), shared.end(), sharedBefore);
    std::vector<std::uint32_t> numbered;
    for(const SharedSlot& slot : shared) numbered.push_back(slot.planOrder);
    REQUIRE(numbered == std::vector<std::uint32_t>{10, 20, 30, 40});
}

TEST_CASE("Pre-placement: retry retains obstructions and hostile units cancel", "[preplace][completion]") {
    REQUIRE(decideCompletion(false, false, true, false) == Completion::Retry);
    REQUIRE(decideCompletion(false, true, true, false) == Completion::Retry);
    REQUIRE(decideCompletion(true, true, true, false) == Completion::Place);
    REQUIRE(decideCompletion(false, true, true, true) == Completion::Cancel);
    REQUIRE(decideCompletion(true, true, true, true) == Completion::Cancel);
    REQUIRE(decideCompletion(false, true, false, true) == Completion::WaitForUnits);
    std::list<Entry> queue = {reserved(18, 1, 5, 5)};
    queue.front().reservationFailed = true;
    REQUIRE_FALSE(frontAwaitsReservedPlacement(queue));
    REQUIRE(frontAwaitsReservedPlacement(queue, true));
    queue.front().reservationID = 0;
    REQUIRE_FALSE(frontAwaitsReservedPlacement(queue, true));
}
