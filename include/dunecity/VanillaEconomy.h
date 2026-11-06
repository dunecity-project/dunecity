#ifndef DUNECITY_VANILLA_ECONOMY_H
#define DUNECITY_VANILLA_ECONOMY_H
#include <algorithm>
#include <array>
namespace DuneCity {
// Additional room for spice-funded vanilla fleets; explicit lobby overrides win.
inline int vanillaHarvesterCapacity(int configured) {
    return configured <= 0 ? configured : configured + configured / 2;
}
// Budget 1,500 remaining spice per worker (just over two 700-spice loads).
// Still share the field between houses and taper investment as it depletes.
inline int vanillaHarvesterTarget(int spice, int competitors, int capacity) {
    return std::clamp(std::max(0, spice) / std::max(1, competitors) / 1500, 0, std::max(0, capacity));
}
inline int vanillaYardTarget(int credits, int harvesters) {
    const int economyTarget = std::min(credits / 4000, 1 + std::max(0, harvesters) / 8);
    const int cashTarget = 1 + std::max(0, credits) / 10000;
    return std::clamp(std::max(economyTarget, cashTarget), 1, 8);
}
inline int vanillaMcvShortfall(int credits, int harvesters, int yards, int pendingMcvs) {
    return std::max(0, vanillaYardTarget(credits, harvesters)
        - std::max(0, yards) - std::max(0, pendingMcvs));
}
// Credits here are net of queued production and the protected economy reserve.
inline bool prioritizeVanillaMcv(int credits, int harvesters, int yards, int pendingMcvs, int mcvPrice) {
    return mcvPrice > 0 && credits >= mcvPrice + 1000
        && vanillaMcvShortfall(credits, harvesters, yards, pendingMcvs) > 0;
}
inline int vanillaAttackThreshold(int configured, int difficulty) {
    return difficulty == 3 ? std::min(configured, 24000)
         : difficulty == 2 ? std::min(configured, 28000) : configured;
}
// Tank, siege, launcher, special, air. Keep learned preferences anchored to
// the configured combined-arms mix, and reserve at least 75% for ground units.
inline std::array<int, 5> balancedVanillaUnitMix(const std::array<int, 5>& observed,
                                               const std::array<int, 5>& configured) {
    auto normalize = [](std::array<int, 5> values) {
        int total = 0;
        for (auto& value : values) { value = std::max(0, value); total += value; }
        if (total == 0) return std::array<int, 5>{2500,2500,2500,2500,0};
        int assigned = 0;
        for (auto& value : values) { value = value * 10000 / total; assigned += value; }
        *std::max_element(values.begin(), values.end()) += 10000 - assigned;
        return values;
    };
    const auto learned = normalize(observed);
    const auto baseline = normalize(configured);
    std::array<int, 5> result;
    for (size_t i=0; i<result.size(); ++i) result[i] = learned[i] + baseline[i];
    result = normalize(result);
    if (result[4] > 2500) {
        const int ground = 10000 - result[4];
        int assigned = 0;
        for (size_t i=0; i<4; ++i) {
            result[i] = ground > 0 ? result[i] * 7500 / ground : 1875;
            assigned += result[i];
        }
        *std::max_element(result.begin(), result.begin()+4) += 7500-assigned;
        result[4] = 2500;
    }
    return result;
}
inline int vanillaFactoryTarget(int policyTarget, int harvesters, int credits) {
    // A starting grant can fund production before spice income catches up.
    // Retain the economy limit when cash is low; do not sell existing factories.
    const int economyTarget = std::max(1, harvesters / 3);
    const int cashTarget = 1 + std::max(0, credits - 10000) / 4000;
    return std::clamp(std::min(policyTarget, std::max(economyTarget, cashTarget)), 1, 24);
}
inline bool prioritizeVanillaFactory(int credits, int factoriesIncludingQueued,
                                     int yardsIncludingQueued, int target) {
    // Two factories per yard accelerate MCVs and troops without making the
    // first yard finish the entire factory target before advancing its tech.
    return credits >= 20000
        && factoriesIncludingQueued < std::min(target, 2 * std::clamp(yardsIncludingQueued, 1, 8));
}
}
#endif
