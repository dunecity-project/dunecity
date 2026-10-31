#ifndef DUNECITY_CITYEFFECTS_H
#define DUNECITY_CITYEFFECTS_H

#include <data.h>
#include <dunecity/ParkTerrainPolicy.h>
#include <fixmath/FixPoint.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DuneCity {

// =============================================================================
// City effects spec — pure functions, fully unit-testable.
//
// Every "city role" structure (a zone or an existing Dune building that maps
// to a SimCity-Classic role) carries an integer `level` 0..maxLevel. For
// zones that's tile->getCityZoneDensity(). For non-zone city-role structures
// (Refinery, Silo, Radar, Factories, Repair Yard, House IX, HighTech) it's
// pStruct->getCityOccupancy().
//
// Zones grow up from level 0 (vacant). Non-zone city-role structures are
// player-built, so the city sim treats their effective level as max(1,
// cityOccupancy) — they immediately provide tier-1 jobs the moment they
// finish construction, then grow to higher tiers via the usual demand
// gates. The level-1 floor lives in CityEffectsRuntime.cpp and avoids the
// chicken-and-egg where a Refinery can't level up without residents and
// residents can't move in without jobs.
//
// All supply / pollution / population numbers below are "at level L". A
// structure at level 0 contributes nothing; level 3 contributes the full
// amount listed for its role tier. This keeps the model uniform across
// hand-placed Dune buildings and player-zoned R/C/I.
//
// Mapping reference (as agreed in spec discussion):
//   Slab/Slab4         -> Road
//   WindTrap           -> Wind power / Industrial light (no pollution)
//   Nuclear Plant      -> Nuclear Power (no pollution; detonates when destroyed)
//   Refinery           -> Industrial medium
//   Light Factory      -> Industrial medium
//   Heavy Factory      -> Industrial high
//   HighTech Factory   -> Industrial high
//   Repair Yard        -> Industrial high
//   Spice Silo         -> Industrial high (no pollution)
//   Construction Yard  -> Industrial high (acts as factory)
//   Radar              -> Commercial medium
//   House IX           -> Commercial high
//   Starport           -> Shipyard / Industrial high (jobs, economic role)
//   Palace             -> Civic dual: 1x Residential + 1x Commercial pop, land-value boost
//   Stadium            -> Civic amenity (large land-value boost)
//   Airport            -> Commercial high (transport/trade boost)
//   Barracks           -> Residential high (infantry garrison = population)
//   WOR                -> Residential high (heavy infantry garrison = population)
//   Gun Turret        -> Park bonus + 15% Police
//   Rocket Turret     -> one park terrain source, 15% police coverage
//   Wall               -> Park bonus only
//   Sand (terrain)     -> Water (land-value bonus)
// =============================================================================

// --- Radii (in tiles, distances measured Chebyshev / square) -----------------

constexpr int kPollutionRadius   = 5;  // emission falls off linearly to this
// SimCity Classic stored police in an 8-tile-block map and ran three smooth
// passes, so a single station's influence spread ~3 blocks = ~24 tiles —
// roughly 8 zone-widths in SC's 3x3-footprint zones. Our zones are 2x2 so a
// "zone-width" is two tiles here; 8 zone-widths = 16 tiles. Linear falloff
// over that range preserves SC's "one station noticeably affects the whole
// neighbourhood" feel without over-reaching for our smaller gameplay grid.
constexpr int kPoliceRadius      = 23; // Three 6-tile diffusion steps plus the source cell.

// Sand-as-water reach. Each open sand tile stamps a falloff into the land-
// value map, bypassing the /8 dilution of the SC-style smoothing pass.
// Tuned so a zone with one full sand-block neighbour lands solidly inside
// tier 2 (value ≥ 80) and a zone with sand on two-plus sides reaches tier 3
// (value ≥ 150) when reasonably close to the city centre.
constexpr int kSandBonusRadius   = 5;
// Supply visibility radius. SimCity Classic does NOT use a Chebyshev
// supply check — it does a road-network drive of up to
// MAX_TRAFFIC_DISTANCE = 30 tiles (see SC traffic.cpp). We use a much
// simpler straight-line lookup, but bump the radius to 16 so the reach
// is in the same ballpark as a typical SC drive (allowing for some
// road-detour overhead). Smaller values caused commercial zones to
// permanently stall at level 1 when industrial sat in a separate
// cluster — commercial growth gates on local industrial supply.
constexpr int kSupplyRadius      = 16;

// --- Effect strengths --------------------------------------------------------

constexpr int kParkLandValueBonus       = 15;  // Micropolis raw park terrain contribution, before smoothing
constexpr int kStadiumLandValueBonus   = 40;  // Stadium provides a much larger boost
constexpr int kSandLandValueBonus       = 8;   // Per-tile direct stampFalloff
/// Per-tile contribution to the block-smoothed terrain feature map. Box-
/// smoothed twice and added to the SC-style land-value base, this is the
/// "close-adjacency" lift sand provides — distinct from the long-reach
/// stampFalloff above (kSandLandValueBonus). Larger than SC's +15 because
/// Dune-sized maps don't tolerate as much smoothing dilution.
constexpr int kSandTerrainRawBonus      = 40;
constexpr int kPoliceCoverageFull       = 1000; // Micropolis MAX_POLICE_STATION_EFFECT
constexpr int kPoliceCoverageGunTurret  = 150; // 15% of a police station
constexpr int kPoliceCoverageRocketTurret = kPoliceCoverageFull * 15 / 100;  // 15% of police-station coverage.

constexpr int kMaxLandValue = 250;

/// Damage devalues a developed building's own land proportionally. Applied
/// after amenity bonuses; repairs restore value on the next effects scan.
inline int conditionLandValue(int value, FixPoint health, int maxHealth) {
    if (value <= 0) return 0;
    if (maxHealth <= 0) return value;
    return std::clamp((FixPoint(value) * std::max(FixPoint(0), health) / maxHealth).floor(), 1, value);
}

constexpr int kMaxPollution = 250;

/// SimCity Classic feedback: when crime exceeds this threshold on a block,
/// the next land-value scan subtracts kCrimeLandValuePenalty from that
/// block's base. Source: MicropolisEngine scan.cpp `pollutionTerrainLand
/// ValueScan`, line 279 — `if (crimeRateMap.get(x, y) > 190) dis -= 20;`.
constexpr int kCrimeLandValueThreshold = 190;
constexpr int kCrimeLandValuePenalty   = 20;

/// SimCity Classic crime-base midpoint. `crime = 128 - landValue + pop`.
/// 128 means lv ≈ 128 yields zero base crime — typical mid-range blocks
/// sit near neutral. Source: scan.cpp `crimeScan`, line 427.
constexpr int kCrimeBaseMidpoint = 128;
/// Intermediate clamp before subtracting police coverage (SC line 430).


// --- City roles --------------------------------------------------------------

enum class CityRole {
    None,
    Residential,
    Commercial,
    Industrial,
};

/// What SC-Classic role this Dune structure plays in the city sim, if any.
/// Palace → civic dual (contributes to BOTH R and C pop — handled specially).
///   For growth purposes Palace uses Residential role.
/// StarPort → shipyard / industrial high (jobs + economic role).
/// Airport → commercial high (transport/trade boost).
/// Barracks/WOR → residential high (infantry garrisons = population).
inline CityRole getStructureCityRole(int itemID) {
    switch (itemID) {
        case Structure_ZoneResidential:
        case Structure_Palace:
        case Structure_Barracks:
        case Structure_WOR:
            return CityRole::Residential;
        case Structure_ZoneCommercial:
        case Structure_Radar:
        case Structure_IX:
        case Structure_Airport:
            return CityRole::Commercial;
        case Structure_ZoneIndustrial:
        case Structure_ConstructionYard:
        case Structure_LightFactory:
        case Structure_HeavyFactory:
        case Structure_HighTechFactory:
        case Structure_RepairYard:
        case Structure_Refinery:
        case Structure_Silo:
        case Structure_StarPort:
            return CityRole::Industrial;
        default:
            return CityRole::None;
    }
}

/// Maximum "level" this city-role structure can reach. Zones go 1..3.
/// Non-zone city-role structures cap at the tier they map to.
inline int getStructureMaxLevel(int itemID) {
    switch (itemID) {
        case Structure_ZoneResidential:
        case Structure_ZoneCommercial:
        case Structure_ZoneIndustrial:
        case Structure_Palace:          // civic dual — grows to max density
            return 3;
        case Structure_WindTrap:        // government power, no industry
            return 0;
        case Structure_Silo:            // industrial low (no pollution)
        case Structure_LightFactory:    // industrial low
            return 1;
        case Structure_Radar:           // commercial medium
        case Structure_Refinery:        // industrial medium
        case Structure_HeavyFactory:    // industrial medium
        case Structure_HighTechFactory: // industrial medium
        case Structure_RepairYard:      // industrial medium
            return 2;
        case Structure_IX:              // commercial high
        case Structure_ConstructionYard: // industrial high (acts as factory)
        case Structure_StarPort:        // shipyard — industrial high
        case Structure_Airport:         // transport hub — commercial high
        case Structure_Barracks:        // residential high (infantry)
        case Structure_WOR:             // residential high (heavy infantry)
        case Structure_TechCenter:      // civic high (IX vehicle support)
            return 3;
        default:
            return 0;
    }
}

/// Private zones pay tax. The Palace is the sole government exception, with
/// both R and C income. Fiscal status is independent of employment role.
inline bool isTaxableCityStructure(int itemID) {
    return itemID == Structure_ZoneResidential || itemID == Structure_ZoneCommercial
        || itemID == Structure_ZoneIndustrial || itemID == Structure_Palace;
}

inline int effectiveCityLevel(int itemID, int level) {
    return std::clamp(level, 0, getStructureMaxLevel(itemID));
}

// --- Pollution emission ------------------------------------------------------

/// Per-source pollution emission (0-100 scale), scaled by current level.
/// Industrial sources pollute by their capped city density. Government tax
/// exemption does not remove factory emissions.
inline int getPollutionEmission(int itemID, int level) {
    level = effectiveCityLevel(itemID, level);
    if (level <= 0) return 0;
    if (getStructureCityRole(itemID) != CityRole::Industrial) return 0;

    // Starport is Industrial for jobs/demand but does not pollute (it's a
    // trade hub, not a factory). Per spec override.
    if (itemID == Structure_StarPort) return 0;

    // Spice Silo is industrial-low for jobs/demand but stores spice — no
    // smokestacks, no pollution.
    if (itemID == Structure_Silo) return 0;

    // Construction Yard counts as industrial-high (a "heavy factory") for
    // supply purposes, but the player can't reposition it — emitting pollution
    // would unfairly stunt the residential cluster that grows around it.
    if (itemID == Structure_ConstructionYard) return 0;

    // Industrial pollution scales with level: 10 / 25 / 50.
    static constexpr int kIndustrialPollution[3] = { 10, 25, 50 };
    return kIndustrialPollution[level - 1];
}

// --- Supply (jobs/residents available within reach) --------------------------

namespace detail {
inline int supplyForLevel(int level) {
    if (level <= 0) return 0;
    if (level > 3) level = 3;
    static constexpr int kTable[3] = { 10, 25, 50 };
    return kTable[level - 1];
}
}

inline int getCommercialSupply(int itemID, int level) {
    return (getStructureCityRole(itemID) == CityRole::Commercial || itemID == Structure_Palace)
        ? detail::supplyForLevel(effectiveCityLevel(itemID, level)) : 0;
}

inline int getIndustrialSupply(int itemID, int level) {
    return (getStructureCityRole(itemID) == CityRole::Industrial)
        ? detail::supplyForLevel(effectiveCityLevel(itemID, level)) : 0;
}

inline int getResidentialSupply(int itemID, int level) {
    return (getStructureCityRole(itemID) == CityRole::Residential)
        ? detail::supplyForLevel(effectiveCityLevel(itemID, level)) : 0;
}

// --- Police coverage (crime reduction in radius) -----------------------------
//
// Police Station (DuneCity city-mode building) is the sole source of full
// coverage. Barracks and WOR are pure infantry-production buildings and
// no longer reduce crime — putting a barracks down should not magically
// turn your slums into a tier-3 luxury neighborhood. Gun turrets retain
// fractional coverage; rocket turrets provide 15% plus land value.

inline int getPoliceCoverage(int itemID) {
    switch (itemID) {
        case Structure_PoliceStation:   return kPoliceCoverageFull;
        case Structure_GunTurret:       return kPoliceCoverageGunTurret;
        case Structure_RocketTurret:    return kPoliceCoverageRocketTurret;
        default:                        return 0;
    }
}

// --- Police annual cost (city budget) ----------------------------------------

/// Annual upkeep uses fixed-point credits: guns cost exactly half of rockets.
/// Funding scales both service coverage and the continuously charged expense.
constexpr int kPoliceCostPoliceStation = 100;
inline FixPoint getPoliceAnnualCost(int itemID) {
    switch (itemID) {
        case Structure_PoliceStation: return FixPoint(100);
        case Structure_GunTurret: return FixPoint(15) / 2;
        case Structure_RocketTurret: return FixPoint(15);
        default: return FixPoint(0);
    }
}

// --- Park-style land-value bonus (Wall / Turrets) ----------------------------

inline bool usesParkTerrain(int itemID) {
    return itemID == Structure_Wall || itemID == Structure_GunTurret || itemID == Structure_RocketTurret;
}

inline int getParkLandValueBonus(int itemID) {
    switch (itemID) {
        case Structure_Wall:            return kParkLandValueBonus;
        case Structure_GunTurret:       return kParkLandValueBonus; // one park source
        case Structure_RocketTurret:    return kParkLandValueBonus;
        case Structure_Stadium:         return kStadiumLandValueBonus;
        case Structure_Palace:          return kStadiumLandValueBonus;   // civic building — large boost
        default:                        return 0;
    }
}

inline int getParkLandValueRadius(int itemID) {
    if (itemID == Structure_Stadium || itemID == Structure_Palace) return 8;
    // Conservative candidate-search bound, not a radial gameplay bonus.
    return kParkTerrainSearchRadius;
}

// --- Linear distance falloff helper ------------------------------------------

/// Returns a coverage strength scaled by (radius - distance) / radius.
/// Returns 0 for distance >= radius. Distances are in tiles.
inline int falloff(int strength, int distance, int radius) {
    if (distance >= radius || radius <= 0) return 0;
    if (distance < 0) distance = 0;
    return (strength * (radius - distance)) / radius;
}

// --- SC-Classic base land-value formula --------------------------------------
//
// Pure helper extracted from runEffectsScans so it can be unit-tested.
// Mirrors SimCity Classic's `pollutionTerrainLandValueScan` in
// MicropolisEngine/src/scan.cpp (lines 274-282 in the MicropolisCore tree):
//
//     dis = 34 - getCityCenterDistance(worldX, worldY) / 2;
//     dis = dis << 2;                              // dis *= 4
//     dis += terrainDensityMap.get(x>>1, y>>1);    // smoothed water/tree
//     dis -= pollutionDensityMap.get(x, y);
//     if (crimeRateMap.get(x, y) > 190) dis -= 20; // crime feedback
//     dis = clamp(dis, 1, 250);
//
// Distance is Manhattan (xDis + yDis) from the city centroid, clamped to
// 64 (SC line 408). Block-coordinate distances would compress the falloff
// and land everyone in tier 0.
constexpr int kLandValueDistanceClamp = 64;  ///< SC scan.cpp line 408

inline int computeBaseLandValue(int distanceFromCenter,
                                int terrainBonus,
                                int pollution,
                                int crime = 0) {
    int dist = distanceFromCenter;
    if (dist < 0)                          dist = 0;
    if (dist > kLandValueDistanceClamp)    dist = kLandValueDistanceClamp;
    int v = (34 - dist / 2) * 4;
    v += terrainBonus;
    v -= pollution;
    if (crime > kCrimeLandValueThreshold) {
        v -= kCrimeLandValuePenalty;
    }
    if (v < 1)             v = 1;
    if (v > kMaxLandValue) v = kMaxLandValue;
    return v;
}

// --- Crime formula -----------------------------------------------------------
//
// Mirrors SimCity Classic's `crimeScan` (scan.cpp lines 425-432):
//
//     z = landValueMap.worldGet(x, y);
//     if (z > 0) {
//         z = 128 - z;                       // invert around midpoint
//         z += populationDensityMap.worldGet(x, y);   // pop fuels crime
//         z = min(z, 300);
//         z -= policeStationMap.worldGet(x, y);
//         z = clamp(z, 0, 250);
//     }
//
// The pre-service cap and final 0–250 cap are both part of the original
// crimeScan.  Pollution affects this equation through land value only; it is
// not a separate crime input in Micropolis.
inline int computeCrimeBeforePolice(int landValue, int populationDensity = 0) {
    if (landValue <= 0) {
        // SC's `if (z > 0)` guard: undeveloped blocks have zero crime.
        return 0;
    }
    const int crime = kCrimeBaseMidpoint - landValue + populationDensity;
    return std::clamp(crime, 0, 300);
}

inline int computeCrimeAfterPolice(int landValue, int populationDensity, int coverage) {
    return std::clamp(computeCrimeBeforePolice(landValue, populationDensity)
                          - std::max(0, coverage),
                      0, 250);
}

// Legacy no-service query: return the final Micropolis display range.
inline int computeBaseCrime(int landValue, int populationDensity = 0) {
    return computeCrimeAfterPolice(landValue, populationDensity, 0);
}

// Micropolis calls the 192–250 band "Dangerous". DuneCity unrest uses that
// source-defined band, with long-lived district buildup before a larger outbreak.
constexpr int kCrimeDangerousThreshold = 192;
constexpr uint32_t kCrimeUnrestBuildupMs = 6 * 60 * 1000;
inline int hostileLandValuePenalty(int distanceSquared) {
    if (distanceSquared < 0 || distanceSquared > 16) return 0;
    int distance = 0;
    while (distance * distance < distanceSquared) ++distance;
    return (5 - distance) * 16;
}

inline int crimeUnrestRate(int crime) {
    return crime < kCrimeDangerousThreshold ? 0
                                             : 100 + 50 * std::min(58, crime - kCrimeDangerousThreshold) / 58;
}

// Gangs are a DuneCity event, separate from Micropolis' local crime formula.
// Starter settlements cannot sustain outbreaks; mature cities keep the normal timer.
inline int cityCrimeUnrestRate(int crime, int displayedPopulation) {
    return displayedPopulation < 5000 ? 0 : crimeUnrestRate(crime);
}

// --- Micropolis display categories ------------------------------------------
// These are the thresholds used by Micropolis `getDensity` (tool.cpp):
// land value: 0–29, 30–79, 80–149, 150+;
// crime: 0–63, 64–127, 128–191, 192+;
// pollution: 0, 1–127, 128–191, 192+.
inline const char* landValueCategory(int value) {
    if (value < 30)  return "Slum";
    if (value < 80)  return "Lower Class";
    if (value < 150) return "Middle Class";
    return "High";
}

inline const char* crimeCategory(int value) {
    if (value < 64)  return "Safe";
    if (value < 128) return "Light";
    if (value < 192) return "Moderate";
    return "Dangerous";
}

inline const char* pollutionCategory(int value) {
    if (value <= 0)  return "None";
    if (value < 128) return "Moderate";
    if (value < 192) return "Heavy";
    return "Very Heavy";
}

// --- Sprite-atlas indexing ---------------------------------------------------

/// Compute the cell index inside a zone's sprite atlas given the tile's
/// current density (column) and the sampled land-value tier (row).
/// Atlas layout is (numDensity columns × numTiers rows), so the linear
/// index used by `blitToScreen` is `density + tier * numDensity`.
/// Returned values are clamped to the atlas bounds so callers can't
/// over-read in case density transiently exceeds numDensity (which can
/// happen during the transition from a higher-tier sprite atlas to a
/// lower-tier one in mid-growth).
inline int computeZoneSpriteFrame(int density, int valueTier,
                                  int numDensity, int numTiers) {
    if (density   < 0)            density   = 0;
    if (density   >= numDensity)  density   = numDensity - 1;
    if (valueTier < 0)            valueTier = 0;
    if (valueTier >= numTiers)    valueTier = numTiers - 1;
    return density + valueTier * numDensity;
}

// --- Pollution growth suppression --------------------------------------------
//
// High pollution suppresses residential and commercial growth. Industrial
// zones tolerate pollution (they produce it). This is the threshold above
// which the growth stochastic roll is halved.

constexpr int kPollutionGrowthThreshold = 80;  // pollution > this slows R/C growth
constexpr int kPollutionGrowthBlock     = 160; // pollution > this blocks R/C growth entirely

/// Returns true if pollution at this block is too high for the given role
/// to grow to the requested level.
inline bool isPollutionBlockingGrowth(int pollution, CityRole role, int /*targetLevel*/) {
    if (role == CityRole::Industrial) return false;  // industry tolerates pollution
    if (role == CityRole::None)       return false;
    return pollution >= kPollutionGrowthBlock;
}

/// Returns true if pollution should halve the growth probability for R/C.
inline bool isPollutionSlowingGrowth(int pollution, CityRole role) {
    if (role == CityRole::Industrial) return false;
    if (role == CityRole::None)       return false;
    return pollution >= kPollutionGrowthThreshold;
}

// --- Demand model for zone growth --------------------------------------------

/// Minimum residential supply (population) required for an Industrial or
/// Commercial zone to grow to the requested level. Indexed 1..3. Tuned so
/// L1→L2 needs one adjacent supplier and L2→L3 needs a small cluster,
/// matching the small footprints typical of Dune-sized maps.
inline int getDemandResidentialThreshold(int level) {
    if (level <= 1) return 0;
    if (level == 2) return 10;
    return 30;  // level 3
}

/// Minimum (industrial + commercial) supply combined required for a
/// Residential zone to grow to the requested level.
inline int getDemandJobsThreshold(int level) {
    if (level <= 1) return 0;
    if (level == 2) return 10;
    return 30;
}

/// Land-value floor (out of kMaxLandValue) below which a zone refuses to
/// grow to the requested level. We keep zeros across the board so density
/// is allowed to climb to L3 even on rock with no parks — value tier
/// already differentiates rich vs poor neighbourhoods visually, and
/// gatekeeping density behind park-spam felt punishing on Dune-sized maps.
inline int getDemandLandValueFloor(int /*level*/) {
    return 0;
}

/// Map a land-value sample (0..kMaxLandValue) to a Micropolis-style value
/// tier. Used by ZoneStructure to pick which "row" of its sprite atlas to
/// draw (rich neighbourhoods show fancier buildings even at the same
/// density). `numTiers` clamps the result to what the zone type ships —
/// Industrial only has two value tiers, R/C have four.
///
/// Thresholds match SimCity Classic's getLandPollutionValue() (zone.cpp):
///   < 30  -> tier 0 (poor)
///   < 80  -> tier 1
///   < 150 -> tier 2
///   else  -> tier 3 (rich)
/// SC computes these from (landValue - pollution); our scan already bakes
/// pollution into landValue, so we pass the stored value straight through.
inline int getZoneValueTier(int landValue, int numTiers) {
    if (numTiers <= 1) return 0;
    int tier;
    if      (landValue <  30) tier = 0;
    else if (landValue <  80) tier = 1;
    else if (landValue < 150) tier = 2;
    else                      tier = 3;
    if (tier >= numTiers) tier = numTiers - 1;
    return tier;
}

// --- Population accounting ---------------------------------------------------

/// People (R) or jobs (C/I) contributed by one city-role structure at the
/// given level. Values match SimCity Classic (Micropolis) exactly:
///   R: getResZonePop  → CzDen*8+16 → 16, 24, 32, 40
///   C: getComZonePop  → CzDen+1    → 1, 2, 3, 4, 5
///   I: getIndZonePop  → CzDen+1    → 1, 2, 3, 4
/// DuneCity maps levels 1/2/3 to SC's low/mid/high density tiers.
/// Fewer zones on Dune maps means smaller absolute populations — by design.
///
/// Palace is a civic dual structure that contributes to BOTH R and C.
/// getZonePopulation returns the residential portion; use
/// getPalaceCommercialPopulation() for the commercial half.
inline int getZonePopulation(int itemID, int level) {
    level = effectiveCityLevel(itemID, level);
    if (level <= 0) return 0;
    if (level > 3) level = 3;

    // SC Classic values (mapped to 3 DuneCity levels):
    static constexpr int kResidential[3] = { 16, 24, 40 };
    static constexpr int kCommercial [3] = {  1,  3,  5 };
    static constexpr int kIndustrial [3] = {  1,  3,  4 };

    switch (getStructureCityRole(itemID)) {
        case CityRole::Residential: return kResidential[level - 1];
        case CityRole::Commercial:  return kCommercial [level - 1];
        case CityRole::Industrial:  return kIndustrial [level - 1];
        default:                    return 0;
    }
}

/// Palace commercial population contribution (one commercial zone).
/// Separate from getZonePopulation so the main loop can add this to comPop.
inline int getPalaceCommercialPopulation(int level) {
    return getZonePopulation(Structure_ZoneCommercial, level);
}

/// Weighted tax population in eighths. Private R/C/I income receives a 2x
/// balance multiplier; Palace retains its unboosted R+C contribution. Preserve
/// fractional houses until the city-wide annual total is rounded.
constexpr int kZoneTaxMultiplier = 2;
inline int taxablePopulationEighths(int itemID, int population, int level) {
    population = std::max(0, population);
    switch (itemID) {
        case Structure_ZoneResidential: return population * kZoneTaxMultiplier;
        case Structure_ZoneCommercial:
        case Structure_ZoneIndustrial: return population * 8 * kZoneTaxMultiplier;
        case Structure_Palace:
            return population + getPalaceCommercialPopulation(effectiveCityLevel(itemID,level)) * 8;
        default: return 0;
    }
}

// --- Traffic connectivity result ---------------------------------------------

enum class TrafficResult {
    NoRoad,          // no perimeter road found around the structure
    NoDestination,   // road found but cannot reach the needed complementary zone
    Connected,       // reachable destination found via road network
};

/// Maximum BFS distance (in road tiles) for a traffic attempt.
/// Micropolis uses MAX_TRAFFIC_DISTANCE = 30; we use 20 for smaller maps.
constexpr int kMaxTrafficDistance = 20;

// --- Zone score for growth/decline decisions ---------------------------------
//
// SimCity Classic computes zscore = globalValve + localEval per zone.
// Growth requires zscore > -350; decline is likely when zscore < 350.
// When unpowered, zscore = -500 (hard-coded in SC). DuneCity ports these
// semantics so that negative RCI demand valves actively suppress growth
// and trigger residential shrink.

/// Zone-score threshold above which growth attempts are allowed.
/// SimCity Classic uses -350 (doResidential guard in zone.cpp).
constexpr int kZscoreGrowthGate  = -350;

/// Zone-score threshold below which decline can trigger.
/// SimCity Classic: decline roll starts when zscore < 350.
constexpr int kZscoreDeclineGate = 350;

/// Local zone evaluation inspired by SimCity's evalRes/evalCom/evalInd.
/// Now consumes traffic connectivity result alongside land value, pollution,
/// and crime. Each zone type weights these inputs differently:
///
/// Residential: high sensitivity to pollution and crime, needs traffic to
///   commercial, land value helps. evalRes = clamp((lv - poll)*32, 0, 6000)
///   - 3000; traffic failure = -3000. Scaled down by /4 for DuneCity's
///   +-2000 valve range.
///
/// Commercial: land value and traffic to industrial matter, crime hurts,
///   pollution hurts moderately. Uses comRate-style calculation.
///
/// Industrial: traffic to residential dominates, pollution is output not
///   blocker, local conditions mostly irrelevant.
inline int computeLocalEval(CityRole role, int landValue, int pollution,
                            int crime, TrafficResult traffic) {
    switch (role) {
        case CityRole::Residential: {
            // SC evalRes: clamp((lv - poll) * 32, 0, 6000) - 3000
            // Scale /4 to fit DuneCity's valve range: max +750, min -750
            int raw = (landValue - pollution) * 32;
            if (raw < 0)    raw = 0;
            if (raw > 6000) raw = 6000;
            int v = (raw - 3000) / 4;
            // Crime penalty: high crime areas are undesirable for residents
            if (crime > 150) v -= 200;
            else if (crime > 100) v -= 100;
            // Traffic: no road = moderate penalty, no destination = large
            if (traffic == TrafficResult::NoRoad) v -= 300;
            else if (traffic == TrafficResult::NoDestination) v -= 600;
            return v;
        }
        case CityRole::Commercial: {
            // Commercial values: land value, connectivity, low crime
            int v = landValue * 2;
            if (crime > 120) v -= 150;
            else if (crime > 80) v -= 75;
            // Pollution hurts moderately (less than residential)
            if (pollution > 100) v -= 100;
            else if (pollution > 50) v -= 50;
            // Traffic to industrial is important
            if (traffic == TrafficResult::NoRoad) v -= 200;
            else if (traffic == TrafficResult::NoDestination) v -= 400;
            return v;
        }
        case CityRole::Industrial: {
            // Industrial: traffic to residential dominates
            // Pollution is output, not a self-blocker
            int v = 0;
            if (traffic == TrafficResult::NoRoad) v -= 200;
            else if (traffic == TrafficResult::NoDestination) v -= 300;
            return v;
        }
        default:
            return 0;
    }
}

/// Commercial-rate adjustment: adjusts the base commercial local eval
/// with supply-side and infrastructure factors not available to the pure
/// local-eval function. Matches the spec's "computeCommercialRate(node)"
/// helper (Section D).
///
/// nearbyRes: residential supply within kSupplyRadius
/// nearbyInd: industrial supply within kSupplyRadius
/// hasAirport: whether an Airport exists in the city
///
/// Returns an additive adjustment to the commercial zscore.
inline int computeCommercialRate(int nearbyRes, int nearbyInd, bool hasAirport) {
    int adj = 0;
    // Commercial zones thrive near residential population (customers)
    // and industrial output (goods to sell). Each 10 supply adds +10.
    adj += std::min(200, nearbyRes);
    adj += std::min(100, nearbyInd / 2);
    // Airport unlocks trade capacity — flat bonus when present.
    if (hasAirport) adj += 100;
    return adj;
}

/// Backward-compatible overload for tests that don't supply traffic/crime.
inline int computeLocalEval(CityRole role, int landValue, int pollution,
                            bool hasRoad) {
    TrafficResult tr = hasRoad ? TrafficResult::Connected : TrafficResult::NoRoad;
    return computeLocalEval(role, landValue, pollution, 0, tr);
}

/// Compute the zone score for growth/decline decisions.
/// zscore = globalValve + localEval; if unpowered, returns -500.
inline int computeZscore(int globalValve, int localEval, bool powered) {
    if (!powered) return -500;
    return globalValve + localEval;
}

/// Determine if a zone should decline based on its zscore and current level.
/// The roll parameter should be a deterministic hash in [0, 16).
///
/// Probability tiers (per tick):
///   zscore <= -1000: 25% (roll < 4)
///   zscore <= -500:  12.5% (roll < 2)
///   zscore < 0:      6.25% (roll == 0)
///   zscore in [0, kZscoreDeclineGate): no decline (buffer zone)
///
/// L1->L0 (back to vacant) only fires when zscore <= -1500, avoiding
/// flicker on freshly placed zones during the bootstrap grace period.
inline bool shouldZoneDecline(int zscore, int level, uint32_t roll) {
    if (level <= 0) return false;
    if (zscore >= kZscoreDeclineGate) return false;

    bool decline = false;
    if      (zscore <= -1000) decline = (roll < 4);   // 25%
    else if (zscore <= -500)  decline = (roll < 2);   // 12.5%
    else if (zscore < 0)      decline = (roll == 0);  // 6.25%

    // Protect L1 from declining to vacant unless conditions are extreme.
    if (decline && level == 1 && zscore > -1500) decline = false;

    return decline;
}

// --- Micropolis-shaped demand valve computation ------------------------------
//
// SimCity Classic's setValves() derives demand from employment, migration,
// births, labour base, internal/external market, and tax. DuneCity adapts
// this for its smaller maps and slower cadence.

/// Micropolis tax table (21 entries, indexed by min(cityTax + gameLevel, 20)).
/// Values range from 200 (low tax) to -600 (high tax). Source: simulate.cpp.
inline int getTaxTableEntry(int taxRate) {
    static constexpr int kTaxTable[21] = {
        200, 150, 120, 100, 80, 50, 30, 0, -10, -40,
        -100, -150, -200, -250, -300, -350, -400, -450, -500, -550, -600
    };
    if (taxRate < 0) taxRate = 0;
    if (taxRate > 20) taxRate = 20;
    return kTaxTable[taxRate];
}

/// Accumulating demand valve computation matching Micropolis setValves().
/// Each tick computes a delta from population ratios and ADDS it to the
/// existing valve (velocity model). This matches SC Classic exactly:
///   valve = clamp(valve + delta, -range, range)
///
/// resPop/comPop/indPop: current population counts (from zone scan)
/// prevResPop/prevComPop/prevIndPop: previous tick values (for employment/labor)
/// resValve/comValve/indValve: current valve values to accumulate onto
struct ValveInputs {
    int resPop = 0;
    int comPop = 0;
    int indPop = 0;
    int prevResPop = 0;   // raw previous population; normalize by 8 for SC resHist[1]
    int prevComPop = 0;   // previous tick (SC uses comHist[1])
    int prevIndPop = 0;   // previous tick (SC uses indHist[1])
    int16_t resValve = 0; // current valve to accumulate onto
    int16_t comValve = 0;
    int16_t indValve = 0;
    int taxRate = 7;
    bool hasStadium = false;
    bool hasPalace  = false;
    bool hasAirport = false;
    bool hasStarport = false;
};

struct ValveOutputs {
    int16_t resValve = 0;
    int16_t comValve = 0;
    int16_t indValve = 0;
    uint8_t civicDemandBlocked = 0; // positive demand stopped by missing civics
};

enum CivicRequirement : uint8_t { NeedStadium = 1, NeedAirport = 2, NeedStarport = 4 };

inline uint8_t missingDemandCivics(const ValveInputs& in) {
    return (in.resPop > 500 && !in.hasStadium ? NeedStadium : 0)
         | (in.comPop > 100 && !in.hasAirport ? NeedAirport : 0)
         | (in.indPop > 70 && !in.hasStarport ? NeedStarport : 0);
}

/// Valve ranges matching Micropolis: R=2000, C=1500, I=1500
constexpr int kResValveRange = 2000;
constexpr int kComValveRange = 1500;
constexpr int kIndValveRange = 1500;

/// Repair a demand valve loaded from a save whose matching population is
/// empty. Older broken builds could pin these valves below zero permanently,
/// preventing the first level-0 zone from ever bootstrapping.
inline int16_t recoverLoadedEmptyPopulationValve(int population, int16_t valve) {
    return population == 0 && valve < 0 ? 0 : valve;
}

inline ValveOutputs computeDemandValves(const ValveInputs& in) {

    // Micropolis constants (from simulate.cpp)
    constexpr int    resPopDenom = 8;
    constexpr double birthRate = 0.02;
    constexpr double laborBaseMax = 1.3;
    constexpr double internalMarketDenom = 3.7;
    constexpr double projectedIndPopMin = 5.0;
    constexpr double resRatioDefault = 1.3;
    constexpr double resRatioMax = 2.0;
    constexpr double comRatioMax = 2.0;
    constexpr double indRatioMax = 2.0;
    constexpr double taxTableScale = 600.0;
    // Game difficulty: SC Classic's extMarketParamTable = {1.2, 1.1, 0.98}.
    // DuneCity uses Easy (1.2) — boosts industrial growth.
    constexpr double extMarketParam = 1.2;

    // Normalize residential population (SC divides by 8)
    const double normResPop = std::max(1.0, static_cast<double>(in.resPop) / resPopDenom);

    // Employment uses previous-tick C+I values (SC: comHist[1] + indHist[1])
    const double prevJobs = static_cast<double>(in.prevComPop + in.prevIndPop);
    double employment;
    if (in.resPop > 0) {
        employment = prevJobs / normResPop;
    } else {
        employment = 1.0;
    }

    // Migration and births
    double migration = normResPop * (employment - 1.0);
    // Tiny-city safeguard: the first residential zone (16 internal pop)
    // creates 2 normResPop with effectively zero jobs, which lands employment
    // near 0 and dumps migration into a deep negative — within ~7 scan ticks
    // the resValve clamps at -2000 and stays there. SC Classic ran on bigger
    // initial cities; DuneCity starts from zero, so suppress the negative
    // migration penalty until residential population reaches a real threshold.
    // Births still drive a small positive demand during this phase, so the
    // player sees R demand grow normally instead of crashing the moment they
    // zone their first block.
    constexpr int kTinyCityResPopThreshold = 64;  // ~3 R-zones at L1
    if (in.resPop < kTinyCityResPopThreshold && migration < 0.0) {
        migration = 0.0;
    }
    const double births = normResPop * birthRate;
    const double projectedResPop = normResPop + migration + births;

    // Labour base uses previous-tick values (SC: resHist[1] / (comHist[1]+indHist[1]))
    double laborBase;
    // DuneCity starts maps and loaded campaigns with genuinely empty history.
    // A jobs-only history must not produce laborBase=0: that collapses both
    // projected job populations and floors the C/I valves before R can start.
    if (prevJobs > 0.0 && in.prevResPop > 0) {
        // Micropolis stores resHist in worker-equivalents (resPop / 8).
        // Our saved history is raw population, so normalize at this boundary.
        laborBase = (static_cast<double>(in.prevResPop) / resPopDenom) / prevJobs;
    } else {
        laborBase = 1.0;
    }
    if (laborBase > laborBaseMax) laborBase = laborBaseMax;
    if (laborBase < 0.0) laborBase = 0.0;

    // Internal market
    const double internalMarket = (normResPop + in.comPop + in.indPop) / internalMarketDenom;

    // Projected populations
    const double projectedComPop = internalMarket * laborBase;
    const double projectedIndPop = std::max(projectedIndPopMin,
        static_cast<double>(in.indPop)) * laborBase * extMarketParam;

    // Growth ratios (SC defaults resRatio to 1.3 when resPop is 0)
    double resRatio, comRatio, indRatio;
    if (normResPop > 0) {
        resRatio = projectedResPop / normResPop;
    } else {
        resRatio = resRatioDefault;
    }
    if (in.comPop > 0) {
        comRatio = projectedComPop / in.comPop;
    } else {
        // Micropolis setValves uses the raw projected population when no
        // commercial population exists, including projections below one.
        comRatio = projectedComPop;
    }
    if (in.indPop > 0) {
        indRatio = projectedIndPop / in.indPop;
    } else {
        indRatio = 1.0;
    }
    if (resRatio > resRatioMax) resRatio = resRatioMax;
    if (comRatio > comRatioMax) comRatio = comRatioMax;
    if (indRatio > indRatioMax) indRatio = indRatioMax;

    // Tax adjustment — SC uses min(cityTax + gameLevel, 20) but DuneCity
    // has no game level concept, so we just use taxRate directly.
    const int taxAdj = getTaxTableEntry(in.taxRate);
    int resDelta = static_cast<int>((resRatio - 1.0) * taxTableScale) + taxAdj;
    int comDelta = static_cast<int>((comRatio - 1.0) * taxTableScale) + taxAdj;
    int indDelta = static_cast<int>((indRatio - 1.0) * taxTableScale) + taxAdj;

    // Accumulate onto existing valves (SC: valve = clamp(valve + delta))
    int newRes = in.resValve + resDelta;
    int newCom = in.comValve + comDelta;
    int newInd = in.indValve + indDelta;

    // Civic caps (SC thresholds from message.cpp):
    //   resCap: resPop > 500 && no stadium → valve capped to 0
    //   comCap: comPop > 100 && no airport        → valve capped to 0
    //   indCap: indPop > 70  && no seaport/starport → valve capped to 0
    const uint8_t missingCivics = missingDemandCivics(in);
    uint8_t blockedCivics = 0;
    if ((missingCivics & NeedStadium) && newRes > 0) {
        blockedCivics |= NeedStadium;
        newRes = 0;
    }
    if ((missingCivics & NeedAirport) && newCom > 0) {
        blockedCivics |= NeedAirport;
        newCom = 0;
    }
    if ((missingCivics & NeedStarport) && newInd > 0) {
        blockedCivics |= NeedStarport;
        newInd = 0;
    }

    // Clamp to SC ranges
    if (newRes >  kResValveRange) newRes =  kResValveRange;
    if (newRes < -kResValveRange) newRes = -kResValveRange;
    if (newCom >  kComValveRange) newCom =  kComValveRange;
    if (newCom < -kComValveRange) newCom = -kComValveRange;
    if (newInd >  kIndValveRange) newInd =  kIndValveRange;
    if (newInd < -kIndValveRange) newInd = -kIndValveRange;

    return { static_cast<int16_t>(newRes),
             static_cast<int16_t>(newCom),
             static_cast<int16_t>(newInd), blockedCivics };
}

// --- Traffic pollution -------------------------------------------------------
//
// SC Classic's traffic density feeds into the pollution scan: road segments
// with heavy traffic contribute pollution proportional to density. Light
// traffic (density < threshold) contributes a small amount; heavy traffic
// (density >= threshold) contributes more. This closes the feedback loop:
// zones that generate traffic also generate pollution along the roads they
// use, penalizing dense residential areas near busy corridors.

constexpr int kTrafficPollutionLightThreshold = 64;   // density >= this → light traffic pollution
constexpr int kTrafficPollutionHeavyThreshold = 150;  // density >= this → heavy traffic pollution
constexpr int kTrafficPollutionLight = 2;   // per-block pollution contribution from light traffic
constexpr int kTrafficPollutionHeavy = 6;   // per-block pollution contribution from heavy traffic

/// Compute traffic pollution contribution for a block given its traffic density.
inline int getTrafficPollution(int trafficDensity) {
    if (trafficDensity >= kTrafficPollutionHeavyThreshold) return kTrafficPollutionHeavy;
    if (trafficDensity >= kTrafficPollutionLightThreshold) return kTrafficPollutionLight;
    return 0;
}

// --- Growth rate tracking ----------------------------------------------------

/// Growth-rate delta to apply when a zone grows or declines. Positive for
/// growth, negative for decline. The rate map decays toward zero each scan.
constexpr int kGrowthRateIncrement = 4;
constexpr int kGrowthRateDecrement = -4;

/// Decay amount subtracted from absolute growth rate each city day.
constexpr int kGrowthRateDecay = 1;

// --- Tax / city-year cadence -------------------------------------------------

/// Game cycles per minute at GAMESPEED_DEFAULT (16 ms/cycle): 60000/16 = 3750. The classic
/// city year; the AI's economic forecasts use it as their fixed time unit.
constexpr uint32_t kCyclesPerMinute = 3750;

/// Game cycles per simulated city year: the budget unit (taxes and police are annual amounts,
/// paid a cycle at a time) and the clock of zone growth (48 day ticks a year). 7500 (120 s)
/// with the Slow City Year match rule, the classic 3750 (60 s) without it or without a game.
uint32_t cyclesPerCityYear();

/// SimCity Classic divides a year into 48 cityTime units (4 weeks × 12
/// months). Each unit is a "day tick" — the cadence at which we run
/// effects scans and roll for zone growth (~1.25 s a day with a 60 s year,
/// ~2.5 s with 120 s). Zones get ~48 growth attempts per year, gated
/// stochastically inside runZoneGrowth() so population climbs gradually.
constexpr int      kCityDaysPerYear   = 48;
inline uint32_t cyclesPerCityDay() { return cyclesPerCityYear() / kCityDaysPerYear; }

/// An annual city amount (tax, police cost) in credits per minute, the AI's forecast unit.
inline int32_t annualPerMinute(int64_t annual) {
    return static_cast<int32_t>(annual * kCyclesPerMinute / cyclesPerCityYear());
}

/// Budget tick cadence: every game cycle (smooth credits like harvester
/// unloading). Each tick pays 1/cyclesPerCityYear() of the annual budget via
/// FixPoint arithmetic so fractional credits accumulate correctly.
constexpr uint32_t kCyclesPerBudgetTick = 1;

/// Micropolis easy tax: (R/8 + C + I) * landValue/120 * taxRate * 1.4.
/// Input is tax population in EIGHTHS, excluding government except Palace.
/// Round only the city-wide annual total; retain partial-house contributions.
/// Explicit zero land value means zero income; forecasts may use default128.
inline int32_t computeAnnualTaxRevenue(int taxBaseEighths, int taxRatePct,
                                       int avgLandValue = 128) {
    if (taxBaseEighths <= 0 || taxRatePct <= 0 || avgLandValue <= 0) return 0;
    return static_cast<int32_t>(int64_t(taxBaseEighths) * avgLandValue * taxRatePct * 14
        / (8 * 120 * 10));
}

// --- Hospital and Church census (SC Classic) --------------------------------
//
// SC Classic auto-creates hospitals and churches on residential zones —
// the game decides, not the player. 1 per 256 residential population.
// DuneCity tracks the auto-count for display in the budget window.

/// Residential population per hospital (SC: resPop >> 8 = resPop / 256).
constexpr int kResPopPerHospital = 256;
/// Residential population per church.
constexpr int kResPopPerChurch   = 256;

/// Auto-count of hospitals created by the game on residential zones.
inline int computeHospitalCount(int resPop) {
    return resPop / kResPopPerHospital;
}

/// Auto-count of churches created by the game on residential zones.
inline int computeChurchCount(int resPop) {
    return resPop / kResPopPerChurch;
}

// --- Unemployment -----------------------------------------------------------

/// Compute unemployment rate as a percentage (0-100).
/// Uses SC Classic's employment formula: employment = jobs / (resPop/8).
/// Unemployment = max(0, 1.0 - employment) * 100.
inline int computeUnemploymentRate(int resPop, int comPop, int indPop) {
    if (resPop <= 0) return 0;
    const double normRes = static_cast<double>(resPop) / 8.0;
    const double jobs = static_cast<double>(comPop + indPop);
    const double employment = jobs / normRes;
    if (employment >= 1.0) return 0;
    return static_cast<int>((1.0 - employment) * 100.0);
}

} // namespace DuneCity

#endif // DUNECITY_CITYEFFECTS_H
