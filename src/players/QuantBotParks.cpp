/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

// QuantBot ParkDistricts (default off; docs/formations.md, "Parks in the districts"): Micropolis
// parks planted where they raise the land value of our own residential and commercial lots.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>
#include <players/ParkDistrictPolicy.h>

using QuantBotInternal::cityRoadImpact;
using QuantBotInternal::wouldLandlockNeighbouringZone;

Coord QuantBot::findParkDistrictSite(int* gainOut) {
    if (gainOut) *gainOut = 0;
    AITelemetry::PerformanceScope perfScope("ai.findParkDistrictSite", getGameCycleCount(), getHouse()->getHouseID());
    auto* sim = currentGame ? currentGame->getCitySimulation() : nullptr;
    if (!sim || !currentGame->isCitySimEnabled()) return Coord::Invalid();
    const Map& map = getMap();
    const int w = map.getSizeX(), h = map.getSizeY();
    const auto& landValue = sim->getLandValueMap();
    const int bs = std::max(1, landValue.getBlockSize());
    const int blocksW = (w + bs - 1) / bs, blocksH = (h + bs - 1) / bs;
    constexpr int tb = DuneCity::kParkTerrainBlockSize;
    const int terrainW = (w + tb - 1) / tb, terrainH = (h + tb - 1) / tb;

    // The last scan's park terrain plus the park-terrain buildings other yards have reserved.
    auto terrain = sim->getParkTerrain();
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder || !DuneCity::usesParkTerrain(entry.second.item)) continue;
        terrain.addSource(entry.second.location.x, entry.second.location.y,
                          DuneCity::getParkLandValueBonus(entry.second.item));
    }

    // Land-value blocks holding a tile of our R/C lots, and the terrain blocks near them.
    std::vector<uint8_t> lotBlock(size_t(blocksW) * blocksH, 0), nearLots(size_t(terrainW) * terrainH, 0);
    int lots = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        const Uint32 item = structure->getItemID();
        if (item != Structure_ZoneResidential && item != Structure_ZoneCommercial) continue;
        ++lots;
        const Coord o = structure->getLocation(), size = structure->getStructureSize();
        for (int y = std::max(0, o.y); y < std::min(h, o.y + size.y); ++y)
            for (int x = std::max(0, o.x); x < std::min(w, o.x + size.x); ++x)
                lotBlock[size_t(y / bs) * blocksW + x / bs] = 1;
        const int r = ParkDistrictPolicy::kLotReach;
        for (int y = std::max(0, o.y - r); y <= std::min(h - 1, o.y + size.y - 1 + r); ++y)
            for (int x = std::max(0, o.x - r); x <= std::min(w - 1, o.x + size.x - 1 + r); ++x)
                nearLots[size_t(y / tb) * terrainW + x / tb] = 1;
    }

    // A park's gain depends only on its terrain block: about +7 on that block's tiles and +1 on
    // its four neighbours, after Micropolis's smoothing. Score each block once, in map order.
    struct Candidate { int gain, sx, sy; };
    std::vector<Candidate> candidates;
    for (int sy = 0; sy < terrainH; ++sy)
        for (int sx = 0; sx < terrainW; ++sx) {
            if (!nearLots[size_t(sy) * terrainW + sx]) continue;
            const int cx = std::min(w - 1, sx * tb), cy = std::min(h - 1, sy * tb);
            const int x0 = std::max(0, (sx - 1) * tb) / bs, x1 = std::min(w - 1, (sx + 2) * tb - 1) / bs;
            const int y0 = std::max(0, (sy - 1) * tb) / bs, y1 = std::min(h - 1, (sy + 2) * tb - 1) / bs;
            int gain = 0;
            for (int by = y0; by <= y1; ++by)
                for (int bx = x0; bx <= x1; ++bx) {
                    if (!lotBlock[size_t(by) * blocksW + bx]) continue;
                    gain += ParkDistrictPolicy::blockGain(landValue.get(bx, by),
                        terrain.marginalGain(cx, cy, DuneCity::kParkLandValueBonus, bx * bs, by * bs, bs));
                }
            if (gain >= ParkDistrictPolicy::kMinGain) candidates.push_back({gain, sx, sy});
        }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.gain > b.gain; });

    // The first legal tile of the best block: open ground first, then a road tile whose removal
    // keeps every connection and leaves each neighbouring lot a side for its road.
    constexpr size_t kChecked = 48;
    Coord best = Coord::Invalid();
    int bestGain = 0, checked = 0;
    auto legal = [&](int x, int y, bool road) {
        if (overlapsReservedStructure(x, y, 1, 1) || !placeableAmongOwnUnits(x, y, 1, 1, Structure_Park)
            || dangerAt(Coord(x, y)) > 0) return false;
        if (road && (!cityRoadImpact(map, x, y, 1, 1, Structure_Park).preservesConnections
                     || wouldLandlockNeighbouringZone(map, getHouse()->getHouseID(), x, y, 1, 1, nullptr))) return false;
        return preservesGroundAccess(Structure_Park, Coord(x, y));
    };
    for (size_t i = 0; i < candidates.size() && i < kChecked && !best.isValid(); ++i) {
        const auto& c = candidates[i];
        ++checked;
        for (int round = 0; round < 2 && !best.isValid(); ++round)
            for (int y = c.sy * tb; y < std::min(h, (c.sy + 1) * tb) && !best.isValid(); ++y)
                for (int x = c.sx * tb; x < std::min(w, (c.sx + 1) * tb) && !best.isValid(); ++x) {
                    const bool road = map.getTile(x, y)->isRoad();
                    if (road != (round == 1) || !legal(x, y, road)) continue;
                    best = Coord(x, y);
                    bestGain = c.gain;
                }
    }
    if (gainOut) *gainOut = bestGain;
    traceDecision("park_district_site", AITelemetry::Record().set("valid", best.isValid()).set("x", best.x)
        .set("y", best.y).set("gain", bestGain).set("lots", lots).set("candidates", int(candidates.size()))
        .set("checked", checked));
    return best;
}

void QuantBot::chooseYardParkDistrict(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    if (!botConfig().force.parkDistricts || !pass.citySimEnabled || gameMode != GameMode::Custom
        || yardTurn.itemID != NONE_ID || yardTurn.skipRemainingStructureLogic
        || pass.orderedThisTick.count(Structure_Park)
        || pass.defendingEconomy || pass.airEngaged || pass.groundThreatAsset.isValid()
        || !campaignAvailableToBuild(turn.pBuilder, Structure_Park)) return;
    const int lots = pass.itemCount[Structure_ZoneResidential] + pass.itemCount[Structure_ZoneCommercial];
    const int parks = pass.itemCount[Structure_Park];
    // One park at a time: the next is chosen against the terrain the last one added.
    const bool pending = parks != getHouse()->getNumItems(Structure_Park);
    if (!ParkDistrictPolicy::wantsPark(lots, parks, pending, ParkDistrictPolicy::kMinGain)) return;
    const int price = currentGame->objectData.data[Structure_Park][getHouse()->getHouseID()].price;
    if (pass.money < price) return;
    int gain = 0;
    const Coord site = findParkDistrictSite(&gain);
    if (!site.isValid() || !ParkDistrictPolicy::wantsPark(lots, parks, pending, gain)) return;
    yardTurn.itemID = Structure_Park;
    yardTurn.crimeServiceSite = site;
    yardTurn.structureRule = "park_district";
    yardTurn.skipRemainingStructureLogic = true;
    traceDecision("park_district", AITelemetry::Record().set("builder", turn.pBuilder->getObjectID())
        .set("x", site.x).set("y", site.y).set("gain", gain).set("lots", lots).set("parks", parks)
        .set("money", pass.money));
}
