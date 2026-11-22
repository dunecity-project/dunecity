#ifndef EASY_RELIEF_POLICY_H
#define EASY_RELIEF_POLICY_H

#include <algorithm>
#include <cstdint>

// EasyRelief ([Force Concentration], default off): a gentler Easy opponent in the two places
// live results show ordinary players stuck - campaign levels 8 and 9, and custom games without
// the city simulation. Medium, Hard and Brutal, campaign levels 1-7, helpers and city games
// never read these rules (docs/campaign-ai-difficulty-matrix.md, "Easy relief"). Its waves and
// their timing stay native: later and smaller campaign waves were tested and removed.
namespace EasyReliefPolicy {

/// Campaign missions 20 and 21 are level 8, mission 22 is level 9.
constexpr int kFirstCampaignMission = 20;
/// The late-mission army floor Easy already has (QuantBot.cpp, "Mission 21+ override").
constexpr int kCampaignArmyFloor = 2000;

inline bool campaignMission(int mission) { return mission >= kFirstCampaignMission; }

/// The army value an Easy campaign opponent replaces its losses up to: \a percent of the army
/// the mission started it with, at least the late-mission floor, never above its native limit
/// (twice the starting army).
inline int campaignArmyCeiling(int startingArmy, int nativeLimit, int percent) {
    const int scaled = int(int64_t(std::max(0, startingArmy)) * percent / 100);
    return std::min(std::max(0, nativeLimit), std::max(kCampaignArmyFloor, scaled));
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
