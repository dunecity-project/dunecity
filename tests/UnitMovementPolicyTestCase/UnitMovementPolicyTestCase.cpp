/*
 *  UnitMovementPolicyTestCase.cpp
 *
 *  Tests the policy that decides when a forced move order should release a
 *  pending carryall pickup booking. The bug this guards against: harvesters
 *  with awaitingPickup=true silently ignore navigate(), so a player move order
 *  appears to do nothing (see ornithopter-hunt log: "HARVESTER 152 STUCK 30s
 *  ... awaiting=1 ... pathSize=0").
 */

#include <catch2/catch_all.hpp>
#include <units/UnitMovementPolicy.h>
#include <DataTypes.h>

using UnitMovementPolicy::shouldCancelPickupOnMove;
using UnitMovementPolicy::attackModeAfterCancellingPickup;

TEST_CASE("Forced move on a unit awaiting pickup releases the booking", "[unit-movement]") {
    REQUIRE(shouldCancelPickupOnMove(/*awaitingPickup=*/true, GUARD,             /*bForced=*/true));
    REQUIRE(shouldCancelPickupOnMove(/*awaitingPickup=*/true, HARVEST,           /*bForced=*/true));
    REQUIRE(shouldCancelPickupOnMove(/*awaitingPickup=*/true, CARRYALLREQUESTED, /*bForced=*/true));
}

TEST_CASE("Forced move on a unit with CARRYALLREQUESTED but no booking still cancels", "[unit-movement]") {
    // Carryall booking can be cleared by checkPos before pickup arrives, but
    // the next checkPos re-books because attackMode is still CARRYALLREQUESTED.
    // A forced move must clear the mode too, otherwise the harvester gets
    // re-trapped immediately.
    REQUIRE(shouldCancelPickupOnMove(/*awaitingPickup=*/false, CARRYALLREQUESTED, /*bForced=*/true));
}

TEST_CASE("Non-forced (AI) move never cancels a pickup", "[unit-movement]") {
    // AI internal repathing during harvest must not break in-flight carryall
    // pickups.
    REQUIRE_FALSE(shouldCancelPickupOnMove(true,  GUARD,             /*bForced=*/false));
    REQUIRE_FALSE(shouldCancelPickupOnMove(true,  CARRYALLREQUESTED, /*bForced=*/false));
    REQUIRE_FALSE(shouldCancelPickupOnMove(false, HARVEST,           /*bForced=*/false));
}

TEST_CASE("Forced move on a unit with no pickup booking is a no-op", "[unit-movement]") {
    REQUIRE_FALSE(shouldCancelPickupOnMove(false, GUARD,   true));
    REQUIRE_FALSE(shouldCancelPickupOnMove(false, HARVEST, true));
    REQUIRE_FALSE(shouldCancelPickupOnMove(false, HUNT,    true));
}

TEST_CASE("Cancelled CARRYALLREQUESTED falls back to HARVEST for harvesters", "[unit-movement]") {
    REQUIRE(attackModeAfterCancellingPickup(CARRYALLREQUESTED, /*isHarvester=*/true) == HARVEST);
}

TEST_CASE("Cancelled CARRYALLREQUESTED falls back to GUARD for combat units", "[unit-movement]") {
    REQUIRE(attackModeAfterCancellingPickup(CARRYALLREQUESTED, /*isHarvester=*/false) == GUARD);
}

TEST_CASE("Non-CARRYALLREQUESTED attack modes are preserved on cancellation", "[unit-movement]") {
    // A harvester in HARVEST mode that happened to be awaiting pickup should
    // stay in HARVEST mode after the player overrides with a manual move.
    REQUIRE(attackModeAfterCancellingPickup(HARVEST,   true)  == HARVEST);
    REQUIRE(attackModeAfterCancellingPickup(GUARD,     false) == GUARD);
    REQUIRE(attackModeAfterCancellingPickup(AREAGUARD, false) == AREAGUARD);
    REQUIRE(attackModeAfterCancellingPickup(HUNT,      false) == HUNT);
    REQUIRE(attackModeAfterCancellingPickup(STOP,      false) == STOP);
}

TEST_CASE("Legacy AI harvesters restart while qBot and human STOP orders persist", "[unit-movement][harvester]") {
    using UnitMovementPolicy::resumeStoppedHarvester;
    REQUIRE(resumeStoppedHarvester(true, false));  // Original/Smart AI deployment.
    REQUIRE_FALSE(resumeStoppedHarvester(true, true));  // qBot evacuation hold.
    REQUIRE_FALSE(resumeStoppedHarvester(false, false)); // Human stop.
    REQUIRE_FALSE(resumeStoppedHarvester(false, true)); // Human with qBot coplayer.
}

TEST_CASE("A carryall drop keeps a player's unit in its own mode", "[unit-movement][carryall]") {
    using UnitMovementPolicy::attackModeAfterCarryallDrop;
    // A unit flown to its factory's destination guards the spot, like one that drove there,
    // instead of chasing sandworms in Area Guard (player report on 1.0.873).
    REQUIRE(attackModeAfterCarryallDrop(GUARD, false, false, /*aiHouse=*/false, false) == GUARD);
    REQUIRE(attackModeAfterCarryallDrop(CARRYALLREQUESTED, false, false, false, false) == GUARD);
    // A mode the player chose survives the flight.
    REQUIRE(attackModeAfterCarryallDrop(HOLD,   false, false, false, false) == HOLD);
    REQUIRE(attackModeAfterCarryallDrop(AMBUSH, false, false, false, false) == AMBUSH);
    REQUIRE(attackModeAfterCarryallDrop(AREAGUARD, false, false, false, false) == AREAGUARD);
    // An attack-move or patrol keeps fighting on its way.
    REQUIRE(attackModeAfterCarryallDrop(GUARD, false, false, false, /*attackMoving=*/true) == AREAGUARD);
}

TEST_CASE("A carryall drop keeps the legacy modes for bots, saboteurs and harvesters", "[unit-movement][carryall]") {
    using UnitMovementPolicy::attackModeAfterCarryallDrop;
    REQUIRE(attackModeAfterCarryallDrop(GUARD, false, false, /*aiHouse=*/true, false) == AREAGUARD);
    REQUIRE(attackModeAfterCarryallDrop(HOLD,  false, false, true, false) == AREAGUARD);
    REQUIRE(attackModeAfterCarryallDrop(GUARD, /*isSaboteur=*/true, false, false, false) == HUNT);
    REQUIRE(attackModeAfterCarryallDrop(GUARD, true, false, true, false) == HUNT);
    REQUIRE(attackModeAfterCarryallDrop(GUARD, false, /*isHarvester=*/true, false, false) == HARVEST);
    REQUIRE(attackModeAfterCarryallDrop(GUARD, false, true, true, false) == HARVEST);
}
