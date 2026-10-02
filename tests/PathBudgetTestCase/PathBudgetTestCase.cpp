/*
 *  PathBudgetTestCase.cpp - Unit tests for PathBudget synchronization protocol
 *
 *  Tests the contract defined in: .analysis/features/pathbudget-sync/design.md
 */

#include <catch2/catch_all.hpp>
#include <Network/PathBudgetSync.h>

using namespace PathBudgetSync;

// Constants matching Game.cpp
static constexpr uint32_t kBudgetCheckInterval = 375;

// =============================================================================
// Budget Validity Tests (DESYNC Detection)
// =============================================================================

TEST_CASE("PathBudget: Budget matches current is valid", "[pathbudget][desync]") {
    // Scenario 1: Normal operation - client budget matches host budget
    uint32_t clientBudget = 15500;
    size_t hostBudget = 15500;
    size_t previousBudget = 15000;
    uint32_t clientReportCycle = 749;
    uint32_t lastChangeCycle = 750;
    
    REQUIRE(isBudgetValid(clientBudget, hostBudget, previousBudget, 
                          clientReportCycle, lastChangeCycle));
}

TEST_CASE("PathBudget: Delayed packet with grace period is valid", "[pathbudget][desync]") {
    // Scenario 2: Delayed packet - client sent old budget before change
    uint32_t clientBudget = 15000;  // Old budget
    size_t hostBudget = 15500;      // New budget (already applied)
    size_t previousBudget = 15000;  // Previous budget matches client
    uint32_t clientReportCycle = 749;  // Before change
    uint32_t lastChangeCycle = 750;    // Change happened at 750
    
    // Should be valid - client report is from before change
    REQUIRE(isBudgetValid(clientBudget, hostBudget, previousBudget,
                          clientReportCycle, lastChangeCycle));
}

TEST_CASE("PathBudget: Real desync after change cycle is invalid", "[pathbudget][desync]") {
    // Scenario 3: Real DESYNC - client reports old budget AFTER change cycle
    uint32_t clientBudget = 15000;     // Old budget
    size_t hostBudget = 15500;         // New budget
    size_t previousBudget = 15000;     // Matches client, but...
    uint32_t clientReportCycle = 751;  // AFTER change cycle
    uint32_t lastChangeCycle = 750;
    
    // Should be INVALID - client report is from after change
    REQUIRE_FALSE(isBudgetValid(clientBudget, hostBudget, previousBudget,
                                clientReportCycle, lastChangeCycle));
}

TEST_CASE("PathBudget: Real desync wrong previous budget is invalid", "[pathbudget][desync]") {
    // Scenario 4: Real DESYNC - client budget doesn't match current OR previous
    uint32_t clientBudget = 14000;     // Neither current nor previous
    size_t hostBudget = 16000;         // Current
    size_t previousBudget = 15500;     // Previous
    uint32_t clientReportCycle = 1124;
    uint32_t lastChangeCycle = 1125;
    
    // Should be INVALID - budget matches neither
    REQUIRE_FALSE(isBudgetValid(clientBudget, hostBudget, previousBudget,
                                clientReportCycle, lastChangeCycle));
}

TEST_CASE("PathBudget: Multiple changes with missed budget is invalid", "[pathbudget][desync]") {
    // Scenario 5: Multiple changes - client missed several changes
    uint32_t clientBudget = 15000;     // Very old budget
    size_t hostBudget = 16000;         // Current (after 2 changes)
    size_t previousBudget = 15500;     // Previous (15500, not 15000)
    uint32_t clientReportCycle = 1124;
    uint32_t lastChangeCycle = 1125;
    
    // Should be INVALID - client budget doesn't match previous
    REQUIRE_FALSE(isBudgetValid(clientBudget, hostBudget, previousBudget,
                                clientReportCycle, lastChangeCycle));
}

// =============================================================================
// Budget-for-Decision-Time Tests
// =============================================================================

TEST_CASE("PathBudget: No pending change returns current budget", "[pathbudget][decision]") {
    size_t currentBudget = 15000;
    std::vector<PendingBudgetChange> pending;
    uint32_t currentCycle = 749;
    
    REQUIRE(getBudgetForDecisionTime(currentBudget, pending, currentCycle) == currentBudget);
}

TEST_CASE("PathBudget: With pending change returns new budget", "[pathbudget][decision]") {
    size_t currentBudget = 15000;
    std::vector<PendingBudgetChange> pending = {
        {15500, 750}  // Change to 15500 at cycle 750
    };
    uint32_t currentCycle = 749;
    
    REQUIRE(getBudgetForDecisionTime(currentBudget, pending, currentCycle) == 15500);
}

TEST_CASE("PathBudget: Multiple pending changes uses correct one", "[pathbudget][decision]") {
    size_t currentBudget = 15000;
    std::vector<PendingBudgetChange> pending = {
        {15500, 750},   // First change at 750
        {16000, 1125}   // Second change at 1125
    };
    uint32_t currentCycle = 749;
    
    // Should return 15500 (change at 750), not 16000
    REQUIRE(getBudgetForDecisionTime(currentBudget, pending, currentCycle) == 15500);
}

TEST_CASE("PathBudget: Pending change for wrong cycle returns current", "[pathbudget][decision]") {
    size_t currentBudget = 15000;
    std::vector<PendingBudgetChange> pending = {
        {15500, 1125}  // Change at 1125, not 750
    };
    uint32_t currentCycle = 749;
    
    REQUIRE(getBudgetForDecisionTime(currentBudget, pending, currentCycle) == currentBudget);
}

// =============================================================================
// Timing Tests
// =============================================================================

TEST_CASE("PathBudget: Stats should be sent one cycle before interval", "[pathbudget][timing]") {
    REQUIRE(shouldSendStats(374, kBudgetCheckInterval));
    REQUIRE(shouldSendStats(749, kBudgetCheckInterval));
    REQUIRE(shouldSendStats(1124, kBudgetCheckInterval));
    
    REQUIRE_FALSE(shouldSendStats(375, kBudgetCheckInterval));
    REQUIRE_FALSE(shouldSendStats(373, kBudgetCheckInterval));
}

TEST_CASE("PathBudget: Decision should be made at interval boundaries", "[pathbudget][timing]") {
    REQUIRE(shouldMakeDecision(375, kBudgetCheckInterval));
    REQUIRE(shouldMakeDecision(750, kBudgetCheckInterval));
    REQUIRE(shouldMakeDecision(1125, kBudgetCheckInterval));
    REQUIRE(shouldMakeDecision(0, kBudgetCheckInterval));
    
    REQUIRE_FALSE(shouldMakeDecision(374, kBudgetCheckInterval));
    REQUIRE_FALSE(shouldMakeDecision(376, kBudgetCheckInterval));
}

TEST_CASE("PathBudget: Apply cycle is next interval boundary", "[pathbudget][timing]") {
    REQUIRE(calculateApplyCycle(375, kBudgetCheckInterval) == 750);
    REQUIRE(calculateApplyCycle(376, kBudgetCheckInterval) == 750);
    REQUIRE(calculateApplyCycle(749, kBudgetCheckInterval) == 750);
    REQUIRE(calculateApplyCycle(750, kBudgetCheckInterval) == 1125);
    REQUIRE(calculateApplyCycle(0, kBudgetCheckInterval) == 375);
}

// =============================================================================
// Single-player reduction gate: an unrelated stall must not shrink the budget
// =============================================================================

// Mirrors Game::applySinglePlayerBudgetAdjustment / Game::kPathReductionFloorMs.
static constexpr double kLowFpsThreshold   = 58.0;
static constexpr double kReductionFloorMs  = 4.0;

TEST_CASE("PathBudget: AI stall with cheap pathfinding holds the budget", "[pathbudget][reduction]") {
    // The measured 1.0.793 stutter episode: 34.5 and 53.9 FPS windows whose cost
    // was AI (9.0ms/frame) while pathfinding averaged 1.49ms/frame. Every one of
    // these reduced the budget before the gate existed; none may now.
    REQUIRE(shouldHoldBudgetDespiteLowFps(34.5, 1.49, kLowFpsThreshold, kReductionFloorMs));
    REQUIRE(shouldHoldBudgetDespiteLowFps(53.9, 1.49, kLowFpsThreshold, kReductionFloorMs));
    REQUIRE(shouldHoldBudgetDespiteLowFps(5.4,  0.80, kLowFpsThreshold, kReductionFloorMs));
    // A city-effects stall looks identical from the frame rate alone.
    REQUIRE(shouldHoldBudgetDespiteLowFps(45.0, 0.00, kLowFpsThreshold, kReductionFloorMs));
}

TEST_CASE("PathBudget: genuine path overload still reduces", "[pathbudget][reduction]") {
    // Pathfinding itself over the floor: the reduction tiers must still run, at
    // every severity, so the existing protection against path overload survives.
    REQUIRE_FALSE(shouldHoldBudgetDespiteLowFps(49.0, 12.0, kLowFpsThreshold, kReductionFloorMs));
    REQUIRE_FALSE(shouldHoldBudgetDespiteLowFps(54.0,  8.0, kLowFpsThreshold, kReductionFloorMs));
    REQUIRE_FALSE(shouldHoldBudgetDespiteLowFps(57.0,  4.0, kLowFpsThreshold, kReductionFloorMs));
    // Exactly at the floor counts as costly: the gate is a strict "below floor".
    REQUIRE_FALSE(shouldHoldBudgetDespiteLowFps(30.0, kReductionFloorMs,
                                                kLowFpsThreshold, kReductionFloorMs));
}

TEST_CASE("PathBudget: recovery and healthy frame rates are unaffected", "[pathbudget][reduction]") {
    // At or above the reduction threshold the gate never engages, so the raise
    // branch and its own pathfinding ceiling keep deciding as before.
    REQUIRE_FALSE(shouldHoldBudgetDespiteLowFps(58.0, 0.5, kLowFpsThreshold, kReductionFloorMs));
    REQUIRE_FALSE(shouldHoldBudgetDespiteLowFps(60.0, 0.5, kLowFpsThreshold, kReductionFloorMs));
    REQUIRE_FALSE(shouldHoldBudgetDespiteLowFps(59.5, 12.0, kLowFpsThreshold, kReductionFloorMs));
    // Recovery: once an AI stall clears, a still-low frame rate caused by real
    // path load reduces again rather than staying held.
    REQUIRE(shouldHoldBudgetDespiteLowFps(50.0, 3.9, kLowFpsThreshold, kReductionFloorMs));
    REQUIRE_FALSE(shouldHoldBudgetDespiteLowFps(50.0, 4.1, kLowFpsThreshold, kReductionFloorMs));
}
