#ifndef QUANTBOT_VANILLA_POLICY_H
#define QUANTBOT_VANILLA_POLICY_H

#include <algorithm>
#include <cstdint>

namespace QuantBotVanillaPolicy {

enum class Plan { None, Harvester, Refinery, GreedyEconomy, GreedyArmy, ReactiveEconomy, ReactiveArmy };

// Receipts are written by ordinary AI planning, never by the pure observer, and do not feed decisions.
struct Observation {
    Plan plan{Plan::None};
    int stopMinute{};
    int64_t ownArmy{}, enemyArmy{}, nearArmy{}, ownEconomy{}, leaderEconomy{};
    int armyRatioBps{}, economyRatioBps{};
    int64_t reachableSpiceShare{}, refineryCap{}, harvesterCap{};
    int currentWorkers{};
};

struct Targets { int workers{}, refineries{}, factories{}; };
struct SpiceCaps { int64_t refineries{}, workers{}; };

inline int spicePerRefineryLevel(int value) { return value == 8000 ? 8000 : 4000; }
inline SpiceCaps spiceCaps(int64_t share, int perRefinery) {
    share = std::max<int64_t>(0, share);
    const int level = spicePerRefineryLevel(perRefinery);
    return {share / level, 2 * share / level};
}
// Split each tile between equally near structures before summing a house's claims.
inline int64_t spiceTileShare(int64_t spice, int nearest, int ownNearest) {
    return ownNearest > 0 ? spice / nearest * ownNearest : 0;
}
inline Plan spiceGatedPlan(Plan plan, const SpiceCaps& cap, int refineries, int workers) {
    if (plan == Plan::Refinery && cap.refineries <= refineries) return Plan::None;
    if (plan == Plan::Harvester && cap.workers <= workers) return Plan::None;
    return plan;
}
inline Targets capSpiceTargets(Targets target, const SpiceCaps& cap) {
    target.refineries = int(std::min<int64_t>(target.refineries, cap.refineries));
    target.workers = int(std::min<int64_t>(target.workers, cap.workers));
    return target;
}

// Count the original occupied controllers. An asymmetric two-team survival game is not an FFA.
inline bool rosterScope(int allies, int enemies, bool severalEnemyTeams) {
    return allies > 0 && enemies > 0 && (severalEnemyTeams || enemies <= allies);
}
inline bool boomScope(bool ffaOnly, int enemies, bool severalEnemyTeams) {
    return !ffaOnly || (enemies >= 3 && severalEnemyTeams);
}

inline Plan timedPlan(Plan kind, unsigned minute, int greedyStop, bool refineryGreedy) {
    // The compound arm keeps the refinery opening, then uses the existing greedy conversion.
    if (kind == Plan::Refinery && refineryGreedy)
        return minute < unsigned(greedyStop) ? Plan::Refinery : minute < 20 ? Plan::GreedyArmy : Plan::None;
    if (kind == Plan::Harvester || kind == Plan::Refinery) return minute < 10 ? kind : Plan::None;
    if (kind == Plan::GreedyEconomy)
        return minute < unsigned(greedyStop) ? Plan::GreedyEconomy : minute < 20 ? Plan::GreedyArmy : Plan::None;
    return kind;
}
inline bool armyPhase(Plan plan) {
    return plan == Plan::GreedyArmy || plan == Plan::ReactiveArmy;
}
inline Targets targets(Plan plan, int harvesterWorkers, int refineryCount, int reactiveWorkers, int sustainable) {
    Targets result;
    switch (plan) {
        case Plan::Harvester: result = {harvesterWorkers, 2, 1}; break;
        case Plan::Refinery: result = {2 * refineryCount, refineryCount, 1}; break;
        case Plan::GreedyEconomy: result = {16, 6, 2}; break;
        case Plan::ReactiveEconomy: result = {reactiveWorkers, (reactiveWorkers + 2) / 3, 2}; break;
        case Plan::GreedyArmy: case Plan::ReactiveArmy: result = {4, 2, 1}; break;
        default: return result;
    }
    result.workers = std::clamp(result.workers, 0, std::max(0, sustainable));
    result.refineries = std::min(result.refineries, std::max(1, (result.workers + 1) / 2));
    return result;
}
inline bool deferTech(Plan plan, int workers, int refineries, int factories, const Targets& target) {
    return plan == Plan::GreedyEconomy || armyPhase(plan) || workers < target.workers
        || refineries < target.refineries || factories < target.factories;
}
inline bool reactiveArmy(int64_t ownArmy, int64_t reachableEnemyArmy, int64_t nearbyEnemyArmy,
                         int64_t ownEconomy, int64_t leaderEconomy, int armyRatioBps, int economyRatioBps) {
    return nearbyEnemyArmy > ownArmy || ownArmy * 10000 < reachableEnemyArmy * armyRatioBps
        || ownEconomy * 10000 >= leaderEconomy * economyRatioBps;
}
inline int readiness(Plan plan) {
    return plan == Plan::GreedyEconomy || plan == Plan::GreedyArmy ? 150 : 0;
}
inline const char* phaseName(Plan plan) {
    switch (plan) {
        case Plan::Harvester: return "harvester";
        case Plan::Refinery: return "refinery";
        case Plan::GreedyEconomy: return "greedy_economy";
        case Plan::GreedyArmy: return "greedy_army";
        case Plan::ReactiveEconomy: return "reactive_economy";
        case Plan::ReactiveArmy: return "reactive_army";
        default: return "off";
    }
}

} // namespace QuantBotVanillaPolicy

#endif
