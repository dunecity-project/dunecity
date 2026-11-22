#ifndef EASY_RELIEF_POLICY_H
#define EASY_RELIEF_POLICY_H

#include <algorithm>
#include <cstdint>

// EasyRelief ([Force Concentration], default 0 = off): a gentler Easy opponent in custom games
// without the city simulation, where live results show ordinary players stuck. Medium, Hard and
// Brutal, every campaign, helpers and city games never read these rules. Three campaign rules
// for levels 8-9 were tested and removed (docs/campaign-ai-difficulty-matrix.md, "Easy relief").
namespace EasyReliefPolicy {

/// The switch's three levels. The shipped candidate is kAgainstHumans: games without a human
/// (the AI regression gates, the difficulty ladder) play exactly as with the switch off.
/// kEveryOpponent lets AI test games measure the same rules with an AI as the player.
constexpr int kOff = 0, kAgainstHumans = 1, kEveryOpponent = 2;
inline bool applies(int scope, bool opposesHuman) {
    return scope == kEveryOpponent || (scope == kAgainstHumans && opposesHuman);
}

/// The army value an Easy custom opponent builds up to: \a percent of its limit for the map size.
inline int customArmyCeiling(int nativeLimit, int percent) {
    return int(int64_t(std::max(0, nativeLimit)) * percent / 100);
}

/// A wave must stay launchable under the lower ceiling: never wait for more than four fifths of it.
inline int readiness(int required, int ceiling) {
    return ceiling > 0 ? std::min(required, ceiling * 4 / 5) : required;
}

/// Harvesters an Easy custom opponent keeps: \a percent of the configured ceiling, rounded up,
/// at least two. 0 (no configured ceiling on this map size) stays 0.
inline int harvesterCeiling(int configured, int percent) {
    return configured <= 0 ? configured : std::max(2, (configured * percent + 99) / 100);
}

/// No attack leaves an Easy custom opponent's base before this game minute.
inline bool attackHeld(uint32_t cycle, uint32_t firstAttackCycle) { return cycle < firstAttackCycle; }

} // namespace EasyReliefPolicy

#endif
