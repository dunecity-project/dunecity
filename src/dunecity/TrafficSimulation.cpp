/*
 *  TrafficSimulation.cpp
 *
 *  BFS-based traffic connectivity with Micropolis density sampling.
 *  Each zone attempts to reach a complementary zone type via the road network:
 *    Residential -> Commercial
 *    Commercial  -> Industrial
 *    Industrial  -> Residential
 *
 *  Returns TrafficResult::Connected / NoDestination / NoRoad.
 *  On Connected, returns the ordered route for density sampling by the caller.
 */

#include <dunecity/TrafficSimulation.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CityConstants.h>

#include <globals.h>
#include <Map.h>
#include <Tile.h>
#include <structures/StructureBase.h>

#include <algorithm>
#include <cstdlib>

namespace DuneCity {

TrafficSimulation::TrafficSimulation() = default;

void TrafficSimulation::init(CitySimulation*) {
    routeFinder_.clear();
    const size_t tiles = currentGameMap ? static_cast<size_t>(currentGameMap->getSizeX()) * currentGameMap->getSizeY() : 0;
    roadMemo_.assign(tiles, -1);
    roleMemo_.assign(tiles, 0);
}

bool TrafficSimulation::isRoad(int x, int y) const {
    if (!currentGameMap) return false;
    if (!currentGameMap->tileExists(x, y)) return false;
    const size_t index = static_cast<size_t>(y) * currentGameMap->getSizeX() + x;
    if (index >= roadMemo_.size()) return currentGameMap->getTile(x, y)->isRoadConnection();
    if (roadMemo_[index] < 0) roadMemo_[index] = currentGameMap->getTile(x, y)->isRoadConnection() ? 1 : 0;
    return roadMemo_[index] != 0;
}

// Bit 1/2/4: a Residential/Commercial/Industrial structure on one of the 4 adjacent tiles.
uint8_t TrafficSimulation::neighbourRoles(int x, int y) const {
    uint8_t roles = 0;
    for (int d = 0; d < 4; ++d) {
        const int nx = x + DX[d];
        const int ny = y + DY[d];
        if (!currentGameMap->tileExists(nx, ny)) continue;
        const Tile* t = currentGameMap->getTile(nx, ny);
        if (!t || !t->hasANonInfantryGroundObject()) continue;
        const ObjectBase* pObj = t->getNonInfantryGroundObject();
        if (!pObj || !pObj->isAStructure()) continue;
        const StructureBase* pStruct = static_cast<const StructureBase*>(pObj);

        switch (getStructureCityRole(pStruct->getItemID())) {
            case CityRole::Residential: roles |= 1; break;
            case CityRole::Commercial:  roles |= 2; break;
            case CityRole::Industrial:  roles |= 4; break;
            default: break;
        }
    }
    return roles;
}

bool TrafficSimulation::driveDone(int x, int y, ZoneType destZone) const {
    if (!currentGameMap) return false;
    // Check 4 adjacent tiles for a structure matching the destination role
    uint8_t wanted = 0;
    switch (destZone) {
        case ZoneType::Residential: wanted = 1; break;
        case ZoneType::Commercial:  wanted = 2; break;
        case ZoneType::Industrial:  wanted = 4; break;
        default: return false;
    }
    const size_t index = static_cast<size_t>(y) * currentGameMap->getSizeX() + x;
    if (!currentGameMap->tileExists(x, y) || index >= roleMemo_.size()) return (neighbourRoles(x, y) & wanted) != 0;
    if (!(roleMemo_[index] & 0x80)) roleMemo_[index] = neighbourRoles(x, y) | 0x80;
    return (roleMemo_[index] & wanted) != 0;
}

bool TrafficSimulation::findPerimeterRoad(int zoneX, int zoneY,
                                          int& roadX, int& roadY) const {
    // 2x2 zone footprint: scan the perimeter (ring of tiles around the zone)
    // Order: top row, bottom row, left col, right col (excluding corners
    // already covered by rows).
    static const int perimDX[] = { -1, 0, 1, 2,  -1, 0, 1, 2,  -1, 2,  -1, 2 };
    static const int perimDY[] = { -1,-1,-1,-1,   2, 2, 2, 2,   0, 0,   1, 1 };
    static constexpr int kPerimCount = 12;

    for (int i = 0; i < kPerimCount; ++i) {
        const int px = zoneX + perimDX[i];
        const int py = zoneY + perimDY[i];
        if (isRoad(px, py)) {
            roadX = px;
            roadY = py;
            return true;
        }
    }
    return false;
}

bool TrafficSimulation::tryDrive(int startX, int startY, ZoneType destZone, unsigned directionOffset) {
    routeFinder_.clear();
    if (!currentGameMap) return false;
    return routeFinder_.find(currentGameMap->getSizeX(),currentGameMap->getSizeY(),
        {startX,startY},kMaxTrafficDistance,
        [&](int x,int y) { return isRoad(x,y); },
        [&](int x,int y) { return driveDone(x,y,destZone); }, directionOffset);
}

int TrafficSimulation::makeTraffic(int x, int y, ZoneType destZone, uint32_t day) {
    routeFinder_.clear();
    int roadX = 0, roadY = 0;
    if (!findPerimeterRoad(x, y, roadX, roadY)) {
        return -1;  // NoRoad
    }

    // Vary equal shortest routes without consuming gameplay RNG or extra searches.
    const unsigned directionOffset = (day + unsigned(x)*3u + unsigned(y)) & 3u;
    if (tryDrive(roadX, roadY, destZone, directionOffset)) {
        return 1;   // Connected
    }

    return 0;  // NoDestination
}

constexpr int TrafficSimulation::DX[4];
constexpr int TrafficSimulation::DY[4];

} // namespace DuneCity
