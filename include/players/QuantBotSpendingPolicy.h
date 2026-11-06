#ifndef QUANTBOT_SPENDING_POLICY_H
#define QUANTBOT_SPENDING_POLICY_H

#include <algorithm>
#include <cstdint>
#include <climits>

// Integer-only, recomputed from live state each planning pass. No saved timers,
// random tie breaking, or assumption that forecast income is spendable today.
namespace QuantBotSpendingPolicy {
constexpr int horizonMinutes = 4;
// StartWhatMakesSense: army production keeps this share of cash and income while
// the existing target/mix/caps admit units it can actually spend it on. 30% is
// the default (OFF) bot's measured pooled military share of credits paid in the
// first 20 minutes (b8a627aa cohort, 0.307); spare funds may add to it.
constexpr int sensibleArmyShareBps = 3000;
enum class Bucket : unsigned { Production, Economy, Military };
inline const char* bucketName(Bucket bucket) {
    return bucket == Bucket::Production ? "production"
        : bucket == Bucket::Economy ? "economy" : "military";
}
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
// Capacity follows the live income share, not a fixed one/two-factory ceiling.
inline int incomeShareDemand(int income, int shareBps) {
    return int(int64_t(std::max(0,income))*std::clamp(shareBps,0,10000)/10000);
}
inline int factoryTargetForDemand(int actual, int capacity, int nextCapacity, int demand) {
    actual=std::max(0,actual);
    if (nextCapacity<=0 || demand<=capacity) return actual;
    const int64_t missing=int64_t(demand)-std::max(0,capacity);
    return int(std::min<int64_t>(INT_MAX,int64_t(actual)+(missing+nextCapacity-1)/nextCapacity));
}
inline int reserveForOther(int cash, int price, bool supplier, bool consumed, bool emergency) {
    return supplier || consumed || emergency ? 0 : std::min(std::max(0,cash),std::max(0,price));
}
}
#endif
