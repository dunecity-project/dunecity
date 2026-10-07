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

#include <AStarSearch.h>

#include <globals.h>

#include <Map.h>
#include <Game.h>
#include <units/UnitBase.h>

#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <misc/exceptions.h>

#include <algorithm>
#include <limits>
#include <stdlib.h>
#include <cstring>

std::array<AStarSearch::TilePoolEntry, AStarSearch::TilePoolSize> AStarSearch::tilePool = {};
static size_t gPoolReuseHits = 0;
static size_t gPoolBufferExpansions = 0;
static size_t gPoolFallbackAllocs = 0;

#define MAX_NODES_CHECKED   (128*128)

Uint32 AStarSearch::nextGeneration(TilePoolEntry& entry) {
    if(entry.generation == std::numeric_limits<Uint32>::max()) {
        // One wrap in 4 billion searches: reset every entry to generation 0 so
        // that stamp 1 is unambiguous again. This is the only full-buffer clear
        // that remains, and it covers the whole capacity because a later search
        // on a different map size may address entries past requiredCount.
        std::fill_n(entry.buffer, entry.capacity, TileData{});
        entry.generation = 1;
    } else {
        ++entry.generation;
    }

    return entry.generation;
}

AStarSearch::AcquiredBuffer AStarSearch::acquireTileBuffer(size_t requiredCount) {
    // First pass: exact/oversized matches to minimize new allocations
    for(TilePoolEntry& entry : tilePool) {
        if(entry.inUse || entry.buffer == nullptr) {
            continue;
        }

        if(entry.capacity >= requiredCount) {
            entry.inUse = true;
            ++gPoolReuseHits;
            return {entry.buffer, nextGeneration(entry)};
        }
    }

    // Second pass: grow or create buffers in free slots
    for(TilePoolEntry& entry : tilePool) {
        if(entry.inUse) {
            continue;
        }

        if(entry.buffer == nullptr || entry.capacity < requiredCount) {
            std::free(entry.buffer);
            entry.buffer = static_cast<TileData*>(std::calloc(requiredCount, sizeof(TileData)));
            if(entry.buffer == nullptr) {
                throw std::bad_alloc();
            }
            entry.capacity = requiredCount;
            entry.generation = 0;
            ++gPoolBufferExpansions;
        }

        entry.inUse = true;
        return {entry.buffer, nextGeneration(entry)};
    }

    // Fallback: allocate without pooling (should be rare)
    TileData* buffer = static_cast<TileData*>(std::calloc(requiredCount, sizeof(TileData)));
    if(buffer == nullptr) {
        throw std::bad_alloc();
    }
    ++gPoolFallbackAllocs;
    return {buffer, 1};
}

void AStarSearch::releaseTileBuffer(TileData* buffer) {
    if(buffer == nullptr) {
        return;
    }

    for(TilePoolEntry& entry : tilePool) {
        if(entry.buffer == buffer) {
            entry.inUse = false;
            return;
        }
    }

    std::free(buffer);
}

AStarSearch::PoolUsageStats AStarSearch::getPoolUsageStats() {
    PoolUsageStats stats;
    stats.reuseHits = gPoolReuseHits;
    stats.bufferExpansions = gPoolBufferExpansions;
    stats.fallbackAllocs = gPoolFallbackAllocs;
    stats.totalBuffers = TilePoolSize;

    size_t inUse = 0;
    for(const TilePoolEntry& entry : tilePool) {
        if(entry.inUse && entry.buffer != nullptr) {
            ++inUse;
        }
    }
    stats.buffersInUse = inUse;

    return stats;
}

void AStarSearch::acquireScratch(size_t tileCount) {
    const AcquiredBuffer acquired = acquireTileBuffer(tileCount);
    mapData = acquired.buffer;
    generation = acquired.generation;
}

void AStarSearch::restoreMapData(size_t index, const TileData& entry) {
    mapData[index] = entry;
    mapData[index].generation = generation;
}

void AStarSearch::prepare(Map* pMap, UnitBase* pUnit, Coord searchStart, Coord searchDestination) {
    rotationSpeed = 1.0_fix/(currentGame->objectData.data[pUnit->getItemID()][pUnit->getOriginalHouseID()].turnspeed * TILESIZE);

    sizeX = pMap->getSizeX();
    sizeY = pMap->getSizeY();
    numNodesChecked = 0;
    start = searchStart;
    destination = searchDestination;
    nodesClosedLastStep = 0;

    acquireScratch(static_cast<size_t>(sizeX) * static_cast<size_t>(sizeY));

    const FixPoint heuristic = blockDistance(start, destination);
    smallestHeuristic = FixPt_MAX;
    bestCoord = Coord::Invalid();

    //if the unit is not directly next to its destination or it is and the destination is unblocked
    if ((heuristic > 1.5_fix) || (pUnit->canPass(destination.x, destination.y) == true)) {
        putOnOpenListIfBetter(start, Coord::Invalid(), 0 , heuristic);
        depthCheckCount.assign(std::min(sizeX, sizeY), 0);
        completed = false;
    } else {
        // Nothing to search: the whole-search form returned an empty path here.
        completed = true;
    }
}

AStarSearch::AStarSearch(Map* pMap, UnitBase* pUnit, Coord start, Coord destination) {
    prepare(pMap, pUnit, start, destination);
    // Unbounded quota: identical to the original single-shot constructor.
    while(step(pMap, pUnit, std::numeric_limits<size_t>::max(),
               std::numeric_limits<size_t>::max()) == Status::Running) {
    }
}

AStarSearch::AStarSearch(Map* pMap, UnitBase* pUnit, Coord start, Coord destination, Resumable) {
    prepare(pMap, pUnit, start, destination);
}

AStarSearch::Status AStarSearch::step(Map* pMap, UnitBase* pUnit, size_t nodeQuota, size_t iterationLimit) {
    nodesClosedLastStep = 0;

    if(completed) {
        return Status::Completed;
    }

    // One epoch per slice: passability observed in an earlier slice is refreshed
    // on first touch here rather than trusted for the life of the search.
    ++passabilityEpoch;

    size_t iterations = 0;

    {
        while(openList.empty() == false) {
            if(nodesClosedLastStep >= nodeQuota || iterations >= iterationLimit) {
                return Status::Running;
            }
            ++iterations;

            Coord currentCoord = extractMin();

            if (getMapData(currentCoord).h < smallestHeuristic) {
                smallestHeuristic = getMapData(currentCoord).h;
                bestCoord = currentCoord;
            }

            // Approach a blocked goal without exhaustively proving it unreachable.
            // Keep the original goal: an enterable yard/refinery still needs an
            // exact route. Nodes were passable when opened; movement checks each
            // step again because occupants can change between search slices.
            if(currentCoord == destination
               || (getMapData(currentCoord).h <= 1.5_fix && !pUnit->canPass(destination.x, destination.y))) {
                // Goal or adjacent approach found.
                smallestHeuristic = getMapData(currentCoord).h;
                bestCoord = currentCoord;
                break;
            }

            if (numNodesChecked < MAX_NODES_CHECKED) {
                // Parent direction is identical for all eight neighbours.
                const auto& current = getMapData(currentCoord);
                const bool hasParent = current.parentCoord.isValid();
                const int parentAngle = hasParent
                    ? currentGameMap->getPosAngle(current.parentCoord, currentCoord) : 0;
                //push a node for each direction we could go
                for (int angle=0; angle<=7; angle++) {
                    Coord nextCoord = pMap->getMapPos(angle, currentCoord);
                    if (!pMap->tileExists(nextCoord)) continue;
                    auto& next = getMapData(nextCoord);
                    // Closed nodes cannot be improved. Avoid object lookups
                    // and terrain/turn calculations for them altogether.
                    if (next.bClosed) continue;
                    if (next.passability == 0 || next.passabilityEpoch != passabilityEpoch) {
                        next.passability = pUnit->canPass(nextCoord.x, nextCoord.y) ? 2 : 1;
                        next.passabilityEpoch = passabilityEpoch;
                    }
                    if(next.passability == 2) {
                        Tile& nextTile = *(pMap->getTile(nextCoord));
                        FixPoint g = getMapData(currentCoord).g;

                        {
                            FixPoint terrainCost = pUnit->isAFlyingUnit() ? 1.0_fix : pUnit->getTerrainDifficulty((TERRAINTYPE) nextTile.getType());
                            // Road tiles are faster for ground units; reduce path cost to match.
                            if(!pUnit->isAFlyingUnit() && nextTile.isRoad()) {
                                terrainCost /= ROADSPEEDMULTIPLIER;
                            }
                            if((nextCoord.x != currentCoord.x) && (nextCoord.y != currentCoord.y)) {
                                g += FixPt_SQRT2 * terrainCost;
                            } else {
                                g += terrainCost;
                            }
                        }

                        if(hasParent)  {
                            //add cost of turning time
                            g += angleDiff(angle,parentAngle) * rotationSpeed;
                        }

                        FixPoint h = blockDistance(nextCoord, destination);

                        putOnOpenListIfBetter(nextCoord, currentCoord, g, h);
                    }

                }
            }

            if (getMapData(currentCoord).bClosed == false) {

                int depth = std::max(abs(currentCoord.x - destination.x), abs(currentCoord.y - destination.y));

                if(depth < std::min(sizeX,sizeY)) {

                    // calculate maximum number of tiles in a square shape
                    // you could look at without success around a destination x,y
                    // with a specific k distance before knowing that it is
                    // imposible to get to the destination.  Each time the astar
                    // algorithm pushes a node with a max diff of k,
                    // depthcheckcount(k) is incremented, if it reaches the
                    // value in depthcheckmax(x,y,k), we know we have done a full
                    // square around target, and thus it is impossible to reach
                    // the target, so we should try and get closer if possible,
                    // but otherwise stop
                    //
                    // Examples on 6x4 map:
                    //
                    //  ......
                    //  ..###.     - k=1 => 3x3 Square
                    //  ..# #.     - (x,y)=(3,2) => Square completely inside map
                    //  ..###.     => depthcheckmax(3,2,1) = 8
                    //
                    //  .#....
                    //  ##....     - k=1 => 3x3 Square
                    //  ......     - (x,y)=(0,0) => Square only partly inside map
                    //  ......     => depthcheckmax(0,0,1) = 3
                    //
                    //  ...#..
                    //  ...#..     - k=2 => 5x5 Square
                    //  ...#..     - (x,y)=(0,1) => Square only partly inside map
                    //  ####..     => depthcheckmax(0,1,2) = 7


                    int x = destination.x;
                    int y = destination.y;
                    int k = depth;
                    int horizontal = std::min(sizeX-1, x+(k-1)) - std::max(0, x-(k-1)) + 1;
                    int vertical = std::min(sizeY-1, y+k) - std::max(0, y-k) + 1;
                    int depthCheckMax = ((x-k >= 0) ? vertical : 0) +  ((x+k < sizeX) ? vertical : 0) + ((y-k >= 0) ? horizontal : 0) +  ((y+k < sizeY) ? horizontal : 0);


                    if (++depthCheckCount[k] >= depthCheckMax) {
                        // we have searched a whole square around destination, it can't be reached
                        break;
                    }
                }

                getMapData(currentCoord).bClosed = true;
                numNodesChecked++;
                ++nodesClosedLastStep;
            }
        }

    }

    completed = true;
    return Status::Completed;
}

void AStarSearch::save(OutputStream& stream) const {
    stream.writeSint32(sizeX);
    stream.writeSint32(sizeY);
    stream.writeSint32(numNodesChecked);
    stream.writeSint32(bestCoord.x);
    stream.writeSint32(bestCoord.y);
    stream.writeSint32(start.x);
    stream.writeSint32(start.y);
    stream.writeSint32(destination.x);
    stream.writeSint32(destination.y);
    stream.writeFixPoint(rotationSpeed);
    stream.writeFixPoint(smallestHeuristic);
    stream.writeBool(completed);

    stream.writeUint32(static_cast<Uint32>(depthCheckCount.size()));
    for(const short count : depthCheckCount) {
        stream.writeSint32(count);
    }

    // Heap order matters: extractMin() depends on the exact array layout.
    stream.writeUint32(static_cast<Uint32>(openList.size()));
    for(const Coord& coord : openList) {
        stream.writeSint32(coord.x);
        stream.writeSint32(coord.y);
    }

    // Only entries this search touched are live; everything else is logically
    // zero. Walking the buffer once is cheap next to a whole checkpoint.
    const size_t tileCount = static_cast<size_t>(sizeX) * static_cast<size_t>(sizeY);
    size_t touched = 0;
    for(size_t index = 0; index < tileCount; ++index) {
        if(mapData[index].generation == generation) ++touched;
    }
    stream.writeUint32(static_cast<Uint32>(touched));
    for(size_t index = 0; index < tileCount; ++index) {
        const TileData& entry = mapData[index];
        if(entry.generation != generation) continue;
        stream.writeUint32(static_cast<Uint32>(index));
        stream.writeSint32(entry.parentCoord.x);
        stream.writeSint32(entry.parentCoord.y);
        stream.writeUint32(static_cast<Uint32>(entry.openListIndex));
        stream.writeFixPoint(entry.g);
        stream.writeFixPoint(entry.h);
        stream.writeFixPoint(entry.f);
        stream.writeBools(entry.bInOpenList, entry.bClosed);
        stream.writeUint8(entry.passability);
    }
}

std::unique_ptr<AStarSearch> AStarSearch::load(InputStream& stream) {
    std::unique_ptr<AStarSearch> search(new AStarSearch());

    // Every bound below comes from the live map, never from the stream: a
    // malformed or hostile spectator continuation must not be able to choose an
    // allocation size or address outside the scratch buffer. The dimensions are
    // therefore checked against the actual map before anything is allocated.
    if(currentGameMap == nullptr) {
        THROW(std::runtime_error, "Suspended search restored without a map");
    }
    const int mapSizeX = currentGameMap->getSizeX();
    const int mapSizeY = currentGameMap->getSizeY();
    search->sizeX = stream.readSint32();
    search->sizeY = stream.readSint32();
    if(search->sizeX != mapSizeX || search->sizeY != mapSizeY) {
        THROW(std::runtime_error, "Suspended search size %dx%d does not match the map %dx%d",
              search->sizeX, search->sizeY, mapSizeX, mapSizeY);
    }
    const size_t tileCount = static_cast<size_t>(mapSizeX) * static_cast<size_t>(mapSizeY);

    auto requireOnMap = [&](const Coord& coord, const char* what) {
        if(coord.x < 0 || coord.y < 0 || coord.x >= mapSizeX || coord.y >= mapSizeY) {
            THROW(std::runtime_error, "Suspended search %s (%d,%d) is off the map", what, coord.x, coord.y);
        }
    };

    search->numNodesChecked = stream.readSint32();
    if(search->numNodesChecked < 0 || static_cast<size_t>(search->numNodesChecked) > tileCount) {
        THROW(std::runtime_error, "Invalid suspended search node count %d", search->numNodesChecked);
    }
    search->bestCoord.x = stream.readSint32();
    search->bestCoord.y = stream.readSint32();
    if(search->bestCoord.isValid()) requireOnMap(search->bestCoord, "best coordinate");
    search->start.x = stream.readSint32();
    search->start.y = stream.readSint32();
    requireOnMap(search->start, "start");
    search->destination.x = stream.readSint32();
    search->destination.y = stream.readSint32();
    requireOnMap(search->destination, "destination");
    search->rotationSpeed = stream.readFixPoint();
    search->smallestHeuristic = stream.readFixPoint();
    search->completed = stream.readBool();

    // The live search sizes this exactly, and every index used against it is a
    // Chebyshev depth, so both the length and the counts are bounded.
    const Uint32 depths = stream.readUint32();
    if(depths != static_cast<Uint32>(std::min(mapSizeX, mapSizeY))) {
        THROW(std::runtime_error, "Invalid suspended search depth array length %u", depths);
    }
    search->depthCheckCount.resize(depths);
    for(Uint32 i = 0; i < depths; ++i) {
        const Sint32 count = stream.readSint32();
        if(count < 0 || count > std::numeric_limits<short>::max()) {
            THROW(std::runtime_error, "Invalid suspended search depth counter %d", count);
        }
        search->depthCheckCount[i] = static_cast<short>(count);
    }

    const Uint32 openCount = stream.readUint32();
    if(openCount > tileCount) {
        THROW(std::runtime_error, "Invalid suspended search frontier size %u", openCount);
    }
    search->openList.resize(openCount);
    for(Uint32 i = 0; i < openCount; ++i) {
        search->openList[i].x = stream.readSint32();
        search->openList[i].y = stream.readSint32();
        requireOnMap(search->openList[i], "frontier entry");
    }

    // The restored buffer carries a fresh generation, so every entry not written
    // below reads as untouched exactly as it would in a live search.
    search->acquireScratch(tileCount);
    const Uint32 touched = stream.readUint32();
    if(touched > tileCount) {
        THROW(std::runtime_error, "Invalid suspended search touched count %u", touched);
    }
    size_t openListEntries = 0;
    std::vector<bool> seen(tileCount, false);
    for(Uint32 i = 0; i < touched; ++i) {
        const Uint32 index = stream.readUint32();
        if(index >= tileCount) {
            THROW(std::runtime_error, "Invalid suspended search tile index %u", index);
        }
        // A repeated index would let one entry overwrite another and could build a
        // parent chain the writer never produced.
        if(seen[index]) {
            THROW(std::runtime_error, "Duplicate suspended search tile index %u", index);
        }
        seen[index] = true;
        TileData entry{};
        entry.parentCoord.x = stream.readSint32();
        entry.parentCoord.y = stream.readSint32();
        // Coord::Invalid() is the root sentinel getFoundPath() stops on; anything
        // else has to be a real tile or walking the parents could leave the buffer.
        // Off-map parents are only legal as the exact root sentinel the writer
        // emits; anything else is a forged chain.
        if(!entry.parentCoord.isValid()) {
            if(!(entry.parentCoord == Coord::Invalid())) {
                THROW(std::runtime_error, "Suspended search parent (%d,%d) is not the root sentinel",
                      entry.parentCoord.x, entry.parentCoord.y);
            }
        } else {
            requireOnMap(entry.parentCoord, "parent coordinate");
            const size_t parentIndex = static_cast<size_t>(entry.parentCoord.y) * static_cast<size_t>(mapSizeX)
                                     + static_cast<size_t>(entry.parentCoord.x);
            if(parentIndex == index) {
                THROW(std::runtime_error, "Suspended search tile %u is its own parent", index);
            }
        }
        entry.openListIndex = stream.readUint32();
        entry.g = stream.readFixPoint();
        entry.h = stream.readFixPoint();
        entry.f = stream.readFixPoint();
        stream.readBools(&entry.bInOpenList, &entry.bClosed);
        entry.passability = stream.readUint8();
        if(entry.passability > 2) {
            THROW(std::runtime_error, "Invalid suspended search passability %u", entry.passability);
        }
        if(entry.bInOpenList) {
            // The heap and the entries index each other; a lie here would let
            // trickleUp/extractMin write through openListIndex out of bounds.
            if(entry.openListIndex >= openCount) {
                THROW(std::runtime_error, "Suspended search heap index %zu outside the frontier", entry.openListIndex);
            }
            const size_t flatIndex = static_cast<size_t>(search->openList[entry.openListIndex].y) * static_cast<size_t>(mapSizeX)
                                   + static_cast<size_t>(search->openList[entry.openListIndex].x);
            if(flatIndex != index) {
                THROW(std::runtime_error, "Suspended search heap entry %zu does not point back at its tile", entry.openListIndex);
            }
            ++openListEntries;
        } else if(entry.openListIndex != 0 && entry.openListIndex >= openCount) {
            THROW(std::runtime_error, "Suspended search stale heap index %zu is out of range", entry.openListIndex);
        }
        // Restored passability is deliberately stale: the next slice's epoch
        // differs, so every touched entry is re-evaluated against the world the
        // restoring simulation actually has.
        entry.passabilityEpoch = 0;
        search->restoreMapData(index, entry);
    }
    if(openListEntries != openCount) {
        THROW(std::runtime_error, "Suspended search frontier has %u slots but %zu tiles claim them",
              openCount, openListEntries);
    }
    search->passabilityEpoch = 0;

    return search;
}

AStarSearch::~AStarSearch() {
    releaseTileBuffer(mapData);
}
