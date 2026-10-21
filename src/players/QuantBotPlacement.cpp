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

// QuantBot structure placement: city lots, reserved and recently lost footprints, redevelopment,
// turrets, batteries and battery clearance.

#include "QuantBotInternal.h"

using QuantBotInternal::rocketCoverageTier;
using QuantBotInternal::cityRoadImpact;
using QuantBotInternal::wouldLandlockNeighbouringZone;

namespace {
// A structure on this tile that is one of our city zones, or nullptr.
const StructureBase* ownZoneAt(const Map& map, int houseID, int x, int y) {
	if (!map.tileExists(x, y)) return nullptr;
	const ObjectBase* pObject = map.getTile(x, y)->getNonInfantryGroundObject();
	if (pObject == nullptr || !pObject->isAStructure()) return nullptr;
	const auto* pStructure = static_cast<const StructureBase*>(pObject);
	if (!DuneCity::isCityZoneStructure(pStructure->getItemID())) return nullptr;
	if (pStructure->getOwner() == nullptr || pStructure->getOwner()->getHouseID() != houseID) return nullptr;
	return pStructure;
}

// Count nearby R/C/I on each side, including across a single road tile.
// Two or more occupied sides identify an infill gap, independently of zone type.
int residentialInfillSides(const Map& map,int house,int x,int y,int w,int h) {
    int sides=0;
    for (int side=0;side<4;++side) {
        bool found=false;
        for (int gap=1;gap<=2 && !found;++gap) {
            const int length=side<2 ? w : h;
            for (int offset=0;offset<length;++offset) {
                const int tx=side<2 ? x+offset : (side==2 ? x-gap : x+w-1+gap);
                const int ty=side>=2 ? y+offset : (side==0 ? y-gap : y+h-1+gap);
                found |= ownZoneAt(map,house,tx,ty)!=nullptr;
            }
        }
        sides+=found;
    }
    return sides;
}

// Prefer filling a four-zone block, but only where its perimeter can carry roads.
int fourZoneBlockBonus(const Map& map, int house, int x, int y) {
    int best=0;
    for (int oy : {0,2}) for (int ox : {0,2}) {
        const int bx=x-ox, by=y-oy;
        if (!CityPlacementPolicy::fourZoneBlockFits(map.getSizeX(),map.getSizeY(),bx,by)) continue;
        bool valid=true; int neighbours=0;
        for (int sy=0; sy<4 && valid; sy+=2) for (int sx=0; sx<4 && valid; sx+=2) {
            const int zx=bx+sx, zy=by+sy;
            if (zx==x && zy==y) continue;
            const auto* zone=ownZoneAt(map,house,zx,zy);
            if (zone && zone->getLocation()==Coord(zx,zy)) { ++neighbours; continue; }
            for (int dy=0; dy<2; ++dy) for (int dx=0; dx<2; ++dx) {
                const auto* tile=map.getTile(zx+dx,zy+dy);
                if (!tile || tile->hasAStructure() || !DuneCity::isCityZoneTerrain(tile->getType())) valid=false;
            }
        }
        for (int dy=-1; dy<=4 && valid; ++dy) for (int dx=-1; dx<=4 && valid; ++dx) {
            if (dx>=0 && dx<4 && dy>=0 && dy<4) continue;
            const auto* tile=map.getTile(bx+dx,by+dy);
            if (!tile || (!tile->isRoadConnection() && (tile->hasAStructure()
                || !DuneCity::isCityBuildableTerrain(tile->getType())))) valid=false;
        }
        if (valid) best=std::max(best,neighbours*90);
    }
    return best;
}

// True when the tile could carry a road or already does, ignoring tiles that
// the candidate footprint (x, y, w, h) is about to cover.
bool tileKeepsFrontage(const Map& map, int tx, int ty, int x, int y, int w, int h) {
	if (!map.tileExists(tx, ty)) return false;
	if (tx >= x && tx < x + w && ty >= y && ty < y + h) return false;
	const Tile* t = map.getTile(tx, ty);
	if (t->isRoad()) return true;
	return !t->hasAStructure() && !t->hasCityZone() && !t->isMountain()
		&& !t->hasAGroundObject() && DuneCity::isCityZoneTerrain(t->getType());
}

// Is there one of our zones directly beside this lot, or one road tile away,
// sharing its row or column? That is the "next to each other or one away"
// pattern that keeps a road path along every row of lots.
bool alignedWithNeighbouringZone(const Map& map, int houseID, int x, int y, int w, int h) {
	const int offsets[4][2] = { { w, 0 }, { w + 1, 0 }, { 0, h }, { 0, h + 1 } };
	for (const auto& offset : offsets) {
		for (int sign = -1; sign <= 1; sign += 2) {
			const int nx = x + sign * offset[0];
			const int ny = y + sign * offset[1];
			const StructureBase* pZone = ownZoneAt(map, houseID, nx, ny);
			if (pZone != nullptr && pZone->getX() == nx && pZone->getY() == ny) return true;
		}
	}
	return false;
}

// What findPlaceLocation() reports about a candidate site (placementScoreDetails). Collected as
// plain numbers per candidate and turned into telemetry once, for the chosen site.
struct SiteQuality {
    int blockBonus = 0;
    bool city = false, sensitive = false;
    int supply = 0, separation = -1, nearestR = -1, nearestC = -1, nearestI = -1;
    int meanPollution = 0, meanTraffic = 0, meanLandValue = 0, sand = 0;
    int infill = 0, enemyClearance = 0, lossRisk = 0, fireRisk = 0, rear = 0;
    bool clearsReactor = false;
    int tier = 0, score = 0;
};

// The same fields, in the same order, as the per-candidate record findPlaceLocation() used to build.
AITelemetry::Record siteQualityRecord(const SiteQuality& q) {
    AITelemetry::Record record;
    record.set("four_zone_block_bonus", q.blockBonus);
    if (q.city) {
        record.set("supply_reachable", q.supply).set("pollution_buffer_tiles", q.separation)
            .set("nearest_res_origin", q.nearestR).set("nearest_com_origin", q.nearestC)
            .set("nearest_ind_origin", q.nearestI);
        if (q.sensitive)
            record.set("mean_pollution", q.meanPollution).set("mean_traffic", q.meanTraffic)
                .set("mean_land_value", q.meanLandValue).set("adjacent_sand", q.sand);
    }
    record.set("residential_infill_sides", q.infill);
    record.set("enemy_clearance_tiles", q.enemyClearance).set("recent_loss_risk", q.lossRisk)
        .set("enemy_fire_risk", q.fireRisk).set("rear_score", q.rear).set("reactor_clearance", q.clearsReactor ? 1 : 0);
    return record.set("tier", q.tier).set("score", q.score);
}
} // namespace

namespace QuantBotInternal {
CityPlacementPolicy::RoadImpact cityRoadImpact(const Map& map, int x, int y, int w, int h, Uint32 item) {
    if (!currentGame || !currentGame->isCitySimEnabled() || item == Structure_Slab1
        || item == Structure_Slab4 || item == Structure_Road) return {};
    return CityPlacementPolicy::assessRoadsOnMap(map.getSizeX(), map.getSizeY(), x, y, w, h,
        item == Structure_RocketTurret,
        [&](int tx, int ty) { return map.tileExists(tx, ty) && map.getTile(tx, ty)->isRoadConnection(); },
        [&](int tx, int ty) {
            const auto* tile=map.getTile(tx,ty);
            return tile && !tile->hasAGroundObject() && DuneCity::isCityBuildableTerrain(tile->getType());
        });
}

// Would a lot at (x, y, w, h) take away the last open side of a neighbouring
// zone? Every lot must keep a side where a road can run.
// `clearedZones`, when given, are lots this placement demolishes: they cannot
// be landlocked by it and their tiles become open frontage for the lots around
// them.
bool wouldLandlockNeighbouringZone(const Map& map, int houseID, int x, int y, int w, int h,
                                   const std::vector<Uint32>* clearedZones) {
	std::set<Uint32> checked;
	auto isCleared = [&](Uint32 id) {
		return clearedZones && std::find(clearedZones->begin(), clearedZones->end(), id) != clearedZones->end();
	};
	auto keepsFrontage = [&](int tx, int ty) {
		if (clearedZones && map.tileExists(tx, ty)) {
			const ObjectBase* pObject = map.getTile(tx, ty)->getNonInfantryGroundObject();
			if (pObject != nullptr && isCleared(pObject->getObjectID())
			    && !(tx >= x && tx < x + w && ty >= y && ty < y + h))
				return true;
		}
		return tileKeepsFrontage(map, tx, ty, x, y, w, h);
	};
	auto sealsNeighbour = [&](int nx, int ny) {
		const StructureBase* pZone = ownZoneAt(map, houseID, nx, ny);
		if (pZone == nullptr || isCleared(pZone->getObjectID())
		    || !checked.insert(pZone->getObjectID()).second) return false;
		const int zx = pZone->getX(), zy = pZone->getY();
		const int zw = pZone->getStructureSizeX(), zh = pZone->getStructureSizeY();
		for (int i = zx; i < zx + zw; i++) {
			if (keepsFrontage(i, zy - 1)) return false;
			if (keepsFrontage(i, zy + zh)) return false;
		}
		for (int j = zy; j < zy + zh; j++) {
			if (keepsFrontage(zx - 1, j)) return false;
			if (keepsFrontage(zx + zw, j)) return false;
		}
		return true;
	};
	for (int i = x; i < x + w; i++) {
		if (sealsNeighbour(i, y - 1) || sealsNeighbour(i, y + h)) return true;
	}
	for (int j = y; j < y + h; j++) {
		if (sealsNeighbour(x - 1, j) || sealsNeighbour(x + w, j)) return true;
	}
	return false;
}
} // namespace QuantBotInternal

std::vector<std::array<int, 4>> QuantBot::reactorBlastSites(Uint32 item) const {
    // Exactly the pairs reactorClearance() always tested: a reactor against protected buildings,
    // a protected building against reactors. Every other own structure and reservation passed
    // trivially, so leaving them out of the list changes no answer.
    std::vector<std::array<int, 4>> sites;
    const auto critical = TacticalSafetyPolicy::protectedReactorNeighbour;
    if (!critical(item)) return sites;
    auto relevant = [&](Uint32 other) {
        return (item == Structure_NuclearPlant && critical(other)) || (other == Structure_NuclearPlant && critical(item));
    };
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0 && relevant(structure->getItemID())) {
            const Coord location = structure->getLocation(), size = structure->getStructureSize();
            sites.push_back({location.x, location.y, size.x, size.y});
        }
    for (const auto& entry : reservedStructures)
        if (entry.first != planningBuilder && relevant(entry.second.item)) {
            const Coord size = getStructureSize(entry.second.item);
            sites.push_back({entry.second.location.x, entry.second.location.y, size.x, size.y});
        }
    return sites;
}

bool QuantBot::clearsBlastSites(const std::vector<std::array<int, 4>>& sites, Coord pos, Coord size) {
    for (const auto& site : sites)
        if (!TacticalSafetyPolicy::blastClearance(pos.x, pos.y, size.x, size.y, site[0], site[1], site[2], site[3]))
            return false;
    return true;
}

bool QuantBot::reactorClearance(Uint32 item, Coord pos) const {
    if (!TacticalSafetyPolicy::protectedReactorNeighbour(item)) return true;
    return clearsBlastSites(reactorBlastSites(item), pos, getStructureSize(item));
}

int QuantBot::rearScore(Coord pos, Coord base) const {
    if (visibleEnemyBases.empty()) return 0;
    int fromSite = 100000, fromBase = 100000;
    for (Coord enemy : visibleEnemyBases) {
        fromSite = std::min(fromSite, std::max(std::abs(pos.x-enemy.x), std::abs(pos.y-enemy.y)));
        fromBase = std::min(fromBase, std::max(std::abs(base.x-enemy.x), std::abs(base.y-enemy.y)));
    }
    return std::clamp(fromSite-fromBase, -20, 20) * 40;
}

const DuneCity::BoxAnyField& QuantBot::anchorField() const {
    // Map::isWithinBuildRange() is a box dilation of this house's owned tiles
    // evaluated pointwise: BUILDRANGE is 2, so each query scans 5x5 tiles and
    // stops at the first owned one. The marker here is exactly that scan's
    // per-tile test, isConstructionAnchor(tile->getOwner(), houseID), and
    // BoxAnyField clips its queries to the map just as getTile_internal() skips
    // off-map probes — so anyWithin(x,y,BUILDRANGE) answers the same question
    // for every coordinate, map edges included.
    const int houseID=getHouse()->getHouseID();
    const Map& map=getMap();
    const int width=map.getSizeX(), height=map.getSizeY();
    auto& cache=anchorFieldCache;
    const Uint64 ownership=Tile::ownerRevision(houseID);
    if (cache.valid && cache.ownership==ownership && cache.map==&map && cache.width==width && cache.height==height) {
        ++cache.hits;
        return cache.field;
    }
    AITelemetry::PerformanceScope anchorScope("ai.placement.anchor_field", getGameCycleCount(), houseID);
    // Tiles are stored column by column: read the owners in that order, then
    // build the field from the row-major marks.
    std::vector<uint8_t> owned(static_cast<size_t>(width)*height,0);
    for (int x=0; x<width; ++x)
        for (int y=0; y<height; ++y)
            owned[static_cast<size_t>(y)*width+x]=DuneCity::isConstructionAnchor(map.getTile(x,y)->getOwner(),houseID);
    cache.field.build(width,height,[&](int x,int y) { return owned[static_cast<size_t>(y)*width+x]!=0; });
    cache.ownership=ownership; cache.map=&map; cache.width=width; cache.height=height; cache.valid=true;
    ++cache.builds;
    return cache.field;
}

bool QuantBot::nearRecentStructureLoss(int x, int y, int width, int height) const {
    for (const auto& loss : recentStructureLosses)
        if (CityPlacementPolicy::recentLossBlocks(x, y, width, height,
                loss.location.x, loss.location.y, loss.size.x, loss.size.y,
                getGameCycleCount() - loss.cycle, MILLI2CYCLES(60000))) return true;
    return false;
}

bool QuantBot::overlapsReservedStructure(int x, int y, int width, int height) const {
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder) continue;
        const auto& plan = entry.second;
        const auto size = getStructureSize(plan.item);
        if (CityPlacementPolicy::overlaps(x, y, width, height,
                plan.location.x, plan.location.y, size.x, size.y)) return true;
    }
    return false;
}

bool QuantBot::ownUnitCannotLeave(const UnitBase* unit) const {
    // Ordered away (a forced move) yet standing with no path coming: the walk off the site
    // failed, so the site waits for nothing and is planned elsewhere instead.
    return !unit->isRespondable()
        || (unit->wasForced() && !unit->isMoving() && !unit->hasPendingPathRequest()
            && unit->getDestination().isValid() && unit->getDestination() != unit->getLocation());
}

bool QuantBot::tileBlocksOwnPlacement(const Tile* tile) const {
    if (tile->isMountain() || tile->hasAStructure()) return true;
    if (tile->hasANonInfantryGroundObject()) {
        const auto* unit = dynamic_cast<const UnitBase*>(tile->getNonInfantryGroundObject());
        if (!unit || unit->getOwner() != getHouse() || ownUnitCannotLeave(unit)) return true;
    }
    for (const Uint32 id : tile->getInfantryList()) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit || unit->getOwner() != getHouse() || ownUnitCannotLeave(unit)) return true;
    }
    return false;
}

bool QuantBot::placeableAmongOwnUnits(int x, int y, int width, int height, Uint32 item) const {
    // The engine check with units ignored (which also skips its structure test), then the
    // footprint itself: no mountain or structure, and nobody but our own leaving traffic.
    if (!getMap().okayToPlaceStructure(x, y, width, height, false,
            item == Structure_ConstructionYard ? nullptr : getHouse(), true, item)) return false;
    for (int ty = y; ty < y + height; ++ty)
        for (int tx = x; tx < x + width; ++tx)
            if (tileBlocksOwnPlacement(getMap().getTile(tx, ty))) return false;
    return true;
}

bool QuantBot::ownUnitsOnFootprint(int x, int y, int width, int height) const {
    for (int ty = y; ty < y + height; ++ty)
        for (int tx = x; tx < x + width; ++tx) {
            if (!getMap().tileExists(tx, ty)) continue;
            const Tile* tile = getMap().getTile(tx, ty);
            if (tile->hasANonInfantryGroundObject()) {
                const auto* object = tile->getNonInfantryGroundObject();
                if (object && object->isAUnit() && object->getOwner() == getHouse()) return true;
            }
            for (const Uint32 id : tile->getInfantryList()) {
                const auto* object = getObject(id);
                if (object && object->getOwner() == getHouse()) return true;
            }
        }
    return false;
}

bool QuantBot::onPlannedSite(Coord p) const {
    for (const auto& entry : reservedStructures) {
        const auto size = getStructureSize(entry.second.item);
        if (CityPlacementPolicy::overlaps(p.x, p.y, 1, 1, entry.second.location.x, entry.second.location.y,
                size.x, size.y)) return true;
    }
    return false;
}

void QuantBot::keepPlannedSitesClear() {
    // Every yard's planned site, in yard id order (the map's), so every peer issues the same
    // orders. Units walking through are left alone; idle and holding units are sent to the
    // nearest free tile beside the site.
    for (const auto& [builderID, plan] : reservedStructures) {
        const auto* yard = dynamic_cast<const ConstructionYard*>(getObject(builderID));
        if (!yard || yard->getOwner() != getHouse() || !plan.location.isValid()) continue;
        const Coord size = getStructureSize(plan.item);
        if (!ownUnitsOnFootprint(plan.location.x, plan.location.y, size.x, size.y)) continue;
        doMoveOwnUnitsOffPlot(yard, plan.item, plan.location.x, plan.location.y);
    }
}

namespace {
bool needsGroundExit(Uint32 item) {
    return item == Structure_HeavyFactory || item == Structure_LightFactory
        || item == Structure_Refinery || item == Structure_RepairYard
        || item == Structure_Barracks || item == Structure_WOR
        || item == Structure_StarPort || item == Structure_PoliceStation;
}
bool blocksGroundAccess(Uint32 item) {
    return item != Structure_Road && item != Structure_Slab1 && item != Structure_Slab4;
}

// Distinct-building counter for one side of a candidate footprint. Placement
// scoring only asks whether a side touches more than one building, which is
// exactly "a second, different id arrived"; repeats of one id never count.
struct SideBuildings {
    Uint32 first = 0;
    bool present = false;
    bool multiple = false;

    void insert(Uint32 buildingID) {
        if (!present) { first = buildingID; present = true; }
        else if (buildingID != first) { multiple = true; }
    }

    bool touchesMultiple() const { return multiple; }
};
} // namespace

void QuantBot::clearPlacementCache(bool geometryChanged, bool reuseForBuilder) {
    // Within a build pass, unreserved yards see the same map and reservation
    // set until an order changes geometry. Reuse successful AND failed searches.
    // A yard with its own reservation excludes that reservation, so its key
    // differs. All order/geometry changes keep the original invalidation.
    const Uint32 excluded=reservedStructures.count(planningBuilder) ? planningBuilder : NONE_ID;
    if (geometryChanged || !reuseForBuilder || placementCacheExcludedBuilder!=excluded) {
        placementCache.clear();
        // Correctness does not depend on this: the distance fields re-verify
        // their inputs on every use. Released here so a finished build pass
        // holds no field.
        placementDistances.invalidate();
    }
    placementCacheExcludedBuilder=excluded;
    // The emplacement searches are not keyed by the reservation set alone: an
    // enemy-facing battery is anchored on the colony of the yard that is
    // planning it, so two unreserved yards at opposite ends of the map share
    // placementCacheExcludedBuilder==NONE_ID and would otherwise read each
    // other's belt. Anything anchored on the planning builder is retired when
    // that builder changes as well as on any geometry change.
    const bool anchorChanged = turretSearchBuilder != planningBuilder;
    if (geometryChanged) {
        cityServiceSearch.invalidate();
        cityTurretSearch.invalidate();
    }
    if (geometryChanged || anchorChanged) {
        // The non-city emplacement search is keyed by item and reused by the
        // several validity probes in one build pass. A geometry change - a
        // placement or a reservation - retires it exactly like the others.
        turretSiteCache.clear();
        // The clearance fallback depends on the same geometry, on the same
        // anchor and on which lots are still standing, so it goes with them.
        batteryClearanceCache.reset();
    }
    turretSearchBuilder = planningBuilder;
}

bool QuantBot::preservesGroundAccess(Uint32 item, Coord pos, const std::vector<Uint32>* clearedZones) {
    if (!blocksGroundAccess(item)) return true;
    if (!pos.isValid()) return false;
    const auto size=getStructureSize(item);
    // A lot this placement is about to demolish is already gone as far as local
    // passage is concerned: the building only stands once the ground is clear.
    // Judging it against the lot still in place would reject exactly the
    // crowded sites the clearance fallback exists for.
    auto cleared=[&](const ObjectBase* object) {
        return clearedZones && object && std::find(clearedZones->begin(),clearedZones->end(),
            object->getObjectID())!=clearedZones->end();
    };
    auto passable=[&](int x,int y) {
        if (!getMap().tileExists(x,y)) return false;
        const auto* tile=getMap().getTile(x,y);
        if (tile->isMountain()) return false;
        if (tile->hasAStructure() && !cleared(tile->getNonInfantryGroundObject())) return false;
        for (const auto& entry:reservedStructures) {
            if (entry.first==planningBuilder || !blocksGroundAccess(entry.second.item)) continue;
            const auto p=entry.second.location, extent=getStructureSize(entry.second.item);
            if (x>=p.x && x<p.x+extent.x && y>=p.y && y<p.y+extent.y) return false;
        }
        return true;
    };
    if (!GroundAccessPolicy::allows({pos.x,pos.y,size.x,size.y},needsGroundExit(item),passable,
            item==Structure_RocketTurret)) return false;
    auto keepsExit=[&](Coord p,Coord extent) {
        for (int ey=p.y-1;ey<=p.y+extent.y;++ey) for (int ex=p.x-1;ex<=p.x+extent.x;++ex) {
            if (ex>=p.x && ex<p.x+extent.x && ey>=p.y && ey<p.y+extent.y) continue;
            if (ex>=pos.x && ex<pos.x+size.x && ey>=pos.y && ey<pos.y+size.y) continue;
            if (passable(ex,ey)) return true;
        }
        return false;
    };
    // Check only producers touching this placement, so its last deployment
    // opening cannot be covered. No scan of all factories or moving units.
    std::set<Uint32> checked;
    for (int y=pos.y-1;y<=pos.y+size.y;++y) for (int x=pos.x-1;x<=pos.x+size.x;++x) {
        if (x>=pos.x && x<pos.x+size.x && y>=pos.y && y<pos.y+size.y) continue;
        if (!getMap().tileExists(x,y)) continue;
        const auto* object=getMap().getTile(x,y)->getNonInfantryGroundObject();
        if (!object || !object->isAStructure() || !needsGroundExit(object->getItemID())
            || !checked.insert(object->getObjectID()).second) continue;
        if (!keepsExit(object->getLocation(),getStructureSize(object->getItemID()))) return false;
    }
    for (const auto& entry:reservedStructures) {
        if (entry.first==planningBuilder || !needsGroundExit(entry.second.item)) continue;
        const auto p=entry.second.location, extent=getStructureSize(entry.second.item);
        if (CityPlacementPolicy::overlaps(pos.x-1,pos.y-1,size.x+2,size.y+2,p.x,p.y,extent.x,extent.y)
            && !keepsExit(p,extent)) return false;
    }
    return true;
}

bool QuantBot::redevelopmentZones(Uint32 item, Coord pos, std::vector<Uint32>& zones) const {
    zones.clear();
    auto* sim = currentGame->isCitySimEnabled() ? currentGame->getCitySimulation() : nullptr;
    if (!sim || (item != Structure_HeavyFactory && item != Structure_NuclearPlant && item != Structure_WindTrap)) return false;
    const Coord size = getStructureSize(item);
    bool range = false;
    for (int y=pos.y; y<pos.y+size.y; ++y) for (int x=pos.x; x<pos.x+size.x; ++x) {
        const auto* tile = getMap().getTile(x,y);
        if (!tile || !tile->isRock()) return false;
        range = range || getMap().isWithinBuildRange(x,y,getHouse());
        const auto* object = tile->getNonInfantryGroundObject();
        if (!object) { if (tile->isBlocked() || tile->hasCityZone()) return false; continue; }
        const auto* zone = dynamic_cast<const ZoneStructure*>(object);
        if (!zone || zone->getOwner() != getHouse()) return false;
        const Coord z = zone->getLocation();
        if (sim->getLandValueMap().worldGet(z.x,z.y) > 64
            || zone->getCivicOverlay() != ZoneStructure::CivicOverlay::None) return false;
        if (std::find(zones.begin(),zones.end(),zone->getObjectID()) == zones.end()) zones.push_back(zone->getObjectID());
    }
    return range && !zones.empty() && zones.size() <= 4;
}

Coord QuantBot::findRedevelopmentSite(Uint32 item) {
    AITelemetry::PerformanceScope perfScope("ai.findRedevelopmentSite", getGameCycleCount(), getHouse()->getHouseID());
    if (!currentGame->isCitySimEnabled()
        || (item != Structure_HeavyFactory && item != Structure_NuclearPlant && item != Structure_WindTrap)) return Coord::Invalid();
    const Coord size=getStructureSize(item), base=findBaseCentre(getHouse()->getHouseID());
    Coord best=Coord::Invalid();
    int bestScore=std::numeric_limits<int>::max();
    auto bestFactoryRank=TacticalSafetyPolicy::factorySiteRank(1,-1,-1,0);
    auto bestReactorRank=TacticalSafetyPolicy::reactorSiteRank(100000,100000,-1,false,0,0);
    auto* sim=currentGame->getCitySimulation();
    if (!sim) return best;
    const bool blastChecked = TacticalSafetyPolicy::protectedReactorNeighbour(item);
    const auto blastSites = reactorBlastSites(item);   // fixed for the whole search
    for (int y=std::max(0,base.y-50); y<=std::min(getMap().getSizeY()-size.y,base.y+50); ++y)
        for (int x=std::max(0,base.x-50); x<=std::min(getMap().getSizeX()-size.x,base.x+50); ++x) {
            const Coord pos(x,y);
            std::vector<Uint32> zones;
            if (!redevelopmentZones(item,pos,zones) || overlapsReservedStructure(x,y,size.x,size.y)
                || nearRecentStructureLoss(x,y,size.x,size.y) || dangerAt(pos,size)>0
                || (blastChecked && !clearsBlastSites(blastSites,pos,size)) || !preservesGroundAccess(item,pos) || !cityRoadImpact(getMap(),x,y,size.x,size.y,item).preservesConnections) continue;
            int score=static_cast<int>(zones.size())*100;
            for (Uint32 id : zones) {
                const auto* zone=static_cast<const ZoneStructure*>(currentGame->getObjectManager().getObject(id));
                const Coord z=zone->getLocation();
                const int density=std::max(int(getMap().getTile(z.x,z.y)->getCityZoneDensity()),
                    zone->getResidentialPopulation() > 0 ? 1 : 0);
                const auto& state=sim->getHouseState(getHouse()->getHouseID());
                const bool residential=zone->getItemID()==Structure_ZoneResidential;
                const int demand=residential ? state.resValve : zone->getItemID()==Structure_ZoneCommercial ? state.comValve : state.indValve;
                score += RedevelopmentPolicy::displacementCost(density,sim->getLandValueMap().worldGet(z.x,z.y),demand,residential?2000:1500);
            }
            score += blockDistance(pos,base).lround()-rearScore(pos,base);
            const auto rank=TacticalSafetyPolicy::factorySiteRank(dangerAt(pos,size,true),
                TacticalSafetyPolicy::footprintClearance(factoryClearance(),getMap().getSizeX(),getMap().getSizeY(),
                    x,y,size.x,size.y),0,-score);
            const auto reactorRank=TacticalSafetyPolicy::reactorSiteRank(0,dangerAt(pos,size,true),
                TacticalSafetyPolicy::footprintClearance(factoryClearance(),getMap().getSizeX(),getMap().getSizeY(),
                    x,y,size.x,size.y),true,rearScore(pos,base),-score);
            if (item==Structure_NuclearPlant ? reactorRank>bestReactorRank
                : TacticalSafetyPolicy::productionFactory(item) ? rank>bestFactoryRank : score<bestScore) {
                bestReactorRank=reactorRank; bestFactoryRank=rank; bestScore=score; best=pos;
            }
        }
    return best;
}

Coord QuantBot::findPlaceLocation(Uint32 itemID) {
    AITelemetry::PerformanceScope perfScope("ai.findPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
    refreshTacticalDanger();
    int accessRejected = 0, pollutionRejected = 0, reservedRejected = 0, roadRejected = 0, neighbourRejected = 0;
    int productionPlotRejected = 0;
    int searchPassUsed = 0;
	// Check per-build-cycle cache first
	auto cacheIt = placementCache.find(itemID);
	if (cacheIt != placementCache.end()) {
        AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"ai.placement_cache_hit",1,itemID,false);
		return cacheIt->second;
	}
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"ai.placement_search",1,itemID,false);

	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();
	Coord baseCenter = findBaseCentre(getHouse()->getHouseID());

	int bestLocationScore = std::numeric_limits<int>::min();
	Coord bestLocation = Coord::Invalid();
    int bestSiteTier = -1;
    int bestInfill=0;
    bool bestSafe=false;
    const bool factoryPlacement = TacticalSafetyPolicy::productionFactory(itemID);
    auto bestFactoryRank = TacticalSafetyPolicy::factorySiteRank(1,-1,-1,0);
    AITelemetry::Record bestQuality;
    SiteQuality bestSite;
    bool haveBestSite = false;
    auto bestReactorRank = TacticalSafetyPolicy::reactorSiteRank(100000,100000,-1,false,0,std::numeric_limits<int>::min());
    int candidates = 0, threatRejected = 0, blastRejected = 0, lossRejected = 0;

	bool itemIsBuilder = (itemID == Structure_HeavyFactory
		|| itemID == Structure_RepairYard
		|| itemID == Structure_LightFactory
		|| itemID == Structure_WOR
		|| itemID == Structure_Barracks
		|| itemID == Structure_StarPort);

	// City zones follow road-frontage rules instead of the compact-base
	// scoring: they sit next to each other or one road tile apart, and never
	// pack into blocks that landlock the inner lots.
	const bool cityZonePlacement = currentGame && currentGame->isCitySimEnabled()
		&& DuneCity::isCityZoneStructure(itemID);
	const int houseID = getHouse()->getHouseID();

	// Bound search to radius around base center instead of scanning entire map
	int searchRadius = 50;
	int mapW = getMap().getSizeX();
	int mapH = getMap().getSizeY();
	int startX = std::max(0, baseCenter.x - searchRadius);
	int startY = std::max(0, baseCenter.y - searchRadius);
	int endX = std::min(mapW - newSizeX, baseCenter.x + searchRadius);
	int endY = std::min(mapH - newSizeY, baseCenter.y + searchRadius);

    // Snapshot only the zones used by proximity scoring. A bounded index
    // replaces the full structure-list walk for every candidate origin.
    struct ZoneNeighbour { Coord location; int item; };
    std::vector<ZoneNeighbour> zoneNeighbours;
    LocalPointIndex zoneIndex(mapW,mapH);
    if (DuneCity::isCityZoneStructure(itemID)) {
        for (const auto* structure : getStructureList()) {
            if (structure->getOwner() != getHouse() || !DuneCity::isCityZoneStructure(structure->getItemID())) continue;
            const auto location=structure->getLocation();
            zoneIndex.add(location.x,location.y,zoneNeighbours.size());
            zoneNeighbours.push_back({location,structure->getItemID()});
        }
    }

    // Resolve nearest origins and full-footprint pollution separation once,
    // instead of scanning every neighbour again at every candidate. These
    // fields include the same live buildings and other yards' reservations.
    // Build lazily: crowded maps often reject every site before scoring.
    auto* citySim = currentGame && currentGame->isCitySimEnabled() ? currentGame->getCitySimulation() : nullptr;
    const auto newRole = DuneCity::getStructureCityRole(itemID);
    const bool newSensitive = newRole == DuneCity::CityRole::Residential || newRole == DuneCity::CityRole::Commercial;
    const bool newPolluter = DuneCity::getPollutionEmission(itemID, DuneCity::getStructureMaxLevel(itemID)) > 0;
    const bool needCityDistances = citySim && (newSensitive || newPolluter);
    bool cityDistancesReady=false;
    auto& nearestResidential=placementDistances.nearestResidential;
    auto& nearestCommercial=placementDistances.nearestCommercial;
    auto& nearestIndustrial=placementDistances.nearestIndustrial;
    auto& pollutionSeparation=placementDistances.pollutionSeparation;
    auto addNeighbour = [&](Uint32 item, Coord location, Coord size, bool origins, bool pollution) {
        const auto role = DuneCity::getStructureCityRole(item);
        if (origins) {
            if (role == DuneCity::CityRole::Residential) nearestResidential.add(location.x,location.y);
            if (role == DuneCity::CityRole::Commercial) nearestCommercial.add(location.x,location.y);
            if (role == DuneCity::CityRole::Industrial) nearestIndustrial.add(location.x,location.y);
        }
        if (!pollution) return;
        const bool pollutes=DuneCity::getPollutionEmission(item,DuneCity::getStructureMaxLevel(item))>0;
        const bool sensitive=role==DuneCity::CityRole::Residential || role==DuneCity::CityRole::Commercial;
        if ((newSensitive && pollutes) || (newPolluter && sensitive))
            pollutionSeparation.add(location.x,location.y,size.x,size.y);
    };
    // The contributions are a walk over a few thousand entries and the fields
    // only bucket them (CityDistanceField answers each query from the source
    // rectangles). Compare the list element by element against the cached one
    // and rebuild only the fields whose inputs actually moved. The nearest-origin
    // fields are independent of the planned item, so they survive across itemIDs.
    auto prepareCityDistances = [&] {
        if (cityDistancesReady) return;
        cityDistancesReady=true;
        AITelemetry::PerformanceScope fieldScope("ai.placement.distance_fields",
            getGameCycleCount(), getHouse()->getHouseID(), static_cast<int>(itemID));

        auto& cache=placementDistances;
        std::vector<PlacementDistanceCache::Contribution> key;
        for (const auto* structure : getStructureList())
            if (structure->getOwner() == getHouse() && structure->getHealth() > 0) {
                const auto location=structure->getLocation(), size=structure->getStructureSize();
                key.push_back({static_cast<Uint32>(structure->getItemID()),location.x,location.y,size.x,size.y});
            }
        for (const auto& entry : reservedStructures)
            if (entry.first != planningBuilder) {
                const auto location=entry.second.location, size=getStructureSize(entry.second.item);
                key.push_back({static_cast<Uint32>(entry.second.item),location.x,location.y,size.x,size.y});
            }

        const bool sameInputs = cache.width==mapW && cache.height==mapH && cache.key==key;
        const bool reuseOrigins = sameInputs && cache.originFieldsValid;
        const bool reusePollution = sameInputs && cache.pollutionValid
            && cache.sensitive==newSensitive && cache.polluter==newPolluter;
        if (reuseOrigins && reusePollution) return;

        if (!reuseOrigins) {
            nearestResidential=CityDistanceField(mapW,mapH); nearestCommercial=CityDistanceField(mapW,mapH);
            nearestIndustrial=CityDistanceField(mapW,mapH);
        }
        if (!reusePollution) pollutionSeparation=CityDistanceField(mapW,mapH);
        for (const auto& contribution : key)
            addNeighbour(contribution.item,Coord(contribution.x,contribution.y),
                Coord(contribution.w,contribution.h),!reuseOrigins,!reusePollution);
        if (!reuseOrigins) { nearestResidential.build(); nearestCommercial.build(); nearestIndustrial.build(); ++cache.originBuilds; }
        if (!reusePollution) { pollutionSeparation.build(); ++cache.pollutionBuilds; }

        cache.key=std::move(key); cache.width=mapW; cache.height=mapH;
        cache.originFieldsValid=true; cache.pollutionValid=true;
        cache.sensitive=newSensitive; cache.polluter=newPolluter;
    };

	// Pre-collect spice tile positions for refinery placement (avoids O(N^2) inner loop)
	std::vector<Coord> spiceTiles;
	if (itemID == Structure_Refinery) {
		// Only scan spice in a wider area around the base (no need for full map)
		int spiceRadius = searchRadius + 30;
		int spStartX = std::max(0, baseCenter.x - spiceRadius);
		int spStartY = std::max(0, baseCenter.y - spiceRadius);
		int spEndX = std::min(mapW - 1, baseCenter.x + spiceRadius);
		int spEndY = std::min(mapH - 1, baseCenter.y + spiceRadius);
		for (int sx = spStartX; sx <= spEndX; sx++) {
			for (int sy = spStartY; sy <= spEndY; sy++) {
				if (getMap().tileExists(sx, sy) && getMap().getTile(sx, sy)->hasSpice()) {
					spiceTiles.emplace_back(sx, sy);
				}
			}
		}
	}

    // A cramped city keeps legal ground that no longer suits the road grid.
    // Roads are a layout preference; production capacity is not. Factories,
    // builders and repair yards may take such a site once the ordinary search
    // has failed, so the base is never called full while it can still host the
    // building an engine placement check accepts.
    const bool roadingFallbackItem = currentGame->isCitySimEnabled() && (itemIsBuilder || factoryPlacement);
    bool roadingRelaxed = false;

    // Owned-tile field for the build-range gate (anchorField(): rebuilt only when
    // this house's tile ownership changed). Reservations and planningBuilder do
    // not move an owned tile.
    const bool gateByBuildRange = itemID != Structure_ConstructionYard;
    const DuneCity::BoxAnyField* buildRangeField = nullptr;
    size_t buildRangeSkipped = 0;
    if (gateByBuildRange) buildRangeField = &anchorField();

    // Reactor blast spacing: the own buildings and reservations it is tested against do not
    // change during the search, so collect them once instead of walking every structure on the
    // map (all houses, every city zone) for each candidate.
    const bool blastChecked = TacticalSafetyPolicy::protectedReactorNeighbour(itemID);
    const auto blastSites = reactorBlastSites(itemID);
    auto clearsReactorAt = [&](int x, int y) {
        return !blastChecked || clearsBlastSites(blastSites, Coord(x, y), Coord(newSizeX, newSizeY));
    };

    // Keep the fast ordinary search, but never treat its averaged base centre
    // as the limit of a spread-out city's buildable territory.
    AITelemetry::PerformanceScope candidateScope("ai.placement.candidates",
        getGameCycleCount(), getHouse()->getHouseID(), static_cast<int>(itemID));
    for (int searchPass=0; searchPass<3; ++searchPass) {
        if (searchPass==1) {
            if (bestLocation.isValid() && itemID != Structure_NuclearPlant) break;
            startX=0; startY=0; endX=mapW-newSizeX; endY=mapH-newSizeY;
        }
        if (searchPass==2 && (bestLocation.isValid() || !roadingFallbackItem)) break;
        // Only the road-layout preferences are dropped. Engine placement, ground
        // egress, reservations, blast clearance and threat rules still decide.
        const bool relaxRoading = searchPass==2;
        searchPassUsed=searchPass;
	for (int placeLocationX = startX; placeLocationX <= endX; placeLocationX++) {
		for (int placeLocationY = startY; placeLocationY <= endY; placeLocationY++) {
			if (!relaxRoading && !CityPlacementPolicy::inPlacementSearchPass(placeLocationX,placeLocationY,
                baseCenter.x,baseCenter.y,searchRadius,searchPass)) continue;
            // Both okayToPlaceStructure() overloads finish with
            // `return withinBuildRange`, an OR over the footprint of
            // isWithinBuildRange(), which is itself "any owned tile within
            // BUILDRANGE". So the whole call can only return true when at least
            // one anchor lies in the footprint expanded by BUILDRANGE. Where no
            // anchor does, the call is guaranteed false and is skipped here
            // instead of re-running a 5x5 ownership probe per footprint tile.
            // The function is const and has no side effects, so skipping is the
            // same control flow the false return produced — `candidates` is only
            // incremented inside the body either way. A construction yard passes
            // pHouse == nullptr, which makes withinBuildRange unconditionally
            // true, so the gate must not apply to it.
            if (gateByBuildRange && !buildRangeField->anyNearFootprint(
                    placeLocationX, placeLocationY, newSizeX, newSizeY, BUILDRANGE)) {
                ++buildRangeSkipped;
                continue;
            }
            // First check if this location is valid for building. Our own units standing
            // there are traffic (the footprint rule): they are asked off once it is planned.
			if (placeableAmongOwnUnits(placeLocationX, placeLocationY, newSizeX, newSizeY, itemID)) {

                // A zone may spill onto sand, but slabs cannot. When foundations
                // are enabled, choose a footprint that can actually be prepared.
                if (cityZonePlacement && getGameInitSettings().getGameOptions().concreteRequired
                    && !getMap().okayToPlaceStructure(placeLocationX,placeLocationY,newSizeX,newSizeY,false,getHouse(),true)) continue;
                ++candidates;
                if (overlapsReservedStructure(placeLocationX, placeLocationY, newSizeX, newSizeY)) { ++reservedRejected; continue; }
                // In DuneCity every structure keeps off the reserved factory/repair plots (a
                // factory may use its own; an emergency power plant may use any): five
                // refineries on the heavy factory's plot left Ordos m17 without one for 80 min.
                if ((cityZonePlacement || planningCityProductionPlots
                        || (citySim && !(!getHouse()->hasPower()
                            && (itemID==Structure_WindTrap || itemID==Structure_NuclearPlant))))
                    && std::any_of(cityProductionPlots.begin(),cityProductionPlots.end(),[&](const auto& plot) {
                        if (plot.item==itemID) return false;
                        const auto size=getStructureSize(plot.item);
                        // Leave access around the future factory/repair footprint.
                        return CityPlacementPolicy::overlaps(placeLocationX,placeLocationY,newSizeX,newSizeY,
                            plot.location.x-1,plot.location.y-1,size.x+2,size.y+2);
                    })) { ++productionPlotRejected; continue; }
                // Use the same origin sample and role-specific gate as zone growth.
                // Industry tolerates pollution; R/C must not become vacant dead lots.
                if (citySim && cityZonePlacement && DuneCity::isPollutionBlockingGrowth(
                    citySim->getPollutionDensityMap().worldGet(placeLocationX, placeLocationY), newRole, 1)) {
                    ++pollutionRejected;
                    continue;
                }
                if (!preservesGroundAccess(itemID,Coord(placeLocationX,placeLocationY))) { ++accessRejected; continue; }
                if (itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall
                    && itemID != Structure_NuclearPlant
                    && nearRecentStructureLoss(placeLocationX, placeLocationY, newSizeX, newSizeY)) { ++lossRejected; continue; }
                if (itemID != Structure_RocketTurret && itemID != Structure_GunTurret && itemID != Structure_Wall) {
                    if (dangerAt(Coord(placeLocationX, placeLocationY), Coord(newSizeX, newSizeY)) > 0) { ++threatRejected; continue; }
                    if (!TacticalSafetyPolicy::reactorPlacementAllowed(itemID, clearsReactorAt(placeLocationX, placeLocationY))) { ++blastRejected; continue; }
                }
                const auto roads = cityRoadImpact(getMap(), placeLocationX, placeLocationY, newSizeX, newSizeY, itemID);
                if (!relaxRoading && !roads.preservesConnections) { ++roadRejected; continue; }
                if (!relaxRoading && currentGame && currentGame->isCitySimEnabled()
                    && wouldLandlockNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) { ++neighbourRejected; continue; }
				int locationScore = 0;
                const int blockBonus = cityZonePlacement ? fourZoneBlockBonus(getMap(),houseID,placeLocationX,placeLocationY) : 0;
                locationScore += roads.junctionBonus + roads.redundantRoadsCovered * 40;
				int placeLocationEndX = placeLocationX + newSizeX;
				int placeLocationEndY = placeLocationY + newSizeY;

		// Big bonus if building is directly at the map edge
		bool atMapEdge = (placeLocationX == 0 || placeLocationX + newSizeX >= getMap().getSizeX() ||
		                  placeLocationY == 0 || placeLocationY + newSizeY >= getMap().getSizeY());
		if (atMapEdge) {
			locationScore += 12;  // Bonus for edge placement
		}

			// Count adjacent friendly structures and track unique buildings per side
			int adjacentFriendlyStructureTiles = 0;
			int oneTileGapFriendlyStructureTiles = 0;
			// Only "does this side touch more than one distinct building" is
			// ever read, so a first id plus a duplicate flag answers it exactly
			// and keeps four allocations out of the hottest AI loop.
			SideBuildings northSideBuildings;  // Buildings touching north side
			SideBuildings southSideBuildings;  // Buildings touching south side
			SideBuildings eastSideBuildings;   // Buildings touching east side
			SideBuildings westSideBuildings;   // Buildings touching west side

			// Evaluate surrounding tiles
			for (int i = placeLocationX - 1; i <= placeLocationEndX; i++) {
				for (int j = placeLocationY - 1; j <= placeLocationEndY; j++) {
					if (getMap().tileExists(i, j) && (getMap().getSizeX() > i) && (0 <= i) && (getMap().getSizeY() > j) && (0 <= j)) {
					if (getMap().getTile(i, j)->hasAStructure()) {
						// Favor being near our buildings, avoid enemy buildings
						if (getMap().getTile(i, j)->getOwner() == getHouse()->getHouseID()) {
							adjacentFriendlyStructureTiles++;
							locationScore += cityZonePlacement ? 0 : 10;  // compact bases only; lots keep frontage instead

							// Track which side this building is on and which building it is
							const ObjectBase* pObject = getMap().getTile(i, j)->getObject();
							if (pObject) {
								Uint32 buildingID = pObject->getObjectID();

								// North side (j == placeLocationY - 1)
								if (j == placeLocationY - 1 && i >= placeLocationX && i < placeLocationEndX) {
									northSideBuildings.insert(buildingID);
								}
								// South side (j == placeLocationEndY)
								if (j == placeLocationEndY && i >= placeLocationX && i < placeLocationEndX) {
									southSideBuildings.insert(buildingID);
								}
								// West side (i == placeLocationX - 1)
								if (i == placeLocationX - 1 && j >= placeLocationY && j < placeLocationEndY) {
									westSideBuildings.insert(buildingID);
								}
								// East side (i == placeLocationEndX)
								if (i == placeLocationEndX && j >= placeLocationY && j < placeLocationEndY) {
									eastSideBuildings.insert(buildingID);
								}
							}
						}
						else {
							locationScore -= 10;
						}
					}
					else if (!getMap().getTile(i, j)->isRock() && !getMap().getTile(i, j)->hasPreparedFoundation()) {
						// Favor non-rock tiles (open buildable terrain)
						locationScore += 4;
					}
				else if (getMap().getTile(i, j)->hasAGroundObject()) {
					if (getMap().getTile(i, j)->getOwner() != getHouse()->getHouseID()) {
						// Avoid building next to enemy units
						locationScore -= 100;
					}
					// No penalty for own units
				}
					}
		// Don't penalize tiles outside map - edge placement should be encouraged
			}
		}

		// A second perimeter identifies structures separated by exactly one tile.
		// This lets some bases form lanes and courtyards without using randomness,
		// which would risk multiplayer lockstep divergence.
		for (int i = placeLocationX - 2; i <= placeLocationEndX + 1; i++) {
			for (int j = placeLocationY - 2; j <= placeLocationEndY + 1; j++) {
				const bool onOuterRing = (i == placeLocationX - 2 || i == placeLocationEndX + 1
					|| j == placeLocationY - 2 || j == placeLocationEndY + 1);
				if (!onOuterRing || !getMap().tileExists(i, j)) {
					continue;
				}
				const Tile* tile = getMap().getTile(i, j);
				if (tile->hasAStructure() && tile->getOwner() == getHouse()->getHouseID()) {
					oneTileGapFriendlyStructureTiles++;
				}
			}
		}

	// Penalty if any single side is touching multiple different buildings (gap-filling)
	int sidesWithMultipleBuildings = 0;
	if (northSideBuildings.touchesMultiple()) sidesWithMultipleBuildings++;
	if (southSideBuildings.touchesMultiple()) sidesWithMultipleBuildings++;
	if (eastSideBuildings.touchesMultiple()) sidesWithMultipleBuildings++;
	if (westSideBuildings.touchesMultiple()) sidesWithMultipleBuildings++;

	if (sidesWithMultipleBuildings > 0) {
		// BAD: At least one side is touching multiple buildings (gap-filling)
		// Penalty should be smaller than benefit of adjacency to discourage but not completely prohibit
		locationScore -= 10 * sidesWithMultipleBuildings;
	}

	// Deterministically vary ordinary base spacing. One third of placements
	// prefer a one-cell lane, one third remain compact, and one third are
	// neutral. Defensive pieces, slabs, and refineries retain their specialist
	// placement behavior.
	const bool supportsVariedSpacing = !(currentGame && currentGame->isCitySimEnabled())
		&& getHouse()->getNumStructures() >= 3
		&& itemID != Structure_GunTurret
		&& itemID != Structure_RocketTurret
		&& itemID != Structure_Wall
		&& itemID != Structure_Slab1
		&& itemID != Structure_Slab4
		&& itemID != Structure_Refinery;
	if (supportsVariedSpacing) {
		const int spacingStyle = (getHouse()->getHouseID() * 37 + itemID * 17
			+ getHouse()->getNumStructures()) % 3;
		if (spacingStyle == 0) {
			locationScore -= adjacentFriendlyStructureTiles * 16;
			locationScore += std::min(oneTileGapFriendlyStructureTiles, 8) * 6;
		} else if (spacingStyle == 2) {
			locationScore -= adjacentFriendlyStructureTiles * 5;
			locationScore += std::min(oneTileGapFriendlyStructureTiles, 4) * 2;
		}
	}

	// Bonus for building on concrete tiles
	for (int i = placeLocationX; i < placeLocationEndX; i++) {
		for (int j = placeLocationY; j < placeLocationEndY; j++) {
			if (getMap().tileExists(i, j) && getMap().getTile(i, j)->hasPreparedFoundation()) {
				locationScore += 2;  // Small bonus - concrete protects from damage
			}
		}
	}

		// Building-specific positioning
		if (itemIsBuilder || factoryPlacement || itemID == Structure_GunTurret || itemID == Structure_RocketTurret) {
            if (!factoryPlacement)
			    locationScore -= lround(blockDistance(squadRallyLocation, Coord(placeLocationX, placeLocationY)));
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		} else if (itemID == Structure_Refinery) {
			// Refineries prefer being close to spice deposits
			int closestSpiceDistance = 10000;
			for (const auto& spiceCoord : spiceTiles) {
				int spiceDistance = lround(blockDistance(Coord(placeLocationX, placeLocationY), spiceCoord));
				if (spiceDistance < closestSpiceDistance) {
					closestSpiceDistance = spiceDistance;
				}
			}
			if (closestSpiceDistance < 10000) {
				locationScore += 50 - closestSpiceDistance * 2; // Strong bonus for being closer to spice
			}

			// Bonus for adjacent sand tiles (harvester access)
			// Double bonus if the sand has spice
			Coord structureSize = getStructureSize(itemID);
			for (int adjX = placeLocationX - 1; adjX <= placeLocationX + structureSize.x; adjX++) {
				for (int adjY = placeLocationY - 1; adjY <= placeLocationY + structureSize.y; adjY++) {
					// Skip tiles inside the structure footprint
					if (adjX >= placeLocationX && adjX < placeLocationX + structureSize.x &&
						adjY >= placeLocationY && adjY < placeLocationY + structureSize.y) {
						continue;
					}
					if (getMap().tileExists(adjX, adjY)) {
						const Tile* pTile = getMap().getTile(adjX, adjY);
						if (pTile->isSand()) {
							if (pTile->hasSpice()) {
								locationScore += 6; // Double bonus for sand with spice
							} else {
								locationScore += 3; // Base bonus for sand
							}
						}
					}
				}
			}

			// Also apply base center distance penalty (but weaker than spice bonus)
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		} else {
			// For other buildings, apply base center distance penalty
			locationScore -= lround(blockDistance(baseCenter, Coord(placeLocationX, placeLocationY)));
		}

		// === CITY MODE: grid alignment + road spacing + zone-type scoring ===
		if (currentGame && currentGame->isCitySimEnabled()) {

			// Grid alignment: snap to a 3-cell grid (2-tile footprint +
			// 1-tile road gap) anchored on the base centre. Positions
			// that land on grid intersections get a massive bonus so the
			// AI naturally builds in neat rows with roads between.
			int gridOffsetX = ((placeLocationX - baseCenter.x) % 3 + 3) % 3;
			int gridOffsetY = ((placeLocationY - baseCenter.y) % 3 + 3) % 3;
            if (cityZonePlacement) {
                locationScore += blockBonus;
                if (CityPlacementPolicy::fourZoneGridSlot(placeLocationX,placeLocationY,baseCenter.x,baseCenter.y)) locationScore += 60;
            } else if (gridOffsetX == 0 && gridOffsetY == 0) {
				locationScore += 40;  // grid alignment bonus
			} else if (cityZonePlacement && alignedWithNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) {
				locationScore += 50;  // continues a row of lots: touching or one road tile apart
			} else {
				locationScore -= 40;  // off-grid penalty
			}

			// Road-spacing: check 4 sides for road / open / structure.
			int sidesWithRoad = 0;
			int sidesWithOpen = 0;
			int sidesTouchingStructure = 0;

			struct SideCheck { int startI, startJ, endI, endJ; };
			SideCheck sides[4] = {
				{ placeLocationX, placeLocationY - 1, placeLocationEndX, placeLocationY },
				{ placeLocationX, placeLocationEndY, placeLocationEndX, placeLocationEndY + 1 },
				{ placeLocationX - 1, placeLocationY, placeLocationX, placeLocationEndY },
				{ placeLocationEndX, placeLocationY, placeLocationEndX + 1, placeLocationEndY }
			};

			for (const auto& side : sides) {
				bool sideHasRoad = false, sideHasOpen = false, sideTouchesStruct = false;
				for (int si = side.startI; si < side.endI; si++) {
					for (int sj = side.startJ; sj < side.endJ; sj++) {
						if (!getMap().tileExists(si, sj)) continue;
						const Tile* t = getMap().getTile(si, sj);
						if (t->isRoad()) sideHasRoad = true;
						else if (t->hasAStructure() || t->hasCityZone()) sideTouchesStruct = true;
						else if (t->isRock() && !t->isMountain() && !t->hasAGroundObject()) sideHasOpen = true;
					}
				}
				if (sideHasRoad) sidesWithRoad++;
				if (sideHasOpen) sidesWithOpen++;
				if (sideTouchesStruct) sidesTouchingStructure++;
			}

			// Road or open rock frontage is what a well-laid-out city wants. The
			// relaxed pass keeps only the access the engine enforces, which
			// preservesGroundAccess() has already checked on this footprint.
			if (!relaxRoading && sidesWithRoad == 0 && sidesWithOpen == 0) {
				continue;  // landlocked — skip
			}
			if (cityZonePlacement && wouldLandlockNeighbouringZone(getMap(), houseID, placeLocationX, placeLocationY, newSizeX, newSizeY)) {
				continue;  // would take a neighbour's last road frontage
			}
			locationScore += sidesWithRoad * 25;
			locationScore += sidesWithOpen * 5;
			locationScore -= sidesTouchingStructure * (cityZonePlacement && blockBonus>0 ? 0 : 30);

			// Zone-type proximity scoring:
			// R/C avoid industrial pollution (radius 5) but want it
			// within supply range (16). I clusters with itself and
			// wants residential nearby for workers.
			bool isResidential = (itemID == Structure_ZoneResidential);
			bool isCommercial  = (itemID == Structure_ZoneCommercial);
			bool isIndustrial  = (itemID == Structure_ZoneIndustrial);

			if (isResidential || isCommercial || isIndustrial) {
				// Favour lots that use sand so rock stays free for Dune
				// structures. The placement check already guarantees at
				// least one rock tile under the lot.
				for (int px = placeLocationX; px < placeLocationEndX; px++) {
					for (int py = placeLocationY; py < placeLocationEndY; py++) {
						if (getMap().tileExists(px, py)) {
							const Tile* t = getMap().getTile(px, py);
							if (t->isSand() || t->isDunes()) locationScore += 6;
						}
					}
				}

                int closestIndDist = 100, closestResDist = 100, closestComDist = 100;
                int nearbyRes = 0, nearbyCom = 0, nearbyInd = 0;
                zoneIndex.visit(placeLocationX,placeLocationY,16,[&](size_t index) {
                    const auto& zone=zoneNeighbours[index];
                    const int dist=lround(blockDistance(Coord(placeLocationX,placeLocationY),zone.location));
                    if (dist>16) return;
                    if (zone.item==Structure_ZoneIndustrial) { ++nearbyInd; closestIndDist=std::min(closestIndDist,dist); }
                    if (zone.item==Structure_ZoneResidential) { ++nearbyRes; closestResDist=std::min(closestResDist,dist); }
                    if (zone.item==Structure_ZoneCommercial) { ++nearbyCom; closestComDist=std::min(closestComDist,dist); }
                });

				if (isResidential || isCommercial) {
					auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;

					// Penalty if within pollution radius of industrial
					if (closestIndDist <= 5) {
						locationScore -= 50;
					}
					// Bonus if industrial is reachable but outside pollution
					else if (closestIndDist <= 16) {
						locationScore += 20;
					}

					// Pollution density penalty (city sim layer)
					if (citySim) {
						const auto& polMap = citySim->getPollutionDensityMap();
						int totalPollution = 0;
						for (int px = placeLocationX; px < placeLocationEndX; px++) {
							for (int py = placeLocationY; py < placeLocationEndY; py++) {
								totalPollution += polMap.worldGet(px, py);
							}
						}
						locationScore -= totalPollution / 10;
					}

					// Crime rate penalty (city sim layer)
					if (citySim) {
						const auto& crimeMap = citySim->getCrimeRateMap();
						int totalCrime = 0;
						for (int px = placeLocationX; px < placeLocationEndX; px++) {
							for (int py = placeLocationY; py < placeLocationEndY; py++) {
								totalCrime += crimeMap.worldGet(px, py);
							}
						}
						locationScore -= totalCrime / 5;
					}

					// Sand adjacency bonus — higher land value near sand/desert
					Coord zoneSize = getStructureSize(itemID);
					int sandBonus = 0;
					for (int adjX = placeLocationX - 1; adjX <= placeLocationX + zoneSize.x; adjX++) {
						for (int adjY = placeLocationY - 1; adjY <= placeLocationY + zoneSize.y; adjY++) {
							if (adjX >= placeLocationX && adjX < placeLocationX + zoneSize.x &&
								adjY >= placeLocationY && adjY < placeLocationY + zoneSize.y)
								continue;
							if (getMap().tileExists(adjX, adjY) && getMap().getTile(adjX, adjY)->isSand())
								sandBonus += 5;
						}
					}
					locationScore += sandBonus;

					if (isResidential) {
						// Employment access: bonus if C or I zones reachable
						if (nearbyCom > 0 || nearbyInd > 0) locationScore += 20;
						// Extra for having both (mixed economy nearby)
						if (nearbyCom > 0 && nearbyInd > 0) locationScore += 10;
						// R ↔ C synergy
						if (nearbyCom > 0) locationScore += 15;
					}

					if (isCommercial) {
						// Commercial wants both R (customers) and I (supply) nearby
						if (nearbyRes > 0) locationScore += 20;
						if (nearbyInd > 0) locationScore += 15;
						// Strong bonus for being between R and I
						if (nearbyRes > 0 && nearbyInd > 0) locationScore += 15;
					}
				}

				if (isIndustrial) {
					// I clusters with other I (pollution doesn't affect I)
					locationScore += nearbyInd * 10;

					// I should stay away from R/C to avoid polluting them
					// but within commute distance (6-16 tiles = sweet spot)
                    // All following distance decisions use only the 6/16 thresholds;
                    // a missing neighbour within 16 has the same score as any
                    // more distant neighbour, so reuse the bounded query above.

					// Sweet spot: outside pollution radius but within commute
					if (closestResDist >= 6 && closestResDist <= 16) {
						locationScore += 25;
					} else if (closestResDist < 6) {
						locationScore -= 30;  // too close — will pollute residential
					} else if (closestResDist > 16) {
						locationScore -= 10;  // too far — no workers
					}

					if (closestComDist >= 6 && closestComDist <= 16) {
						locationScore += 15;
					} else if (closestComDist < 6) {
						locationScore -= 20;  // too close to commercial
					}
				}
			}
		}

                int siteTier = 0;
                // Plain numbers per candidate; the telemetry record is written once, for the
                // chosen site, after the search (it used to be built as JSON for every candidate).
                SiteQuality quality;
                quality.blockBonus = blockBonus;
                if (needCityDistances) {
                    prepareCityDistances();
                    const int separation=pollutionSeparation.footprint(placeLocationX,placeLocationY,newSizeX,newSizeY);
                    const int nearestR=nearestResidential.get(placeLocationX,placeLocationY);
                    const int nearestC=nearestCommercial.get(placeLocationX,placeLocationY);
                    const int nearestI=nearestIndustrial.get(placeLocationX,placeLocationY);
                    auto inReachOrMissing = [](int distance) { return distance == 1000000 || distance <= DuneCity::kSupplyRadius; };
                    const bool withinSupply = newRole == DuneCity::CityRole::Residential
                        ? inReachOrMissing(std::min(nearestC, nearestI))
                        : newRole == DuneCity::CityRole::Commercial
                            ? inReachOrMissing(nearestR) && inReachOrMissing(nearestI)
                            : inReachOrMissing(nearestR);
                    siteTier = CityPlacementPolicy::cityPlacementTier(withinSupply, separation);
                    quality.city = true;
                    quality.supply = withinSupply;
                    quality.separation = separation == 1000000 ? -1 : separation;
                    quality.nearestR = nearestR == 1000000 ? -1 : nearestR;
                    quality.nearestC = nearestC == 1000000 ? -1 : nearestC;
                    quality.nearestI = nearestI == 1000000 ? -1 : nearestI;
                    locationScore += CityPlacementPolicy::pollutionSeparationScore(separation);
                    if (newSensitive) {
                        int pollution = 0, value = 0, traffic = 0, sand = 0;
                        for (int x = placeLocationX; x < placeLocationX+newSizeX; ++x)
                            for (int y = placeLocationY; y < placeLocationY+newSizeY; ++y) {
                                pollution += citySim->getPollutionDensityMap().worldGet(x, y);
                                value += citySim->getLandValueMap().worldGet(x, y);
                                traffic += citySim->getTrafficDensityMap().worldGet(x, y);
                            }
                        for (int x = placeLocationX-1; x <= placeLocationX+newSizeX; ++x)
                            for (int y = placeLocationY-1; y <= placeLocationY+newSizeY; ++y) {
                                if (x >= placeLocationX && x < placeLocationX+newSizeX
                                    && y >= placeLocationY && y < placeLocationY+newSizeY) continue;
                                if (!getMap().tileExists(x,y)) continue;
                                const auto* tile = getMap().getTile(x,y);
                                if (!tile->hasAStructure() && (tile->isSand() || tile->isDunes())) ++sand;
                            }
                        const int meanPollution = pollution/(newSizeX*newSizeY);
                        const int meanTraffic = traffic/(newSizeX*newSizeY);
                        siteTier = CityPlacementPolicy::sensitivePlacementTier(withinSupply, separation,
                            meanPollution, meanTraffic);
                        quality.sensitive = true;
                        quality.meanPollution = meanPollution; quality.meanTraffic = meanTraffic;
                        quality.meanLandValue = value/(newSizeX*newSizeY); quality.sand = sand;
                        locationScore += CityPlacementPolicy::residentialCommercialEnvironmentScore(
                            meanPollution, value/(newSizeX*newSizeY), sand, meanTraffic);
                    }
                }

                const int infill = cityZonePlacement && itemID==Structure_ZoneResidential
                    ? residentialInfillSides(getMap(),houseID,placeLocationX,placeLocationY,newSizeX,newSizeY) : 0;
                quality.infill = infill;
                const int lossRisk = dangerAt(Coord(placeLocationX, placeLocationY), Coord(newSizeX, newSizeY), true);
                // Safety outranks pollution/grid preferences; losses decay over five minutes.
                siteTier += lossRisk == 0 ? 6 : 0;
                locationScore -= lossRisk * 5;
                const int rear = (itemID == Structure_NuclearPlant || factoryPlacement) ? rearScore(Coord(placeLocationX, placeLocationY), baseCenter) : 0;
                locationScore += rear;
                const int enemyClearance = (factoryPlacement || itemID == Structure_NuclearPlant) ? TacticalSafetyPolicy::footprintClearance(
                    factoryClearance(),mapW,mapH,placeLocationX,placeLocationY,newSizeX,newSizeY) : 0;
                const auto factoryRank = TacticalSafetyPolicy::factorySiteRank(lossRisk,enemyClearance,siteTier,locationScore);
                const bool clearsReactor = itemID != Structure_NuclearPlant || clearsReactorAt(placeLocationX, placeLocationY);
                const int fireRisk = itemID == Structure_NuclearPlant ? dangerAt(Coord(placeLocationX,placeLocationY),Coord(newSizeX,newSizeY)) : 0;
                const auto reactorRank = TacticalSafetyPolicy::reactorSiteRank(fireRisk,lossRisk,enemyClearance,clearsReactor,rear,locationScore);
                quality.enemyClearance = enemyClearance; quality.lossRisk = lossRisk; quality.fireRisk = fireRisk;
                quality.rear = rear; quality.clearsReactor = clearsReactor;

				// Pick this location if it has the best score
				if (itemID == Structure_NuclearPlant ? reactorRank > bestReactorRank
                    : factoryPlacement ? factoryRank > bestFactoryRank
                    : CityPlacementPolicy::preferCitySite(lossRisk==0,infill,siteTier,locationScore,
                        bestSafe,bestInfill,bestSiteTier,bestLocationScore)) {
                    bestReactorRank = reactorRank;
                    bestFactoryRank = factoryRank;
                    bestSiteTier = siteTier;
                    bestSafe=lossRisk==0; bestInfill=infill;
                    roadingRelaxed = relaxRoading;
                    quality.tier = siteTier; quality.score = locationScore;
                    bestSite = quality; haveBestSite = true;
					bestLocationScore = locationScore;
					bestLocation = Coord(placeLocationX, placeLocationY);
				}
			}
		}
	}

    } // search passes
    if (haveBestSite) bestQuality = siteQualityRecord(bestSite);
    if (bestLocation.isInvalid()) {
        bestLocation = findRedevelopmentSite(itemID);
        if (bestLocation.isValid()) { bestQuality.set("redevelopment",1); roadingRelaxed=false; }
    }
	placementCache[itemID] = bestLocation;
    bestQuality.set("roading_relaxed",roadingRelaxed)
        .set("legal_candidates",candidates).set("threat_rejections",threatRejected)
        .set("blast_rejections",blastRejected).set("recent_loss_rejections",lossRejected)
        .set("search_pass",searchPassUsed).set("search_center_x",baseCenter.x).set("search_center_y",baseCenter.y)
        .set("production_plot_rejections",productionPlotRejected)
        .set("reserved_rejections",reservedRejected).set("road_rejections",roadRejected).set("neighbour_rejections",neighbourRejected)
        .set("ground_access_rejections",accessRejected).set("pollution_rejections",pollutionRejected)
        .set("build_range_skipped",static_cast<int>(buildRangeSkipped));
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "ai.placement.build_range_skipped",static_cast<int64_t>(buildRangeSkipped),
        static_cast<int>(itemID),false);
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "ai.anchor.field_builds",static_cast<int64_t>(anchorFieldCache.builds),-1,false);
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),
        "ai.anchor.field_hits",static_cast<int64_t>(anchorFieldCache.hits),-1,false);
    anchorFieldCache.builds = anchorFieldCache.hits = 0;
    placementScoreDetails[itemID] = bestQuality;
	return bestLocation;
}

Coord QuantBot::findSlabPlaceLocation(Uint32 itemID) {
	int slabSizeX = getStructureSize(itemID).x;
	int slabSizeY = getStructureSize(itemID).y;

	int bestLocationScore = -10000;
	Coord bestLocation = Coord::Invalid();

	// Check all map tiles for valid slab placement
	for (int x = 0; x <= getMap().getSizeX() - slabSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - slabSizeY; y++) {
			// Check if this location is valid for slab placement
			if (getMap().okayToPlaceStructure(x, y, slabSizeX, slabSizeY, false, getHouse())) {

				int locationScore = 0;
				bool hasExistingSlab = false;

				// Check if any of the slab tiles already have concrete
				for (int i = x; i < x + slabSizeX; i++) {
					for (int j = y; j < y + slabSizeY; j++) {
						if (getMap().getTile(i, j)->hasPreparedFoundation()) {
							hasExistingSlab = true;
							break;
						}
					}
					if (hasExistingSlab) break;
				}

				// Skip if already has concrete - we don't want to place over existing slabs
				if (hasExistingSlab) {
					continue;
				}

			// Count adjacent tiles - favor building next to existing buildings or concrete
			int adjacentStructureTiles = 0;
			int adjacentConcreteTiles = 0;
			int adjacentRockTiles = 0;

			for (int i = x - 1; i <= x + slabSizeX; i++) {
				for (int j = y - 1; j <= y + slabSizeY; j++) {
					if (getMap().tileExists(i, j)) {
						const Tile* pTile = getMap().getTile(i, j);

						// Check if this is directly adjacent (edge-touching, not diagonal)
						bool isDirectlyAdjacent = ((i == x - 1 || i == x + slabSizeX) && j >= y && j < y + slabSizeY) ||
						                          ((j == y - 1 || j == y + slabSizeY) && i >= x && i < x + slabSizeX);

						if (isDirectlyAdjacent) {
							// Count structures that are directly adjacent (highest priority)
							if (pTile->hasAStructure() && pTile->getOwner() == getHouse()->getHouseID()) {
								adjacentStructureTiles++;
							}
							// Count concrete tiles that are directly adjacent (second priority)
							else if (pTile->hasPreparedFoundation()) {
							adjacentConcreteTiles++;
						}
							// Count rock tiles that are directly adjacent (room to expand)
							else if (pTile->isRock() && !pTile->isMountain()) {
							adjacentRockTiles++;
							}
						}
					}
				}
			}

		// SCORING: Favor building next to existing buildings or concrete
		// 1. Highest priority: directly adjacent to our structures
		locationScore += adjacentStructureTiles * 10;

		// 2. Second priority: directly adjacent to existing concrete
		locationScore += adjacentConcreteTiles * 5;

		// 3. Bonus for adjacent rock (room to expand)
		locationScore += adjacentRockTiles * 2;


				// Pick this location if it has the best score
				if (locationScore > bestLocationScore) {
					bestLocationScore = locationScore;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

	return bestLocation;
}

// A battery belongs to ONE colony. The average of every structure sits in the
// sand between two colonies, so the belt would form nowhere useful and its
// "enemy side" would be meaningless. Anchor it on the yard whose own colony is
// building this turret - the planning builder - so each base gets its own
// directional belt, and fall back to the shelter cluster and then the centre.
Coord QuantBot::batteryAnchor() {
    Coord anchor = findBaseCentre(getHouse()->getHouseID());
    Coord colony = Coord::Invalid();
    if (const auto* owner = dynamic_cast<const BuilderBase*>(
            currentGame->getObjectManager().getObject(planningBuilder)))
        if (owner->getOwner() == getHouse() && owner->getHealth() > 0)
            colony = owner->getLocation();
    if (colony.isInvalid()) colony = shelterAnchor();
    if (colony.isValid()) anchor = colony;
    return anchor;
}

// Collected once per search, not per candidate, and shared by the free-ground
// search and the clearance fallback so the two cannot drift apart.
QuantBot::BatteryGeometry QuantBot::batteryGeometry(Coord anchor, Coord forward) {
    BatteryGeometry geometry;
    geometry.anchor = anchor;
    geometry.forward = forward;
    const int turretRange = std::max(1,
        currentGame->objectData.data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange);
    geometry.coverRadius = std::max(1, turretRange - 1);
    geometry.clusterRadius = FrontBatteryPolicy::clusterRadius(turretRange);
    const int colonyRadius = 2 * turretRange;
    for (const StructureBase* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && (structure->getItemID() == Structure_RocketTurret
                || structure->getItemID() == Structure_GunTurret))
            geometry.turrets.push_back(structure->getLocation());
    for (const auto& entry : reservedStructures) {
        // A yard's own reservation is the emplacement it is planning, not a
        // neighbour it has to keep clear of - the same exclusion
        // overlapsReservedStructure() makes. Without it a yard re-examining
        // its own reserved site measures a distance of zero to itself and
        // rejects the plan it already made.
        if (entry.first == planningBuilder) continue;
        if (entry.second.item == Structure_RocketTurret || entry.second.item == Structure_GunTurret)
            geometry.turrets.push_back(entry.second.location);
    }
    for (const Coord turret : geometry.turrets) {
        if (blockDistance(turret, anchor).lround() > colonyRadius) continue;
        ++geometry.localTurrets;
        if (FrontBatteryPolicy::onFrontSide(turret.x - anchor.x, turret.y - anchor.y,
                forward.x, forward.y)) ++geometry.frontTurrets;
    }
    // Front allowance is recomputed from the live goal rather than stored, so a
    // destroyed battery immediately frees its share again.
    geometry.frontAllowance = FrontBatteryPolicy::frontAllowance(frontBatteryGoal(
        RocketTurretPolicy::coverageTurretCap([&] {
            int demand = 0;
            const Uint32 anchorYard = mainConstructionYardID();
            for (const auto* structure : getStructureList())
                if (structure->getOwner() == getHouse() && structure->getHealth() > 0)
                    demand += RocketTurretPolicy::desiredCoverage(structure->getItemID(),
                        rocketCoverageTier(difficulty),
                        isExpansionYard(structure->getItemID(), structure->getObjectID(), anchorYard));
            return demand;
        }()),
        getHouse()->getNumItems(Structure_Refinery),
        getHouse()->getNumItems(Structure_HeavyFactory),
        getHouse()->getNumItems(Structure_RepairYard)));
    return geometry;
}

Coord QuantBot::findTurretPlaceLocation(Uint32 itemID) {
    AITelemetry::PerformanceScope perfScope("ai.findTurretPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
    // One search per item per build pass. Several rules call this purely to ask
    // whether a legal site exists, and the map and reservations cannot change
    // between those calls; clearPlacementCache() retires the entry the moment
    // they do. This returns exactly what a repeated search would have returned.
    if (const auto cached = turretSiteCache.find(itemID); cached != turretSiteCache.end()) {
        AITelemetry::log().performance(getGameCycleCount(), getHouse()->getHouseID(),
            "turret.site_cache_hit", 1, itemID, false);
        return cached->second;
    }
	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();
	Coord baseCenter = findBaseCentre(getHouse()->getHouseID());

    // Enemy-facing batteries use the direction this house has actually observed:
    // visible enemy bases first, then the direction its own buildings have been
    // lost in. The established behaviour elsewhere keeps the squad rally proxy,
    // which is derived from where the workers are standing and is therefore not
    // a front at all.
    const bool battery = recoveryActive() && itemID == Structure_RocketTurret
        && botConfig().recovery.frontBatteriesEnabled && baseCenter.isValid();
    if (battery) baseCenter = batteryAnchor();
    Coord forward(0, 0);
    if (battery) forward = observedFrontDirection(baseCenter);
    Coord enemyDirection = Coord::Invalid();
    if (battery && (forward.x != 0 || forward.y != 0)) enemyDirection = baseCenter + forward;
    else enemyDirection = squadRallyLocation.isValid() ? squadRallyLocation : Coord::Invalid();

	// If no squad rally, find closest enemy structure
	if (!enemyDirection.isValid()) {
		FixPoint closestEnemyDistance = FixPt_MAX;
		for (const StructureBase* pStructure : getStructureList()) {
			if (pStructure && pStructure->getOwner() && pStructure->getOwner()->getTeamID() != getHouse()->getTeamID()) {
				FixPoint distance = blockDistance(baseCenter, pStructure->getLocation());
				if (distance < closestEnemyDistance) {
					closestEnemyDistance = distance;
					enemyDirection = pStructure->getLocation();
				}
			}
		}
	}

    // Battery geometry. Collected once, not per candidate: existing and already
    // reserved emplacements, so the belt overlaps without stacking and without
    // sealing a lane.
    const BatteryGeometry geometry = battery ? batteryGeometry(baseCenter, forward) : BatteryGeometry{};
    const std::vector<Coord>& ownTurrets = geometry.turrets;
    const int frontTurrets = geometry.frontTurrets;
    const int localTurrets = geometry.localTurrets;
    const int turretRange = std::max(1,
        currentGame->objectData.data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange);
    const int coverRadius = std::max(1, turretRange - 1);
    const int clusterRadius = FrontBatteryPolicy::clusterRadius(turretRange);
    const int frontAllowance = geometry.frontAllowance;

	FixPoint bestScore = -FixPt_MAX;
	Coord bestLocation = Coord::Invalid();

	// Check every tile on the map for valid placement
	for (int x = 0; x <= getMap().getSizeX() - newSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - newSizeY; y++) {
			// First check if this location is valid for building (own units are traffic)
			if (placeableAmongOwnUnits(x, y, newSizeX, newSizeY, itemID)) {

                if (overlapsReservedStructure(x, y, newSizeX, newSizeY)
                    || !preservesGroundAccess(itemID,Coord(x,y))) continue;
                const auto roads = cityRoadImpact(getMap(), x, y, newSizeX, newSizeY, itemID);
                if (!roads.preservesConnections) continue;
                if (battery) {
                    // In city mode an emplacement must not strand a zone, the
                    // same rule the city coverage planner applies. The battery
                    // has its own search, so it carries the rule itself rather
                    // than inheriting it.
                    if (currentGame->isCitySimEnabled()
                        && wouldLandlockNeighbouringZone(getMap(), getHouse()->getHouseID(),
                               x, y, newSizeX, newSizeY)) continue;
                    // Spacing, local density, mutual support and a movement
                    // corridor. GroundAccessPolicy already protects local
                    // connectivity and factory exits, and it deliberately
                    // tolerates a diagonal-only gap for turrets; the corridor
                    // margin stops a belt from reducing every lane to that.
                    int nearest = -1, inCluster = 0;
                    bool covered = false;
                    for (const Coord turret : ownTurrets) {
                        const int d = std::max(std::abs(turret.x - x), std::abs(turret.y - y));
                        if (nearest < 0 || d < nearest) nearest = d;
                        if (d <= clusterRadius) ++inCluster;
                        if (d <= coverRadius) covered = true;
                    }
                    if (!FrontBatteryPolicy::spacedEnough(nearest)) continue;
                    if (!FrontBatteryPolicy::withinClusterLimit(inCluster)) continue;
                    if (!FrontBatteryPolicy::mutuallySupported(localTurrets, covered)) continue;
                    int passable = 0;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        if (!getMap().tileExists(x + dx, y + dy)) continue;
                        const Tile* tile = getMap().getTile(x + dx, y + dy);
                        if (!tile->isMountain() && !tile->hasAStructure()) ++passable;
                    }
                    if (!FrontBatteryPolicy::keepsCorridor(passable)) continue;
                    // Keep a share of the goal for the flank and the rear, so a
                    // second front is not opened by a one-sided belt.
                    if (frontAllowance > 0 && frontTurrets >= frontAllowance
                        && FrontBatteryPolicy::onFrontSide(x - baseCenter.x, y - baseCenter.y,
                               forward.x, forward.y)) continue;
                }
				FixPoint score = 0;
                score += roads.junctionBonus + roads.redundantRoadsCovered * 40;
				Coord candidatePos(x, y);

				// 1. Favor being CLOSE to base center (integrated into base, not perimeter)
				FixPoint distanceFromBase = blockDistance(candidatePos, baseCenter);
				score -= distanceFromBase * 2; // Penalty for being far from center

				// 2. Strong bonus for adjacency to own buildings
				int adjacentOwnBuildings = 0;
				for (int dx = -1; dx <= newSizeX; dx++) {
					for (int dy = -1; dy <= newSizeY; dy++) {
						// Check tiles around the structure
						if ((dx == -1 || dx == newSizeX || dy == -1 || dy == newSizeY) && 
							getMap().tileExists(x + dx, y + dy)) {
							const Tile* pTile = getMap().getTile(x + dx, y + dy);
							if (pTile->hasAStructure()) {
								const StructureBase* pStructure = dynamic_cast<const StructureBase*>(pTile->getObject());
								if (pStructure && pStructure->getOwner() == getHouse()) {
									adjacentOwnBuildings++;
								}
							}
						}
					}
				}
				score += adjacentOwnBuildings * 15; // Strong bonus for being next to own buildings

				// 3. Favor the side of the base closest to the enemy
				// We want turrets between our base and the enemy
				if (enemyDirection.isValid() && baseCenter.isValid()) {
					// Calculate vector from base to enemy
					int baseToEnemyX = enemyDirection.x - baseCenter.x;
					int baseToEnemyY = enemyDirection.y - baseCenter.y;

					// Calculate vector from base to candidate position
					int baseToCandidateX = candidatePos.x - baseCenter.x;
					int baseToCandidateY = candidatePos.y - baseCenter.y;

					// Dot product: positive if candidate is on the enemy side of base
					int dotProduct = baseToEnemyX * baseToCandidateX + baseToEnemyY * baseToCandidateY;
					if (dotProduct > 0) {
						score += dotProduct / 10; // Bonus for being on enemy-facing side
					}
				}

				// 4. Slight preference for sand over rock (buildable terrain)
				int sandTiles = 0;
				for (int dx = 0; dx < newSizeX; dx++) {
					for (int dy = 0; dy < newSizeY; dy++) {
						if (getMap().tileExists(x + dx, y + dy)) {
							const Tile* pTile = getMap().getTile(x + dx, y + dy);
							if (!pTile->isRock()) {
								sandTiles++;
							}
						}
					}
				}
				score += sandTiles * 2; // Minor bonus for sand

                // 5. A battery belongs on the enemy-facing edge of the base, not
                //    buried in its most built-up corner. Eight adjacent
                //    buildings are already worth 120 points here, so the
                //    directional term is deliberately weighted above that: it
                //    decides the side, and adjacency and compactness then decide
                //    where on that side. The flank/rear allowance above is what
                //    stops this from putting the whole belt on one side.
                if (battery && (forward.x != 0 || forward.y != 0)) {
                    const int scale = std::max(1, std::max(std::abs(forward.x), std::abs(forward.y)));
                    const int along = (forward.x * (x - baseCenter.x) + forward.y * (y - baseCenter.y)) / scale;
                    score += std::clamp(along, -20, 20) * 14;
                }

				// Check if this is the best location so far
				if (score > bestScore) {
					bestScore = score;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

    turretSiteCache[itemID] = bestLocation;
    if (battery) traceDecision("front_battery_site", AITelemetry::Record()
        .set("x", bestLocation.x).set("y", bestLocation.y)
        .set("valid", bestLocation.isValid())
        .set("forward_x", forward.x).set("forward_y", forward.y)
        .set("own_turrets", int(ownTurrets.size())).set("front_turrets", frontTurrets)
        .set("front_allowance", frontAllowance)
        .set("min_spacing", FrontBatteryPolicy::kMinSpacing)
        .set("cluster_radius", clusterRadius).set("max_cluster", FrontBatteryPolicy::kMaxCluster));
	return bestLocation;
}

// Threat turrets on the base perimeter (U, on by default, docs/formations.md "Round 5"): the band
// around the outermost buildings on the observed enemy side (ForceConcentrationPolicy::
// onPerimeterBand), a bounded box around it, the established legality checks and the battery's
// spacing and corridor rules. Best: the most room from other turrets (a line, not a block),
// then nearest the front line through the base centre, then nearest the outermost building;
// ties go to the first tile in row order.
Coord QuantBot::findPerimeterTurretLocation() {
    AITelemetry::PerformanceScope perfScope("ai.findPerimeterTurretLocation", getGameCycleCount(), getHouse()->getHouseID());
    namespace Force = ForceConcentrationPolicy;
    const Perimeter& p = perimeter();
    if (p.length <= 0) return Coord::Invalid();
    std::vector<Coord> turrets;
    for (const StructureBase* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && (structure->getItemID() == Structure_RocketTurret || structure->getItemID() == Structure_GunTurret))
            turrets.push_back(structure->getLocation());
    for (const auto& entry : reservedStructures)
        if (entry.first != planningBuilder
            && (entry.second.item == Structure_RocketTurret || entry.second.item == Structure_GunTurret))
            turrets.push_back(entry.second.location);
    const Coord centre(p.anchor.x + int(int64_t(p.forward.x) * p.depth / p.length),
                       p.anchor.y + int(int64_t(p.forward.y) * p.depth / p.length));
    const int box = Force::kPerimeterHalfWidth + Force::kPerimeterInnerTiles;
    int bestScore = std::numeric_limits<int>::min();
    Coord best = Coord::Invalid();
    for (int y = std::max(0, centre.y - box); y <= std::min(getMap().getSizeY() - 1, centre.y + box); ++y) {
        for (int x = std::max(0, centre.x - box); x <= std::min(getMap().getSizeX() - 1, centre.x + box); ++x) {
            const int dx = x - p.anchor.x, dy = y - p.anchor.y;
            const int along = int((int64_t(dx) * p.forward.x + int64_t(dy) * p.forward.y) / p.length);
            const int across = int((int64_t(dy) * p.forward.x - int64_t(dx) * p.forward.y) / p.length);
            if (!Force::onPerimeterBand(along, across, p.depth)) continue;
            if (!placeableAmongOwnUnits(x, y, 1, 1, Structure_RocketTurret)) continue;
            if (overlapsReservedStructure(x, y, 1, 1) || !preservesGroundAccess(Structure_RocketTurret, Coord(x, y))) continue;
            if (!cityRoadImpact(getMap(), x, y, 1, 1, Structure_RocketTurret).preservesConnections) continue;
            if (currentGame->isCitySimEnabled()
                && wouldLandlockNeighbouringZone(getMap(), getHouse()->getHouseID(), x, y, 1, 1)) continue;
            int nearest = -1;
            for (const Coord turret : turrets) {
                const int d = std::max(std::abs(turret.x - x), std::abs(turret.y - y));
                if (nearest < 0 || d < nearest) nearest = d;
            }
            if (!FrontBatteryPolicy::spacedEnough(nearest)) continue;
            int passable = 0;
            for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                if ((!ox && !oy) || !getMap().tileExists(x + ox, y + oy)) continue;
                const Tile* tile = getMap().getTile(x + ox, y + oy);
                if (!tile->isMountain() && !tile->hasAStructure()) ++passable;
            }
            if (!FrontBatteryPolicy::keepsCorridor(passable)) continue;
            const int room = nearest < 0 ? 5 : std::min(nearest, 5);
            const int score = room * 100 - std::abs(across) * 4 - std::abs(along - p.depth) * 10;
            if (score > bestScore) { bestScore = score; best = Coord(x, y); }
        }
    }
    traceDecision("perimeter_turret_site", AITelemetry::Record().set("x", best.x).set("y", best.y)
        .set("valid", best.isValid()).set("depth", p.depth).set("forward_x", p.forward.x).set("forward_y", p.forward.y)
        .set("turrets", int(turrets.size())));
    return best;
}

bool QuantBot::selectCityServiceInvestment(const BuilderBase* builder, int money, bool emergency,
                                         Uint32& selectedItem, Coord& selectedSite, bool landValueOnly, Uint32 requiredItem) {
    AITelemetry::PerformanceScope perfScope("ai.selectCityServiceInvestment", getGameCycleCount(), getHouse()->getHouseID());
    using CityServiceInvestmentPolicy::Value;
    const auto* sim = currentGame->getCitySimulation();
    if (!sim || !sim->isInitialized()) return false;
    const int house = getHouse()->getHouseID(), w = getMap().getSizeX(), h = getMap().getSizeY();
    const auto& prices = currentGame->objectData.data;
    unsigned available = 0;
    const std::array<Uint32,2> serviceItems{Structure_PoliceStation,Structure_RocketTurret};
    bool coreReady=true;
    if (gameMode==GameMode::Custom && !supportMode)
    for (Uint32 item:{Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard})
        if (prices[item][house].enabled && prices[item][house].techLevel<=currentGame->techLevel
            && getHouse()->getNumItems(item)==0) coreReady=false;
    const bool rocketOpeningReady=coreReady || getHouse()->getNumItems(Structure_RocketTurret)<2;
    for (unsigned i=0;i<serviceItems.size();++i)
        if ((serviceItems[i]!=Structure_RocketTurret || rocketOpeningReady)
            && (requiredItem == NONE_ID || requiredItem == serviceItems[i])
            && campaignAvailableToBuild(builder,serviceItems[i]) && money >= prices[serviceItems[i]][house].price)
            available |= 1u << i;
    if (!available || (landValueOnly && !(available & 2))) return false;
    const unsigned mode = landValueOnly ? 2 : emergency ? 1 : 0;
    // A yard with its own reservation excludes that plan from marginal gains.
    const Uint32 key = reservedStructures.count(planningBuilder) ? planningBuilder : NONE_ID;
    auto selectResult = [&](const CityServiceResults& results) {
        const CityServiceSite* best = nullptr;
        for (unsigned i=0;i<serviceItems.size();++i) {
            const auto& candidate = results[mode][i];
            if (!(available & (1u<<i)) || candidate.site.isInvalid()) continue;
            // Routine coverage belongs to multipurpose turrets. Buy the
            // stronger station when occupied buildings face dangerous crime.
            if (serviceItems[i]==Structure_PoliceStation) {
                if (candidate.value.dangerousRelief<32) continue;
                const auto& hs=sim->getHouseState(house);
                const bool stationCommitted=getHouse()->getNumItems(Structure_PoliceStation)>0
                    || std::any_of(reservedStructures.begin(),reservedStructures.end(),[](const auto& plan) {
                        return plan.second.item==Structure_PoliceStation;
                    });
                // A lower service budget must not trigger a construction loop
                // that buys replacement coverage with still more permanent bills.
                // Grow income before adding another station to an underfunded city.
                if (gameMode==GameMode::Custom && !supportMode && stationCommitted
                    && hs.policeFundingPercent<100) continue;
                const int annualTax=DuneCity::computeAnnualTaxRevenue(hs.taxBaseEighths,sim->getCityTax(),hs.avgLandValue);
                const int annualPower=getHouse()->isPowerRequired() ? getHouse()->getPowerRequirement()/8 : 0;
                int committedCost=hs.nominalPoliceCost;
                for (const auto& plan:reservedStructures)
                    if (plan.first!=planningBuilder)
                        committedCost+=DuneCity::getPoliceAnnualCost(plan.second.item).lround();
                const bool overBudget=gameMode==GameMode::Custom && !supportMode
                    && committedCost+DuneCity::getPoliceAnnualCost(Structure_PoliceStation).lround()
                    > CityServiceInvestmentPolicy::policingAllowance(annualTax,annualPower,
                        CityServiceInvestmentPolicy::policingBudgetPercent(getHouse()->getCredits(),annualTax,annualPower,committedCost));
                // Prefer cheaper useful relief before accepting another permanent
                // station bill. Accept an otherwise unaffordable full-strength
                // station only for danger a turret cannot treat; its actual
                // funding still remains under the recurring budget limit.
                const auto& rocket=results[mode][1];
                const bool rocketTreatsDanger=(available&2) && rocket.site.isValid()
                    && rocket.value.dangerousRelief>=32;
                if (overBudget && (!emergency || rocketTreatsDanger)) continue;
            }
            if (!best || candidate.value.betterThan(best->value)) {
                best = &candidate;
                selectedItem = serviceItems[i];
            }
        }
        if (!best) return false;
        selectedSite = best->site;
        const auto& value = best->value;
        traceDecision("city_service_investment", AITelemetry::Record().set("builder",builder->getObjectID())
            .set("item",selectedItem).set("x",selectedSite.x).set("y",selectedSite.y).set("emergency",emergency)
            .set("crime_reduction",value.crime).set("annual_tax_gain",value.tax)
            .set("crime_utility",value.crimeUtility).set("dangerous_relief",value.dangerousRelief)
            .set("pre_outbreak_relief",value.preOutbreakRelief)
            .set("estimated_growth_tax",value.growthTax).set("threat_defense_value",value.defense)
            .set("build_cost",value.buildCost).set("annual_upkeep",value.upkeep)
            .set("power_cost",value.powerCost).set("placement_overlap_penalty",value.overlapPenalty)
            .set("horizon_years",1));
        return true;
    };
    if (const auto* cached = cityServiceSearch.get(key)) {
        AITelemetry::log().performance(getGameCycleCount(),house,"service.cache_hit",1,-1,false);
        return selectResult(*cached);
    }
    if (!cityServiceSearch.start(key)) {
        AITelemetry::log().performance(getGameCycleCount(),house,"service.search_deferred",1,-1,false);
        return false;
    }
    auto& results = cityServiceSearch.result();
    CityPlanningPolicy::ScanWindow scan(w,h,getGameCycleCount(),house);
    // Compare the whole candidate neighbourhood before spending. A rotating
    // map stripe both chose fringe sites and forgot savings on the next pass.
    scan.begin = 0; scan.end = w*h;

    auto plannedTerrain = sim->getParkTerrain();
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder || !DuneCity::usesParkTerrain(entry.second.item)) continue;
        const auto& plan = entry.second;
        plannedTerrain.addSource(plan.location.x,plan.location.y,DuneCity::getParkLandValueBonus(plan.item));
    }
    const auto& state = sim->getHouseState(house);
    const auto& data = currentGame->objectData.data;
    const bool powered = getHouse()->getProducedPower() >= getHouse()->getPowerRequirement();
    struct Property {
        Coord p;
        int item, value, crime, baseCrime, coverage, population, nextPopulation, demand, pollution, threat;
    };
    std::vector<Property> properties;
    std::vector<const UnitBase*> threats;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() && unit->getOwner()->getTeamID() != getHouse()->getTeamID()
            && unit->getHealth() > 0 && unit->isVisible(getHouse()->getTeamID())
            && data[unit->getItemID()][unit->getOwner()->getHouseID()].weapondamage > 0)
            threats.push_back(unit);
    }
    std::vector<Coord> turretSites, stationSites;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner()!=getHouse()) continue;
        if (structure->getItemID()==Structure_RocketTurret) turretSites.push_back(structure->getLocation());
        if (structure->getItemID()==Structure_PoliceStation && structure->getHealth()>0)
            stationSites.push_back(structure->getLocation());
    }
    for (const auto& entry:reservedStructures)
        if (entry.first!=planningBuilder && entry.second.item==Structure_PoliceStation)
            stationSites.push_back(entry.second.location);
    DuneCity::CityMapLayer<int32_t> plannedPolice;
    plannedPolice.init(w,h,DuneCity::kPoliceMapBlockSize);
    auto addSource = [&](int item,Coord p,Coord size,int funding,bool hasPower) {
        const int strength=DuneCity::getPoliceCoverage(item);
        if (strength<=0) return;
        const auto source=DuneCity::policeSource(getMap(),p.x,p.y,size.x,size.y,strength,funding,hasPower);
        DuneCity::addPoliceCoverage(plannedPolice,w,h,source.x,source.y,source.strength);
    };
    for (const auto* structure:getStructureList()) {
        const auto* owner=structure->getOwner();
        if (!owner || structure->getHealth()<=0) continue;
        addSource(structure->getItemID(),structure->getLocation(),structure->getStructureSize(),
            sim->getHouseState(owner->getHouseID()).policeFundingPercent,
            owner->getProducedPower()>=owner->getPowerRequirement());
    }
    for (const auto& entry:reservedStructures) {
        if (entry.first==planningBuilder) continue;
        const auto& plan=entry.second;
        addSource(plan.item,plan.location,getStructureSize(plan.item),state.policeFundingPercent,powered);
    }
    DuneCity::smoothPoliceCoverage(plannedPolice,w,h);
    int totalTaxBaseEighths = 0, sampleCount = 0;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        const Coord p = structure->getLocation();
        if (!getMap().tileExists(p.x, p.y)) continue;
        const int item = structure->getItemID();
        const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
        const int level = zone ? getMap().getTile(p.x,p.y)->getCityZoneDensity() : structure->getCityOccupancy();
        const int pop = DuneCity::getStructurePopulation(structure, level);
        totalTaxBaseEighths += DuneCity::getStructureTaxBaseEighths(structure, level);
        const int value = sim->getLandValueMap().worldGet(p.x,p.y);
        if (value > 0) ++sampleCount;
        int coverage = plannedPolice.worldGet(p.x,p.y);
        const int landBlockSize = sim->getLandValueMap().getBlockSize();
        int plannedValue = plannedTerrain.landValueContribution(p.x,p.y,landBlockSize)
            - sim->getParkTerrain().landValueContribution(p.x,p.y,landBlockSize);
        for (const auto& entry : reservedStructures) {
            if (entry.first == planningBuilder) continue;
            const auto& plan = entry.second;
            if (!DuneCity::usesParkTerrain(plan.item))
                plannedValue += CityServiceInvestmentPolicy::parkContribution(plan.item,
                    plan.location.x,plan.location.y,p.x,p.y,landBlockSize,plannedTerrain);
        }
        const int base = sim->getCrimeBeforePoliceMap().worldGet(p.x,p.y);
        const int crime = std::clamp(base - coverage, 0, 250);
        int threat = 0;
        if (RocketTurretPolicy::defenseWeight(item)) {
            for (const auto* unit : threats) {
                const Coord u = unit->getLocation();
                const int distance = std::max(std::abs(p.x-u.x),std::abs(p.y-u.y));
                if (distance <= 12) threat += data[unit->getItemID()][unit->getOwner()->getHouseID()].price * (13-distance)/13;
            }
            threat = std::min(threat, data[item][house].price) * RocketTurretPolicy::defenseWeight(item) / 4;
            for (const Coord t : turretSites) {
                if (std::max(std::abs(p.x-t.x),std::abs(p.y-t.y)) <= data[Structure_RocketTurret][house].weaponrange)
                    threat /= 2;
            }
            for (const auto& entry : reservedStructures) {
                if (entry.first == planningBuilder || entry.second.item != Structure_RocketTurret) continue;
                const Coord t = entry.second.location;
                if (std::max(std::abs(p.x-t.x),std::abs(p.y-t.y)) <= data[Structure_RocketTurret][house].weaponrange)
                    threat /= 2;
            }
        }
        const int demand = item == Structure_ZoneResidential ? state.resValve
            : item == Structure_ZoneCommercial ? state.comValve : item == Structure_ZoneIndustrial ? state.indValve : 0;
        const int nextPop = zone && item == Structure_ZoneResidential
            ? DuneCity::ResidentialPopulation::grow(pop,sim->getPopulationDensityMap().worldGet(p.x,p.y))
            : zone && level < DuneCity::getStructureMaxLevel(item) ? DuneCity::getZonePopulation(item,level+1) : pop;
        properties.push_back({p,item,std::min(250,value+plannedValue),crime,base,coverage,pop,nextPop,demand,
            sim->getPollutionDensityMap().worldGet(p.x,p.y),threat});
    }
    AITelemetry::log().performance(getGameCycleCount(),house,"service.properties",properties.size(),-1,false);
    LocalPointIndex propertyIndex(w,h);
    for (size_t i=0;i<properties.size();++i) propertyIndex.add(properties[i].p.x,properties[i].p.y,i);
    for (unsigned itemIndex=0; itemIndex<serviceItems.size(); ++itemIndex) {
        const Uint32 item = serviceItems[itemIndex];
        // Search both service types once, independent of the first yard's
        // upgrades/reserve. Each caller applies its own affordability and tech
        // checks when selecting. Otherwise an early low-tech yard can starve
        // every later upgraded yard's rocket search.
        if (!data[item][house].enabled || data[item][house].techLevel > currentGame->techLevel) continue;
        Value itemBest;
        Coord itemSite = Coord::Invalid();
        const Coord size = getStructureSize(item);
        std::vector<bool> candidates(w*h, false);
        for (const auto& p : properties) {
            if (p.crime < 60 && (item != Structure_RocketTurret || (p.value >= 250 && p.threat == 0))) continue;
            for (int y=std::max(scan.begin/w,p.p.y-23); y<=std::min({h-size.y,p.p.y+23,(scan.end-1)/w}); ++y)
                for (int x=std::max(0,p.p.x-23); x<=std::min(w-size.x,p.p.x+23); ++x) candidates[y*w+x]=true;
        }
        AITelemetry::PerformanceScope itemScope("ai.service_site_search",getGameCycleCount(),house,item);
        int scoredSites = 0;
        for (int cell=scan.begin;cell<scan.end;++cell) {
            const int x=cell%w, y=cell/w;
            if (x>w-size.x || y>h-size.y) continue;
            if (!candidates[y*w+x] || overlapsReservedStructure(x,y,size.x,size.y)
                || !placeableAmongOwnUnits(x,y,size.x,size.y,item)
                || !preservesGroundAccess(item,Coord(x,y))) continue;
            const auto road = cityRoadImpact(getMap(),x,y,size.x,size.y,item);
            if (!road.preservesConnections || (item == Structure_RocketTurret && road.junctionBonus <= 0)
                || wouldLandlockNeighbouringZone(getMap(),house,x,y,size.x,size.y)) continue;
            ++scoredSites;
            Value value;
            value.buildCost = data[item][house].price;
            // Placement-only cost discourages another station beside one already
            // built/planned. It does not change stacking in the simulation.
            if (item == Structure_PoliceStation) {
                for (const Coord p : stationSites)
                    value.overlapPenalty += CityServiceInvestmentPolicy::stationOverlapCost(value.buildCost,
                        std::max(std::abs(x-p.x),std::abs(y-p.y)));
            }
            value.upkeep = (DuneCity::getPoliceAnnualCost(item)*state.policeFundingPercent/100).lround();
            const int power = std::max(0,data[item][house].power);
            const int spare = std::max(0,getHouse()->getProducedPower()-getHouse()->getPowerRequirement());
            // Power upkeep (50-second budget year / 15-second power bill) plus
            // the share of a windtrap needed when spare generation is insufficient.
            value.powerCost = power*50/(32*15) + std::max(0,power-spare)*data[Structure_WindTrap][house].price
                / std::max(1,-data[Structure_WindTrap][house].power);
            const auto source = DuneCity::policeSource(getMap(),x,y,size.x,size.y,
                DuneCity::getPoliceCoverage(item),state.policeFundingPercent,powered);
            int valueGainSum = 0, neighbourhoodGainSum = 0, growthTax = 0;
            propertyIndex.visit(x,y,23,[&](size_t propertyID) {
                const auto& p=properties[propertyID];
                const int distance = std::max(std::abs(x-p.p.x),std::abs(y-p.p.y));
                const int added = DuneCity::policeCoverageAt(source.x,source.y,p.p.x,p.p.y,2,source.strength,w,h);
                const int reduction = DuneCity::marginalCrimeReduction(p.baseCrime,p.coverage,added);
                if (p.population > 0) {
                    value.crime += reduction;
                    value.crimeUtility += CityServiceInvestmentPolicy::underservedUtility(
                        CityServiceInvestmentPolicy::crimeHarm(p.crime,p.item,p.population)
                        - CityServiceInvestmentPolicy::crimeHarm(p.crime-reduction,p.item,p.population),p.coverage);
                    value.dangerousRelief += CityServiceInvestmentPolicy::reliefAboveBand(
                        p.crime,reduction,CityServiceInvestmentPolicy::dangerousBand);
                    value.preOutbreakRelief += CityServiceInvestmentPolicy::reliefAboveBand(
                        p.crime,reduction,CityServiceInvestmentPolicy::preOutbreakBand);
                }
                const int park = CityServiceInvestmentPolicy::parkContribution(item,x,y,p.p.x,p.p.y,
                    sim->getLandValueMap().getBlockSize(),plannedTerrain);
                const int restored = p.crime > 190 && p.crime-reduction <= 190 ? 20 : 0;
                const int gain = std::min(250-p.value,park+restored);
                if (p.value > 0) valueGainSum += gain;
                const auto role = DuneCity::getStructureCityRole(p.item);
                if (p.value > 0 && (role == DuneCity::CityRole::Residential || role == DuneCity::CityRole::Commercial))
                    neighbourhoodGainSum += gain;
                // Conservative forecast: at most a quarter of one demanded growth
                // level, only where improved value and acceptable pollution allow it.
                if (powered && p.demand > 0 && p.population > 0 && p.nextPopulation > p.population
                    && (p.item == Structure_ZoneResidential || p.item == Structure_ZoneCommercial)
                    && p.pollution < 128 && gain > 0)
                    growthTax += DuneCity::computeAnnualTaxRevenue(DuneCity::taxablePopulationEighths(p.item,p.nextPopulation-p.population,0),
                        sim->getCityTax(),std::max(1,state.avgLandValue)) * std::min(gain,64) / (4*64);
                if (powered && item == Structure_RocketTurret && distance <= data[item][house].weaponrange)
                    value.defense += p.threat;
            });
            value.tax = CityServiceInvestmentPolicy::annualTaxGain(totalTaxBaseEighths,sim->getCityTax(),valueGainSum,sampleCount);
            value.growthTax = growthTax;
            if (item == Structure_RocketTurret) {
                if (value.crime <= 0) continue; // Civic turrets must reduce actual crime.
                value.neighbourhoodTax = CityServiceInvestmentPolicy::annualTaxGain(
                    totalTaxBaseEighths,sim->getCityTax(),neighbourhoodGainSum,sampleCount);
            }
            for (unsigned resultMode=0;resultMode<results.size();++resultMode) {
                if (!value.useful(resultMode == 1)) continue;
                if (resultMode == 2 && (item != Structure_RocketTurret || !value.landValueTurretEligible())) continue;
                auto& result = results[resultMode][itemIndex];
                if (result.site.isInvalid() || (item == Structure_PoliceStation
                    ? value.betterPoliceSiteThan(result.value) : value.betterThan(result.value)))
                    result = {Coord(x,y),value};
            }
            itemSite = results[mode][itemIndex].site;
            itemBest = results[mode][itemIndex].value;
        }
        AITelemetry::log().performance(getGameCycleCount(),house,"service.scored_sites",scoredSites,item,false);
        AITelemetry::log().performance(getGameCycleCount(),house,"service.scanned_tiles",scan.end-scan.begin,item,false);
        traceDecision("city_service_candidate", AITelemetry::Record().set("builder",builder->getObjectID())
            .set("item",item).set("eligible",itemSite.isValid()).set("x",itemSite.x).set("y",itemSite.y)
            .set("crime_reduction",itemBest.crime).set("annual_tax_gain",itemBest.tax)
            .set("crime_utility",itemBest.crimeUtility).set("dangerous_relief",itemBest.dangerousRelief)
            .set("pre_outbreak_relief",itemBest.preOutbreakRelief)
            .set("res_com_tax_gain",itemBest.neighbourhoodTax)
            .set("estimated_growth_tax",itemBest.growthTax).set("threat_defense_value",itemBest.defense)
            .set("build_cost",itemBest.buildCost).set("annual_upkeep",itemBest.upkeep)
            .set("power_cost",itemBest.powerCost).set("placement_overlap_penalty",itemBest.overlapPenalty)
            .set("emergency",emergency));
    }
    return selectResult(results);
}

namespace QuantBotInternal {
// Desired-overlap tier for city defence: 0 Easy/Defend, 1 Medium, 2 Hard,
// 3 Brutal. The build pass shares it so the mapping stays single.
int rocketCoverageTier(QuantBot::Difficulty difficulty) {
    using Difficulty = QuantBot::Difficulty;
    return difficulty == Difficulty::Brutal ? 3 : difficulty == Difficulty::Hard ? 2
        : difficulty == Difficulty::Medium ? 1 : 0;
}
} // namespace QuantBotInternal

Coord QuantBot::findCityTurretPlaceLocation(Uint32 itemID, int* defenseScore, int* amenityScore,
                                            int* crimeBenefit, int* crimeHotspot) {
    AITelemetry::PerformanceScope perfScope("ai.findCityTurretPlaceLocation", getGameCycleCount(), getHouse()->getHouseID(), itemID);
    if (defenseScore) *defenseScore = 0;
    if (amenityScore) *amenityScore = 0;
    if (crimeBenefit) *crimeBenefit = 0;
    if (crimeHotspot) *crimeHotspot = 0;
    auto* citySim = currentGame ? currentGame->getCitySimulation() : nullptr;
    if (!citySim || itemID != Structure_RocketTurret) return Coord::Invalid();
    const Uint32 key = reservedStructures.count(planningBuilder) ? planningBuilder : NONE_ID;
    auto resultSite = [&](const CityTurretResult& result) {
        if (defenseScore) *defenseScore = result.defense;
        if (amenityScore) *amenityScore = result.amenity;
        if (crimeBenefit) *crimeBenefit = result.crime;
        if (crimeHotspot) *crimeHotspot = result.hotspot;
        return result.site;
    };
    if (const auto* cached = cityTurretSearch.get(key)) {
        AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.cache_hit",1,itemID,false);
        return resultSite(*cached);
    }
    if (!cityTurretSearch.start(key)) {
        AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.search_deferred",1,itemID,false);
        return Coord::Invalid();
    }
    const int w = getMap().getSizeX(), h = getMap().getSizeY();
    CityPlanningPolicy::ScanWindow scan(w,h,getGameCycleCount(),getHouse()->getHouseID(),
        key == NONE_ID ? 1 : cityReadyYardCount);
    scan.begin = 0; scan.end = w*h; // Compare all owned districts before buying coverage.
    auto plannedTerrain = citySim->getParkTerrain();
    for (const auto& entry : reservedStructures) {
        if (entry.first == planningBuilder || !DuneCity::usesParkTerrain(entry.second.item)) continue;
        const auto& plan = entry.second;
        plannedTerrain.addSource(plan.location.x,plan.location.y,DuneCity::getParkLandValueBonus(plan.item));
    }


    // Existing and planned turrets count as coverage, so additional yards do
    // not buy the same protection/amenity repeatedly.
    std::vector<Coord> turrets;
    struct Target { Coord position; Coord origin; Coord size; int defense; int demand; int value;
                    int firstCover; bool expansionYard; int coverage; bool covered; bool builderExpansion; };
    std::vector<Target> targets;
    const int defenseRadius = std::max(1,
        currentGame->objectData.data[itemID][getHouse()->getHouseID()].weaponrange - 1);
    // One turret in range is not cover for the assets the city cannot lose.
    // Demand the difficulty's overlap for those and keep looking for sites
    // until it is met, so a single raid cannot open the whole base.
    const int tier = rocketCoverageTier(difficulty);
    const Uint32 mainYard = mainConstructionYardID();
    auto addTarget = [&](Uint32 item, Coord pos, Coord size, Uint32 objectID) {
        const int weight = RocketTurretPolicy::assetPriority(item);
        const bool amenity = item == Structure_ZoneResidential || item == Structure_ZoneCommercial;
        if (!weight && !amenity) return;
        // Use the same structure origin that receives city land-value scans.
        const Coord point = weight ? Coord(pos.x + size.x/2, pos.y + size.y/2) : pos;
        const bool expansion = isExpansionYard(item, objectID, mainYard);
        targets.push_back({point, pos, size, weight,
            weight ? RocketTurretPolicy::desiredCoverage(item, tier, expansion) : 1,
            citySim->getLandValueMap().worldGet(point.x, point.y),
            weight ? RocketTurretPolicy::firstCoverPriority(item, expansion) : 0, expansion, 0, false, expansion && objectID==planningBuilder});
    };
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        if (structure->getItemID() == Structure_RocketTurret) turrets.push_back(structure->getLocation());
        else addTarget(structure->getItemID(), structure->getLocation(), structure->getStructureSize(),
                       structure->getObjectID());
    }
    for (const auto& entry : reservedStructures) {
        const auto& plan = entry.second;
        if (plan.item == Structure_RocketTurret) {
            if (entry.first != planningBuilder) turrets.push_back(plan.location);
        }
        else addTarget(plan.item, plan.location, getStructureSize(plan.item), NONE_ID);
    }
    for (auto& target : targets) {
        const int radius = target.defense ? defenseRadius : DuneCity::getParkLandValueRadius(Structure_RocketTurret);
        for (const auto& turret : turrets) {
            if (target.defense
                ? RocketTurretPolicy::coversBuilding(turret,target.origin,target.size,radius)
                : plannedTerrain.marginalGain(turret.x,turret.y,DuneCity::kParkLandValueBonus,
                    target.position.x,target.position.y)>0) {
                if (++target.coverage >= target.demand) { target.covered = true; break; }
            }
        }
    }
    std::vector<bool> candidates(w*h, false);
    for (const auto& target : targets) {
        if (target.covered || (!target.defense && target.value >= DuneCity::kMaxLandValue)) continue;
        const int radius = target.defense ? defenseRadius : DuneCity::getParkLandValueRadius(Structure_RocketTurret);
        for (int y = std::max(scan.begin/w, target.position.y-radius); y <= std::min({h-1, target.position.y+radius,(scan.end-1)/w}); ++y)
            for (int x = std::max(0, target.position.x-radius); x <= std::min(w-1, target.position.x+radius); ++x)
                candidates[y*w+x] = true;
    }
    // City crime is a third legitimate turret role. It is deliberately
    // secondary to critical asset defence, but lets the 15% service effect
    // fill road intersections in districts that police have not covered.
    struct CrimeTarget { Coord position; int crime; int plannedCoverage = 0; };
    std::vector<CrimeTarget> crimeTargets;
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        if (DuneCity::getStructureCityRole(structure->getItemID()) == DuneCity::CityRole::None) continue;
        const Coord p = structure->getLocation();
        const int crime = citySim->getCrimeRateMap().worldGet(p.x, p.y);
        if (crime >= 80) {
            crimeTargets.push_back({p, crime});
            for (int y = std::max(scan.begin/w, p.y-DuneCity::kPoliceRadius); y <= std::min({h-1, p.y+DuneCity::kPoliceRadius,(scan.end-1)/w}); ++y)
                for (int x = std::max(0, p.x-DuneCity::kPoliceRadius); x <= std::min(w-1, p.x+DuneCity::kPoliceRadius); ++x)
                    candidates[y*w+x] = true;
        }
    }
    auto plannedCoverageAt = [&](Coord point) {
        int coverage = 0;
        for (const auto& entry : reservedStructures) {
            if (entry.first == planningBuilder) continue;
            const int strength = DuneCity::getPoliceCoverage(entry.second.item);
            if (strength <= 0) continue;
            const Coord size = getStructureSize(entry.second.item);
            const auto source = DuneCity::policeSource(getMap(), entry.second.location.x,
                entry.second.location.y, size.x, size.y, strength,
                citySim->getHouseState(getHouse()->getHouseID()).policeFundingPercent,
                getHouse()->getProducedPower() >= getHouse()->getPowerRequirement());
            coverage += DuneCity::policeCoverageAt(source.x,source.y,point.x,point.y,2,
                source.strength,w,h);
        }
        return coverage;
    };
    for (auto& target : crimeTargets) target.plannedCoverage = plannedCoverageAt(target.position);
    Coord best = Coord::Invalid();
    RocketTurretPolicy::Score bestScore;
    int bestCrimeBenefit = 0, bestCrimeHotspot = 0;
    AITelemetry::log().performance(getGameCycleCount(),getHouse()->getHouseID(),"turret.scanned_tiles",scan.end-scan.begin,itemID,false);
    for (int cell=scan.begin;cell<scan.end;++cell) {
        const int x=cell%w, y=cell/w;
        if (!candidates[y*w+x] || overlapsReservedStructure(x, y, 1, 1)) continue;
        if (!placeableAmongOwnUnits(x, y, 1, 1, itemID)) continue;
        if (!preservesGroundAccess(itemID,Coord(x,y))) continue;
        const auto roads = cityRoadImpact(getMap(), x, y, 1, 1, itemID);
        if (!roads.preservesConnections
            || wouldLandlockNeighbouringZone(getMap(), getHouse()->getHouseID(), x, y, 1, 1)) continue;
        RocketTurretPolicy::Score score;
        score.junction = roads.junctionBonus;
        int candidateCrimeBenefit = 0, candidateCrimeHotspot = 0;
        for (const auto& target : targets) {
            if (target.covered) continue;
            const int distance = std::max(std::abs(x-target.position.x), std::abs(y-target.position.y));
            if (target.defense && RocketTurretPolicy::coversBuilding(Coord(x,y),target.origin,target.size,defenseRadius)) {
                // Nothing covers this asset yet: its first turret ranks above
                // any amount of additional overlap elsewhere. An expansion
                // yard keeps that priority, at a lower weight, until all three
                // of its demanded turrets stand — one emplacement does not
                // survive a focused wing.
                if (target.builderExpansion) score.expansion=1;
                if (target.coverage == 0) score.critical += target.firstCover;
                else if (target.expansionYard) score.critical += 1;
                score.defense += target.defense;
                score.proximity += target.defense * (defenseRadius-distance);
            } else if (!target.defense) {
                score.amenity += RocketTurretPolicy::amenityBenefit(target.value, false,
                    plannedTerrain.marginalGain(x,y,DuneCity::getParkLandValueBonus(itemID),
                        target.position.x,target.position.y));
            }
        }
        const auto source = DuneCity::policeSource(getMap(), x, y, 1, 1, DuneCity::getPoliceCoverage(itemID),
            citySim->getHouseState(getHouse()->getHouseID()).policeFundingPercent,
            getHouse()->getProducedPower() >= getHouse()->getPowerRequirement());
        for (const auto& target : crimeTargets) {
            const int added = DuneCity::policeCoverageAt(source.x,source.y,target.position.x,target.position.y,
                2,source.strength,w,h);
            const int reduction = DuneCity::marginalCrimeReduction(
                citySim->getCrimeBeforePoliceMap().worldGet(target.position.x, target.position.y),
                citySim->getPoliceCoverageMap().worldGet(target.position.x, target.position.y)
                    + target.plannedCoverage, added);
            candidateCrimeBenefit += reduction;
            if (reduction > 0) candidateCrimeHotspot = std::max(candidateCrimeHotspot, target.crime);
        }
        // Amenity normally ranges in the tens. A turret must reduce crime in
        // several blocks before it competes with a demanded tax-base zone.
        score.amenity += candidateCrimeBenefit / 8;
        if (score.useful() && (!best.isValid() || score.betterThan(bestScore))) {
            best = Coord(x, y); bestScore = score;
            bestCrimeBenefit = candidateCrimeBenefit;
            bestCrimeHotspot = candidateCrimeHotspot;
        }
    }
    if (defenseScore) *defenseScore = bestScore.defense;
    if (amenityScore) *amenityScore = bestScore.amenity;
    if (crimeBenefit) *crimeBenefit = bestCrimeBenefit;
    if (crimeHotspot) *crimeHotspot = bestCrimeHotspot;
    cityTurretSearch.result() = {best,bestScore.defense,bestScore.amenity,bestCrimeBenefit,bestCrimeHotspot};
    return best;
}

Coord QuantBot::findEffectiveTurretPlaceLocation(Uint32 itemID) {
    if (currentGame && currentGame->isCitySimEnabled() && itemID == Structure_RocketTurret)
        return findCityTurretPlaceLocation(itemID);
    return findTurretPlaceLocation(itemID);
}

Coord QuantBot::findFrontBatteryPlaceLocation() {
    // The enemy-facing battery has its own site search in BOTH modes.
    //
    // findEffectiveTurretPlaceLocation routes rocket turrets to the city
    // coverage planner when the city simulation is on, and that planner scores
    // sites by asset coverage, crime and land value - correctly, for the
    // critical-asset and remote-colony cover it owns. It has no notion of a
    // front, so a battery placed through it would not face the enemy at all.
    //
    // This is additive: the city planner keeps every ordinary rocket-turret
    // decision, including the first cover of a critical asset, the three-turret
    // minimum for an outlying colony, the service reserve and the growth
    // interleave. Only the battery rule uses this entry point.
    return findTurretPlaceLocation(Structure_RocketTurret);
}

std::array<int,6> QuantBot::batteryZoneCounts(Coord anchor) const {
    std::array<int,6> counts{};
    const int radius = 2 * std::max(1,
        currentGame->objectData.data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange);
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0
            || !DuneCity::isCityZoneStructure(structure->getItemID())
            || blockDistance(structure->getLocation(), anchor).lround() > radius) continue;
        const int type = structure->getItemID() - Structure_ZoneResidential;
        ++counts[type];
        const auto* tile = getMap().getTile(structure->getLocation());
        counts[type+3] += std::max(1,DuneCity::getStructurePopulation(structure,
            tile ? int(tile->getCityZoneDensity()) : 0));
    }
    return counts;
}

bool QuantBot::batteryClearanceZones(Coord pos, std::vector<Uint32>& zones, Coord anchor,
                                      const std::array<int,6>* localCounts) const {
    zones.clear();
    if (!anchor.isValid()) return false;
    // Scope. The fallback exists for the Custom Hard/Brutal city bot and
    // nothing else: no campaign, no support role, no Easy/Medium, no human and
    // no house a human shares, and only while the battery feature is on.
    if (!currentGame || !currentGame->isCitySimEnabled()) return false;
    if (!recoveryActive() || !botConfig().recovery.frontBatteriesEnabled) return false;
    if (!getHouse() || !getHouse()->isAI()) return false;
    const auto* sim = currentGame->getCitySimulation();
    if (!sim || !sim->isInitialized()) return false;
    if (!pos.isValid() || !getMap().tileExists(pos.x, pos.y)) return false;

    // Ground. The emplacement needs rock inside this house's build range; a lot
    // may legally sit on sand, and clearing one would not make the tile
    // buildable, so that lot is not a candidate.
    const Tile* tile = getMap().getTile(pos.x, pos.y);
    if (!tile->isRock() || tile->isMountain()) return false;
    if (!getMap().isWithinBuildRange(pos.x, pos.y, getHouse())) return false;

    // The occupant must be one of this house's own R/C/I lots. Roads, services,
    // essential buildings, anything foreign and anything that is not a zone at
    // all fall out here, because nothing else is a ZoneStructure of ours.
    const ObjectBase* object = tile->getNonInfantryGroundObject();
    const auto* zone = dynamic_cast<const ZoneStructure*>(object);
    if (!zone || zone->getOwner() != getHouse() || zone->getHealth() <= 0) return false;
    if (!DuneCity::isCityZoneStructure(zone->getItemID())) return false;

    const Coord lot = zone->getLocation();
    const Coord extent(zone->getStructureSizeX(), zone->getStructureSizeY());
    if (extent.x <= 0 || extent.y <= 0) return false;
    // A genuinely local economic floor. The count is over the colony this
    // battery belongs to, not over the house: a second colony elsewhere on the
    // map must not be able to mask the loss of the last lots standing next to
    // the anchor. The neighbourhood is the same colony geometry the belt uses.
    const int colonyRadius = 2 * std::max(1,
        currentGame->objectData.data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange);
    if (blockDistance(lot, anchor).lround() > colonyRadius) return false;
    const auto counts = localCounts ? *localCounts : batteryZoneCounts(anchor);
    const int localTotal = counts[0] + counts[1] + counts[2];
    if (!FrontBatteryPolicy::preservesLocalZoneFloor(
            counts[zone->getItemID() - Structure_ZoneResidential], localTotal)) return false;
    const int level = int(getMap().getTile(lot)->getCityZoneDensity());
    const int economicPopulation = std::max(1,DuneCity::getStructurePopulation(zone,level));
    if (!FrontBatteryPolicy::clearableLot(level,zone->getResidentialPopulation(),
            zone->getCivicOverlay()!=ZoneStructure::CivicOverlay::None,economicPopulation,
            counts[zone->getItemID()-Structure_ZoneResidential+3])) return false;

    // Nothing may be standing on the lot and no yard may have reserved it.
    for (int y = lot.y; y < lot.y + extent.y; ++y) for (int x = lot.x; x < lot.x + extent.x; ++x) {
        if (!getMap().tileExists(x, y)) return false;
        const Tile* lotTile = getMap().getTile(x, y);
        if (lotTile->hasInfantry()) return false;
        const ObjectBase* occupant = lotTile->getNonInfantryGroundObject();
        if (occupant != nullptr && occupant != object) return false;
    }
    if (overlapsReservedStructure(lot.x, lot.y, extent.x, extent.y)) return false;

    zones.push_back(zone->getObjectID());
    return int(zones.size()) <= FrontBatteryPolicy::kClearanceLotsPerPass;
}

Coord QuantBot::findBatteryClearanceSite(Coord requiredSite) {
    // Last resort, and only that. Ordinary free ground is searched first and
    // answered first; this runs only when that search has nothing, which is
    // what an established city facing a fixed direction eventually looks like.
    //
    // This is advice to the chooser and nothing more. It demolishes nothing,
    // reserves nothing and spends nothing.
    //
    // `requiredSite` turns the same search into a re-validation of one tile:
    // commitBatteryClearance() passes the tile it is about to displace a lot
    // on, so every scope, economy, front, allowance, spacing, density, mutual
    // support, corridor, road, access, ownership, floor, occupancy and
    // reservation rule below is re-applied to the live map at the moment the
    // lot would actually be lost - rather than being approximated by a second
    // copy of the same conditions.
    const bool revalidating = requiredSite.isValid();
    if (findFrontBatteryPlaceLocation().isValid()) return Coord::Invalid();
    if (!revalidating && batteryClearanceCache) return *batteryClearanceCache;
    if (!revalidating) batteryClearanceCache = Coord::Invalid();
    if (!currentGame || !currentGame->isCitySimEnabled()) return Coord::Invalid();
    if (!recoveryActive() || !botConfig().recovery.frontBatteriesEnabled) return Coord::Invalid();
    if (!getHouse() || !getHouse()->isAI()) return Coord::Invalid();
    const auto* sim = currentGame->getCitySimulation();
    if (!sim || !sim->isInitialized()) return Coord::Invalid();
    AITelemetry::PerformanceScope perfScope("ai.findBatteryClearanceSite", getGameCycleCount(),
        getHouse()->getHouseID());

    const Coord anchor = batteryAnchor();
    if (anchor.isInvalid()) return Coord::Invalid();
    const Coord forward = observedFrontDirection(anchor);
    // No observed enemy approach means no enemy-facing side to clear towards,
    // and an unaimed demolition is exactly what this must not do.
    if (forward.x == 0 && forward.y == 0) return Coord::Invalid();
    const BatteryGeometry geometry = batteryGeometry(anchor, forward);
    // The flank and rear allowance is spent the same way here as on free
    // ground: once the enemy-facing share of the goal is taken, nothing is
    // displaced for it.
    if (geometry.frontAllowance <= 0 || geometry.frontTurrets >= geometry.frontAllowance)
        return Coord::Invalid();

    const int houseID = getHouse()->getHouseID();
    const auto& state = sim->getHouseState(houseID);
    const auto localCounts = batteryZoneCounts(anchor); // One census per search, not per tile.
    Coord best = Coord::Invalid();
    int64_t bestScore = std::numeric_limits<int64_t>::min();
    int considered = 0, cleared = 0;

    for (const StructureBase* structure : getStructureList()) {
        const auto* zone = dynamic_cast<const ZoneStructure*>(structure);
        if (!zone || zone->getOwner() != getHouse() || zone->getHealth() <= 0) continue;
        const Coord lot = zone->getLocation();
        const Coord extent(zone->getStructureSizeX(), zone->getStructureSizeY());
        if (revalidating && !(requiredSite.x >= lot.x && requiredSite.x < lot.x + extent.x
                && requiredSite.y >= lot.y && requiredSite.y < lot.y + extent.y)) continue;
        // The lot itself must be on the observed enemy side of the anchor.
        if (!FrontBatteryPolicy::onFrontSide(lot.x - anchor.x, lot.y - anchor.y,
                forward.x, forward.y)) continue;
        ++considered;
        // Displacement price of this lot, in the same currency the established
        // redevelopment rule uses, so an empty or struggling lot is preferred
        // over a growing one and a cheap district over an expensive one.
        const bool residential = zone->getItemID() == Structure_ZoneResidential;
        const int demand = residential ? state.resValve
            : zone->getItemID() == Structure_ZoneCommercial ? state.comValve : state.indValve;
        const int displacement = RedevelopmentPolicy::displacementCost(
            std::max(int(getMap().getTile(lot.x, lot.y)->getCityZoneDensity()),
                zone->getResidentialPopulation() > 0 ? 1 : 0),
            sim->getLandValueMap().worldGet(lot.x, lot.y), demand, residential ? 2000 : 1500);

        for (int y = lot.y; y < lot.y + extent.y; ++y) for (int x = lot.x; x < lot.x + extent.x; ++x) {
            const Coord site(x, y);
            if (revalidating && site != requiredSite) continue;
            std::vector<Uint32> lots;
            // Every ownership, type, service, growth, local floor,
            // occupancy and reservation rule lives in one place and is asked
            // here per candidate tile - and asked again immediately before the
            // lot is actually demolished.
            if (!batteryClearanceZones(site, lots, anchor, &localCounts)) continue;
            if (!FrontBatteryPolicy::onFrontSide(x - anchor.x, y - anchor.y, forward.x, forward.y)) continue;
            // The emplacement itself must be legal once the lot is gone: in
            // range, on rock, not landlocking a neighbouring lot, keeping the
            // roads, local ground access and factory exits.
            if (overlapsReservedStructure(x, y, 1, 1)) continue;
            if (wouldLandlockNeighbouringZone(getMap(), houseID, x, y, 1, 1, &lots)) continue;
            if (!cityRoadImpact(getMap(), x, y, 1, 1, Structure_RocketTurret).preservesConnections) continue;
            if (!preservesGroundAccess(Structure_RocketTurret, site, &lots)) continue;
            // Battery geometry, identical to the free-ground search.
            int nearest = -1, inCluster = 0;
            bool covered = false;
            for (const Coord turret : geometry.turrets) {
                const int d = std::max(std::abs(turret.x - x), std::abs(turret.y - y));
                if (nearest < 0 || d < nearest) nearest = d;
                if (d <= geometry.clusterRadius) ++inCluster;
                if (d <= geometry.coverRadius) covered = true;
            }
            if (!FrontBatteryPolicy::spacedEnough(nearest)) continue;
            if (!FrontBatteryPolicy::withinClusterLimit(inCluster)) continue;
            if (!FrontBatteryPolicy::mutuallySupported(geometry.localTurrets, covered)) continue;
            int passable = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                if (!getMap().tileExists(x + dx, y + dy)) continue;
                const Tile* neighbour = getMap().getTile(x + dx, y + dy);
                const bool freed = dx + x >= lot.x && dx + x < lot.x + extent.x
                    && dy + y >= lot.y && dy + y < lot.y + extent.y;
                if (!neighbour->isMountain() && (freed || !neighbour->hasAStructure())) ++passable;
            }
            if (!FrontBatteryPolicy::keepsCorridor(passable)) continue;
            ++cleared;
            // Cheapest lot first, then the tile furthest towards the enemy, then
            // a fixed coordinate order so the choice is reproducible.
            const int scale = std::max(1, std::max(std::abs(forward.x), std::abs(forward.y)));
            const int along = (forward.x * (x - anchor.x) + forward.y * (y - anchor.y)) / scale;
            const int64_t score = -int64_t(displacement) * 1000
                + int64_t(std::clamp(along, -20, 20)) * 10;
            if (score > bestScore || (score == bestScore && best.isValid()
                    && (y < best.y || (y == best.y && x < best.x)))) {
                bestScore = score;
                best = site;
            }
        }
    }

    traceDecision(revalidating ? "front_battery_clearance_revalidated" : "front_battery_clearance_site",
        AITelemetry::Record()
            .set("x", best.x).set("y", best.y).set("valid", best.isValid())
            .set("forward_x", forward.x).set("forward_y", forward.y)
            .set("front_lots_considered", considered).set("legal_tiles", cleared)
            .set("front_allowance", geometry.frontAllowance)
            .set("front_turrets", geometry.frontTurrets));
    if (!revalidating) batteryClearanceCache = best;
    return best;
}

bool QuantBot::commitBatteryClearance(const BuilderBase* builder, Uint32 itemToBePlaced,
                                      std::list<Coord>& placeLocations, int spendable,
                                      int economyReserve, int commitmentShortfall, bool* defer) {
    /*
        The only place a lot is ever displaced for a battery.

        By the time control reaches here the yard is holding *finished
        material*: `builder->isWaitingToPlace()` is true and `itemToBePlaced`
        has been produced and paid for. That is the first moment at which
        demolishing is not a gamble, which is why nothing happens at order
        acceptance - an accepted order is neither payment nor a placement, and
        a cancellation, a re-site or the loss of the yard before this point
        must leave the lot untouched. Each of those simply never reaches here.

        Two shapes of commit:

          * No concrete required, or the lot's ground is already prepared. The
            finished item IS the emplacement. The lot goes and the turret is
            placed on the ground it frees, in this same synchronous call.

          * Concrete required. A slab cannot be laid on an occupied tile and
            the turret cannot stand on bare ground without taking foundation
            damage, so the legitimate sequence is slab first. The accepted
            narrow trade-off is to commit at the *completed foundation slab*:
            real material, really paid for, placed on the freed tile in this
            same call. It is only taken while the emplacement itself is still
            in this yard's real production queue under this yard's
            reservation, and while queued obligations plus the established
            economy reserve are actually funded.

        Honest statement of the residual: after this point the house has spent
        a lot and owns concrete, and an attack or a player cancellation can
        still stop the turret from ever being finished. That window is one
        building's construction time, it cannot be removed without either
        demolishing earlier (worse) or inventing an escrow, and it is the same
        exposure the established redevelopment rule already carries.
    */
    if (defer) *defer = false;
    if (!builder || !currentGame || !currentGame->isCitySimEnabled()) return false;
    if (builder->getOwner() != getHouse() || builder->getObjectID() != planningBuilder
        || builder->getItemID() != Structure_ConstructionYard || !builder->isActive()
        || builder->getHealth() <= 0 || builder->getCurrentProducedItem() != itemToBePlaced) return false;
    const auto reserved = reservedStructures.find(planningBuilder);
    if (reserved == reservedStructures.end() || reserved->second.item != Structure_RocketTurret) return false;
    const Coord site = reserved->second.location;
    const bool placingTurret = itemToBePlaced == Structure_RocketTurret;
    const bool placingFoundation = itemToBePlaced == Structure_Slab1 || itemToBePlaced == Structure_Slab4;
    if (!placingTurret && !placingFoundation) return false;
    // From here on this really is a reserved emplacement whose finished
    // material is in hand, so every refusal is worth a reason in the log.
    auto decline = [&](const char* reason) {
        traceDecision("front_battery_clearance_declined", AITelemetry::Record()
            .set("builder", planningBuilder).set("x", site.x).set("y", site.y)
            .set("reason", reason).set("item", itemToBePlaced));
        return false;
    };
    if (!FrontBatteryPolicy::mayOrderThisPass(batteryClearancesThisPass,
            FrontBatteryPolicy::kClearanceLotsPerPass)) {
        if (defer) *defer = true;
        return decline("pass_budget_spent");
    }
    if (!site.isValid() || placeLocations.empty() || placeLocations.front() != site)
        return decline("material_not_planned_for_this_site");

    const Coord anchor = batteryAnchor();
    std::vector<Uint32> lots;
    if (!batteryClearanceZones(site, lots, anchor) || lots.empty())
        return decline("site_no_longer_a_displaceable_lot");

    // Free ground beats displacing a lot, right up to the last moment. If a
    // site has opened since the order, the reservation and the finished
    // material move there and the lot is left standing.
    const Coord freeSite = findFrontBatteryPlaceLocation();
    if (freeSite.isValid()) {
        reserved->second.location = freeSite;
        placeLocations.clear();
        placeLocations.push_back(freeSite);
        if (placingFoundation) placeLocations.push_back(freeSite);
        traceDecision("front_battery_clearance_retargeted", AITelemetry::Record()
            .set("builder", planningBuilder).set("from_x", site.x).set("from_y", site.y)
            .set("to_x", freeSite.x).set("to_y", freeSite.y).set("item", itemToBePlaced));
        return false;
    }
    // And the whole chooser is re-run against this one tile, so the scope,
    // economy, demand, allowance, front, geometry, corridor, road, access and
    // reservation rules that justified the plan must all still hold now. A
    // coverage turret that merely happens to be reserved where a lot has since
    // appeared is not a battery decision and fails here.
    if (findBatteryClearanceSite(site) != site) return decline("revalidation_failed");
    if (!builder->isWaitingToPlace()) return decline("material_not_finished");
    if (!campaignAvailableToBuild(builder, Structure_RocketTurret)) return decline("rocket_tech_lost");
    const int requiredPower = std::max(0,
        currentGame->objectData.data[Structure_RocketTurret][getHouse()->getHouseID()].power)
        + std::max({DuneCity::getZonePower(Structure_ZoneResidential,1),
                    DuneCity::getZonePower(Structure_ZoneCommercial,1),
                    DuneCity::getZonePower(Structure_ZoneIndustrial,1)});
    if (getGameInitSettings().getGameOptions().rocketTurretsNeedPower
        && getHouse()->getProducedPower() - getHouse()->getPowerRequirement() < requiredPower)
        return decline("power_buffer_lost");
    // An emplacement needs one tile. A bulk slab's extra tiles must never be
    // cleared implicitly; the normal foundation planner chooses Slab1 here.
    if (placingFoundation && (itemToBePlaced != Structure_Slab1
        || getMap().getTile(site)->isRoad())) return decline("foundation_shape_changed");
    if (placingTurret && getGameInitSettings().getGameOptions().concreteRequired
        && !getMap().getTile(site)->hasPreparedFoundation()) return decline("foundation_not_ready");

    if (placingFoundation) {
        // The emplacement must still be a real, queued obligation of this yard,
        // and the house must still be able to finish paying for it.
        bool turretQueued = false;
        int turretPrice = 0;
        for (const auto& offer : builder->getBuildList())
            if (offer.itemID == Structure_RocketTurret) {
                // BuildItem::num is this yard's live queued count for the item,
                // decremented when the item finishes and when it is cancelled.
                turretQueued = offer.num > 0;
                turretPrice = int(offer.price);
            }
        if (!turretQueued) return false;
        const int paid = builder->getCurrentProducedItem() == Structure_RocketTurret
            ? builder->getProductionProgress().lround() : 0;
        const int remaining = std::max(0, turretPrice - paid);
        // The caller already withheld every queued obligation, including this
        // turret. Requiring its price again would make a fully funded project
        // wait for twice its cost. A raw shortfall must not disappear when the
        // ordinary new-order budget is clamped to zero.
        if (commitmentShortfall > 0 || spendable < economyReserve) {
            if (defer) *defer = true;
            traceDecision("front_battery_clearance_deferred", AITelemetry::Record()
                .set("builder", planningBuilder).set("reason", "turret_not_funded")
                .set("spendable", spendable).set("reserve", economyReserve).set("remaining", remaining));
            return false;
        }
    }

    AITelemetry::Record removed;
    const auto* sim = currentGame->getCitySimulation();
    int displaced = 0;
    for (const Uint32 id : lots) {
        auto* zone = dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(id));
        if (!zone || zone->getOwner() != getHouse()) continue;
        const Coord z = zone->getLocation();
        const auto& state = sim->getHouseState(getHouse()->getHouseID());
        const int demand = zone->getItemID() == Structure_ZoneResidential ? state.resValve
            : zone->getItemID() == Structure_ZoneCommercial ? state.comValve : state.indValve;
        removed.set(std::to_string(id), AITelemetry::Record().set("item", zone->getItemID())
            .set("demand", demand).set("land_value", sim->getLandValueMap().worldGet(z.x, z.y))
            .set("density", getMap().getTile(z.x, z.y)->getCityZoneDensity()));
        zone->demolish();
        ++displaced;
    }
    if (!displaced) return false;
    ++batteryClearancesThisPass;
    // The map changed under every placement search; the reservation itself
    // stays, because the emplacement is still owed this ground.
    clearPlacementCache();
    traceDecision("front_battery_clearance_committed", AITelemetry::Record()
        .set("builder", planningBuilder).set("x", site.x).set("y", site.y)
        .set("placing", itemToBePlaced).set("removed_zones", removed)
        .set("pass_clearances", batteryClearancesThisPass));
    return true;
}

Coord QuantBot::findPlaceLocationSimple(Uint32 itemID) {
	int newSizeX = getStructureSize(itemID).x;
	int newSizeY = getStructureSize(itemID).y;

	squadRallyLocation = findSquadRallyLocation();

	FixPoint bestScore = -FixPt_MAX;
	Coord bestLocation = Coord::Invalid();

	// Check every tile on the map for valid placement
	for (int x = 0; x <= getMap().getSizeX() - newSizeX; x++) {
		for (int y = 0; y <= getMap().getSizeY() - newSizeY; y++) {
			// First check if this location is valid for building (own units are traffic)
			if (placeableAmongOwnUnits(x, y, newSizeX, newSizeY, itemID)) {
                if (DuneCity::isCityZoneStructure(itemID) && getGameInitSettings().getGameOptions().concreteRequired
                    && !getMap().okayToPlaceStructure(x,y,newSizeX,newSizeY,false,getHouse(),true)) continue;
                if (!preservesGroundAccess(itemID,Coord(x,y))) continue;

				FixPoint score = 0;

				// Base scoring - favor being close to existing buildings
				FixPoint closestOwnBuildingDistance = FixPt_MAX;
				for (const StructureBase* pStructure : getStructureList()) {
					if (pStructure->getOwner() == getHouse()) {
						FixPoint distance = blockDistance(Coord(x, y), Coord(pStructure->getX(), pStructure->getY()));
						if (distance < closestOwnBuildingDistance) {
							closestOwnBuildingDistance = distance;
						}
					}
				}
				if (closestOwnBuildingDistance < FixPt_MAX) {
					score += 50 - closestOwnBuildingDistance; // Bonus for being close to our buildings
				}

				// Building-specific placement preferences
				if (itemID == Structure_GunTurret || itemID == Structure_RocketTurret) {
					// Turrets prefer map edges for defensive positioning
					int distanceToEdge = std::min({x, y, getMap().getSizeX() - 1 - x, getMap().getSizeY() - 1 - y});
					score += (10 - distanceToEdge) * 5; // Higher score for being closer to edges

					// Rocket turrets also prefer being close to squad rally point
					if (itemID == Structure_RocketTurret) {
						FixPoint distanceToRally = blockDistance(squadRallyLocation, Coord(x, y));
						score += 30 - distanceToRally * 2; // Bonus for being close to rally point
					}
				}
				else if (itemID == Structure_Refinery) {
					// Refineries prefer being close to spice deposits
					FixPoint closestSpiceDistance = FixPt_MAX;
					for (int spiceX = 0; spiceX < getMap().getSizeX(); spiceX++) {
						for (int spiceY = 0; spiceY < getMap().getSizeY(); spiceY++) {
							if (getMap().tileExists(spiceX, spiceY) && getMap().getTile(spiceX, spiceY)->hasSpice()) {
								FixPoint spiceDistance = blockDistance(Coord(x, y), Coord(spiceX, spiceY));
								if (spiceDistance < closestSpiceDistance) {
									closestSpiceDistance = spiceDistance;
								}
							}
						}
					}
					if (closestSpiceDistance < FixPt_MAX) {
						score += 50 - closestSpiceDistance * 2; // Higher bonus for being closer to spice
					}
				}
				else if (itemID == Structure_HeavyFactory || itemID == Structure_LightFactory || 
						 itemID == Structure_WOR || itemID == Structure_Barracks || itemID == Structure_StarPort) {
					// Production buildings prefer being close to rally point and base center
					FixPoint distanceToRally = blockDistance(squadRallyLocation, Coord(x, y));
					FixPoint distanceToBase = blockDistance(findBaseCentre(getHouse()->getHouseID()), Coord(x, y));
					score += 20 - distanceToRally / 2; // Bonus for being close to rally point
					score += 20 - distanceToBase; // Bonus for being close to base center
				}

				// Favor map edges in general for defensive positioning
				if (x == 0 || x == getMap().getSizeX() - newSizeX || y == 0 || y == getMap().getSizeY() - newSizeY) {
					score += 10;
				}

				// Check if this is the best location so far
				if (score > bestScore) {
					bestScore = score;
					bestLocation = Coord(x, y);
				}
			}
		}
	}

	return bestLocation;
}
