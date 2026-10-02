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

#ifndef ASTARSEARCH_H
#define ASTARSEARCH_H

#include <DataTypes.h>
#include <fixmath/FixPoint.h>

#include <list>
#include <memory>
#include <vector>
#include <array>

class UnitBase;
class Map;
class InputStream;
class OutputStream;

class AStarSearch {
public:
    /// Runs the whole search immediately. Every direct caller keeps this form.
    AStarSearch(Map* pMap, UnitBase* pUnit, Coord start, Coord destination);

    /// Prepares a search without expanding anything; the caller drives step().
    /// Slicing never changes the outcome: the loop carries no state that is not
    /// a member, so a sliced search closes the same nodes in the same order and
    /// returns the same route and node count as the whole search above.
    struct Resumable {};
    AStarSearch(Map* pMap, UnitBase* pUnit, Coord start, Coord destination, Resumable);

    ~AStarSearch();

    AStarSearch(const AStarSearch &) = delete;
    AStarSearch(AStarSearch &&) = delete;
    AStarSearch& operator=(const AStarSearch &) = delete;
    AStarSearch& operator=(AStarSearch &&) = delete;

    std::list<Coord> getFoundPath() {
        std::list<Coord> path;

        if(bestCoord.isInvalid()) {
            return path;
        }

        // A route cannot revisit a tile, so the tile count bounds the walk. That
        // also means a malformed restored parent chain cannot spin here: it is
        // abandoned instead, and the caller sees no path rather than hanging.
        const size_t limit = static_cast<size_t>(sizeX) * static_cast<size_t>(sizeY);
        Coord currentCoord = bestCoord;
        for(size_t steps = 0; steps <= limit; ++steps) {
            Coord nextCoord = getMapData(currentCoord).parentCoord;

            if(nextCoord.isInvalid()) {
                return path;
            }

            path.push_front(currentCoord);
            currentCoord = nextCoord;
        }

        path.clear();
        return path;
    };

    int getNodesChecked() const { return numNodesChecked; }

    enum class Status { Running, Completed };

    /// Closes at most \a nodeQuota nodes and returns whether the search finished.
    /// \a iterationLimit bounds the slice when the frontier is being drained
    /// without closing anything, so a slice cannot run long on zero charge.
    /// The live map and unit are passed in rather than held: a suspended search
    /// must never own a pointer that can dangle between cycles.
    Status step(Map* pMap, UnitBase* pUnit, size_t nodeQuota, size_t iterationLimit);
    Status getStatus() const { return completed ? Status::Completed : Status::Running; }
    /// Nodes closed by the most recent step(), i.e. what that slice must be charged.
    size_t getNodesClosedLastStep() const { return nodesClosedLastStep; }

    Coord getStart() const { return start; }
    Coord getDestination() const { return destination; }
    size_t frontierSize() const { return openList.size(); }

    /// Complete continuation state, for the observer runtime stream: frontier
    /// order, every touched tile entry including its cached passability, the best
    /// coordinate, heuristic and depth counters, and the search parameters.
    void save(OutputStream& stream) const;
    static std::unique_ptr<AStarSearch> load(InputStream& stream);

    struct PoolUsageStats {
        size_t reuseHits = 0;
        size_t bufferExpansions = 0;
        size_t fallbackAllocs = 0;
        size_t buffersInUse = 0;
        size_t totalBuffers = 0;
    };

    static PoolUsageStats getPoolUsageStats();

private:
    struct TileData {
        Coord    parentCoord;
        size_t   openListIndex;
        FixPoint g;
        FixPoint h;
        FixPoint f;
        bool     bInOpenList;
        bool     bClosed;
        // Zero = unchecked, one = blocked, two = passable. Cached per slice, not
        // for the life of the search: a resumable search spans world updates, so
        // an entry whose epoch is not the current slice's is re-evaluated on first
        // touch. That refreshes moving occupants without ever clearing the buffer,
        // and with a static world it returns the same answer, which is what keeps
        // a sliced search exactly equal to the whole search.
        unsigned char passability;
        Uint32   passabilityEpoch;
        // Which search last wrote this entry. Anything else is logically a
        // freshly zeroed entry, so a reused buffer no longer has to be cleared.
        Uint32   generation;
    };


    // Every read and write of the scratch buffer goes through here, which is
    // what lets the clear be deferred to first touch without changing any
    // value a search can observe.
    inline TileData& getMapData(const Coord& coord) const {
        TileData& entry = mapData[coord.y * sizeX + coord.x];

        if(entry.generation != generation) {
            entry = TileData{};
            entry.generation = generation;
        }

        return entry;
    };

    void trickleUp(size_t openListIndex) {
        Coord bottom = openList[openListIndex];
        FixPoint newf = getMapData(bottom).f;

        size_t current = openListIndex;
        size_t parent = (openListIndex - 1)/2;
        while (current > 0 && getMapData(openList[parent]).f > newf) {

            // copy parent to position of current
            openList[current] = openList[parent];
            getMapData(openList[current]).openListIndex = current;

            // go up one level in the tree
            current = parent;
            parent = (parent - 1)/2;
        }

        openList[current] = bottom;
        getMapData(openList[current]).openListIndex = current;
    };

    void putOnOpenListIfBetter(const Coord& coord, const Coord& parentCoord, FixPoint g, FixPoint h) {
        FixPoint f = g + h;

        if(getMapData(coord).bInOpenList == false) {
            // not yet in openlist => add at the end of the open list
            getMapData(coord).g = g;
            getMapData(coord).h = h;
            getMapData(coord).f = f;
            getMapData(coord).parentCoord = parentCoord;
            getMapData(coord).bInOpenList = true;
            openList.push_back(coord);
            getMapData(coord).openListIndex = openList.size() - 1;

            trickleUp(openList.size() - 1);
        } else {
            // already on openlist
            if(f >= getMapData(coord).f) {
                // new item is worse => don't change anything
                return;
            } else {
                // new item is better => replace
                getMapData(coord).g = g;
                getMapData(coord).h = h;
                getMapData(coord).f = f;
                getMapData(coord).parentCoord = parentCoord;
                trickleUp(getMapData(coord).openListIndex);
            }
        }
    };

    Coord extractMin() {
        Coord ret = openList[0];
        getMapData(ret).bInOpenList = false;

        openList[0] = openList.back();
        getMapData(openList[0]).openListIndex = 0;
        openList.pop_back();

        if (openList.empty())
            return ret;

        size_t current = 0;
        Coord top = openList[current];  // save root
        FixPoint topf = getMapData(top).f;
        while(current < openList.size()/2) {

            size_t leftChild = 2*current+1;
            size_t rightChild = leftChild+1;

            // find smaller child
            size_t smallerChild;
            FixPoint smallerChildf;
            if(rightChild < openList.size()) {
                FixPoint leftf = getMapData(openList[leftChild]).f;
                FixPoint rightf = getMapData(openList[rightChild]).f;

                if(leftf < rightf) {
                    smallerChild = leftChild;
                    smallerChildf = leftf;
                } else {
                    smallerChild = rightChild;
                    smallerChildf = rightf;
                }
            } else {
                // there is only a left child
                smallerChild = leftChild;
                smallerChildf = getMapData(openList[leftChild]).f;
            }

            // top >= largerChild?
            if(topf <= smallerChildf)
                break;

            // shift child up
            openList[current] = openList[smallerChild];
            getMapData(openList[current]).openListIndex = current;

            // go down one level in the tree
            current = smallerChild;
        }

        openList[current] = top;
        getMapData(openList[current]).openListIndex = current;

        return ret;
    };

    // Initialised here as well as in prepare(): load() builds an empty search
    // first and may throw on a malformed stream before the scratch is acquired,
    // and the destructor releases mapData unconditionally.
    int sizeX = 0;
    int sizeY = 0;
    int numNodesChecked = 0;
    Coord bestCoord = Coord::Invalid();
    TileData* mapData = nullptr;
    Uint32 generation = 0;
    std::vector<Coord> openList;

    // Loop state that used to be local to the constructor. All of it has to
    // survive a slice boundary for a resumed search to behave like a whole one.
    Coord start{};
    Coord destination{};
    FixPoint rotationSpeed{};
    FixPoint smallestHeuristic{};
    std::vector<short> depthCheckCount;
    bool completed = false;
    size_t nodesClosedLastStep = 0;
    /// Bumped once per slice, so cached passability is at most one slice old.
    Uint32 passabilityEpoch = 0;

    AStarSearch() = default;  // for load()
    void prepare(Map* pMap, UnitBase* pUnit, Coord searchStart, Coord searchDestination);
    void acquireScratch(size_t tileCount);
    /// Writes a restored entry under the current generation, bypassing the lazy
    /// materialisation in getMapData().
    void restoreMapData(size_t index, const TileData& entry);

    static constexpr size_t TilePoolSize = 8;

    struct TilePoolEntry {
        TileData* buffer = nullptr;
        size_t capacity = 0;
        bool inUse = false;
        // Bumped on every acquisition. A calloc'd buffer starts with all
        // entries at zero, so the first acquisition stamps 1 and every
        // untouched entry reads as stale.
        Uint32 generation = 0;
    };

    static std::array<TilePoolEntry, TilePoolSize> tilePool;

    struct AcquiredBuffer {
        TileData* buffer;
        Uint32 generation;
    };

    static AcquiredBuffer acquireTileBuffer(size_t requiredCount);
    static Uint32 nextGeneration(TilePoolEntry& entry);
    static void releaseTileBuffer(TileData* buffer);
};

#endif //ASTARSEARCH_H
