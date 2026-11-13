#ifndef CAMPAIGN_DIFFICULTY_POLICY_H
#define CAMPAIGN_DIFFICULTY_POLICY_H

#include <algorithm>
#include <cstdint>
#include <set>
#include <limits>

namespace CampaignDifficultyPolicy {
struct Profile {
    int houses, units, value, recoveryMs, graceMs, sortieMs, reservePercent;
    int enemyCommitPercent;
    bool limitedWave;
};
// Mission-scale trial values, in game time. Never grant free units/resources.
inline Profile profile(int difficulty, int tech) {
    const int stage = tech <= 3 ? 0 : tech <= 6 ? 1 : 2;
    switch (difficulty) {
        // Late Easy waves have a 1,700-credit ceiling; the separate
        // half-ready-army budget still retains defenders.
        case 0: return {1, 3+stage, stage==2 ? 1700 : (3+stage)*300, 180000-stage*30000, 120000, 150000, 25, 50, true};
        case 1: return {1, std::numeric_limits<int>::max(), 2500, 120000-stage*15000, 120000, 180000, 15, 100, true};
        case 2: return {2, std::numeric_limits<int>::max(), 3500, 75000-stage*15000, 120000, 240000, 10, 100, true};
        default:return {8, 16+stage*4, (16+stage*4)*700, 40000-stage*10000, 0, 300000, 5, 100, false};
    }
}
// Seeded offsets keep each house independent and deterministic across peers.
inline uint32_t staggerMs(uint32_t seed, uint32_t cycle, uint32_t house, uint32_t spanMs) {
    uint32_t value=seed ^ (house+1)*0x9e3779b9u ^ cycle*0x85ebca6bu;
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu; value ^= value >> 16;
    return value % (spanMs+1);
}
inline uint32_t openingDelayMs(int difficulty, uint32_t seed, uint32_t triggerCycle, uint32_t house) {
    return difficulty == 3 ? 0 : staggerMs(seed,triggerCycle,house,120000);
}
inline uint32_t repeatDelayMs(int difficulty, uint32_t seed, uint32_t cycle, uint32_t house) {
    // Easy, Medium and Hard each start an independent 1–3-minute countdown.
    return 60000u + staggerMs(seed,cycle,house,120000);
}
struct Wave {
    bool initialized = false;
    uint32_t opening = 0, launched = 0, lastActive = 0;
    uint32_t front = UINT32_MAX;
    std::set<uint32_t> members;
    template<class Stream> void save(Stream& s) const {
        s.writeBool(initialized); s.writeUint32(opening);
        s.writeUint32(launched); s.writeUint32(lastActive); s.writeUint32(front);
        s.writeUint32(static_cast<uint32_t>(members.size()));
        for (auto id : members) s.writeUint32(id);
    }
    template<class Stream> void load(Stream& s) {
        initialized=s.readBool(); opening=s.readUint32();
        launched=s.readUint32(); lastActive=s.readUint32(); front=s.readUint32();
        members.clear(); const auto n=s.readUint32();
        for (uint32_t i=0;i<n;++i) members.insert(s.readUint32());
    }
};
struct Pressure { int houses=0, units=0, value=0; uint32_t lastActive=0; };
inline bool canLaunch(const Profile& p, const Pressure& used, uint32_t now,
                      uint32_t opening, uint32_t recoveryCycles) {
    return now >= opening && used.houses < p.houses
        && (!p.limitedWave || (used.units < p.units && used.value < p.value))
        && (used.houses > 0 || used.lastActive == 0
            || now-used.lastActive >= recoveryCycles);
}
inline bool fits(const Profile& p, const Pressure& used, int value) {
    return value > 0 && (!p.limitedWave || (used.units < p.units && value <= p.value-used.value));
}
inline bool needsWindtrap(int produced, int required, int queuedDemand, int nextDemand,
                          bool generatorPending) {
    return !generatorPending && produced < required + std::max(0,queuedDemand) + std::max(0,nextDemand);
}
}
#endif
