#ifndef FRONT_BATTERY_POLICY_H
#define FRONT_BATTERY_POLICY_H

/*
    Enemy-facing rocket batteries for Custom Hard/Brutal QuantBot.

    This sits on top of the established defence rules and replaces nothing:
    critical-asset first cover, the three-turret minimum for an outlying colony
    and the counter-aircraft goal all keep their existing meaning and their
    existing priority. What is added is a demand-scaled battery goal that does
    not depend on the enemy owning aircraft, so a base facing a purely ground
    opponent stops capping itself at the two baseline emplacements.

    The goal is deliberately expressed through the coverage demand the base
    already computes (RocketTurretPolicy::coverageTurretCap), because that figure
    already scales with base size and with the difficulty's overlap tier: a small
    base lands in the provisional 8-12 band and a mature one in the 24-plus band
    without a second hand-tuned table. It is a goal, not a universal maximum.

    Siting rules are geometric and integer only. Nothing here reads a weapon
    damage table, infers a combat weight from a past match, or models line of
    sight: this engine's turrets acquire by range, so coverage is range and
    corner geometry, which RocketTurretPolicy already implements.
*/

#include <algorithm>
#include <cstdint>

namespace FrontBatteryPolicy {

/// Provisional. A battery is only started once the economy that pays for it
/// exists, so turrets never outrank the refinery/factory growth that funds them.
inline bool economyReady(int refineries, int heavyFactories, int repairYards) {
    return refineries >= 2 && heavyFactories >= 1 && repairYards >= 1;
}

/// Provisional floor and ceiling of the opening band, used only to keep a very
/// small coverage demand from producing a one-turret "battery" once the economy
/// gate has opened. The upper bound is not a cap on mature bases: the demand
/// term below is free to exceed it.
constexpr int kOpeningBandMin = 8;
constexpr int kOpeningBandMax = 12;

/// How much an observed front disadvantage may raise the goal, in basis points
/// of the demand-derived figure. Provisional: at most half again.
constexpr int kFrontThreatBoostDivisorBps = 20000;

/// Demand-scaled battery goal.
///
/// `coverageCap` is RocketTurretPolicy::coverageTurretCap(coverageDemand).
/// `frontThreatBps` is observed hostile front power as a share of our own
/// holding power, clamped by the caller to [0, 10000]; it is an honest
/// observation proxy, not a predicted battle outcome.
inline int batteryGoal(int coverageCap, bool economyReady, int frontThreatBps, bool enabled = true) {
    if (!enabled || !economyReady) return 0;
    int goal = std::max(0, coverageCap);
    goal = std::max(goal, kOpeningBandMin);
    if (goal < kOpeningBandMax && std::max(0, coverageCap) < kOpeningBandMin)
        goal = kOpeningBandMin; // A tiny base still gets a usable opening battery.
    goal += int(int64_t(goal) * std::clamp(frontThreatBps, 0, 10000) / kFrontThreatBoostDivisorBps);
    return goal;
}

/// Share of the goal that may stand on the enemy-facing half-plane. The rest is
/// left for flank and rear cover, so a second front is not undefended.
/// Provisional two thirds.
constexpr int kFrontShareBps = 6667;
inline int frontAllowance(int goal) {
    return int(int64_t(std::max(0, goal)) * kFrontShareBps / 10000);
}

/// Is this offset from the defended anchor on the observed enemy side?
/// Dot product of the candidate offset with the forward direction.
inline bool onFrontSide(int dx, int dy, int forwardX, int forwardY) {
    return int64_t(dx) * forwardX + int64_t(dy) * forwardY > 0;
}

/// Provisional spacing. Two tiles keeps the battery compact and heavily
/// overlapping - a rocket turret out-ranges this many times over - while
/// stopping one splash, Death Hand or Devastator from taking several at once,
/// and stopping a solid diagonal wall from forming.
constexpr int kMinSpacing = 2;
inline bool spacedEnough(int chebyshevToNearestTurret, int minSpacing = kMinSpacing) {
    return chebyshevToNearestTurret < 0 || chebyshevToNearestTurret >= minSpacing;
}

/// Provisional local density: a belt, not a block. Measured inside half the
/// emplacement's own weapon range, so the limit scales with the mod's data.
constexpr int kMaxCluster = 6;
inline int clusterRadius(int weaponRange) { return std::max(2, weaponRange / 2); }
inline bool withinClusterLimit(int neighboursInClusterRadius, int maxCluster = kMaxCluster) {
    return neighboursInClusterRadius < maxCluster;
}

/// Movement corridor margin: a candidate must leave this many passable tiles in
/// its own eight-neighbourhood. GroundAccessPolicy already protects local
/// connectivity and factory exits; this additionally stops a battery from
/// narrowing every lane to the single diagonal that check tolerates.
constexpr int kMinCorridorNeighbours = 3;
inline bool keepsCorridor(int passableNeighbours, int minimum = kMinCorridorNeighbours) {
    return passableNeighbours >= minimum;
}

/// Mutual support: past the opening pair, a new emplacement must itself be
/// covered by an existing or already reserved one.
inline bool mutuallySupported(int existingTurrets, bool coveredByAnotherTurret) {
    return existingTurrets < 2 || coveredByAnotherTurret;
}

/// Orders and spend this construction pass. A battery is built over minutes,
/// not in one cash dump, so growth keeps its share of the yards.
constexpr int kOrdersPerPass = 1;
inline bool mayOrderThisPass(int orderedThisPass, int limit = kOrdersPerPass) {
    return orderedThisPass < limit;
}

/*
    Clearance fallback.

    An established city eventually has no free legal ground left on the side
    the enemy comes from, and the battery then simply stops growing where it is
    needed most. The fallback is to displace one of this house's own R/C/I lots
    - the same thing the established redevelopment rule already does for a
    factory, a reactor or a windtrap - and put the emplacement on the ground it
    frees.

    It is a last resort, not a preference: the ordinary free-site search runs
    first and the fallback is only consulted when that search finds nothing. An
    emplacement is one tile, so at most one 2x2 lot is ever displaced per order,
    and the one-order-per-pass rule above already bounds how often that happens.

    Displacement cost prefers empty, less developed and less valuable lots.
    Modest growth does not invalidate a funded project; mature lots require a
    bounded local population/job share. Local zone floors protect the colony.
    Civic overlays (hospital, church) are never displaced.
*/

/// Modest first-stage development can still give way to a needed battery.
/// Requiring a lot to remain empty throughout construction would cancel the
/// project whenever ordinary city growth happens first. More developed lots
/// may be displaced only when they represent at most one fifth of this
/// colony's residential population or jobs of that type.
constexpr int kClearanceModestDensity = 1;
constexpr int kClearanceModestResidents = 8;
constexpr int kClearanceEconomicShareBps = 2000;
/// Lots of the displaced type that must remain afterwards *in the colony that
/// is building the battery*. A house-wide count is not an economic floor at
/// all: a second colony on the far side of the map would mask the loss of the
/// last residential lot standing next to this one. The neighbourhood the
/// caller counts over is the colony geometry the belt itself already uses -
/// two emplacement weapon ranges from the battery anchor - so "local" means
/// the same thing in both places.
constexpr int kClearanceTypeFloor = 3;
/// Lots of all three types that must remain in that same neighbourhood. A
/// colony may be legitimately short of one type; it may not be stripped.
constexpr int kClearanceLocalTotalFloor = 6;
/// Lots displaced per construction pass, across every yard. One emplacement
/// covers one tile, so a single order can never reach a second lot, but
/// several yards can each hold a clearance reservation; the executing side
/// enforces this bound itself rather than trusting the chooser.
constexpr int kClearanceLotsPerPass = 1;

/// Services are protected. Prefer low displacement cost in the chooser; the
/// execution guard preserves a meaningful local population/job share.
inline bool clearableLot(int density, int residents, bool civicOverlay,
                         int economicPopulation, int localEconomicPopulation) {
    if (civicOverlay) return false;
    if (density <= kClearanceModestDensity && residents <= kClearanceModestResidents) return true;
    return int64_t(std::max(1,economicPopulation))*10000
        <= int64_t(std::max(0,localEconomicPopulation))*kClearanceEconomicShareBps;
}

/// Would displacing one lot leave this colony a working local economy? Both
/// counts are of lots inside the colony neighbourhood, including the lot about
/// to go.
inline bool preservesLocalZoneFloor(int localLotsOfThisType, int localLotsOfAllTypes,
                                    int typeFloor = kClearanceTypeFloor,
                                    int totalFloor = kClearanceLocalTotalFloor) {
    return localLotsOfThisType - 1 >= typeFloor && localLotsOfAllTypes - 1 >= totalFloor;
}

} // namespace FrontBatteryPolicy

#endif // FRONT_BATTERY_POLICY_H
