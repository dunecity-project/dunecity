/*
 *  ValueModelTestCase.cpp
 *
 *  SimCity-Classic conformance tests for the DuneCity land-value, crime,
 *  and pollution model.
 *
 *  Source of truth: MicropolisCore/MicropolisEngine/src/scan.cpp and
 *                   .../zone.cpp from the GPL-3 Micropolis source.
 *  Each test cites the SC source location it locks against.
 *
 *  When DuneCity intentionally diverges from SC, the test documents the
 *  divergence and explains why (typically: 2x2 zone footprint vs SC's
 *  3x3, sand-as-water substitution, or density-level scaling that SC
 *  didn't have).
 *
 *  Scope: this suite exercises only the PURE helpers in CityEffects.h
 *  plus a few public CitySimulation methods. The full scan pipeline
 *  (runEffectsScans) needs Map/Tile and is stubbed in the test build —
 *  see CityEffectsRuntime_test_stub.cpp. Integration tests for the
 *  scan would need that target expanded.
 */

#include <catch2/catch_all.hpp>
#include <dunecity/CityEffects.h>
#include <dunecity/CityMapLayer.h>
#include <dunecity/CitySimulation.h>
#include <data.h>

using namespace DuneCity;

// =============================================================================
// LAND VALUE — SC `pollutionTerrainLandValueScan`, scan.cpp lines 274-282
//
//   dis = 34 - getCityCenterDistance(worldX, worldY) / 2;   // Manhattan
//   dis = dis << 2;                                          // *= 4
//   dis += terrainDensityMap.get(x>>1, y>>1);                // sand/water
//   dis -= pollutionDensityMap.get(x, y);
//   if (crimeRateMap.get(x, y) > 190) dis -= 20;             // crime feedback
//   dis = clamp(dis, 1, 250);
// =============================================================================

TEST_CASE("SC land value: city centre with no terrain/pollution yields 136",
          "[sc-conformance][value][formula]") {
    // (34 - 0/2) * 4 = 136. Below the 150 luxury threshold by design —
    // SC requires positive terrain bonus to reach tier 3 at any distance.
    REQUIRE(computeBaseLandValue(/*dist*/0, /*terrain*/0, /*poll*/0) == 136);
}

TEST_CASE("SC land value: Manhattan distance penalty drops 4 per 2 tiles",
          "[sc-conformance][value][formula]") {
    // (34 - dist/2) * 4. Integer division on dist/2 makes the falloff
    // chunky: every 2 tiles of distance lowers base by exactly 4.
    REQUIRE(computeBaseLandValue(0,  0, 0) == 136);
    REQUIRE(computeBaseLandValue(2,  0, 0) == 132);
    REQUIRE(computeBaseLandValue(10, 0, 0) == 116);
    REQUIRE(computeBaseLandValue(20, 0, 0) == 96);
    REQUIRE(computeBaseLandValue(40, 0, 0) == 56);
    REQUIRE(computeBaseLandValue(60, 0, 0) == 16);
}

TEST_CASE("SC land value: distance clamps to 64 tiles (scan.cpp line 408)",
          "[sc-conformance][value][formula][regression]") {
    // SC: `if (xDis + yDis > 64) return 64;`
    REQUIRE(kLandValueDistanceClamp == 64);
    REQUIRE(computeBaseLandValue(64,  0, 0) == 8);  // (34-32)*4
    REQUIRE(computeBaseLandValue(128, 0, 0) == 8);  // saturated past clamp
    REQUIRE(computeBaseLandValue(9999, 0, 0) == 8);
}

TEST_CASE("SC land value: terrain bonus adds linearly",
          "[sc-conformance][value][formula]") {
    REQUIRE(computeBaseLandValue(0,  20, 0) == 156);
    REQUIRE(computeBaseLandValue(0,  60, 0) == 196);
    REQUIRE(computeBaseLandValue(20, 50, 0) == 146);
}

TEST_CASE("SC land value: pollution subtracts directly",
          "[sc-conformance][value][formula]") {
    REQUIRE(computeBaseLandValue(0, 0, 10) == 126);
    REQUIRE(computeBaseLandValue(0, 0, 50) == 86);
}

TEST_CASE("SC land value: crime > 190 triggers -20 feedback (scan.cpp line 279)",
          "[sc-conformance][value][formula][regression]") {
    // SC: `if (crimeRateMap.get(x, y) > 190) dis -= 20;`
    REQUIRE(kCrimeLandValueThreshold == 190);
    REQUIRE(kCrimeLandValuePenalty   == 20);

    // Below threshold: no penalty.
    REQUIRE(computeBaseLandValue(0, 0, 0, /*crime*/190) == 136);
    REQUIRE(computeBaseLandValue(0, 0, 0, /*crime*/0)   == 136);

    // Above threshold: -20 penalty applied.
    REQUIRE(computeBaseLandValue(0, 0, 0, /*crime*/191) == 116);
    REQUIRE(computeBaseLandValue(0, 0, 0, /*crime*/250) == 116);
}

TEST_CASE("SC land value: clamps to [1, 250] (scan.cpp line 282)",
          "[sc-conformance][value][formula][regression]") {
    // SC: `dis = clamp(dis, 1, 250);` — the floor is 1, not 0, so a
    // developed block always has *some* value (0 means undeveloped).
    REQUIRE(kMaxLandValue == 250);
    REQUIRE(computeBaseLandValue(0, 0, /*poll*/500) == 1);     // heavily polluted
    REQUIRE(computeBaseLandValue(0, 9999, 0)        == 250);   // over-bonused
}

// =============================================================================
// VALUE TIER — SC `getLandPollutionValue`, zone.cpp lines 330-342
//
//   if (landVal < 30)  return 0;
//   if (landVal < 80)  return 1;
//   if (landVal < 150) return 2;
//   return 3;
// =============================================================================

TEST_CASE("SC value tier: thresholds 30 / 80 / 150 lock zone sprite rows",
          "[sc-conformance][tier][regression]") {
    // The thresholds come straight from SC's `getLandPollutionValue`.
    // Changing these re-skins every existing city — lock them.
    REQUIRE(getZoneValueTier(0,   4) == 0);
    REQUIRE(getZoneValueTier(29,  4) == 0);
    REQUIRE(getZoneValueTier(30,  4) == 1);   // boundary into tier 1
    REQUIRE(getZoneValueTier(79,  4) == 1);
    REQUIRE(getZoneValueTier(80,  4) == 2);   // boundary into tier 2
    REQUIRE(getZoneValueTier(149, 4) == 2);
    REQUIRE(getZoneValueTier(150, 4) == 3);   // boundary into tier 3 (luxury)
    REQUIRE(getZoneValueTier(250, 4) == 3);
}

TEST_CASE("Industrial atlas clamps to 2 tiers (SC only ships clean/dirty)",
          "[sc-conformance][tier][atlas]") {
    // Industrial zones in SC visually distinguish only clean vs polluted
    // (2 sprite variants). DuneCity matches: ZoneIndustrial atlas is 4×2.
    // A luxury land value (>= 150) must clamp to row 1, not produce
    // an out-of-atlas row 3 sprite index.
    REQUIRE(getZoneValueTier(0,   2) == 0);
    REQUIRE(getZoneValueTier(50,  2) == 1);
    REQUIRE(getZoneValueTier(200, 2) == 1);   // clamped
    REQUIRE(getZoneValueTier(250, 2) == 1);
}

TEST_CASE("Value tier: defensive guards for unexpected inputs",
          "[tier]") {
    REQUIRE(getZoneValueTier(250, 1) == 0);    // degenerate atlas
    REQUIRE(getZoneValueTier(250, 0) == 0);
    REQUIRE(getZoneValueTier(-1,   4) == 0);   // negative lv → tier 0
    REQUIRE(getZoneValueTier(-100, 4) == 0);
}

// =============================================================================
// CRIME — SC `crimeScan`, scan.cpp lines 425-432
//
//   z = landValueMap.worldGet(x, y);
//   if (z > 0) {
//       z = 128 - z;                                  // invert around 128
//       z += populationDensityMap.worldGet(x, y);     // pop fuels crime
//       z = min(z, 300);                              // intermediate clamp
//       z -= policeStationMap.worldGet(x, y);
//       z = clamp(z, 0, 250);                         // final clamp
//   }
// =============================================================================

TEST_CASE("SC crime: undeveloped land (lv==0) has zero crime",
          "[sc-conformance][crime][formula]") {
    // SC: `if (z > 0)` guard means lv==0 skips the whole computation
    // and crime stays at whatever was previously stored (effectively 0
    // for a fresh map block). Lock that behavior.
    REQUIRE(computeBaseCrime(0, 0)    == 0);
    REQUIRE(computeBaseCrime(0, 100)  == 0);  // pop alone doesn't generate crime
    REQUIRE(computeBaseCrime(-5, 100) == 0);
}

TEST_CASE("SC crime: midpoint inversion — lv near 128 yields zero crime",
          "[sc-conformance][crime][formula][regression]") {
    // `crime = 128 - lv + pop`. With pop=0, lv=128 → crime=0.
    REQUIRE(kCrimeBaseMidpoint == 128);
    REQUIRE(computeBaseCrime(128, 0) == 0);
    REQUIRE(computeBaseCrime(127, 0) == 1);
    REQUIRE(computeBaseCrime(129, 0) == 0);  // clamped at 0 (would be -1)
}

TEST_CASE("SC crime: linear inverse of land value for typical inputs",
          "[sc-conformance][crime][formula]") {
    // pop=0 ⇒ crime = max(0, 128 - lv).
    REQUIRE(computeBaseCrime(0,   0) == 0);    // SC guard skips
    REQUIRE(computeBaseCrime(1,   0) == 127);  // floor of devolved-blockcrime
    REQUIRE(computeBaseCrime(50,  0) == 78);
    REQUIRE(computeBaseCrime(100, 0) == 28);
    REQUIRE(computeBaseCrime(150, 0) == 0);   // luxury → no base crime
    REQUIRE(computeBaseCrime(250, 0) == 0);
}

TEST_CASE("SC crime: population density adds to crime",
          "[sc-conformance][crime][formula][regression]") {
    // Higher pop in a slum compounds crime; in a luxury area pop still
    // contributes but is offset by the negative (128 - lv) term.
    REQUIRE(computeBaseCrime(50,  50) == 128);   // 128-50+50
    REQUIRE(computeBaseCrime(50,  200) == 250);  // final Micropolis clamp
    REQUIRE(computeBaseCrime(150, 50) == 28);    // 128-150+50=28
    REQUIRE(computeBaseCrime(150, 200) == 178);  // 128-150+200=178
}

TEST_CASE("SC crime: high density is capped for the display map",
          "[sc-conformance][crime][formula][regression]") {
    // Intermediate crime stops at 300, then the display map at 250.
    REQUIRE(computeBaseCrime(1, 999) == 250);
}

TEST_CASE("SC crime: nonnegative with the source gameplay ceiling",
          "[sc-conformance][crime][formula][regression]") {

    REQUIRE(computeBaseCrime(250, 0)   == 0);     // luxury, no crime
    REQUIRE(computeBaseCrime(9999, 0)  == 0);     // saturated lv → no crime
    REQUIRE(computeBaseCrime(1, 9999) == 250);
}

// =============================================================================
// CRIME → LAND VALUE feedback (SC scan.cpp line 279)
// =============================================================================

TEST_CASE("Crime feedback: high-crime block loses 20 land value next tick",
          "[sc-conformance][feedback][regression]") {
    // Scenario from the audit: a slum hits crime > 190; on the next
    // scan the land-value pass reads that crime and subtracts 20.
    // The runtime keeps crime across the land-value pass exactly so
    // this one-tick feedback works.
    const int lvWithoutFeedback = computeBaseLandValue(20, 30, 0, /*crime*/0);
    const int lvWithFeedback    = computeBaseLandValue(20, 30, 0, /*crime*/200);
    REQUIRE(lvWithFeedback == lvWithoutFeedback - kCrimeLandValuePenalty);
}

TEST_CASE("Crime feedback: threshold is strictly > 190, not >=",
          "[sc-conformance][feedback][regression]") {
    // SC: `if (crimeRateMap.get(x, y) > 190)`. Strict greater-than.
    // Lock so a "fix typo" change doesn't silently shift behavior.
    REQUIRE(computeBaseLandValue(0, 0, 0, 190) == 136);  // boundary, no penalty
    REQUIRE(computeBaseLandValue(0, 0, 0, 191) == 116);  // one over, penalty fires
}

// =============================================================================
// POLLUTION emission table — DuneCity diverges from SC (intentional)
//
// SC: flat 50 per industrial tile (scan.cpp `getPollutionValue` line 376).
// Ours: scales by density level — 10 / 25 / 50 for L1 / L2 / L3.
// Justification: SC didn't have a per-zone density level model; DuneCity
// does, and tying emission to level lets early-game industrial be a
// tolerable neighbor that gets dirtier as it grows.
// =============================================================================

TEST_CASE("Pollution: DuneCity scales industrial emission by level (divergence)",
          "[divergence][pollution][table][regression]") {
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 1) == 10);
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 2) == 25);
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 3) == 50);
    // At max level we hit SC's flat-50 number, so high-density I lines up
    // with SC's worst-case noise.
    REQUIRE(getPollutionEmission(Structure_HeavyFactory, 3) == 25); // capped medium I, including old saves
}

TEST_CASE("Pollution: residential and commercial are clean at every level",
          "[sc-conformance][pollution][table]") {
    for (int lvl = 0; lvl <= 3; ++lvl) {
        REQUIRE(getPollutionEmission(Structure_ZoneResidential, lvl) == 0);
        REQUIRE(getPollutionEmission(Structure_ZoneCommercial,  lvl) == 0);
    }
}

TEST_CASE("Pollution: vacant zones (level 0) never emit",
          "[sc-conformance][pollution]") {
    REQUIRE(getPollutionEmission(Structure_ZoneIndustrial, 0) == 0);
    REQUIRE(getPollutionEmission(Structure_HeavyFactory,   0) == 0);
    REQUIRE(getPollutionEmission(Structure_Refinery,       0) == 0);
}

// =============================================================================
// POLLUTION × LAND VALUE interaction — gameplay feedback loop
// =============================================================================

TEST_CASE("Pollution can demote a luxury block out of tier 3",
          "[sc-conformance][pollution][value][integration]") {
    // City-centre tier-3 luxury block (terrain=60 → lv=196).
    int v = computeBaseLandValue(0, 60, 0);
    REQUIRE(getZoneValueTier(v, 4) == 3);

    // Drop a heavy-industry stamp on top (pollution=50): lv=146 → tier 2.
    v = computeBaseLandValue(0, 60, 50);
    REQUIRE(v == 146);
    REQUIRE(getZoneValueTier(v, 4) == 2);

    // Add another industrial source (pollution=100 total): tier 1.
    v = computeBaseLandValue(0, 60, 100);
    REQUIRE(v == 96);
    REQUIRE(getZoneValueTier(v, 4) == 2);  // 96 is still in [80, 150) → tier 2
}

TEST_CASE("Pollution + crime + distance compose to land a slum at tier 0",
          "[sc-conformance][integration]") {
    // Far from centre (40 tiles), modest pollution, high crime, no
    // terrain bonus. All the negatives stack and pile this block at
    // the floor.
    const int v = computeBaseLandValue(40, /*terrain*/0, /*poll*/20, /*crime*/200);
    REQUIRE(v == 16);   // (34-20)*4 - 20 - 20 = 16
    REQUIRE(getZoneValueTier(v, 4) == 0);   // slum
}

// =============================================================================
// SAND-AS-WATER — DuneCity divergence from SC (intentional)
//
// SC `pollutionTerrainLandValueScan` (scan.cpp lines 247-254):
//   if (loc < RUBBLE) {
//       tempMap3.set(x>>1, y>>1, value + 15);    // +15 per water/tree tile
//   }
//
// DuneCity stand-in: each open sand/dunes tile contributes +40 to the
// per-block terrain feature density AND fires a long-reach stampFalloff
// of +8 per tile across radius 5. The boost over SC's +15 is needed
// because:
//   (a) the box-smooth /8 dilution hits any block more than ~1 block
//       from sand, and SC's larger 120x100 map naturally has wider
//       contiguous bands of water than Dune-sized 64x64 maps;
//   (b) Dune's zone footprint is 2x2, so each "neighbour block" is half
//       the tile count SC's 3x3 zones get to lean on.
// =============================================================================

TEST_CASE("Sand bonus: DuneCity uses +40 per tile (divergence from SC's +15)",
          "[divergence][sand][regression]") {
    REQUIRE(kSandTerrainRawBonus == 40);
}

TEST_CASE("Sand bonus: long-reach stampFalloff layer is +8 over radius 5",
          "[divergence][sand][regression]") {
    REQUIRE(kSandLandValueBonus == 8);
    REQUIRE(kSandBonusRadius   == 5);
}

TEST_CASE("Sand bonus: tier-3 luxury becomes reachable from sand-adjacent zones",
          "[sand][value][integration][regression]") {
    // Realistic city-edge scenario: 20 tiles from centroid, with a
    // smoothed sand contribution of ~30 from one full-sand neighbour
    // block. Without sand the block sits at 96 (tier 2). With sand it
    // climbs into the 120s — still tier 2, not yet luxury. Need TWO
    // adjacent sand sides (~60 total) plus the stampFalloff layer for
    // tier 3 to fire. Documents the tuning intent.
    REQUIRE(getZoneValueTier(computeBaseLandValue(20, 0,   0), 4) == 2);   // bare
    REQUIRE(getZoneValueTier(computeBaseLandValue(20, 30,  0), 4) == 2);   // 1 side sand
    REQUIRE(getZoneValueTier(computeBaseLandValue(20, 60,  0), 4) == 3);   // 2 sides sand
}

// =============================================================================
// SPRITE FRAME indexing — what the player actually sees
// =============================================================================

TEST_CASE("Sprite frame: (density column) + (tier row × numDensity) for R/C 4x4",
          "[sprite][frame]") {
    // Frame index into a (numDensity × numTiers) atlas, row-major.
    REQUIRE(computeZoneSpriteFrame(0, 0, 4, 4) == 0);    // empty plot, slum
    REQUIRE(computeZoneSpriteFrame(3, 0, 4, 4) == 3);    // max density, slum
    REQUIRE(computeZoneSpriteFrame(0, 3, 4, 4) == 12);   // empty plot, luxury
    REQUIRE(computeZoneSpriteFrame(3, 3, 4, 4) == 15);   // max density, luxury
}

TEST_CASE("Sprite frame: industrial 4x2 atlas stays inside bounds",
          "[sprite][frame]") {
    REQUIRE(computeZoneSpriteFrame(0, 0, 4, 2) == 0);
    REQUIRE(computeZoneSpriteFrame(3, 1, 4, 2) == 7);
}

TEST_CASE("Sprite frame: out-of-range density and tier clamp to atlas",
          "[sprite][frame][regression]") {
    // Defensive: a transient density bump beyond numDensity must not
    // index into the next row.
    REQUIRE(computeZoneSpriteFrame(99, 2, 4, 4) == 11);  // 3 + 2*4
    REQUIRE(computeZoneSpriteFrame(2, 99, 4, 2) == 6);   // 2 + 1*4
    REQUIRE(computeZoneSpriteFrame(-5, -3, 4, 4) == 0);
}

// =============================================================================
// CityMapLayer<T> — block storage every map sits on top of
// =============================================================================

TEST_CASE("CityMapLayer: block grid is (W+bs-1)/bs by (H+bs-1)/bs",
          "[citymaplayer][storage]") {
    // SC's MapByte2 is templated on blockSize=2 (micropolis.h line 1246).
    // DuneCity uses the same blockSize for parity.
    CityMapLayer<uint8_t> m;
    m.init(64, 64, 2);
    REQUIRE(m.getBlockSize() == 2);
    m.set(0, 0, 42);
    REQUIRE(m.get(0, 0) == 42);
    m.set(31, 31, 99);   // last block index for a 32-wide grid
    REQUIRE(m.get(31, 31) == 99);
}

TEST_CASE("CityMapLayer: worldGet maps tile coords through blockSize",
          "[citymaplayer][storage]") {
    CityMapLayer<uint8_t> m;
    m.init(32, 32, 2);
    m.set(5, 7, 123);   // block (5, 7) covers tiles (10-11, 14-15)
    REQUIRE(m.worldGet(10, 14) == 123);
    REQUIRE(m.worldGet(11, 14) == 123);
    REQUIRE(m.worldGet(10, 15) == 123);
    REQUIRE(m.worldGet(11, 15) == 123);
    // Neighbour block (6, 7) covers tiles (12, 14) and is untouched.
    REQUIRE(m.worldGet(12, 14) == 0);
}

TEST_CASE("CityMapLayer: out-of-bounds reads/writes are safe",
          "[citymaplayer][storage]") {
    CityMapLayer<uint8_t> m;
    m.init(32, 32, 2);
    REQUIRE(m.get(-1, 0) == 0);
    REQUIRE(m.get(16, 0) == 0);     // 32 tiles / bs=2 = 16 blocks; index 16 is past end
    m.set(100, 100, 200);            // must not throw or write garbage
    REQUIRE(m.get(100, 100) == 0);
}

TEST_CASE("CityMapLayer: odd dimensions round block grid up",
          "[citymaplayer][storage]") {
    CityMapLayer<uint8_t> m;
    m.init(65, 65, 2);              // 65 tiles → 33 blocks (ceil)
    m.set(32, 32, 7);                // last valid block index
    REQUIRE(m.get(32, 32) == 7);
    REQUIRE(m.get(33, 32) == 0);
}

TEST_CASE("CityMapLayer: re-init wipes the grid",
          "[citymaplayer][storage]") {
    CityMapLayer<uint8_t> m;
    m.init(16, 16, 2);
    m.set(2, 2, 50);
    m.init(16, 16, 2);
    REQUIRE(m.get(2, 2) == 0);
}

// =============================================================================
// TAX setter — DuneCity addition (SC had a slider; we mirror the API). Each house has its own
// rate (save 9880): one house's budget never changes another's.
// =============================================================================

TEST_CASE("Tax setter: every house starts at the published default (7%)",
          "[tax][setter]") {
    CitySimulation sim;
    for (int h = 0; h < kMaxCityHouses; ++h) REQUIRE(sim.getCityTax(h) == kDefaultCityTaxRate);
    REQUIRE(kDefaultCityTaxRate == 7);
}

TEST_CASE("Tax setter: a house's rate is its own",
          "[tax][setter][regression]") {
    CitySimulation sim;
    sim.setCityTax(2, 12);
    REQUIRE(sim.getCityTax(2) == 12);
    for (int h = 0; h < kMaxCityHouses; ++h)
        if (h != 2) REQUIRE(sim.getCityTax(h) == kDefaultCityTaxRate);
    sim.setCityTax(-1, 3);
    sim.setCityTax(kMaxCityHouses, 3);
    for (int h = 0; h < kMaxCityHouses; ++h) REQUIRE(sim.getCityTax(h) != 3);
}

TEST_CASE("Tax setter: out-of-band values clamp to [kMinTaxRate, kMaxTaxRate]",
          "[tax][setter][regression]") {
    CitySimulation sim;
    sim.setCityTax(0, -1);
    REQUIRE(sim.getCityTax(0) == CitySimulation::kMinTaxRate);
    sim.setCityTax(0, -100);
    REQUIRE(sim.getCityTax(0) == CitySimulation::kMinTaxRate);
    sim.setCityTax(0, 21);
    REQUIRE(sim.getCityTax(0) == CitySimulation::kMaxTaxRate);
    sim.setCityTax(0, 100);
    REQUIRE(sim.getCityTax(0) == CitySimulation::kMaxTaxRate);
}

// =============================================================================
// 2x2 FOOTPRINT adjustment — explicit divergence notes
// =============================================================================

TEST_CASE("DuneCity: zone footprint is 2x2 vs SC's 3x3",
          "[divergence][footprint]") {
    // SC zone iteration uses static Zx[9]/Zy[9] arrays (zone.cpp 366-375).
    // DuneCity zones are 2x2 throughout. This is an architectural
    // invariant called out in CLAUDE.md — do NOT widen to 3x3 to "match SC"
    // without rewriting placement, growth, and rendering.
    //
    // The visible effect on this test suite: radii like kSupplyRadius and
    // kPoliceRadius are tuned in TILES (not zone-widths), so SC's
    // "police covers 3 blocks = 24 tiles" stays at 16-24 tiles in
    // DuneCity rather than mechanically scaling 2/3.
    REQUIRE(kSupplyRadius >= 4);
    REQUIRE(kPoliceRadius >= 8);
}

TEST_CASE("Block size: every map uses 2-tile blocks (DuneCity parity)",
          "[citymaplayer][storage][regression]") {
    // SC uses 2-tile blocks for landValue/crime/pollution/population/
    // traffic, and 4-tile blocks for terrainDensity. DuneCity uses
    // 2-tile blocks across the board for simplicity — the terrain
    // density map is per-block-of-2 here, and the per-tile sand bonus
    // is amplified (kSandTerrainRawBonus=40 vs SC's +15) to compensate
    // for the smaller block coverage.
    CitySimulation sim;
    sim.init(64, 64);
    REQUIRE(sim.getLandValueMap()        .getBlockSize() == 2);
    REQUIRE(sim.getCrimeRateMap()        .getBlockSize() == 2);
    REQUIRE(sim.getPollutionDensityMap() .getBlockSize() == 2);
    REQUIRE(sim.getPopulationDensityMap().getBlockSize() == 2);
    REQUIRE(sim.getTrafficDensityMap()   .getBlockSize() == 2);
}
