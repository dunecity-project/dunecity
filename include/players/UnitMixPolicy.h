#ifndef UNIT_MIX_POLICY_H
#define UNIT_MIX_POLICY_H
#include <algorithm>
#include <array>
#include <cstdint>
namespace UnitMixPolicy {
// Tank, siege, launcher, special, ornithopter, trike, raider trike, quad.
using Weights = std::array<int64_t, 8>;
using Mix = std::array<int, 8>;
// Full-match evidence is authoritative in House's saved combat counters.
// Keep the former window's serialized layout so existing saves can be read;
// update replaces even a loaded decayed window with the complete match totals.
struct PerformanceHistory {
    uint32_t sampled=0;
    bool initialized=false;
    Weights previousReward{},previousLoss{},reward{},loss{};
    void update(uint32_t cycle,const Weights& totals,const Weights& losses) {
        reward=totals; loss=losses;
        previousReward=totals; previousLoss=losses; sampled=cycle; initialized=true;
    }
    template<class Stream> void save(Stream& stream) const {
        stream.writeUint32(sampled); stream.writeBool(initialized);
        for (const auto* values:{&previousReward,&previousLoss,&reward,&loss})
            for (const auto value:*values) stream.writeSint64(value);
    }
    template<class Stream> void load(Stream& stream) {
        sampled=stream.readUint32(); initialized=stream.readBool();
        for (auto* values:{&previousReward,&previousLoss,&reward,&loss})
            for (auto& value:*values) value=stream.readSint64();
    }
};
inline int64_t performanceScore(int64_t damage, int64_t lostValue, int priorPrice) {
    return std::max<int64_t>(0, damage) * 1000000
        / std::max<int64_t>(1, std::max<int64_t>(0, lostValue) + std::max(1, priorPrice));
}
// Keep a fading trial prior for each available type, rather than starving
// an untested type when other types already have combat results.
inline Weights exploredScores(const Weights& scores, const Weights& rewards,
        const Weights& losses, const Weights& prices, const std::array<bool,8>& available) {
    int64_t prior = 0, count = 0;
    for (size_t i=0; i<8; ++i) if (available[i] && scores[i]>0) { prior+=scores[i]; ++count; }
    prior = count ? prior/count : 1000000;
    Weights result{};
    for (size_t i=0; i<8; ++i) if (available[i]) {
        const int64_t evidence = std::max<int64_t>(0,rewards[i]) + std::max<int64_t>(0,losses[i]);
        const int64_t uncertainty = std::max<int64_t>(1,prices[i])*4;
        result[i] = (scores[i]*evidence + prior*uncertainty)/(evidence+uncertainty);
    }
    return result;
}
inline Mix normalize(const Weights& weights) {
    int64_t total = 0;
    for (auto weight : weights) total += std::max<int64_t>(0, weight);
    Mix result{};
    if (total == 0) return result;
    int assigned = 0;
    for (size_t i=0; i<result.size(); ++i) {
        result[i] = static_cast<int>(std::max<int64_t>(0, weights[i]) * 10000 / total);
        assigned += result[i];
    }
    *std::max_element(result.begin(), result.end()) += 10000-assigned;
    return result;
}
// Integer square root keeps the 1.5 exponent identical on every platform.
inline uint64_t integerSqrt(uint64_t value) {
    uint64_t result=0, bit=uint64_t(1)<<62;
    while (bit>value) bit>>=2;
    while (bit) {
        if (value>=result+bit) { value-=result+bit; result=(result>>1)+bit; }
        else result>>=1;
        bit>>=2;
    }
    return result;
}
inline Weights sharpenScores(const Weights& scores) {
    // Normalise first to bound products even for a loss-free veteran type.
    const auto shares=normalize(scores);
    Weights weights{};
    for (size_t i=0;i<weights.size();++i)
        weights[i]=int64_t(shares[i])*integerSqrt(uint64_t(shares[i])*1000000);
    return weights;
}
// Opening light support shrinks as heavier technology becomes available.
inline int openingLightShare(int tech) {
    return tech >= 7 ? 400 : tech >= 5 ? 800 : tech >= 4 ? 1500 : 3000;
}
inline Mix openingMix(int tech, const Weights& configured, const std::array<bool,8>& available) {
    Weights heavy{}, light{};
    for (size_t i=0; i<8; ++i) if (available[i]) {
        if (i < 5) heavy[i] = std::max<int64_t>(0, configured[i]);
        else light[i] = i == 7 ? 2 : 1; // Prefer quads within the small support share.
    }
    // A zero configured ratio must not stall the only available production type.
    if (*std::max_element(heavy.begin(), heavy.end()) == 0)
        for (size_t i=0; i<5; ++i) if (available[i]) heavy[i] = 1;
    const Mix heavyMix = normalize(heavy), lightMix = normalize(light);
    const bool hasHeavy = *std::max_element(heavyMix.begin(), heavyMix.end()) > 0;
    const bool hasLight = *std::max_element(lightMix.begin(), lightMix.end()) > 0;
    const int lightBps = !hasLight ? 0 : !hasHeavy ? 10000 : openingLightShare(tech);
    Mix result{};
    auto distribute = [&](const Mix& shares, int budget) {
        if (budget == 0 || *std::max_element(shares.begin(), shares.end()) == 0) return;
        int assigned = 0;
        for (size_t i=0; i<8; ++i) {
            const int amount = shares[i]*budget/10000;
            result[i] += amount;
            assigned += amount;
        }
        result[std::max_element(shares.begin(), shares.end())-shares.begin()] += budget-assigned;
    };
    distribute(heavyMix,10000-lightBps);
    distribute(lightMix,lightBps);
    return result;
}
// The opening mix is a prior. Its influence fades as the value of units lost
// in combat grows relative to the army currently fielded. This gives early
// games a stable composition and lets a battle-tested army follow results.
inline int evidenceConfidenceBps(int64_t lostValue, int64_t armyValue) {
    const int64_t evidence = std::max<int64_t>(0, lostValue);
    const int64_t prior = std::max<int64_t>(1, armyValue);
    return static_cast<int>(evidence * 10000 / (evidence + prior));
}
inline Mix blendByEvidence(const Mix& baseline, const Mix& performance,
                           int64_t lostValue, int64_t armyValue) {
    const int confidence = evidenceConfidenceBps(lostValue, armyValue);
    Weights blend{};
    for (size_t i=0; i<blend.size(); ++i)
        blend[i] = int64_t(baseline[i]) * (10000-confidence)
                 + int64_t(performance[i]) * confidence;
    return normalize(blend);
}
inline Mix allocate(const Weights& scores, const Weights& defaults, bool learning, bool vanilla,
                    int64_t lostValue = 0, int64_t armyValue = 0) {
    const Mix baseline = normalize(defaults);
    Mix result = normalize(sharpenScores(scores));
    if (!learning || *std::max_element(result.begin(), result.end()) == 0) return baseline;
    if (vanilla) result = blendByEvidence(baseline, result, lostValue, armyValue);
    auto cap = [&](size_t index, int limit) {
        if (result[index] <= limit) return;
        Weights others{};
        for (size_t i=0; i<others.size(); ++i) if (i != index) others[i] = result[i];
        Mix shares = normalize(others);
        if (*std::max_element(shares.begin(), shares.end()) == 0) {
            for (size_t i=0; i<others.size(); ++i) if (i != index) others[i] = baseline[i];
            shares = normalize(others);
        }
        if (*std::max_element(shares.begin(), shares.end()) == 0) return;
        int assigned = limit;
        for (size_t i=0; i<result.size(); ++i) if (i != index) {
            result[i] = shares[i] * (10000-limit) / 10000;
            assigned += result[i];
        }
        result[index] = limit;
        const auto recipient = std::max_element(shares.begin(), shares.end()) - shares.begin();
        result[recipient] += 10000-assigned;
    };
    for (size_t i=0; i<result.size(); ++i) cap(i,8000);
    if (vanilla) cap(4,2500);
    return result;
}
/// Minimum mobile AA value share while visible hostile aircraft justify it.
/// This role budget is a tactical heuristic, not an optimal battle ratio.
constexpr int kMobileAntiAirFloorBps = 2000;

/// Raise one entry to a floor, funding it from the other entries in proportion
/// to their current share. Deterministic, order-independent and total-preserving
/// (the mix still sums to 10000). Existing upper caps are unaffected: the
/// floored entry only rises towards the floor and every other entry only falls.
inline Mix applyFloor(const Mix& mix, size_t index, int floorBps) {
    if(index >= mix.size() || floorBps <= 0 || mix[index] >= floorBps) return mix;
    int othersTotal = 0;
    for(size_t i = 0; i < mix.size(); ++i) if(i != index) othersTotal += std::max(0, mix[i]);
    if(othersTotal <= 0) return mix;        // Nothing to redistribute from.
    Mix result = mix;
    const int shortfall = floorBps - mix[index];
    result[index] = floorBps;
    int taken = 0;
    for(size_t i = 0; i < result.size(); ++i) {
        if(i == index) continue;
        const int cut = static_cast<int>(int64_t(shortfall) * std::max(0, mix[i]) / othersTotal);
        result[i] -= cut;
        taken += cut;
    }
    // Integer division leaves at most one basis point per other entry. Hand the
    // remainder to the largest remaining share, lowest index breaking ties, so
    // every peer computes the identical mix.
    for(int remaining = shortfall - taken; remaining > 0; --remaining) {
        size_t best = index;
        int bestShare = 0;
        for(size_t i = 0; i < result.size(); ++i)
            if(i != index && result[i] > bestShare) { bestShare = result[i]; best = i; }
        if(best == index) { result[index] -= remaining; break; }
        --result[best];
    }
    return result;
}

inline int64_t deficit(int targetBps, int armyValue, int currentCount, int price) {
    return int64_t(targetBps) * std::max(0, armyValue)
        - int64_t(std::max(0, currentCount)) * std::max(0, price) * 10000;
}
}
#endif
