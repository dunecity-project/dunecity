#ifndef CITY_SERVICE_INVESTMENT_POLICY_H
#define CITY_SERVICE_INVESTMENT_POLICY_H
#include <algorithm>
#include <cstdint>
#include <dunecity/CityEffects.h>

namespace CityServiceInvestmentPolicy {
inline int stationOverlapCost(int buildCost, int distance) {
    const int overlap = std::max(0,12-distance);
    return buildCost * 4 * overlap * overlap / (12*12);
}
inline int underservedUtility(int utility, int coverage) {
    return int(int64_t(utility)*100/(100+std::max(0,coverage)));
}
inline int crimeHarm(int crime, int item, int population) {
    int growthPenalty = 0;
    if (item == Structure_ZoneResidential) growthPenalty = crime > 150 ? 200 : crime > 100 ? 100 : 0;
    if (item == Structure_ZoneCommercial) growthPenalty = crime > 120 ? 150 : crime > 80 ? 75 : 0;
    return std::max(0, crime - 63) / 4 + growthPenalty * std::max(1, population) / 4;
}
// Match park terrain for walls/turrets; retain the separate civic stamp model.
inline int parkContribution(int item, int cx, int cy, int px, int py, int blockSize,
                            const DuneCity::ParkTerrainPolicy& terrain) {
    if (DuneCity::usesParkTerrain(item))
        return terrain.marginalGain(cx,cy,DuneCity::getParkLandValueBonus(item),px,py,blockSize);
    int value = 0;
    for (int y = (py/blockSize)*blockSize; y < (py/blockSize+1)*blockSize; ++y)
        for (int x = (px/blockSize)*blockSize; x < (px/blockSize+1)*blockSize; ++x)
            value += DuneCity::falloff(DuneCity::getParkLandValueBonus(item),
                std::max(std::abs(cx-x),std::abs(cy-y)),DuneCity::getParkLandValueRadius(item)+1);
    return value;
}
// Relief measured against a band threshold: the part of the crime reduction
// that actually leaves the band, not the whole reduction.
inline int reliefAboveBand(int crime, int reduction, int threshold) {
    return std::max(0, crime - threshold) - std::max(0, crime - reduction - threshold);
}
// Rebels gather in the 192+ "Dangerous" band over six minutes before the
// outbreak spawns. Relief in the approach band is the investment that stops
// the buildup starting; relief inside it only answers one already under way.
constexpr int preOutbreakBand = 159; // One below the 160 approach threshold.
constexpr int dangerousBand   = 191; // One below Micropolis' 192 dangerous band.
// Savings or a sustained city surplus unlock the later budget. Cash itself
// never becomes recurring income in the allowance calculation.
inline int policingBudgetPercent(int cash, int annualTax, int annualPower, int nominalCost) {
    return cash>=5000 || annualTax-annualPower-nominalCost>=500 ? 50 : 33;
}
inline int policingAllowance(int annualTax, int annualPower, int percent) {
    return int(int64_t(std::max(0,annualTax-annualPower))*percent/100);
}
inline int affordablePoliceFunding(int annualTax, int annualPower, int nominalCost, int percent) {
    if (nominalCost <= 0) return 100;
    return std::clamp(int(int64_t(policingAllowance(annualTax,annualPower,percent))*100/nominalCost),0,100);
}
// Cuts enforce the recurring limit immediately. Ordinary increases wait for
// a review; small ones wait longer instead of being discarded forever. This
// preserves the noise deadband without trapping a recovered city at 96-99%.
// The existing serialized last-change cycle also makes recovery reload-safe.
constexpr uint32_t kPoliceIncreaseReviewCycles = 1875; // 30 game-seconds at the default speed.
constexpr int kPoliceIncreaseDeadband = 5;             // Percentage points of nominal cost.
constexpr uint32_t kPoliceRecoveryReviewMultiplier = 4; // Two minutes for a small recovery.
inline int smoothedPoliceFunding(int current, int target, uint32_t cycle, uint32_t lastChangeCycle,
                                 bool recoverSmallIncreases = true,
                                 uint32_t reviewCycles = kPoliceIncreaseReviewCycles,
                                 int deadband = kPoliceIncreaseDeadband) {
    if (target <= current) return target;                     // cap enforcement: immediate
    const bool smallIncrease = target - current < deadband;
    if (smallIncrease && !recoverSmallIncreases) return current;
    const uint64_t interval = uint64_t(reviewCycles) * (smallIncrease ? kPoliceRecoveryReviewMultiplier : 1);
    // A reload or a rewound clock must not unlock an increase early.
    const uint32_t elapsed = cycle >= lastChangeCycle ? cycle - lastChangeCycle : 0;
    return elapsed >= interval ? target : current;
}
// One game-year horizon. Crime is a civic utility weight, not tax income:
// weighted by actual growth thresholds and severe-crime relief.
struct Value {
    int crime = 0, tax = 0, growthTax = 0, defense = 0;
    int buildCost = 0, upkeep = 0, powerCost = 0, overlapPenalty = 0;
    int neighbourhoodTax = 0; // R/C share of tax gain; preference, not extra income.
    int crimeUtility = 0, dangerousRelief = 0, preOutbreakRelief = 0;
    int cost() const { return std::max(1, buildCost + upkeep + powerCost + overlapPenalty); }
    int64_t benefit() const { return int64_t(crimeUtility) + tax + growthTax + defense; }
    bool repaysThroughLandValue() const { return int64_t(tax) + growthTax > cost(); }
    bool landValueTurretEligible() const {
        return crime > 0 && neighbourhoodTax > 0 && repaysThroughLandValue();
    }
    // A reserved service order exists to buy relief the tax return alone does
    // not justify. Accept the cheaper pre-outbreak relief too, at a higher bar,
    // so districts are treated on the way up instead of after the spawn.
    bool useful(bool emergency) const {
        return crime > 0 && (benefit() > cost()
            || (emergency && (dangerousRelief >= 32 || preOutbreakRelief >= 64)));
    }
    bool betterPoliceSiteThan(const Value& other) const {
        // A police station's location maximizes actual crime removed from
        // occupied buildings. Marginal coverage already discounts overlap;
        // a separate spacing or tax preference must not send it to the fringe.
        if (crime != other.crime) return crime > other.crime;
        if (dangerousRelief != other.dangerousRelief) return dangerousRelief > other.dangerousRelief;
        return betterThan(other);
    }
    bool betterThan(const Value& other) const {
        // Modest placement preference for improving homes and businesses.
        const int64_t left = (benefit()+neighbourhoodTax/2) * other.cost();
        const int64_t right = (other.benefit()+other.neighbourhoodTax/2) * cost();
        return left != right ? left > right : crime > other.crime;
    }
};
inline int annualTaxGain(int taxBaseEighths, int taxPercent, int valueGainSum, int sampledBuildings) {
    if (sampledBuildings <= 0) return 0;
    return int(int64_t(taxBaseEighths) * 14 * taxPercent * valueGainSum
        / (int64_t(8) * 120 * 10 * sampledBuildings));
}
}
#endif
