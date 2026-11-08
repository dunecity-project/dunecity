#ifndef QUANTBOT_VANILLA_POLICY_H
#define QUANTBOT_VANILLA_POLICY_H

#include <algorithm>

namespace QuantBotVanillaPolicy {

enum class Plan { None, Boom, Expansion, Foundry, Neighbour, Tech, AdaptiveBoom, AdaptiveWar };

// FFA rivals also fight one another; an outnumbered two-team survival match is a different mode.
inline bool rosterScope(int allies, int enemies, bool severalEnemyTeams) {
    return allies > 0 && enemies > 0 && (severalEnemyTeams || enemies <= allies);
}

// Opening commitments expire; an unsuccessful opening cannot permanently prevent recovery.
inline bool opening(Plan plan, unsigned minute, bool ixOwned) {
    return plan != Plan::None && minute < (plan == Plan::Neighbour ? 8u : 12u)
        && !(plan == Plan::Tech && ixOwned);
}
inline int workers(Plan plan, int sustainable) {
    const int target = plan == Plan::Boom ? 16 : plan == Plan::Expansion ? 12
        : plan == Plan::Foundry ? 8 : plan == Plan::Neighbour ? 4
        : plan == Plan::AdaptiveBoom ? 12 : 6;
    return std::clamp(target, 0, std::max(0, sustainable));
}
inline int refineries(Plan plan, int workerTarget) {
    const int target = plan == Plan::Boom || plan == Plan::Expansion ? 6
        : plan == Plan::AdaptiveBoom ? 4 : plan == Plan::Neighbour || plan == Plan::AdaptiveWar ? 2 : 3;
    return std::min(target, std::max(1, (workerTarget + 1) / 2));
}
inline int factories(Plan plan) {
    return plan == Plan::Foundry ? 3 : plan == Plan::Boom || plan == Plan::Expansion
        || plan == Plan::AdaptiveBoom || plan == Plan::AdaptiveWar ? 2 : 1;
}
inline bool deferTech(Plan plan, int workersOwned, int workerTarget, int yards, int factoriesOwned) {
    switch (plan) {
        case Plan::Boom: return workersOwned < workerTarget || factoriesOwned < 2;
        case Plan::Expansion: return yards < 2 || factoriesOwned < 2;
        case Plan::Foundry: return factoriesOwned < 3;
        case Plan::Neighbour: case Plan::AdaptiveWar: return true;
        case Plan::AdaptiveBoom: return workersOwned < workerTarget;
        default: return false;
    }
}
inline int readiness(Plan plan) {
    return plan == Plan::Neighbour ? 40 : plan == Plan::Boom || plan == Plan::Foundry ? 150 : 100;
}
inline Plan adaptive(int spiceShare, int nearestKnownEnemy, int enemyArmy, int ownArmy) {
    return spiceShare >= 60000 && nearestKnownEnemy >= 48 && enemyArmy <= ownArmy
        ? Plan::AdaptiveBoom : Plan::AdaptiveWar;
}

} // namespace QuantBotVanillaPolicy

#endif
