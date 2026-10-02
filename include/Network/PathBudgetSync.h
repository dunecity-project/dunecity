/*
 *  PathBudgetSync.h - Testable pure functions for PathBudget synchronization
 *
 *  See: .analysis/features/pathbudget-sync/design.md for protocol contract
 */

#ifndef PATHBUDGETSYNC_H
#define PATHBUDGETSYNC_H

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <vector>

namespace PathBudgetSync {

/**
 * Represents a pending budget change queued for future application.
 */
struct PendingBudgetChange {
    size_t newBudget;
    uint32_t applyCycle;
};

/**
 * Determines if a client's reported budget is valid given the current state.
 * 
 * Protocol contract:
 * - Budget is valid if it matches current budget (normal case)
 * - Budget is valid if client's report is from before the last change AND matches previous budget
 * 
 * @param clientBudget      Budget reported by the client
 * @param hostBudget        Host's current negotiated budget
 * @param previousBudget    Budget before the last change (for grace period)
 * @param clientReportCycle Cycle when client's stats were captured
 * @param lastChangeCycle   Cycle when the last budget change was applied
 * @return true if budget is considered valid (no desync)
 */
inline bool isBudgetValid(
    uint32_t clientBudget,
    size_t hostBudget,
    size_t previousBudget,
    uint32_t clientReportCycle,
    uint32_t lastChangeCycle
) {
    // Case 1: Budget matches current - normal case
    if (clientBudget == hostBudget) {
        return true;
    }
    
    // Case 2: Defense-in-depth - allow previous budget if client report is from before change
    bool clientReportFromBeforeChange = (clientReportCycle < lastChangeCycle);
    bool matchesPrevious = (clientBudget == previousBudget);
    
    return clientReportFromBeforeChange && matchesPrevious;
}

/**
 * Calculates the budget to report for a given decision cycle.
 * 
 * Protocol contract:
 * - Stats are sent 1 cycle before decision (cycle N-1 for decision at N)
 * - Must report budget-at-decision-time, not current budget
 * - Check pending changes for the decision cycle
 * 
 * @param currentBudget     Current negotiated budget
 * @param pendingChanges    List of pending budget changes
 * @param currentCycle      Current game cycle (stats are sent at decisionCycle - 1)
 * @return Budget to include in stats packet (budget-at-decision-time)
 */
inline size_t getBudgetForDecisionTime(
    size_t currentBudget,
    const std::vector<PendingBudgetChange>& pendingChanges,
    uint32_t currentCycle
) {
    uint32_t decisionCycle = currentCycle + 1;
    
    for (const auto& pending : pendingChanges) {
        if (pending.applyCycle == decisionCycle) {
            return pending.newBudget;
        }
    }
    
    return currentBudget;
}

/**
 * Determines if stats should be sent this cycle.
 * 
 * Stats are sent 1 cycle BEFORE the check interval to ensure host has fresh data.
 * 
 * @param currentCycle      Current game cycle
 * @param checkInterval     Budget check interval (typically 375)
 * @return true if stats should be sent this cycle
 */
inline bool shouldSendStats(uint32_t currentCycle, uint32_t checkInterval) {
    return ((currentCycle + 1) % checkInterval) == 0;
}

/**
 * Determines if budget decision should be made this cycle.
 * 
 * @param currentCycle      Current game cycle
 * @param checkInterval     Budget check interval (typically 375)
 * @return true if budget decision should be made this cycle
 */
inline bool shouldMakeDecision(uint32_t currentCycle, uint32_t checkInterval) {
    return (currentCycle % checkInterval) == 0;
}

/**
 * Calculates the apply cycle for a new budget change.
 * 
 * Budget changes are scheduled for the NEXT interval boundary,
 * giving clients a full interval to receive the change.
 * 
 * @param currentCycle      Current game cycle
 * @param checkInterval     Budget check interval (typically 375)
 * @return Cycle at which the budget change should be applied
 */
inline uint32_t calculateApplyCycle(uint32_t currentCycle, uint32_t checkInterval) {
    return ((currentCycle / checkInterval) + 1) * checkInterval;
}

/**
 * Most budget orders that may be waiting to be applied.
 *
 * The host schedules at most one order per check interval, and every order is consumed at its
 * apply cycle, so a legitimate session never holds more than a couple. The bound stops a peer
 * from growing the queue without limit.
 */
constexpr size_t kMaxPendingBudgetChanges = 8;

/**
 * Decides whether a received budget order is one the host could legitimately have scheduled.
 *
 * Protocol contract (see Game::broadcastBudgetChange):
 * - the budget is inside the negotiated range,
 * - the order applies in the future, but not further away than one full check interval plus
 *   the slack needed for the message to arrive, and
 * - the apply cycle is a check interval boundary, which is what keeps the change deterministic.
 *
 * \param newBudget      budget the order asks for
 * \param applyCycle     cycle the order should take effect at
 * \param currentCycle   receiver's current game cycle
 * \param checkInterval  budget check interval (typically 375)
 * \param minBudget      lowest budget the simulation accepts
 * \param maxBudget      highest budget the simulation accepts
 * \return true if the order may be queued
 */
inline bool isAcceptableBudgetOrder(
    size_t newBudget,
    uint32_t applyCycle,
    uint32_t currentCycle,
    uint32_t checkInterval,
    size_t minBudget,
    size_t maxBudget
) {
    if(checkInterval == 0) {
        return false;
    }
    if(newBudget < minBudget || newBudget > maxBudget) {
        return false;
    }
    if((applyCycle % checkInterval) != 0) {
        return false;
    }
    if(applyCycle < currentCycle) {
        return false;
    }
    // Two intervals of headroom covers the interval the order is scheduled for plus a late
    // delivery; anything beyond that is not something the host schedules.
    const uint32_t headroom = checkInterval * 2;
    if(currentCycle > 0xFFFFFFFFu - headroom) {
        return true;
    }
    return applyCycle <= currentCycle + headroom;
}

/// Should a single-player FPS-driven budget reduction be suppressed?
///
/// Low FPS caused by AI or city simulation must not starve unit movement.
/// Require pathfinding's own average cost to reach \a reductionFloorMs before
/// reducing its budget. Called only by the single-player adjustment path;
/// multiplayer continues to use the existing host negotiation.
inline bool shouldHoldBudgetDespiteLowFps(
    double avgFps,
    double avgPathfindingMs,
    double lowFpsThreshold,
    double reductionFloorMs
) {
    // Only a frame rate the reduction tiers would actually have acted on is
    // held; anything at or above the threshold was never a reduction candidate.
    if(avgFps >= lowFpsThreshold) {
        return false;
    }
    return avgPathfindingMs < reductionFloorMs;
}

} // namespace PathBudgetSync

#endif // PATHBUDGETSYNC_H
