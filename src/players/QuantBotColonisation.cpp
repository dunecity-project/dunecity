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

// QuantBot MCV deployment and colonisation: rock expansion sites, formations, expansion cover,
// lost colonists and colonist escorts.

#include "QuantBotInternal.h"

#include <FormationOrders.h>

#include <iterator>

Coord QuantBot::findRockExpansionSite(const MCV* mcv, bool needsLocalSpace) {
    // The base survey notices colonists that died on the way since it last
    // ran; colonySiteOpen() holds further ones back while that is remembered.
    if(!mcv) recordLostColonists();
    // A per-MCV query is a trip about to start, so the shut gates refuse it
    // before the survey costs anything. The lost-colonist hold stops new
    // colonists, not one already bought that has no footprint at home
    // (needsLocalSpace): parking that MCV until the losses age out is the
    // idle MCV this rule must never create. The base survey always runs to
    // the end: it measures how much room the base has left, which tells
    // production the city has nowhere left to build, and it reports whether
    // new rock exists at all while colonising it is blocked, which keeps
    // production yards from stacking on the home rock in the meantime.
    // Whether a colonist may set out for that rock is colonySiteOpen()'s
    // decision.
    if(mcv && ((!needsLocalSpace && colonistsLost()>=kRepeatedYardLossLimit)
            || !expansionDefenceReady(needsLocalSpace)))
        return Coord::Invalid();
    AITelemetry::PerformanceScope survey("ai.rock_survey.anchor_field",
        getGameCycleCount(), getHouse()->getHouseID());
    const int w=getMap().getSizeX(),h=getMap().getSizeY();
    // Map::isWithinBuildRange() is a box dilation of the owned-tile set, but it
    // is evaluated pointwise: BUILDRANGE 2 means a 5x5 probe for every rock
    // tile on the map. One summed-area table over the anchor indicator answers
    // the same question in four lookups. isConstructionAnchor() is exactly
    // tileOwner==houseID, and getTile_internal() simply skips off-map probes,
    // which is the same clipping the box query performs — so the predicate is
    // unchanged for every input, including map edges.
    DuneCity::BoxAnyField anchorField;
    buildAnchorField(anchorField);
    auto anchorsInRange=[&](int x,int y) { return anchorField.anyWithin(x,y,BUILDRANGE); };
    survey.next("ai.rock_survey.tilescan");
    std::vector<RockExpansionPolicy::Tile> tiles(w*h);
    std::vector<char> buildable(static_cast<size_t>(w)*h,0);
    std::vector<int> starts,enemies,reserved;
    int freeBase=0;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const auto* tile=getMap().getTile(x,y);
        const auto* ground=tile->getNonInfantryGroundObject();
        auto& out=tiles[y*w+x];
        out.rock=tile->isRock()&&!tile->isMountain();
        out.free=!tile->hasAStructure() && (!tile->hasAGroundObject() || ground==mcv);
        out.walkable=!tile->isMountain()&&!tile->hasAStructure();
        out.owned=tile->hasAStructure()&&tile->getOwner()==getHouse()->getHouseID();
        // A site that already swallowed repeated construction yards is not
        // expanded onto again just because the short-term loss block expired:
        // redeploying there without cover is what turned single losses into a
        // chain of replacements.
        out.unsafe=dangerAt(Coord(x,y),Coord(1,1))>0||nearRecentStructureLoss(x,y,1,1)
            ||lostYardsNear(x,y,kRepeatedYardLossRadius)>=kRepeatedYardLossLimit;
        const bool inBuildRange=out.rock&&anchorsInRange(x,y);
        if(inBuildRange&&out.free) {
            ++freeBase;
            if(!mcv)starts.push_back(y*w+x);
        }
        // Room for a future building, which is not the same as a free tile:
        // zoned and reserved ground is spoken for, and a unit parked on rock
        // is traffic, not a reason to call the base full.
        if(!mcv) buildable[y*w+x]=inBuildRange&&!out.unsafe&&!tile->hasAStructure()&&!tile->hasCityZone()
            &&!overlapsReservedStructure(x,y,1,1);
    }
    if(!mcv) {
        survey.next("ai.rock_survey.footprints");
        availableBaseRock=freeBase;
        availableBaseFootprints=QuantBotColonisationPolicy::freeFootprints(w,h,buildable,2,2,
            QuantBotColonisationPolicy::kCrampedFootprints);
    }
    if(mcv && mcv->getLocation().isValid()) starts.push_back(mcv->getY()*w+mcv->getX());
    if(!mcv && starts.empty()) {
        // A base with no free rock left still stands somewhere. Start the route
        // survey from the open ground around its buildings: the city that most
        // needs to settle elsewhere is exactly the one with nothing left to
        // start from, and it must not be the one that finds nowhere to go.
        for(const auto* structure:getStructureList()) {
            if(structure->getOwner()!=getHouse()) continue;
            const Coord at=structure->getLocation(),size=structure->getStructureSize();
            for(int x=at.x-1;x<=at.x+size.x;++x) for(int y=at.y-1;y<=at.y+size.y;++y)
                if(getMap().tileExists(x,y)&&tiles[y*w+x].walkable) starts.push_back(y*w+x);
        }
    }
    survey.next("ai.rock_survey.enemies");
    auto enemy=[&](const ObjectBase* object) {
        if(object->getOwner() && object->getOwner()->getTeamID()!=getHouse()->getTeamID()
            && object->isVisible(getHouse()->getTeamID())&&object->getLocation().isValid())
            enemies.push_back(object->getY()*w+object->getX());
    };
    for(const auto* structure:getStructureList())enemy(structure);
    for(const auto* unit:getUnitList()) {
        enemy(unit);
        if(unit->getOwner()==getHouse()&&unit->getItemID()==Unit_MCV&&unit!=mcv) {
            const auto it=mcvExpansionSites.find(unit->getObjectID());
            if(it!=mcvExpansionSites.end()&&getMap().tileExists(it->second.x,it->second.y))reserved.push_back(it->second.y*w+it->second.x);
        }
    }
    const StructureBase* mainYard=nullptr;
    for(const auto* structure:getStructureList())
        if(structure->getOwner()==getHouse() && structure->getItemID()==Structure_ConstructionYard
            && structure->isActive() && structure->getHealth()>0
            && (!mainYard || structure->getObjectID()<mainYard->getObjectID())) mainYard=structure;
    const int mainBase=mainYard ? mainYard->getY()*w+mainYard->getX() : -1;
    survey.next("ai.rock_survey.choose");
    const auto result=RockExpansionPolicy::choose(w,h,tiles,starts,enemies,reserved,mainBase);
    if(!result.valid())return Coord::Invalid();
    const Coord site(result.x,result.y);
    if(!overlapsReservedStructure(site.x,site.y,2,2)&&preservesGroundAccess(Structure_ConstructionYard,site)) {
        traceDecision("rock_expansion_site",AITelemetry::Record().set("mcv",mcv?mcv->getObjectID():NONE_ID)
            .set("x",site.x).set("y",site.y).set("free_base_rock",freeBase)
            .set("local_free_rock",result.room).set("enemy_clearance",result.clearance).set("route_tiles",result.distance)
            .set("main_base_x",mainYard?mainYard->getX():-1).set("main_base_y",mainYard?mainYard->getY():-1)
            .set("base_distance",result.baseDistance).set("selection_rule","nearest_main_base_safe_rock")
            .set("base_free_footprints",availableBaseFootprints)
            .set("base_production_room_blocked",baseProductionRoomBlocked));
        return site;
    }
    return Coord::Invalid();
}

bool QuantBot::baseBuiltOut() const {
    QuantBotColonisationPolicy::Demand demand;
    demand.productionRoomBlocked=baseProductionRoomBlocked;
    demand.freeFootprints=availableBaseFootprints;
    return QuantBotColonisationPolicy::builtOut(demand);
}

bool QuantBot::colonySiteOpen() const {
    // The survey's destination says where new rock lies. Whether a colonist
    // may set out for it now is decided against the current base: its core,
    // and colonists already lost on the way.
    return rockExpansionSite.isValid() && colonistsLost()<kRepeatedYardLossLimit && expansionDefenceReady();
}

QuantBotColonisationPolicy::Demand QuantBot::colonisationDemand(int mcvsIncludingQueued, int yardLimit) const {
    QuantBotColonisationPolicy::Demand demand;
    demand.citySim=currentGame->isCitySimEnabled();
    demand.customGame=gameMode==GameMode::Custom;
    // Campaign missions keep the base the script gave them, whatever mode the
    // helper bot runs in. Colonisation is a custom-game rule only.
    demand.campaignGame=isCampaignGameType(currentGame->gameType);
    demand.supportMode=supportMode;
    demand.siteAvailable=colonySiteOpen();
    demand.productionRoomBlocked=baseProductionRoomBlocked;
    demand.freeFootprints=availableBaseFootprints;
    demand.yards=getHouse()->getNumItems(Structure_ConstructionYard);
    demand.mcvsIncludingQueued=mcvsIncludingQueued;
    demand.yardLimit=yardLimit;
    return demand;
}

bool QuantBot::colonisationMcvDue(int mcvsIncludingQueued, int yardLimit) const {
    return QuantBotColonisationPolicy::due(colonisationDemand(mcvsIncludingQueued,yardLimit));
}

bool QuantBot::colonyMissionDue(const MCV* pMCV) const {
    // The MCV in hand may have been bought for anything; what decides its job
    // is the base it stands in. A built-out base sends it away, and so does a
    // formation that already runs its share of yards: stacking every yard on
    // one rock leaves the rest of the map unsettled. Only a surveyed
    // destination ever sends one across the map, under exactly the rules that
    // would have bought a colonist, so campaigns, helpers and capped games
    // keep the behaviour they had.
    auto demand=colonisationDemand(0,getGameInitSettings().getGameOptions().maximumNumberOfConstructionYardsOverride);
    // The formation flood only runs for an MCV that could actually leave.
    if(!QuantBotColonisationPolicy::colonisationAllowed(demand)||!demand.siteAvailable) return false;
    if(pMCV&&pMCV->getLocation().isValid()) demand.yardsOnFormation=yardsOnFormation(pMCV->getLocation());
    return QuantBotColonisationPolicy::missionDue(demand);
}

// The opening search, for an MCV that is not growing an existing city base:
// city growth and colonisation are chosen by manageMcv instead.
Coord QuantBot::findMcvPlaceLocation(const MCV* pMCV) {
    AITelemetry::PerformanceScope perfScope("ai.findMcvPlaceLocation", getGameCycleCount(), getHouse()->getHouseID());
	// Always search for best location near the MCV's current position
	// This works for both first MCV and expansion MCVs.
	//
	// Perf bound: the distance penalty (-10 per tile) makes any spot more
	// than ~30 tiles away strictly worse than a closer candidate, so a full
	// map scan is pointless. Cap the outer search to a window around the
	// MCV and the inner rock-count to a small radius — the inner is just an
	// "is there room here" heuristic, not a precise survey. Without these
	// caps this function is O(W*H*innerR^2) ~= 23M ops per call on a 192^2
	// map, and gets called per undeployed MCV per AI tick.
	constexpr int kOuterRadius = 25;
	constexpr int kInnerRadius = 6;

	int bestLocationScore = -10000;
	Coord bestLocation = Coord::Invalid();
	Coord mcvLocation = pMCV->getLocation();

	const int mapW = getMap().getSizeX();
	const int mapH = getMap().getSizeY();
	const int xLo = std::max(1, mcvLocation.x - kOuterRadius);
	const int xHi = std::min(mapW - 2, mcvLocation.x + kOuterRadius);
	const int yLo = std::max(1, mcvLocation.y - kOuterRadius);
	const int yHi = std::min(mapH - 2, mcvLocation.y + kOuterRadius);

	for (int placeLocationX = xLo; placeLocationX <= xHi; placeLocationX++) {
		for (int placeLocationY = yLo; placeLocationY <= yHi; placeLocationY++) {
			Coord placeLocation(placeLocationX, placeLocationY);

			if (getMap().okayToPlaceStructure(placeLocationX, placeLocationY, 2, 2, false, nullptr)
                && !overlapsReservedStructure(placeLocationX,placeLocationY,2,2)
                && preservesGroundAccess(Structure_ConstructionYard,placeLocation)) {
				int locationScore = 0;

				// Calculate distance penalty (closer is better)
				int distance = lround(blockDistance(mcvLocation, placeLocation));
				locationScore -= distance * 10;  // Strong penalty for distance - MCVs should deploy near where they spawn

				// Calculate available rock in the area (more buildable space is better)
				int availableRock = 0;

				for (int x = placeLocationX - kInnerRadius; x <= placeLocationX + kInnerRadius; x++) {
					for (int y = placeLocationY - kInnerRadius; y <= placeLocationY + kInnerRadius; y++) {
						if (getMap().tileExists(x, y)) {
							const Tile* pTile = getMap().getTile(x, y);
							// Count rock tiles that aren't mountains (buildable with concrete)
							if (pTile->isRock() && !pTile->isMountain() && !pTile->hasAGroundObject()) {
								availableRock++;
							}
						}
					}
				}

				// Score based on available rock
				// A 2x2 building needs 4 tiles, so 6 buildings = 24 tiles minimum
				// But we want more space for growth
				int buildingSites = availableRock / 4;  // Rough estimate of potential building count

				if (buildingSites >= 6) {
					// Location has room for 6+ buildings, give good base score
					locationScore += 200;
					// Additional bonus for even more space (diminishing returns)
					locationScore += (buildingSites - 6) * 5;
				} else {
					// Not enough space - heavy penalty
					locationScore += buildingSites * 15;  // Still give some credit
					locationScore -= 100;  // But penalize insufficient space heavily
				}

				// Bonus for being somewhat central but not too far
				// Prefer locations that aren't at extreme corners
				int distanceFromCenter = lround(blockDistance(placeLocation, 
					Coord(getMap().getSizeX() / 2, getMap().getSizeY() / 2)));
				int mapRadius = (getMap().getSizeX() + getMap().getSizeY()) / 4;

				if (distanceFromCenter < mapRadius / 2) {
					locationScore += 20;  // Bonus for being near map center
				}

				// Pick best location
				if (locationScore > bestLocationScore) {
					bestLocationScore = locationScore;
					bestLocation = placeLocation;
				}
			}
		}
	}

	if (bestLocation.isValid()) {
		logDebug("MCV deployment location found at (%d, %d) with score %d", 
			bestLocation.x, bestLocation.y, bestLocationScore);
	}

	return bestLocation;
}

std::vector<Coord> QuantBot::otherMcvSites(const MCV* mcv) const {
    // Read through the live unit list so a destroyed MCV cannot keep a tile
    // reserved, exactly as the rock expansion survey does.
    std::vector<Coord> sites;
    for(const auto* unit:getUnitList()) {
        if(unit->getOwner()!=getHouse()||unit->getItemID()!=Unit_MCV||unit==mcv) continue;
        const auto it=mcvExpansionSites.find(unit->getObjectID());
        if(it!=mcvExpansionSites.end()&&it->second.isValid()) sites.push_back(it->second);
    }
    return sites;
}

bool QuantBot::onOwnRockFormation(Coord site) const {
    if(site.isInvalid()) return false;
    const auto& map=getMap();
    const int houseID=getHouse()->getHouseID();
    const int w=map.getSizeX(),h=map.getSizeY();
    if(!map.tileExists(site.x,site.y)) return false;
    // Build range can bridge sand: ownership requires connected buildable rock.
    std::vector<bool> seen(static_cast<size_t>(w)*h,false);
    std::vector<int> queue{site.y*w+site.x};
    seen[queue.front()]=true;
    for(size_t at=0;at<queue.size();++at) {
        const int index=queue[at],x=index%w,y=index/w;
        const auto* tile=map.getTile(x,y);
        if(!tile->isRock()||tile->isMountain()) continue;
        if(tile->hasAStructure()&&tile->getOwner()==houseID) return true;
        const int steps[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
        for(const auto& step:steps) {
            const int nx=x+step[0],ny=y+step[1];
            if(!map.tileExists(nx,ny)||seen[ny*w+nx]) continue;
            const auto* next=map.getTile(nx,ny);
            if(!next->isRock()||next->isMountain()) continue;
            seen[ny*w+nx]=true;queue.push_back(ny*w+nx);
        }
    }
    return false;
}

namespace {
// Marks the rock formation containing \a start in \a seen and returns whether
// \a start is buildable rock at all. Formations are edge-connected rock
// without mountains, exactly as onOwnRockFormation() reads them.
bool markFormation(const Map& map, int start, std::vector<char>& seen) {
    const int w=map.getSizeX();
    const auto* first=map.getTile(start%w,start/w);
    if(!first||!first->isRock()||first->isMountain()) return false;
    std::vector<int> queue{start};
    seen[start]=1;
    for(size_t at=0;at<queue.size();++at) {
        const int x=queue[at]%w,y=queue[at]/w;
        const int steps[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
        for(const auto& step:steps) {
            const int nx=x+step[0],ny=y+step[1];
            if(!map.tileExists(nx,ny)||seen[ny*w+nx]) continue;
            const auto* next=map.getTile(nx,ny);
            if(!next->isRock()||next->isMountain()) continue;
            seen[ny*w+nx]=1;queue.push_back(ny*w+nx);
        }
    }
    return true;
}
} // namespace

int QuantBot::yardsOnFormation(Coord at) const {
    const auto& map=getMap();
    if(at.isInvalid()||!map.tileExists(at.x,at.y)) return 0;
    const int w=map.getSizeX();
    std::vector<char> seen(static_cast<size_t>(w)*map.getSizeY(),0);
    if(!markFormation(map,at.y*w+at.x,seen)) return 0;
    int yards=0;
    for(const auto* structure:getStructureList()) {
        if(structure->getOwner()!=getHouse()||structure->getItemID()!=Structure_ConstructionYard
            ||structure->getHealth()<=0) continue;
        const Coord pos=structure->getLocation();
        if(map.tileExists(pos.x,pos.y)&&seen[pos.y*w+pos.x]) ++yards;
    }
    return yards;
}

int QuantBot::yardFormations() const {
    const auto& map=getMap();
    const int w=map.getSizeX();
    std::vector<char> seen(static_cast<size_t>(w)*map.getSizeY(),0);
    int formations=0;
    for(const auto* structure:getStructureList()) {
        if(structure->getOwner()!=getHouse()||structure->getItemID()!=Structure_ConstructionYard
            ||structure->getHealth()<=0) continue;
        const Coord pos=structure->getLocation();
        if(!map.tileExists(pos.x,pos.y)||seen[pos.y*w+pos.x]) continue;
        if(markFormation(map,pos.y*w+pos.x,seen)) ++formations;
    }
    return formations;
}

bool QuantBot::mcvSiteUsable(const MCV* pMCV, Coord site) const {
    if(site.isInvalid()) return false;
    for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
        if(!getMap().tileExists(site.x+dx,site.y+dy)) return false;
        const auto* tile=getMap().getTile(site.x+dx,site.y+dy);
        // Permanent obstacles only. A unit crossing the site is transient and
        // must not throw away a destination the MCV is still driving towards.
        if(!tile->isRock()||tile->isMountain()||tile->hasCityZone()||tile->hasAStructure()) return false;
    }
    if(overlapsReservedStructure(site.x,site.y,2,2)) return false;
    for(const auto& taken:otherMcvSites(pMCV))
        if(CityPlacementPolicy::overlaps(site.x,site.y,2,2,taken.x,taken.y,2,2)) return false;
    return true;
}

Coord QuantBot::findLocalDeploySite(const MCV* pMCV, Coord current) {
    AITelemetry::PerformanceScope perfScope("ai.findLocalDeploySite", getGameCycleCount(), getHouse()->getHouseID());
    if(pMCV==nullptr||pMCV->getLocation().isInvalid()) return Coord::Invalid();
    const auto& map=getMap();
    const Coord centre=pMCV->getLocation();
    const int xLo=std::max(0,centre.x-kMcvLocalRadius),xHi=std::min(map.getSizeX()-1,centre.x+kMcvLocalRadius);
    const int yLo=std::max(0,centre.y-kMcvLocalRadius),yHi=std::min(map.getSizeY()-1,centre.y+kMcvLocalRadius);
    const int w=xHi-xLo+1,h=yHi-yLo+1;
    if(w<2||h<2) return Coord::Invalid();
    const int houseID=getHouse()->getHouseID();
    std::vector<McvDeployPolicy::Tile> tiles(static_cast<size_t>(w)*h);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const auto* tile=map.getTile(xLo+x,yLo+y);
        const auto* ground=tile->getNonInfantryGroundObject();
        auto& out=tiles[y*w+x];
        out.rock=tile->isRock()&&!tile->isMountain();
        out.free=!tile->hasAStructure()&&(!tile->hasAGroundObject()||ground==pMCV);
        out.passable=!tile->isMountain()&&!tile->hasAStructure();
        out.owned=tile->hasAStructure()&&tile->getOwner()==houseID;
        out.blocked=tile->hasCityZone()||overlapsReservedStructure(xLo+x,yLo+y,1,1)
            ||dangerAt(Coord(xLo+x,yLo+y))>0
            ||lostYardsNear(xLo+x,yLo+y,kRepeatedYardLossRadius)>=kRepeatedYardLossLimit;
    }
    // One yard per remembered site: two MCVs never drive at the same tile.
    for(const auto& taken:otherMcvSites(pMCV))
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            const int x=taken.x+dx-xLo,y=taken.y+dy-yLo;
            if(x>=0&&x<w&&y>=0&&y<h) tiles[y*w+x].blocked=true;
        }
    const int start=(centre.y-yLo)*w+(centre.x-xLo);
    int prefer=-1;
    if(current.isValid()&&current.x>=xLo&&current.x<=xHi&&current.y>=yLo&&current.y<=yHi)
        prefer=(current.y-yLo)*w+(current.x-xLo);
    // Ownership can lie outside this local window on the same formation.
    if(onOwnRockFormation(centre)) tiles[start].owned=true;
    if(std::none_of(tiles.begin(),tiles.end(),[](const auto& tile) { return tile.owned; }))
        return Coord::Invalid();
    const auto site=McvDeployPolicy::choose(w,h,tiles,start,kMcvDeployRoom,prefer,
        [&](int x,int y) {
            const Coord chosen(xLo+x,yLo+y);
            return preservesGroundAccess(Structure_ConstructionYard,chosen);
        });
    return site.valid() ? Coord(xLo+site.x,yLo+site.y) : Coord::Invalid();
}

bool QuantBot::mcvMayDeployHere(const MCV* pMCV, bool expansion) {
    const Coord at=pMCV->getLocation();
    if(!pMCV->canDeploy()||overlapsReservedStructure(at.x,at.y,2,2)
        || !preservesGroundAccess(Structure_ConstructionYard,at)) return false;
    // An opening yard keeps the original rule: a legal footprint is enough.
    if(!expansion) return true;
    if(dangerAt(at,Coord(2,2))>0
        || lostYardsNear(at.x,at.y,kRepeatedYardLossRadius)>=kRepeatedYardLossLimit) return false;
    // Growth on the rock the base already holds is ordinary building work. It
    // is covered by the base's own defences, so it must not wait for the
    // outlying-colony checklist (repair yard, high tech factory and three
    // rocket turrets over every expansion yard) the way a new formation does.
    if(onOwnRockFormation(at)) return true;
    // A mission that was already approved deploys where it was sent. The core
    // prerequisite is waived for exactly the site this MCV was given and for no
    // other tile, so a colonist that arrives after the base regains a little
    // room is not turned away from the formation it just crossed the map for.
    const auto assigned=mcvExpansionSites.find(pMCV->getObjectID());
    const bool approvedSite=assigned!=mcvExpansionSites.end()&&assigned->second==at;
    return expansionDefenceReady(approvedSite)&&!nearRecentStructureLoss(at.x,at.y,2,2);
}

void QuantBot::manageMcv(const MCV* pMCV) {
    if(planningBuilder!=NONE_ID) {
        planningBuilder=NONE_ID;
        clearPlacementCache();
    }
    const Uint32 id=pMCV->getObjectID();
    // Progress refreshes the retry timer; traffic gets a grace period after arrival.
    if(pMCV->isMoving()) {
        mcvSurveyCycles[id]=getGameCycleCount();
        return;
    }
    if(pMCV->wasForced()) return;
    const Coord location=pMCV->getLocation();
    const bool expansion=currentGame->isCitySimEnabled()&&getHouse()->getNumItems(Structure_ConstructionYard)>0;
    const Uint32 cycle=getGameCycleCount();
    const auto surveyed=mcvSurveyCycles.find(id);
    const bool stale=surveyed==mcvSurveyCycles.end()||cycle-surveyed->second>=MILLI2CYCLES(5000);

    auto remembered=[&]() {
        const auto it=mcvExpansionSites.find(id);
        return it!=mcvExpansionSites.end() ? it->second : Coord::Invalid();
    };
    Coord target=expansion ? remembered() : Coord::Invalid();
    const char* choice="remembered";
    bool surveyedNow=false;
    // After five seconds without movement, a blocked destination can be
    // replaced. Progress and brief traffic keep the existing assignment.
    const bool deployableHere=mcvMayDeployHere(pMCV,expansion);
    if(expansion&&(stale||(target.isValid()&&!mcvSiteUsable(pMCV,target)))) {
        mcvSurveyCycles[id]=cycle;
        surveyedNow=true;
        // Grow on the formation the base already stands on first; only when it
        // has no room left, or already runs its share of yards, does
        // colonising a new one apply, under its own gates. A stalled trip can
        // use the nearest clear footprint; moving MCVs retain their
        // assignments above.
        //
        // Either of those reverses the order: the colony is offered first and
        // the local search stays as the fallback, so an MCV with nowhere safe
        // to go still becomes a yard where it stands instead of waiting. The
        // first production yard on a formation still deploys on the spot.
        //
        // A trip already under way to a remote formation is kept while its
        // site is still a legal footprint. The survey re-runs every five
        // seconds and ranks formations by distance from the main base, and an
        // MCV that has left its home rock no longer stands on a full
        // formation, so without this it could be sent between two equally good
        // formations, or back home, for ever.
        const bool onTrip=target.isValid()&&mcvSiteUsable(pMCV,target)&&!onOwnRockFormation(target);
        const bool colonise=onTrip||colonyMissionDue(pMCV);
        Coord site=onTrip ? target : colonise ? findRockExpansionSite(pMCV) : Coord::Invalid();
        choice=site.isValid() ? "colony" : "local";
        if(!site.isValid()) {
            site=findLocalDeploySite(pMCV,(deployableHere||stale) ? Coord::Invalid() : target);
            if(!site.isValid()) {
                choice="expansion";
                if(!colonise) site=findRockExpansionSite(pMCV);
            }
            if(!site.isValid()) {
                // The local search has just failed for this MCV: there is no
                // usable yard footprint on the rock the base stands on, whatever
                // the base survey last measured. A core building that could only
                // ever be built at home must not keep the colonist parked, so
                // the remote survey runs once more with that prerequisite
                // waived. The delivered colonist in the reported match waited a
                // minute here because the base briefly reported room again.
                // A trip already under way never reaches this point: it was
                // kept above.
                site=findRockExpansionSite(pMCV,true);
                if(site.isValid()) choice="expansion_no_local";
            }
        }
        if(site.isValid()) {
            target=site;
            mcvExpansionSites[id]=site;
        } else if(!mcvSiteUsable(pMCV,target)) {
            target=Coord::Invalid();
            mcvExpansionSites.erase(id);
            choice="none";
        }
    }

    const bool atTarget=target.isValid()&&target==location;
    const bool ready=(!expansion||atTarget)&&deployableHere;
    // Read what the trace needs while the MCV still exists: a successful
    // deployment destroys it. The formation checks are only worth their flood
    // fills when a stalled MCV is actually being reported.
    const uint64_t signature=(static_cast<uint64_t>(target.isValid()?target.x+1:0)<<40)
        ^(static_cast<uint64_t>(target.isValid()?target.y+1:0)<<24)
        ^(static_cast<uint64_t>(ready?1:0)<<16)^(static_cast<uint64_t>(atTarget?1:0)<<8)
        ^static_cast<uint64_t>(expansion?1:0);
    const bool report=ready||lastMcvTrace[id]!=signature;
    const bool couldDeploy=pMCV->canDeploy();
    const bool ownFormation=report&&expansion&&onOwnRockFormation(location);
    const int formationYards=report&&expansion ? yardsOnFormation(location) : 0;
    const bool colonyMission=report&&colonyMissionDue(pMCV);
    const bool deployed=ready&&doDeploy(pMCV);
    if(deployed) {
        mcvExpansionSites.erase(id);
        mcvSurveyCycles.erase(id);
        rockSurveyCycle=std::numeric_limits<Uint32>::max();
        clearPlacementCache();
    } else if(!expansion&&stale) {
        mcvSurveyCycles[id]=cycle;
        const Coord pos=findMcvPlaceLocation(pMCV);
        if(pos.isValid()) doMove2Pos(pMCV,pos.x,pos.y,true);
    } else if(target.isValid()&&!atTarget&&surveyedNow) {
        // A colonist leaves its home rock only with its escort gathered beside
        // it; the escort is ordered to it in checkAllUnits meanwhile. Once off
        // the home rock it keeps going, escort or not: turning back would
        // strand it between the two formations.
        bool awaitingEscort=false;
        if(expansion&&!onOwnRockFormation(target)) {
            bool gathered=true;
            const int escorts=colonistEscortCount(pMCV,&gathered);
            awaitingEscort=(escorts==0||!gathered)&&onOwnRockFormation(location);
            traceDecision("colonist_escort",AITelemetry::Record().set("mcv",id)
                .set("x",location.x).set("y",location.y).set("target_x",target.x).set("target_y",target.y)
                .set("escorts",escorts).set("gathered",gathered).set("departing",!awaitingEscort));
        }
        // Reissue only at the retry cadence or after a permanent obstruction.
        if(!awaitingEscort) doMove2Pos(pMCV,target.x,target.y,true);
    }

    if(report) {
        lastMcvTrace[id]=signature;
        traceDecision("mcv_deployment",AITelemetry::Record().set("mcv",id)
            .set("x",location.x).set("y",location.y)
            .set("target_x",target.isValid()?target.x:-1).set("target_y",target.isValid()?target.y:-1)
            .set("site",choice).set("expansion",expansion).set("deployed",deployed)
            .set("at_target",atTarget).set("can_deploy",couldDeploy)
            .set("own_formation",ownFormation)
            .set("formation_yards",formationYards)
            .set("colony_mission",colonyMission)
            .set("base_free_footprints",availableBaseFootprints)
            .set("base_production_room_blocked",baseProductionRoomBlocked)
            .set("danger",dangerAt(location,Coord(2,2)))
            .set("defence_ready",expansionDefenceReady())
            .set("yards",getHouse()->getNumItems(Structure_ConstructionYard)));
    }
    if(deployed) lastMcvTrace.erase(id);
}

Uint32 QuantBot::mainConstructionYardID() const {
    // The oldest surviving yard anchors the main base and already sits inside
    // its defence.
    Uint32 mainYard = NONE_ID;
    for (const auto* structure : getStructureList())
        if (structure->getOwner() == getHouse() && structure->getHealth() > 0
            && structure->getItemID() == Structure_ConstructionYard
            && (mainYard == NONE_ID || structure->getObjectID() < mainYard))
            mainYard = structure->getObjectID();
    return mainYard;
}

QuantBot::ExpansionCoverFacts QuantBot::collectExpansionCoverFacts() const {
    // One pass yields both facts the cover test needs. The main-yard rule is
    // mainConstructionYardID()'s: the lowest object id among living owned
    // yards. The turret list keeps structure-list order, so the coverage
    // count below is the same count the per-call walk produced.
    ExpansionCoverFacts facts;
    facts.turretRadius =
        std::max(1,currentGame->objectData.data[Structure_RocketTurret][getHouse()->getHouseID()].weaponrange-1);
    for (const auto* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getHealth() <= 0) continue;
        const auto item = structure->getItemID();
        if (item == Structure_ConstructionYard
            && (facts.mainYardID == NONE_ID || structure->getObjectID() < facts.mainYardID))
            facts.mainYardID = structure->getObjectID();
        if (item == Structure_RocketTurret) facts.turrets.push_back(structure->getLocation());
    }
    return facts;
}

int QuantBot::expansionTurretsMissing(const StructureBase* yard, bool planned,
                                      const ExpansionCoverFacts& facts) const {
    // Reject anything that is not a construction yard before touching the
    // main-yard anchor: isExpansionYard() already returns false for every
    // other item, so this returns 0 on exactly the same inputs as before
    // while skipping the structure-list walk for the whole base.
    if (!yard || yard->getItemID()!=Structure_ConstructionYard
        || yard->getOwner()!=getHouse() || yard->getHealth()<=0
        || !isExpansionYard(yard->getItemID(),yard->getObjectID(),facts.mainYardID)) return 0;
    const int radius=facts.turretRadius;
    int coverage=0;
    for (const auto& turret : facts.turrets)
        if (RocketTurretPolicy::coversBuilding(turret,yard->getLocation(),yard->getStructureSize(),radius)) ++coverage;
    if (planned) for (const auto& entry:reservedStructures)
        if (entry.second.item==Structure_RocketTurret
            && RocketTurretPolicy::coversBuilding(entry.second.location,yard->getLocation(),yard->getStructureSize(),radius)) ++coverage;
    return std::max(0,3-coverage);
}

int QuantBot::expansionTurretsMissing(const StructureBase* yard, bool planned) const {
    // Single-shot callers keep the original signature and behaviour. The
    // original guard's cheap rejections run first, so an unowned or destroyed
    // structure still costs nothing and never triggers a structure-list walk.
    if (!yard || yard->getItemID()!=Structure_ConstructionYard
        || yard->getOwner()!=getHouse() || yard->getHealth()<=0) return 0;
    return expansionTurretsMissing(yard,planned,collectExpansionCoverFacts());
}

bool QuantBot::expansionDefenceReady(bool needsLocalSpace) const {
    if (gameMode != GameMode::Custom || !currentGame->isCitySimEnabled() || supportMode
        || getHouse()->getNumItems(Structure_ConstructionYard) == 0) return true;
    const auto& data = currentGame->objectData.data;
    const int house = getHouse()->getHouseID();
    // A base with nowhere left to put a production building cannot finish its
    // core where it stands: the missing factory is the very thing the colony
    // has to make room for. Waive that prerequisite only once the survey has
    // measured the base as built out, so the gate cannot close on itself.
    //
    // A caller that has just failed to find any site at home knows the same
    // thing first-hand, and more recently than the survey: the reported
    // colonist stood idle for a minute because the base measurement briefly
    // reported room again while its rock held no legal footprint at all.
    const bool builtOut = needsLocalSpace
        || (rockSurveyCycle != std::numeric_limits<Uint32>::max() && baseBuiltOut());
    // Complete the core first. Rocket cover is not a prerequisite: the new
    // rock has no turrets until a yard stands on it, and the yard is what
    // builds them there (expansionTurretsMissing() orders its three covers).
    // The escort protects the trip; a yard lost before its covers stand
    // leaves the rock free for the next one. Recovery of the only yard is exempt.
    if (!builtOut)
        for (Uint32 item : {Structure_HeavyFactory, Structure_HighTechFactory, Structure_RepairYard})
            if (data[item][house].enabled && data[item][house].techLevel <= currentGame->techLevel
                && getHouse()->getNumItems(item) == 0) return false;
    return true;
}

bool QuantBot::isExpansionYard(Uint32 item, Uint32 objectID, Uint32 mainYardID) {
    // Every yard other than the anchor — and every yard still only planned —
    // belongs to an expansion that has to be covered from scratch.
    return item == Structure_ConstructionYard && mainYardID != NONE_ID && objectID != mainYardID;
}

int QuantBot::lostYardsNear(int x, int y, int radius) const {
    // Yard losses stay in the retained history far longer than the placement
    // block, so a site that keeps eating construction yards stays recognisable
    // after the short-term block expires.
    int lost = 0;
    for (const auto& loss : recentStructureLosses)
        if (loss.item == Structure_ConstructionYard
            && std::max(std::abs(loss.location.x-x),std::abs(loss.location.y-y)) <= radius) ++lost;
    return lost;
}

int QuantBot::colonistsLost() const {
    return static_cast<int>(std::count_if(recentStructureLosses.begin(),recentStructureLosses.end(),
        [](const auto& loss) { return loss.item == Unit_MCV; }));
}

void QuantBot::recordLostColonists() {
    // Deployment erases an MCV's remembered site, so a site whose MCV no
    // longer exists belongs to one that never arrived. One bound for another
    // formation was a colonist lost on the way; it joins the retained loss
    // history (and ages out with it) at the site it was sent to, which keeps
    // the saved layout unchanged. Ids are handled in order so every peer
    // records the same history whatever its hash map iteration order.
    std::vector<Uint32> vanished;
    for (const auto& entry : mcvExpansionSites)
        if (currentGame->getObjectManager().getObject(entry.first) == nullptr) vanished.push_back(entry.first);
    std::sort(vanished.begin(), vanished.end());
    for (Uint32 id : vanished) {
        const Coord site = mcvExpansionSites[id];
        if (site.isValid() && !onOwnRockFormation(site)) {
            recentStructureLosses.push_back({site, Coord(2,2), getGameCycleCount(), static_cast<Uint32>(Unit_MCV)});
            traceDecision("colonist_lost", AITelemetry::Record().set("mcv", id).set("x", site.x).set("y", site.y)
                .set("colonists_lost", colonistsLost()));
        }
        mcvExpansionSites.erase(id);
        mcvSurveyCycles.erase(id);
        lastMcvTrace.erase(id);
    }
}

const std::map<Uint32,Uint32>& QuantBot::colonistEscorts() const {
    const Uint32 now = getGameCycleCount();
    if (colonistEscortCycle == now) return colonistEscortSet;
    colonistEscortCycle = now;
    colonistEscortSet.clear();
    if (currentGame == nullptr || supportMode || gameMode != GameMode::Custom
        || !currentGame->isCitySimEnabled()) return colonistEscortSet;
    // A colonist is an MCV on its way to another formation: its saved site
    // says so. In id order, so every peer gives escorts out the same way.
    std::vector<const UnitBase*> colonists;
    for (const auto* unit : getUnitList()) {
        if (unit->getOwner() != getHouse() || unit->getItemID() != Unit_MCV || unit->getHealth() <= 0) continue;
        const auto site = mcvExpansionSites.find(unit->getObjectID());
        if (site == mcvExpansionSites.end() || site->second.isInvalid() || onOwnRockFormation(site->second)) continue;
        colonists.push_back(unit);
    }
    if (colonists.empty()) return colonistEscortSet;
    std::sort(colonists.begin(), colonists.end(),
        [](const UnitBase* a, const UnitBase* b) { return a->getObjectID() < b->getObjectID(); });
    // Free vehicles only: never an attacker already committed, an emergency
    // defender, a repair run or a human order. Infantry cannot keep pace.
    std::vector<const UnitBase*> pool;
    for (const auto* unit : getUnitList()) {
        if (!orderableCombatUnit(unit) || !unit->isAGroundUnit() || unit->isInfantry()) continue;
        if (unitRole(unit, {UnitRole::Repair, UnitRole::Defence, UnitRole::Wave}) != UnitRole::Free) continue;
        if (unit->getAttackMode() == HUNT || unit->getAttackMode() == RETREAT) continue;
        pool.push_back(unit);
    }
    std::vector<bool> taken(pool.size(), false);
    for (const auto* colonist : colonists) {
        struct Pick { size_t index; int distance; Uint32 id; };
        std::vector<Pick> picks;
        for (size_t i = 0; i < pool.size(); ++i)
            if (!taken[i]) picks.push_back({i, blockDistance(colonist->getLocation(), pool[i]->getLocation()).lround(),
                pool[i]->getObjectID()});
        std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) {
            return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
        });
        for (size_t i = 0; i < picks.size() && i < size_t(kColonistEscortSize); ++i) {
            taken[picks[i].index] = true;
            colonistEscortSet[picks[i].id] = colonist->getObjectID();
        }
    }
    return colonistEscortSet;
}

int QuantBot::colonistEscortCount(const UnitBase* mcv, bool* gathered) const {
    if (gathered) *gathered = true;
    if (mcv == nullptr) return 0;
    int escorts = 0;
    for (const auto& entry : colonistEscorts()) {
        if (entry.second != mcv->getObjectID()) continue;
        ++escorts;
        const auto* unit = getObject(entry.first);
        if (gathered && (!unit || blockDistance(unit->getLocation(), mcv->getLocation()).lround() > kEscortAssemblyRadius))
            *gathered = false;
    }
    return escorts;
}

bool QuantBot::escortColonist(const UnitBase* unit) {
    if (unit == nullptr || unit->getOwner() != getHouse()) return false;
    const auto& escorts = colonistEscorts();
    const auto entry = escorts.find(unit->getObjectID());
    if (entry == escorts.end()) return false;
    const auto* colonist = getObject(entry->second);
    if (colonist == nullptr) return false;
    // Area Guard, never a forced march: the escort turns on whatever comes near it on the way
    // and at the new rock, and re-forms afterwards.
    if (unit->getAttackMode() != AREAGUARD) doSetAttackMode(unit, AREAGUARD);
    // The escort forms round the colonist as a moving anchor (docs/formations.md): a ring
    // facing where the MCV is going, re-planned from its tile whenever it moves on. The MCV
    // keeps its own order and path; the escort only follows its ring places, so it travels
    // at the MCV's speed. Sonic escorts take the front places, so no escort and not the MCV
    // stands ahead of one. The guard point follows the ring place, so Area Guard never
    // pulls the escort back onto the MCV.
    const Coord ringTile = colonistEscortPlace(unit, colonist);
    const_cast<UnitBase*>(unit)->setGuardPoint(ringTile.isValid() ? ringTile : colonist->getLocation());
    if (unit->hasATarget()) return true;
    if (ringTile.isInvalid() || unit->getLocation() == ringTile || unit->getDestination() == ringTile) return true;
    if (currentGame->getPathRequestQueueSize() > 150) return true;
    doMove2Pos(unit, ringTile.x, ringTile.y, false);
    return true;
}

Coord QuantBot::colonistEscortPlace(const UnitBase* escort, const ObjectBase* colonist) const {
    const auto* mcv = dynamic_cast<const UnitBase*>(colonist);
    if (mcv == nullptr) return Coord::Invalid();
    // Heading: where the MCV is driving, else towards its colony site, else north.
    const Coord here = mcv->getLocation();
    Coord ahead = mcv->getDestination();
    if (ahead.isInvalid() || ahead == here) {
        const auto site = mcvExpansionSites.find(mcv->getObjectID());
        ahead = site != mcvExpansionSites.end() ? site->second : Coord::Invalid();
    }
    Coord forward = (ahead.isValid() && ahead != here) ? FormationPlanner::facing(here, ahead) : Coord(0, -1);
    if (forward == Coord(0, 0)) forward = Coord(0, -1);
    const Coord lateral(-forward.y, forward.x);
    // This colonist's escorts: sonic units first, then by object id (the map's order).
    std::vector<const UnitBase*> ring;
    for (const auto& entry : colonistEscorts())
        if (entry.second == mcv->getObjectID())
            if (const auto* member = dynamic_cast<const UnitBase*>(getObject(entry.first))) ring.push_back(member);
    std::stable_sort(ring.begin(), ring.end(), [](const UnitBase* a, const UnitBase* b) {
        return FormationPlanner::isSonic(a->getItemID()) && !FormationPlanner::isSonic(b->getItemID());
    });
    // Places as (ahead, across): the two front corners first (each covers the front and a
    // side; three tiles ahead, so an escort still leads while the MCV closes up between
    // re-plans), then behind, then the flanks. Sonic escorts only take front-row places.
    constexpr int kFrontRow = 3;
    static const std::pair<int, int> places[] = {{kFrontRow, -2}, {kFrontRow, 2}, {-2, 0}, {0, -2}, {0, 2}, {-2, -2}, {-2, 2},
                                                 {kFrontRow, -4}, {kFrontRow, 4}};
    std::vector<bool> taken(std::size(places), false);
    for (const UnitBase* member : ring) {
        const bool sonic = FormationPlanner::isSonic(member->getItemID());
        size_t pick = std::size(places);
        for (size_t i = 0; i < std::size(places) && pick == std::size(places); ++i)
            if (!taken[i] && (!sonic || places[i].first == kFrontRow)) pick = i;
        if (pick == std::size(places)) {
            if (member == escort) return Coord::Invalid();
            continue;
        }
        taken[pick] = true;
        if (member != escort) continue;
        const Coord wanted(here.x + forward.x * places[pick].first + lateral.x * places[pick].second,
                           here.y + forward.y * places[pick].first + lateral.y * places[pick].second);
        if (!currentGameMap->tileExists(wanted)) return Coord::Invalid();
        return FormationOrders::revalidatedSlot(escort, wanted);
    }
    return Coord::Invalid();
}
