#ifndef SIMPLE_ARMY_POLICY_H
#define SIMPLE_ARMY_POLICY_H
#include <algorithm>
#include <cstdint>
#include <vector>
#include <optional>
#include <utility>
namespace SimpleArmyPolicy {
// Stateless variation from saved simulation inputs. Every house gets the same
// 75–125% interval distribution instead of a permanently house-biased delay.
inline int attackDelay(int baseCycles,uint32_t seed,uint32_t cycle,uint32_t house) {
    uint32_t hash=seed ^ (cycle*0x9e3779b9u) ^ ((house+1)*0x85ebca6bu);
    hash^=hash>>16; hash*=0x7feb352du; hash^=hash>>15;
    hash*=0x846ca68bu; hash^=hash>>16;
    const int percent=75+int(hash%51);
    return int(std::clamp<int64_t>(int64_t(baseCycles)*percent/100,1,INT32_MAX));
}
struct Responder { uint32_t id; int value; int distance; };
inline int attackBudget(int armyValue, int percent) {
    return int(int64_t(std::max(0, armyValue)) * std::clamp(percent, 0, 100) / 100);
}
// The attack readiness a house can still reach. When its credits and all the spice left on the
// map together cannot buy the difference between its army and `required`, the army it has is
// the largest it will get: 0, so it attacks once a viable wave stands instead of waiting for a
// value it can never field (a spice-exhausted map held both houses at home for 35 minutes).
inline int reachableReadiness(int required, int militaryValue, int credits, int mapSpice) {
    const int64_t gap = int64_t(required) - std::max(0, militaryValue);
    return gap > int64_t(std::max(0, credits)) + std::max(0, mapSpice) ? 0 : required;
}
// A campaign wave shares its budget with troops already hunting. Rechecking
// readiness cannot gradually dispatch the reserve while the first wave lives.
inline std::vector<uint32_t> limitedAttack(int armyValue, int committed, int percent,
                                         std::vector<Responder> candidates) {
    const int budget = attackBudget(armyValue, percent);
    int value = std::max(0, committed);
    if (budget <= value) return {};
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });
    std::vector<uint32_t> selected;
    const Responder* cheapest = nullptr;
    for (const auto& candidate : candidates) {
        if (candidate.value <= 0) continue;
        if (!cheapest || candidate.value < cheapest->value) cheapest = &candidate;
        if (candidate.value > budget - value) continue;
        selected.push_back(candidate.id);
        value += candidate.value;
    }
    // A depleted army must still be able to send one unit. This exception never
    // adds a second oversized unit to an existing wave.
    if (selected.empty() && committed <= 0 && cheapest) selected.push_back(cheapest->id);
    return selected;
}
// A custom game commits the configured share of its own current ground army,
// and nothing else: no unit count and no absolute value ceiling. Survivors of
// earlier waves count against that share, so waves reinforce instead of stack.
// Strictly the percentage budget: a dispatch never overshoots, so an army too
// small for one more unit simply waits instead of attacking.
inline std::vector<uint32_t> customAttack(int armyValue,int committedValue,int percent,
                                          std::vector<Responder> candidates) {
    const int budget=attackBudget(armyValue,percent);
    int value=std::max(0,committedValue);
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        return a.id<b.id;
    });
    std::vector<uint32_t> selected;
    for (const auto& candidate : candidates) {
        if (candidate.value<=0 || candidate.value>budget-value) continue;
        selected.push_back(candidate.id); value+=candidate.value;
    }
    return selected;
}
inline int responseValue(int threat) { return std::max(0,threat) + (std::max(0,threat)+3)/4; }
// Prefer the nearest usable troops; existing responders count against the budget.
// The last unit may overshoot, but a small incident cannot requisition the army.
inline std::vector<uint32_t> reinforcements(int threat, int committed, std::vector<Responder> candidates) {
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        return a.distance!=b.distance ? a.distance<b.distance : a.id<b.id;
    });
    std::vector<uint32_t> selected;
    int value=std::max(0,committed);
    for (const auto& candidate:candidates) {
        if (value>=responseValue(threat)) break;
        if (candidate.value<=0) continue;
        selected.push_back(candidate.id); value+=candidate.value;
    }
    return selected;
}
// A proactive clearing mission needs a complete local response. Do not trickle
// a few distant troops into a defended spice field just because it is dangerous.
inline std::vector<uint32_t> clearingForce(int threat,int committed,std::vector<Responder> candidates,int radius) {
    candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const auto& c) {
        return c.distance>radius;
    }),candidates.end());
    int available=std::max(0,committed);
    for (const auto& c:candidates) available+=std::max(0,c.value);
    if (available<responseValue(threat)) return {};
    return reinforcements(threat,committed,std::move(candidates));
}
// Bounded local scatter, never a flood fill or an occupied centre fallback.
template<class Usable>
std::optional<std::pair<int,int>> rallyOffset(uint32_t id,int radius,Usable usable) {
    const int r=std::max(2,radius),width=2*r+1;
    for (int attempt=0;attempt<8;++attempt) {
        const int x=int((id*17+attempt*7)%width)-r,y=int((id*31+attempt*11)%width)-r;
        if (usable(x,y)) return std::pair<int,int>{x,y};
    }
    return std::nullopt;
}
}
#endif
