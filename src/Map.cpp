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

#include <Map.h>
#include <DynastyProjectile.h>
#include <GasDeviationPolicy.h>
#include <OrdinaryDamagePolicy.h>
#include <dunecity/CityConstants.h>

#include <globals.h>

#include <Game.h>
#include <House.h>
#include <ScreenBorder.h>
#include <sand.h>
#include <mod/ModManager.h>

#include <units/UnitBase.h>
#include <units/InfantryBase.h>
#include <units/AirUnit.h>
#include <structures/StructureBase.h>

#include <climits>
#include <algorithm>
#include <stack>
#include <set>

namespace {

bool isTornieModActive() {
    return ModManager::instance().isInitialized() && ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) == "Tornie";
}

} // namespace

Map::Map(int xSize, int ySize)
 : sizeX(xSize), sizeY(ySize), lastSinglySelectedObject(nullptr), pathingRevision(0) {

    tiles.resize(sizeX * sizeY);

    init_tile_location();
}


Map::~Map() = default;

void Map::load(InputStream& stream) {
    invalidateTerrainConnectivity();
    sizeX = stream.readSint32();
    sizeY = stream.readSint32();

    tiles.clear();
    tiles.resize(sizeX * sizeY);

    for (auto& tile : tiles)
        tile.load(stream);

    init_tile_location();
}

void Map::save(OutputStream& stream) const {
    stream.writeSint32(sizeX);
    stream.writeSint32(sizeY);

    for (auto& tile : tiles)
        tile.save(stream);
}

void Map::init_tile_location() {
    for (auto i = 0; i < sizeX; ++i) {
        for (auto j = 0; j < sizeY; ++j) {
            tiles[tile_index(i, j)].location = Coord(i, j);
        }
    }
}

void Map::incrementPathingRevision() noexcept {
    ++pathingRevision;
}

void Map::ensureTerrainConnectivity() const {
    if (!vehicleTerrainRegions.empty()) return;
    vehicleTerrainRegions.assign(tiles.size(), 0);
    ++terrainConnectivityBuilds;
    std::vector<Coord> pending;
    pending.reserve(tiles.size());
    int region=0;
    for (const auto& tile:tiles) {
        if (tile.isMountain() || vehicleTerrainRegions[tile_index(tile.location.x,tile.location.y)]) continue;
        ++region;
        pending.clear();
        pending.push_back(tile.location);
        vehicleTerrainRegions[tile_index(tile.location.x,tile.location.y)]=region;
        for (size_t i=0;i<pending.size();++i) {
            const Coord point=pending[i];
            // Match A*'s eight-neighbour movement, including diagonal openings.
            for (int angle=0;angle<NUM_ANGLES;++angle) {
                const Coord next=getMapPos(angle,point);
                if (!tileExists(next)) continue;
                const int index=tile_index(next.x,next.y);
                if (vehicleTerrainRegions[index] || tiles[index].isMountain()) continue;
                vehicleTerrainRegions[index]=region;
                pending.push_back(next);
            }
        }
    }
}

bool Map::terrainAttackReachable(const ObjectBase& seeker, const ObjectBase& target) const {
    // Infantry climb mountains; flying/underground units have their own rules.
    if (!seeker.isAGroundUnit() || seeker.isInfantry() || seeker.getItemID()==Unit_Sandworm) return true;
    const Coord from=seeker.getLocation(), goal=target.getClosestPoint(from);
    if (!tileExists(from) || !tileExists(goal)) return false;
    const int range=seeker.getWeaponRange();
    if (blockDistance(from,goal)<=range) return true; // Can already shoot across a ridge.
    ensureTerrainConnectivity();
    const int region=vehicleTerrainRegions[tile_index(from.x,from.y)];
    if (!region) return false;
    if (vehicleTerrainRegions[tile_index(goal.x,goal.y)]==region) return true;
    // Reaching a firing position is sufficient: the target itself may stand
    // on mountains, or a building may straddle several terrain components.
    const Coord origin=target.getLocation();
    const Coord size=target.isAStructure()
        ? static_cast<const StructureBase&>(target).getStructureSize() : Coord(1,1);
    for (int y=std::max(0,origin.y-range);y<std::min(sizeY,origin.y+size.y+range);++y)
        for (int x=std::max(0,origin.x-range);x<std::min(sizeX,origin.x+size.x+range);++x) {
            if (vehicleTerrainRegions[tile_index(x,y)]!=region) continue;
            const Coord point(x,y);
            if (blockDistance(point,target.getClosestPoint(point))<=range) return true;
        }
    return false;
}

void Map::createSandRegions() {
    std::stack<Tile*> tileQueue;
    std::vector<bool> visited(tiles.size());

    for (auto& tile : tiles)
        tile.setSandRegion(NONE_ID);

    Uint32 region = 0;
    for (auto& tile : tiles) {
        if (!tile.isRock() && !visited[tile_index(tile.location.x, tile.location.y)]) {
            tileQueue.push(&tile);

            while (!tileQueue.empty()) {
                const auto pTile = tileQueue.top();
                tileQueue.pop();

                pTile->setSandRegion(region);
                for (auto angle = 0; angle < NUM_ANGLES; angle++) {
                    const auto pos = getMapPos(angle, pTile->location);

                    if (!tileExists(pos))
                        continue;

                    const auto index = tile_index(pos.x, pos.y);

                    const auto tile_angle = &tiles[index];

                    if (!tile_angle->isRock() && !visited[index]) {
                        tileQueue.push(tile_angle);
                        visited[index] = true;
                    }
                }
            }
            region++;
        }
    }
}

void Map::damage(Uint32 damagerID, House* damagerOwner, const Coord& realPos, Uint32 bulletID, FixPoint damage, int damageRadius, bool air, bool affectTerrain, Uint32 interceptedAirUnit,
                 const DeviationReward::Provenance& provenance) {
    const bool dynastyBlast = DynastyProjectile::parameters(bulletID).step != 0
        || (bulletID == Bullet_ShellTurret && air);
    const auto blastDistance = [&](Coord point) {
        return dynastyBlast ? DynastyProjectile::distance(point*4, realPos*4)/4
                            : lround(distanceFrom(point, realPos));
    };
    // An ordinary gun, shell or rocket impact scores its ground victims from
    // OrdinaryDamagePolicy instead. This is a separate question from dynastyBlast, which also
    // decides whether the air layer is reachable at all: the anti-air behaviour, the target
    // eligibility and every special path below are left exactly as they were.
    const bool ordinaryGroundBlast = OrdinaryDamagePolicy::ordinaryBlast(bulletID);
    // The weapon category follows the shot, not its shooter's present owner. The provenance
    // snapshot records the natural source type at firing time, so it survives the shooter's
    // death, its reversion and its recapture. Shots without a snapshot - a save written before
    // provenance existed, or a direct engine call - fall back to the live damager.
    const Uint32 ordinarySourceItemID = [&]() -> Uint32 {
        if(!ordinaryGroundBlast) return NONE_ID;
        if(provenance.sourceItemID != NONE_ID) return provenance.sourceItemID;
        if(damagerID == NONE_ID) return NONE_ID;
        const auto* pDamager = currentGame->getObjectManager().getObject(damagerID);
        return pDamager ? static_cast<Uint32>(pDamager->getItemID()) : NONE_ID;
    }();
    const auto location = Coord(realPos.x/TILESIZE, realPos.y/TILESIZE);

    std::set<Uint32>    affectedAirUnits;
    std::set<Uint32>    affectedGroundAndUndergroundUnits;

    for(auto i = location.x-2; i <= location.x+2; i++) {
        for(auto j = location.y-2; j <= location.y+2; j++) {
            const auto pTile = getTile_internal(i,j);

            if (!pTile)
                continue;

            affectedAirUnits.insert(pTile->getAirUnitList().begin(), pTile->getAirUnitList().end());
            affectedGroundAndUndergroundUnits.insert(pTile->getInfantryList().begin(), pTile->getInfantryList().end());
            affectedGroundAndUndergroundUnits.insert(pTile->getUndergroundUnitList().begin(), pTile->getUndergroundUnitList().end());
            affectedGroundAndUndergroundUnits.insert(pTile->getNonInfantryGroundObjectList().begin(), pTile->getNonInfantryGroundObjectList().end());
        }
    }

    if(bulletID == Bullet_Sandworm) {
        for(auto objectID : affectedGroundAndUndergroundUnits) {
            auto pObject = currentGame->getObjectManager().getObject(objectID);
            if(pObject
                && (pObject->getItemID() != Unit_Sandworm)
                && (pObject->isAGroundUnit() || pObject->isInfantry())
                && (pObject->getLocation() == location))
            {
                pObject->setVisible(VIS_ALL, false);
                pObject->handleDamage(lround(damage), damagerID, damagerOwner, provenance);
            }
        }
    } else {
        if(air || dynastyBlast) {
            // air damage
            if(dynastyBlast || bulletID == Bullet_DRocket || bulletID == Bullet_Rocket || bulletID == Bullet_TurretRocket || bulletID == Bullet_SmallRocket) {
                for(auto objectID : affectedAirUnits) {
                    auto pAirUnit = dynamic_cast<AirUnit*>(currentGame->getObjectManager().getObject(objectID));
                    if(pAirUnit == nullptr || (dynastyBlast && pAirUnit->getItemID() == Unit_Frigate))
                        continue;

                    const auto centerPoint = pAirUnit->getCenterPoint();
                    // A swept missile collision occurred earlier within this movement
                    // interval. The aircraft has already advanced to the frame endpoint;
                    // use direct-hit damage for that proven contact only.
                    const auto distance = objectID == interceptedAirUnit ? 0 : blastDistance(centerPoint);

                    if(dynastyBlast ? distance >= damageRadius : distance > damageRadius)
                        continue;

                    if(bulletID == Bullet_DRocket) {
                        // Gas converts crewed aircraft, Ornithopters and Carryalls
                        // included; it never removes hit points. One synchronized draw
                        // per eligible target keeps lockstep peers in step.
                        if(GasDeviationPolicy::eligibleTarget(pAirUnit->getItemID())) {
                            if(currentGame->randomGen.randFixPoint() < GasDeviationPolicy::conversionChance()) {
                                pAirUnit->deviate(damagerOwner, provenance.known() ? provenance.sourceObjectID : damagerID);
                            }
                        }
                    } else {
                        const int scaledDamage = lround(damage) >> (distance/(TILESIZE/4));
                        const auto healthBefore = pAirUnit->getHealth();
                        pAirUnit->handleDamage(scaledDamage, damagerID, damagerOwner, provenance);
                        
                        // MULTIPLAYER-SAFE: Track rocket hits/kills on ornithopters (after damage)
                        if(pAirUnit->getItemID() == Unit_Ornithopter && healthBefore > 0) {
                            if(bulletID == Bullet_TurretRocket) {
                                currentGame->combatStats.turretRocketsHitOrni++;
                                if(pAirUnit->getHealth() <= 0) {
                                    currentGame->combatStats.turretRocketsKillOrni++;
                                }
                            } else if(bulletID == Bullet_Rocket || bulletID == Bullet_DRocket) {
                                currentGame->combatStats.launcherRocketsHitOrni++;
                                if(pAirUnit->getHealth() <= 0) {
                                    currentGame->combatStats.launcherRocketsKillOrni++;
                                }
                            }
                        }
                    }
                }
            }
        }
        if(!air || dynastyBlast) {
            // Dynasty explosions affect nearby units irrespective of the aimed layer.
            for(auto objectID : affectedGroundAndUndergroundUnits) {
                const auto pObject = currentGame->getObjectManager().getObject(objectID);

                if(pObject && pObject->isAStructure()) {
                    auto pStructure = static_cast<StructureBase*>(pObject);

                    const auto topLeftCorner = pStructure->getLocation()*TILESIZE;
                    const auto bottomRightCorner = topLeftCorner + pStructure->getStructureSize()*TILESIZE;

                    if(bulletID != Bullet_DRocket && realPos.x >= topLeftCorner.x && realPos.y >= topLeftCorner.y && realPos.x < bottomRightCorner.x && realPos.y < bottomRightCorner.y) {
                        pStructure->handleDamage(lround(damage), damagerID, damagerOwner, provenance);

                        if( (bulletID == Bullet_LargeRocket || bulletID == Bullet_Rocket || bulletID == Bullet_TurretRocket || bulletID == Bullet_SmallRocket || bulletID == Bullet_Flame)
                            && (pStructure->getHealth() < pStructure->getMaxHealth()/2)) {
                            if(pStructure->getNumSmoke() < 5) {
                                pStructure->addSmoke(realPos, currentGame->getGameCycleCount());
                            }
                        }
                    }
                } else if(pObject && pObject->isAUnit()) {
                    const auto pUnit = static_cast<UnitBase*>(pObject);

                    const auto centerPoint = pUnit->getCenterPoint();
                    // Ordinary impacts use the Dynasty blast metric over a full tile whether or
                    // not the projectile flies a Dynasty trajectory, so a shell splashes as far
                    // as a rocket does. The special paths keep their own per-bullet radius.
                    const auto distance = ordinaryGroundBlast ? OrdinaryDamagePolicy::distance(centerPoint, realPos)
                                                              : blastDistance(centerPoint);
                    const bool withinBlast = ordinaryGroundBlast
                        ? OrdinaryDamagePolicy::withinBlast(distance)
                        : (dynastyBlast ? distance < damageRadius : distance <= damageRadius);

                    if(withinBlast) {
                        // MULTIPLAYER-SAFE: Track rocket hits on ornithopters (before damage)
                        const bool isOrni = (pUnit->getItemID() == Unit_Ornithopter);
                        const FixPoint healthBefore = isOrni ? pUnit->getHealth() : 0;
                        
                        if(bulletID == Bullet_DRocket) {
                            // Same shared eligibility list and chance as the air branch.
                            if(GasDeviationPolicy::eligibleTarget(pUnit->getItemID())) {
                                if(currentGame->randomGen.randFixPoint() < GasDeviationPolicy::conversionChance()) {
                                    pUnit->deviate(damagerOwner, provenance.known() ? provenance.sourceObjectID : damagerID);
                                }
                            }
                        } else if(bulletID == Bullet_Sonic || bulletID == Bullet_SonicTrike) {
                            pUnit->handleDamage(lround(damage), damagerID, damagerOwner, provenance);
                        } else if(bulletID == Bullet_Flame) {
                            auto scaledDamage = lround(damage) >> (distance/16 + 1);
                            if(pUnit->isInfantry()) {
                                scaledDamage = std::max<int>(lround(damage), scaledDamage * 2);
                            }
                            pUnit->handleDamage(scaledDamage, damagerID, damagerOwner, provenance);
                        } else if(ordinaryGroundBlast) {
                            const auto scaledDamage = OrdinaryDamagePolicy::damageAt(
                                bulletID, ordinarySourceItemID, lround(damage), distance);
                            pUnit->handleDamage(scaledDamage, damagerID, damagerOwner, provenance);
                        } else {
                            const auto scaledDamage = lround(damage) >> (distance/(TILESIZE/4) + (dynastyBlast ? 0 : 1));
                            pUnit->handleDamage(scaledDamage, damagerID, damagerOwner, provenance);
                        }
                        
                        // MULTIPLAYER-SAFE: Track rocket hits/kills on ornithopters (after damage)
                        if(isOrni && healthBefore > 0) {
                            if(bulletID == Bullet_TurretRocket) {
                                currentGame->combatStats.turretRocketsHitOrni++;
                                if(pUnit->getHealth() <= 0) {
                                    currentGame->combatStats.turretRocketsKillOrni++;
                                }
                            } else if(bulletID == Bullet_Rocket || bulletID == Bullet_DRocket) {
                                currentGame->combatStats.launcherRocketsHitOrni++;
                                if(pUnit->getHealth() <= 0) {
                                    currentGame->combatStats.launcherRocketsKillOrni++;
                                }
                            }
                        }
                    }
                }
            }

            const auto pTile = currentGameMap->getTile_internal(location.x, location.y);

            if(affectTerrain && pTile
                && ((bulletID == Bullet_Rocket) || (bulletID == Bullet_TurretRocket) || (bulletID == Bullet_SmallRocket) || (bulletID == Bullet_LargeRocket) || (bulletID == Bullet_Flame))
                && (!pTile->hasAGroundObject() || !pTile->getGroundObject()->isAStructure()) )
            {
                const auto type = pTile->getType();

                if(((type == Terrain_Rock) && (pTile->getTerrainTile() == Tile::TerrainTile_RockFull)) || (type == Terrain_Slab)) {
                    if(type == Terrain_Slab) {
                        pTile->setType(Terrain_Rock);
                        pTile->setDestroyedStructureTile(Destroyed1x1Structure);
                        pTile->setOwner(NONE_ID);
                    }

                    pTile->addDamage(Tile::Terrain_RockDamage, (bulletID==Bullet_SmallRocket) ? Tile::RockDamage1 : Tile::RockDamage2, realPos);

                } else if((type == Terrain_Sand) || pTile->isSpice()) {
                    const auto damage_tile = bulletID == Bullet_SmallRocket
                        ? currentGame->randomGen.rand(Tile::SandDamage1, Tile::SandDamage2)
                        : currentGame->randomGen.rand(Tile::SandDamage3, Tile::SandDamage4);

                    pTile->addDamage(Tile::Terrain_SandDamage, damage_tile, realPos);
                }
            }

            // Roads are tile flags, not structures — destroy them on sufficient impact.
            if (affectTerrain && pTile && pTile->isRoad() && damage >= 10) {
                pTile->setRoad(false);
                pTile->setDestroyedStructureTile(Destroyed1x1Structure);
            }
        }
    }

    if (affectTerrain && (bulletID != Bullet_Sonic) && (bulletID != Bullet_SonicTrike) && (bulletID != Bullet_Sandworm)) {
        const auto tile = getTile_internal(location.x, location.y);

        if (tile && tile->isSpiceBloom()) {
            tile->triggerSpiceBloom(damagerOwner);
        }
    }
}

/**
    Check each tile which surrounds the building location to make sure there
    is no building. We want to ensure we don't block units in and have
    traffic lanes for our troops
**/

bool Map::isAStructureGap(int x, int y, int buildingSizeX, int buildingSizeY) const {

    // Spacing rules don't apply for rocket turrets
    if(buildingSizeX == 1){
        return true;
    }

    const auto xMin = x - 1;
    const auto xMax = x + buildingSizeX + 1;
    const auto yMin = y - 1;
    const auto yMax = y + buildingSizeY + 1;

    for(auto i = xMin; i < xMax; i++) {
        for(auto j = yMin; j < yMax; j++) {
            if(!((i == xMin || i == xMax) && (j == yMin || j == yMax))) { //Corners are ok as units can get through

                const auto pTile = getTile_internal(i,j);

                if(pTile && (pTile->hasAStructure() && !pTile->isConcrete())) { // I need some more conditions to make it ignore units

                    return false;
                }
            }
        }
    }

    return true;
}

bool Map::okayToPlaceStructure(int x, int y, int buildingSizeX, int buildingSizeY, bool tilesRequired, const House* pHouse, bool bIgnoreUnits) const {
    bool withinBuildRange = false;

    for(auto i = x; i < x + buildingSizeX; i++) {
        for(auto j = y; j < y + buildingSizeY; j++) {

            const auto pTile = getTile_internal(i,j);

            if (!pTile)
                return false;

            // Block placement on city zone tiles (residential/commercial/industrial).
            // Road tiles are allowed — the road flag will be cleared on placement.
            if(pTile->hasCityZone()) {
                return false;
            }

            if(!pTile->isRock() || (tilesRequired && !pTile->hasPreparedFoundation()) || (!bIgnoreUnits && pTile->isBlocked())) {
                return false;
            }

            if((pHouse == nullptr) || isWithinBuildRange(i, j, pHouse)) {
                withinBuildRange = true;
            }
        }
    }
    return withinBuildRange;
}

bool Map::okayToPlaceStructure(int x, int y, int buildingSizeX, int buildingSizeY, bool tilesRequired, const House* pHouse, bool bIgnoreUnits, int itemID) const {
    if(!DuneCity::isCityOnlyStructure(itemID)) {
        return okayToPlaceStructure(x, y, buildingSizeX, buildingSizeY, tilesRequired, pHouse, bIgnoreUnits);
    }

    bool withinBuildRange = false;
    const bool zoneStructure = DuneCity::isCityZoneStructure(itemID);
    int anchoredTiles = 0;

    for(auto i = x; i < x + buildingSizeX; i++) {
        for(auto j = y; j < y + buildingSizeY; j++) {
            const auto pTile = getTile_internal(i, j);

            if(!pTile)
                return false;

            // DuneCity structures cannot overlap existing city zones.
            if(pTile->hasCityZone()) {
                return false;
            }

            // Zones may spill onto sand as long as one tile anchors on rock or
            // slab; other city-only structures keep the Dune substrate rule.
            // Spice, blooms and mountains stay out of every city placement.
            const uint32_t terrain = pTile->getType();
            const bool terrainOk = zoneStructure
                ? DuneCity::isCityZoneTerrain(terrain)
                : DuneCity::isCityBuildableTerrain(terrain);
            if(!terrainOk || (!bIgnoreUnits && pTile->isBlocked())) {
                return false;
            }
            if(DuneCity::isCityBuildableTerrain(terrain)) {
                anchoredTiles++;
            }

            if((pHouse == nullptr) || isWithinBuildRange(i, j, pHouse)) {
                withinBuildRange = true;
            }
        }
    }
    if(zoneStructure && anchoredTiles == 0) {
        return false;
    }
    return withinBuildRange;
}

bool Map::isWithinBuildRange(int x, int y, const House* pHouse) const {
    for (auto i = x - BUILDRANGE; i <= x + BUILDRANGE; i++) {
        for (auto j = y - BUILDRANGE; j <= y + BUILDRANGE; j++) {
            const auto tile = getTile_internal(i, j);

            if (tile && DuneCity::isConstructionAnchor(tile->getOwner(), pHouse->getHouseID()))
                return true;
        }
    }

    return false;
}

/**
    This method figures out the direction of tile pos relative to tile source.
    \param  source  the starting point
    \param  pos     the destination
    \return one of RIGHT, RIGHTUP, UP, LEFTUP, LEFT, LEFTDOWN, DOWN, RIGHTDOWN or INVALID
*/
int Map::getPosAngle(const Coord& source, const Coord& pos) {
    if(pos.x > source.x) {
        if(pos.y > source.y) {
            return RIGHTDOWN;
        } else if(pos.y < source.y) {
            return RIGHTUP;
        } else {
            return RIGHT;
        }
    } else if(pos.x < source.x) {
        if(pos.y > source.y) {
            return LEFTDOWN;
        } else if(pos.y < source.y) {
            return LEFTUP;
        } else {
            return LEFT;
        }
    } else {
        if(pos.y > source.y) {
            return DOWN;
        } else if(pos.y < source.y) {
            return UP;
        } else {
            return INVALID;
        }
    }
}

/**
    This method calculates the coordinate of one of the neighbor tiles of source
    \param  angle   one of RIGHT, RIGHTUP, UP, LEFTUP, LEFT, LEFTDOWN, DOWN, RIGHTDOWN
    \param  source  the tile to calculate neighbor tiles from
*/
Coord Map::getMapPos(int angle, const Coord& source) {
    switch (angle)
    {
        case (RIGHT):       return Coord(source.x + 1 , source.y);       break;
        case (RIGHTUP):     return Coord(source.x + 1 , source.y - 1);   break;
        case (UP):          return Coord(source.x     , source.y - 1);   break;
        case (LEFTUP):      return Coord(source.x - 1 , source.y - 1);   break;
        case (LEFT):        return Coord(source.x - 1 , source.y);       break;
        case (LEFTDOWN):    return Coord(source.x - 1 , source.y + 1);   break;
        case (DOWN):        return Coord(source.x     , source.y + 1);   break;
        case (RIGHTDOWN):   return Coord(source.x + 1 , source.y + 1);   break;
        default:            return Coord(source.x     , source.y);       break;
    }
}

//building size is num squares
Coord Map::findDeploySpot(UnitBase* pUnit, const Coord& origin, Random& randomGen, const Coord& gatherPoint, const Coord& buildingSize) const {
    auto closestDistance = FixPt_MAX;
    Coord       closestPoint;
    Coord       size;

    auto found = false;
    auto foundClosest = false;

    auto counter = 0;
    auto depth = 0;

    if(pUnit->isAFlyingUnit()) {
        return origin;
    }

    auto ranX = origin.x;
    auto ranY = origin.y;

    do {
        const auto edge = randomGen.rand(0, 3);
        switch(edge) {
            case 0: //right edge
                ranX = origin.x + buildingSize.x + depth;
                ranY = randomGen.rand(origin.y - depth, origin.y + buildingSize.y + depth);
                break;
            case 1: //top edge
                ranX = randomGen.rand(origin.x - depth, origin.x + buildingSize.x + depth);
                ranY = origin.y - depth - ((buildingSize.y == 0) ? 0 : 1);
                break;
            case 2: //left edge
                ranX = origin.x - depth - ((buildingSize.x == 0) ? 0 : 1);
                ranY = randomGen.rand(origin.y - depth, origin.y + buildingSize.y + depth);
                break;
            case 3: //bottom edge
                ranX = randomGen.rand(origin.x - depth, origin.x + buildingSize.x + depth);
                ranY = origin.y + buildingSize.y + depth;
                break;
            default:
                break;
        }

        auto bOK2Deploy = pUnit->canPass(ranX, ranY);

        if (bOK2Deploy && pUnit->isTracked()) {
            const auto tile = getTile_internal(ranX, ranY);

            if (tile && tile->hasInfantry()) {
                // we do not deploy on enemy infantry
                bOK2Deploy = false;
            }
        }

        if(bOK2Deploy) {
            if(gatherPoint.isInvalid()) {
                closestPoint.x = ranX;
                closestPoint.y = ranY;
                found = true;
            } else {
                const auto temp = Coord(ranX, ranY);
                if(blockDistance(temp, gatherPoint) < closestDistance) {
                    closestDistance = blockDistance(temp, gatherPoint);
                    closestPoint.x = ranX;
                    closestPoint.y = ranY;
                    foundClosest = true;
                }
            }
        }

        if(counter++ >= 100) {
            //if hasn't found a spot on tempObject layer in 100 tries, goto next

            counter = 0;
            if(++depth > (std::max(currentGameMap->getSizeX(), currentGameMap->getSizeY()))) {
                closestPoint.invalidate();
                found = true;
                SDL_Log("Warning: Cannot find deploy position because the map is full!");
            }
        }
    } while (!found && (!foundClosest || (counter > 0)));

    return closestPoint;
}

/**
    This method finds the tile which is at a map border and is at minimal distance to the structure
    specified by origin and buildingsSize. This method is especcially useful for Carryalls and Frigates
    that have to enter the map to deliver units.
    \param origin           the position of the structure in map coordinates
    \param buildingSize    the number of tiles occupied by the building (e.g. 3x2 for refinery)
*/
Coord Map::findClosestEdgePoint(const Coord& origin, const Coord& buildingSize) const {
    auto closestDistance = INT_MAX;
    Coord closestPoint;

    if(origin.x < (sizeX - (origin.x + buildingSize.x))) {
        closestPoint.x = 0;
        closestDistance = origin.x;
    } else {
        closestPoint.x = sizeX - 1;
        closestDistance = sizeX - (origin.x + buildingSize.x);
    }
    closestPoint.y = origin.y;

    if(origin.y < closestDistance) {
        closestPoint.x = origin.x;
        closestPoint.y = 0;
        closestDistance = origin.y;
    }

    if((sizeY - (origin.y + buildingSize.y)) < closestDistance) {
        closestPoint.x = origin.x;
        closestPoint.y = sizeY - 1;
        // closestDistance = origin.y;  //< Not needed anymore
    }

    return closestPoint;
}


void Map::removeObjectFromMap(Uint32 objectID) {
    for (auto& tile : tiles)
        tile.unassignObject(objectID);
}

void Map::selectObjects(const House* pHouse, int x1, int y1, int x2, int y2, int realX, int realY, bool objectARGMode) {

    ObjectBase *lastCheckedObject = nullptr;
    ObjectBase *lastSelectedObject = nullptr;

    //if selection rectangle is checking only one tile and has shift selected we want to add/ remove that unit from the selected group of units
    if(!objectARGMode) {
        currentGame->unselectAll(currentGame->getSelectedList());
        currentGame->getSelectedList().clear();
        currentGame->selectionChanged();
    }

    if((x1 == x2) && (y1 == y2)) {
        const auto tile_center = getTile_internal(x1, y1);

        if (!tile_center)
            return;

        if(tile_center->isExploredByTeam(pHouse->getTeamID()) || debug) {
            lastCheckedObject = tile_center->getObjectAt(realX, realY);
        } else {
            lastCheckedObject = nullptr;
        }

        if((lastCheckedObject != nullptr) && (lastCheckedObject->getOwner() == pHouse)) {
            // Buildings have a single-object sidebar. Shift must not combine a
            // building with troops (or another building) into a troop command panel.
            if (lastCheckedObject->isAStructure()) {
                currentGame->unselectAll(currentGame->getSelectedList());
                currentGame->getSelectedList().clear();
                currentGame->selectionChanged();
            } else if (objectARGMode) {
                auto& selection = currentGame->getSelectedList();
                for (auto it = selection.begin(); it != selection.end();) {
                    auto* selected = currentGame->getObjectManager().getObject(*it);
                    if (selected && selected->isAStructure()) {
                        selected->setSelected(false);
                        it = selection.erase(it);
                        currentGame->selectionChanged();
                    } else ++it;
                }
            }
            if((lastCheckedObject == lastSinglySelectedObject) && ( !lastCheckedObject->isAStructure())) {
                for(auto i = screenborder->getTopLeftTile().x; i <= screenborder->getBottomRightTile().x; i++) {
                    for(auto j = screenborder->getTopLeftTile().y; j <= screenborder->getBottomRightTile().y; j++) {
                        const auto tile = getTile_internal(i, j);

                        if (tile && tile->hasAnObject()) {
                            tile->selectAllPlayersUnitsOfType(pHouse->getHouseID(), lastSinglySelectedObject->getItemID(), &lastCheckedObject, &lastSelectedObject);
                        }
                    }
                }
                lastSinglySelectedObject = nullptr;

            } else if(!lastCheckedObject->isSelected()) {

                lastCheckedObject->setSelected(true);
                currentGame->getSelectedList().insert(lastCheckedObject->getObjectID());
                currentGame->selectionChanged();
                lastSelectedObject = lastCheckedObject;
                lastSinglySelectedObject = lastSelectedObject;

            } else if(objectARGMode) {
                //holding down shift, unselect this unit
                lastCheckedObject->setSelected(false);
                currentGame->getSelectedList().erase(lastCheckedObject->getObjectID());
                currentGame->selectionChanged();
            }

        } else {
            lastSinglySelectedObject = nullptr;
        }

    } else {
        lastSinglySelectedObject = nullptr;
        for(auto i = std::min(x1, x2); i <= std::max(x1, x2); i++) {
            for(auto j = std::min(y1, y2); j <= std::max(y1, y2); j++) {
                const auto tile = getTile_internal(i, j);

                if (tile && tile->hasAnObject() && tile->isExploredByTeam(pHouse->getTeamID()) && !tile->isFoggedByTeam(pHouse->getTeamID())) {
                    tile->selectAllPlayersUnits(pHouse->getHouseID(), &lastCheckedObject, &lastSelectedObject);
                }
            }
        }
    }

    //select an enemy unit if none of your units found
    if(currentGame->getSelectedList().empty() && (lastCheckedObject != nullptr) && !lastCheckedObject->isSelected()) {
        lastCheckedObject->setSelected(true);
        lastSelectedObject = lastCheckedObject;
        currentGame->getSelectedList().insert(lastCheckedObject->getObjectID());
        currentGame->selectionChanged();
    } else if (lastSelectedObject != nullptr) {
        lastSelectedObject->playSelectSound();  //we only want one unit responding
    }
}


bool Map::findSpice(Coord& destination, const Coord& origin) const {

    const auto tile_origin = getTile_internal(origin.x, origin.y);

    if (tile_origin && tile_origin->hasSpice()) {
        destination = origin;
        return true;
    }

    auto counter = 0;
    auto depth = 1;

    while(true) {
        auto ranX = 0;
        auto ranY = 0;
        do {
            ranX = currentGame->randomGen.rand(origin.x-depth, origin.x + depth);
            ranY = currentGame->randomGen.rand(origin.y-depth, origin.y + depth);
        } while(((ranX >= (origin.x+1 - depth)) && (ranX < (origin.x + depth))) && ((ranY >= (origin.y+1 - depth)) && (ranY < (origin.y + depth))));

        const auto tile = getTile_internal(ranX, ranY);

        if (tile && !tile->hasAGroundObject() && tile->hasSpice()) {
            destination.x = ranX;
            destination.y = ranY;
            return true;
        }

        counter++;
        if(counter >= 100) {
            //if hasn't found a spot on tempObject layer in 100 tries, goto next
            counter = 0;
            depth++;
        }

        if(depth > std::max(sizeX, sizeY)) {
            return false;   //there is possibly no spice left anywhere on map
        }
    }
}

/**
    This method fixes surounding thick spice tiles after spice gone to make things look smooth.
    \param coord    the coordinate where spice was removed from
*/
void Map::spiceRemoved(const Coord& coord) {

    auto pCenterTile = getTile_internal(coord.x , coord.y);

    if(!pCenterTile || pCenterTile->getType() != Terrain_Sand) return;

    //thickspice tiles can't handle non-(thick)spice tiles next to them, if this happens after changes, make it non thick
    for(auto i = coord.x-1; i <= coord.x+1; i++) {
        for(auto j = coord.y-1; j <= coord.y+1; j++) {
            if ((i != coord.x) && (j != coord.y)) {
                // skip diagonal
                continue;
            }

            auto pTile = getTile_internal(i, j);

            if (pTile && pTile->isThickSpice()) {
                //only check tile right, up, left and down of this one
                if(pTile->getType() == Terrain_ThickGreenSpice) {
                    pTile->setType(Terrain_GreenSpice);
                } else if(pTile->getType() == Terrain_ThickRedSpice) {
                    pTile->setType(Terrain_RedSpice);
                } else {
                    pTile->setType(Terrain_Spice);
                }
            }
        }
    }
}

void Map::viewMap(int houseID, const Coord& location, const int maxViewRange) {

//makes map viewable in an area like as shown below
//
//                    *
//                  *****
//                  *****
//                 ***T***
//                  *****
//                  *****
//                    *

    const auto cycle_count = currentGame->getGameCycleCount();

    const auto startY = std::max(0, location.y - maxViewRange);
    const auto endY = std::min(sizeY-1, location.y + maxViewRange);
    const auto startX = std::max(0, location.x - maxViewRange);
    const auto endX = std::min(sizeX - 1, location.x + maxViewRange);

    Coord coord;

    for (coord.x = startX; coord.x <= endX; coord.x++) {
        for(coord.y = startY; coord.y <= endY; coord.y++) {
            const auto distance = maxViewRange <= 1 ? maximumDistance(location, coord) : blockDistanceApprox(location, coord);
            if(distance > maxViewRange) {
                continue;
            }

            const auto tile = getTile_internal(coord.x, coord.y);

            if (!tile) {
                continue;
            }

            tile->setExplored(houseID, cycle_count);
        }
    }
}

/**
    Creates a spice field of the given radius at the given location.
    \param  location            the location in tile coordinates
    \param  radius              the radius in tiles (0 = only one tile is filled)
    \param  centerIsThickSpice  if set the center is filled with thick spice
*/
std::pair<int, int> Map::chooseGeneratedSpiceTerrain(int thinSpiceTerrain, int thickSpiceTerrain) const {
    if(currentGame == nullptr
       || thinSpiceTerrain != Terrain_Spice
       || thickSpiceTerrain != Terrain_ThickSpice
       || !isTornieModActive()) {
        return std::make_pair(thinSpiceTerrain, thickSpiceTerrain);
    }

    const int roll = currentGame->randomGen.rand(0, 99);
    if(roll < 10) {
        return std::make_pair(Terrain_GreenSpice, Terrain_ThickGreenSpice);
    }
    if(roll < 20) {
        return std::make_pair(Terrain_RedSpice, Terrain_ThickRedSpice);
    }

    return std::make_pair(Terrain_Spice, Terrain_ThickSpice);
}

void Map::createSpiceField(Coord location, int radius, bool centerIsThickSpice, int thinSpiceTerrain, int thickSpiceTerrain) const {
    Coord offset;
    for(offset.x = -radius; offset.x <= radius; offset.x++) {
        for(offset.y = -radius; offset.y <= radius; offset.y++) {
            const auto coord = location + offset;

            const auto pTile = currentGameMap->getTile_internal(coord.x, coord.y);

            if (!pTile)
                continue;

            if(pTile->isSand() && (distanceFrom(location, coord) <= radius)) {
                if(centerIsThickSpice && (offset.x == 0) && (offset.y == 0)) {
                    pTile->setType(thickSpiceTerrain);
                } else {
                    pTile->setType(thinSpiceTerrain);
                }
            }
        }
    }
}
