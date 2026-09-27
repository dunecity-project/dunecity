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

#ifndef MAP_H
#define MAP_H

#include <Tile.h>
#include <dunecity/CityMapLayer.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <misc/exceptions.h>
#include <misc/Random.h>
#include <players/DeviationReward.h>

#include <cstdio>
#include <utility>

class Map
{
public:
    /**
        Creates a map of size xSize x ySize. The map is initialized with all tiles of type Terrain_Sand.
    */
    Map(int xSize, int ySize);
    Map(const Map& o) = delete;
    Map(Map&& o) = delete;
    ~Map();

    Map& operator=(const Map &) = delete;
    Map& operator=(Map &&) = delete;

    void load(InputStream& stream);
    void save(OutputStream& stream) const;

    void createSandRegions();
    /**
        \param  provenance  who to credit for everything this impact removes, snapshotted when
                            the shot was fired. Leave unknown to decide from the live damager.
    */
    void damage(Uint32 damagerID, House* damagerOwner, const Coord& realPos, Uint32 bulletID, FixPoint damage, int damageRadius, bool air, bool affectTerrain = true, Uint32 interceptedAirUnit = NONE_ID,
                const DeviationReward::Provenance& provenance = DeviationReward::Provenance());
    static Coord getMapPos(int angle, const Coord& source);
    void removeObjectFromMap(Uint32 objectID);
    void spiceRemoved(const Coord& coord);
    void selectObjects(const House* pHouse, int x1, int y1, int x2, int y2, int realX, int realY, bool objectARGMode);

    void viewMap(int houseID, const Coord& location, const int maxViewRange);
    void viewMap(int houseID, int x, int y, const int maxViewRange) {
        viewMap(houseID, Coord(x, y), maxViewRange);
    }

    bool findSpice(Coord& destination, const Coord& origin) const;
    bool okayToPlaceStructure(int x, int y, int buildingSizeX, int buildingSizeY, bool tilesRequired, const House* pHouse, bool bIgnoreUnits = false) const;
    bool okayToPlaceStructure(int x, int y, int buildingSizeX, int buildingSizeY, bool tilesRequired, const House* pHouse, bool bIgnoreUnits, int itemID) const;
    bool isAStructureGap(int x, int y, int buildingSizeX, int buildingSizeY) const; // Allows AI to check to see if a gap exists between the current structure
    bool isWithinBuildRange(int x, int y, const House* pHouse) const;
    static int getPosAngle(const Coord& source, const Coord& pos);
    Coord findClosestEdgePoint(const Coord& origin, const Coord& buildingSize) const;
    Coord findDeploySpot(UnitBase* pUnit, const Coord& origin, Random& randomGen, const Coord& gatherPoint = Coord::Invalid(), const Coord& buildingSize = Coord(0, 0)) const; //building size is num squares

    std::pair<int, int> chooseGeneratedSpiceTerrain(int thinSpiceTerrain = Terrain_Spice, int thickSpiceTerrain = Terrain_ThickSpice) const;
    void createSpiceField(Coord location, int radius, bool centerIsThickSpice = false, int thinSpiceTerrain = Terrain_Spice, int thickSpiceTerrain = Terrain_ThickSpice) const;

    Uint32 getPathingRevision() const noexcept {
        return pathingRevision;
    }

    void incrementPathingRevision() noexcept;
    // Terrain-only connectivity is shared by all vehicles. Moving occupants do
    // not invalidate it; detailed routes still use the ordinary pathfinder.
    void invalidateTerrainConnectivity() noexcept { vehicleTerrainRegions.clear(); }
    bool terrainAttackReachable(const ObjectBase& seeker, const ObjectBase& target) const;
    void restoreObserverPathingRevision(Uint32 revision) noexcept { pathingRevision=revision; }

    Sint32 getSizeX() const noexcept {
        return sizeX;
    }

    Sint32 getSizeY() const noexcept {
        return sizeY;
    }

    bool tileExists(int xPos, int yPos) const noexcept {
        return ((xPos >= 0) && (xPos < sizeX) && (yPos >= 0) && (yPos < sizeY));
    }

    bool tileExists(const Coord& pos) const noexcept {
        return tileExists(pos.x, pos.y);
    }

    const Tile* getTile(int xPos, int yPos) const {
        const auto tile = getTile_internal(xPos, yPos);

        if (!tile)
            THROW(std::out_of_range, "Tile (%d, %d) does not exist!", xPos, yPos);

        return tile;
    }

    Tile* getTile(int xPos, int yPos) {
        const auto tile = getTile_internal(xPos, yPos);

        if (!tile)
            THROW(std::out_of_range, "Tile (%d, %d) does not exist!", xPos, yPos);

        return tile;
    }

    const Tile* getTile(const Coord& location) const {
        return getTile(location.x, location.y);
    }

    Tile* getTile(const Coord& location) {
        return getTile(location.x, location.y);
    }

    template<typename F>
    void for_all(F&& f)
    {
        std::for_each(std::begin(tiles), std::end(tiles), f);
    }

    template<typename F>
    void for_all(F&& f) const
    {
        std::for_each(std::begin(tiles), std::end(tiles), f);
    }

    template<typename F>
    void for_each(int x1, int y1, int x2, int y2, F&& f) const
    {
        if (x1 < 0)
            x1 = 0;
        if (x2 < x1)
            x2 = x1;
        else if (x2 >= sizeX)
            x2 = sizeX;
        if (x1 > x2)
            x1 = x2;

        if (y1 < 0)
            y1 = 0;
        if (y2 < y1)
            y2 = y1;
        else if (y2 >= sizeY)
            y2 = sizeY;
        if (y1 > y2)
            y1 = y2;

        for (auto x = x1; x < x2; ++x) {
            for (auto y = y1; y < y2; ++y) {
                f(tiles[tile_index(x, y)]);
            }
        }
    }

    template<typename F>
    void for_each(int x1, int y1, int x2, int y2, F&& f)
    {
        if (x1 < 0)
            x1 = 0;
        if (x2 < x1)
            x2 = x1;
        else if (x2 >= sizeX)
            x2 = sizeX;
        if (x1 > x2)
            x1 = x2;

        if (y1 < 0)
            y1 = 0;
        if (y2 < y1)
            y2 = y1;
        else if (y2 >= sizeY)
            y2 = sizeY;
        if (y1 > y2)
            y1 = y2;

        for (auto x = x1; x < x2; ++x) {
            for (auto y = y1; y < y2; ++y) {
                f(tiles[tile_index(x, y)]);
            }
        }
    }

private:
    Sint32  sizeX;                          ///< number of tiles this map is wide (read only)
    Sint32  sizeY;                          ///< number of tiles this map is high (read only)
    std::vector<Tile> tiles;                ///< the 2d-array containing all the tiles of the map
    ObjectBase* lastSinglySelectedObject;   ///< The last selected object. If selected again all units of the same type are selected
    Uint32 pathingRevision = 0;             ///< Bumps when long-lived blocking geometry changes to invalidate cached paths
    mutable std::vector<int> vehicleTerrainRegions; // Derived; never serialized.
    mutable unsigned terrainConnectivityBuilds = 0;
    void ensureTerrainConnectivity() const;

    void init_tile_location();

    int tile_index(int xPos, int yPos) const noexcept
    {
        return xPos * sizeY + yPos;
    }

    Tile* getTile_internal(int xPos, int yPos) noexcept {
        if (!tileExists(xPos, yPos))
            return nullptr;

        return &tiles[tile_index(xPos, yPos)];
    }

    const Tile* getTile_internal(int xPos, int yPos) const noexcept {
        if (!tileExists(xPos, yPos))
            return nullptr;

        return &tiles[tile_index(xPos, yPos)];
    }
};


#endif // MAP_H
