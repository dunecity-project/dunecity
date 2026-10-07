// Headless fixture for Tile::setTrack bounds. Linked with the production game
// objects plus a probe-local build of src/Tile.cpp, so run-tile-track-bounds-probe.py
// can point the very same checks at the pre-fix source and show them failing there.
//
// Replay produced a track direction outside 0..NUM_ANGLES-1. The write then landed
// past tracksCreationTime on the damage vector's own pointers, so the damage.data()
// and neighbour-tile comparisons below are the corruption detector; they are real
// state reads, not an inspection of the guard.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <units/GroundUnit.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace {

void requireTrack(bool ok, const std::string& why) {
    if(!ok) throw std::runtime_error(why);
}

/// Everything about a tile that can be read without dereferencing a container,
/// so a clobbered begin pointer is reported instead of crashing the comparison.
struct TileScalars {
    const void* damageData = nullptr;
    size_t      damageSize = 0;
    size_t      damageCapacity = 0;
    const void* deadData = nullptr;
    size_t      deadSize = 0;
    size_t      deadCapacity = 0;
    size_t      airUnits = 0;
    size_t      infantry = 0;
    size_t      undergroundUnits = 0;
    size_t      groundObjects = 0;
    Uint32      type = 0;
    Sint32      destroyed = 0;
    Uint32      tracks[NUM_ANGLES]{};
};

TileScalars readScalars(const Tile& tile) {
    TileScalars s;
    s.damageData = tile.damage.data();
    s.damageSize = tile.damage.size();
    s.damageCapacity = tile.damage.capacity();
    s.deadData = tile.deadUnits.data();
    s.deadSize = tile.deadUnits.size();
    s.deadCapacity = tile.deadUnits.capacity();
    s.airUnits = tile.assignedAirUnitList.size();
    s.infantry = tile.assignedInfantryList.size();
    s.undergroundUnits = tile.assignedUndergroundUnitList.size();
    s.groundObjects = tile.assignedNonInfantryGroundObjectList.size();
    s.type = tile.type;
    s.destroyed = tile.destroyedStructureTile;
    for(int i = 0; i < NUM_ANGLES; ++i) s.tracks[i] = tile.tracksCreationTime[i];
    return s;
}

void compareScalars(const TileScalars& before, const TileScalars& after, const std::string& what) {
    requireTrack(before.damageData == after.damageData, what + ": damage buffer pointer changed");
    requireTrack(before.damageSize == after.damageSize, what + ": damage size changed");
    requireTrack(before.damageCapacity == after.damageCapacity, what + ": damage capacity changed");
    requireTrack(before.deadData == after.deadData, what + ": dead-unit buffer pointer changed");
    requireTrack(before.deadSize == after.deadSize, what + ": dead-unit size changed");
    requireTrack(before.deadCapacity == after.deadCapacity, what + ": dead-unit capacity changed");
    requireTrack(before.airUnits == after.airUnits, what + ": air-unit list changed");
    requireTrack(before.infantry == after.infantry, what + ": infantry list changed");
    requireTrack(before.undergroundUnits == after.undergroundUnits, what + ": underground list changed");
    requireTrack(before.groundObjects == after.groundObjects, what + ": ground-object list changed");
    requireTrack(before.type == after.type, what + ": terrain type changed");
    requireTrack(before.destroyed == after.destroyed, what + ": destroyed-structure tile changed");
    for(int i = 0; i < NUM_ANGLES; ++i) {
        requireTrack(before.tracks[i] == after.tracks[i],
                     what + ": track direction " + std::to_string(i) + " changed");
    }
}

std::string serialize(const Tile& tile) {
    OMemoryStream stream;
    stream.open();
    tile.save(stream);
    return std::string(stream.getData(), stream.getDataLength());
}

/// Contents, read only once the scalar comparison says the buffers are intact.
struct TileContents {
    std::vector<std::string> damage;
    std::vector<std::string> dead;
    std::list<Uint32>        groundObjects;
};

TileContents readContents(const Tile& tile) {
    TileContents c;
    for(const auto& d : tile.damage) {
        c.damage.push_back(std::to_string(d.damageType) + "/" + std::to_string(d.tile) + "/"
                           + std::to_string(d.realPos.x) + "," + std::to_string(d.realPos.y));
    }
    for(const auto& d : tile.deadUnits) {
        c.dead.push_back(std::to_string(d.type) + "/" + std::to_string(d.house) + "/"
                         + std::to_string(d.onSand) + "/" + std::to_string(d.timer) + "/"
                         + std::to_string(d.realPos.x) + "," + std::to_string(d.realPos.y));
    }
    c.groundObjects = tile.assignedNonInfantryGroundObjectList;
    return c;
}

void compareContents(const TileContents& before, const TileContents& after, const std::string& what) {
    requireTrack(before.damage == after.damage, what + ": damage entries changed");
    requireTrack(before.dead == after.dead, what + ": dead-unit entries changed");
    requireTrack(before.groundObjects == after.groundObjects, what + ": assigned ground objects changed");
}

/// A fresh tile loaded from this tile's own bytes: the save/load path must keep
/// working after an ignored direction, and must still carry the valid timestamps.
void checkRoundTrip(const Tile& tile, const std::string& what) {
    const std::string bytes = serialize(tile);
    Tile restored;
    IMemoryStream in(bytes.data(), static_cast<int>(bytes.size()));
    restored.load(in);
    requireTrack(in.getRemainingLength() == 0, what + ": tile bytes not fully consumed on load");
    for(int i = 0; i < NUM_ANGLES; ++i) {
        requireTrack(restored.tracksCreationTime[i] == tile.tracksCreationTime[i],
                     what + ": track " + std::to_string(i) + " lost in save/load");
    }
    requireTrack(readContents(restored).damage == readContents(tile).damage,
                 what + ": damage lost in save/load");
    requireTrack(readContents(restored).dead == readContents(tile).dead,
                 what + ": dead units lost in save/load");
    requireTrack(restored.assignedNonInfantryGroundObjectList == tile.assignedNonInfantryGroundObjectList,
                 what + ": ground objects lost in save/load");
    requireTrack(serialize(restored) == bytes, what + ": re-serialized tile differs");
}

/// A column of tiles with nothing on them: tile_index() is x*sizeY+y, so the
/// successors in the same column are this tile's neighbours in the tiles vector
/// and therefore what an over-long index writes into.
Coord findClearColumn(int startX, int startY, int span, const std::vector<Coord>& taken) {
    auto* map = currentGameMap;
    for(int x = startX; x < map->getSizeX(); ++x) {
        for(int y = startY; y + span < map->getSizeY(); ++y) {
            bool clear = true;
            for(int dx = -1; dx <= 1 && clear; ++dx) {
                for(int dy = -1; dy <= span && clear; ++dy) {
                    if(!map->tileExists(x + dx, y + dy)) { clear = false; break; }
                    const auto* t = map->getTile(x + dx, y + dy);
                    if(t->hasAGroundObject() || t->hasAnUndergroundUnit() || t->hasAnAirUnit()
                       || t->isRock() || t->hasDeadUnits()) clear = false;
                }
            }
            for(const auto& other : taken) {
                if(std::abs(other.x - x) <= 2 && std::abs(other.y - y) <= span + 2) clear = false;
            }
            if(clear) return Coord(x, y);
        }
    }
    return Coord::Invalid();
}

} // namespace

int runTileTrackBoundsProbe() {
    const std::string mod = std::getenv("TILE_TRACK_PROBE_MOD") ? std::getenv("TILE_TRACK_PROBE_MOD") : "vanilla";
    const std::string testCase = std::getenv("TILE_TRACK_PROBE_CASE") ? std::getenv("TILE_TRACK_PROBE_CASE") : "valid";
    const std::string label = std::getenv("TILE_TRACK_PROBE_LABEL") ? std::getenv("TILE_TRACK_PROBE_LABEL") : "unlabelled";
    const int seed = std::getenv("TILE_TRACK_PROBE_SEED") ? std::atoi(std::getenv("TILE_TRACK_PROBE_SEED")) : 315473198;
    std::unique_ptr<Game> game;
    try {
        requireTrack(ModManager::instance().setActiveMod(mod), "Missing track fixture mod");
        settings.general.playerName = "Track tester";
        std::ifstream mapFile(PROBE_MAP_PATH);
        const std::string mapData((std::istreambuf_iterator<char>(mapFile)), {});
        requireTrack(!mapData.empty(), "Track fixture map is empty");
        game = std::make_unique<Game>();
        currentGame = game.get();
        GameInitSettings init("tile-track.ini", mapData, "Tile track bounds", true, settings.gameOptions);
        init.randomSeed = seed;
        GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
        humans.addPlayerInfo({"Track tester", "HumanPlayer"});
        init.addHouseInfo(humans);
        GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
        enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
        init.addHouseInfo(enemies);
        game->initGame(init);
        auto* owner = game->getHouse(HOUSE_ATREIDES);
        requireTrack(owner && owner->isAlive(), "Track fixture house missing");
        game->initializeGameLoop();
        // Tile::load() only reads the fields a current save writes once the loaded
        // version says so; this fixture round-trips through the current format.
        game->loadedSavegameVersion = SAVEGAMEVERSION;

        const int neighbourSpan = 12;
        const Coord timestampTile = findClearColumn(currentGameMap->getSizeX() / 4,
                                                    currentGameMap->getSizeY() / 4, neighbourSpan, {});
        requireTrack(timestampTile.isValid(), "No clear column for the timestamp checks");
        const Coord stateTile = findClearColumn(timestampTile.x + 3, currentGameMap->getSizeY() / 4,
                                                neighbourSpan, {timestampTile});
        requireTrack(stateTile.isValid(), "No clear column for the tile-state checks");

        // Distinct game cycles per direction, on a tile with no unit on it so nothing
        // but this fixture writes tracks there.
        auto* clockTile = currentGameMap->getTile(timestampTile);
        clockTile->setType(Terrain_Sand);
        Uint32 expected[NUM_ANGLES]{};
        for(int direction = 0; direction < NUM_ANGLES; ++direction) {
            for(int i = 0; i < 3; ++i) game->updateGameState();
            expected[direction] = game->getGameCycleCount();
            requireTrack(expected[direction] != 0, "Game cycle 0 cannot be told from 'no track'");
            clockTile->setTrack(direction);
            requireTrack(clockTile->tracksCreationTime[direction] == expected[direction],
                         "Valid direction " + std::to_string(direction) + " did not record its cycle");
        }
        for(int direction = 0; direction < NUM_ANGLES; ++direction) {
            requireTrack(clockTile->tracksCreationTime[direction] == expected[direction],
                         "Track " + std::to_string(direction) + " did not survive the other directions");
        }
        checkRoundTrip(*clockTile, "timestamp tile");
        SDL_Log("TILE_TRACK_PROBE_TIMESTAMPS: %u %u %u %u %u %u %u %u", expected[0], expected[1],
                expected[2], expected[3], expected[4], expected[5], expected[6], expected[7]);

        // The tile the out-of-range call is made on: real damage, real dead units and a
        // real deployed occupant, all of which live right behind the track array.
        auto* tile = currentGameMap->getTile(stateTile);
        tile->setType(Terrain_Sand);
        tile->addDamage(Tile::Terrain_SandDamage, Tile::SandDamage1, tile->getCenterPoint());
        tile->addDamage(Tile::Terrain_SandDamage, Tile::SandDamage2, tile->getCenterPoint() + Coord(1, 2));
        tile->addDamage(Tile::Terrain_SandDamage, Tile::SandDamage3, tile->getCenterPoint() + Coord(3, 4));
        tile->assignDeadUnit(DeadUnit_Infantry, HOUSE_ATREIDES, tile->getCenterPoint());
        tile->assignDeadUnit(DeadUnit_Ornithopter, HOUSE_HARKONNEN, tile->getCenterPoint() + Coord(2, 2));
        auto* occupant = owner->createUnit(Unit_Tank, true);
        requireTrack(occupant != nullptr, "Could not create the occupying unit");
        occupant->deploy(stateTile);
        requireTrack(tile->hasANonInfantryGroundObject(), "Occupant did not land on the state tile");
        requireTrack(tile->damage.size() == 3 && tile->deadUnits.size() == 2,
                     "State tile did not keep its damage and dead units");
        SDL_Log("TILE_TRACK_PROBE_LAYOUT: sizeofTile=%d trackBytes=%d damageOffsetFromTracks=%d",
                static_cast<int>(sizeof(Tile)), static_cast<int>(sizeof(tile->tracksCreationTime)),
                static_cast<int>(reinterpret_cast<const char*>(&tile->damage)
                                 - reinterpret_cast<const char*>(&tile->tracksCreationTime[0])));

        // All eight valid directions on the populated tile, then the state that must
        // not move when an impossible direction arrives.
        for(int direction = 0; direction < NUM_ANGLES; ++direction) tile->setTrack(direction);
        const Uint32 cycle = game->getGameCycleCount();
        for(int direction = 0; direction < NUM_ANGLES; ++direction) {
            requireTrack(tile->tracksCreationTime[direction] == cycle,
                         "State tile direction " + std::to_string(direction) + " was not recorded");
        }
        checkRoundTrip(*tile, "state tile before the call");

        std::vector<TileScalars> neighbourScalars;
        std::vector<std::string> neighbourBytes;
        for(int offset = 1; offset <= neighbourSpan; ++offset) {
            const auto* neighbour = currentGameMap->getTile(stateTile.x, stateTile.y + offset);
            neighbourScalars.push_back(readScalars(*neighbour));
            neighbourBytes.push_back(serialize(*neighbour));
        }
        if(testCase == "valid") {
            // Control: a legal direction must still be written through the new check.
            tile->tracksCreationTime[3] = 0;
        }
        TileScalars before = readScalars(*tile);
        const TileContents beforeContents = readContents(*tile);
        const std::string beforeBytes = serialize(*tile);

        if(testCase == "valid") {
            tile->setTrack(3);
        } else if(testCase == "oob8") {
            tile->setTrack(NUM_ANGLES);
        } else if(testCase == "oob9") {
            // One past the padding on a 64-bit host: this index is where the write lands
            // on the damage vector's begin pointer, the shape the browser crash had.
            tile->setTrack(NUM_ANGLES + 1);
        } else if(testCase == "oob255") {
            tile->setTrack(255);
        } else if(testCase == "negative") {
            const Sint8 drawnAngle = -1;  // the type GroundUnit::checkPos() passes
            tile->setTrack(drawnAngle);
        } else {
            requireTrack(false, "Unknown track probe case: " + testCase);
        }

        if(testCase == "valid") {
            requireTrack(tile->tracksCreationTime[3] == cycle,
                         "A legal direction stopped recording its cycle");
            before.tracks[3] = cycle;
        }
        compareScalars(before, readScalars(*tile), "state tile after setTrack(" + testCase + ")");
        compareContents(beforeContents, readContents(*tile), "state tile after setTrack(" + testCase + ")");
        for(int offset = 1; offset <= neighbourSpan; ++offset) {
            compareScalars(neighbourScalars[offset - 1],
                           readScalars(*currentGameMap->getTile(stateTile.x, stateTile.y + offset)),
                           "tiles-vector neighbour +" + std::to_string(offset));
        }
        for(int offset = 1; offset <= neighbourSpan; ++offset) {
            requireTrack(neighbourBytes[offset - 1]
                             == serialize(*currentGameMap->getTile(stateTile.x, stateTile.y + offset)),
                         "tiles-vector neighbour +" + std::to_string(offset) + " serialized differently");
        }
        if(testCase != "valid") {
            requireTrack(serialize(*tile) == beforeBytes, "state tile serialized differently");
        }
        checkRoundTrip(*tile, "state tile after setTrack(" + testCase + ")");

        // Separately allocated tiles: the browser crash surfaced in the destructor's
        // free(), so the same call has to survive allocator neighbours and teardown.
        std::vector<std::unique_ptr<Tile>> heap;
        for(int i = 0; i < 5; ++i) heap.push_back(std::make_unique<Tile>());
        auto* loose = heap[2].get();
        loose->setType(Terrain_Sand);
        loose->deadUnitRegistered = true;  // not a map tile; keep it out of the corpse registry
        loose->addDamage(Tile::Terrain_SandDamage, Tile::SandDamage4, Coord(7, 7));
        loose->addDamage(Tile::Terrain_SandDamage, Tile::SandDamage1, Coord(8, 9));
        loose->assignDeadUnit(DeadUnit_Infantry_Squashed1, HOUSE_ORDOS, Coord(5, 6));
        loose->assignNonInfantryGroundObject(4242);
        for(int direction = 0; direction < NUM_ANGLES; ++direction) loose->setTrack(direction);
        std::vector<TileScalars> heapScalars;
        for(const auto& t : heap) heapScalars.push_back(readScalars(*t));
        const TileContents looseContents = readContents(*loose);
        if(testCase == "valid") loose->setTrack(3);
        else if(testCase == "oob8") loose->setTrack(NUM_ANGLES);
        else if(testCase == "oob9") loose->setTrack(NUM_ANGLES + 1);
        else if(testCase == "oob255") loose->setTrack(255);
        else { const Sint8 drawnAngle = -1; loose->setTrack(drawnAngle); }
        for(size_t i = 0; i < heap.size(); ++i) {
            compareScalars(heapScalars[i], readScalars(*heap[i]),
                           "separately allocated tile " + std::to_string(i));
        }
        compareContents(looseContents, readContents(*loose), "separately allocated tile 2");
        checkRoundTrip(*loose, "separately allocated tile 2");
        heap.clear();  // the 1.0.809 crash was here: free() on a tile whose heap was smashed
        std::vector<std::unique_ptr<Tile>> churn;
        for(int i = 0; i < 16; ++i) churn.push_back(std::make_unique<Tile>());
        churn.clear();

        occupant->cancelDeployment();
        SDL_Log("TILE_TRACK_PROBE_PASS: case=%s label=%s mod=%s seed=%d state=(%d,%d) clock=(%d,%d)",
                testCase.c_str(), label.c_str(), mod.c_str(), seed,
                stateTile.x, stateTile.y, timestampTile.x, timestampTile.y);
    } catch(const std::exception& e) {
        SDL_Log("TILE_TRACK_PROBE_FAIL: case=%s label=%s %s", testCase.c_str(), label.c_str(), e.what());
    }
    game.reset();
    currentGame = nullptr;
    return -1;
}
