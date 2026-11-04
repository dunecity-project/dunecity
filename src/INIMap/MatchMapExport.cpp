#include <INIMap/MatchMapExport.h>

#include <FileClasses/INIFile.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <INIMap/MapMetadata.h>
#include <INIMap/ZoneStartLevel.h>
#include <Map.h>
#include <ObjectManager.h>
#include <Tile.h>
#include <data.h>
#include <globals.h>
#include <misc/SDL2pp.h>
#include <misc/WebRuntime.h>
#include <misc/fnkdat.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <sand.h>
#include <structures/Refinery.h>
#include <structures/StructureBase.h>
#include <structures/WindTrap.h>
#include <structures/ZoneStructure.h>
#include <units/Carryall.h>
#include <units/UnitBase.h>

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace MatchMapExport {
namespace {

namespace fs = std::filesystem;
constexpr std::size_t kDefaultNameLength = 64;
constexpr std::size_t kMaxNameLength = 120;

std::unique_ptr<INIFile> readMap(const std::string& data) {
    if(data.empty() || data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())
       || data.find('\0') != std::string::npos)
        throw std::runtime_error("The original map data is missing or is not an INI map.");
    auto input = sdl2::RWops_ptr{SDL_RWFromConstMem(data.data(), static_cast<int>(data.size()))};
    if(!input) throw std::runtime_error("The map data could not be opened.");
    auto ini = std::make_unique<INIFile>(input.get());
    if(!ini->hasSection("MAP")) throw std::runtime_error("The map data has no MAP section.");
    return ini;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool forbiddenNameByte(unsigned char c) {
    return c < 32 || c == 127 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos;
}

bool reservedName(const std::string& name) {
    const std::string base = lower(name.substr(0, name.find('.')));
    if(base == "con" || base == "prn" || base == "aux" || base == "nul"
       || base == "conin$" || base == "conout$" || base == "clock$") return true;
    return base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0)
        && base[3] >= '1' && base[3] <= '9';
}

std::string withoutExtension(std::string name) {
    if(name.size() >= 4 && lower(name.substr(name.size() - 4)) == ".ini") name.resize(name.size() - 4);
    return name;
}

bool validUTF8(std::string_view text) {
    for(std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if(first < 0x80) continue;
        const int following = first >= 0xc2 && first <= 0xdf ? 1
            : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
        if(!following || text.size() - i < static_cast<std::size_t>(following)) return false;
        const auto second = static_cast<unsigned char>(text[i]);
        if((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)
           || (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return false;
        for(int n = 0; n < following; ++n)
            if((static_cast<unsigned char>(text[i++]) & 0xc0u) != 0x80u) return false;
    }
    return true;
}

std::string checkedStem(const std::string& mapName) {
    std::string name = withoutExtension(mapName);
    if(name.empty() || name.size() > kMaxNameLength || name.front() == ' ' || name.back() == ' '
       || name.back() == '.' || reservedName(name) || !validUTF8(name)
       || std::any_of(name.begin(), name.end(), [](unsigned char c) { return forbiddenNameByte(c); }))
        throw std::runtime_error("Choose a UTF-8 map name without path separators, reserved filenames, trailing dots or spaces (120 bytes maximum).");
    return name;
}

void truncateUTF8(std::string& text, std::size_t length) {
    if(text.size() <= length) return;
    while(length > 0 && (static_cast<unsigned char>(text[length]) & 0xc0u) == 0x80u) --length;
    text.resize(length);
}

void trimName(std::string& text) {
    const auto begin = text.find_first_not_of(" .\t\r\n");
    if(begin == std::string::npos) { text.clear(); return; }
    text.erase(0, begin);
    text.erase(text.find_last_not_of(" .\t\r\n") + 1);
}

std::string suggestedName(const Game& game, const INIFile& source) {
    std::string name = source.getStringValue("BASIC", "Name");
    if(name.empty()) name = source.getStringValue("BASIC", "Description");
    if(name.empty()) name = fs::u8path(game.getGameInitSettings().getFilename()).stem().u8string();
    name = withoutExtension(std::move(name));
    for(char& c : name) if(forbiddenNameByte(static_cast<unsigned char>(c))) c = '-';
    if(!validUTF8(name)) for(char& c : name) if(static_cast<unsigned char>(c) >= 0x80) c = '-';
    trimName(name);
    if(name.empty()) name = "Saved map";
    if(reservedName(name)) name = "Map - " + name;

    std::string seed = source.getStringValue("RANDOMSKIRMISH", "Seed");
    if(seed.empty()) seed = source.getStringValue("CIVILIZATIONWARS", "Seed");
    if(seed.size() > 10 || !std::all_of(seed.begin(), seed.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        seed.clear();
    if(!seed.empty()) {
        // Generated display names already end in their seed. Keep it at the end even when
        // a long description must be shortened to fit the dialog's 64-byte name box.
        if(name.size() >= seed.size() && name.compare(name.size() - seed.size(), seed.size(), seed) == 0
           && (name.size() == seed.size() || !std::isdigit(static_cast<unsigned char>(name[name.size() - seed.size() - 1])))) {
            name.resize(name.size() - seed.size());
            trimName(name);
        }
        const std::string suffix = " - " + seed;
        truncateUTF8(name, kDefaultNameLength - suffix.size());
        trimName(name);
        if(name.empty()) name = "Map";
        name += suffix;
    } else {
        truncateUTF8(name, kDefaultNameLength);
        trimName(name);
    }
    return checkedStem(name);
}

std::string padded(unsigned int value, std::size_t width) {
    std::string result = std::to_string(value);
    if(result.size() < width) result.insert(0, width - result.size(), '0');
    return result;
}

char terrainChar(int terrain) {
    switch(terrain) {
        case Terrain_Sand: return '-';
        case Terrain_Dunes: return '^';
        case Terrain_Spice: return '~';
        case Terrain_ThickSpice: return '+';
        case Terrain_GreenSpice: return 'g';
        case Terrain_ThickGreenSpice: return 'G';
        case Terrain_GreenSpiceBloom: return 'b';
        case Terrain_RedSpice: return 'r';
        case Terrain_ThickRedSpice: return 'R';
        case Terrain_RedSpiceBloom: return 'B';
        case Terrain_PaleLilacSpice: return 'l';
        case Terrain_ThickPaleLilacSpice: return 'L';
        case Terrain_PaleLilacSpiceBloom: return 'i';
        case Terrain_WhiteSpice: return 'w';
        case Terrain_ThickWhiteSpice: return 'W';
        case Terrain_WhiteSpiceBloom: return 'x';
        case Terrain_Slab: // Concrete is restored separately, before buildings and roads.
        case Terrain_Rock: return '%';
        case Terrain_Mountain: return '@';
        case Terrain_SpiceBloom: return 'O';
        case Terrain_SpecialBloom: return 'Q';
        default: throw std::runtime_error("The current map contains an unsupported terrain type.");
    }
}

int health256(const ObjectBase& object) {
    const int maximum = object.getMaxHealth();
    if(maximum <= 0) throw std::runtime_error("An object has invalid maximum health.");
    return std::clamp(static_cast<int>(lround((object.getHealth() * 256) / maximum)), 1, 256);
}

void copyKeys(const INIFile& source, INIFile& target, const char* section,
              std::initializer_list<const char*> keys) {
    for(const char* key : keys)
        if(source.hasKey(section, key)) target.setStringValue(section, key, source.getStringValue(section, key));
}

std::string serialize(const INIFile& ini, std::size_t reserve) {
    std::string result;
    result.reserve(reserve);
    // INIFile already owns all quoting and line-ending rules. Its RWops writer only needs
    // write(), so no temporary file, fixed-size buffer or second INI serializer is needed.
    SDL_RWops output{};
    output.hidden.unknown.data1 = &result;
    output.write = [](SDL_RWops* stream, const void* bytes, std::size_t size, std::size_t count) -> std::size_t {
        auto& text = *static_cast<std::string*>(stream->hidden.unknown.data1);
        if(size && count > (text.max_size() - text.size()) / size) return 0;
        text.append(static_cast<const char*>(bytes), size * count);
        return count;
    };
    if(!ini.saveChangesTo(&output)) throw std::runtime_error("The current map could not be serialized.");
    return result;
}

struct UnitPlacement {
    const UnitBase* unit;
    Coord location;
    bool deployed;
};

int unitLayer(const UnitBase& unit) {
    // House::placeUnit rejects any existing ground occupant, even for an aircraft or worm.
    // Put those layers first; Ornithopters also require an empty air layer when loaded.
    if(isOrnithopterUnit(unit.getItemID())) return 0;
    if(unit.isAFlyingUnit()) return 1;
    if(isSandwormUnit(unit.getItemID())) return 2;
    return 3;
}

class Occupancy {
public:
    explicit Occupancy(const Map& map)
        : map_(map), cells_(static_cast<std::size_t>(map.getSizeX()) * map.getSizeY()) { }

    void addStructure(const StructureBase& structure) {
        const Coord at = structure.getLocation(), size = structure.getStructureSize();
        if(size.x <= 0 || size.y <= 0 || !map_.tileExists(at)
           || !map_.tileExists(at.x + size.x - 1, at.y + size.y - 1))
            throw std::runtime_error("A current structure has no valid map footprint.");
        for(int y = at.y; y < at.y + size.y; ++y) for(int x = at.x; x < at.x + size.x; ++x) {
            Cell& cell = cells_[index(x, y)];
            if(cell.structure) throw std::runtime_error("Overlapping structures cannot be represented in a map.");
            cell.structure = true;
        }
    }

    bool accepts(const UnitBase& unit, int x, int y) const {
        if(!map_.tileExists(x, y)) return false;
        const Cell& cell = cells_[index(x, y)];
        // Scenario placement rejects a ground structure even for air/worm layers.
        if(cell.structure) return false;
        if(unit.isAFlyingUnit()) return !isOrnithopterUnit(unit.getItemID()) || !cell.air;
        if(isSandwormUnit(unit.getItemID())) {
            const int terrain = map_.getTile(x, y)->getType();
            return !cell.underground && terrain != Terrain_Rock && terrain != Terrain_Slab && terrain != Terrain_Mountain;
        }
        if(cell.vehicle) return false;
        if(unit.isInfantry())
            return cell.infantry < NUM_INFANTRY_PER_TILE
                && (cell.infantry == 0 || cell.infantryTeam == unit.getOwner()->getTeamID());
        return cell.infantry == 0 && map_.getTile(x, y)->getType() != Terrain_Mountain;
    }

    void addUnit(const UnitBase& unit, const Coord& at) {
        Cell& cell = cells_[index(at.x, at.y)];
        if(unit.isAFlyingUnit()) cell.air = true;
        else if(isSandwormUnit(unit.getItemID())) cell.underground = true;
        else if(unit.isInfantry()) {
            ++cell.infantry;
            cell.infantryTeam = unit.getOwner()->getTeamID();
        } else cell.vehicle = true;
    }

    Coord place(const UnitBase& unit, Coord anchor) {
        anchor.x = std::clamp(anchor.x, 0, map_.getSizeX() - 1);
        anchor.y = std::clamp(anchor.y, 0, map_.getSizeY() - 1);
        const int maximum = std::max(map_.getSizeX(), map_.getSizeY());
        Coord chosen;
        auto placeAt = [&](int x, int y) {
            if(!accepts(unit, x, y)) return false;
            chosen = Coord(x, y);
            addUnit(unit, chosen);
            return true;
        };
        // Visit each tile once, on growing ring perimeters, rather than repeatedly
        // scanning their interiors when a crowded snapshot needs a distant tile.
        for(int radius = 0; radius < maximum; ++radius) {
            const int left = anchor.x - radius, right = anchor.x + radius;
            const int top = anchor.y - radius, bottom = anchor.y + radius;
            const int firstX = std::max(0, left), lastX = std::min(map_.getSizeX() - 1, right);
            if(top >= 0) for(int x = firstX; x <= lastX; ++x)
                if(placeAt(x, top)) return chosen;
            for(int y = std::max(0, top + 1); y <= std::min(map_.getSizeY() - 1, bottom - 1); ++y) {
                if(left >= 0 && placeAt(left, y)) return chosen;
                if(right < map_.getSizeX() && right != left && placeAt(right, y)) return chosen;
            }
            if(bottom < map_.getSizeY() && bottom != top) for(int x = firstX; x <= lastX; ++x)
                if(placeAt(x, bottom)) return chosen;
        }
        throw std::runtime_error("There is no legal map tile for " + getItemNameByID(unit.getItemID())
                                 + " (object " + std::to_string(unit.getObjectID()) + ").");
    }

private:
    struct Cell {
        bool structure = false, vehicle = false, air = false, underground = false;
        unsigned char infantry = 0;
        int infantryTeam = -1;
    };
    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * map_.getSizeX() + x; }
    const Map& map_;
    std::vector<Cell> cells_;
};

std::string currentMap(const Game& game, const INIFile& source) {
    if(currentGame != &game || !currentGameMap)
        throw std::runtime_error("The current match has no active map to export.");
    const Map& map = *currentGameMap;
    const int width = map.getSizeX(), height = map.getSizeY();
    if(width <= 0 || height <= 0 || static_cast<std::size_t>(width) * height > std::numeric_limits<int>::max())
        throw std::runtime_error("The current map dimensions cannot be represented in an INI map.");

    // Game's existing read accessors are non-const. Only their returned state is inspected;
    // no simulation command, placement routine or random generator is ever invoked here.
    Game& readableGame = const_cast<Game&>(game);
    std::array<const House*, NUM_HOUSES> houses{};
    std::array<std::string, NUM_HOUSES> houseNames;
    std::array<bool, NUM_HOUSES> participant{};
    int firstParticipant = INVALID;
    int anonymousHouse = INVALID;
    for(int h = 0; h < NUM_PLAYER_HOUSES; ++h) {
        houses[h] = readableGame.getHouse(h);
        if(houses[h]) {
            houseNames[h] = getHouseNameByNumber(static_cast<HOUSETYPE>(h));
            participant[h] = houses[h]->getTeamID() > 0 || !houses[h]->getPlayerList().empty();
            if(participant[h] && firstParticipant == INVALID) firstParticipant = h;
        } else if(anonymousHouse == INVALID && isCustomGameHouseAvailable(static_cast<HOUSETYPE>(h))) {
            anonymousHouse = h;
        }
    }
    if(firstParticipant == INVALID) throw std::runtime_error("The current match has no playable house to export.");
    const std::string anonymousName = anonymousHouse == INVALID ? std::string()
        : getHouseNameByNumber(static_cast<HOUSETYPE>(anonymousHouse));

    std::vector<const StructureBase*> structures;
    std::vector<UnitPlacement> units;
    structures.reserve(structureList.size());
    units.reserve(unitList.size());
    std::map<Uint32, Coord> containerLocations;
    const ObjectManager& objects = readableGame.getObjectManager();
    objects.forEachObject([&](Uint32, const ObjectBase* object) {
        if(!object || object->getHealth() <= 0) return;
        const House* owner = object->getOwner();
        if(!owner || owner->getHouseID() < 0 || owner->getHouseID() >= NUM_HOUSES)
            throw std::runtime_error("A current object has no valid house.");
        if(object->isAStructure()) {
            if(owner->isNatives()) throw std::runtime_error("Independent Natives structures are not supported by the map format.");
            const auto* structure = static_cast<const StructureBase*>(object);
            structures.push_back(structure);
            auto contained = [&](const UnitBase* unit) {
                if(unit) containerLocations.emplace(unit->getObjectID(), structure->getLocation());
            };
            contained(structure->getContainedRepairUnit());
            if(const auto* refinery = dynamic_cast<const Refinery*>(structure)) {
                for(int bay = 0; bay < refinery->getNumBays(); ++bay) contained(refinery->getContainedHarvester(bay));
            } else if(structure->acceptsHarvesterDropoff() && !structure->isHarvesterDropoffFree()) {
                contained(structure->getContainedHarvesterUnit());
            }
        } else if(object->isAUnit()) {
            const auto* unit = static_cast<const UnitBase*>(object);
            // Crime troopers have no map-format owner token: never turn Natives into a
            // selectable extra faction or silently transfer their hostility to a player.
            if(owner->isNatives() && object->getItemID() != Unit_Sandworm) return;
            units.push_back({unit, object->getLocation(), object->isActive() && !unit->isPickedUp() && map.tileExists(object->getLocation())});
            if(const auto* carrier = dynamic_cast<const Carryall*>(unit))
                for(Uint32 id : carrier->getCargoIds()) containerLocations.emplace(id, carrier->getLocation());
        }
    });

    Occupancy occupancy(map);
    for(const StructureBase* structure : structures) occupancy.addStructure(*structure);
    std::sort(units.begin(), units.end(), [](const UnitPlacement& a, const UnitPlacement& b) {
        const int aLayer = unitLayer(*a.unit), bLayer = unitLayer(*b.unit);
        return aLayer != bLayer ? aLayer < bLayer : a.unit->getObjectID() < b.unit->getObjectID();
    });
    // Preserve deployed units' tiles before finding room for cargo, repair-bay occupants and
    // off-map deliveries. Invalid limbo positions use their container, then their own base.
    for(bool deployed : {true, false}) for(UnitPlacement& placement : units) {
        if(placement.deployed != deployed) continue;
        Coord anchor = placement.location;
        if(!deployed) {
            const auto container = containerLocations.find(placement.unit->getObjectID());
            if(container != containerLocations.end()) anchor = container->second;
            else if(!map.tileExists(anchor)) {
                anchor = Coord(width / 2, height / 2);
                for(const StructureBase* structure : structures)
                    if(structure->getOwner() == placement.unit->getOwner()) { anchor = structure->getLocation(); break; }
            }
        }
        placement.location = occupancy.place(*placement.unit, anchor);
    }

    INIFile result(false, "Current match exported as an editable map, not a saved game.");
    // Whitelist descriptive fields instead of copying executable scenario/startup sections.
    // Research, production queues, AI teams, timeout and scheduled reinforcements start afresh.
    copyKeys(source, result, "BASIC", {"Name", "Description", "Author", "License", "LosePicture", "WinPicture", "BriefPicture"});
    result.setIntValue("BASIC", "Version", 2);
    result.setIntValue("BASIC", "MapVersion", 1);
    if(result.getStringValue("BASIC", "Name").empty()) result.setStringValue("BASIC", "Name", suggestedName(game, source));
    result.setIntValue("BASIC", "TechLevel", game.techLevel);
    result.setIntValue("BASIC", "WinFlags", game.winFlags & ~WINLOSEFLAGS_TIMEOUT);
    result.setIntValue("BASIC", "LoseFlags", game.loseFlags & ~WINLOSEFLAGS_TIMEOUT);
    result.setIntValue("BASIC", "TimeOut", 0);
    result.setIntValue("MAP", "SizeX", width);
    result.setIntValue("MAP", "SizeY", height);
    std::string row;
    row.reserve(width);
    for(int y = 0; y < height; ++y) {
        row.clear();
        for(int x = 0; x < width; ++x) row += terrainChar(map.getTile(x, y)->getType());
        result.setStringValue("MAP", padded(y, 3), row, false);
    }

    for(int h = 0; h < NUM_PLAYER_HOUSES; ++h) if(participant[h]) {
        result.setIntValue(houseNames[h], "Credits", houses[h]->getCredits());
        result.setIntValue(houseNames[h], "MaxUnit", houses[h]->getMaxUnits());
        result.setStringValue(houseNames[h], "Brain", "Team" + std::to_string(houses[h]->getTeamID()), false);
        if(houses[h]->getQuota() > 0) result.setIntValue(houseNames[h], "Quota", houses[h]->getQuota());
    }

    auto substrateOwner = [&](const Tile& tile) -> const std::string& {
        const int owner = tile.getOwner();
        return owner >= 0 && owner < NUM_PLAYER_HOUSES && !houseNames[owner].empty()
            ? houseNames[owner] : houseNames[firstParticipant];
    };
    for(int y = 0; y < height; ++y) for(int x = 0; x < width; ++x) {
        const Tile& tile = *map.getTile(x, y);
        if(tile.getType() == Terrain_Slab)
            result.setStringValue("STRUCTURES", "GEN" + padded(y * width + x, 3), substrateOwner(tile) + ",Concrete", false);
    }
    for(const StructureBase* structure : structures) {
        const Coord at = structure->getLocation();
        const std::string key = "ID" + padded(structure->getObjectID(), 3);
        std::string value = houseNames[structure->getOwner()->getHouseID()] + "," + getItemNameByID(structure->getItemID())
            + "," + std::to_string(health256(*structure)) + "," + std::to_string(at.y * width + at.x);
        if(dynamic_cast<const ZoneStructure*>(structure))
            value += ZoneStartLevel::format(std::min<int>(map.getTile(at)->getCityZoneDensity(), ZoneStartLevel::kMaxLevel));
        result.setStringValue("STRUCTURES", key, value, false);
        if(const auto* trap = dynamic_cast<const WindTrap*>(structure); trap && trap->isPowerUpgraded())
            result.setBoolValue("WINDTRAPUPGRADES", key, true);
    }
    // Concrete and Road may share a tile. Decimal zero padding gives the second entry a
    // distinct key; appending roads after buildings also restores overlays they clear.
    for(int y = 0; y < height; ++y) for(int x = 0; x < width; ++x) {
        const Tile& tile = *map.getTile(x, y);
        if(tile.isRoad()) result.setStringValue("STRUCTURES", "GEN" + padded(y * width + x, 10), substrateOwner(tile) + ",Road", false);
    }

    for(const UnitPlacement& placement : units) {
        const UnitBase& unit = *placement.unit;
        const int owner = unit.getOwner()->getHouseID();
        const std::string& house = owner == HOUSE_NATIVES
            ? (anonymousName.empty() ? houseNames[firstParticipant] : anonymousName) : houseNames[owner];
        // The reader supports Retreat even though the editor's generic mode-name helper
        // cannot write it. The INI still has no field for a destination or target object.
        const std::string mode = unit.getAttackMode() == RETREAT ? "Retreat" : getAttackModeNameByMode(unit.getAttackMode());
        const int angle = ((NUM_ANGLES - unit.getAngle() + 2) % NUM_ANGLES) * 32;
        result.setStringValue("UNITS", "ID" + padded(unit.getObjectID(), 3), house + "," + getItemNameByID(unit.getItemID())
            + "," + std::to_string(health256(unit)) + "," + std::to_string(placement.location.y * width + placement.location.x)
            + "," + std::to_string(angle) + "," + mode, false);
    }

    if(source.hasSection("CHOAM")) for(const INIFile::Key& key : source.getSection("CHOAM"))
        result.setStringValue("CHOAM", key.getKeyName(), key.getStringValue());
    // Generator provenance is descriptive. Deliberately do not replay per-house starting
    // army/city descriptions or the old Player<N> to faction mapping in a current snapshot.
    copyKeys(source, result, "RANDOMSKIRMISH", {"Seed", "MapSeed", "TerrainVersion", "Sand", "Rock", "Mountain",
             "TerrainOptionsHash", "Spice", "Sandworms", "Symmetric"});
    if(source.hasSection("CIVILIZATIONWARS")) {
        copyKeys(source, result, "CIVILIZATIONWARS", {"Version", "Seed", "Size", "TeamSize", "WormsPerLane", "Spice"});
        for(int h = 0; h < NUM_PLAYER_HOUSES; ++h) if(participant[h])
            result.setStringValue("CIVILIZATIONWARS", houseNames[h], "Team" + std::to_string(houses[h]->getTeamID()), false);
    }
    // Category is content-derived, like the editor; save() pins the exact gameplay mod in
    // the existing Workshop sidecar rather than mistaking that category for a dependency.
    result.setStringValue("BASIC", "Mod", MapMetadata::inferMod(result));
    return serialize(result, static_cast<std::size_t>(width) * height + (structures.size() + units.size()) * 80 + 4096);
}

} // namespace

bool available(const Game& game) {
    const auto& init = game.getGameInitSettings();
    if((game.gameType != GameType::CustomGame && game.gameType != GameType::CustomMultiplayer)
       || (init.getGameType() != GameType::CustomGame && init.getGameType() != GameType::CustomMultiplayer)
       || init.getFiledata().empty()) return false;
    try { return !game.isCoopCampaign(); }
    catch(const std::exception&) { return false; }
}

std::string defaultName(const Game& game) {
    if(!available(game)) throw std::runtime_error("Save as Map is only available for custom and generated skirmish matches.");
    try { return suggestedName(game, *readMap(game.getGameInitSettings().getFiledata())); }
    catch(const std::runtime_error&) { throw; }
    catch(const std::exception& error) { throw std::runtime_error(std::string("The map name could not be read: ") + error.what()); }
}

std::string mapData(const Game& game, Moment moment) {
    if(!available(game)) throw std::runtime_error("Save as Map is only available for custom and generated skirmish matches.");
    try {
        const std::string& original = game.getGameInitSettings().getFiledata();
        switch(moment) {
            case Moment::Start: return original;
            case Moment::Current: return currentMap(game, *readMap(original));
        }
        throw std::runtime_error("Choose a valid map export moment.");
    } catch(const std::runtime_error&) { throw; }
    catch(const std::exception& error) { throw std::runtime_error(std::string("The current match could not be exported: ") + error.what()); }
}

int playerCount(const std::string& data) {
    try {
        const auto source = readMap(data);
        int count = MapMetadata::countPlayers(*source);
        // A custom faction can use its own display name instead of the canonical Custom
        // spelling. Do not count its alias twice when both spellings happen to be present.
        if(ModManager::instance().isInitialized() && !source->hasSection("Custom") && !source->hasSection("Corruptique")
           && isHouseAvailable(HOUSE_CUSTOM)) {
            const std::string customName = getHouseNameByNumber(HOUSE_CUSTOM);
            if(source->hasSection(customName)) ++count;
        }
        return count;
    } catch(const std::runtime_error&) { throw; }
    catch(const std::exception& error) { throw std::runtime_error(std::string("The map players could not be read: ") + error.what()); }
}

std::string destination(const std::string& mapName, const std::string& data) {
    const std::string name = checkedStem(mapName);
    const char* directory = playerCount(data) > 1 ? "maps/multiplayer/" : "maps/singleplayer/";
    char path[FILENAME_MAX];
    if(fnkdat(directory, path, sizeof(path), FNKDAT_USER | FNKDAT_CREAT) < 0)
        throw std::runtime_error("The user maps directory could not be created or opened.");
    return (fs::u8path(path) / fs::u8path(name + ".ini")).u8string();
}

void save(const std::string& filename, const std::string& data, bool overwrite) {
    if(filename.empty() || filename.find('\0') != std::string::npos)
        throw std::runtime_error("Choose a valid map destination.");
    (void)playerCount(data);
    const fs::path path = fs::u8path(filename);
    if(!path.has_filename()) throw std::runtime_error("Choose a map filename, not a directory.");
    fs::path staged = path;
    staged += ".saving-" + Workshop::newID();
    try {
        std::error_code error;
        const auto status = fs::symlink_status(path, error);
        if(error && error != std::errc::no_such_file_or_directory) throw fs::filesystem_error("Cannot inspect map destination", path, error);
        if(fs::exists(status)) {
            if(!overwrite) throw std::runtime_error("A map with that filename already exists. Confirm overwrite first.");
            if(!fs::is_regular_file(status)) throw std::runtime_error("The map destination is not a regular file.");
        }
        std::ofstream output(staged, std::ios::binary | std::ios::trunc);
        if(!output) throw std::runtime_error("The map could not be opened for saving. Check the destination and permissions.");
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if(!output) throw std::runtime_error("The complete map could not be saved. Check free disk space.");
        if(overwrite) Workshop::replaceFile(staged, path);
        else {
            // A preflight exists() check alone races with another save. A hard-link publish
            // is exclusive and exposes only the complete staged bytes, without copying them.
#ifdef __EMSCRIPTEN__
            // Emscripten's virtual filesystems do not implement hard links; copy_file with no
            // overwrite flag still creates the destination exclusively.
            if(!fs::copy_file(staged, path, fs::copy_options::none))
                throw std::runtime_error("A map with that filename already exists. Confirm overwrite first.");
#else
            fs::create_hard_link(staged, path);
#endif
            fs::remove(staged);
        }
        auto& mods = ModManager::instance();
        if(mods.isInitialized() && !mods.getActiveModName().empty()) {
            Workshop::saveMap(path.u8string(), mods.getActiveModName());
            const std::string sidecarPath = path.u8string() + ".workshop.ini";
            INIFile metadata(sidecarPath);
            const std::string displayName = path.stem().u8string();
            if(metadata.getStringValue("Workshop", "Name") != displayName) {
                // An unchanged overwrite may reuse a previous revision's name. The browser
                // reads this display-only pointer, so the INI's original bytes stay intact.
                metadata.setStringValue("Workshop", "Name", displayName);
                if(!metadata.saveChangesTo(staged.u8string()))
                    throw std::runtime_error("The map's display name could not be saved.");
                Workshop::replaceFile(staged, fs::u8path(sidecarPath));
            }
        }
        WebRuntime::syncPersistentFiles();
    } catch(const std::exception& error) {
        std::error_code ignored;
        fs::remove(staged, ignored);
        throw std::runtime_error(std::string("Could not save map: ") + error.what());
    }
}

} // namespace MatchMapExport
