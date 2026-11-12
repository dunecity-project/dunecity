#ifndef ROCKET_TURRET_POLICY_H
#define ROCKET_TURRET_POLICY_H
#include <dunecity/CityEffects.h>
#include <tuple>
#include <mmath.h>

namespace RocketTurretPolicy {
inline int defenseWeight(int item) {
    if (item == Structure_NuclearPlant) return 2;
    // Every real building needs protection, including outlying R/C/I districts.
    // Surface tiles and defensive emplacements do not recursively demand turrets.
    if (!isStructure(item) || item == Structure_Slab1 || item == Structure_Slab4
        || item == Structure_Road || item == Structure_PowerLine || item == Structure_Wall
        || item == Structure_GunTurret || item == Structure_RocketTurret) return 0;
    return 1;
}
inline bool coversBuilding(Coord turret, Coord origin, Coord size, int range) {
    // All corners must fit the actual octile weapon range. A square radius
    // around the centre incorrectly counts exposed diagonal edges as covered.
    for(int y : {0,size.y-1}) for(int x : {0,size.x-1})
        if(blockDistance(turret,Coord(origin.x+x,origin.y+y))>range) return false;
    return true;
}
// Losing one of these ends the city's ability to rebuild, to refine or to
// produce, so they are worth overlapping cover; everything else is replaceable
// while the core stands.
inline bool coreAsset(int item) {
    return item == Structure_ConstructionYard || item == Structure_Refinery
        || item == Structure_HeavyFactory || item == Structure_LightFactory
        || item == Structure_HighTechFactory || item == Structure_StarPort
        || item == Structure_RepairYard || item == Structure_NuclearPlant;
}
// How many turrets should have a core asset in range. Tier is 0 Easy,
// 1 Medium, 2 Hard, 3 Brutal: a single emplacement dies to the first raid that
// focuses it, so from Medium up the core keeps a second one, and Brutal a
// third. From Medium up, city districts also get overlapping coverage as they
// grow, so routine defence does not end at the core. The reactor keeps a two-turret
// minimum at every difficulty because its loss damages the whole district.
// A new outlying construction yard is the most exposed building a city owns:
// it stands beyond the base's existing emplacements, it is what rebuilds the
// expansion, and a wing of ornithopters reaches it long before ground defence
// can. Every expansion therefore demands the full three-turret
// ring rather than the ordinary core overlap.
inline int expansionYardCoverage(int) { return 3; }
inline int desiredCoverage(int item, int tier, bool newExpansion = false) {
    if (defenseWeight(item) <= 0) return 0;
    const int core = tier <= 0 ? 1 : tier >= 3 ? 3 : 2;
    int coverage = coreAsset(item) ? core : (tier <= 0 ? 1 : 2);
    if (item == Structure_NuclearPlant) coverage = std::max(coverage, 2);
    if (newExpansion && item == Structure_ConstructionYard)
        coverage = std::max(coverage, expansionYardCoverage(tier));
    return coverage;
}
// Weight each core asset as three ordinary buildings when choosing coverage.
// This favours rebuild and income capacity while still valuing dense districts.
inline int assetPriority(int item) {
    if (defenseWeight(item) <= 0) return 0;
    return coreAsset(item) ? 3 : 1;
}
// A goal counted from the enemy's wing size stops while whole districts are
// still uncovered, and the interim cap of two emplacements left large bases
// exposed until optional tech finished. Scale the interim ceiling with the
// coverage the base actually demands: a two-turret baseline plus one per four
// demanded covers. It is a ceiling, never a build target on its own.
inline int coverageTurretCap(int coverageDemand) {
    return 2 + std::max(0, coverageDemand) / 4;
}
// A modest early battery, also bounding ordinary cover while optional core/tech
// work finishes. Small established bases get one extra; only substantially
// larger Hard+ bases get a second. Funding and growth still gate purchases.
inline int earlyRocketLimit(bool more, int tier, int structures) {
    if (!more || tier <= 0 || structures < 8) return 2;
    return tier >= 2 && structures >= 16 ? 4 : 3;
}
// An actual ground raid is not the peaceful coverage plan. Price the observed
// vehicles, not the whole enemy army; never restore the old unbounded battery.
constexpr int threatTurretTotalCap = 12;
inline int groundThreatGoal(int visibleGroundValue, bool more) {
    if (visibleGroundValue <= 0) return 0;
    const int goal = more ? 1 + (visibleGroundValue - 1) / 175
                          : 1 + (visibleGroundValue - 1) / 200;
    return std::clamp(goal, 2, 8);
}
// How much construction the city does between two peaceful emplacements.
constexpr unsigned growthOrdersPerProactiveTurret = 3;
// Whether this construction pass may spend its slot on proactive coverage.
//
// Deepening the overlap on assets that are already covered is a plan, not a
// present loss, and it competes with the city for the one thing a construction
// yard cannot buy more of: build time. Left ungated it took twenty-seven of the
// first ten minutes' orders while the base still had five heavy factories,
// because on a spread base the difficulty's overlap demand can never be met.
//
// Two things are not optional and keep every slot they need: aircraft that have
// actually reached our buildings, and a core asset — yard, refinery, factory,
// reactor — that nothing covers at all. An enemy wing merely owned somewhere on
// the map is neither. Everything else is interleaved with growth.
inline bool proactiveCoverageTurn(bool airEngaged, int uncoveredCoreAssets,
                                  unsigned nonServiceOrders) {
    return airEngaged || uncoveredCoreAssets > 0
        || nonServiceOrders >= growthOrdersPerProactiveTurret;
}
inline int amenityBenefit(int landValue, bool alreadyCovered, int terrainGain) {
    if (alreadyCovered) return 0;
    return std::min(std::max(0, DuneCity::kMaxLandValue - landValue), std::max(0, terrainGain));
}
// Weight of giving an asset that nothing covers its *first* turret. Summed
// coverage alone preferred one more overlapping emplacement over several
// central buildings to the only turret an isolated expansion yard would ever
// get, which is how twelve new yards were lost to aircraft. First cover of a
// core asset therefore outranks any amount of extra cover elsewhere, and an
// exposed expansion yard outranks the rest of the core.
inline int firstCoverPriority(int item, bool newExpansion) {
    if (!coreAsset(item)) return 0;
    return (newExpansion && item == Structure_ConstructionYard) ? 8 : 3;
}
struct Score {
    int defense = 0, junction = 0, amenity = 0, proximity = 0, critical = 0, expansion = 0;
    bool useful() const { return defense > 0 || (junction > 0 && amenity > 0); }
    bool betterThan(const Score& other) const {
        return std::tie(expansion, critical, defense, junction, amenity, proximity)
             > std::tie(other.expansion, other.critical, other.defense, other.junction, other.amenity, other.proximity);
    }
};
}
#endif
