#ifndef QUANTBOT_SPENDING_POLICY_H
#define QUANTBOT_SPENDING_POLICY_H

#include <algorithm>
#include <array>
#include <cstdint>

// Integer-only, recomputed from live state each planning pass. No saved timers,
// random tie breaking, or assumption that forecast income is spendable today.
namespace QuantBotSpendingPolicy {
constexpr int horizonMinutes = 4;
enum class Bucket : unsigned { Production, Economy, Military };
constexpr size_t bucketCount = 3;
constexpr int64_t creditScale = 10000;
using Shares = std::array<int, bucketCount>;
inline Shares incomeShares(bool threatened) {
    return threatened ? Shares{1500, 2000, 6500} : Shares{3000, 3500, 3500};
}
inline const char* bucketName(Bucket bucket) {
    return bucket == Bucket::Production ? "production"
        : bucket == Bucket::Economy ? "economy" : "military";
}
// The balance is allocated net income, not forecast receipts. Full accepted
// commitments are charged once, before the engine pays their installments.
// Subtracting unpaid queues from cash makes their later payments wealth-neutral.
// Emergency borrowing leaves debt in its own bucket; subsequent income repays
// it rather than silently spending another bucket's savings a second time.
struct Ledger {
    static constexpr size_t savedBytes = 1 + 8 + bucketCount * 8;
    std::array<int64_t, bucketCount> balance{};
    int64_t lastWealth = 0;
    bool initialized = false;

    void reconcile(int64_t wealth, const Shares& shares) {
        const int64_t receipts = initialized ? wealth - lastWealth : wealth;
        for (size_t i = 0; i < bucketCount; ++i) balance[i] += receipts * shares[i];
        lastWealth = wealth;
        initialized = true;
    }
    int available(Bucket bucket) const {
        return int(std::clamp<int64_t>(balance[static_cast<size_t>(bucket)] / creditScale, 0, INT32_MAX));
    }
    void charge(Bucket bucket, int price) {
        balance[static_cast<size_t>(bucket)] -= int64_t(price) * creditScale;
        lastWealth -= price;
    }
    template<class Stream> void save(Stream& stream) const {
        stream.writeBool(initialized);
        stream.writeSint64(lastWealth);
        for (const auto value : balance) stream.writeSint64(value);
    }
    template<class Stream> void load(Stream& stream) {
        initialized = stream.readBool();
        lastWealth = stream.readSint64();
        for (auto& value : balance) value = stream.readSint64();
    }
};
struct CashFlow {
    int projectedCash = 0;
    int netBurnPerMinute = 0;
    int runwaySeconds = -1; // -1 means income covers sustained production.
    bool fundsParallelProduction = false;
};
// Unpaid orders have already been removed from spendable cash. Subtract only
// the next orders needed to keep those lines running, not their queues again.
inline CashFlow cashFlow(int cash, int spendable, int income, int continuedProductionCost,
                        int sustainedProductionCost, int reserve) {
    CashFlow result;
    result.projectedCash = spendable + income - continuedProductionCost;
    result.netBurnPerMinute = (sustainedProductionCost - income) / horizonMinutes;
    if (sustainedProductionCost > income)
        result.runwaySeconds = int(int64_t(std::max(0,cash-reserve))*horizonMinutes*60
            / (sustainedProductionCost-income));
    result.fundsParallelProduction = spendable >= reserve && result.projectedCash >= reserve;
    return result;
}
inline int receipts(int rate, int cycles, int year) {
    return int(int64_t(std::max(0,rate))*std::max(0,cycles)/std::max(1,year));
}
// Both alternatives share the current fleet. Credit only additional receipts
// after delivery, capped by the spice left after the existing fleet's work.
inline int marginalSpice(int spice, int beforeRate, int afterRate, int delay, int horizon, int year) {
    const int remaining = std::max(0,spice-receipts(beforeRate,std::min(delay,horizon),year));
    const int operating = std::max(0,horizon-delay);
    return std::max(0,std::min(remaining,receipts(afterRate,operating,year))
        - std::min(remaining,receipts(beforeRate,operating,year)));
}
inline int economyScore(int proceeds, int totalCost) {
    return totalCost > 0 ? int(std::min<int64_t>(4000,int64_t(std::max(0,proceeds))*1000/totalCost)) : 0;
}
inline int readiness(int army, int target) {
    return target > 0 ? int(int64_t(std::max(0,target-army))*1000/target) : 0;
}
inline int militaryScore(int price, int value, int army, int target, bool defending) {
    if (price <= 0 || value <= 0 || int64_t(army)+value > target) return 0;
    return int(int64_t(value)*(readiness(army,target)+(defending ? 4000 : 0))/price);
}
// Production buildings earn their priority from units that can actually be
// funded AFTER the existing lines and the building/power costs are covered.
inline int additionalProduction(int funding, int existingCapacity, int addedCapacity, int shortfall, int cost) {
    return std::max(0,std::min({addedCapacity,shortfall-existingCapacity,funding-existingCapacity-cost}));
}
inline int productionScore(int additionalValue, int cost, int army, int target) {
    return additionalValue > 0 && cost > 0
        ? int(int64_t(additionalValue)*readiness(army,target)/(cost+additionalValue)) : 0;
}
inline int reserveForOther(int cash, int price, bool supplier, bool consumed, bool emergency) {
    return supplier || consumed || emergency ? 0 : std::min(std::max(0,cash),std::max(0,price));
}
}
#endif
