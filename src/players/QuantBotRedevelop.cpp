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

// QuantBot city redevelopment for land value (Stefan, 2026-11-27: "the AI could benefit from
// demolishing and rebuilding to either remove pollution from an area ... or by adding in more
// palaces or parks ... it has lots of 2 lane roads which is a waste"). Three switches, each its
// own arm, all default off ([Force Concentration]; docs/formations.md "Redevelopment for land
// value"; RedevelopmentPolicy.h):
//   RoadLaneParks        a Park on the spare lane of a two-lane road strip (a road tile in a 2x2
//                        square of own road), where it raises our R/C land value, while a lane
//                        and every unit path through the tile stay open;
//   LotParks             a low-value own R/C lot cleared for parks that raise its neighbours'
//                        land value, then its remaining tiles planted;
//   PollutionRelocation  an Industrial zone whose pollution costs our R/C lots land value
//                        rebuilt at an Industrial site clear of our homes, when that site costs
//                        at most half as much; the old zone goes once the new one stands;
//   IndustrySeparation   (QuantBotPlacement.cpp) no new Industrial zone within
//                        kIndustrySeparationTiles of an own R/C lot.
// Searches run once a period per house, on the next idle yard pass after the period has passed.
// Each order is followed (updateRedevelopmentOrders): placed at its site or lost, and the land
// value of the lots it was for read 30 s after it stands (SCENARIO_SWITCH_ACTIVITY). That list
// and the search timers are not saved: after a load the next search is simply due at once.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>
#include <players/ParkDistrictPolicy.h>
#include <players/CityPlacementPolicy.h>
#include <players/RedevelopmentPolicy.h>
#include <chrono>

using QuantBotInternal::cityRoadImpact;
using QuantBotInternal::wouldLandlockNeighbouringZone;

namespace {
bool ownLot(const StructureBase* s, const House* house) {
    return s->getOwner() == house && s->getHealth() > 0
        && (s->getItemID() == Structure_ZoneResidential || s->getItemID() == Structure_ZoneCommercial);
}

/// Adds the wall-clock time of a redevelopment search to the house's activity (the cost line).
struct SearchClock {
    long& total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit SearchClock(long& t) : total(t) {}
    ~SearchClock() {
        total += long(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    }
};

int centreDistance(Coord a, Coord sa, Coord b, Coord sb) {
    // Doubled centres keep 2x2 and odd footprints exact in integers.
    const int ax = 2 * a.x + sa.x, ay = 2 * a.y + sa.y, bx = 2 * b.x + sb.x, by = 2 * b.y + sb.y;
    return std::max(std::abs(ax - bx), std::abs(ay - by)) / 2;
}
} // namespace

/// Land value a park-terrain source of \a strength at (cx, cy) adds to the blocks of our R/C lots
/// (each block capped at the land-value ceiling), skipping the lot at \a skip.
int QuantBot::parkSourceLotGain(const DuneCity::ParkTerrainPolicy& terrain, int cx, int cy, int strength,
                                Coord skip) const {
    const auto* sim = currentGame->getCitySimulation();
    const auto& landValue = sim->getLandValueMap();
    const int bs = std::max(1, landValue.getBlockSize());
    constexpr int tb = DuneCity::kParkTerrainBlockSize;
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    const int sx = cx / tb, sy = cy / tb;
    // A source moves its own terrain block and the four next to it.
    const int x0 = std::max(0, (sx - 1) * tb), x1 = std::min(w - 1, (sx + 2) * tb - 1);
    const int y0 = std::max(0, (sy - 1) * tb), y1 = std::min(h - 1, (sy + 2) * tb - 1);
    std::set<std::pair<int, int>> blocks;
    for (const auto* s : getStructureList()) {
        if (!ownLot(s, getHouse()) || s->getLocation() == skip) continue;
        const Coord o = s->getLocation(), size = s->getStructureSize();
        if (o.x + size.x - 1 < x0 || o.x > x1 || o.y + size.y - 1 < y0 || o.y > y1) continue;
        for (int y = std::max(y0, o.y); y <= std::min(y1, o.y + size.y - 1); ++y)
            for (int x = std::max(x0, o.x); x <= std::min(x1, o.x + size.x - 1); ++x)
                blocks.insert({x / bs, y / bs});
    }
    int gain = 0;
    for (const auto& [bx, by] : blocks)
        gain += ParkDistrictPolicy::blockGain(landValue.get(bx, by),
            terrain.marginalGain(cx, cy, strength, bx * bs, by * bs, bs));
    return gain;
}

/// The last scan's park terrain plus the park-terrain buildings our yards have reserved.
DuneCity::ParkTerrainPolicy QuantBot::plannedParkTerrain() const {
    auto terrain = currentGame->getCitySimulation()->getParkTerrain();
    for (const auto& entry : reservedStructures) {
        if (!DuneCity::usesParkTerrain(entry.second.item)) continue;
        terrain.addSource(entry.second.location.x, entry.second.location.y,
                          DuneCity::getParkLandValueBonus(entry.second.item));
    }
    return terrain;
}

/// Whether a Park at (x, y) keeps every unit path and every road path that crossed the tile: the
/// open (respectively road) side neighbours of the tile still reach one another, without it, inside
/// the 9x9 window around it (RedevelopmentPolicy::sidesStayConnected). A path that turned through the
/// tile can then go around it. (The first version only looked at the eight neighbours and refused
/// every tile where a side road joined a two-lane strip: 11 of 11 candidates in the probe.)
bool QuantBot::parkKeepsPassage(int x, int y) const {
    const Map& map = getMap();
    constexpr int r = RedevelopmentPolicy::kPassageWindow, side = 2 * r + 1;
    bool open[side * side], road[side * side];
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const int nx = x + dx, ny = y + dy, i = (dy + r) * side + (dx + r);
            const Tile* tile = map.tileExists(nx, ny) ? map.getTile(nx, ny) : nullptr;
            open[i] = tile && !tile->isMountain() && !tile->hasAStructure() && !overlapsReservedStructure(nx, ny, 1, 1);
            road[i] = tile && tile->isRoad();
        }
    return RedevelopmentPolicy::sidesStayConnected(open) && RedevelopmentPolicy::sidesStayConnected(road);
}

void QuantBot::keepParkPassages() {
    const auto& force = botConfig().force;
    if ((!force.roadLaneParks && !force.lotParks) || getGameCycleCount() % MILLI2CYCLES(1000) != 0) return;
    std::vector<StructureBase*> cut;
    for (const auto* s : getStructureList())
        if (s->getOwner() == getHouse() && s->getItemID() == Structure_Park && s->getHealth() > 0
            && !parkKeepsUnitPassage(s->getLocation().x, s->getLocation().y))
            cut.push_back(const_cast<StructureBase*>(s));
    for (auto* park : cut) {
        const Coord p = park->getLocation();
        SDL_Log("REDEVELOP: house=%d event=retired kind=park x=%d y=%d cycle=%u", getHouse()->getHouseID(), p.x, p.y,
                getGameCycleCount());
        redevelopmentActivity.note("park_retired_for_passage");
        park->demolish();
        if (auto* tile = currentGameMap->getTile(p.x, p.y)) {
            tile->setDestroyedStructureTile(DestroyedStructure_None);
            tile->setRoad(true);
        }
    }
}

/// Whether units can still get round the Park at (x, y): its open side neighbours meet over open
/// ground without it, inside the passage window (the Park itself blocks units).
bool QuantBot::parkKeepsUnitPassage(int x, int y) const {
    const Map& map = getMap();
    constexpr int r = RedevelopmentPolicy::kPassageWindow, side = 2 * r + 1;
    bool open[side * side];
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const Tile* tile = map.tileExists(x + dx, y + dy) ? map.getTile(x + dx, y + dy) : nullptr;
            open[(dy + r) * side + (dx + r)] = tile && !tile->isMountain() && !tile->hasAStructure();
        }
    return RedevelopmentPolicy::sidesStayConnected(open);
}

bool QuantBot::nearRelocatedFrom(int x, int y) const {
    const Uint32 cycle = getGameCycleCount();
    for (const auto& [site, when] : relocatedFrom)
        if (cycle - when < MILLI2CYCLES(RedevelopmentPolicy::kRelocatedFromMs)
            && CityPlacementPolicy::footprintDistance(x, y, 2, 2, site.x, site.y, 2, 2) <= RedevelopmentPolicy::kRelocatedFromTiles)
            return true;
    return false;
}

bool QuantBot::redevelopmentSiteHeld(Uint32 item, Coord site) const {
    return std::any_of(redevelopmentOrders.begin(), redevelopmentOrders.end(), [&](const RedevelopmentOrder& o) {
        return o.placedCycle == 0 && o.item == item && o.site == site;
    });
}

/// RoadLaneParks: the spare-lane road tile where a Park adds the most land value to our R/C lots.
Coord QuantBot::findLaneParkSite(int* gainOut) {
    SearchClock clock(redevelopmentActivity.searchMicros);
    if (gainOut) *gainOut = 0;
    AITelemetry::PerformanceScope perfScope("ai.findLaneParkSite", getGameCycleCount(), getHouse()->getHouseID());
    const Map& map = getMap();
    const int w = map.getSizeX(), h = map.getSizeY(), houseID = getHouse()->getHouseID();
    const auto terrain = plannedParkTerrain();
    // Tiles near our R/C lots (a park reaches two terrain blocks).
    std::vector<uint8_t> near(size_t(w) * h, 0);
    const int r = ParkDistrictPolicy::kLotReach;
    for (const auto* s : getStructureList()) {
        if (!ownLot(s, getHouse())) continue;
        const Coord o = s->getLocation(), size = s->getStructureSize();
        for (int y = std::max(0, o.y - r); y <= std::min(h - 1, o.y + size.y - 1 + r); ++y)
            for (int x = std::max(0, o.x - r); x <= std::min(w - 1, o.x + size.x - 1 + r); ++x)
                near[size_t(y) * w + x] = 1;
    }
    // A road is ours when its tile is, or when it is unowned rock (the auto-paved ring keeps the
    // ground's owner, and bare rock has none) beside one of our buildings.
    auto ownBuildingAt = [&](int x, int y) {
        if (!map.tileExists(x, y)) return false;
        const auto* object = map.getTile(x, y)->getNonInfantryGroundObject();
        return object && object->isAStructure() && object->getOwner() == getHouse();
    };
    auto ownRoad = [&](int x, int y) {
        if (!map.tileExists(x, y)) return false;
        const Tile* tile = map.getTile(x, y);
        if (!tile->isRoad() || tile->hasAGroundObject()) return false;
        if (tile->getOwner() == houseID) return true;
        if (tile->getOwner() >= 0) return false;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            if (ownBuildingAt(x + dx, y + dy)) return true;
        return false;
    };
    auto spareLane = [&](int x, int y) {
        for (int oy = -1; oy <= 0; ++oy) for (int ox = -1; ox <= 0; ++ox)
            if (ownRoad(x + ox, y + oy) && ownRoad(x + ox + 1, y + oy) && ownRoad(x + ox, y + oy + 1)
                && ownRoad(x + ox + 1, y + oy + 1)) return true;
        return false;
    };
    // Gain depends only on the terrain block; score each block once.
    constexpr int tb = DuneCity::kParkTerrainBlockSize;
    std::map<std::pair<int, int>, int> blockGain;
    struct Candidate { int gain, x, y; };
    std::vector<Candidate> candidates;
    int lanes = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!near[size_t(y) * w + x] || !ownRoad(x, y) || !spareLane(x, y)) continue;
            ++lanes;
            const auto key = std::make_pair(x / tb, y / tb);
            auto it = blockGain.find(key);
            if (it == blockGain.end())
                it = blockGain.emplace(key, parkSourceLotGain(terrain, x, y, DuneCity::kParkLandValueBonus)).first;
            if (it->second >= ParkDistrictPolicy::kMinGain) candidates.push_back({it->second, x, y});
        }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.gain > b.gain; });
    constexpr size_t kChecked = 48;
    Coord best = Coord::Invalid();
    int bestGain = 0, checked = 0;
    auto& why = redevelopmentActivity;
    why.note("lane_search");
    why.note("lane_spare_tiles", lanes);
    why.note("lane_candidates", int(candidates.size()));
    for (size_t i = 0; i < candidates.size() && i < kChecked && !best.isValid(); ++i) {
        const auto& c = candidates[i];
        ++checked;
        if (overlapsReservedStructure(c.x, c.y, 1, 1)) { why.note("lane_reject_reserved"); continue; }
        if (!placeableAmongOwnUnits(c.x, c.y, 1, 1, Structure_Park)) { why.note("lane_reject_placeable"); continue; }
        if (dangerAt(Coord(c.x, c.y)) > 0) { why.note("lane_reject_danger"); continue; }
        if (!parkKeepsPassage(c.x, c.y)) { why.note("lane_reject_passage"); continue; }
        if (!cityRoadImpact(map, c.x, c.y, 1, 1, Structure_Park).preservesConnections) { why.note("lane_reject_city_road"); continue; }
        if (wouldLandlockNeighbouringZone(map, houseID, c.x, c.y, 1, 1, nullptr)) { why.note("lane_reject_landlock"); continue; }
        if (!preservesGroundAccess(Structure_Park, Coord(c.x, c.y))) { why.note("lane_reject_ground_access"); continue; }
        best = Coord(c.x, c.y);
        bestGain = c.gain;
    }
    if (gainOut) *gainOut = bestGain;
    traceDecision("lane_park_site", AITelemetry::Record().set("valid", best.isValid()).set("x", best.x)
        .set("y", best.y).set("gain", bestGain).set("lanes", lanes).set("candidates", int(candidates.size()))
        .set("checked", checked));
    return best;
}

/// LotParks: first a free tile of a lot already being turned into parks (beside one of our Parks,
/// not road, adding land value); else the cheapest own R/C lot whose parks would add the most
/// land value to the house's other lots. \a lotCleared is set when the site is a standing lot.
Coord QuantBot::findLotParkSite(int* gainOut, bool* lotCleared) {
    SearchClock clock(redevelopmentActivity.searchMicros);
    if (gainOut) *gainOut = 0;
    if (lotCleared) *lotCleared = false;
    AITelemetry::PerformanceScope perfScope("ai.findLotParkSite", getGameCycleCount(), getHouse()->getHouseID());
    namespace R = RedevelopmentPolicy;
    const Map& map = getMap();
    auto* sim = currentGame->getCitySimulation();
    const auto terrain = plannedParkTerrain();
    // Fill: the tiles left of a cleared lot.
    std::vector<Coord> parks;
    for (const auto* s : getStructureList())
        if (s->getOwner() == getHouse() && s->getItemID() == Structure_Park) parks.push_back(s->getLocation());
    for (const auto& entry : reservedStructures)
        if (entry.second.item == Structure_Park) parks.push_back(entry.second.location);
    Coord fill = Coord::Invalid();
    int fillGain = 0;
    for (const Coord p : parks)
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int x = p.x + dx, y = p.y + dy;
            if ((dx == 0 && dy == 0) || !map.tileExists(x, y)) continue;
            const Tile* tile = map.getTile(x, y);
            if (tile->isRoad() || tile->hasAGroundObject() || tile->hasCityZone()
                || !DuneCity::isCityZoneTerrain(tile->getType()) || overlapsReservedStructure(x, y, 1, 1)) continue;
            const int gain = parkSourceLotGain(terrain, x, y, DuneCity::kParkLandValueBonus);
            if (gain <= fillGain || gain < ParkDistrictPolicy::kMinGain) continue;
            if (!placeableAmongOwnUnits(x, y, 1, 1, Structure_Park) || dangerAt(Coord(x, y)) > 0
                || !parkKeepsPassage(x, y) || !preservesGroundAccess(Structure_Park, Coord(x, y))) continue;
            fill = Coord(x, y);
            fillGain = gain;
        }
    if (fill.isValid()) {
        if (gainOut) *gainOut = fillGain;
        traceDecision("lot_park_site", AITelemetry::Record().set("valid", true).set("fill", true)
            .set("x", fill.x).set("y", fill.y).set("gain", fillGain));
        return fill;
    }
    // Clear: the low-value lot whose four parks add the most to the other lots.
    redevelopmentActivity.note("lot_search");
    Coord best = Coord::Invalid();
    int bestGain = 0, candidates = 0, bestValue = 0;
    for (const auto* s : getStructureList()) {
        if (!ownLot(s, getHouse())) continue;
        const auto* zone = dynamic_cast<const ZoneStructure*>(s);
        if (!zone || zone->getCivicOverlay() != ZoneStructure::CivicOverlay::None) continue;
        const Coord o = s->getLocation();
        const int value = sim->getLandValueMap().worldGet(o.x, o.y);
        const int density = map.getTile(o.x, o.y)->getCityZoneDensity();
        if (value > R::kLotParkMaxValue || density > R::kLotParkMaxDensity) continue;
        ++candidates;
        const int gain = parkSourceLotGain(terrain, o.x, o.y, R::kLotParkSources * DuneCity::kParkLandValueBonus, o);
        if (gain < R::kLotParkMinGain) { redevelopmentActivity.note("lot_reject_gain"); continue; }
        if (gain <= bestGain) continue;
        if (overlapsReservedStructure(o.x, o.y, 2, 2) || dangerAt(o, Coord(2, 2)) > 0) { redevelopmentActivity.note("lot_reject_site"); continue; }
        best = o;
        bestGain = gain;
        bestValue = value;
    }
    if (gainOut) *gainOut = bestGain;
    if (lotCleared) *lotCleared = best.isValid();
    redevelopmentActivity.note("lot_cheap_lots", candidates);
    traceDecision("lot_park_site", AITelemetry::Record().set("valid", best.isValid()).set("fill", false)
        .set("x", best.x).set("y", best.y).set("gain", bestGain).set("land_value", bestValue)
        .set("candidates", candidates));
    return best;
}

/// PollutionRelocation: the own Industrial zone whose pollution costs our R/C lots the most, and
/// where the house's own Industrial siting would put it instead.
bool QuantBot::findPollutionRelocation(Uint32& zoneID, Coord& site, int& harm, int& newHarm) {
    SearchClock clock(redevelopmentActivity.searchMicros);
    AITelemetry::PerformanceScope perfScope("ai.findPollutionRelocation", getGameCycleCount(), getHouse()->getHouseID());
    zoneID = NONE_ID; site = Coord::Invalid(); harm = newHarm = 0;
    const Map& map = getMap();
    std::vector<std::pair<Coord, Coord>> lots;
    for (const auto* s : getStructureList())
        if (ownLot(s, getHouse())) lots.push_back({s->getLocation(), s->getStructureSize()});
    auto harmAt = [&](Coord pos, Coord size, int emission, Coord skip) {
        int total = 0;
        for (const auto& [o, lotSize] : lots) {
            if (o == skip) continue;
            total += RedevelopmentPolicy::pollutionHarm(emission, centreDistance(pos, size, o, lotSize),
                                                        DuneCity::kPollutionRadius);
        }
        return total;
    };
    int emission = 0;
    for (const auto* s : getStructureList()) {
        if (s->getOwner() != getHouse() || s->getHealth() <= 0 || s->getItemID() != Structure_ZoneIndustrial) continue;
        const Coord o = s->getLocation();
        const int e = DuneCity::getPollutionEmission(Structure_ZoneIndustrial, map.getTile(o.x, o.y)->getCityZoneDensity());
        const int h = harmAt(o, s->getStructureSize(), e, Coord::Invalid());
        if (h > harm) { harm = h; zoneID = s->getObjectID(); emission = e; }
    }
    if (zoneID == NONE_ID || harm < RedevelopmentPolicy::kRelocateMinHarm) { redevelopmentActivity.note("relocate_no_harm"); return false; }
    // The new site keeps clear of our homes whatever IndustrySeparation says: a relocation that
    // moved the polluter next to other lots would be no relocation.
    placementCache.erase(Structure_ZoneIndustrial);
    industrySeparationSearch = true;
    site = findPlaceLocation(Structure_ZoneIndustrial);
    industrySeparationSearch = false;
    placementCache.erase(Structure_ZoneIndustrial);
    // A city with no ground clear of its homes in reach (low.dls: 61 of 73 searches) still moves the
    // polluter to a different area when the ordinary Industrial site at least halves the harm.
    if (!site.isValid()) {
        redevelopmentActivity.note("relocate_no_clear_site");
        site = findPlaceLocation(Structure_ZoneIndustrial);
        placementCache.erase(Structure_ZoneIndustrial);
    }
    if (!site.isValid()) { redevelopmentActivity.note("relocate_no_site"); return false; }
    newHarm = harmAt(site, getStructureSize(Structure_ZoneIndustrial), emission, Coord::Invalid());
    if (!RedevelopmentPolicy::relocationPays(harm, newHarm)) { redevelopmentActivity.note("relocate_not_paying"); return false; }
    return true;
}

void QuantBot::chooseYardRedevelopment(BuildPass& pass, BuilderTurn& turn, YardTurn& yardTurn) {
    const auto& force = botConfig().force;
    const Uint32 cycle = getGameCycleCount();
    namespace R = RedevelopmentPolicy;
    // A search is due once its period has passed and runs on the next idle yard pass (a fixed
    // window used to miss the idle yard: PollutionRelocation fired once in 30 minutes).
    const bool relocateDue = force.pollutionRelocation && cycle >= nextRelocationSearch;
    const bool parkDue = (force.roadLaneParks || force.lotParks) && cycle >= nextParkSearch;
    if (!relocateDue && !parkDue) return;
    // Not gated on pass.defendingEconomy: in a long war some enemy is always firing at some
    // building of ours (low.dls: 85% of windows), while redevelopment happens inside the city and
    // every site finder refuses a site under danger (dangerAt).
    if (!pass.citySimEnabled || gameMode != GameMode::Custom || pass.redevelopmentSearched
        || yardTurn.itemID != NONE_ID || yardTurn.skipRemainingStructureLogic
        || pass.airEngaged || pass.groundThreatAsset.isValid()) {
        if (!pass.redevelopmentSearched) {
            auto& why = redevelopmentActivity;
            if (!pass.citySimEnabled || gameMode != GameMode::Custom) why.note("gate_not_city_custom");
            else if (yardTurn.itemID != NONE_ID || yardTurn.skipRemainingStructureLogic) why.note("gate_yard_busy");
            else why.note("gate_threat");
        }
        if (!pass.redevelopmentSearched)
            traceDecision("redevelopment_gate", AITelemetry::Record().set("builder", turn.pBuilder->getObjectID())
                .set("item", yardTurn.itemID).set("rule", yardTurn.structureRule ? yardTurn.structureRule : "")
                .set("skip", yardTurn.skipRemainingStructureLogic).set("defending", pass.defendingEconomy)
                .set("air", pass.airEngaged).set("ground", pass.groundThreatAsset.isValid())
                .set("custom", gameMode == GameMode::Custom).set("money", pass.money));
        return;
    }

    // PollutionRelocation: one Industrial zone at a time, ordered at a site clear of our homes;
    // the old zone is demolished only once the new one stands (updateRedevelopmentOrders), so an
    // order that is cancelled, released for funds or placed elsewhere costs nothing.
    const bool relocationPending = std::any_of(redevelopmentOrders.begin(), redevelopmentOrders.end(),
        [](const RedevelopmentOrder& o) { return o.kind == R::Relocation && o.placedCycle == 0; });
    if (relocateDue && !relocationPending && !pass.orderedThisTick.count(Structure_ZoneIndustrial)
        && campaignAvailableToBuild(turn.pBuilder, Structure_ZoneIndustrial)) {
        pass.redevelopmentSearched = true;
        nextRelocationSearch = cycle + MILLI2CYCLES(R::kRelocatePeriodMs);
        const int price = purchasePrice(turn.pBuilder, Structure_ZoneIndustrial);
        Uint32 zoneID = NONE_ID;
        Coord site = Coord::Invalid();
        int harm = 0, newHarm = 0;
        redevelopmentActivity.note("relocate_search");
        if (pass.money < price) redevelopmentActivity.note("relocate_no_money");
        const bool found = pass.money >= price && findPollutionRelocation(zoneID, site, harm, newHarm);
        traceDecision("pollution_relocation_search", AITelemetry::Record().set("money", pass.money)
            .set("price", price).set("zone", zoneID).set("harm", harm).set("new_harm", newHarm)
            .set("site_valid", site.isValid()).set("found", found));
        const auto* zone = found ? dynamic_cast<const ZoneStructure*>(currentGame->getObjectManager().getObject(zoneID)) : nullptr;
        if (zone && zone->getOwner() == getHouse()) {
            const Coord old = zone->getLocation();
            traceDecision("pollution_relocation", AITelemetry::Record().set("builder", turn.pBuilder->getObjectID())
                .set("zone", zoneID).set("from_x", old.x).set("from_y", old.y).set("x", site.x).set("y", site.y)
                .set("harm", harm).set("new_harm", newHarm).set("density", getMap().getTile(old.x, old.y)->getCityZoneDensity())
                .set("money", pass.money));
            noteRedevelopmentOrder(R::Relocation, Structure_ZoneIndustrial, site, zoneID, old);
            redevelopmentOrders.back().harm = harm;
            redevelopmentOrders.back().newHarm = newHarm;
            yardTurn.itemID = Structure_ZoneIndustrial;
            yardTurn.crimeServiceSite = site;
            yardTurn.structureRule = "pollution_relocation";
            yardTurn.skipRemainingStructureLogic = true;
            return;
        }
    }

    if (!parkDue) return;
    auto& why = redevelopmentActivity;
    if (pass.orderedThisTick.count(Structure_Park)) { why.note("park_ordered_this_tick"); return; }
    if (!campaignAvailableToBuild(turn.pBuilder, Structure_Park)) { why.note("park_not_buildable"); return; }
    // One park at a time: the next is chosen against the terrain the last one added.
    if (pass.itemCount[Structure_Park] != getHouse()->getNumItems(Structure_Park)) { why.note("park_pending"); return; }
    const int price = currentGame->objectData.data[Structure_Park][getHouse()->getHouseID()].price;
    if (pass.money < price) { why.note("park_no_money"); return; }
    pass.redevelopmentSearched = true;
    nextParkSearch = cycle + MILLI2CYCLES(R::kParkSearchPeriodMs);
    int gain = 0;
    Coord site = Coord::Invalid();
    const char* rule = nullptr;
    int kind = R::LanePark;
    if (force.lotParks) {
        bool cleared = false;
        site = findLotParkSite(&gain, &cleared);
        if (site.isValid()) { rule = cleared ? "lot_park_clear" : "lot_park_fill"; kind = cleared ? R::LotClear : R::LotFill; }
    }
    if (!site.isValid() && force.roadLaneParks) {
        site = findLaneParkSite(&gain);
        if (site.isValid()) { rule = "lane_park"; kind = R::LanePark; }
    }
    if (!site.isValid()) return;
    // A cleared lot is demolished by the yard's redevelopment route once the order is accepted
    // (redevelopmentZones, lotPark), as a Palace's lots are.
    noteRedevelopmentOrder(kind, Structure_Park, site, NONE_ID, site);
    yardTurn.itemID = Structure_Park;
    yardTurn.crimeServiceSite = site;
    yardTurn.structureRule = rule;
    yardTurn.skipRemainingStructureLogic = true;
    traceDecision("redevelopment_park", AITelemetry::Record().set("builder", turn.pBuilder->getObjectID())
        .set("rule", rule).set("x", site.x).set("y", site.y).set("gain", gain).set("money", pass.money));
}

/// Own R/C lots within \a reach (Chebyshev, origin to origin) of \a centre, with their land value now.
std::vector<std::pair<Coord, int>> QuantBot::redevelopmentLots(Coord centre, int reach, Coord skip) const {
    std::vector<std::pair<Coord, int>> lots;
    const auto& landValue = currentGame->getCitySimulation()->getLandValueMap();
    for (const auto* s : getStructureList()) {
        if (!ownLot(s, getHouse())) continue;
        const Coord o = s->getLocation();
        if (o == skip || std::max(std::abs(o.x - centre.x), std::abs(o.y - centre.y)) > reach) continue;
        lots.push_back({o, landValue.worldGet(o.x, o.y)});
    }
    return lots;
}

void QuantBot::noteRedevelopmentOrder(int kind, Uint32 item, Coord site, Uint32 oldZone, Coord effectCentre) {
    namespace R = RedevelopmentPolicy;
    RedevelopmentOrder order;
    order.kind = kind;
    order.item = item;
    order.site = site;
    order.oldZone = oldZone;
    order.orderCycle = getGameCycleCount();
    // The lots the change is for: around the new Park, or around the polluter being moved (its own
    // footprint excluded; a cleared lot is excluded as it goes).
    order.lots = redevelopmentLots(effectCentre, kind == R::Relocation ? R::kRelocationEffectReach : R::kParkEffectReach,
                                   kind == R::LotClear ? site : Coord::Invalid());
    const auto& pollutionMap = currentGame->getCitySimulation()->getPollutionDensityMap();
    for (const auto& lot : order.lots) order.pollution.push_back(pollutionMap.worldGet(lot.first.x, lot.first.y));
    redevelopmentOrders.push_back(std::move(order));
    ++redevelopmentActivity.ordered[kind];
    // Per-event match-log lines (tests/ai/activation-events.json counts them per play style).
    SDL_Log("REDEVELOP: house=%d event=ordered kind=%s x=%d y=%d cycle=%u", getHouse()->getHouseID(),
            R::kindName(kind), site.x, site.y, getGameCycleCount());
}

/// Once a second: an order whose building stands at its site counts as placed (a relocation then
/// demolishes the old zone); one still missing after kOrderTimeoutMs counts as lost; kEffectDelayMs
/// after placement the affected lots' mean land value is read against its value at the order.
void QuantBot::updateRedevelopmentOrders() {
    namespace R = RedevelopmentPolicy;
    if (redevelopmentOrders.empty() || !currentGame->getCitySimulation()) return;
    const Uint32 cycle = getGameCycleCount();
    if (cycle % MILLI2CYCLES(1000) != 0) return;
    relocatedFrom.erase(std::remove_if(relocatedFrom.begin(), relocatedFrom.end(), [&](const auto& entry) {
        return cycle - entry.second >= MILLI2CYCLES(R::kRelocatedFromMs); }), relocatedFrom.end());
    const auto& landValue = currentGame->getCitySimulation()->getLandValueMap();
    for (auto it = redevelopmentOrders.begin(); it != redevelopmentOrders.end();) {
        auto& o = *it;
        if (o.placedCycle == 0) {
            const Tile* tile = getMap().tileExists(o.site.x, o.site.y) ? getMap().getTile(o.site.x, o.site.y) : nullptr;
            const auto* object = tile ? tile->getNonInfantryGroundObject() : nullptr;
            if (object && object->isAStructure() && Uint32(object->getItemID()) == o.item && object->getOwner() == getHouse()
                && static_cast<const StructureBase*>(object)->getLocation() == o.site) {
                o.placedCycle = cycle;
                ++redevelopmentActivity.placed[o.kind];
                SDL_Log("REDEVELOP: house=%d event=placed kind=%s x=%d y=%d cycle=%u", getHouse()->getHouseID(),
                        R::kindName(o.kind), o.site.x, o.site.y, cycle);
                if (o.kind == R::Relocation) {
                    auto* old = dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(o.oldZone));
                    const bool demolish = old && old->getOwner() == getHouse();
                    int homes = 1000000;
                    for (const auto* s : getStructureList())
                        if (ownLot(s, getHouse()))
                            homes = std::min(homes, CityPlacementPolicy::footprintDistance(o.site.x, o.site.y, 2, 2,
                                s->getLocation().x, s->getLocation().y, s->getStructureSize().x, s->getStructureSize().y));
                    SDL_Log("REDEVELOP: house=%d event=moved kind=relocation x=%d y=%d from_x=%d from_y=%d home_distance=%d "
                            "harm=%d new_harm=%d old_demolished=%d cycle=%u", getHouse()->getHouseID(), o.site.x, o.site.y,
                            demolish ? old->getLocation().x : -1, demolish ? old->getLocation().y : -1,
                            homes == 1000000 ? -1 : homes, o.harm, o.newHarm, int(demolish), cycle);
                    if (demolish) {
                        relocatedFrom.push_back({old->getLocation(), cycle});
                        traceDecision("pollution_relocation_done", AITelemetry::Record().set("zone", o.oldZone)
                            .set("x", o.site.x).set("y", o.site.y));
                        old->demolish();
                    }
                }
            } else if (cycle - o.orderCycle > MILLI2CYCLES(R::kOrderTimeoutMs)) {
                ++redevelopmentActivity.lost[o.kind];
                SDL_Log("REDEVELOP: house=%d event=lost kind=%s x=%d y=%d cycle=%u", getHouse()->getHouseID(),
                        R::kindName(o.kind), o.site.x, o.site.y, cycle);
                traceDecision("redevelopment_lost", AITelemetry::Record().set("kind", R::kindName(o.kind))
                    .set("x", o.site.x).set("y", o.site.y));
                it = redevelopmentOrders.erase(it);
                continue;
            }
        } else if (cycle - o.placedCycle >= MILLI2CYCLES(R::kEffectDelayMs)) {
            // The lots still standing, before and after.
            long before = 0, after = 0, pollutionBefore = 0, pollutionAfter = 0;
            int n = 0;
            const auto& pollutionMap = currentGame->getCitySimulation()->getPollutionDensityMap();
            for (size_t i = 0; i < o.lots.size(); ++i) {
                const auto& [lot, value] = o.lots[i];
                const auto* object = getMap().getTile(lot.x, lot.y)->getNonInfantryGroundObject();
                if (!object || !object->isAStructure() || object->getOwner() != getHouse()
                    || !ownLot(static_cast<const StructureBase*>(object), getHouse())) continue;
                before += value;
                after += landValue.worldGet(lot.x, lot.y);
                pollutionBefore += i < o.pollution.size() ? o.pollution[i] : 0;
                pollutionAfter += pollutionMap.worldGet(lot.x, lot.y);
                ++n;
            }
            if (n > 0) {
                ++redevelopmentActivity.checked[o.kind];
                redevelopmentActivity.before10[o.kind] += before * 10 / n;
                redevelopmentActivity.after10[o.kind] += after * 10 / n;
                if (after > before) ++redevelopmentActivity.raised[o.kind];
                redevelopmentActivity.pollutionBefore10[o.kind] += pollutionBefore * 10 / n;
                redevelopmentActivity.pollutionAfter10[o.kind] += pollutionAfter * 10 / n;
                if (pollutionAfter < pollutionBefore) ++redevelopmentActivity.cleaner[o.kind];
            }
            SDL_Log("REDEVELOP: house=%d event=effect kind=%s raised=%d cleaner=%d lots=%d before=%ld after=%ld "
                    "pollution_before=%ld pollution_after=%ld cycle=%u", getHouse()->getHouseID(), R::kindName(o.kind),
                    int(n > 0 && after > before), int(n > 0 && pollutionAfter < pollutionBefore), n, before, after,
                    pollutionBefore, pollutionAfter, cycle);
            traceDecision("redevelopment_effect", AITelemetry::Record().set("kind", R::kindName(o.kind))
                .set("x", o.site.x).set("y", o.site.y).set("lots", n).set("before", before).set("after", after));
            it = redevelopmentOrders.erase(it);
            continue;
        }
        ++it;
    }
}

/// Every own R/C lot placed: whether it landed within kIndustrySeparationTiles of an own polluter
/// (a diagnostic: a HomeSeparation switch refusing such sites was tried and removed on 2026-11-27,
/// because the homes it refused were cleaner and worth more than the city's average home).
void QuantBot::noteHomePlaced(Coord site) {
    const Coord size = getStructureSize(Structure_ZoneResidential);
    int nearest = 1000000;
    for (const auto* s : getStructureList()) {
        if (s->getOwner() != getHouse() || s->getHealth() <= 0
            || DuneCity::getPollutionEmission(s->getItemID(), DuneCity::getStructureMaxLevel(s->getItemID())) <= 0) continue;
        const Coord o = s->getLocation(), other = s->getStructureSize();
        nearest = std::min(nearest, CityPlacementPolicy::footprintDistance(site.x, site.y, size.x, size.y,
                                                                           o.x, o.y, other.x, other.y));
    }
    ++redevelopmentActivity.homesPlaced;
    const bool near = RedevelopmentPolicy::industryTooCloseToHomes(nearest);
    if (near) ++redevelopmentActivity.homesNearIndustry;
    SDL_Log("HOME_PLACED: house=%d near_industry=%d industry_distance=%d x=%d y=%d", getHouse()->getHouseID(), int(near), nearest == 1000000 ? -1 : nearest, site.x, site.y);
}

/// Every own Industrial zone placed: whether it landed within kIndustrySeparationTiles of an own R/C lot.
void QuantBot::noteIndustryPlaced(Coord site) {
    const Coord size = getStructureSize(Structure_ZoneIndustrial);
    int nearest = 1000000;
    for (const auto* s : getStructureList()) {
        if (!ownLot(s, getHouse())) continue;
        const Coord o = s->getLocation(), lotSize = s->getStructureSize();
        nearest = std::min(nearest, CityPlacementPolicy::footprintDistance(site.x, site.y, size.x, size.y,
                                                                           o.x, o.y, lotSize.x, lotSize.y));
    }
    ++redevelopmentActivity.industryPlaced;
    const bool near = RedevelopmentPolicy::industryTooCloseToHomes(nearest);
    if (near) ++redevelopmentActivity.industryNearHomes;
    SDL_Log("INDUSTRY_PLACED: house=%d separated=%d near_homes=%d home_distance=%d x=%d y=%d", getHouse()->getHouseID(),
            int(botConfig().force.industrySeparation), int(near), nearest == 1000000 ? -1 : nearest, site.x, site.y);
}
