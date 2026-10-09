// Included only by tools/scenarios/export-save-scenario.py, never by the shipped game.
//
// Converts the CURRENT WORLD of a saved game into an ordinary playable scenario
// INI, using the native save loader to read it and the existing
// MapEditor::saveMap() writer to write it. No new serializer and no new scenario
// format: everything this tool emits is what the map editor itself emits, plus
// one supplementary representation for road tiles (see emitRoadSupplement).
//
// Three modes, one per process, so every engine load starts from fresh state:
//
//   export   load <save>, write <out>.ini + export-facts.txt
//   verify   load <out>.ini as an ordinary custom game, compare against
//            export-facts.txt, then step a bounded match
//   fixture  build a small synthetic world natively and save it as <save>,
//            so the regression test needs no private save file
//
// The facts files are flat text on purpose: the Python driver turns them into
// the machine-readable report, so no JSON writer is linked into this probe.

#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <ObjectManager.h>
#include <ScreenBorder.h>
#include <Tile.h>
#include <data.h>
#include <globals.h>
#include <sand.h>
#include <Definitions.h>
#include <FileClasses/INIFile.h>
#include <INIMap/MapMetadata.h>
#include <MapEditor/MapData.h>
#include <MapEditor/MapEditor.h>
#include <MapEditor/MapInfo.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <Network/WorkshopGameContent.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <units/Carryall.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ScenarioExport {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- diagnostics

int gChecks = 0;
std::vector<std::string> gFailures;

void fail(const std::string& why) {
    SDL_Log("SCENARIO_EXPORT_FAIL: %s", why.c_str());
    throw std::runtime_error(why);
}

void require(bool ok, const std::string& why) {
    if(!ok) fail(why);
    ++gChecks;
}

/// A comparison that is reported rather than aborted, so one run lists every
/// mismatch instead of only the first.
bool check(bool ok, const std::string& what) {
    if(ok) { ++gChecks; return true; }
    gFailures.push_back(what);
    SDL_Log("SCENARIO_EXPORT_MISMATCH: %s", what.c_str());
    return false;
}

std::string env(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

std::string requiredEnv(const char* name) {
    const std::string value = env(name);
    if(value.empty()) fail(std::string("Missing required environment value ") + name);
    return value;
}

// ------------------------------------------------------------------- snapshot

/// Health as the scenario INI stores it: 0..256 of the item's maximum. Never 0
/// for a live object, because 0 in the INI means a destroyed shell.
int quantizeHealth(FixPoint health, int maxHealth) {
    if(maxHealth <= 0) return 256;
    const int scaled = static_cast<int>(lround((health * 256) / maxHealth));
    return std::min(256, std::max(1, scaled));
}

/// The character the terrain row would carry, extended with 'S' for concrete so
/// the expected-versus-loaded comparison can see concrete as its own state.
/// Mirrors the writer's own mapping (src/MapEditor/MapEditor.cpp:645-717).
char terrainChar(int type) {
    switch(type) {
        case Terrain_Slab:            return 'S';
        case Terrain_Dunes:           return '^';
        case Terrain_Spice:           return '~';
        case Terrain_ThickSpice:      return '+';
        case Terrain_GreenSpice:      return 'g';
        case Terrain_ThickGreenSpice: return 'G';
        case Terrain_GreenSpiceBloom: return 'b';
        case Terrain_RedSpice:        return 'r';
        case Terrain_ThickRedSpice:   return 'R';
        case Terrain_RedSpiceBloom:   return 'B';
        case Terrain_Rock:            return '%';
        case Terrain_Mountain:        return '@';
        case Terrain_SpiceBloom:      return 'O';
        case Terrain_SpecialBloom:    return 'Q';
        default:                      return '-';
    }
}

/// Terrain written into the MAP rows. The writer has no Terrain_Slab case and
/// would silently degrade concrete to sand, so concrete becomes rock in the row
/// and is restored by a per-tile Concrete entry in [STRUCTURES].
int rowTerrain(int type) {
    return (type == Terrain_Slab) ? Terrain_Rock : type;
}

struct StructureSnapshot {
    Uint32 objectID = 0;
    int house = 0;
    int itemID = 0;
    int health256 = 256;
    int x = 0;
    int y = 0;
};

struct UnitSnapshot {
    Uint32 objectID = 0;
    int house = 0;
    int itemID = 0;
    int health256 = 256;
    int x = 0;
    int y = 0;
    int angle = 0;
    ATTACKMODE mode = GUARD;
    int sourceMode = GUARD;
    bool onMap = true;      ///< respondable and deployed in the source world
    int placedX = 0;        ///< where the exported scenario puts it
    int placedY = 0;
    bool relocated = false;
};

struct SubstrateSnapshot {
    int x = 0;
    int y = 0;
    int owner = INVALID;
    bool ownerWasUnset = false;
};

struct HouseSnapshot {
    int houseID = 0;
    int team = 0;
    int credits = 0;
    int structures = 0;
    int units = 0;
};

struct World {
    int sizeX = 0;
    int sizeY = 0;
    std::vector<int> terrain;          ///< source terrain type per tile
    std::vector<char> roadFlag;        ///< source Tile::isRoad()
    std::vector<int> tileOwner;
    std::vector<StructureSnapshot> structures;
    std::vector<UnitSnapshot> units;
    std::vector<SubstrateSnapshot> concrete;
    std::vector<SubstrateSnapshot> roads;
    std::map<int, HouseSnapshot> houses;
    std::set<int> activeHouses;        ///< houses with surviving structures or units
    std::map<int, int> itemCounts;     ///< itemID -> count, for the report
    int techLevel = 0;
    int winFlags = 3;
    int loseFlags = 1;
    Uint32 gameCycle = 0;
    std::string modName;
    bool citySim = false;

    int index(int x, int y) const { return y * sizeX + x; }
};

/// Reads the live world. Objects are visited in ascending object id, which is a
/// property of ObjectManager's container, so the export is deterministic.
World capture(Game& game) {
    World world;
    require(currentGameMap != nullptr, "The loaded game has a map");
    world.sizeX = currentGameMap->getSizeX();
    world.sizeY = currentGameMap->getSizeY();
    require(world.sizeX > 0 && world.sizeY > 0, "The loaded map has a positive size");

    world.terrain.resize(static_cast<size_t>(world.sizeX) * world.sizeY, Terrain_Sand);
    world.roadFlag.resize(world.terrain.size(), 0);
    world.tileOwner.resize(world.terrain.size(), INVALID);

    for(int y = 0; y < world.sizeY; ++y) {
        for(int x = 0; x < world.sizeX; ++x) {
            const Tile* tile = currentGameMap->getTile(x, y);
            const int i = world.index(x, y);
            world.terrain[i] = tile->getType();
            world.roadFlag[i] = tile->isRoad() ? 1 : 0;
            world.tileOwner[i] = tile->getOwner();
        }
    }

    game.getObjectManager().forEachObject([&world](Uint32 objectID, ObjectBase* object) {
        if(object == nullptr) return;
        const Coord location = object->getLocation();
        const int house = object->getOwner() != nullptr ? object->getOwner()->getHouseID() : INVALID;
        if(object->isAStructure()) {
            StructureSnapshot snapshot;
            snapshot.objectID = objectID;
            snapshot.house = house;
            snapshot.itemID = object->getItemID();
            snapshot.health256 = quantizeHealth(object->getHealth(), object->getMaxHealth());
            snapshot.x = location.x;
            snapshot.y = location.y;
            world.structures.push_back(snapshot);
            ++world.itemCounts[snapshot.itemID];
        } else if(object->isAUnit()) {
            const UnitBase* unit = static_cast<const UnitBase*>(object);
            UnitSnapshot snapshot;
            snapshot.objectID = objectID;
            snapshot.house = house;
            snapshot.itemID = object->getItemID();
            snapshot.health256 = quantizeHealth(object->getHealth(), object->getMaxHealth());
            snapshot.x = location.x;
            snapshot.y = location.y;
            snapshot.angle = unit->getAngle();
            snapshot.mode = unit->getAttackMode();
            snapshot.sourceMode = static_cast<int>(snapshot.mode);
            // Fresh scenarios restart orders; the editor cannot store Retreat.
            try { (void)getAttackModeNameByMode(snapshot.mode); }
            catch(const std::invalid_argument&) { snapshot.mode = AREAGUARD; }
            snapshot.onMap = object->isActive() && !unit->isPickedUp();
            world.units.push_back(snapshot);
            ++world.itemCounts[snapshot.itemID];
        }
    });

    for(int h = 0; h < NUM_HOUSES; ++h) {
        if(game.house[h] == nullptr) continue;
        HouseSnapshot snapshot;
        snapshot.houseID = h;
        snapshot.team = game.house[h]->getTeamID();
        snapshot.credits = game.house[h]->getCredits();
        world.houses[h] = snapshot;
    }
    for(const auto& structure : world.structures) {
        world.houses[structure.house].houseID = structure.house;
        ++world.houses[structure.house].structures;
    }
    for(const auto& unit : world.units) {
        world.houses[unit.house].houseID = unit.house;
        ++world.houses[unit.house].units;
    }
    // Saved generic lobby rows may still say Random; use the resolved houses.
    // Team zero owners are ambient/passive rather than selectable factions.
    for(const auto& entry : world.houses) {
        const int h = entry.first;
        if(h >= 0 && h < NUM_HOUSES && game.house[h]
           && game.house[h]->getTeamID() > 0 && game.house[h]->isAlive()
           && !game.house[h]->getPlayerList().empty()) world.activeHouses.insert(h);
    }

    for(int y = 0; y < world.sizeY; ++y) {
        for(int x = 0; x < world.sizeX; ++x) {
            const int i = world.index(x, y);
            if(world.terrain[i] == Terrain_Slab) {
                SubstrateSnapshot tile;
                tile.x = x; tile.y = y; tile.owner = world.tileOwner[i];
                world.concrete.push_back(tile);
            }
            if(world.roadFlag[i] != 0) {
                SubstrateSnapshot tile;
                tile.x = x; tile.y = y; tile.owner = world.tileOwner[i];
                world.roads.push_back(tile);
            }
        }
    }

    world.techLevel = static_cast<int>(game.techLevel);
    world.winFlags = static_cast<int>(game.winFlags);
    world.loseFlags = static_cast<int>(game.loseFlags);
    world.gameCycle = game.getGameCycleCount();
    world.modName = ModManager::instance().getActiveModName();
    world.citySim = ModManager::instance().isCityModeActive();
    return world;
}

// ----------------------------------------------------------------- placement

/// Target-world occupancy, rebuilt from what the scenario will contain rather
/// than from the source map: the loader places units before structures, so
/// ground units must leave building footprints available for placement
/// (src/House.cpp:1383-1415). Simulating that here is what keeps unit counts
/// honest instead of discovering losses in the loader's warning log.
class Occupancy {
public:
    Occupancy(int sizeX, int sizeY, const std::vector<int>& terrain)
     : sizeX_(sizeX), sizeY_(sizeY), terrain_(terrain),
       ground_(static_cast<size_t>(sizeX) * sizeY, 0),
       structures_(static_cast<size_t>(sizeX) * sizeY, 0),
       infantry_(static_cast<size_t>(sizeX) * sizeY, 0),
       air_(static_cast<size_t>(sizeX) * sizeY, 0),
       underground_(static_cast<size_t>(sizeX) * sizeY, 0) { }

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < sizeX_ && y < sizeY_; }

    void occupyStructure(int x, int y, int itemID) {
        const Coord size = getStructureSize(itemID);
        for(int dy = 0; dy < std::max(1, size.y); ++dy)
            for(int dx = 0; dx < std::max(1, size.x); ++dx)
                if(inside(x + dx, y + dy)) structures_[index(x + dx, y + dy)] = 1;
    }

    /// Terrain-only passability. The live canPass() consults the SOURCE map's
    /// occupancy, so it cannot answer a question about the target world; these
    /// are the terrain halves of the real overrides (TrackedUnit.cpp:63-73,
    /// InfantryBase.cpp:156-168, AirUnit.cpp:140-143, SandWorm.cpp:475-480).
    bool terrainAccepts(int itemID, int x, int y) const {
        const int type = terrain_[index(x, y)];
        if(isFlyingUnit(itemID)) return true;
        if(itemID == Unit_Sandworm)
            return type != Terrain_Rock && type != Terrain_Slab && type != Terrain_Mountain;
        if(isInfantryUnit(itemID)) return true;   // infantry may climb mountains
        return type != Terrain_Mountain;
    }

    bool accepts(int itemID, int x, int y) const {
        if(!inside(x, y) || !terrainAccepts(itemID, x, y)) return false;
        const int i = index(x, y);
        if(isInfantryUnit(itemID)) return ground_[i] == 0 && structures_[i] == 0 && infantry_[i] < NUM_INFANTRY_PER_TILE;
        // Every non-infantry unit, air units included, needs a tile with no
        // ground object at all; an Ornithopter additionally needs no air unit.
        if(ground_[i] != 0 || infantry_[i] != 0) return false;
        if(isFlyingUnit(itemID)) return air_[i] == 0;
        if(structures_[i] != 0) return false;
        if(itemID == Unit_Sandworm) return underground_[i] == 0;
        return true;
    }

    void occupyUnit(int itemID, int x, int y) {
        const int i = index(x, y);
        if(isInfantryUnit(itemID)) { ++infantry_[i]; return; }
        if(isFlyingUnit(itemID)) { ++air_[i]; return; }
        if(itemID == Unit_Sandworm) { underground_[i] = 1; return; }
        ground_[i] = 1;
    }

private:
    int index(int x, int y) const { return y * sizeX_ + x; }
    int sizeX_, sizeY_;
    const std::vector<int>& terrain_;
    std::vector<unsigned char> ground_, structures_, infantry_, air_, underground_;
};

/// Deterministic nearest-free placement: the recorded tile first, then growing
/// rings in a fixed order. A unit that cannot be placed is never dropped
/// quietly - the run fails and names it.
void planUnitPlacement(World& world, int searchRadius) {
    Occupancy occupancy(world.sizeX, world.sizeY, world.terrain);
    for(const auto& structure : world.structures)
        occupancy.occupyStructure(structure.x, structure.y, structure.itemID);

    for(auto& unit : world.units) {
        const bool validStart = occupancy.inside(unit.x, unit.y);
        Coord anchor(world.sizeX / 2, world.sizeY / 2);
        if(!validStart) for(const auto& structure : world.structures)
            if(structure.house == unit.house && structure.itemID == Structure_ConstructionYard) {
                anchor = Coord(structure.x, structure.y); break;
            }
        const int startX = validStart ? unit.x : anchor.x;
        const int startY = validStart ? unit.y : anchor.y;
        bool placed = false;
        for(int radius = 0; radius <= searchRadius && !placed; ++radius) {
            for(int dy = -radius; dy <= radius && !placed; ++dy) {
                for(int dx = -radius; dx <= radius && !placed; ++dx) {
                    if(radius != 0 && std::abs(dx) != radius && std::abs(dy) != radius) continue;
                    const int x = startX + dx, y = startY + dy;
                    if(!occupancy.accepts(unit.itemID, x, y)) continue;
                    unit.placedX = x;
                    unit.placedY = y;
                    unit.relocated = (!validStart) || (x != unit.x) || (y != unit.y);
                    occupancy.occupyUnit(unit.itemID, x, y);
                    placed = true;
                }
            }
        }
        if(!placed)
            fail("No legal tile within " + std::to_string(searchRadius) + " of ("
                 + std::to_string(unit.x) + "," + std::to_string(unit.y) + ") for "
                 + getItemNameByID(unit.itemID) + " (object " + std::to_string(unit.objectID)
                 + "). Raise --search-radius or report this world as unconvertible.");
    }
}

// -------------------------------------------------------------------- writing

std::string houseName(int houseID) {
    return getHouseNameByNumber(static_cast<HOUSETYPE>(houseID));
}

/// Road tiles, added after the ordinary serialization.
///
/// The writer emits exactly one GEN key per tile position
/// (src/MapEditor/MapEditor.cpp:829-834), so concrete and a road on the same
/// tile would overwrite each other. The loader parses the position with
/// istringstream (include/misc/string_util.h:78-82), which reads leading zeros
/// as decimal, so a zero-padded spelling is a distinct INI key naming the same
/// tile. Order matters too: placing concrete clears the road flag
/// (src/House.cpp:1136-1141), and the loader walks the section in file order,
/// so these appended keys are read after every Concrete key.
void emitRoadSupplement(const std::string& path, const World& world, int substrateFallbackHouse) {
    INIFile ini(path);
    // GEN walls omit damage; ordinary ID walls preserve health.
    int wallID = 1000000;
    for(const auto& wall : world.structures) if(wall.itemID == Structure_Wall) {
        const int position = wall.y * world.sizeX + wall.x;
        ini.removeKey("STRUCTURES", fmt::sprintf("GEN%.3d", position));
        ini.setStringValue("STRUCTURES", "ID" + std::to_string(wallID++),
            houseName(wall.house) + ",Wall," + std::to_string(wall.health256) + "," + std::to_string(position), false);
    }
    for(const auto& road : world.roads) {
        const int owner = (road.owner >= 0 && road.owner < NUM_HOUSES) ? road.owner : substrateFallbackHouse;
        const int position = road.y * world.sizeX + road.x;
        std::ostringstream key;
        key << "GEN" << std::setw(8) << std::setfill('0') << position;
        ini.setStringValue("STRUCTURES", key.str(), houseName(owner) + ",Road", false);
    }
    // The category the writer derives is computed before this supplement exists
    // (MapEditor.cpp:858-861); recompute it so the file reports the same
    // dependency it would have reported had roads gone through the writer.
    const std::string stem = fs::path(path).stem().string();
    ini.setStringValue("BASIC", "Mod", MapMetadata::inferMod(ini, stem));
    if(!ini.saveChangesTo(path, false)) fail("Could not write the road supplement to " + path);
}

/// Everything the report needs, in a flat format the Python driver turns into
/// JSON and the verify pass reads back.
void writeFacts(const std::string& path, const World& world, int substrateFallbackHouse) {
    std::ofstream out(path, std::ios::trunc);
    if(!out) fail("Could not write " + path);
    out << "size " << world.sizeX << ' ' << world.sizeY << '\n';
    out << "tech " << world.techLevel << '\n';
    out << "win " << world.winFlags << '\n';
    out << "lose " << world.loseFlags << '\n';
    out << "cycle " << world.gameCycle << '\n';
    out << "mod " << world.modName << '\n';
    out << "citysim " << (world.citySim ? 1 : 0) << '\n';
    out << "substrate_fallback_house " << substrateFallbackHouse << '\n';
    for(const auto& entry : world.houses) {
        const auto& house = entry.second;
        out << "house " << house.houseID << ' ' << house.team << ' ' << house.credits << ' '
            << house.structures << ' ' << house.units << ' '
            << (world.activeHouses.count(house.houseID) ? 1 : 0) << ' ' << houseName(house.houseID) << '\n';
    }
    for(const auto& structure : world.structures)
        out << "struct " << structure.house << ' ' << structure.itemID << ' ' << structure.x << ' '
            << structure.y << ' ' << structure.health256 << '\n';
    for(const auto& unit : world.units)
        out << "unit " << unit.house << ' ' << unit.itemID << ' ' << unit.placedX << ' ' << unit.placedY
            << ' ' << unit.angle << ' ' << static_cast<int>(unit.mode) << ' ' << unit.health256 << ' '
            << (unit.relocated ? 1 : 0) << ' ' << unit.x << ' ' << unit.y << ' '
            << (unit.onMap ? 1 : 0) << ' ' << unit.objectID << '\n';
    for(const auto& unit : world.units) if(unit.sourceMode != static_cast<int>(unit.mode))
        out << "reset_mode " << unit.objectID << ' ' << unit.sourceMode << ' ' << static_cast<int>(unit.mode) << '\n';
    for(const auto& tile : world.concrete)
        out << "concrete " << tile.x << ' ' << tile.y << ' ' << tile.owner << '\n';
    for(const auto& tile : world.roads)
        out << "road " << tile.x << ' ' << tile.y << ' ' << tile.owner << '\n';
    for(const auto& entry : world.itemCounts)
        out << "item " << entry.first << ' ' << entry.second << ' ' << getItemNameByID(entry.first) << '\n';
    for(int y = 0; y < world.sizeY; ++y) {
        std::string row;
        row.reserve(world.sizeX);
        for(int x = 0; x < world.sizeX; ++x) row += terrainChar(world.terrain[world.index(x, y)]);
        out << "terrain " << y << ' ' << row << '\n';
    }
    out.flush();
    if(!out) fail("Could not flush " + path);
}

/// Fills the editor from the snapshot and lets the ordinary writer serialize it.
void writeScenario(const World& world, const std::string& outPath, const std::string& author,
                   int substrateFallbackHouse) {
    auto editor = std::make_unique<MapEditor>();

    MapData mapData(world.sizeX, world.sizeY, Terrain_Sand);
    for(int y = 0; y < world.sizeY; ++y)
        for(int x = 0; x < world.sizeX; ++x)
            mapData(x, y) = static_cast<TERRAINTYPE>(rowTerrain(world.terrain[world.index(x, y)]));

    MapInfo info;
    info.mapSeed = INVALID;                 // keeps the version 2 full-row format
    info.author = author;
    info.license = "CC-BY-SA";
    info.techLevel = world.techLevel;
    info.winFlags = world.winFlags;
    info.loseFlags = world.loseFlags;
    info.timeout = 0;
    editor->setMap(mapData, info);

    // setMap() seeds the players vector in canonical house order and the writer
    // indexes house2housename[] by house id (MapEditor.cpp:819,827), so the
    // slots are edited in place and defeated houses stay as inactive
    // placeholders rather than being compacted away.
    auto& players = editor->getPlayers();
    for(size_t i = 0; i < players.size(); ++i) {
        const int houseID = static_cast<int>(i);
        const bool active = world.activeHouses.count(houseID) > 0;
        players[i].bAnyHouse = false;       // pin the real faction, not a Player<N> slot
        players[i].bActive = active;
        players[i].quota = 0;
        const auto found = world.houses.find(houseID);
        players[i].credits = (found != world.houses.end()) ? found->second.credits : 0;
        players[i].brain = "Team" + std::to_string(found != world.houses.end() ? found->second.team : 0);
    }
    for(const auto& houseID : world.activeHouses)
        require(houseID >= 0 && houseID < static_cast<int>(players.size()),
                "Active house " + std::to_string(houseID) + " has a writer slot");

    int nextID = 1;
    auto& structures = editor->getStructureList();
    for(const auto& structure : world.structures)
        structures.push_back(MapEditor::Structure(nextID++, static_cast<HOUSETYPE>(structure.house),
                                                  structure.itemID, structure.health256,
                                                  Coord(structure.x, structure.y)));
    // Concrete is tile state in the engine but a GEN structure entry in the INI.
    for(const auto& tile : world.concrete) {
        const int owner = (tile.owner >= 0 && tile.owner < NUM_HOUSES) ? tile.owner : substrateFallbackHouse;
        structures.push_back(MapEditor::Structure(nextID++, static_cast<HOUSETYPE>(owner),
                                                  Structure_Slab1, 256, Coord(tile.x, tile.y)));
    }

    auto& units = editor->getUnitList();
    int nextUnitID = 1;
    for(const auto& unit : world.units)
        units.push_back(MapEditor::Unit(nextUnitID++, static_cast<HOUSETYPE>(unit.house), unit.itemID,
                                        unit.health256, Coord(unit.placedX, unit.placedY),
                                        static_cast<unsigned char>(unit.angle), unit.mode));

    editor->saveMap(outPath);
    editor.reset();

    emitRoadSupplement(outPath, world, substrateFallbackHouse);

    // saveMap() pinned the pre-supplement bytes and wrote the revision sidecar.
    // Re-pin so the sidecar names the file that actually exists. Local only.
    try {
        Workshop::saveMap(outPath, ModManager::instance().getActiveModName());
    } catch(const std::exception& error) {
        SDL_Log("SCENARIO_EXPORT_NOTE: revision sidecar not refreshed (%s); removing it", error.what());
        std::error_code ignored;
        fs::remove(outPath + ".workshop.ini", ignored);
    }
}

// ------------------------------------------------------------------ game setup

std::unique_ptr<Game> loadSavedGame(const std::string& path) {
    // Resolve offline before the interactive loader can offer a download dialog.
    const auto bytes = readCompleteFile(path);
    IMemoryStream stream(bytes.data(), bytes.size());
    GameInitSettings::HouseInfoList roster;
    auto saved = GameInitSettings::readSaveSetup(stream, roster);
    try { WorkshopGameContent::resolveMod(saved, false); }
    catch(const std::exception&) {
        fail("The pinned mod is unavailable offline; supply --profile-from with its Workshop revision cache");
    }
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;   // no briefing, no analytics, no map collection
    game->initGame(GameInitSettings(path));
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;
    return game;
}

void teardown(std::unique_ptr<Game>& game) {
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if(!in) fail("Cannot read " + path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// --------------------------------------------------------------- expected facts

struct ExpectedStruct { int house, itemID, x, y, health256; };
struct ExpectedUnit { int house, itemID, x, y, angle, mode, health256; };

struct Expected {
    int sizeX = 0, sizeY = 0, techLevel = 0, winFlags = 0, loseFlags = 0;
    int substrateFallbackHouse = 0;
    std::map<int, HouseSnapshot> houses;
    std::set<int> activeHouses;
    std::vector<ExpectedStruct> structures;
    std::vector<ExpectedUnit> units;
    std::vector<SubstrateSnapshot> concrete, roads;
    std::vector<std::string> terrainRows;
    int relocatedUnits = 0;
};

Expected readExpected(const std::string& path) {
    Expected expected;
    std::ifstream in(path);
    if(!in) fail("Cannot read " + path);
    std::string line;
    while(std::getline(in, line)) {
        std::istringstream row(line);
        std::string kind;
        row >> kind;
        if(kind == "size") row >> expected.sizeX >> expected.sizeY;
        else if(kind == "tech") row >> expected.techLevel;
        else if(kind == "win") row >> expected.winFlags;
        else if(kind == "lose") row >> expected.loseFlags;
        else if(kind == "substrate_fallback_house") row >> expected.substrateFallbackHouse;
        else if(kind == "house") {
            HouseSnapshot house; int active = 0;
            row >> house.houseID >> house.team >> house.credits >> house.structures >> house.units >> active;
            expected.houses[house.houseID] = house;
            if(active != 0) expected.activeHouses.insert(house.houseID);
        } else if(kind == "struct") {
            ExpectedStruct item{};
            row >> item.house >> item.itemID >> item.x >> item.y >> item.health256;
            expected.structures.push_back(item);
        } else if(kind == "unit") {
            ExpectedUnit item{}; int relocated = 0, sourceX = 0, sourceY = 0, onMap = 0; unsigned objectID = 0;
            row >> item.house >> item.itemID >> item.x >> item.y >> item.angle >> item.mode
                >> item.health256 >> relocated >> sourceX >> sourceY >> onMap >> objectID;
            expected.units.push_back(item);
            expected.relocatedUnits += relocated;
        } else if(kind == "concrete" || kind == "road") {
            SubstrateSnapshot tile;
            row >> tile.x >> tile.y >> tile.owner;
            (kind == "concrete" ? expected.concrete : expected.roads).push_back(tile);
        } else if(kind == "terrain") {
            int y = 0; std::string chars;
            row >> y >> chars;
            if(static_cast<int>(expected.terrainRows.size()) <= y) expected.terrainRows.resize(y + 1);
            expected.terrainRows[y] = chars;
        }
    }
    require(expected.sizeX > 0 && expected.sizeY > 0, "The expected facts name a map size");
    return expected;
}

// ------------------------------------------------------------------- the modes

void runExport() {
    const std::string savePath = requiredEnv("SCENARIO_EXPORT_SAVE");
    const std::string outPath = requiredEnv("SCENARIO_EXPORT_OUTPUT");
    const std::string factsPath = requiredEnv("SCENARIO_EXPORT_FACTS");
    const std::string author = env("SCENARIO_EXPORT_AUTHOR", "DuneCity save export");
    const int searchRadius = std::max(1, std::atoi(env("SCENARIO_EXPORT_SEARCH_RADIUS", "24").c_str()));

    auto game = loadSavedGame(savePath);
    World world = capture(*game);
    SDL_Log("SCENARIO_EXPORT_SOURCE: size=%dx%d structures=%zu units=%zu concrete=%zu roads=%zu "
            "houses=%zu active=%zu cycle=%u mod=%s citysim=%d tech=%d win=%u lose=%u",
            world.sizeX, world.sizeY, world.structures.size(), world.units.size(),
            world.concrete.size(), world.roads.size(), world.houses.size(), world.activeHouses.size(),
            world.gameCycle, world.modName.c_str(), world.citySim ? 1 : 0, world.techLevel,
            world.winFlags, world.loseFlags);
    teardown(game);     // the editor owns its own ScreenBorder; the game must be gone first

    require(!world.activeHouses.empty(), "The saved world still has at least one surviving house");
    // GEN needs an owner. Use a passive house so unowned roads do not grant
    // any surviving player construction anchors throughout other cities.
    int substrateFallbackHouse = INVALID;
    for(int h = 0; h < getNumAvailableHouses(); ++h)
        if(!world.activeHouses.count(h)) { substrateFallbackHouse = h; break; }
    const auto unowned = [](const auto& tile) { return tile.owner < 0 || tile.owner >= NUM_HOUSES; };
    const bool hasUnownedSubstrate = std::any_of(world.roads.begin(), world.roads.end(), unowned)
        || std::any_of(world.concrete.begin(), world.concrete.end(), unowned);
    require(substrateFallbackHouse != INVALID || !hasUnownedSubstrate,
        "Unowned road tiles require an unused passive house in this scenario format");
    if(substrateFallbackHouse == INVALID) substrateFallbackHouse = *world.activeHouses.begin();
    planUnitPlacement(world, searchRadius);

    int relocated = 0;
    for(const auto& unit : world.units) if(unit.relocated) ++relocated;
    SDL_Log("SCENARIO_EXPORT_PLACEMENT: units=%zu relocated=%d search_radius=%d",
            world.units.size(), relocated, searchRadius);

    writeFacts(factsPath, world, substrateFallbackHouse);
    writeScenario(world, outPath, author, substrateFallbackHouse);
    SDL_Log("SCENARIO_EXPORT_WROTE: %s bytes=%llu", outPath.c_str(),
            static_cast<unsigned long long>(fs::file_size(outPath)));
    SDL_Log("SCENARIO_EXPORT_PASS: export checks=%d", gChecks);
}

void runVerify() {
    const std::string scenarioPath = requiredEnv("SCENARIO_EXPORT_OUTPUT");
    const std::string factsPath = requiredEnv("SCENARIO_EXPORT_FACTS");
    const std::string verifyPath = requiredEnv("SCENARIO_EXPORT_VERIFY_FACTS");
    const int seconds = std::max(0, std::atoi(env("SCENARIO_EXPORT_MATCH_SECONDS", "60").c_str()));

    const Expected expected = readExpected(factsPath);

    // An ordinary custom game on the generated scenario: same constructor the
    // Custom Game menu uses for a locally chosen map.
    const std::string contents = readFile(scenarioPath);
    GameInitSettings init(scenarioPath, contents, true, settings.gameOptions);
    init.randomSeed = 20261009u;
    bool firstHouse = true;
    for(const int houseID : expected.activeHouses) {
        const auto found = expected.houses.find(houseID);
        const int team = (found != expected.houses.end()) ? found->second.team : 1;
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(houseID), team);
        // An ordinary custom match has one local human house; the rest are AI.
        if(firstHouse) info.addPlayerInfo({settings.general.playerName, "HumanPlayer"});
        else info.addPlayerInfo({houseName(houseID), "qBotEasy"});
        firstHouse = false;
        init.addHouseInfo(info);
    }

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    game->bPause = false;

    World loaded = capture(*game);

    check(loaded.sizeX == expected.sizeX && loaded.sizeY == expected.sizeY,
          "map size " + std::to_string(loaded.sizeX) + "x" + std::to_string(loaded.sizeY)
          + " != " + std::to_string(expected.sizeX) + "x" + std::to_string(expected.sizeY));
    check(loaded.techLevel == expected.techLevel, "tech level " + std::to_string(loaded.techLevel)
          + " != " + std::to_string(expected.techLevel));
    check(loaded.winFlags == expected.winFlags, "win flags differ");
    check(loaded.loseFlags == expected.loseFlags, "lose flags differ");

    // Terrain, concrete and roads, tile by tile.
    int terrainMismatch = 0, roadMismatch = 0, ownerMismatch = 0;
    if(loaded.sizeX == expected.sizeX && loaded.sizeY == expected.sizeY
       && static_cast<int>(expected.terrainRows.size()) == expected.sizeY) {
        for(int y = 0; y < loaded.sizeY; ++y) {
            for(int x = 0; x < loaded.sizeX; ++x) {
                const int i = loaded.index(x, y);
                if(terrainChar(loaded.terrain[i]) != expected.terrainRows[y][x]) ++terrainMismatch;
            }
        }
    } else {
        ++terrainMismatch;
    }
    check(terrainMismatch == 0, "terrain or concrete differs on " + std::to_string(terrainMismatch) + " tiles");

    std::set<std::pair<int,int>> loadedRoads;
    for(const auto& road : loaded.roads) loadedRoads.insert({road.x, road.y});
    for(const auto& road : expected.roads) if(loadedRoads.count({road.x, road.y}) == 0) ++roadMismatch;
    std::set<std::pair<int,int>> expectedRoads, frontage;
    for(const auto& road : expected.roads) expectedRoads.insert({road.x, road.y});
    // Ordinary city scenario placement auto-paves building frontage. Accept
    // only that documented startup normalization; every saved road must remain.
    for(const auto& structure : loaded.structures) {
        if(structure.itemID == Structure_Wall) continue;
        const Coord size = getStructureSize(structure.itemID);
        for(int dy = -1; dy <= size.y; ++dy) for(int dx = -1; dx <= size.x; ++dx) {
            if(dx >= 0 && dx < size.x && dy >= 0 && dy < size.y) continue;
            const int x = structure.x + dx, y = structure.y + dy;
            if(x >= 0 && x < loaded.sizeX && y >= 0 && y < loaded.sizeY
               && DuneCity::isCityBuildableTerrain(loaded.terrain[loaded.index(x,y)]))
                frontage.insert({x,y});
        }
    }
    std::vector<SubstrateSnapshot> autoRoads;
    int unexpectedRoads = 0;
    for(const auto& road : loaded.roads) if(!expectedRoads.count({road.x,road.y})) {
        if(!loaded.citySim || !frontage.count({road.x,road.y})) ++unexpectedRoads;
        autoRoads.push_back(road);
    }
    check(roadMismatch == 0 && unexpectedRoads == 0,
          "saved roads missing=" + std::to_string(roadMismatch)
          + " unexplained added roads=" + std::to_string(unexpectedRoads));
    check(loaded.concrete.size() == expected.concrete.size(),
          "concrete tiles " + std::to_string(loaded.concrete.size()) + " != "
          + std::to_string(expected.concrete.size()));

    auto ownerOf = [&](const std::vector<SubstrateSnapshot>& tiles) {
        std::map<std::pair<int,int>, int> owners;
        for(const auto& tile : tiles) owners[{tile.x, tile.y}] = tile.owner;
        return owners;
    };
    const auto loadedConcreteOwner = ownerOf(loaded.concrete);
    const auto loadedRoadOwner = ownerOf(loaded.roads);
    for(const auto& tile : expected.concrete) {
        const int want = (tile.owner >= 0 && tile.owner < NUM_HOUSES) ? tile.owner : expected.substrateFallbackHouse;
        const auto found = loadedConcreteOwner.find({tile.x, tile.y});
        if(found == loadedConcreteOwner.end() || found->second != want) ++ownerMismatch;
    }
    for(const auto& tile : expected.roads) {
        const int want = (tile.owner >= 0 && tile.owner < NUM_HOUSES) ? tile.owner : expected.substrateFallbackHouse;
        const auto found = loadedRoadOwner.find({tile.x, tile.y});
        if(found == loadedRoadOwner.end() || found->second != want) ++ownerMismatch;
    }
    check(ownerMismatch == 0, "substrate owner differs on " + std::to_string(ownerMismatch) + " tiles");

    // Structures and units: every expected entity must exist, at its tile, for
    // its faction, with its health inside the 0..256 quantization.
    std::multimap<std::pair<int,int>, const StructureSnapshot*> loadedStructures;
    for(const auto& structure : loaded.structures) loadedStructures.insert({{structure.x, structure.y}, &structure});
    int structureMissing = 0, structureHealth = 0;
    for(const auto& want : expected.structures) {
        const auto range = loadedStructures.equal_range({want.x, want.y});
        const StructureSnapshot* match = nullptr;
        for(auto it = range.first; it != range.second; ++it)
            if(it->second->itemID == want.itemID && it->second->house == want.house) match = it->second;
        if(match == nullptr) { ++structureMissing; continue; }
        if(std::abs(match->health256 - want.health256) > 1) ++structureHealth;
    }
    check(structureMissing == 0, std::to_string(structureMissing) + " exported structures did not survive the loader");
    check(structureHealth == 0, std::to_string(structureHealth) + " structures differ by more than quantization");
    check(loaded.structures.size() == expected.structures.size(),
          "structure count " + std::to_string(loaded.structures.size()) + " != "
          + std::to_string(expected.structures.size()));

    std::multimap<std::pair<int,int>, const UnitSnapshot*> loadedUnits;
    for(const auto& unit : loaded.units) loadedUnits.insert({{unit.x, unit.y}, &unit});
    std::set<Uint32> consumed;
    int unitMissing = 0, unitHealth = 0, unitAngle = 0, unitMode = 0;
    for(const auto& want : expected.units) {
        const auto range = loadedUnits.equal_range({want.x, want.y});
        const UnitSnapshot* match = nullptr;
        for(auto it = range.first; it != range.second; ++it) {
            if(consumed.count(it->second->objectID) != 0) continue;
            if(it->second->itemID == want.itemID && it->second->house == want.house) { match = it->second; break; }
        }
        if(match == nullptr) { ++unitMissing; continue; }
        consumed.insert(match->objectID);
        if(std::abs(match->health256 - want.health256) > 1) ++unitHealth;
        if(match->angle != want.angle) ++unitAngle;
        if(static_cast<int>(match->mode) != want.mode) ++unitMode;
    }
    check(unitMissing == 0, std::to_string(unitMissing) + " exported units did not survive the loader");
    check(unitHealth == 0, std::to_string(unitHealth) + " units differ by more than quantization");
    check(unitAngle == 0, std::to_string(unitAngle) + " units lost their facing");
    check(unitMode == 0, std::to_string(unitMode) + " units lost their attack mode");
    check(loaded.units.size() == expected.units.size(),
          "unit count " + std::to_string(loaded.units.size()) + " != " + std::to_string(expected.units.size()));

    // Factions: the same surviving houses, the same teams, the same balance, and
    // defeated houses must not have become playable slots.
    check(loaded.activeHouses == expected.activeHouses, "the set of surviving houses differs");
    int teamMismatch = 0, creditMismatch = 0, perHouseMismatch = 0;
    for(const int houseID : expected.activeHouses) {
        const auto want = expected.houses.find(houseID);
        const auto got = loaded.houses.find(houseID);
        if(want == expected.houses.end() || got == loaded.houses.end()) { ++perHouseMismatch; continue; }
        if(want->second.team != got->second.team) ++teamMismatch;
        if(want->second.credits != got->second.credits) ++creditMismatch;
        if(want->second.structures != got->second.structures || want->second.units != got->second.units)
            ++perHouseMismatch;
    }
    check(teamMismatch == 0, std::to_string(teamMismatch) + " houses lost their team");
    check(creditMismatch == 0, std::to_string(creditMismatch) + " houses lost their credit balance");
    check(perHouseMismatch == 0, std::to_string(perHouseMismatch) + " houses have different per-house counts");

    // Whether this is a living world rather than a bare starting map is a
    // property of the source, not of the loader, so it is reported here and
    // enforced by the caller's --min-structures/--min-units thresholds.
    SDL_Log("SCENARIO_EXPORT_WORLD: structures=%zu units=%zu concrete=%zu roads=%zu houses=%zu",
            loaded.structures.size(), loaded.units.size(), loaded.concrete.size(),
            loaded.roads.size(), loaded.activeHouses.size());

    std::ofstream out(verifyPath, std::ios::trunc);
    if(!out) fail("Could not write " + verifyPath);
    out << "size " << loaded.sizeX << ' ' << loaded.sizeY << '\n';
    out << "structures " << loaded.structures.size() << '\n';
    out << "units " << loaded.units.size() << '\n';
    out << "concrete " << loaded.concrete.size() << '\n';
    out << "roads " << loaded.roads.size() << '\n';
    out << "active_houses " << loaded.activeHouses.size() << '\n';
    out << "terrain_mismatch " << terrainMismatch << '\n';
    out << "road_missing " << roadMismatch << '\n';
    out << "added_auto_roads " << autoRoads.size() << '\n';
    out << "unexpected_roads " << unexpectedRoads << '\n';
    for(const auto& road : autoRoads) out << "auto_road " << road.x << ' ' << road.y << ' ' << road.owner << '\n';
    out << "substrate_owner_mismatch " << ownerMismatch << '\n';
    out << "structure_missing " << structureMissing << '\n';
    out << "unit_missing " << unitMissing << '\n';
    out << "unit_angle_mismatch " << unitAngle << '\n';
    out << "unit_mode_mismatch " << unitMode << '\n';
    out << "health_outside_quantization " << (structureHealth + unitHealth) << '\n';
    out << "relocated_units " << expected.relocatedUnits << '\n';
    for(const auto& entry : loaded.houses)
        out << "house " << entry.first << ' ' << entry.second.team << ' ' << entry.second.credits << ' '
            << entry.second.structures << ' ' << entry.second.units << '\n';
    for(const auto& entry : loaded.itemCounts)
        out << "item " << entry.first << ' ' << entry.second << ' ' << getItemNameByID(entry.first) << '\n';

    // Bounded ordinary match, to catch a scenario that loads but cannot be played.
    const Uint32 cycles = static_cast<Uint32>(MILLI2CYCLES(seconds * 1000));
    const Uint32 before = game->getGameCycleCount();
    Uint32 stepped = 0;
    for(; stepped < cycles && !game->isGameFinished(); ++stepped) game->updateGameState();
    const Uint32 advanced = game->getGameCycleCount() - before;
    const bool finished = game->isGameFinished();
    out << "match_seconds " << seconds << '\n';
    out << "match_cycles " << advanced << '\n';
    out << "match_finished_early " << (finished ? 1 : 0) << '\n';
    out << "failures " << gFailures.size() << '\n';
    for(const auto& failure : gFailures) out << "failure " << failure << '\n';
    out.flush();
    if(!out) fail("Could not flush " + verifyPath);

    SDL_Log("SCENARIO_EXPORT_MATCH: requested_cycles=%u advanced=%u finished_early=%d",
            cycles, advanced, finished ? 1 : 0);
    check(advanced == stepped && (stepped == cycles || finished),
          "the bounded match advanced " + std::to_string(advanced) + " cycles across "
          + std::to_string(stepped) + " steps of " + std::to_string(cycles));
    teardown(game);

    if(!gFailures.empty())
        fail(std::to_string(gFailures.size()) + " round-trip mismatches; see " + verifyPath);
    SDL_Log("SCENARIO_EXPORT_PASS: verify checks=%d", gChecks);
}

#ifdef DUNECITY_SCENARIO_FIXTURE_INC
#include DUNECITY_SCENARIO_FIXTURE_INC
#endif

} // namespace ScenarioExport

int runSaveScenarioExport() {
    const std::string mode = ScenarioExport::env("SCENARIO_EXPORT_MODE", "export");
    settings.general.playerName = "Scenario export diagnostic";
    settings.network.metaServer = "http://127.0.0.1:1/";   // no external network
    try {
        if(mode == "export") ScenarioExport::runExport();
        else if(mode == "verify") ScenarioExport::runVerify();
#ifdef DUNECITY_SCENARIO_FIXTURE_INC
        else if(mode == "fixture") ScenarioExport::runFixture();
#endif
        else ScenarioExport::fail("Unknown SCENARIO_EXPORT_MODE: " + mode);
    } catch(const std::exception& error) {
        SDL_Log("SCENARIO_EXPORT_ERROR: %s", error.what());
        return MENU_QUIT_DEFAULT;
    }
    return MENU_QUIT_DEFAULT;
}
