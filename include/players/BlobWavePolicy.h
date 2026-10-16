#ifndef BLOB_WAVE_POLICY_H
#define BLOB_WAVE_POLICY_H

/*
    QuantBot hunt parties that gather, march as one blob, release into engine Hunt on contact,
    reform after the fight and fall back a short way to reform when locally outmatched
    (docs/formations.md, "Blob waves").

    Everything here is pure integer arithmetic on values the caller observes from the
    simulation, so two peers deciding on the same cycle reach the same answer. No container
    iteration order, no clock, no random source.
*/

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace BlobWavePolicy {

/// What a hunt party is doing. Saved (SAVEGAMEVERSION 9862) as its underlying value.
enum class Phase : uint8_t {
    Gather = 0,     ///< members walk to the rally point; the party waits for them
    March = 1,      ///< the blob advances leg by leg; the fast members wait at each leg's end
    Fight = 2,      ///< released on contact: every member is an engine hunter
    Reform = 3,     ///< after a fight or a fall-back: members close up on one point
    Fallback = 4    ///< locally outmatched: the party withdraws a few tiles to reform
};

inline bool validPhase(unsigned value) { return value <= unsigned(Phase::Fallback); }

inline const char* phaseName(Phase phase) {
    switch (phase) {
        case Phase::Gather:   return "gather";
        case Phase::March:    return "march";
        case Phase::Fight:    return "fight";
        case Phase::Reform:   return "reform";
        case Phase::Fallback: return "fallback";
    }
    return "unknown";
}

// ---- Tunables (code defaults; not in the shipped QuantBot config) ---------------------------

/// A party needs this many members; below it the members hunt on their own (as None).
constexpr int kMinMembers = 3;
/// A gather or reform ends when this share of the members stands at the point ...
constexpr int kGatherQuorumBps = 9000;
/// ... or when the members still away have all stopped (blocked), none has arrived for
/// kGatherStallMs and at least this share is there ...
constexpr int kGatherStallQuorumBps = 6000;
constexpr int kGatherStallMs = 8000;
/// ... or at the latest after the members' travel time to the point (see travelLimit), so a
/// blocked or distant member never stalls the offensive: it catches up on the march.
constexpr int kGatherMaxMs = 60000;
constexpr int kReformMaxMs = 30000;
/// The rally point lies this far from the members' anchor towards the objective.
constexpr int kGatherAdvanceTiles = 6;
/// A fight is over when no enemy has been within reach of a member and no member has been hit
/// for this long.
constexpr int kQuietMs = 6000;
/// A blob whose anchor gets no closer to the objective for this long is released to Hunt.
constexpr int kMarchStallMs = 60000;
/// A leg ends when this share of the members that are not stragglers stands at its end ...
constexpr int kLegQuorumBps = 9000;
/// ... or after the members' travel time to it (see travelLimit).
constexpr int kLegMaxMs = 90000;
/// A gather, reform or leg waits at most the slowest member's travel time times this factor
/// (percent) plus kTravelSlackMs, never less than kTravelMinMs and never more than its cap.
constexpr int kTravelTimePercent = 150;
constexpr int kTravelSlackMs = 4000;
constexpr int kTravelMinMs = 8000;
/// The tactical fall-back: how far, how long at most, and how often per party.
constexpr int kFallbackTiles = 8;
constexpr int kFallbackMaxMs = 20000;
constexpr int kFallbackArrivedBps = 7000;
constexpr int kMaxFallbacks = 3;
/// Local comparison around the party's anchor (the posture policy's 1.3x and 2.0x).
constexpr int kLocalRadiusTiles = 10;
constexpr int kOutmatchBps = 13000;
constexpr int kSevereBps = 20000;
constexpr int kOutmatchPersistMs = 4000;

// ---- Geometry and decisions ------------------------------------------------------------------

/// Radius (tiles) a blob of this many members needs: two members per squared tile, as the
/// army assembly radius (QuantBot::assemblyRadius) is derived.
inline int blobRadius(int members) {
    int radius = 2;
    while (radius * radius * 2 < std::max(1, members)) ++radius;
    return radius;
}

/// One march leg (tiles): long enough that a large blob is not stopping every few tiles,
/// short enough that its fastest member is never far ahead of its slowest.
inline int legTiles(int members) { return std::max(8, 2 * blobRadius(members)); }

/// A member further than two legs plus the blob radius from the current leg's end is a
/// straggler: the leg does not wait for it (it is sent to every leg end and catches up there).
inline bool straggler(int distanceToLegEnd, int leg, int radius) {
    return distanceToLegEnd > 2 * leg + radius;
}

/// Has the blob reached the end of this leg? \a counted members are not stragglers, \a arrived
/// of them stand at the leg's end; \a elapsed and \a maxCycles in cycles.
inline bool legComplete(int counted, int arrived, uint32_t elapsed, uint32_t maxCycles,
                        int quorumBps = kLegQuorumBps) {
    if (counted <= 0 || elapsed >= maxCycles) return true;
    return int64_t(arrived) * 10000 >= int64_t(quorumBps) * counted;
}

/// The time limit (cycles) of a gather, reform or leg whose slowest member needs
/// \a travelCycles to get there; \a maxMs is the phase's cap.
inline uint32_t travelLimit(int64_t travelCycles, int64_t cyclesPerSecond, int maxMs) {
    const int64_t limit = std::max<int64_t>(0, travelCycles) * kTravelTimePercent / 100
        + int64_t(kTravelSlackMs) * cyclesPerSecond / 1000;
    return uint32_t(std::clamp<int64_t>(limit, int64_t(kTravelMinMs) * cyclesPerSecond / 1000,
                                        int64_t(maxMs) * cyclesPerSecond / 1000));
}

/// Is a gather (or reform) over? \a elapsed since it started, \a sinceArrival since the
/// gathered count last grew; all in cycles. \a awayMoving: some member that is not there yet
/// is still on its way, so a slow arrival is not a stall.
inline bool gatherComplete(int members, int gathered, uint32_t elapsed, uint32_t sinceArrival,
                           uint32_t maxCycles, uint32_t stallCycles, bool awayMoving,
                           int quorumBps = kGatherQuorumBps) {
    if (members <= 0) return true;
    if (int64_t(gathered) * 10000 >= int64_t(quorumBps) * members) return true;
    if (elapsed >= maxCycles) return true;
    return !awayMoving && sinceArrival >= stallCycles
        && int64_t(gathered) * 10000 >= int64_t(kGatherStallQuorumBps) * members;
}

/// Is a fall-back over? Enough members stand at the fall-back point, or it took too long.
inline bool fallbackComplete(int members, int arrived, uint32_t elapsed, uint32_t maxCycles) {
    return members <= 0 || elapsed >= maxCycles || int64_t(arrived) * 10000 >= int64_t(kFallbackArrivedBps) * members;
}

/// The posture policy's local comparison: the enemy here is at least \a bps of us.
inline bool outmatched(int64_t friendly, int64_t hostile, int bps) {
    return hostile > 0 && hostile * 10000 >= int64_t(bps) * std::max<int64_t>(1, friendly);
}

/// One integer step of exactly \a tiles (Chebyshev) from (0,0) along (dx,dy); (0,0) stays.
inline std::pair<int, int> scaled(int dx, int dy, int tiles) {
    const int length = std::max(std::abs(dx), std::abs(dy));
    if (length == 0) return {0, 0};
    // Round half away from zero, symmetric for both signs.
    auto part = [&](int value) {
        const int64_t scaledValue = int64_t(value) * tiles;
        return int(scaledValue >= 0 ? (scaledValue + length / 2) / length : -((-scaledValue + length / 2) / length));
    };
    return {part(dx), part(dy)};
}

/// Offset of the tactical fall-back point from the party's anchor: \a tiles away from the
/// threat, bent towards home. Away from the threat counts twice as much as towards home,
/// so a party never falls back through the enemy, and with no threat known it goes home.
inline std::pair<int, int> fallbackOffset(int anchorX, int anchorY, int threatX, int threatY,
                                          bool threatKnown, int homeX, int homeY, bool homeKnown, int tiles) {
    const auto away = threatKnown ? scaled(anchorX - threatX, anchorY - threatY, 100) : std::pair<int, int>{0, 0};
    const auto home = homeKnown ? scaled(homeX - anchorX, homeY - anchorY, 100) : std::pair<int, int>{0, 0};
    return scaled(2 * away.first + home.first, 2 * away.second + home.second, tiles);
}

/// Offset of the rally point from the members' anchor: up to kGatherAdvanceTiles towards the
/// objective, never more than a third of the way.
inline std::pair<int, int> gatherOffset(int anchorX, int anchorY, int goalX, int goalY) {
    const int distance = std::max(std::abs(goalX - anchorX), std::abs(goalY - anchorY));
    return scaled(goalX - anchorX, goalY - anchorY, std::min(kGatherAdvanceTiles, distance / 3));
}

/// Offset of the next leg's end from the anchor: one leg towards the goal, or all the way.
inline std::pair<int, int> legOffset(int anchorX, int anchorY, int goalX, int goalY, int leg) {
    const int distance = std::max(std::abs(goalX - anchorX), std::abs(goalY - anchorY));
    return scaled(goalX - anchorX, goalY - anchorY, std::min(leg, distance));
}

} // namespace BlobWavePolicy

#endif // BLOB_WAVE_POLICY_H
