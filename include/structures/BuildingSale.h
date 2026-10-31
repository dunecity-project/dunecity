#ifndef BUILDINGSALE_H
#define BUILDINGSALE_H

#include <fixmath/FixPoint.h>

#include <algorithm>
#include <cstdint>

namespace BuildingSale {

/// Completed investments are refunded once, after applying the house's sale rate.
inline int refund(int purchasePrice, int conversionCost, int upgradeLevels,
                  int upgradeCost, bool powerUpgraded, bool easyHouse) {
    const std::int64_t investment = std::max(0, purchasePrice)
        + static_cast<std::int64_t>(std::max(0, conversionCost))
        + static_cast<std::int64_t>(upgradeLevels) * std::max(0, upgradeCost)
        + (powerUpgraded ? 150 : 0);
    return static_cast<int>(investment * (easyHouse ? 75 : 50) / 100);
}

struct PendingRefund {
    FixPoint total = 0;
    FixPoint fromStarting = 0;
};

/// Queued-but-unstarted items cost nothing; paid progress returns at face value.
inline PendingRefund pendingRefund(int queueSize, FixPoint productionProgress,
                                   FixPoint productionFromStarting, bool upgrading,
                                   FixPoint upgradeProgress, FixPoint upgradeFromStarting) {
    PendingRefund result;
    if(queueSize > 0) {
        result.total = productionProgress;
        result.fromStarting = productionFromStarting;
    }
    if(upgrading) {
        result.total += upgradeProgress;
        result.fromStarting += upgradeFromStarting;
    }
    return result;
}

} // namespace BuildingSale

#endif // BUILDINGSALE_H
