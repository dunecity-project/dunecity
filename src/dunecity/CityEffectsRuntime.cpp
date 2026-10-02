#include <dunecity/CityStructurePopulation.h>
#include <units/UnitBase.h>
#include <dunecity/PoliceCoveragePolicy.h>
#include <players/AIDecisionLog.h>
#include <players/Player.h>
#include <dunecity/CityTrafficPolicy.h>
/*
 *  CityEffectsRuntime.cpp
 *
 *  Implementation of CitySimulation's full-map effect scans and zone growth.
 *  Split out from CitySimulation.cpp because these symbols pull in Map, Tile,
 *  StructureBase, ZoneStructure, and House — heavy dependencies that the test
 *  harness deliberately does not link. The unit tests link CitySimulation.cpp
 *  with empty stubs from CityEffectsRuntime_test_stub.cpp instead.
 */

#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CityConstants.h>
#include <dunecity/TrafficSimulation.h>
#include <dunecity/ZonePower.h>
#include <FileClasses/TextManager.h>
#include <dunecity/PopulationDensityPolicy.h>

#include <globals.h>
#include <Game.h>
#include <Map.h>
#include <Tile.h>
#include <House.h>
#include <structures/StructureBase.h>
#include <structures/BuilderBase.h>
#include <structures/ZoneStructure.h>

#include <SDL2/SDL_log.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <vector>

// --- Per-house getter implementations ---
// These delegate to the local player's HouseCityState for backward-compatible UI.

static int localHouseID() {
    return pLocalHouse ? pLocalHouse->getHouseID() : 0;
}

namespace DuneCity {

int CitySimulation::getResPop() const { return getHouseState(localHouseID()).resPop; }
int CitySimulation::getComPop() const { return getHouseState(localHouseID()).comPop; }
int CitySimulation::getIndPop() const { return getHouseState(localHouseID()).indPop; }
int CitySimulation::getTaxBaseEighths() const { return getHouseState(localHouseID()).taxBaseEighths; }
int CitySimulation::getTotalPop() const { return getHouseState(localHouseID()).getTotalPop(); }
int16_t CitySimulation::getResValve() const { return getHouseState(localHouseID()).resValve; }
int16_t CitySimulation::getComValve() const { return getHouseState(localHouseID()).comValve; }
int16_t CitySimulation::getIndValve() const { return getHouseState(localHouseID()).indValve; }
int CitySimulation::getAvgLandValue() const { return getHouseState(localHouseID()).avgLandValue; }
int CitySimulation::getUnemploymentRate() const { return getHouseState(localHouseID()).unemploymentRate; }
int CitySimulation::getHospitalCount() const { return getHouseState(localHouseID()).hospitalCount; }
int CitySimulation::getChurchCount() const { return getHouseState(localHouseID()).churchCount; }
bool CitySimulation::getHasStadium() const { return getHouseState(localHouseID()).hasStadium; }
bool CitySimulation::getHasAirport() const { return getHouseState(localHouseID()).hasAirport; }
const CityEnvironmentStatus& CitySimulation::getEnvironmentStatus(int houseID) const {
    if (houseID < 0 || houseID >= kMaxCityHouses) houseID = 0;
    return environmentStatus_[houseID];
}
int CitySimulation::getAveragePollution() const { return getEnvironmentStatus(localHouseID()).averagePollution; }
int CitySimulation::getAverageCrime() const { return getEnvironmentStatus(localHouseID()).averageCrime; }
int CitySimulation::getAverageTraffic() const { return getEnvironmentStatus(localHouseID()).averageTraffic; }

int CitySimulation::getPoliceFundingPercent() const { return getHouseState(localHouseID()).policeFundingPercent; }
void CitySimulation::setPoliceFundingPercent(int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    getHouseStateMut(localHouseID()).policeFundingPercent = v;
}
int32_t CitySimulation::getNominalPoliceCost() const { return getHouseState(localHouseID()).nominalPoliceCost; }
int32_t CitySimulation::getLastPoliceExpense() const { return getHouseState(localHouseID()).lastPoliceExpense; }

CityBudget& CitySimulation::getCityBudget() { return getHouseStateMut(localHouseID()).budget; }
const CityBudget& CitySimulation::getCityBudget() const { return getHouseState(localHouseID()).budget; }

} // namespace DuneCity

namespace {

/// The "level" used by the city sim for any city-role structure: zones
/// read it from tile density, non-zone city-role structures read it from
/// StructureBase::cityOccupancy_. Returns 0 for non-role structures.
///
/// Non-zone city-role structures (Refinery, Silo, Factories, RepairYard,
/// Radar, IX, HighTech) are floored at level 1: the player paid to build
/// them, so they should provide their tier-1 jobs immediately rather than
/// sitting "vacant" waiting on residents that won't move in without jobs.
int cityLevelOf(const Tile* t, const StructureBase* pStruct) {
    if (!pStruct) return 0;
    if (DuneCity::getStructureCityRole(pStruct->getItemID()) == DuneCity::CityRole::None) {
        return 0;
    }
    if (dynamic_cast<const ZoneStructure*>(pStruct)) {
        return t ? t->getCityZoneDensity() : 0;
    }
    const int occ = pStruct->getCityOccupancy();
    return DuneCity::effectiveCityLevel(pStruct->getItemID(), std::max(1, occ));
}

template<typename F>
void forEachStructureOrigin(const Map& map, F&& visit) {
    for (int y = 0; y < map.getSizeY(); ++y) {
        for (int x = 0; x < map.getSizeX(); ++x) {
            const Tile* pTile = map.getTile(x, y);
            if (!pTile || !pTile->hasANonInfantryGroundObject()) continue;

            const ObjectBase* pObj = pTile->getNonInfantryGroundObject();
            if (!pObj || !pObj->isAStructure()) continue;

            const StructureBase* pStruct = static_cast<const StructureBase*>(pObj);
            if (pStruct->getLocation().x != x || pStruct->getLocation().y != y) {
                continue;  // skip non-origin tiles of multi-tile structures
            }
            visit(x, y, pStruct);
        }
    }
}

void stampFalloff(DuneCity::CityMapLayer<uint8_t>& dst,
                  int cx, int cy, int radius, int value, int maxValue) {
    if (radius <= 0 || value == 0) return;
    const int blockSize = dst.getBlockSize();
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int dist = std::max(std::abs(dx), std::abs(dy));  // Chebyshev
            if (dist > radius) continue;
            const int contribution =
                DuneCity::falloff(value, dist, radius + 1);
            if (contribution == 0) continue;

            const int wx = cx + dx;
            const int wy = cy + dy;
            const int bx = wx / blockSize;
            const int by = wy / blockSize;
            int current = dst.get(bx, by);
            int updated = current + contribution;
            if (updated < 0)        updated = 0;
            if (updated > maxValue) updated = maxValue;
            dst.set(bx, by, static_cast<uint8_t>(updated));
        }
    }
}

}  // namespace

namespace DuneCity {

int32_t CitySimulation::getTotalFunds() const {
    return pLocalHouse ? static_cast<int32_t>(pLocalHouse->getCredits()) : totalFunds_;
}

bool CitySimulation::spendCityFunds(int32_t amount) {
    if (!pLocalHouse) return false;
    if (pLocalHouse->getCredits() >= amount) {
        pLocalHouse->takeCredits(FixPoint(amount));
        return true;
    }
    return false;
}

void CitySimulation::runEffectsScans() {
    if (!currentGameMap) return;
    AITelemetry::PerformanceScope phase("city.effects.pollution",currentGame->getGameCycleCount());

    // Reset pollution and land value at scan start. Crime is intentionally
    // NOT reset here: SC's land-value scan reads crime from the previous
    // tick (see scan.cpp line 279 — `if (crimeRateMap.get(x, y) > 190)`),
    // and we mirror that one-tick feedback delay below. Crime is reset
    // and recomputed AFTER the land-value pass finishes.
    pollutionDensityMap_.init(mapWidth_, mapHeight_, pollutionDensityMap_.getBlockSize());
    landValueMap_      .init(mapWidth_, mapHeight_, landValueMap_      .getBlockSize());

    const Map& map = *currentGameMap;

    // Pollution emission. Both zones and non-zone industrial structures
    // pollute proportional to their current level.
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const Tile* t = map.getTile(x, y);
        const int level = cityLevelOf(t, pStruct);
        const int emission = getPollutionEmission(pStruct->getItemID(), level);
        if (emission > 0) {
            stampFalloff(pollutionDensityMap_, x, y,
                         kPollutionRadius, emission, kMaxPollution);
        }
    });

    // SC pollution scan smooths twice (scan.cpp lines 298-299), which is
    // what gives a single power plant a soft-edged halo rather than the
    // hard-cut stamp radius. We approximate with two box-smooths over the
    // pollution map after stamping.
    {
        const int bs = pollutionDensityMap_.getBlockSize();
        const int bw = (mapWidth_  + bs - 1) / bs;
        const int bh = (mapHeight_ + bs - 1) / bs;
        auto smoothPass = [&]() {
            std::vector<uint8_t> tmp(bw * bh, 0);
            for (int by = 0; by < bh; ++by) {
                for (int bx = 0; bx < bw; ++bx) {
                    int z = pollutionDensityMap_.get(bx, by);
                    if (bx > 0)      z += pollutionDensityMap_.get(bx - 1, by);
                    if (bx < bw - 1) z += pollutionDensityMap_.get(bx + 1, by);
                    if (by > 0)      z += pollutionDensityMap_.get(bx, by - 1);
                    if (by < bh - 1) z += pollutionDensityMap_.get(bx, by + 1);
                    // SC `smoothDitherMap` non-dither path: (center + 4
                    // neighbors) >> 2. (scan.cpp lines 544-556.)
                    z >>= 2;
                    if (z > kMaxPollution) z = kMaxPollution;
                    tmp[by * bw + bx] = static_cast<uint8_t>(z);
                }
            }
            for (int by = 0; by < bh; ++by) {
                for (int bx = 0; bx < bw; ++bx) {
                    pollutionDensityMap_.set(bx, by, tmp[by * bw + bx]);
                }
            }
        };
        smoothPass();
        smoothPass();
    }

    phase.next("city.effects.terrain_value");
    // ---- Land value, SimCity Classic style ---------------------------------
    //
    // SC formula (scan.cpp::pollutionTerrainLandValueScan):
    //     base = 4 * (34 - cityCenterDistance / 2)
    //     base += terrainDensity   // smoothed near-water/tree score
    //     base -= pollution
    //     if crime > 190: base -= 20
    //     clamp(1, 250) if developed, else 0
    //
    // "Treat sand like water": Dune is mostly sand, so we use sand+dunes
    // tiles as the SC "water/tree" feature. Each non-developed sand tile
    // contributes to a per-block "terrain feature density" map, smoothed
    // twice to give nearby blocks a graduated bonus. The "developed"
    // gate (only tiles touching a road or structure get a value > 0)
    // keeps land value 0 in the empty desert so growth gates work as
    // they should.
    const int bs = std::max(1, landValueMap_.getBlockSize());
    const int blocksW = (mapWidth_  + bs - 1) / bs;
    const int blocksH = (mapHeight_ + bs - 1) / bs;

    // Per-block terrain feature count (raw, pre-smooth).
    std::vector<int> terrainRaw(blocksW * blocksH, 0);
    // Per-block "developed" flag — tile has a road or any ground object.
    std::vector<uint8_t> developed(blocksW * blocksH, 0);

    // Sub-phase timings inside terrain_value. Diagnostic only: they measure the
    // existing work, in the existing order, and feed no decision.
    {
    AITelemetry::PerformanceScope tv("city.effects.tv_tilescan",currentGame->getGameCycleCount());
    for (int wy = 0; wy < mapHeight_; ++wy) {
        for (int wx = 0; wx < mapWidth_; ++wx) {
            const Tile* t = map.getTile(wx, wy);
            if (!t) continue;
            const int bx = wx / bs;
            const int by = wy / bs;
            const int idx = by * blocksW + bx;
            if ((t->isSand() || t->isDunes()) && !t->hasAGroundObject()) {
                // Sand-as-water: on Dune, sand is the scenic "feature" that
                // SimCity's water/tree counted as. The header constant
                // pushes per-tile contribution well above SC's +15 so a 2x2
                // zone adjacent to a band of sand reliably clears the
                // tier-3 land-value threshold (150) after the two smooth
                // passes below.
                terrainRaw[idx] += kSandTerrainRawBonus;
            }
            if (t->isRoad() || t->hasAGroundObject()) {
                developed[idx] = 1;
            }
        }
    }
    }

    // Two box-smooth passes (SC runs three smoothTerrain passes; two is
    // enough at our smaller map sizes to spread the feature score one
    // block in each direction with falloff).
    auto smoothBlocks = [&](std::vector<int>& src) {
        std::vector<int> dst(src.size(), 0);
        for (int by = 0; by < blocksH; ++by) {
            for (int bx = 0; bx < blocksW; ++bx) {
                const int center = src[by * blocksW + bx];
                int sum = center * 4;
                if (bx > 0)            sum += src[by * blocksW + (bx - 1)];
                if (bx < blocksW - 1)  sum += src[by * blocksW + (bx + 1)];
                if (by > 0)            sum += src[(by - 1) * blocksW + bx];
                if (by < blocksH - 1)  sum += src[(by + 1) * blocksW + bx];
                dst[by * blocksW + bx] = sum / 8;
            }
        }
        src = std::move(dst);
    };
    smoothBlocks(terrainRaw);
    smoothBlocks(terrainRaw);

    // Per-CITY centres, not a single global centroid.
    //
    // SC's formula assumes one contiguous city: the centroid of all
    // developed tiles IS the city's heart. On a two-player map with
    // cities in opposite corners, a global centroid lands in the empty
    // desert between them — making `dis = 34 - dist/2` clamp to 0 for
    // every block in both cities, killing the within-city gradient and
    // breaking the dist contribution entirely.
    //
    // Fix: flood-fill developed blocks into clusters and give each
    // cluster its own centroid. Every developed block measures distance
    // to ITS cluster's centroid, recovering SC's "centre is best, edges
    // less so" gradient inside each city independently.
    std::vector<int> clusterId(blocksW * blocksH, -1);
    std::vector<long long> sumX, sumY;
    std::vector<long long> count;
    {
        std::vector<std::pair<int,int>> queue;
        queue.reserve(blocksW * blocksH);
        for (int by = 0; by < blocksH; ++by) {
            for (int bx = 0; bx < blocksW; ++bx) {
                if (!developed[by * blocksW + bx]) continue;
                if (clusterId[by * blocksW + bx] != -1) continue;
                const int cid = static_cast<int>(sumX.size());
                sumX.push_back(0); sumY.push_back(0); count.push_back(0);
                queue.clear();
                queue.push_back({bx, by});
                clusterId[by * blocksW + bx] = cid;
                while (!queue.empty()) {
                    auto [qx, qy] = queue.back();
                    queue.pop_back();
                    sumX[cid] += qx * bs;
                    sumY[cid] += qy * bs;
                    count[cid] += 1;
                    static const int DX[4] = {1, -1, 0, 0};
                    static const int DY[4] = {0, 0, 1, -1};
                    for (int d = 0; d < 4; ++d) {
                        const int nx = qx + DX[d];
                        const int ny = qy + DY[d];
                        if (nx < 0 || ny < 0 || nx >= blocksW || ny >= blocksH) continue;
                        const int nidx = ny * blocksW + nx;
                        if (!developed[nidx] || clusterId[nidx] != -1) continue;
                        clusterId[nidx] = cid;
                        queue.push_back({nx, ny});
                    }
                }
            }
        }
    }
    std::vector<int> clusterCenterWx(sumX.size()), clusterCenterWy(sumX.size());
    for (size_t c = 0; c < sumX.size(); ++c) {
        clusterCenterWx[c] = static_cast<int>(sumX[c] / count[c]);
        clusterCenterWy[c] = static_cast<int>(sumY[c] / count[c]);
    }

    // One raw terrain contribution per park-like structure origin. Smooth
    // using the shared Micropolis kernel, before pollution and clamping.
    parkTerrain_.init(mapWidth_, mapHeight_);
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* structure) {
        const int item = structure->getItemID();
        if (usesParkTerrain(item)) parkTerrain_.addSource(x,y,getParkLandValueBonus(item));
    });

    // SC formula (scan.cpp::pollutionTerrainLandValueScan), evaluated per
    // block. Distance is measured to THIS block's cluster centre.
    //     dis = 34 - getCityCenterDistance(worldX, worldY) / 2;
    //     dis = dis * 4;
    //     dis += terrainDensity;
    //     dis -= pollution;
    //     clamp(1, 250) if developed, else 0
    // Manhattan distance is clamped to 64 inside computeBaseLandValue.
    for (int by = 0; by < blocksH; ++by) {
        for (int bx = 0; bx < blocksW; ++bx) {
            const int idx = by * blocksW + bx;
            if (!developed[idx]) {
                landValueMap_.set(bx, by, 0);
                continue;
            }
            const int worldX = bx * bs;
            const int worldY = by * bs;
            const int cid = clusterId[idx];
            const int dist = std::abs(worldX - clusterCenterWx[cid])
                           + std::abs(worldY - clusterCenterWy[cid]);
            // Crime is read from the PREVIOUS scan tick — crimeRateMap_
            // is reset and recomputed below. This matches SC's one-tick
            // feedback delay; on the very first scan crime is zero and
            // no -20 penalty fires.
            const int v = computeBaseLandValue(
                dist,
                terrainRaw[idx] + parkTerrain_.landValueContribution(worldX,worldY,bs),
                pollutionDensityMap_.get(bx, by),
                crimeRateMap_.get(bx, by));
            landValueMap_.set(bx, by, static_cast<uint8_t>(v));
        }
    }

    // Preserve the separate Palace/Stadium civic bonus. Walls and turrets
    // now contribute through terrain above, never through radial stamps.
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const int parkBonus = getParkLandValueBonus(pStruct->getItemID());
        if (parkBonus > 0 && !usesParkTerrain(pStruct->getItemID())) {
            const int itemID = pStruct->getItemID();
            const int radius = getParkLandValueRadius(itemID);
            stampFalloff(landValueMap_, x, y,
                         radius, parkBonus, kMaxLandValue);
        }
    });

    // "Sand-as-water" — long-reach bonus from open sand/dunes. The
    // terrainRaw+smooth pass above gives close-adjacency a localised
    // boost, but the /8 dilution in box-smooth means a zone more than
    // one block away from sand sees almost nothing. Stamping a falloff
    // from each sand tile bypasses that dilution, so a zone sitting at
    // the edge of the rock can reliably reach tier 3 (luxury) when sand
    // is on one or two sides. Stamps accumulate per source tile, so a
    // solid sand band produces a stronger lift than an isolated tile —
    // matching SC's "shore property is premium" intent.
    {
    AITelemetry::PerformanceScope tv("city.effects.tv_sandstamp",currentGame->getGameCycleCount());
    for (int wy = 0; wy < mapHeight_; ++wy) {
        for (int wx = 0; wx < mapWidth_; ++wx) {
            const Tile* t = map.getTile(wx, wy);
            if (!t) continue;
            if ((t->isSand() || t->isDunes()) && !t->hasAGroundObject()) {
                stampFalloff(landValueMap_, wx, wy,
                             kSandBonusRadius, kSandLandValueBonus,
                             kMaxLandValue);
            }
        }
    }
    }

    // Hostile combat nearby reduces the value of the affected owner's property.
    // Use the strongest nearby threat, not an unlimited sum over an army blob.
    hostileLandValuePenaltyMap_.init(mapWidth_, mapHeight_, bs);
    {
    AITelemetry::PerformanceScope tv("city.effects.tv_hostile",currentGame->getGameCycleCount());
    // hostileLandValuePenalty() is zero beyond a squared separation of 16, so
    // only units within kHostilePenaltyReach tiles of the footprint can ever
    // contribute. Bucket the attack-capable units once and visit just the cells
    // that reach each building, instead of the full structure x unit cross
    // product. Every surviving pair still runs the original owner, visibility
    // and exact-distance tests, and the map accumulates with max(), so the
    // result does not depend on which pairs were skipped or in what order.
    constexpr int kHostilePenaltyReach = 4;   // floor(sqrt(16))
    constexpr int kHostileCell = 8;
    const int gridW = std::max(1, (mapWidth_  + kHostileCell - 1) / kHostileCell);
    const int gridH = std::max(1, (mapHeight_ + kHostileCell - 1) / kHostileCell);
    std::vector<std::vector<const UnitBase*>> hostileCells(
        static_cast<size_t>(gridW) * static_cast<size_t>(gridH));
    for (const UnitBase* unit : unitList) {
        if (!unit->isActive() || !unit->canAttack()) continue;
        const Coord p = unit->getLocation();
        // A unit off the map edge still penalised a near-edge building in the
        // original loop, which had no bounds test at all: a unit at (-1,0) is
        // one tile from a footprint at (0,0). Such units are therefore clamped
        // into the nearest edge cell, never dropped. Clamping only widens the
        // candidate set for edge cells; the exact dx/dy test below still
        // decides, so a distant off-map unit scores zero exactly as before.
        const int cellX = std::min(gridW - 1, std::max(0, p.x) / kHostileCell);
        const int cellY = std::min(gridH - 1, std::max(0, p.y) / kHostileCell);
        hostileCells[static_cast<size_t>(cellY) * gridW + cellX].push_back(unit);
    }
    std::vector<const UnitBase*> nearby;
    for (const StructureBase* building : structureList) {
        if (!building->isActive()) continue;
        const Coord origin = building->getLocation(), size = building->getStructureSize();
        const int team = building->getOwner()->getTeamID();
        const int cellX0 = std::max(0, origin.x - kHostilePenaltyReach) / kHostileCell;
        const int cellY0 = std::max(0, origin.y - kHostilePenaltyReach) / kHostileCell;
        const int cellX1 = std::min(gridW - 1,
            std::max(0, origin.x + size.x - 1 + kHostilePenaltyReach) / kHostileCell);
        const int cellY1 = std::min(gridH - 1,
            std::max(0, origin.y + size.y - 1 + kHostilePenaltyReach) / kHostileCell);
        nearby.clear();
        for (int cy = cellY0; cy <= cellY1; ++cy)
            for (int cx = cellX0; cx <= cellX1; ++cx) {
                const auto& bucket = hostileCells[static_cast<size_t>(cy) * gridW + cx];
                nearby.insert(nearby.end(), bucket.begin(), bucket.end());
            }
        for (const UnitBase* unit : nearby) {
            if (unit->getOwner()->getTeamID() == team || !unit->isVisible(team)) continue;
            const Coord p = unit->getLocation();
            const int dx = std::max({origin.x-p.x, 0, p.x-(origin.x+size.x-1)});
            const int dy = std::max({origin.y-p.y, 0, p.y-(origin.y+size.y-1)});
            const int penalty = hostileLandValuePenalty(dx*dx + dy*dy);
            if (!penalty) continue;
            for (int y=origin.y; y<origin.y+size.y; ++y)
                for (int x=origin.x; x<origin.x+size.x; ++x) {
                    const int bx=x/bs, by=y/bs;
                    hostileLandValuePenaltyMap_.set(bx,by,std::max<int>(hostileLandValuePenaltyMap_.get(bx,by),penalty));
                }
        }
    }
    }
    for (int by=0; by<blocksH; ++by) for (int bx=0; bx<blocksW; ++bx) {
        const int value = landValueMap_.get(bx,by);
        if (value > 0) landValueMap_.set(bx,by,std::max(1,value-hostileLandValuePenaltyMap_.get(bx,by)));
    }

    // All buildings devalue their occupied land when damaged, regardless of
    // role or owner. A block shared by footprints uses the worst condition;
    // never compound penalties based on structure iteration order.
    std::vector<int> conditionValues(blocksW * blocksH, kMaxLandValue);
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* building) {
        const Coord size = building->getStructureSize();
        for (int by=y/bs; by<=(y+size.y-1)/bs; ++by)
            for (int bx=x/bs; bx<=(x+size.x-1)/bs; ++bx) {
                if (bx<0 || by<0 || bx>=blocksW || by>=blocksH) continue;
                const int value = conditionLandValue(landValueMap_.get(bx,by),
                    building->getHealth(), building->getMaxHealth());
                conditionValues[by*blocksW+bx] = std::min(conditionValues[by*blocksW+bx], value);
            }
    });
    for (int by=0; by<blocksH; ++by) for (int bx=0; bx<blocksW; ++bx)
        landValueMap_.set(bx,by,std::min<int>(landValueMap_.get(bx,by),conditionValues[by*blocksW+bx]));

    phase.next("city.effects.population");
    // Population density is needed by the crime formula below (SC's
    // `z += populationDensityMap.worldGet(x, y)` term), so populate it
    // BEFORE clearing and recomputing crime. Micropolis includes every
    // populated R/C/I zone: the scanner multiplies R population by eight,
    // while C/I first use their source `* 8` density weighting. DuneCity's
    // structures are 2x2 rather than Micropolis's 3x3, but using the same
    // density input scale keeps the crime thresholds comparable. The traffic
    // map is built later — it's not consumed by any scan, only the overlay UI.
    populationDensityMap_.init(mapWidth_, mapHeight_, populationDensityMap_.getBlockSize());
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const Tile* originTile = map.getTile(x, y);
        const int level = cityLevelOf(originTile, pStruct);
        const auto role = getStructureCityRole(pStruct->getItemID());
        if (role == CityRole::None) return;
        const int pop      = getStructurePopulation(pStruct, level);
        const int sourceDensity = role == CityRole::Residential ? pop : pop * 8;
        const int popStamp = std::min(254, std::max(0, sourceDensity * 8));
        const int bs = populationDensityMap_.getBlockSize();
        populationDensityMap_.set(x/bs,y/bs,static_cast<uint8_t>(popStamp));
    });
    smoothPopulationDensity(populationDensityMap_,mapWidth_,mapHeight_);

    phase.next("city.effects.crime_rebels");
    // Base crime, SC-Classic style:
    //   z = 128 - landValue + popDensity; clamp to 300;
    //   subtract police coverage; clamp to 0–250.
    // The crime map was intentionally NOT reset at scan start so the
    // land-value pass above could read last tick's crime for the
    // `crime > 190 → -20 land value` feedback. Reset and recompute now.
    crimeRateMap_.init(mapWidth_, mapHeight_, crimeRateMap_.getBlockSize());
    policeCoverageMap_.init(mapWidth_, mapHeight_, crimeRateMap_.getBlockSize());
    crimeBeforePoliceMap_.init(mapWidth_, mapHeight_, crimeRateMap_.getBlockSize());
    CityMapLayer<int32_t> stationMap;
    stationMap.init(mapWidth_, mapHeight_, kPoliceMapBlockSize);
    // Accumulate every source once per cell. Separate buildings stack.
    // Crime remains a global geographic layer; billing/funding is per owner.
    // The local-player budget controls only that owner's service funding.
    FixPoint nominalCosts[kMaxCityHouses] = {};
    for (int h = 0; h < kMaxCityHouses; ++h) {
        houseState_[h].nominalPoliceCost = 0;
        houseState_[h].hasStadium = false;
        houseState_[h].hasPalace  = false;
        houseState_[h].hasAirport = false;
        houseState_[h].hasStarport = false;
    }
    forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
        const int itemID = pStruct->getItemID();
        const House* owner = pStruct->getOwner();
        const int hID = owner ? owner->getHouseID() : -1;
        if (hID < 0 || hID >= kMaxCityHouses) return;
        auto& hs = houseState_[hID];

        // Police coverage — applies to global crime map for all owners
        const int rawCoverage = getPoliceCoverage(itemID);
        if (rawCoverage > 0) {
            const Coord size = pStruct->getStructureSize();
            const auto source = policeSource(map, x, y, size.x, size.y, rawCoverage,
                hs.policeFundingPercent, owner->getProducedPower() >= owner->getPowerRequirement());
            addPoliceCoverage(stationMap, mapWidth_, mapHeight_, source.x, source.y, source.strength);
            nominalCosts[hID] += getPoliceAnnualCost(itemID);
        }

        // Civic building detection per house
        switch (itemID) {
            case Structure_Stadium:  hs.hasStadium  = true; break;
            case Structure_Palace:   hs.hasPalace   = true; break;
            case Structure_Airport:  hs.hasAirport  = true; break;
            case Structure_StarPort: hs.hasStarport = true; break;
            default: break;
        }
    });

    smoothPoliceCoverage(stationMap, mapWidth_, mapHeight_);
    for (int y=0;y<mapHeight_;y+=policeCoverageMap_.getBlockSize())
        for (int x=0;x<mapWidth_;x+=policeCoverageMap_.getBlockSize())
            policeCoverageMap_.set(x/policeCoverageMap_.getBlockSize(),y/policeCoverageMap_.getBlockSize(),
                stationMap.worldGet(x,y));
    // Keep base crime wide until all service coverage has been subtracted.
    const int crimeBlockSize = crimeRateMap_.getBlockSize();
    for (int by=0; by<(mapHeight_+crimeBlockSize-1)/crimeBlockSize; ++by) {
        for (int bx=0; bx<(mapWidth_+crimeBlockSize-1)/crimeBlockSize; ++bx) {
            const int lv=landValueMap_.get(bx,by), pop=populationDensityMap_.get(bx,by);
            crimeBeforePoliceMap_.set(bx,by,computeCrimeBeforePolice(lv,pop));
            crimeRateMap_.set(bx,by,computeCrimeAfterPolice(lv,pop,policeCoverageMap_.get(bx,by)));
        }
    }

    // One outbreak timer per owner and 16x16 district, not per crime-map tile.
    const int districtWidth = (mapWidth_ + 15) / 16;
    const int districtsPerHouse = districtWidth * ((mapHeight_ + 15) / 16);
    std::vector<const StructureBase*> unrestOrigins(crimeUnrestProgress_.size(), nullptr);
    std::vector<int> unrestCrime(crimeUnrestProgress_.size(), 0);
    std::vector<int> districtTroops(crimeUnrestProgress_.size(), 0);
    std::vector<int> dangerousBuildings(crimeUnrestProgress_.size(), 0);
    std::vector<int> districtCrimeTotal(crimeUnrestProgress_.size(), 0);
    for (const StructureBase* building : structureList) {
        if (!building->isActive()) continue;
        const int h = building->getOwner()->getHouseID();
        const Coord p = building->getLocation();
        if (h < 0 || h >= kMaxCityHouses || p.x < 0 || p.y < 0 || p.x >= mapWidth_ || p.y >= mapHeight_) continue;
        const auto index = h * districtsPerHouse + (p.y / 16) * districtWidth + p.x / 16;
        const int crime = crimeRateMap_.worldGet(p.x, p.y);
        const int level = cityLevelOf(map.getTile(p.x,p.y),building);
        const int strength = crimeRebelsForDensity(std::max(level,getStructurePopulation(building,level)>0 ? 1 : 0));
        if (crime >= kCrimeDangerousThreshold && strength > 0) {
            districtTroops[index] += strength;
            ++dangerousBuildings[index];
            districtCrimeTotal[index] += crime;
            if (crime > unrestCrime[index]) { unrestCrime[index] = crime; unrestOrigins[index] = building; }
        }
    }
    const uint32_t unrestThreshold = MILLI2CYCLES(kCrimeUnrestBuildupMs) * 100u;
    constexpr uint32_t gatheringMs=30000;
    std::vector<int> readyStrength(crimeUnrestProgress_.size(),0);
    for (size_t index = 0; index < crimeUnrestProgress_.size(); ++index) {
        const int owner = static_cast<int>(index) / districtsPerHouse;
        const int displayedPopulation = houseState_[owner].getTotalPop() * kPopDisplayMultiplier;
        const int meanCrime = dangerousBuildings[index] ? districtCrimeTotal[index]/dangerousBuildings[index] : 0;
        readyStrength[index] = crimeUnrestProgress_[index].advance(cityCrimeUnrestRate(meanCrime,displayedPopulation),
            districtTroops[index],kCyclesPerCityDay,unrestThreshold);
    }
    const auto outbreaks=crimeOutbreakGroups(crimeUnrestProgress_,readyStrength,districtWidth,(mapHeight_+15)/16,
        unrestThreshold,MILLI2CYCLES(gatheringMs));
    for (const auto& group:outbreaks) {
        size_t index=group.front();
        int requested=0,buildingCount=0,crimeTotal=0;
        AITelemetry::Record contributions;
        for (const auto member:group) {
            requested+=readyStrength[member];
            buildingCount+=dangerousBuildings[member];
            crimeTotal+=districtCrimeTotal[member];
            if(unrestCrime[member]>unrestCrime[index]) index=member;
            contributions.set(std::to_string(member%districtsPerHouse),readyStrength[member]);
            crimeUnrestProgress_[member].reset();
        }
        const int owner=static_cast<int>(index)/districtsPerHouse;
        const int displayedPopulation=houseState_[owner].getTotalPop()*kPopDisplayMultiplier;
        const int meanCrime=buildingCount ? crimeTotal/buildingCount : 0;
        const int rate=cityCrimeUnrestRate(meanCrime,displayedPopulation);
        const StructureBase* origin = unrestOrigins[index];
        if (!origin) continue;
        // Use an existing opposing faction; never commandeer a playable house
        // slot or create a new victory participant for ambient unrest.
        House* hostile = nullptr;
        const int firstHouse = static_cast<int>((index + currentGame->getGameCycleCount() / MILLI2CYCLES(60000)) % NUM_HOUSES);
        for (int offset = 0; offset < NUM_HOUSES; ++offset) {
            const int h = (firstHouse + offset) % NUM_HOUSES;
            House* candidate = currentGame->getHouse(h);
            if (candidate && candidate->getTeamID() != origin->getOwner()->getTeamID()
                && candidate->getNumStructures() > 0
                && currentGame->objectData.data[Unit_Trooper][h].enabled) { hostile = candidate; break; }
        }
        int spawned = 0;
        AITelemetry::Record members;
        const Coord hotspot = origin->getLocation();
        const auto sites = crimeSpawnSites(hotspot.x,hotspot.y,mapWidth_,mapHeight_);
        size_t nextSite = 0;
        if (hostile) for (int n = 0; n < requested; ++n) {
            if (hostile->isUnitLimitReached(Unit_Trooper)) break;
            UnitBase* unit = hostile->createUnit(Unit_Trooper);
            if (!unit) break;
            // All members emerge at the same hotspot, filling nearby legal
            // infantry slots before expanding the group outwards.
            while (nextSite < sites.size() && !unit->canPass(sites[nextSite].first,sites[nextSite].second)) ++nextSite;
            if (nextSite == sites.size()) { unit->cancelDeployment(); break; }
            const Coord spot(sites[nextSite].first,sites[nextSite].second);
            unit->deploy(spot);
            unit->setGuardPoint(spot);
            unit->doSetAttackMode(HUNT);
            unit->doAttackObject(origin, true);
            members.set(std::to_string(unit->getObjectID()), AITelemetry::Record().set("x",spot.x).set("y",spot.y).set("origin",origin->getObjectID()));
            ++spawned;
        }
        AITelemetry::log().write(currentGame->getGameCycleCount(), origin->getOwner()->getHouseID(), -1,
            "crime_unrest", AITelemetry::Record().set("district", static_cast<int>(index % districtsPerHouse))
                .set("crime",unrestCrime[index]).set("origin",origin->getObjectID())
                .set("population", displayedPopulation).set("minimum_population", 5000)
                .set("dangerous_buildings",buildingCount).set("mean_dangerous_crime",meanCrime)
                .set("contributing_districts",group.size()).set("district_strengths",contributions).set("gathering_ms",gatheringMs)
                .set("requested",requested).set("strength_rule","occupied_density_1_2_3")
                .set("hotspot_x",hotspot.x).set("hotspot_y",hotspot.y).set("buildup_rate",rate).set("base_buildup_ms",kCrimeUnrestBuildupMs)
                .set("hostile_house",hostile ? hostile->getHouseID() : -1).set("spawned",spawned)
                .set("members",members).set("reason",!hostile ? "no_enemy_house" : spawned < requested ? "capacity_or_space" : "high_crime"));
        if (spawned > 0 && pLocalHouse == origin->getOwner()) {
            currentGame->addUrgentMessageToNewsTicker(
                _("CRIMINAL GANGS OUT OF CONTROL: Troopers have taken to the streets!"));
        }
    }

    // Preserve the legacy integer save cache, rounding only the aggregate.
    for (int h=0; h<kMaxCityHouses; ++h) houseState_[h].nominalPoliceCost = nominalCosts[h].lround();

    phase.next("city.effects.traffic_status");
    // Keep accumulated trips and decay each 2x2 cell once per city day.
    // Building proximity does not generate traffic. The following growth
    // phase adds sampled successful journeys to this persistent layer.
    CityTraffic::decay(trafficDensityMap_,mapWidth_,mapHeight_);

    // Compute city-wide environmental summaries at each house's structure
    // positions. These are shown in the budget, used for the crime warning,
    // and intentionally stay derived rather than increasing savegame state.
    {
        const int bsLv = landValueMap_.getBlockSize();
        const int bsPollution = pollutionDensityMap_.getBlockSize();
        const int bsCrime = crimeRateMap_.getBlockSize();
        const int bsTraffic = trafficDensityMap_.getBlockSize();
        int lvTotal[kMaxCityHouses] = {}, pollutionTotal[kMaxCityHouses] = {};
        int crimeTotal[kMaxCityHouses] = {}, trafficTotal[kMaxCityHouses] = {};
        int sampleCount[kMaxCityHouses] = {};
        forEachStructureOrigin(map, [&](int x, int y, const StructureBase* pStruct) {
            const House* owner = pStruct->getOwner();
            if (!owner) return;
            const int hID = owner->getHouseID();
            if (hID < 0 || hID >= kMaxCityHouses) return;
            const int lv = landValueMap_.get(x / bsLv, y / bsLv);
            if (lv > 0) {
                lvTotal[hID] += lv;
                pollutionTotal[hID] += pollutionDensityMap_.get(x / bsPollution, y / bsPollution);
                crimeTotal[hID] += crimeRateMap_.get(x / bsCrime, y / bsCrime);
                trafficTotal[hID] += trafficDensityMap_.get(x / bsTraffic, y / bsTraffic);
                sampleCount[hID]++;
            }
        });
        for (int h = 0; h < kMaxCityHouses; ++h) {
            const int count = sampleCount[h];
            houseState_[h].avgLandValue = count > 0 ? lvTotal[h] / count : 0;
            environmentStatus_[h] = CityEnvironmentStatus{
                count > 0 ? lvTotal[h] / count : 0,
                count > 0 ? pollutionTotal[h] / count : 0,
                count > 0 ? crimeTotal[h] / count : 0,
                count > 0 ? trafficTotal[h] / count : 0,
                count};

            // Micropolis announces high city crime once the city average is
            // above 100. Keep a small hysteresis band so one scan's rounding
            // cannot repeatedly fill the ticker. Only the local owner gets a
            // UI message; it never influences deterministic game state.
            constexpr int kCrimeWarningThreshold = 101;
            constexpr int kCrimeWarningClearThreshold = 96;
            if (environmentStatus_[h].averageCrime >= kCrimeWarningThreshold) {
                if (!crimeWarningActive_[h] && pLocalHouse && pLocalHouse->getHouseID() == h) {
                    currentGame->addUrgentMessageToNewsTicker(
                        _("WARNING: Criminal activity is rising. Strengthen the police patrols."));
                }
                crimeWarningActive_[h] = true;
            } else if (environmentStatus_[h].averageCrime <= kCrimeWarningClearThreshold) {
                crimeWarningActive_[h] = false;
            }
        }
    }

    // Decay growth rate map toward zero.
    decayGrowthRateMap();
}

void CitySimulation::reconcileLoadedMapState(uint32_t gameCycleCount) {
    if (!initialized_ || !currentGameMap) return;

    const int beforeRes = getResPop();
    const int beforeCom = getComPop();
    const int beforeInd = getIndPop();
    int cityRoleStructures = 0;
    int zoneStructures = 0;

    Map& map = *currentGameMap;
    for (int y = 0; y < map.getSizeY(); ++y) {
        for (int x = 0; x < map.getSizeX(); ++x) {
            Tile* t = map.getTile(x, y);
            if (!t || !t->hasANonInfantryGroundObject()) continue;
            ObjectBase* pObj = t->getNonInfantryGroundObject();
            if (!pObj || !pObj->isAStructure()) continue;

            auto* pStruct = static_cast<StructureBase*>(pObj);
            if (pStruct->getLocation().x != x || pStruct->getLocation().y != y) {
                continue;
            }

            if (getStructureCityRole(pStruct->getItemID()) == CityRole::None) {
                pStruct->setCityOccupancy(0);
                continue;
            }
            ++cityRoleStructures;

            auto* pZone = dynamic_cast<ZoneStructure*>(pStruct);
            if (!pZone) {
                pStruct->setCityOccupancy(effectiveCityLevel(pStruct->getItemID(),
                    std::max<int>(1, pStruct->getCityOccupancy())));
                continue;
            }

            ++zoneStructures;
            uint8_t density = 0;
            const Coord pos = pZone->getLocation();
            for (int dy = 0; dy < pZone->getStructureSizeY(); ++dy) {
                for (int dx = 0; dx < pZone->getStructureSizeX(); ++dx) {
                    Tile* zt = map.getTile(pos.x + dx, pos.y + dy);
                    if (zt) density = std::max<uint8_t>(density, zt->getCityZoneDensity());
                }
            }
            for (int dy = 0; dy < pZone->getStructureSizeY(); ++dy) {
                for (int dx = 0; dx < pZone->getStructureSizeX(); ++dx) {
                    Tile* zt = map.getTile(pos.x + dx, pos.y + dy);
                    if (!zt) continue;
                    zt->setCityZoneType(pZone->getZoneType());
                    zt->setCityZoneDensity(density);
                }
            }
            if (pZone->getZoneType() == ZoneType::Residential)
                pZone->setResidentialPopulation(pZone->getResidentialPopulation());
            else pZone->refreshZonePowerDraw();
        }
    }

    const uint32_t totalDays = gameCycleCount / kCyclesPerCityDay;
    cityYear_ = static_cast<int>(totalDays / kCityDaysPerYear);
    cityDay_  = static_cast<int>(totalDays % kCityDaysPerYear);
    lastProcessedDay_ = totalDays;
    lastBudgetTick_ = gameCycleCount / kCyclesPerBudgetTick;
    pendingGrowthPhase_ = false;

    int recoveredValves = 0;
    for (int h = 0; h < kMaxCityHouses; ++h) {
        auto& hs = houseState_[h];
        const int16_t resValve = recoverLoadedEmptyPopulationValve(hs.resPop, hs.resValve);
        const int16_t comValve = recoverLoadedEmptyPopulationValve(hs.comPop, hs.comValve);
        const int16_t indValve = recoverLoadedEmptyPopulationValve(hs.indPop, hs.indValve);
        recoveredValves += (resValve != hs.resValve) + (comValve != hs.comValve)
                         + (indValve != hs.indValve);
        hs.resValve = resValve;
        hs.comValve = comValve;
        hs.indValve = indValve;
    }

    runEffectsScans();
    runZoneGrowth();

    SDL_Log("[CitySim] load-reconcile cityRoleStructures=%d zones=%d "
            "recoveredValves=%d pop R/C/I %d/%d/%d -> %d/%d/%d "
            "valves=R%+d C%+d I%+d",
            cityRoleStructures, zoneStructures,
            recoveredValves,
            beforeRes, beforeCom, beforeInd,
            getResPop(), getComPop(), getIndPop(),
            getResValve(), getComValve(), getIndValve());
}

void CitySimulation::runZoneGrowth() {
    if (!currentGameMap) return;

    const Map& map = *currentGameMap;
    const int bs = landValueMap_.getBlockSize();

    // We treat zones AND non-zone city-role structures the same: each has
    // a current "level" (0..maxLevel), each contributes supply at that
    // level, each can grow toward its maxLevel as long as it's powered,
    // land-value floor met, and demand thresholds satisfied.
    struct CityNode {
        int x, y;
        StructureBase* pStruct;
        ZoneStructure*  pZone;     // nullptr for non-zone city-role structures
        CityRole       role;
        int            level;
        int            maxLevel;
        int            supplyR, supplyC, supplyI;
        bool           isDuneStructure;  // true for non-zone city-role (don't destroy)
    };
    std::vector<CityNode> nodes;

    for (int y = 0; y < map.getSizeY(); ++y) {
        for (int x = 0; x < map.getSizeX(); ++x) {
            const Tile* t = map.getTile(x, y);
            if (!t || !t->hasANonInfantryGroundObject()) continue;
            ObjectBase* pObj = const_cast<Tile*>(t)->getNonInfantryGroundObject();
            if (!pObj || !pObj->isAStructure()) continue;
            StructureBase* pStruct = static_cast<StructureBase*>(pObj);
            if (pStruct->getLocation().x != x || pStruct->getLocation().y != y) continue;

            const int itemID = pStruct->getItemID();
            const CityRole role = getStructureCityRole(itemID);
            if (role == CityRole::None) continue;

            ZoneStructure* pZone = dynamic_cast<ZoneStructure*>(pStruct);
            const int level = pZone ? t->getCityZoneDensity()
                                    : effectiveCityLevel(itemID, std::max<int>(1, pStruct->getCityOccupancy()));
            const int maxLevel = getStructureMaxLevel(itemID);

            nodes.push_back({
                x, y, pStruct, pZone, role, level, maxLevel,
                getStructureResidentialSupply(pStruct, level),
                getCommercialSupply (itemID, level),
                getIndustrialSupply (itemID, level),
                pZone == nullptr,
            });
        }
    }

    // One shared budget per house, updated after each node. Multiple growing
    // lots in the same scan must not each spend the same remaining population.
    std::array<int, kMaxCityHouses> cityPopulation{};
    std::array<int, kMaxCityHouses> cityLimits{};
    for(int h=0; h<kMaxCityHouses; ++h) {
        const auto* house = currentGame->getHouse(static_cast<HOUSETYPE>(h));
        if(!house) continue;
        for(const auto& player : house->getPlayerList()) {
            const int limit = player->getCityPopulationLimit(map.getSizeX()*map.getSizeY());
            // A helper's construction ceiling must not cap its human partner's
            // natural city growth. Human and other unrestricted controllers
            // keep the whole house's natural growth unrestricted.
            if(limit <= 0) { cityLimits[h]=0; break; }
            cityLimits[h] = cityLimits[h] ? std::min(cityLimits[h],limit) : limit;
        }
    }
    for(const auto& n : nodes) {
        const int h = n.pStruct->getOwner()->getHouseID();
        cityPopulation[h] += getStructurePopulation(n.pStruct,n.level)
            + (n.pStruct->getItemID()==Structure_Palace ? getPalaceCommercialPopulation(n.level) : 0);
    }

    // Reserve initial population for orders already accepted by construction
    // yards, so natural growth cannot spend the space before placement occurs.
    for(const auto* structure : structureList) {
        const auto* builder = dynamic_cast<const BuilderBase*>(structure);
        if(!builder || builder->getProductionQueueSize()==0) continue;
        const int h=builder->getOwner()->getHouseID();
        if(cityLimits[h]<=0) continue;
        for(const auto& queued : builder->getBuildList()) {
            if(!isStructure(queued.itemID)) continue;
            cityPopulation[h] += queued.num * (getZonePopulation(queued.itemID,1)
                + (queued.itemID==Structure_Palace ? getPalaceCommercialPopulation(1) : 0));
        }
    }

    // Global supply totals (all players) — used in growth loop employment tracking
    int totalResidentialSupply = 0;
    int totalJobSupply = 0;
    for (const auto& n : nodes) {
        totalResidentialSupply += n.supplyR;
        totalJobSupply         += n.supplyC + n.supplyI;
    }
    int freeResidents = std::max(0, totalResidentialSupply - totalJobSupply);

    // ---- Compute RCI population and demand valves PER HOUSE ----
    // Each house gets its own population tally and demand valves.
    // Zone growth (below) uses the owner's demand valves for zscore.
    for (int h = 0; h < kMaxCityHouses; ++h) {
        auto& hs = houseState_[h];
        int curRes = 0, curCom = 0, curInd = 0;
        for (const auto& n : nodes) {
            if (!n.pStruct->getOwner() || n.pStruct->getOwner()->getHouseID() != h)
                continue;
            const int itemID = n.pStruct->getItemID();
            const int pop = getStructurePopulation(n.pStruct, n.level);
            switch (n.role) {
                case CityRole::Residential: curRes += pop; break;
                case CityRole::Commercial:  curCom += pop; break;
                case CityRole::Industrial:  curInd += pop; break;
                default: break;
            }
            if (itemID == Structure_Palace) {
                curCom += getPalaceCommercialPopulation(n.level);
            }
        }

        ValveInputs vi;
        vi.resPop = curRes;
        vi.comPop = curCom;
        vi.indPop = curInd;
        vi.prevResPop = hs.prevResPop;
        vi.prevComPop = hs.prevComPop;
        vi.prevIndPop = hs.prevIndPop;
        vi.resValve = hs.resValve;
        vi.comValve = hs.comValve;
        vi.indValve = hs.indValve;
        vi.taxRate = cityTax_;
        vi.hasStadium  = hs.hasStadium;
        vi.hasPalace   = hs.hasPalace;
        vi.hasAirport  = hs.hasAirport;
        vi.hasStarport = hs.hasStarport;

        const int16_t prevResValve = vi.resValve;
        const int16_t prevComValve = vi.comValve;
        const int16_t prevIndValve = vi.indValve;
        const ValveOutputs vo = computeDemandValves(vi);
        hs.civicDemandBlocked = vo.civicDemandBlocked;
        hs.resValve = vo.resValve;
        hs.comValve = vo.comValve;
        hs.indValve = vo.indValve;

        // Announce actual demand caps to the human controlling this house.
        // Space notices so simultaneous requirements cannot erase each other.
        if (pLocalHouse && pLocalHouse->getHouseID() == h) {
            const uint8_t notice = civicDemandNotices_[h].update(
                missingDemandCivics(vi), vo.civicDemandBlocked,
                currentGame->getGameCycleCount(), kCyclesPerCityYear / 6);
            if (notice == NeedStadium) {
                currentGame->addUrgentMessageToNewsTicker(
                    _("Residents demand a Stadium. Build one to unlock residential growth."));
            } else if (notice == NeedAirport) {
                currentGame->addUrgentMessageToNewsTicker(
                    _("Commerce demands an Airport. Build one to unlock commercial growth."));
            } else if (notice == NeedStarport) {
                currentGame->addUrgentMessageToNewsTicker(
                    _("Industry demands a Starport (seaport). Build one to unlock industrial growth."));
            }
        }


        // Diagnostic: log per-tick valve deltas for the local player's house so
        // future logs can confirm the fix (grep for "[CitySim] valve-debug").
        if (h == localHouseID()) {
            SDL_Log("[CitySim] valve-debug house=%d day=%d/%d "
                    "pop(R/C/I)=%d/%d/%d prevPop(R/C/I)=%d/%d/%d "
                    "deltaR=%+d deltaC=%+d deltaI=%+d "
                    "valves=R%+d C%+d I%+d",
                    h, cityYear_, cityDay_,
                    curRes, curCom, curInd,
                    vi.prevResPop, vi.prevComPop, vi.prevIndPop,
                    static_cast<int>(vo.resValve) - prevResValve,
                    static_cast<int>(vo.comValve) - prevComValve,
                    static_cast<int>(vo.indValve) - prevIndValve,
                    vo.resValve, vo.comValve, vo.indValve);
        }

        hs.prevResPop = curRes;
        hs.prevComPop = curCom;
        hs.prevIndPop = curInd;
    }

    // Traffic connectivity engine (BFS along road network).
    TrafficSimulation trafficSim;
    trafficSim.init(this);

    // Initialize growth rate map if needed.
    if (growthRateMap_.getBlockSize() == 0 || mapWidth_ == 0) {
        growthRateMap_.init(mapWidth_, mapHeight_, 2);
    }

    // Spatial supply grids: pre-bin each node's R/C/I supply into block
    // buckets so the per-node supply lookup is O(blocks-in-radius) instead
    // of O(N) where N = total nodes. Block size 4 with kSupplyRadius=16
    // means scanning a 9x9 block neighborhood (81 blocks) per node.
    constexpr int kSupplyBlockSize = 4;
    const int sbw = (mapWidth_  + kSupplyBlockSize - 1) / kSupplyBlockSize;
    const int sbh = (mapHeight_ + kSupplyBlockSize - 1) / kSupplyBlockSize;
    std::vector<int> supplyGridR(sbw * sbh, 0);
    std::vector<int> supplyGridC(sbw * sbh, 0);
    std::vector<int> supplyGridI(sbw * sbh, 0);
    for (const auto& n : nodes) {
        const int bx = n.x / kSupplyBlockSize;
        const int by = n.y / kSupplyBlockSize;
        const int idx = by * sbw + bx;
        supplyGridR[idx] += n.supplyR;
        supplyGridC[idx] += n.supplyC;
        supplyGridI[idx] += n.supplyI;
    }
    const int supplyBlockRadius = (kSupplyRadius + kSupplyBlockSize - 1) / kSupplyBlockSize;

    for (auto& n : nodes) {
        const Coord pos = n.pStruct->getLocation();
        if (pos.isInvalid()) continue;

        const int initialLevel = n.level;
        const int initialPopulation = getStructurePopulation(n.pStruct,n.level);
        const bool residentialLot = n.pZone && n.role == CityRole::Residential;
        const int nextResidentialPopulation = residentialLot
            ? ResidentialPopulation::grow(initialPopulation,populationDensityMap_.worldGet(pos.x,pos.y)) : 0;
        const int targetLevel = residentialLot ? ResidentialPopulation::density(nextResidentialPopulation) : n.level+1;

        const int ownerHouse = n.pStruct->getOwner()->getHouseID();
        const int palaceBefore = n.pStruct->getItemID()==Structure_Palace
            ? getPalaceCommercialPopulation(initialLevel) : 0;
        const int proposedPopulation = residentialLot ? nextResidentialPopulation
            : getZonePopulation(n.pStruct->getItemID(),targetLevel);
        const int palaceAfter = n.pStruct->getItemID()==Structure_Palace
            ? getPalaceCommercialPopulation(targetLevel) : 0;
        const bool withinCityLimit = cityLimits[ownerHouse] <= 0
            || (static_cast<int64_t>(cityPopulation[ownerHouse]) + proposedPopulation
                + palaceAfter - initialPopulation - palaceBefore) * kPopDisplayMultiplier <= cityLimits[ownerHouse];

        // Local supply within kSupplyRadius — summed from spatial grid blocks.
        int localComm = 0, localInd = 0, localRes = 0;
        {
            const int cbx = pos.x / kSupplyBlockSize;
            const int cby = pos.y / kSupplyBlockSize;
            const int bx0 = std::max(0, cbx - supplyBlockRadius);
            const int bx1 = std::min(sbw - 1, cbx + supplyBlockRadius);
            const int by0 = std::max(0, cby - supplyBlockRadius);
            const int by1 = std::min(sbh - 1, cby + supplyBlockRadius);
            for (int by = by0; by <= by1; ++by) {
                for (int bx = bx0; bx <= bx1; ++bx) {
                    const int idx = by * sbw + bx;
                    localRes  += supplyGridR[idx];
                    localComm += supplyGridC[idx];
                    localInd  += supplyGridI[idx];
                }
            }
        }

        const int landValue = landValueMap_.get(pos.x / bs, pos.y / bs);
        const int pollution = pollutionDensityMap_.get(
            pos.x / pollutionDensityMap_.getBlockSize(),
            pos.y / pollutionDensityMap_.getBlockSize());
        const int crime = crimeRateMap_.get(
            pos.x / crimeRateMap_.getBlockSize(),
            pos.y / crimeRateMap_.getBlockSize());
        const bool powered = n.pStruct->getOwner() &&
                             n.pStruct->getOwner()->getProducedPower() >=
                             n.pStruct->getOwner()->getPowerRequirement();

        bool grew = false;
        bool declined = false;

        // Traffic connectivity attempt: BFS along road network to find
        // the complementary zone type. R->C, C->I, I->R.
        TrafficResult traffic = TrafficResult::Connected;  // default for L0
        if (initialPopulation > 0) {
            ZoneType destZone = ZoneType::None;
            switch (n.role) {
                case CityRole::Residential: destZone = ZoneType::Commercial;  break;
                case CityRole::Commercial:  destZone = ZoneType::Industrial;  break;
                case CityRole::Industrial:  destZone = ZoneType::Residential; break;
                default: break;
            }
            if (destZone != ZoneType::None) {
                const int trafResult = trafficSim.makeTraffic(pos.x, pos.y, destZone, lastProcessedDay_);
                if (trafResult < 0)     traffic = TrafficResult::NoRoad;
                else if (trafResult == 0) traffic = TrafficResult::NoDestination;
                else                      traffic = TrafficResult::Connected;

                // Micropolis generates journeys in R/C/I zone updates, not in
                // special-building updates. Infrastructure remains a destination;
                // its employment must not add a second source of commuter trips.
                if (n.pZone && traffic == TrafficResult::Connected && CityTraffic::journeyDue(
                        n.role == CityRole::Residential,initialPopulation,pos.x,pos.y,lastProcessedDay_)) {
                    CityTraffic::addJourney(trafficDensityMap_,trafficSim.getLastPath(),
                        [&](int x,int y) {
                            const auto* tile = currentGameMap->getTile(x,y);
                            return tile && tile->isRoad();
                        });
                }
            }
        }

        // Zone score: owner's demand valve + local evaluation (traffic,
        // pollution, crime, land value per zone type).
        const int ownerID = n.pStruct->getOwner() ? n.pStruct->getOwner()->getHouseID() : 0;
        const auto& ownerState = houseState_[ownerID >= 0 && ownerID < kMaxCityHouses ? ownerID : 0];
        const int valve = (n.role == CityRole::Residential) ? ownerState.resValve
                        : (n.role == CityRole::Commercial)  ? ownerState.comValve
                        :                                     ownerState.indValve;
        int localEval = computeLocalEval(n.role, landValue, pollution,
                                         crime, traffic);
        // Commercial zones get supply-side and airport adjustments on top
        // of the base local eval (spec Section D: commercial desirability).
        if (n.role == CityRole::Commercial) {
            localEval += computeCommercialRate(localRes, localInd, ownerState.hasAirport);
        }
        const int zscore = computeZscore(valve, localEval, powered);

        // Stochastic growth gate: 1-in-16 chance per day.
        // L0->L1 bootstrap skips the gate.
        const uint32_t roll = (static_cast<uint32_t>(pos.x) * 73u
                             + static_cast<uint32_t>(pos.y) * 149u
                             + lastProcessedDay_) % 16u;
        const bool pollutionBlocked = isPollutionBlockingGrowth(pollution, n.role, targetLevel);
        const bool pollutionSlowed  = isPollutionSlowingGrowth(pollution, n.role);
        const bool growthRolled = (n.level == 0)
            || (roll == 0 && (!pollutionSlowed || (lastProcessedDay_ % 2u == 0)));

        // --- Growth attempt: requires zscore above threshold ---
        if (zscore > kZscoreGrowthGate && (residentialLot ? nextResidentialPopulation > initialPopulation : n.level < n.maxLevel)
            && growthRolled && !pollutionBlocked && withinCityLimit) {
            const int lvFloor = getDemandLandValueFloor(std::max(1,targetLevel));
            if (landValue >= lvFloor) {
                bool meets = false;
                const bool bootstrapping = (n.level == 0);
                switch (n.role) {
                    case CityRole::Residential:
                        meets = bootstrapping
                             || (localComm + localInd) >= getDemandJobsThreshold(targetLevel);
                        break;
                    case CityRole::Commercial:
                        meets = bootstrapping
                             || (localRes >= getDemandResidentialThreshold(targetLevel)
                                 && localInd >= getDemandJobsThreshold(targetLevel) / 2);
                        break;
                    case CityRole::Industrial:
                        meets = bootstrapping
                             || localRes >= getDemandResidentialThreshold(targetLevel);
                        break;
                    default: break;
                }
                // Transport gate: growth beyond level 1 requires road
                // connectivity (not just adjacency). No road = no growth.
                if (meets && !bootstrapping && traffic == TrafficResult::NoRoad) {
                    meets = false;
                }

                if (meets) {
                    if (residentialLot) {
                        n.pZone->setResidentialPopulation(nextResidentialPopulation);
                    } else if (n.pZone) {
                        for (int dy = 0; dy < n.pZone->getStructureSizeY(); ++dy) {
                            for (int dx = 0; dx < n.pZone->getStructureSizeX(); ++dx) {
                                Tile* zt = currentGameMap->getTile(pos.x + dx, pos.y + dy);
                                if (zt) zt->setCityZoneDensity(static_cast<uint8_t>(targetLevel));
                            }
                        }
                        n.pZone->refreshZonePowerDraw();
                    } else {
                        n.pStruct->setCityOccupancy(static_cast<uint8_t>(targetLevel));
                    }
                    n.level = targetLevel;
                    if (n.role != CityRole::Residential) {
                        const int newSupply = (n.role == CityRole::Commercial)
                            ? getCommercialSupply (n.pStruct->getItemID(), targetLevel)
                            : getIndustrialSupply (n.pStruct->getItemID(), targetLevel);
                        const int prevSupply = (n.role == CityRole::Commercial) ? n.supplyC : n.supplyI;
                        const int delta = std::max(0, newSupply - prevSupply);
                        freeResidents = std::max(0, freeResidents - delta);
                    }
                    grew = true;

                    // Update growth rate map.
                    {
                        const int gbs = growthRateMap_.getBlockSize();
                        const int gbx = pos.x / gbs;
                        const int gby = pos.y / gbs;
                        int gr = growthRateMap_.get(gbx, gby) + kGrowthRateIncrement;
                        if (gr > 127) gr = 127;
                        growthRateMap_.set(gbx, gby, static_cast<int8_t>(gr));
                    }

                    SDL_Log("[CitySim] %s GREW (%d,%d) item=%d pop=%d->%d "
                            "zscore=%d valve=%d lv=%d poll=%d crime=%d traf=%d",
                            n.pZone ? "zone" : "bldg",
                            pos.x, pos.y, n.pStruct->getItemID(),
                            initialPopulation, getStructurePopulation(n.pStruct,n.level),
                            zscore, valve, landValue, pollution, crime,
                            static_cast<int>(traffic));
                }
            }
        }

        // --- Demand-based decline ---
        // Non-zone Dune structures (Refinery, Silo, etc.) decline in
        // occupancy but are never destroyed. Zones can go to L0 (vacant).
        if (!grew && initialPopulation > 0) {
            const uint32_t declineRoll = (static_cast<uint32_t>(pos.x) * 97u
                                        + static_cast<uint32_t>(pos.y) * 173u
                                        + lastProcessedDay_) % 16u;
            if (shouldZoneDecline(zscore, std::max(1,n.level), declineRoll)) {
                // Non-zone Dune structures floor at level 1 (never destroyed).
                const int floorLevel = n.isDuneStructure ? 1 : 0;
                const int nextPopulation = residentialLot ? ResidentialPopulation::decline(initialPopulation) : 0;
                const int newLevel = residentialLot ? ResidentialPopulation::density(nextPopulation)
                    : std::max(floorLevel, n.level - 1);
                if (residentialLot ? nextPopulation < initialPopulation : newLevel < n.level) {
                    if (residentialLot) {
                        n.pZone->setResidentialPopulation(nextPopulation);
                    } else if (n.pZone) {
                        for (int dy = 0; dy < n.pZone->getStructureSizeY(); ++dy) {
                            for (int dx = 0; dx < n.pZone->getStructureSizeX(); ++dx) {
                                Tile* zt = currentGameMap->getTile(pos.x + dx, pos.y + dy);
                                if (zt) zt->setCityZoneDensity(static_cast<uint8_t>(newLevel));
                            }
                        }
                        n.pZone->refreshZonePowerDraw();
                    } else {
                        n.pStruct->setCityOccupancy(static_cast<uint8_t>(newLevel));
                    }
                    n.level = newLevel;
                    declined = true;

                    // Update growth rate map (negative).
                    {
                        const int gbs = growthRateMap_.getBlockSize();
                        const int gbx = pos.x / gbs;
                        const int gby = pos.y / gbs;
                        int gr = growthRateMap_.get(gbx, gby) + kGrowthRateDecrement;
                        if (gr < -128) gr = -128;
                        growthRateMap_.set(gbx, gby, static_cast<int8_t>(gr));
                    }

                    SDL_Log("[CitySim] %s DECLINED (%d,%d) item=%d pop=%d->%d "
                            "zscore=%d valve=%d lv=%d poll=%d crime=%d traf=%d (demand)",
                            n.pZone ? "zone" : "bldg",
                            pos.x, pos.y, n.pStruct->getItemID(),
                            initialPopulation, getStructurePopulation(n.pStruct,n.level),
                            zscore, valve, landValue, pollution, crime,
                            static_cast<int>(traffic));
                }
            }
        }

        // Power-starved decline: guaranteed drop when unpowered.
        // Non-zone Dune structures floor at level 1.
        if (!grew && !declined && !powered && n.level > 1) {
            const int floorLevel = n.isDuneStructure ? 1 : 1;  // both floor at 1 for power
            const int nextPopulation = residentialLot ? ResidentialPopulation::decline(initialPopulation) : 0;
            const int newLevel = residentialLot ? ResidentialPopulation::density(nextPopulation)
                : std::max(floorLevel, n.level - 1);
            if (residentialLot ? nextPopulation < initialPopulation : newLevel < n.level) {
                if (residentialLot) {
                    n.pZone->setResidentialPopulation(nextPopulation);
                } else if (n.pZone) {
                    for (int dy = 0; dy < n.pZone->getStructureSizeY(); ++dy) {
                        for (int dx = 0; dx < n.pZone->getStructureSizeX(); ++dx) {
                            Tile* zt = currentGameMap->getTile(pos.x + dx, pos.y + dy);
                            if (zt) zt->setCityZoneDensity(static_cast<uint8_t>(newLevel));
                        }
                    }
                    n.pZone->refreshZonePowerDraw();
                } else {
                    n.pStruct->setCityOccupancy(static_cast<uint8_t>(newLevel));
                }
                n.level = newLevel;
                SDL_Log("[CitySim] %s DECLINED (%d,%d) item=%d pop=%d->%d (power-starved)",
                        n.pZone ? "zone" : "bldg",
                        pos.x, pos.y, n.pStruct->getItemID(),
                        initialPopulation, getStructurePopulation(n.pStruct,n.level));
            }
        }
        const int finalPopulation = getStructurePopulation(n.pStruct,n.level);
        cityPopulation[ownerHouse] += finalPopulation - initialPopulation
            + (n.pStruct->getItemID()==Structure_Palace ? getPalaceCommercialPopulation(n.level) : 0) - palaceBefore;
        const bool populationChanged = finalPopulation != initialPopulation;
        // Observe the decision without changing its rolls, score or ordering.
        if (AITelemetry::log().enabled() && (populationChanged || lastProcessedDay_ % 96u == 0)) {
            const bool supplySatisfied = initialLevel == 0 || (n.role == CityRole::Residential
                ? localComm+localInd >= getDemandJobsThreshold(targetLevel)
                : n.role == CityRole::Commercial
                    ? localRes >= getDemandResidentialThreshold(targetLevel) && localInd >= getDemandJobsThreshold(targetLevel)/2
                    : localRes >= getDemandResidentialThreshold(targetLevel));
            const bool landSatisfied = landValue >= getDemandLandValueFloor(targetLevel);
            const bool roadSatisfied = initialLevel == 0 || traffic != TrafficResult::NoRoad;
            const int crimePenalty = computeLocalEval(n.role,landValue,pollution,0,traffic)
                - computeLocalEval(n.role,landValue,pollution,crime,traffic);
            AITelemetry::log().write(currentGame->getGameCycleCount(),ownerID,-1,
                populationChanged ? "city_level_changed" : "city_growth_sample",
                AITelemetry::Record().set("object",n.pStruct->getObjectID()).set("item",n.pStruct->getItemID())
                    .set("x",pos.x).set("y",pos.y).set("role",static_cast<int>(n.role))
                    .set("old_level",initialLevel).set("new_level",n.level).set("max_level",n.maxLevel)
                    .set("outcome",finalPopulation > initialPopulation ? "growth" : finalPopulation < initialPopulation ? "decline" : "unchanged")
                    .set("reason",finalPopulation > initialPopulation ? "growth_gates_passed" : finalPopulation < initialPopulation ? (powered ? "negative_score" : "power_shortage") : "sample")
                    .set("powered",powered).set("demand",valve).set("local_eval",localEval).set("score",zscore)
                    .set("crime_before_police",crimeBeforePoliceMap_.worldGet(pos.x,pos.y)).set("police_coverage",policeCoverageMap_.worldGet(pos.x,pos.y))
                    .set("hostile_value_penalty",hostileLandValuePenaltyMap_.worldGet(pos.x,pos.y)).set("crime_score_penalty",crimePenalty).set("land_value",landValue).set("crime",crime)
                    .set("population_density",populationDensityMap_.worldGet(pos.x,pos.y)).set("pollution",pollution)
                    .set("traffic_result",static_cast<int>(traffic)).set("traffic_density",trafficDensityMap_.worldGet(pos.x,pos.y))
                    .set("nearby_res_supply",localRes).set("nearby_com_supply",localComm).set("nearby_ind_supply",localInd)
                    .set("supply_satisfied",supplySatisfied).set("land_value_satisfied",landSatisfied).set("road_satisfied",roadSatisfied)
                    .set("pollution_blocked",pollutionBlocked).set("pollution_slowed",pollutionSlowed)
                    .set("growth_roll",roll).set("growth_roll_passed",growthRolled).set("score_satisfied",zscore>kZscoreGrowthGate)
                    .set("at_max_level",initialLevel>=n.maxLevel).set("population_before",initialPopulation)
                    .set("population_after",finalPopulation).set("ai_population_limit",cityLimits[ownerHouse])
                    .set("ai_population_budget_available",withinCityLimit));
        }
    }

    // Traffic pollution: road blocks with heavy traffic density contribute
    // pollution, closing SC's feedback loop where busy roads degrade nearby
    // residential value. Applied after all BFS trips have stamped density.
    {
        const int tbs = trafficDensityMap_.getBlockSize();
        const int pbs = pollutionDensityMap_.getBlockSize();
        const int tw = (mapWidth_  + tbs - 1) / tbs;
        const int th = (mapHeight_ + tbs - 1) / tbs;
        for (int tby = 0; tby < th; ++tby) {
            for (int tbx = 0; tbx < tw; ++tbx) {
                const int tp = getTrafficPollution(trafficDensityMap_.get(tbx, tby));
                if (tp <= 0) continue;
                // Map traffic block coords to pollution block coords.
                const int wx = tbx * tbs;
                const int wy = tby * tbs;
                const int pbx = wx / pbs;
                const int pby = wy / pbs;
                int p = pollutionDensityMap_.get(pbx, pby) + tp;
                if (p > kMaxPollution) p = kMaxPollution;
                pollutionDensityMap_.set(pbx, pby, static_cast<uint8_t>(p));
            }
        }
    }

    if (AITelemetry::log().enabled() && lastProcessedDay_ % 96u == 0) {
        auto layerRecord = [&](const auto& layer) {
            AITelemetry::Record rows;
            const int bs = layer.getBlockSize();
            for (int y = 0; y < (mapHeight_+bs-1)/bs; ++y) {
                std::string row;
                for (int x = 0; x < (mapWidth_+bs-1)/bs; ++x) {
                    if (x) row += ',';
                    row += std::to_string(static_cast<int>(layer.get(x,y)));
                }
                rows.set(std::to_string(y),row);
            }
            return AITelemetry::Record().set("block_size",bs).set("rows_csv",rows);
        };
        AITelemetry::Record terrain, roads;
        for (int y = 0; y < mapHeight_; ++y) {
            std::string tr, rr;
            for (int x = 0; x < mapWidth_; ++x) {
                const auto* tile = currentGameMap->getTile(x,y);
                if (x) tr += ',';
                tr += std::to_string(tile ? tile->getType() : -1);
                rr += tile && tile->isRoad() ? '1' : '0';
            }
            terrain.set(std::to_string(y),tr); roads.set(std::to_string(y),rr);
        }
        AITelemetry::log().write(currentGame->getGameCycleCount(),-1,-1,"city_map_snapshot",
            AITelemetry::Record().set("width",mapWidth_).set("height",mapHeight_)
                .set("phase","after_growth_and_traffic_pollution")
                .set("hostile_value_penalty",layerRecord(hostileLandValuePenaltyMap_)).set("land_value",layerRecord(landValueMap_)).set("population_density",layerRecord(populationDensityMap_))
                .set("crime",layerRecord(crimeRateMap_)).set("crime_before_police",layerRecord(crimeBeforePoliceMap_)).set("police_coverage",layerRecord(policeCoverageMap_)).set("pollution",layerRecord(pollutionDensityMap_))
                .set("traffic_density",layerRecord(trafficDensityMap_)).set("growth_rate",layerRecord(growthRateMap_))
                .set("terrain_rows_csv",terrain).set("road_rows_bits",roads));
    }

    // Recompute population totals per house.
    for (int h = 0; h < kMaxCityHouses; ++h) {
        auto& hs = houseState_[h];
        int newRes = 0, newCom = 0, newInd = 0;
        hs.taxBaseEighths = 0;
        for (const auto& n : nodes) {
            if (!n.pStruct->getOwner() || n.pStruct->getOwner()->getHouseID() != h)
                continue;
            const int itemID = n.pStruct->getItemID();
            const int pop = getStructurePopulation(n.pStruct, n.level);
            hs.taxBaseEighths += getStructureTaxBaseEighths(n.pStruct, n.level);
            switch (n.role) {
                case CityRole::Residential: newRes += pop; break;
                case CityRole::Commercial:  newCom += pop; break;
                case CityRole::Industrial:  newInd += pop; break;
                default: break;
            }
            if (itemID == Structure_Palace) {
                newCom += getPalaceCommercialPopulation(n.level);
            }
        }
        if (newRes != hs.resPop || newCom != hs.comPop || newInd != hs.indPop) {
            SDL_Log("[CitySim] house=%d population R:%d->%d C:%d->%d I:%d->%d total=%d",
                    h, hs.resPop, newRes, hs.comPop, newCom, hs.indPop, newInd,
                    newRes + newCom + newInd);
        }
        hs.resPop = newRes;
        hs.comPop = newCom;
        hs.indPop = newInd;
        hs.unemploymentRate = computeUnemploymentRate(newRes, newCom, newInd);
        hs.hospitalCount = computeHospitalCount(newRes);
        hs.churchCount   = computeChurchCount(newRes);
    }

    // Designate residential zones as hospitals/churches for rendering.
    // Cap overlays: at most 1 hospital per 8 populated res zones and
    // 1 church per 8 zones, so they appear as sparse civic landmarks
    // rather than covering every zone. Spread evenly across zones.
    {
        int populatedResZones = 0;
        for (const auto& n : nodes) {
            auto* zone = dynamic_cast<ZoneStructure*>(n.pStruct);
            if (!zone) continue;
            if (n.role == CityRole::Residential && n.level > 0)
                populatedResZones++;
        }

        int maxHosp = std::max(1, populatedResZones / 8);
        int maxChur = std::max(1, populatedResZones / 8);
        const int lhID = localHouseID();
        int hospRemain = std::min(houseState_[lhID].hospitalCount, maxHosp);
        int churRemain = std::min(houseState_[lhID].churchCount, maxChur);

        // Spacing: place a hospital every N zones, a church offset halfway between
        int stride = (hospRemain + churRemain > 0)
            ? std::max(2, populatedResZones / (hospRemain + churRemain))
            : 0;
        int resIndex = 0;

        for (const auto& n : nodes) {
            auto* zone = dynamic_cast<ZoneStructure*>(n.pStruct);
            if (!zone) continue;
            if (n.role != CityRole::Residential) {
                zone->setCivicOverlay(ZoneStructure::CivicOverlay::None);
                continue;
            }
            if (n.level <= 0) {
                zone->setCivicOverlay(ZoneStructure::CivicOverlay::None);
                continue;
            }

            if (stride > 0 && hospRemain > 0 && (resIndex % stride == 0)) {
                zone->setCivicOverlay(ZoneStructure::CivicOverlay::Hospital);
                --hospRemain;
            } else if (stride > 0 && churRemain > 0 && (resIndex % stride == stride / 2)) {
                zone->setCivicOverlay(ZoneStructure::CivicOverlay::Church);
                --churRemain;
            } else {
                zone->setCivicOverlay(ZoneStructure::CivicOverlay::None);
            }
            resIndex++;
        }
    }
}

void CitySimulation::decayGrowthRateMap() {
    const int gbs = growthRateMap_.getBlockSize();
    if (gbs <= 0) return;
    const int gw = (mapWidth_  + gbs - 1) / gbs;
    const int gh = (mapHeight_ + gbs - 1) / gbs;
    for (int by = 0; by < gh; ++by) {
        for (int bx = 0; bx < gw; ++bx) {
            int v = growthRateMap_.get(bx, by);
            if (v > 0) {
                v -= kGrowthRateDecay;
                if (v < 0) v = 0;
            } else if (v < 0) {
                v += kGrowthRateDecay;
                if (v > 0) v = 0;
            }
            growthRateMap_.set(bx, by, static_cast<int8_t>(v));
        }
    }
}

void CitySimulation::runDailyBudget() {
    if (!currentGameMap) return;
    const Map& map = *currentGameMap;

    // One structure walk collects the tax base and police costs. Roads have no upkeep.
    // All annual amounts are paid fractionally over kBudgetTicksPerYear.
    for (auto& hs : houseState_) {
        hs.taxBaseEighths = 0;
    }
    struct HouseBudget {
        int taxBaseEighths = 0;
        FixPoint policeCost = 0;
        int firstOrigin = std::numeric_limits<int>::max();
    };
    std::vector<std::pair<House*, HouseBudget>> houseBudgets;

    auto findOrAdd = [&](House* h) -> HouseBudget& {
        for (auto& [hh, hb] : houseBudgets) {
            if (hh == h) return hb;
        }
        houseBudgets.push_back({h, HouseBudget{}});
        return houseBudgets.back().second;
    };

    auto accumulateStructure = [&](int x, int y, const StructureBase* pStruct) {
        const House* constOwner = pStruct->getOwner();
        if (!constOwner) return;
        House* owner = currentGame->getHouse(constOwner->getHouseID());
        if (!owner) return;
        const Tile* t = map.getTile(x, y);
        const int itemID = pStruct->getItemID();
        HouseBudget& hb = findOrAdd(owner);
        hb.firstOrigin=std::min(hb.firstOrigin,y*map.getSizeX()+x);

        if (getStructureCityRole(itemID) != CityRole::None) {
            const int level = cityLevelOf(t, pStruct);
            hb.taxBaseEighths += getStructureTaxBaseEighths(pStruct, level);
        }
        hb.policeCost += getPoliceAnnualCost(itemID);
    };
    for (const auto* structure : structureList) {
        const auto location=structure->getLocation();
        if (!map.tileExists(location)) continue;
        const auto* tile=map.getTile(location);
        // Preserve the map-origin filter, including structures removed from
        // the map but not yet removed from the global list.
        if (tile->hasANonInfantryGroundObject() && tile->getNonInfantryGroundObject()==structure)
            accumulateStructure(location.x,location.y,structure);
    }
    // Integer/fixed-point sums are order-independent; keep the old row-major
    // house payout/log order by remembering each house's first origin.
    std::sort(houseBudgets.begin(),houseBudgets.end(),[](const auto& a,const auto& b) {
        return a.second.firstOrigin<b.second.firstOrigin;
    });

    for (auto& [house, hb] : houseBudgets) {
        // Annual values divided by cycles-per-year for smooth payout.
        // Revenue scales with the house's own average land value.
        const int hID = house->getHouseID();
        auto& hs = houseState_[hID >= 0 && hID < kMaxCityHouses ? hID : 0];
        hs.taxBaseEighths = hb.taxBaseEighths;
        const int32_t annualRevenue = computeAnnualTaxRevenue(hb.taxBaseEighths, cityTax_, hs.avgLandValue);
        const int fundingPct = hs.policeFundingPercent;
        const FixPoint annualPaid = (hb.policeCost * fundingPct) / 100;

        // Per-cycle payout: use FixPoint so fractional credits accumulate
        // smoothly (credits tick up like a harvester unloading spice).
        const FixPoint tickRevenue = FixPoint(annualRevenue) / kBudgetTicksPerYear;
        const FixPoint tickPaid    = FixPoint(annualPaid)    / kBudgetTicksPerYear;
        const FixPoint net = tickRevenue - tickPaid;

        // Gross receipts are the debriefing statistic: record them before the
        // police charge, the credit cap or any later spending touch the balance.
        house->addCityTaxReceipts(tickRevenue);
        house->addCityCredits(tickRevenue - tickPaid);
        AITelemetry::log().account(hID, "city_gross", tickRevenue.getRawValue());
        AITelemetry::log().account(hID, "police_charged", tickPaid.getRawValue());

        // Store per-house budget figures
        if (hID >= 0 && hID < kMaxCityHouses) {
            houseState_[hID].lastPoliceExpense = annualPaid.lround(); // legacy display/save cache
            houseState_[hID].budget.setLastTaxRevenue(annualRevenue);
        }

        // Log once every 10 city years per house to keep logs manageable
        if (cityDay_ == 0 && (cityYear_ % 10 == 0)) {
            SDL_Log("[CitySim] year=%d house=%d tax_base_eighths=%d rate=%d%% annual_revenue=%d annual_police=%.3f tick_net=%+d",
                    cityYear_, house->getHouseID(), hb.taxBaseEighths, cityTax_,
                    annualRevenue, annualPaid.toDouble(), lround(net.toDouble()));
        }
    }
}

}  // namespace DuneCity
