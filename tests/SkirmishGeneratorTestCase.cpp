// Random Skirmish generator: pure behaviour against the shipped ObjectData defaults.
#include <MapEditor/SkirmishGenerator.h>
#include <FileClasses/INIFile.h>
#include <INIMap/ZoneStartLevel.h>
#include <INIMap/MapMetadata.h>
#include <INIMap/MapPlayerSectionUtils.h>
#include <dunecity/CityConstants.h>
#include <dunecity/CityEffects.h>
#include <dunecity/ZonePower.h>
#include <players/TacticalSafetyPolicy.h>
#include <misc/Random.h>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <utility>

using namespace SkirmishGenerator;

namespace {

std::string sourceRoot() {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    return root ? root : ".";
}

struct ItemSpec { int id; const char* section; const char* mapName; Coord size; };
const ItemSpec kItems[] = {
    {Structure_Barracks, "Barracks", "Barracks", {2,2}}, {Structure_ConstructionYard, "Construction Yard", "Const Yard", {2,2}},
    {Structure_GunTurret, "Gun-Turret", "Turret", {1,1}}, {Structure_HeavyFactory, "Heavy Factory", "Heavy Fctry", {3,2}},
    {Structure_HighTechFactory, "Hightech Factory", "Hi-Tech", {3,2}}, {Structure_IX, "House IX", "IX", {2,2}},
    {Structure_LightFactory, "Light Factory", "Light Fctry", {2,2}}, {Structure_Palace, "Palace", "Palace", {3,3}},
    {Structure_Radar, "Radar", "Outpost", {2,2}}, {Structure_Refinery, "Refinery", "Refinery", {3,2}},
    {Structure_RepairYard, "Repair Yard", "Repair", {3,2}}, {Structure_RocketTurret, "Rocket-Turret", "R-Turret", {1,1}},
    {Structure_Silo, "Spice Silo", "Spice Silo", {2,2}}, {Structure_Slab1, "Slab1", "Concrete", {1,1}},
    {Structure_StarPort, "Starport", "Starport", {3,3}}, {Structure_Wall, "Wall", "Wall", {1,1}},
    {Structure_WindTrap, "Windtrap", "Windtrap", {2,2}}, {Structure_WOR, "WOR", "WOR", {2,2}},
    {Structure_ZoneResidential, "Residential Zone", "Residential Zone", {2,2}},
    {Structure_ZoneCommercial, "Commercial Zone", "Commercial Zone", {2,2}},
    {Structure_ZoneIndustrial, "Industrial Zone", "Industrial Zone", {2,2}}, {Structure_Road, "Road", "Road", {1,1}},
    {Structure_PoliceStation, "Police Station", "Police Station", {2,2}}, {Structure_Stadium, "Stadium", "Stadium", {3,3}},
    {Structure_Airport, "Airport", "Airport", {3,3}}, {Structure_NuclearPlant, "Nuclear Plant", "Nuclear", {3,3}},
    {Structure_ResearchLab, "Research Lab", "Research Lab", {3,2}},
    {Unit_Soldier, "Soldier", "Soldier", {1,1}}, {Unit_Trooper, "Trooper", "Trooper", {1,1}}, {Unit_Trike, "Trike", "Trike", {1,1}},
    {Unit_RaiderTrike, "Raider Trike", "Raider Trike", {1,1}}, {Unit_Quad, "Quad", "Quad", {1,1}}, {Unit_Tank, "Tank", "Tank", {1,1}},
    {Unit_Launcher, "Launcher", "Launcher", {1,1}}, {Unit_SiegeTank, "Siege Tank", "Siege Tank", {1,1}},
    {Unit_SonicTank, "Sonic Tank", "Sonic Tank", {1,1}}, {Unit_Deviator, "Deviator", "Deviator", {1,1}},
    {Unit_Devastator, "Devastator", "Devastator", {1,1}}, {Unit_MCV, "MCV", "MCV", {1,1}}, {Unit_Harvester, "Harvester", "Harvester", {1,1}},
};
const char kLetters[] = "HAOFSMNR";
const char* kHouseNames[] = {"Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar", "Mercenary", "Neutral", "Rebels"};

/// The shipped ObjectData defaults, read with ObjectData's own "Key(Letter)" override rule.
Catalogue shippedCatalogue(bool cityMode) {
    INIFile ini(sourceRoot() + "/config/ObjectData.ini.default");
    REQUIRE(ini.hasSection("Windtrap"));
    std::map<std::string, int> bySection;
    for(const auto& item : kItems) bySection[item.section] = item.id;
    auto value = [&](const std::string& section, const std::string& key, char letter, const std::string& fallbackSection) {
        const std::string special = key + "(" + letter + ")";
        if(ini.hasKey(section, special)) return ini.getStringValue(section, special);
        if(ini.hasKey(section, key)) return ini.getStringValue(section, key);
        if(ini.hasKey(fallbackSection, key)) return ini.getStringValue(fallbackSection, key);
        return std::string();
    };
    auto toId = [&](std::string name) {
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);
        const auto it = bySection.find(name);
        return it == bySection.end() ? static_cast<int>(ItemID_Invalid) : it->second;
    };
    Catalogue catalogue;
    for(int h = 0; h < 8; ++h) { catalogue.houses.push_back(h); catalogue.houseNames[h] = kHouseNames[h]; }
    for(const auto& item : kItems) {
        catalogue.itemNames[item.id] = item.mapName;
        catalogue.sizes[item.id] = item.size;
        const std::string fallback = isStructure(item.id) ? "default structure" : "default unit";
        for(int h = 0; h < 8; ++h) {
            ItemRule& rule = catalogue.rules[item.id][h];
            const std::string enabled = value(item.section, "Enabled", kLetters[h], fallback);
            rule.enabled = enabled == "true" && (cityMode || !DuneCity::isCityOnlyStructure(item.id));
            rule.price = std::atoi(value(item.section, "Price", kLetters[h], fallback).c_str());
            rule.power = std::atoi(value(item.section, "Power", kLetters[h], fallback).c_str());
            const std::string tech = value(item.section, "TechLevel", kLetters[h], fallback);
            rule.techLevel = tech.empty() ? -1 : std::atoi(tech.c_str());
            // [default unit] has no Builder of its own; ObjectData reads [default structure]'s.
            std::string builder = ini.hasKey(item.section, std::string("Builder(") + kLetters[h] + ")")
                ? ini.getStringValue(item.section, std::string("Builder(") + kLetters[h] + ")")
                : ini.getStringValue(item.section, "Builder", "Invalid");
            rule.builder = toId(builder);
            std::stringstream list(value(item.section, "Prerequisite", kLetters[h], fallback));
            for(std::string name; std::getline(list, name, ',');) if(toId(name) != ItemID_Invalid) rule.prerequisites.push_back(toId(name));
        }
    }
    return catalogue;
}

const Catalogue& vanilla() { static const Catalogue c = shippedCatalogue(false); return c; }
const Catalogue& city() { static const Catalogue c = shippedCatalogue(true); return c; }

Params params(int size, int allies, int enemies, Mode mode, int techLevel, uint32_t seed) {
    Params p;
    p.mapSize = size; p.allies = allies; p.enemies = enemies; p.mode = mode; p.techLevel = techLevel; p.seed = seed;
    p.house = HOUSE_ATREIDES;
    return p;
}

constexpr std::array<int, 8> kLegacyGeologicalTotals{15, 25, 35, 50, 65, 80, 90, 100};

void setTerrainShares(Params& p, int sand, int rock, int mountain) {
    p.sandPercent = sand; p.rockPercent = rock; p.mountainPercent = mountain;
}

bool isSpiceTile(TERRAINTYPE t) {
    return t == Terrain_Spice || t == Terrain_ThickSpice || t == Terrain_SpiceBloom || t == Terrain_SpecialBloom;
}

struct TerrainCounts { int sand = 0, rock = 0, mountain = 0; };

TerrainCounts terrainCounts(const MapData& map) {
    TerrainCounts counts;
    for(int y = 0; y < map.getSizeY(); ++y) for(int x = 0; x < map.getSizeX(); ++x) {
        const TERRAINTYPE t = map(x, y);
        if(t == Terrain_Rock) ++counts.rock;
        else if(t == Terrain_Mountain) ++counts.mountain;
        else {
            REQUIRE((t == Terrain_Sand || t == Terrain_Dunes || isSpiceTile(t)));
            ++counts.sand;    // dunes, spice and blooms still occupy the Sand share
        }
    }
    return counts;
}

/// Mountain painting may neither dirty a start's core nor isolate any harvestable spice.
void requireCleanConnected(const Result& r) {
    REQUIRE_FALSE(r.houses.empty());
    std::vector<Coord> points;
    points.reserve(r.houses.size() + r.map.getSizeX() * r.map.getSizeY());
    for(const HouseSetup& h : r.houses) {
        points.push_back(h.start);
        for(int y = h.plateau.y; y < h.plateau.y + h.plateau.h; ++y)
            for(int x = h.plateau.x; x < h.plateau.x + h.plateau.w; ++x) REQUIRE(r.map(x, y) == Terrain_Rock);
    }
    for(int y = 0; y < r.map.getSizeY(); ++y) for(int x = 0; x < r.map.getSizeX(); ++x)
        if(isSpiceTile(r.map(x, y))) points.emplace_back(x, y);
    REQUIRE(groundConnected(r.map, points));
}

bool isZoneItem(int item) {
    return item == Structure_ZoneResidential || item == Structure_ZoneCommercial || item == Structure_ZoneIndustrial;
}

/// Every placement against the generated terrain, as the loader will meet it.
void requireLegal(const Result& r, const Catalogue& c) {
    const int size = r.map.getSizeX();
    std::vector<int> owner(size * size, 0);   // 0 free, 1 building, 2 tile entry, 3 unit
    for(const Placement& p : r.structures) {
        const Coord d = c.sizes[p.item];
        INFO("structure " << c.itemNames[p.item] << " at " << p.x << "," << p.y);
        REQUIRE(d.x > 0);
        int rockTiles = 0;
        for(int dy = 0; dy < d.y; ++dy) for(int dx = 0; dx < d.x; ++dx) {
            REQUIRE(r.map.isInsideMap(p.x + dx, p.y + dy));
            const TERRAINTYPE t = r.map(p.x + dx, p.y + dy);
            if(isZoneItem(p.item)) REQUIRE(DuneCity::isCityZoneTerrain(t));
            else REQUIRE(t == Terrain_Rock);
            rockTiles += t == Terrain_Rock;
            REQUIRE(owner[(p.y + dy) * size + p.x + dx] == 0);
            owner[(p.y + dy) * size + p.x + dx] = 1;
        }
        REQUIRE(rockTiles > 0);
        if(isZoneItem(p.item)) REQUIRE((p.level >= 0 && p.level <= 3));
    }
    std::set<int> tileKeys;
    for(const Placement& p : r.tiles) {
        INFO("tile " << c.itemNames[p.item] << " at " << p.x << "," << p.y);
        REQUIRE(r.map.isInsideMap(p.x, p.y));
        REQUIRE(r.map(p.x, p.y) == Terrain_Rock);
        REQUIRE(tileKeys.insert(p.y * size + p.x).second);    // GEN keys are unique
        if(p.item == Structure_Slab1) REQUIRE(owner[p.y * size + p.x] == 1);    // concrete under a building
        else { REQUIRE(owner[p.y * size + p.x] == 0); owner[p.y * size + p.x] = 2; }
    }
    // A vehicle owns its tile; Troopers may share one, up to the engine's five per tile.
    std::map<int, int> infantryOn;
    for(const Placement& p : r.units) {
        INFO("unit " << c.itemNames[p.item] << " at " << p.x << "," << p.y);
        REQUIRE(r.map.isInsideMap(p.x, p.y));
        REQUIRE(r.map(p.x, p.y) != Terrain_Mountain);
        const int key = p.y * size + p.x;
        if(p.item == Unit_Trooper || p.item == Unit_Soldier) {
            REQUIRE((owner[key] == 0 || (owner[key] == 4 && infantryOn[key] < 5)));
            owner[key] = 4;
            ++infantryOn[key];
        } else {
            REQUIRE(owner[key] == 0);
            owner[key] = 3;
        }
    }
}

int countSections(const std::string& text, const std::string& section) {
    int n = 0;
    std::istringstream in(text);
    bool inside = false;
    for(std::string line; std::getline(in, line);) {
        if(!line.empty() && line[0] == '[') { inside = line == "[" + section + "]"; continue; }
        if(inside && (line.rfind("ID", 0) == 0 || line.rfind("GEN", 0) == 0)) ++n;
    }
    return n;
}

}

TEST_CASE("Random skirmish: same seed and parameters give identical INI text", "[skirmish][generator]") {
    for(Mode mode : {Mode::Vanilla, Mode::DuneCity}) for(bool symmetric : {false, true}) {
        Params p = params(128, 1, 2, mode, 8, 4242);
        p.symmetric = symmetric;
        setTerrainShares(p, symmetric ? 50 : 60, symmetric ? 20 : 40, symmetric ? 30 : 0);
        p.allySide.city = 2; p.enemySide.city = 2;
        const Catalogue& c = mode == Mode::DuneCity ? city() : vanilla();
        const Result a = generate(p, c), b = generate(p, c);
        REQUIRE(a.ini == b.ini);
        REQUIRE(a.ini.size() > 128 * 128);
        p.seed = 4243;
        REQUIRE(generate(p, c).ini != a.ini);
    }
}

namespace {

/// FNV-1a (64-bit) over bytes: explicit and independent of byte order, so a pinned value holds
/// on every compiler and platform.
uint64_t fnv1a(const std::string& bytes) {
    uint64_t hash = 14695981039346656037ull;
    for(unsigned char c : bytes) { hash ^= c; hash *= 1099511628211ull; }
    return hash;
}

uint64_t terrainDigest(const MapData& map) {
    std::string tiles;
    for(int y = 0; y < map.getSizeY(); ++y) for(int x = 0; x < map.getSizeX(); ++x) tiles += static_cast<char>(map(x, y));
    return fnv1a(tiles);
}

struct LocalStartRock { int tiles = 0; int compactSide = 0; };

/// Independent terrain inspection: neither the public flood nor generator quota diagnostics
/// decide which tiles belong to this component or whether full building footprints fit.
LocalStartRock inspectStartRock(const MapData& map, const Coord& start, const Rect& berth) {
    std::vector<unsigned char> reached(berth.w * berth.h, 0);
    std::vector<Coord> pending;
    pending.reserve(reached.size());
    pending.push_back(start);
    reached[(start.y - berth.y) * berth.w + start.x - berth.x] = 1;
    for(size_t i = 0; i < pending.size(); ++i) {
        const Coord tile = pending[i];
        for(const Coord& step : {Coord(-1, 0), Coord(1, 0), Coord(0, -1), Coord(0, 1)}) {
            const int x = tile.x + step.x, y = tile.y + step.y;
            if(x < berth.x || y < berth.y || x >= berth.x + berth.w || y >= berth.y + berth.h) continue;
            const int key = (y - berth.y) * berth.w + x - berth.x;
            if(reached[key] || map(x, y) != Terrain_Rock) continue;
            reached[key] = 1;
            pending.emplace_back(x, y);
        }
    }
    LocalStartRock rock;
    rock.tiles = static_cast<int>(pending.size());
    // Largest completely ordinary-rock square, restricted to the start's component.
    // A winding one-tile corridor cannot masquerade as room for a future base or city.
    std::vector<int> squares(berth.w + 1, 0);
    for(int y = 0; y < berth.h; ++y) {
        int diagonal = 0;
        for(int x = 0; x < berth.w; ++x) {
            const int above = squares[x + 1];
            squares[x + 1] = reached[y * berth.w + x] ? 1 + std::min({above, squares[x], diagonal}) : 0;
            diagonal = above;
            rock.compactSide = std::max(rock.compactSide, squares[x + 1]);
        }
    }
    return rock;
}

void requireFairStartRock(const Result& r, Mode mode) {
    REQUIRE_FALSE(r.houses.empty());
    const Rect& first = r.houses.front().rockBerth;
    const int goal = mode == Mode::DuneCity ? kCityStartRockBerthSide : kVanillaStartRockBerthSide;
    REQUIRE(first.w >= 8);
    REQUIRE(first.w <= goal);
    REQUIRE(first.w % 2 == 0);
    const int area = first.w * first.w;
    const int floor = (area * kStartRockFairnessPercent + 99) / 100;
    int smallest = area, largest = 0;
    for(const HouseSetup& h : r.houses) {
        const Rect& b = h.rockBerth;
        INFO("house " << h.house << " start " << h.start.x << "," << h.start.y
             << " berth " << b.x << "," << b.y << " " << b.w << "x" << b.h << " floor " << h.minimumRock);
        REQUIRE(b.w == first.w);
        REQUIRE(b.h == first.w);
        REQUIRE(b.x >= 0);
        REQUIRE(b.y >= 0);
        REQUIRE(b.x + b.w <= r.map.getSizeX());
        REQUIRE(b.y + b.h <= r.map.getSizeY());
        REQUIRE(b.contains(h.start.x, h.start.y));
        REQUIRE(r.map(h.start.x, h.start.y) == Terrain_Rock);
        REQUIRE(h.minimumRock == floor);
        const LocalStartRock rock = inspectStartRock(r.map, h.start, b);
        CAPTURE(rock.tiles, rock.compactSide);
        REQUIRE(contiguousBuildableRock(r.map, h.start, b) == rock.tiles);
        REQUIRE(rock.tiles >= floor);
        REQUIRE(rock.tiles <= area);
        REQUIRE(rock.compactSide * rock.compactSide >= floor);
        smallest = std::min(smallest, rock.tiles);
        largest = std::max(largest, rock.tiles);
    }
    REQUIRE(smallest * 100 >= largest * kStartRockFairnessPercent);
}

Params expansionParams(int size, int players, Mode mode, uint32_t seed) {
    Params p = params(size, 0, players - 1, mode, 9, seed);
    p.mod = mode == Mode::DuneCity ? "dunecity" : "vanilla";
    p.enemiesAllied = false;
    for(Side* side : {&p.playerSide, &p.allySide, &p.enemySide}) {
        side->base = BaseSize::None;
        side->army = 0;
        side->city = 0;
    }
    return p;
}

void requireRepeatableStartRock(const Result& a, const Result& b) {
    REQUIRE(a.ini == b.ini);
    REQUIRE(terrainDigest(a.map) == terrainDigest(b.map));
    REQUIRE(a.mapSeed == b.mapSeed);
    REQUIRE(a.attempts == b.attempts);
    REQUIRE(a.flattenedMountains == b.flattenedMountains);
    REQUIRE(a.houses.size() == b.houses.size());
    for(size_t i = 0; i < a.houses.size(); ++i) {
        const HouseSetup& h = a.houses[i];
        const HouseSetup& repeated = b.houses[i];
        REQUIRE(h.house == repeated.house);
        REQUIRE(h.start.x == repeated.start.x);
        REQUIRE(h.start.y == repeated.start.y);
        REQUIRE(h.rockBerth.x == repeated.rockBerth.x);
        REQUIRE(h.rockBerth.y == repeated.rockBerth.y);
        REQUIRE(h.rockBerth.w == repeated.rockBerth.w);
        REQUIRE(h.rockBerth.h == repeated.rockBerth.h);
        REQUIRE(h.minimumRock == repeated.minimumRock);
    }
}

void requireMirroredStartRock(const Result& r, int players) {
    const int size = r.map.getSizeX();
    bool pointMirrored = true, axisMirrored = true;
    for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) {
        pointMirrored = pointMirrored && r.map(x, y) == r.map(size - 1 - x, size - 1 - y);
        if(players > 2) axisMirrored = axisMirrored && r.map(x, y) == r.map(size - 1 - x, y);
    }
    REQUIRE(pointMirrored);
    REQUIRE(axisMirrored);
    for(const HouseSetup& h : r.houses) {
        const Rect& b = h.rockBerth;
        REQUIRE(std::any_of(r.houses.begin(), r.houses.end(), [&](const HouseSetup& other) {
            return other.rockBerth.x == size - b.x - b.w && other.rockBerth.y == size - b.y - b.h;
        }));
        if(players > 2) REQUIRE(std::any_of(r.houses.begin(), r.houses.end(), [&](const HouseSetup& other) {
            return other.rockBerth.x == size - b.x - b.w && other.rockBerth.y == b.y;
        }));
    }
}

/// The seed sweep is one Catch test case per mode and kMapSizes entry (mode-major), so each
/// group runs and is timed on its own. The groups follow kMapSizes; nothing copies the list.
constexpr std::array<Mode, 2> kSweepModes{Mode::Vanilla, Mode::DuneCity};
template<class Indices> struct IndexTypes;
template<size_t... Index> struct IndexTypes<std::index_sequence<Index...>> {
    using type = std::tuple<std::integral_constant<size_t, Index>...>;
};
using RockFairnessGroups = IndexTypes<std::make_index_sequence<kSweepModes.size() * kMapSizes.size()>>::type;

} // namespace

TEST_CASE("Random skirmish: pinned seeds give the same map on every platform", "[skirmish][generator][determinism]") {
    // Terrain-version-3 pins measured on both macOS and Windows (76cb1cb6, 2026-11-04).
    // Every terrain/INI pair matched. Coord(random.rand(..), random.rand(..)) once differed
    // between clang and MSVC argument evaluation orders, so all platforms must agree.
    struct Pin { int size; uint32_t seed; Mode mode; uint64_t terrain, ini; };
    const Pin pins[] = {
        {64, 1u, Mode::Vanilla, 0xf675d9692d12eb6dull, 0xe6522b7963e9b8e3ull},
        {64, 1u, Mode::DuneCity, 0xd20b6e1c298109f1ull, 0xbec43f9fb460977cull},
        {64, 7101u, Mode::Vanilla, 0xd9e704c804d5f651ull, 0x1c52328a1fc2f08full},
        {64, 7101u, Mode::DuneCity, 0xd41b3361210beb75ull, 0xef587ee486abf8c6ull},
        {64, 7202u, Mode::Vanilla, 0x4e56dc69b5e8d14cull, 0x76841cfd2a4dcc17ull},
        {64, 7202u, Mode::DuneCity, 0x2e8a8437ea769c22ull, 0xe7a62a5575237bfdull},
        {64, 7303u, Mode::Vanilla, 0x0fe3b2621bb75cc9ull, 0xcbd5741232f3d074ull},
        {64, 7303u, Mode::DuneCity, 0x131cc35cbbbcc9d1ull, 0x62bd6b03d1b4c0d5ull},
        {64, 4000000000u, Mode::Vanilla, 0x48ff4839149eab41ull, 0xcb05a09a25204a62ull},
        {64, 4000000000u, Mode::DuneCity, 0x955b29fc24b81c94ull, 0x6bb442078b00b535ull},
        {128, 1u, Mode::Vanilla, 0x9b0019382936e411ull, 0xda1694c9b30937bfull},
        {128, 1u, Mode::DuneCity, 0x613ce0c81e9ab971ull, 0x0355e3f1563d9ecbull},
        {128, 7101u, Mode::Vanilla, 0xb851766b5a2d8aa5ull, 0xe29a02b31cf33096ull},
        {128, 7101u, Mode::DuneCity, 0x3892f03b95550631ull, 0x83a36ef5885c5ee3ull},
        {128, 7202u, Mode::Vanilla, 0x031585c1bda1ac31ull, 0x2c861f953975560eull},
        {128, 7202u, Mode::DuneCity, 0xdc2066d30642d1fcull, 0x89156c0584006090ull},
        {128, 7303u, Mode::Vanilla, 0x9cddcab27546bb29ull, 0x28d504cd36932471ull},
        {128, 7303u, Mode::DuneCity, 0x163b6c8da3dc4f35ull, 0xe0ffbccf572d8d99ull},
        {128, 4000000000u, Mode::Vanilla, 0xe5229b0bef357ac3ull, 0xbef80eb9f8dfacdcull},
        {128, 4000000000u, Mode::DuneCity, 0xf3c4586c6516865bull, 0xbad587a384d48d65ull},
        {192, 1u, Mode::Vanilla, 0x909751990f473319ull, 0x2dbf1698476e07c4ull},
        {192, 1u, Mode::DuneCity, 0x5939cc5dc8b31509ull, 0xace0f0524a68e954ull},
        {192, 7101u, Mode::Vanilla, 0xa194db9e1bbc85d1ull, 0x921ba2cc93388174ull},
        {192, 7101u, Mode::DuneCity, 0xe76bb0b1059e2cf9ull, 0xb62bf1854bf7ab13ull},
        {192, 7202u, Mode::Vanilla, 0xc38dac93d763f267ull, 0xa7b2c74b4e43d0c1ull},
        {192, 7202u, Mode::DuneCity, 0x6dd975176da392e1ull, 0x51f01ce507521afaull},
        {192, 7303u, Mode::Vanilla, 0x95109f250d09e7c5ull, 0xd344f11ffc52f916ull},
        {192, 7303u, Mode::DuneCity, 0xa220afcaf7c203b9ull, 0x2a135a8045796a0cull},
        {192, 4000000000u, Mode::Vanilla, 0x9f00c198703b5b16ull, 0xd261524355186501ull},
        {192, 4000000000u, Mode::DuneCity, 0x240deb4f41d4d7d5ull, 0x7a832e638e1307ddull},
    };
    for(const Pin& pin : pins) {
        Params p = params(pin.size, 1, 2, pin.mode, 8, pin.seed);
        p.symmetric = pin.seed % 2 == 1;            // odd seeds mirror the starts
        p.allySide.city = p.enemySide.city = 2;
        const Result r = generate(p, pin.mode == Mode::DuneCity ? city() : vanilla());
        const uint64_t actualTerrain = terrainDigest(r.map), actualIni = fnv1a(r.ini);
        CAPTURE(pin.size, pin.seed, pin.mode == Mode::DuneCity, actualTerrain, actualIni);
        INFO("actual pin {" << pin.size << ", " << pin.seed << "u, Mode::"
             << (pin.mode == Mode::DuneCity ? "DuneCity" : "Vanilla") << ", 0x"
             << std::hex << actualTerrain << "ull, 0x" << actualIni << "ull}");
        CHECK(actualTerrain == pin.terrain);
        CHECK(actualIni == pin.ini);
    }
}

TEST_CASE("Random skirmish: houses, teams and the engine cap", "[skirmish][generator]") {
    Params p = params(128, 2, 3, Mode::Vanilla, 8, 7);
    p.enemiesAllied = true;
    Result r = generate(p, vanilla());
    REQUIRE(r.houses.size() == 6);
    std::set<int> houses;
    for(const auto& h : r.houses) houses.insert(h.house);
    REQUIRE(houses.size() == 6);
    REQUIRE(r.houses[0].role == Role::Human);
    REQUIRE(r.houses[0].house == HOUSE_ATREIDES);
    REQUIRE(r.houses[0].playerClass == "HumanPlayer");
    for(int i = 1; i <= 2; ++i) { REQUIRE(r.houses[i].role == Role::Ally); REQUIRE(r.houses[i].team == 1); }
    for(int i = 3; i <= 5; ++i) { REQUIRE(r.houses[i].role == Role::Enemy); REQUIRE(r.houses[i].team == 2); }

    p.enemiesAllied = false;
    r = generate(p, vanilla());
    std::set<int> enemyTeams;
    for(int i = 3; i <= 5; ++i) { REQUIRE(r.houses[i].team != 1); enemyTeams.insert(r.houses[i].team); }
    REQUIRE(enemyTeams.size() == 3);
    for(int i = 1; i <= 2; ++i) REQUIRE(r.houses[i].team == 1);

    // Allies 0-6, enemies 1-7, at most 7 together (and within the engine's house cap).
    p.allies = 6; p.enemies = 7;
    p.clamp(8);
    REQUIRE(p.allies == 6); REQUIRE(p.enemies == 1);
    p.allies = 9; p.enemies = 9;
    p.clamp(9);                                    // a ninth custom house does not lift the cap
    REQUIRE(p.allies == kMaxAllies); REQUIRE(p.allies + p.enemies == kMaxOtherHouses);
    p.allies = 0; p.enemies = 7;
    p.clamp(8);
    REQUIRE(p.allies == 0); REQUIRE(p.enemies == 7);
    p.allies = 3; p.enemies = 5;
    p.clamp(6);                                    // a smaller engine cap still wins
    REQUIRE(p.allies + p.enemies == 5); REQUIRE(p.allies == 3);
    REQUIRE(generate(params(128, 6, 1, Mode::Vanilla, 9, 1), vanilla()).houses.size() == 8);
    const Result seven = generate(params(128, 0, 7, Mode::Vanilla, 9, 2), vanilla());
    REQUIRE(seven.houses.size() == 8);
    requireLegal(seven, vanilla());

    // A random player house is still a real, seeded choice.
    Params random = params(96, 0, 1, Mode::Vanilla, 8, 99);
    random.house = HOUSE_INVALID;
    const Result a = generate(random, vanilla()), b = generate(random, vanilla());
    REQUIRE(a.houses[0].house >= 0);
    REQUIRE(a.houses[0].house == b.houses[0].house);
}

TEST_CASE("Random skirmish: base templates follow tech level, faction and power", "[skirmish][generator]") {
    const Catalogue& c = vanilla();
    for(int tier = 0; tier <= static_cast<int>(BaseSize::Fortress); ++tier) for(int tech = 1; tech <= 9; ++tech)
        for(int house = 0; house < 8; ++house) {
            std::vector<int> items = baseTemplate(static_cast<BaseSize>(tier), house, tech, c);
            for(int item : items) {
                INFO("tier " << tier << " tech " << tech << " house " << house << " item " << c.itemNames[item]);
                REQUIRE(c.available(item, house, tech));
            }
            addPower(items, house, tech, 0, c);
            if(!items.empty() && std::any_of(items.begin(), items.end(), [&](int i) { return c.rule(i, house).power > 0; }))
                REQUIRE(powerBalance(items, house, 0, c) > 0);
        }
    // Concrete boundaries from the shipped data.
    REQUIRE_FALSE(c.available(Structure_Radar, HOUSE_ATREIDES, 1));
    REQUIRE(c.available(Structure_Radar, HOUSE_ATREIDES, 2));
    REQUIRE_FALSE(c.available(Structure_IX, HOUSE_ATREIDES, 6));
    REQUIRE(c.available(Structure_IX, HOUSE_ATREIDES, 7));
    REQUIRE_FALSE(c.available(Structure_Barracks, HOUSE_HARKONNEN, 9));     // Builder(H) = Invalid
    REQUIRE(c.available(Structure_WOR, HOUSE_HARKONNEN, 2));                // TechLevel(H) = 2
    const auto small = baseTemplate(BaseSize::Small, HOUSE_HARKONNEN, 8, c);
    REQUIRE(std::count(small.begin(), small.end(), Structure_WOR) == 1);    // Barracks slot falls back
    const auto fortress = baseTemplate(BaseSize::Fortress, HOUSE_ATREIDES, 8, c);
    REQUIRE(std::count(fortress.begin(), fortress.end(), Structure_Palace) == 1);
    const auto fortressTech7 = baseTemplate(BaseSize::Fortress, HOUSE_ATREIDES, 7, c);
    REQUIRE(std::count(fortressTech7.begin(), fortressTech7.end(), Structure_Palace) == 0);    // Palace is tech 8
    REQUIRE(baseTemplate(BaseSize::Outpost, HOUSE_ORDOS, 1, c) == std::vector<int>{Structure_ConstructionYard, Structure_Refinery});

    // Whole generated bases, city draw included, run on positive power.
    for(Mode mode : {Mode::Vanilla, Mode::DuneCity}) {
        Params p = params(160, 2, 3, mode, 8, 31);
        p.allySide.base = BaseSize::Large; p.enemySide.base = BaseSize::Fortress;
        p.allySide.city = 3; p.enemySide.city = 4;
        const Catalogue& cat = mode == Mode::DuneCity ? city() : vanilla();
        const Result r = generate(p, cat);
        for(const auto& h : r.houses) {
            std::vector<int> items;
            int zoneDraw = 0;
            for(const auto& s : r.structures) if(s.house == h.house) {
                if(isZoneItem(s.item)) zoneDraw += DuneCity::getZonePower(s.item, s.level);
                else items.push_back(s.item);
            }
            INFO("house " << h.house);
            if(!items.empty()) REQUIRE(powerBalance(items, h.house, zoneDraw, cat) > 0);    // the player's own base too
        }
    }
}

TEST_CASE("Random skirmish: armies spend 90-100% of the budget with a per-type cap", "[skirmish][generator]") {
    const Catalogue& c = vanilla();
    for(int budget : {1000, 2500, 5000, 10000, 20000}) for(int tech = 1; tech <= 9; ++tech) for(int house = 0; house < 8; ++house) {
        const auto types = armyTypes(house, tech, c);
        Random random(static_cast<uint32_t>(budget * 31 + tech * 7 + house));
        const auto army = buyArmy(budget, house, tech, c, random);
        INFO("budget " << budget << " tech " << tech << " house " << house);
        if(types.empty()) { REQUIRE(army.empty()); continue; }
        std::map<int, int> perType;
        int spent = 0;
        for(int unit : army) {
            REQUIRE(c.available(unit, house, tech));
            spent += c.rule(unit, house).price;
            perType[unit] += c.rule(unit, house).price;
        }
        REQUIRE(spent <= budget);
        if(c.rule(types.front(), house).price * 10 <= budget) REQUIRE(spent * 10 >= budget * 9);
        const int capPercent = std::max(kArmyTypeCapPercent, (100 + static_cast<int>(types.size()) - 1) / static_cast<int>(types.size()));
        for(const auto& [unit, value] : perType) REQUIRE(value * 100 <= budget * capPercent);
    }
    // At tech 1 nothing that builds units exists yet; at tech 2 infantry and trikes do.
    REQUIRE(armyTypes(HOUSE_ATREIDES, 1, c).empty());
    REQUIRE_FALSE(armyTypes(HOUSE_ATREIDES, 2, c).empty());
    const auto atreides = armyTypes(HOUSE_ATREIDES, 9, c);
    REQUIRE(std::count(atreides.begin(), atreides.end(), Unit_Devastator) == 0);    // Builder(A) = Invalid
    const auto harkonnen = armyTypes(HOUSE_HARKONNEN, 9, c);
    REQUIRE(std::count(harkonnen.begin(), harkonnen.end(), Unit_Devastator) == 1);

    // In a generated map the placed army is the army that was bought.
    Params p = params(128, 1, 2, Mode::Vanilla, 8, 5);
    p.allySide.army = 4; p.enemySide.army = 5;
    const Result r = generate(p, c);
    for(const auto& h : r.houses) if(h.role != Role::Human) {
        REQUIRE(h.armySpent <= h.armyBudget);
        REQUIRE(h.armySpent * 10 >= h.armyBudget * 9);
        REQUIRE(h.skipped == 0);
    }
}

TEST_CASE("Random skirmish: every structure and unit is on a legal tile", "[skirmish][generator]") {
    struct Case { int size, allies, enemies; Mode mode; BaseSize base; int army, city; bool symmetric; int tech; };
    const Case cases[] = {
        {64, 0, 1, Mode::Vanilla, BaseSize::Small, 2, 0, false, 8},
        {128, 2, 3, Mode::Vanilla, BaseSize::Large, 4, 0, false, 8},
        {128, 1, 1, Mode::Vanilla, BaseSize::Fortress, 5, 0, true, 9},
        {192, 1, 2, Mode::DuneCity, BaseSize::Medium, 3, 3, false, 8},
        {256, 3, 4, Mode::DuneCity, BaseSize::Large, 6, 5, true, 6},
        {96, 1, 2, Mode::Vanilla, BaseSize::None, 1, 0, false, 3},
    };
    for(const Case& k : cases) {
        Params p = params(k.size, k.allies, k.enemies, k.mode, k.tech, 1234 + k.size);
        p.symmetric = k.symmetric;
        p.allySide.base = p.enemySide.base = k.base;
        p.allySide.army = p.enemySide.army = k.army;
        p.allySide.city = p.enemySide.city = k.city;
        const Catalogue& c = k.mode == Mode::DuneCity ? city() : vanilla();
        const Result r = generate(p, c);
        INFO("size " << k.size << " base " << static_cast<int>(k.base));
        requireLegal(r, c);
        REQUIRE(countSections(r.ini, "STRUCTURES") == static_cast<int>(r.structures.size() + r.tiles.size()));
        REQUIRE(countSections(r.ini, "UNITS") == static_cast<int>(r.units.size()));
        for(const auto& h : r.houses) if(h.base == BaseSize::None && h.role != Role::Human)
            REQUIRE(std::count_if(r.units.begin(), r.units.end(), [&](const Placement& u) { return u.house == h.house && u.item == Unit_MCV; }) == 1);
        // The MCV's yard footprint (its tile and the three right of and below it) and the lane
        // round it carry no other unit, so the army never stops the opening deployment.
        for(const auto& mcv : r.units) if(mcv.item == Unit_MCV)
            for(const auto& u : r.units) {
                if(&u == &mcv) continue;
                INFO("MCV " << mcv.x << "," << mcv.y << " unit " << u.item << " at " << u.x << "," << u.y);
                REQUIRE_FALSE((u.x >= mcv.x - 1 && u.x <= mcv.x + 2 && u.y >= mcv.y - 1 && u.y <= mcv.y + 2));
            }
        // Every house's Construction Yard (or MCV) sits on its own plateau.
        for(const auto& h : r.houses) {
            bool start = false;
            for(const auto& s : r.structures) start = start || (s.house == h.house && s.item == Structure_ConstructionYard && h.plateau.contains(s.x, s.y));
            for(const auto& u : r.units) start = start || (u.house == h.house && u.item == Unit_MCV);
            REQUIRE(start);
        }
    }
}

TEST_CASE("Random skirmish: starts are ground-connected and symmetric maps mirror", "[skirmish][generator]") {
    MapData walled(32, 32);
    for(int y = 0; y < 32; ++y) walled(16, y) = Terrain_Mountain;
    REQUIRE_FALSE(groundConnected(walled, {Coord(2, 2), Coord(30, 30)}));
    REQUIRE(groundConnected(walled, {Coord(2, 2), Coord(10, 30)}));
    walled(16, 5) = Terrain_Rock;
    REQUIRE(groundConnected(walled, {Coord(2, 2), Coord(30, 30)}));

    for(int total : {15, 25, 35, 50}) for(uint32_t seed : {1u, 2u, 3u}) {
        Params p = params(128, 1, 2, Mode::Vanilla, 8, seed);
        setTerrainShares(p, 100 - total, total - 5, 5);
        const Result r = generate(p, vanilla());
        requireCleanConnected(r);
    }

    // Legacy geological presets now mean explicit Sand / passable Rock / Mountain shares.
    int previous = 0;
    for(int total : kLegacyGeologicalTotals) {
        Params p = params(128, 0, 1, Mode::Vanilla, 8, 11);
        setTerrainShares(p, 100 - total, total - 5, 5);
        p.playerSide.base = p.enemySide.base = BaseSize::Outpost;
        p.playerSide.army = p.enemySide.army = 0;
        const Result r = generate(p, vanilla());
        const TerrainCounts counts = terrainCounts(r.map);
        const int tiles = counts.rock + counts.mountain;
        const int percent = tiles * 100 / (128 * 128);
        INFO("legacy geological target " << total << " actual share " << percent);
        REQUIRE(tiles * 100 >= total * 128 * 128 - 300);
        REQUIRE(percent <= std::min(100, total + 12));
        REQUIRE(percent > previous);
        if(total == 100) REQUIRE(counts.sand == 0);
        requireCleanConnected(r);
        requireLegal(r, vanilla());
        previous = percent;
    }
    // A crowded, mirrored all-rock map stays symmetric, connected and legal.
    {
        Params p = params(128, 3, 4, Mode::DuneCity, 9, 5);
        setTerrainShares(p, 0, 100, 0); p.symmetric = true;
        const Result r = generate(p, city());
        for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) {
            REQUIRE(r.map(x, y) == Terrain_Rock);
            REQUIRE(r.map(x, y) == r.map(127 - x, 127 - y));
        }
        requireCleanConnected(r);
        requireLegal(r, city());
    }

    for(const std::array<int, 3>& shares : {std::array<int, 3>{75, 20, 5}, {50, 20, 30}, {60, 40, 0}}) {
        Params p = params(128, 1, 2, Mode::Vanilla, 8, 77);
        setTerrainShares(p, shares[0], shares[1], shares[2]);
        p.symmetric = true;
        const Result r = generate(p, vanilla());
        CAPTURE(p.sandPercent, p.rockPercent, p.mountainPercent);
        for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) {
            REQUIRE(r.map(x, y) == r.map(127 - x, y));
            REQUIRE(r.map(x, y) == r.map(x, 127 - y));
        }
        requireCleanConnected(r);
        requireLegal(r, vanilla());
        p.enemies = 1; p.allies = 0;
        const Result duel = generate(p, vanilla());
        for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) REQUIRE(duel.map(x, y) == duel.map(127 - x, 127 - y));
        requireCleanConnected(duel);
    }

    // No spice at all when asked for none.
    Params p = params(128, 0, 1, Mode::Vanilla, 8, 77);
    p.spice = Spice::None;
    const Result dry = generate(p, vanilla());
    for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) REQUIRE_FALSE(isSpiceTile(dry.map(x, y)));
}

TEST_CASE("Random skirmish: explicit terrain shares control Sand Rock and Mountain independently", "[skirmish][generator][terrain]") {
    const std::array<int, 3> mixes[] = {{75, 25, 0}, {75, 20, 5}, {50, 45, 5}, {50, 20, 30}};
    for(int size : {96, 160, 256}) {
        const int area = size * size;
        std::array<TerrainCounts, 4> actual;
        for(size_t i = 0; i < actual.size(); ++i) {
            Params p = params(size, 0, 1, Mode::Vanilla, 8, 20261103u);
            setTerrainShares(p, mixes[i][0], mixes[i][1], mixes[i][2]);
            // Two small starts leave enough room to measure the requested shares rather
            // than the forced cores of a crowded city/army map.
            p.playerSide.base = p.enemySide.base = BaseSize::Outpost;
            p.playerSide.army = p.enemySide.army = 0;
            const Result r = generate(p, vanilla());
            actual[i] = terrainCounts(r.map);
            const TerrainCounts& counts = actual[i];
            INFO("size " << size << " requested " << p.sandPercent << "/" << p.rockPercent << "/" << p.mountainPercent
                 << " actual tiles " << counts.sand << "/" << counts.rock << "/" << counts.mountain);
            REQUIRE(counts.sand + counts.rock + counts.mountain == area);
            // Whole organic editor fields may overshoot geological coverage by 12 points;
            // the independent mountain painter is bounded to the requested tile quota.
            REQUIRE(std::abs(counts.sand * 100 - p.sandPercent * area) <= 12 * area + 300);
            REQUIRE(std::abs(counts.rock * 100 - p.rockPercent * area) <= 12 * area + 300);
            REQUIRE(std::abs(counts.mountain - p.mountainPercent * area / 100) <= 3);
            if(p.mountainPercent == 0) REQUIRE(counts.mountain == 0);
            REQUIRE(counts.sand > 0);
            int spice = 0;
            for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) spice += isSpiceTile(r.map(x, y));
            REQUIRE(spice > 0);
            requireCleanConnected(r);
            requireLegal(r, vanilla());
        }
        // Raising Mountain must increase impassable terrain, not merely total geology.
        REQUIRE(actual[0].mountain < actual[1].mountain);
        REQUIRE(std::abs(actual[1].mountain - actual[2].mountain) <= 3);
        REQUIRE(actual[2].mountain < actual[3].mountain);
        REQUIRE(actual[2].rock > actual[1].rock);
        REQUIRE(actual[2].sand < actual[1].sand);
    }
}

TEST_CASE("Random skirmish: buildable rock flood counts only bounded orthogonal ordinary rock",
          "[skirmish][generator][terrain][rockfairness]") {
    MapData map(12, 9);
    for(int y = 0; y < 9; ++y) for(int x = 0; x < 12; ++x) map(x, y) = Terrain_Sand;
    for(int y = 2; y <= 4; ++y) {
        for(int x = 2; x <= 4; ++x) map(x, y) = Terrain_Rock;
        for(int x = 6; x <= 8; ++x) map(x, y) = Terrain_Rock;
    }
    // Both 3x3 patches connect through this seven-tile row, but only outside the berth.
    for(int x = 2; x <= 8; ++x) map(x, 1) = Terrain_Rock;
    map(5, 5) = Terrain_Rock;    // diagonal contact with both patches is not connectivity
    const Coord start(3, 3);
    const Rect berth{2, 2, 7, 6};
    for(TERRAINTYPE blocker : {Terrain_Mountain, Terrain_Sand, Terrain_Dunes, Terrain_Spice, Terrain_ThickSpice,
                              Terrain_SpiceBloom, Terrain_SpecialBloom, Terrain_GreenSpiceBloom, Terrain_RedSpiceBloom}) {
        CAPTURE(blocker);
        for(int y = 2; y <= 4; ++y) map(5, y) = blocker;
        REQUIRE(contiguousBuildableRock(map, start, berth) == 9);
        REQUIRE(contiguousBuildableRock(map, Coord(6, 3), berth) == 9);
        REQUIRE(contiguousBuildableRock(map, Coord(5, 5), berth) == 1);
        REQUIRE(contiguousBuildableRock(map, Coord(5, 3), berth) == 0);
        REQUIRE(contiguousBuildableRock(map, Coord(2, 1), berth) == 0);
        REQUIRE(contiguousBuildableRock(map, start, Rect{0, 0, 12, 9}) == 25);
    }
    MapData edge(5, 3);
    for(int y = 0; y < 3; ++y) for(int x = 0; x < 5; ++x) edge(x, y) = Terrain_Rock;
    REQUIRE(contiguousBuildableRock(edge, Coord(0, 0), Rect{-2, -2, 9, 7}) == 15);
    REQUIRE(contiguousBuildableRock(edge, Coord(-1, 0), Rect{-2, -2, 9, 7}) == 0);
    REQUIRE(contiguousBuildableRock(edge, Coord(5, 0), Rect{-2, -2, 9, 7}) == 0);
    REQUIRE(contiguousBuildableRock(edge, Coord(0, 0), Rect{0, 0, 0, 3}) == 0);
    REQUIRE(contiguousBuildableRock(edge, Coord(0, 0), Rect{0, 0, 5, 0}) == 0);
}

TEMPLATE_LIST_TEST_CASE("Random skirmish: seed sweep gives every MCV compact fair future expansion rock",
                        "[skirmish][generator][terrain][determinism][rockfairness][rockfairness-sweep]",
                        RockFairnessGroups) {
    const Mode mode = kSweepModes[TestType::value / kMapSizes.size()];
    const int size = kMapSizes[TestType::value % kMapSizes.size()];
    const std::array<uint32_t, 5> seeds{418379001u, 836308809u, 1u, 7101u, 4000000000u};
    const std::array<int, 3> mixes[] = {{75, 20, 5}, {50, 45, 5}, {50, 20, 30}, {90, 5, 5}};
    // One named section per configuration: -c "players=8 shares=50/20/30 seed=418379001"
    // diagnoses a single map without generating the rest of its group.
    for(int players : {2, 4, 8}) for(const auto& shares : mixes)
        for(size_t seedIndex = 0; seedIndex < seeds.size(); ++seedIndex)
            DYNAMIC_SECTION("players=" << players << " shares=" << shares[0] << "/" << shares[1]
                            << "/" << shares[2] << " seed=" << seeds[seedIndex]) {
                Params p = expansionParams(size, players, mode, seeds[seedIndex]);
                setTerrainShares(p, shares[0], shares[1], shares[2]);
                // The two reported FFA failures stay unmirrored; other fixed seeds cover mirrors.
                p.symmetric = seedIndex >= 2 && seedIndex % 2 == 0;
                p.spice = seedIndex == 3 ? Spice::None : Spice::Medium;
                const Catalogue& c = mode == Mode::DuneCity ? city() : vanilla();
                const Result r = generate(p, c);
                INFO("seed " << p.seed << " size " << size << " players " << players
                     << " city mode " << (mode == Mode::DuneCity) << " mirrored " << p.symmetric
                     << " Sand/Rock/Mountain " << shares[0] << "/" << shares[1] << "/" << shares[2]);
                REQUIRE(static_cast<int>(r.houses.size()) == players);
                REQUIRE_FALSE(r.flattenedMountains);
                requireFairStartRock(r, mode);
                requireCleanConnected(r);
                requireLegal(r, c);
                for(const HouseSetup& h : r.houses)
                    REQUIRE(std::count_if(r.units.begin(), r.units.end(), [&](const Placement& unit) {
                        return unit.house == h.house && unit.item == Unit_MCV && h.plateau.contains(unit.x, unit.y);
                    }) == 1);
                if(p.symmetric) requireMirroredStartRock(r, players);
                const TerrainCounts counts = terrainCounts(r.map);
                const int area = size * size;
                // Crowded small maps retain the existing forced-core share exception.
                // Ordinary-sized MCV-only starts must still leave the terrain controls intact.
                if(size >= 128) {
                    REQUIRE(std::abs(counts.sand * 100 - p.sandPercent * area) <= 12 * area + 300);
                    REQUIRE(std::abs(counts.rock * 100 - p.rockPercent * area) <= 12 * area + 300);
                }
                REQUIRE(std::abs(counts.mountain - p.mountainPercent * area / 100) <= 3);
                int spice = 0;
                for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) spice += isSpiceTile(r.map(x, y));
                if(p.spice == Spice::None) REQUIRE(spice == 0);
                else REQUIRE(spice > 0);
                requireRepeatableStartRock(r, generate(p, c));
            }
}

TEST_CASE("Random skirmish: expansion berths reach mode goals and scale with usable ordinary rock",
          "[skirmish][generator][terrain][rockfairness]") {
    REQUIRE(kStartRockFairnessPercent == 75);
    REQUIRE(kCityStartRockBerthSide == 32);
    REQUIRE(kVanillaStartRockBerthSide == 24);
    for(Mode mode : {Mode::Vanilla, Mode::DuneCity}) for(uint32_t seed : {418379001u, 836308809u}) {
        const Catalogue& c = mode == Mode::DuneCity ? city() : vanilla();
        const int goal = mode == Mode::DuneCity ? 32 : 24;
        const Result normal = generate(expansionParams(256, 8, mode, seed), c);
        CAPTURE(seed, mode == Mode::DuneCity);
        REQUIRE(normal.houses.size() == 8);
        requireFairStartRock(normal, mode);
        for(const HouseSetup& h : normal.houses) {
            REQUIRE(h.rockBerth.w == goal);
            REQUIRE(h.rockBerth.h == goal);
            REQUIRE(h.minimumRock == (mode == Mode::DuneCity ? 768 : 432));
        }
        auto sideFor = [&](int size, int players, int rock, int mountain) {
            Params p = expansionParams(size, players, mode, seed);
            setTerrainShares(p, 100 - rock - mountain, rock, mountain);
            const Result r = generate(p, c);
            CAPTURE(size, players, rock, mountain);
            REQUIRE(static_cast<int>(r.houses.size()) == players);
            requireFairStartRock(r, mode);
            requireCleanConnected(r);
            requireLegal(r, c);
            return r.houses.front().rockBerth.w;
        };
        const int spacious = sideFor(256, 2, 20, 5);
        const int small = sideFor(64, 2, 20, 5);
        const int crowded = sideFor(64, 8, 20, 5);
        const int lowRock = sideFor(256, 8, 5, 5);
        REQUIRE(spacious == goal);
        REQUIRE(small < spacious);
        REQUIRE(crowded <= small);
        REQUIRE(crowded < goal);
        REQUIRE(lowRock < goal);
        REQUIRE(sideFor(256, 8, 20, 30) == goal);    // mountains do not enlarge the usable-rock budget
        REQUIRE(sideFor(96, 8, 0, 0) == 8);
        REQUIRE(sideFor(96, 8, 0, 100) == 8);
    }
}

TEST_CASE("Random skirmish: pure Sand and Mountain requests retain legal connected starts", "[skirmish][generator][terrain]") {
    for(Mode mode : {Mode::Vanilla, Mode::DuneCity}) for(bool symmetric : {false, true}) for(bool mountains : {false, true}) {
        Params p = params(96, 1, 2, mode, 8, 20261103u);
        setTerrainShares(p, mountains ? 0 : 100, 0, mountains ? 100 : 0);
        p.symmetric = symmetric;
        for(Side* side : {&p.playerSide, &p.allySide, &p.enemySide}) {
            side->base = BaseSize::Small; side->army = 2; side->city = mode == Mode::DuneCity ? 1 : 0;
        }
        const Catalogue& c = mode == Mode::DuneCity ? city() : vanilla();
        const Result r = generate(p, c);
        const TerrainCounts counts = terrainCounts(r.map);
        CAPTURE(mode == Mode::DuneCity, symmetric, mountains, counts.sand, counts.rock, counts.mountain);
        // Forced starting rock and ground corridors take precedence over an extreme share.
        REQUIRE(counts.rock > 0);
        if(mountains) {
            REQUIRE(counts.mountain > 0);
            REQUIRE(counts.mountain < 96 * 96);
        } else {
            REQUIRE(counts.mountain == 0);
            REQUIRE(counts.sand > 0);
        }
        REQUIRE_FALSE(r.flattenedMountains);
        requireCleanConnected(r);
        requireLegal(r, c);
        requireFairStartRock(r, mode);
        if(symmetric) for(int y = 0; y < 96; ++y) for(int x = 0; x < 96; ++x)
            REQUIRE(r.map(x, y) == r.map(95 - x, 95 - y));
    }
}

TEST_CASE("Random skirmish: Stefan's 80 percent geology no longer overproduces mountains", "[skirmish][generator][terrain]") {
    Params p = params(160, 1, 2, Mode::DuneCity, 1, 418379001u);
    setTerrainShares(p, 20, 75, 5);
    p.house = HOUSE_INVALID; p.mod = "CivWars"; p.spice = Spice::None;
    for(Side* side : {&p.playerSide, &p.allySide, &p.enemySide}) {
        side->base = BaseSize::None; side->army = 0; side->city = 0;
    }
    p.playerSide.playerClass = "qBotHard";
    const Result r = generate(p, city());
    const TerrainCounts counts = terrainCounts(r.map);
    CAPTURE(counts.sand, counts.rock, counts.mountain);
    CHECK(counts.mountain <= 6544 / 2);    // the played legacy map contained 6,544 mountain tiles
    REQUIRE(counts.mountain == 160 * 160 * 5 / 100);
    requireCleanConnected(r);
    requireLegal(r, city());
}

TEST_CASE("Random skirmish: city population lands within 25% of every preset", "[skirmish][generator][city]") {
    for(int preset = 1; preset < static_cast<int>(kCityTargets.size()); ++preset)
        for(BaseSize base : {BaseSize::None, BaseSize::Small, BaseSize::Medium, BaseSize::Large, BaseSize::Fortress})
            for(int tech : {4, 8}) {
                Params p = params(256, 1, 1, Mode::DuneCity, tech, 500 + preset);
                p.allySide.base = p.enemySide.base = base;
                p.allySide.city = p.enemySide.city = preset;
                const Result r = generate(p, city());
                for(const auto& h : r.houses) if(h.role != Role::Human) {
                    INFO("preset " << kCityTargets[preset] << " base " << static_cast<int>(base) << " tech " << tech
                         << " estimate " << h.cityEstimate << " skipped " << h.skipped);
                    REQUIRE(h.cityTarget == kCityTargets[preset]);
                    REQUIRE(h.skipped == 0);
                    REQUIRE(h.cityEstimate == displayedPopulation(r.structures, h.house));
                    int zones = 0;
                    for(const auto& s : r.structures) zones += s.house == h.house && isZoneItem(s.item);
                    if(std::abs(h.cityEstimate - h.cityTarget) * 4 > h.cityTarget) {
                        // Only possible when the base's own garrison (Palace, Barracks, WOR)
                        // already outnumbers the smallest preset: no zones were added.
                        REQUIRE(zones == 0);
                        REQUIRE(h.cityEstimate > h.cityTarget);
                        REQUIRE(kCityTargets[preset] == 1000);
                        REQUIRE(base == BaseSize::Fortress);
                    }
                }
            }
    // Vanilla matches carry no city at all.
    Params p = params(128, 1, 1, Mode::Vanilla, 8, 3);
    p.allySide.city = p.enemySide.city = 4;
    for(const auto& s : generate(p, vanilla()).structures) REQUIRE_FALSE(DuneCity::isCityOnlyStructure(s.item));
}

TEST_CASE("Zone start level: older map entries are unchanged, the new field is strict", "[skirmish][maps]") {
    REQUIRE(ZoneStartLevel::parse("Player1,Residential Zone,256,401") == -1);
    REQUIRE(ZoneStartLevel::parse("Atreides,Const Yard,256,1024") == -1);
    REQUIRE(ZoneStartLevel::parse("Atreides,Residential Zone,256,401,3") == 3);
    REQUIRE(ZoneStartLevel::parse("Atreides,Commercial Zone,256,401, 0 ") == 0);
    REQUIRE(ZoneStartLevel::parse("Atreides,Commercial Zone,256,401,4") == -1);
    REQUIRE(ZoneStartLevel::parse("Atreides,Commercial Zone,256,401,x") == -1);
    REQUIRE(ZoneStartLevel::parse("Atreides,Commercial Zone,256,401,") == -1);
    REQUIRE(ZoneStartLevel::parse("Atreides,Commercial Zone,256,401,1,2") == -1);
    // Every structure entry of every bundled map reads as "no level".
    int zones = 0;
    for(const char* name : {"/data/maps/multiplayer/4P - 192x192 - Metropolis.ini", "/data/maps/singleplayer/4P - 192x192 - DuneCity.ini"}) {
        INIFile map(sourceRoot() + name);
        REQUIRE(map.hasSection("STRUCTURES"));
        for(const INIFile::Key& key : map.getSection("STRUCTURES")) {
            REQUIRE(ZoneStartLevel::parse(key.getStringValue()) == -1);
            zones += key.getStringValue().find("Zone") != std::string::npos;
        }
    }
    REQUIRE(zones > 0);
}

TEST_CASE("Random skirmish: last-used parameters survive the configuration file", "[skirmish][generator]") {
    Params p = params(192, 2, 4, Mode::DuneCity, 6, 4000000000u);
    setTerrainShares(p, 50, 45, 5);
    p.spice = Spice::Low; p.symmetric = true; p.fixedSeed = true;
    p.credits = 7500; p.enemiesAllied = false; p.house = HOUSE_ORDOS;
    p.allySide = Side{"qBotHard", BaseSize::Large, 4, 3};
    p.enemySide = Side{"AIPlayerEasy", BaseSize::Outpost, kArmyRandom, kCityRandom};
    INIFile config(false, std::string());
    p.save(config);
    Params loaded;
    loaded.load(config);
    REQUIRE(loaded.mapSize == 192); REQUIRE(loaded.spice == Spice::Low);
    REQUIRE(loaded.sandPercent == 50); REQUIRE(loaded.rockPercent == 45); REQUIRE(loaded.mountainPercent == 5);
    REQUIRE(config.getIntValue("Random Skirmish", "Sand Percent", -1) == 50);
    REQUIRE(config.getIntValue("Random Skirmish", "Rock Percent", -1) == 45);
    REQUIRE(config.getIntValue("Random Skirmish", "Mountain Percent", -1) == 5);
    REQUIRE_FALSE(config.hasKey("Random Skirmish", "Rock"));
    REQUIRE(loaded.symmetric); REQUIRE(loaded.fixedSeed); REQUIRE(loaded.seed == 4000000000u);
    REQUIRE(loaded.house == HOUSE_ORDOS); REQUIRE(loaded.credits == 7500);
    REQUIRE(loaded.allies == 2); REQUIRE(loaded.enemies == 4); REQUIRE_FALSE(loaded.enemiesAllied);
    REQUIRE(loaded.mode == Mode::DuneCity); REQUIRE(loaded.techLevel == 6);
    REQUIRE(loaded.allySide.playerClass == "qBotHard"); REQUIRE(loaded.allySide.base == BaseSize::Large);
    REQUIRE(loaded.allySide.army == 4); REQUIRE(loaded.allySide.city == 3);
    REQUIRE(loaded.enemySide.playerClass == "AIPlayerEasy"); REQUIRE(loaded.enemySide.base == BaseSize::Outpost);
    REQUIRE(loaded.enemySide.army == kArmyRandom); REQUIRE(loaded.enemySide.city == kCityRandom);

    // Corrupt values clamp instead of escaping into the generator.
    INIFile bad(false, std::string());
    bad.setIntValue("Random Skirmish", "Map Size", 100);
    bad.setIntValue("Random Skirmish", "Tech Level", 42);
    bad.setIntValue("Random Skirmish", "Sand Percent", -35);
    bad.setIntValue("Random Skirmish", "Rock Percent", 285);
    bad.setIntValue("Random Skirmish", "Mountain Percent", 3);
    bad.setStringValue("Random Skirmish", "Seed", "-5");
    Params fallback;
    fallback.load(bad);
    fallback.clamp(8);
    REQUIRE(fallback.mapSize == 96);
    REQUIRE(fallback.techLevel == 9);
    REQUIRE(fallback.sandPercent + fallback.rockPercent + fallback.mountainPercent == 100);
    for(int share : {fallback.sandPercent, fallback.rockPercent, fallback.mountainPercent}) {
        REQUIRE((share >= 0 && share <= 100));
        REQUIRE(share % 5 == 0);
    }
    REQUIRE(fallback.seed == Params().seed);
}

TEST_CASE("Random skirmish: terrain percentages migrate every legacy Rock index", "[skirmish][generator][terrain]") {
    for(size_t index = 0; index < kLegacyGeologicalTotals.size(); ++index) {
        INIFile config(false, std::string());
        config.setIntValue("Random Skirmish", "Rock", static_cast<int>(index));
        Params loaded;
        loaded.load(config);
        loaded.clamp(8);
        const int total = kLegacyGeologicalTotals[index];
        CAPTURE(index, total);
        REQUIRE(loaded.sandPercent == 100 - total);
        REQUIRE(loaded.rockPercent == total - 5);
        REQUIRE(loaded.mountainPercent == 5);
        loaded.save(config);
        REQUIRE_FALSE(config.hasKey("Random Skirmish", "Rock"));
        REQUIRE(config.getIntValue("Random Skirmish", "Sand Percent", -1) == 100 - total);
        REQUIRE(config.getIntValue("Random Skirmish", "Rock Percent", -1) == total - 5);
        REQUIRE(config.getIntValue("Random Skirmish", "Mountain Percent", -1) == 5);
    }

    // Explicit shares win even when an old configuration still carries a conflicting index.
    for(int oldIndex : {0, 7}) {
        INIFile config(false, std::string());
        config.setIntValue("Random Skirmish", "Rock", oldIndex);
        config.setIntValue("Random Skirmish", "Sand Percent", 60);
        config.setIntValue("Random Skirmish", "Rock Percent", 25);
        config.setIntValue("Random Skirmish", "Mountain Percent", 15);
        Params loaded;
        loaded.load(config);
        loaded.clamp(8);
        REQUIRE(loaded.sandPercent == 60);
        REQUIRE(loaded.rockPercent == 25);
        REQUIRE(loaded.mountainPercent == 15);
        loaded.save(config);
        REQUIRE_FALSE(config.hasKey("Random Skirmish", "Rock"));
    }
}

TEST_CASE("Random skirmish: terrain shares clamp deterministically to five point percentages summing to 100", "[skirmish][generator][terrain]") {
    Params defaults;
    REQUIRE(defaults.sandPercent == 75);
    REQUIRE(defaults.rockPercent == 20);
    REQUIRE(defaults.mountainPercent == 5);

    for(const std::array<int, 3>& shares : {std::array<int, 3>{75, 20, 5}, {100, 0, 0}, {0, 100, 0}, {0, 0, 100}, {50, 15, 35}}) {
        Params p;
        setTerrainShares(p, shares[0], shares[1], shares[2]);
        p.clamp(8);
        REQUIRE(p.sandPercent == shares[0]);
        REQUIRE(p.rockPercent == shares[1]);
        REQUIRE(p.mountainPercent == shares[2]);
    }

    const std::array<int, 3> badShares[] = {{-25, 145, -5}, {0, 0, 0}, {73, 21, 6}, {33, 33, 33},
                                         {100, 100, 100}, {2000, -1000, 7}, {1, 2, 1}, {10, 10, 10}};
    for(const auto& shares : badShares) {
        Params a, b;
        setTerrainShares(a, shares[0], shares[1], shares[2]);
        setTerrainShares(b, shares[0], shares[1], shares[2]);
        a.clamp(8); b.clamp(8);
        CAPTURE(shares[0], shares[1], shares[2], a.sandPercent, a.rockPercent, a.mountainPercent);
        REQUIRE(a.sandPercent + a.rockPercent + a.mountainPercent == 100);
        for(int share : {a.sandPercent, a.rockPercent, a.mountainPercent}) {
            REQUIRE((share >= 0 && share <= 100));
            REQUIRE(share % 5 == 0);
        }
        REQUIRE(a.sandPercent == b.sandPercent);
        REQUIRE(a.rockPercent == b.rockPercent);
        REQUIRE(a.mountainPercent == b.mountainPercent);
        a.clamp(8);
        REQUIRE(a.sandPercent == b.sandPercent);
        REQUIRE(a.rockPercent == b.rockPercent);
        REQUIRE(a.mountainPercent == b.mountainPercent);
    }
    Params zero, rounded;
    setTerrainShares(zero, 0, 0, 0); zero.clamp(8);
    REQUIRE(zero.sandPercent == 75); REQUIRE(zero.rockPercent == 20); REQUIRE(zero.mountainPercent == 5);
    setTerrainShares(rounded, 73, 21, 6); rounded.clamp(8);
    REQUIRE(rounded.sandPercent == 75); REQUIRE(rounded.rockPercent == 20); REQUIRE(rounded.mountainPercent == 5);
}

TEST_CASE("Random skirmish: Spectate is remembered, single player only, and leaves the map alone", "[skirmish][generator]") {
    INIFile config(false, std::string());
    Params watch = params(96, 1, 2, Mode::DuneCity, 8, 20261018u);
    watch.mod = "dunecity";
    REQUIRE_FALSE(watch.spectate);
    REQUIRE(watch.spectateClass == "qBotMedium");
    watch.spectate = true; watch.spectateClass = "qBotHard";
    watch.save(config);
    Params back;
    back.load(config);
    REQUIRE(back.spectate); REQUIRE(back.spectateClass == "qBotHard");
    // Watching changes who plays the player's house, never the generated map.
    Params play = watch;
    play.spectate = false;
    REQUIRE(generate(watch, city()).ini == generate(play, city()).ini);
    // The hosted screen neither reads nor writes it; the single-player choice survives a hosted visit.
    Params hosted;
    hosted.multiplayer = true;
    hosted.load(config);
    REQUIRE_FALSE(hosted.spectate);
    hosted.spectate = true;
    hosted.clamp(8);
    REQUIRE_FALSE(hosted.spectate);
    hosted.spectateClass = "AIPlayerEasy";
    hosted.save(config);
    Params again;
    again.load(config);
    REQUIRE(again.spectate); REQUIRE(again.spectateClass == "qBotHard");
    // An empty class from a hand-edited file falls back to the default AI.
    config.setStringValue("Random Skirmish", "Spectate AI", "");
    Params empty;
    empty.load(config);
    empty.clamp(8);
    REQUIRE(empty.spectateClass == "qBotMedium");
}

namespace {

bool rockLike(TERRAINTYPE t) { return t == Terrain_Rock || t == Terrain_Mountain; }

/// Stefan's played configuration (testskirmish.dls): 128, 2 allies + 2 enemies, tech 9, Medium
/// bases, 5,000-credit armies, 40,000-people cities, seed 488631365.
Params stefanParams() {
    Params p = params(128, 2, 2, Mode::DuneCity, 9, 488631365u);
    p.house = HOUSE_REBELS;
    p.allySide = p.enemySide = Side{"qBotMedium", BaseSize::Medium, 3, 4};
    p.playerSide = Side{"", BaseSize::None, 0, 0};
    return p;
}

/// Longest straight piece of rock outline along one row or column boundary.
int longestStraightOutline(const MapData& map) {
    const int s = map.getSizeX();
    int longest = 0;
    for(int pass = 0; pass < 2; ++pass) for(int a = 0; a + 1 < s; ++a) {
        int run = 0, side = 0;
        for(int b = 0; b < s; ++b) {
            const bool here = pass == 0 ? rockLike(map(b, a)) : rockLike(map(a, b));
            const bool next = pass == 0 ? rockLike(map(b, a + 1)) : rockLike(map(a + 1, b));
            const int edge = here != next ? (here ? 1 : 2) : 0;
            run = edge != 0 && edge == side ? run + 1 : edge != 0;
            side = edge;
            longest = std::max(longest, run);
        }
    }
    return longest;
}

/// How far rock reaches beyond a start's clean core, along every side not at the map edge.
struct RimProfile { int minReach = 1 << 20; int changes = 0; int samples = 0; };
RimProfile rimProfile(const MapData& map, const Rect& core) {
    RimProfile out;
    const int size = map.getSizeX(), limit = kRimMax + 2;
    struct Side { int x0, y0, stepX, stepY, length, dx, dy; bool usable; };
    const Side sides[] = {
        {core.x, core.y, 1, 0, core.w, 0, -1, core.y - limit >= 0},
        {core.x, core.y + core.h - 1, 1, 0, core.w, 0, 1, core.y + core.h + limit <= size},
        {core.x, core.y, 0, 1, core.h, -1, 0, core.x - limit >= 0},
        {core.x + core.w - 1, core.y, 0, 1, core.h, 1, 0, core.x + core.w + limit <= size},
    };
    for(const Side& s : sides) {
        if(!s.usable) continue;
        int previous = -1;
        for(int i = 0; i < s.length; ++i) {
            const int x = s.x0 + s.stepX * i, y = s.y0 + s.stepY * i;
            int reach = 0;
            while(reach < limit && rockLike(map(x + s.dx * (reach + 1), y + s.dy * (reach + 1)))) ++reach;
            out.minReach = std::min(out.minReach, reach);
            out.changes += previous >= 0 && reach != previous;
            previous = reach;
            ++out.samples;
        }
    }
    return out;
}

int lotGap(const Placement& a, const Placement& b) {
    return std::max({0, b.x - (a.x + 2), a.x - (b.x + 2), b.y - (a.y + 2), a.y - (b.y + 2)});
}

/**
    The city simulation's trip (TrafficSimulation): the first road on the lot's 12-tile ring
    (top row, bottom row, left column, right column), then at most 20 orthogonal road steps to
    a road tile beside a structure of the wanted role. -1 = no road on the ring, -2 = no trip.
*/
int tripLength(const Result& r, const Catalogue& c, const Placement& lot, DuneCity::CityRole want) {
    const int size = r.map.getSizeX();
    std::vector<char> road(size * size, 0), target(size * size, 0);
    for(const Placement& t : r.tiles) if(t.item == Structure_Road) road[t.y * size + t.x] = 1;
    for(const Placement& s : r.structures) {
        if(s.house != lot.house || DuneCity::getStructureCityRole(s.item) != want) continue;
        const Coord d = c.sizes[s.item];
        for(int dy = 0; dy < d.y; ++dy) for(int dx = 0; dx < d.x; ++dx) target[(s.y + dy) * size + s.x + dx] = 1;
    }
    std::vector<Coord> ring;
    for(int dx = -1; dx <= 2; ++dx) ring.emplace_back(lot.x + dx, lot.y - 1);
    for(int dx = -1; dx <= 2; ++dx) ring.emplace_back(lot.x + dx, lot.y + 2);
    for(int dy = 0; dy <= 1; ++dy) ring.emplace_back(lot.x - 1, lot.y + dy);
    for(int dy = 0; dy <= 1; ++dy) ring.emplace_back(lot.x + 2, lot.y + dy);
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < size && y < size; };
    const auto start = std::find_if(ring.begin(), ring.end(), [&](const Coord& p) { return inside(p.x, p.y) && road[p.y * size + p.x]; });
    if(start == ring.end()) return -1;
    std::vector<int> distance(size * size, -1);
    std::vector<Coord> queue{*start};
    distance[start->y * size + start->x] = 0;
    for(size_t i = 0; i < queue.size(); ++i) {
        const Coord p = queue[i];
        const int d = distance[p.y * size + p.x];
        for(const Coord& n : {Coord(1,0), Coord(-1,0), Coord(0,1), Coord(0,-1)})
            if(inside(p.x + n.x, p.y + n.y) && target[(p.y + n.y) * size + p.x + n.x]) return d;
        if(d >= 20) continue;
        for(const Coord& n : {Coord(1,0), Coord(-1,0), Coord(0,1), Coord(0,-1)}) {
            const int nx = p.x + n.x, ny = p.y + n.y;
            if(!inside(nx, ny) || !road[ny * size + nx] || distance[ny * size + nx] >= 0) continue;
            distance[ny * size + nx] = d + 1;
            queue.emplace_back(nx, ny);
        }
    }
    return -2;
}

int countOf(const Result& r, int house, int item) {
    return static_cast<int>(std::count_if(r.structures.begin(), r.structures.end(),
                                          [&](const Placement& s) { return s.house == house && s.item == item; }));
}

}

TEST_CASE("Random skirmish: terrain looks like the map editor's random map", "[skirmish][generator][terrain]") {
    // Organic outlines: no long straight rock edge anywhere (the old rectangular plateaus had
    // 57-tile edges; editor random maps measure 11-12), and every start's clean core is
    // hidden inside a rim of irregular width.
    struct Case { Params p; const Catalogue* c; };
    std::vector<Case> cases{{stefanParams(), &city()}, {params(128, 1, 2, Mode::Vanilla, 8, 488631365u), &vanilla()},
                            {params(192, 1, 1, Mode::DuneCity, 8, 20261013u), &city()}, {params(64, 0, 1, Mode::Vanilla, 8, 9), &vanilla()}};
    cases[2].p.allySide.city = cases[2].p.enemySide.city = 3;
    for(uint32_t seed : {1u, 2u, 3u}) {
        Params p = params(128, 1, 2, Mode::Vanilla, 8, seed);
        p.symmetric = seed == 2;
        cases.push_back({p, &vanilla()});
    }
    for(const Case& k : cases) {
        const Result r = generate(k.p, *k.c);
        INFO("size " << k.p.mapSize << " seed " << k.p.seed);
        REQUIRE(longestStraightOutline(r.map) <= 16);
        int mountains = 0, dunes = 0;
        for(int y = 0; y < r.map.getSizeY(); ++y) for(int x = 0; x < r.map.getSizeX(); ++x) {
            mountains += r.map(x, y) == Terrain_Mountain;
            dunes += r.map(x, y) == Terrain_Dunes;
        }
        REQUIRE(mountains > 0);
        REQUIRE(dunes > 0);
        for(const HouseSetup& h : r.houses) {
            const RimProfile rim = rimProfile(r.map, h.plateau);
            INFO("house " << h.house << " rim samples " << rim.samples << " changes " << rim.changes << " min reach " << rim.minReach);
            if(rim.samples == 0) continue;
            REQUIRE(rim.minReach >= kRimMin);
            REQUIRE(rim.changes * 8 >= rim.samples);
            // The clean core stays clean: rock only, no mountain, no spice.
            for(int y = h.plateau.y; y < h.plateau.y + h.plateau.h; ++y)
                for(int x = h.plateau.x; x < h.plateau.x + h.plateau.w; ++x) REQUIRE(r.map(x, y) == Terrain_Rock);
        }
    }
}

TEST_CASE("Random skirmish: spice share follows the Spice setting", "[skirmish][generator][terrain]") {
    for(uint32_t seed : {1u, 2u, 3u}) {
        int previous = -1;
        for(int spice = 1; spice <= 4; ++spice) {
            Params p = params(128, 1, 2, Mode::Vanilla, 8, seed);
            p.spice = static_cast<Spice>(spice);
            const Result r = generate(p, vanilla());
            int tiles = 0, thick = 0;
            for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) {
                const TERRAINTYPE t = r.map(x, y);
                tiles += t == Terrain_Spice || t == Terrain_ThickSpice || t == Terrain_SpiceBloom || t == Terrain_SpecialBloom;
                thick += t == Terrain_ThickSpice;
            }
            const int percent = tiles * 100 / (128 * 128);
            INFO("seed " << seed << " spice preset " << spice << " share " << percent << "% thick " << thick);
            REQUIRE(std::abs(percent - kSpicePercent[spice]) <= 3);
            REQUIRE(thick > 0);
            REQUIRE(percent > previous);
            previous = percent;
        }
    }
}

TEST_CASE("Random skirmish: cities are zoned into districts the simulation can serve", "[skirmish][generator][city]") {
    std::vector<Params> cases{stefanParams()};
    for(int preset : {1, 2, 3}) {
        Params p = params(192, 1, 1, Mode::DuneCity, 8, 700 + preset);
        p.allySide.city = p.enemySide.city = p.playerSide.city = preset;
        cases.push_back(p);
    }
    for(const Params& p : cases) {
        const Result r = generate(p, city());
        for(const HouseSetup& h : r.houses) {
            std::vector<Placement> industrial, residential, commercial, police;
            for(const Placement& s : r.structures) if(s.house == h.house) {
                if(s.item == Structure_ZoneIndustrial) industrial.push_back(s);
                if(s.item == Structure_ZoneResidential) residential.push_back(s);
                if(s.item == Structure_ZoneCommercial) commercial.push_back(s);
                if(s.item == Structure_PoliceStation) police.push_back(s);
            }
            if(industrial.empty() && residential.empty()) continue;
            INFO("seed " << p.seed << " house " << h.house << " R/C/I " << residential.size() << "/" << commercial.size() << "/" << industrial.size());
            REQUIRE(h.skipped == 0);
            // Industrial never beside Residential: at least kIndustrialBuffer tiles apart.
            for(const Placement& i : industrial) for(const Placement& res : residential) REQUIRE(lotGap(i, res) >= kIndustrialBuffer);
            // One Industrial district: every lot reachable from the first through lots at most
            // one road apart.
            std::vector<char> joined(industrial.size(), 0);
            std::vector<size_t> stack{0};
            joined[0] = 1;
            while(!stack.empty()) {
                const size_t at = stack.back(); stack.pop_back();
                for(size_t j = 0; j < industrial.size(); ++j)
                    if(!joined[j] && lotGap(industrial[at], industrial[j]) <= 1) { joined[j] = 1; stack.push_back(j); }
            }
            REQUIRE(std::count(joined.begin(), joined.end(), 1) == static_cast<long>(industrial.size()));
            // Every trip the simulation makes finds its destination by road within 20 steps.
            auto trips = [&](const std::vector<Placement>& lots, DuneCity::CityRole want) {
                for(const Placement& z : lots) {
                    INFO("lot at " << z.x << "," << z.y << " item " << z.item);
                    REQUIRE(tripLength(r, city(), z, want) >= 0);
                }
            };
            trips(residential, DuneCity::CityRole::Commercial);
            trips(commercial, DuneCity::CityRole::Industrial);
            trips(industrial, DuneCity::CityRole::Residential);
            // Police cover the whole city: every R/C lot within reach of a station.
            if(!police.empty()) for(const auto* group : {&residential, &commercial}) for(const Placement& z : *group) {
                int nearest = 1 << 20;
                for(const Placement& s : police) nearest = std::min(nearest, std::abs(s.x - z.x) + std::abs(s.y - z.y));
                REQUIRE(nearest <= 23);
            }
        }
    }
}

TEST_CASE("Random skirmish: nuclear plants power large needs where the house can build them", "[skirmish][generator][power]") {
    // A 5,000-people city plus a Medium base: reactors and a handful of wind traps at most.
    for(int tech : {6, 8, 9}) for(int house = 0; house < 8; ++house) {
        std::vector<int> items = baseTemplate(BaseSize::Medium, house, tech, city());
        addPower(items, house, tech, 600, city());
        INFO("tech " << tech << " house " << house);
        if(!city().available(Structure_NuclearPlant, house, tech)) continue;
        REQUIRE(std::count(items.begin(), items.end(), Structure_NuclearPlant) >= 1);
        REQUIRE(std::count(items.begin(), items.end(), Structure_WindTrap) <= kMaxTopUpTraps);
        REQUIRE(powerBalance(items, house, 600, city()) > 0);
    }
    // Before tech 6, and in vanilla matches, wind traps only.
    std::vector<int> early = baseTemplate(BaseSize::Medium, HOUSE_ATREIDES, 5, city());
    addPower(early, HOUSE_ATREIDES, 5, 600, city());
    REQUIRE(std::count(early.begin(), early.end(), Structure_NuclearPlant) == 0);
    REQUIRE(powerBalance(early, HOUSE_ATREIDES, 600, city()) > 0);
    std::vector<int> plain = baseTemplate(BaseSize::Medium, HOUSE_ATREIDES, 9, vanilla());
    addPower(plain, HOUSE_ATREIDES, 9, 600, vanilla());
    REQUIRE(std::count(plain.begin(), plain.end(), Structure_NuclearPlant) == 0);
    REQUIRE(powerBalance(plain, HOUSE_ATREIDES, 600, vanilla()) > 0);

    // Whole generated matches: Stefan's 40,000-people cities and 5,000-people cities with the
    // player's own Medium base.
    Params small = params(128, 1, 1, Mode::DuneCity, 8, 4242);
    small.allySide.city = small.enemySide.city = small.playerSide.city = 2;
    for(const Params& p : {stefanParams(), small}) {
        const Result r = generate(p, city());
        for(const HouseSetup& h : r.houses) {
            std::vector<int> items;
            int zoneDraw = 0;
            for(const Placement& s : r.structures) if(s.house == h.house) {
                if(isZoneItem(s.item)) zoneDraw += DuneCity::getZonePower(s.item, s.level);
                else items.push_back(s.item);
            }
            if(items.empty()) continue;
            INFO("seed " << p.seed << " house " << h.house << " reactors " << countOf(r, h.house, Structure_NuclearPlant)
                 << " wind traps " << countOf(r, h.house, Structure_WindTrap));
            REQUIRE(powerBalance(items, h.house, zoneDraw, city()) > 0);
            // A need beyond a handful of wind traps takes a reactor (Stefan's 40,000-people
            // cities used 19 wind traps each before).
            int required = zoneDraw;
            for(int item : items) required += std::max(0, city().rule(item, h.house).power);
            if(required > kMaxTopUpTraps * -city().rule(Structure_WindTrap, h.house).power)
                REQUIRE(countOf(r, h.house, Structure_NuclearPlant) >= 1);
            if(p.seed == stefanParams().seed) REQUIRE(countOf(r, h.house, Structure_NuclearPlant) >= 1);
            REQUIRE(countOf(r, h.house, Structure_WindTrap) <= kMaxTopUpTraps);
            // Reactors keep their blast distance from the buildings the AI protects.
            for(const Placement& reactor : r.structures) {
                if(reactor.house != h.house || reactor.item != Structure_NuclearPlant) continue;
                for(const Placement& s : r.structures)
                    if(&s != &reactor && s.house == h.house && TacticalSafetyPolicy::protectedReactorNeighbour(s.item))
                        REQUIRE(TacticalSafetyPolicy::blastClearance(reactor.x, reactor.y, 3, 3, s.x, s.y,
                                                                     city().sizes[s.item].x, city().sizes[s.item].y));
            }
        }
    }
}

TEST_CASE("Random skirmish: bases grow around the Construction Yard", "[skirmish][generator][base]") {
    for(const Params& p : {stefanParams(), params(128, 1, 2, Mode::Vanilla, 8, 488631365u)}) {
        const Catalogue& c = p.mode == Mode::DuneCity ? city() : vanilla();
        const Result r = generate(p, c);
        requireLegal(r, c);
        for(const HouseSetup& h : r.houses) {
            const auto yard = std::find_if(r.structures.begin(), r.structures.end(), [&](const Placement& s) {
                return s.house == h.house && s.item == Structure_ConstructionYard; });
            if(yard == r.structures.end()) continue;
            // The yard is the heart: closer to the centroid of the base than any other building.
            int n = 0, cx = 0, cy = 0;
            std::set<int> rows, columns;
            for(const Placement& s : r.structures) if(s.house == h.house && !isZoneItem(s.item) && s.item != Structure_PoliceStation) {
                cx += 2 * s.x + c.sizes[s.item].x; cy += 2 * s.y + c.sizes[s.item].y; ++n;
                rows.insert(s.y); columns.insert(s.x);
            }
            cx /= n; cy /= n;
            const int yardDistance = std::abs(2 * yard->x + 2 - cx) + std::abs(2 * yard->y + 2 - cy);
            int closer = 0;
            for(const Placement& s : r.structures) if(s.house == h.house && !isZoneItem(s.item) && s.item != Structure_PoliceStation)
                closer += std::abs(2 * s.x + c.sizes[s.item].x - cx) + std::abs(2 * s.y + c.sizes[s.item].y - cy) < yardDistance;
            INFO("house " << h.house << " buildings " << n << " closer than the yard " << closer
                 << " distinct rows " << rows.size() << " columns " << columns.size());
            REQUIRE(closer * 4 <= n);
            // Not a grid: building positions spread over many distinct rows and columns.
            REQUIRE(static_cast<int>(rows.size() + columns.size()) * 2 >= n);
        }
    }
}

TEST_CASE("Random skirmish: random configurations stay legal, connected and keep their mountains", "[skirmish][generator][terrain]") {
    // Keep the same 150 seeded configurations, now varying all three terrain shares as well
    // as every size, mode, mirror, spice, base, army and city choice. Crowded small maps may
    // skip items; they must never place anything illegally or isolate starts or spice.
    Random random(99);
    for(int i = 0; i < 150; ++i) {
        Params p = params(kMapSizes[random.rand(0, 5)], random.rand(0, 3), random.rand(1, 4),
                          random.rand(0, 1) ? Mode::DuneCity : Mode::Vanilla, random.rand(1, 9), random.rand());
        p.symmetric = random.rand(0, 1);
        p.sandPercent = 5 * random.rand(0, 20);
        p.mountainPercent = 5 * random.rand(0, (100 - p.sandPercent) / 5);
        p.rockPercent = 100 - p.sandPercent - p.mountainPercent;
        p.spice = static_cast<Spice>(random.rand(0, 4));
        for(Side* side : {&p.allySide, &p.enemySide, &p.playerSide}) {
            side->base = static_cast<BaseSize>(random.rand(0, 5));
            side->city = random.rand(0, 5);
            side->army = random.rand(0, 6);
        }
        const Catalogue& c = p.mode == Mode::DuneCity ? city() : vanilla();
        const Result r = generate(p, c);
        INFO("configuration " << i << " size " << p.mapSize << " houses " << r.houses.size() << " mirrored " << p.symmetric
             << " Sand/Rock/Mountain " << p.sandPercent << "/" << p.rockPercent << "/" << p.mountainPercent);
        requireLegal(r, c);
        REQUIRE_FALSE(r.flattenedMountains);
        requireCleanConnected(r);
        requireFairStartRock(r, p.mode);
        if(p.mountainPercent == 0) REQUIRE(terrainCounts(r.map).mountain == 0);
    }
}

TEST_CASE("Random skirmish: bases from Small up are defended by rocket turrets", "[skirmish][generator][base]") {
    // Template level: every house that can build rocket turrets gets them; the rest get gun
    // turrets in their place, so no tier loses its defences.
    const std::pair<BaseSize, int> rockets[] = {{BaseSize::Small, 1}, {BaseSize::Medium, 2}, {BaseSize::Large, 4}, {BaseSize::Fortress, 8}};
    for(const Catalogue* c : {&vanilla(), &city()}) for(int house = 0; house < 8; ++house) for(auto [tier, count] : rockets) {
        const std::vector<int> items = baseTemplate(tier, house, 9, *c);
        if(items.empty()) continue;
        const auto rocketCount = std::count(items.begin(), items.end(), Structure_RocketTurret);
        INFO("house " << house << " tier " << static_cast<int>(tier));
        if(c->available(Structure_RocketTurret, house, 9)) REQUIRE(rocketCount == count);
        else if(c->available(Structure_GunTurret, house, 9)) REQUIRE(std::count(items.begin(), items.end(), Structure_GunTurret) >= count);
    }

    // Generated map, default tech level: every Medium base that can build them has its rocket
    // turrets actually placed on the map, the player's included.
    for(Mode mode : {Mode::Vanilla, Mode::DuneCity}) {
        Params p = params(128, 1, 2, mode, Params().techLevel, 77);
        REQUIRE(p.techLevel == 9);
        p.allySide.base = p.enemySide.base = p.playerSide.base = BaseSize::Medium;
        const Catalogue& cat = mode == Mode::DuneCity ? city() : vanilla();
        const Result r = generate(p, cat);
        for(const auto& h : r.houses) {
            if(!cat.available(Structure_RocketTurret, h.house, 9)) continue;
            const auto placed = std::count_if(r.structures.begin(), r.structures.end(),
                [&](const Placement& s) { return s.house == h.house && s.item == Structure_RocketTurret; });
            INFO("mode " << static_cast<int>(mode) << " house " << h.house);
            REQUIRE(placed == 2);
        }
    }
}

TEST_CASE("Random skirmish: armies have no light infantry and troopers stand five to a tile", "[skirmish][generator][army]") {
    // No house ever buys light infantry, at any budget or tech level.
    for(const Catalogue* c : {&vanilla(), &city()}) for(int house = 0; house < 8; ++house) for(int tech = 1; tech <= 9; ++tech) {
        const std::vector<int> types = armyTypes(house, tech, *c);
        REQUIRE(std::find(types.begin(), types.end(), Unit_Soldier) == types.end());
    }
    // A generated army spends its credits, and its Troopers share tiles: as few tiles as
    // five-per-tile allows, never more than five on one.
    Params p = params(128, 1, 2, Mode::Vanilla, 9, 31);
    p.allySide.army = p.enemySide.army = p.playerSide.army = static_cast<int>(kArmyBudgets.size()) - 1;
    const Result r = generate(p, vanilla());
    requireLegal(r, vanilla());
    int troopersSeen = 0;
    for(const auto& h : r.houses) {
        std::map<int, int> perTile;
        int troopers = 0;
        for(const Placement& u : r.units) {
            if(u.house != h.house) continue;
            REQUIRE(u.item != Unit_Soldier);
            if(u.item == Unit_Trooper) { ++troopers; ++perTile[u.y * 128 + u.x]; }
        }
        INFO("house " << h.house << " troopers " << troopers);
        REQUIRE(static_cast<int>(perTile.size()) == (troopers + 4) / 5);
        for(const auto& [tile, n] : perTile) REQUIRE(n <= 5);
        REQUIRE(h.armySpent * 10 >= h.armyBudget * 9);
        troopersSeen += troopers;
    }
    REQUIRE(troopersSeen > 5);    // the fixture really exercises bundling
}

TEST_CASE("Random skirmish: Random army and city are drawn once per map", "[skirmish][generator][random]") {
    std::set<int> armies, cities;
    for(uint32_t seed = 1; seed <= 12; ++seed) {
        Params p = params(160, 2, 3, Mode::DuneCity, 9, seed);
        p.playerSide = Side{"", BaseSize::Small, kArmyRandom, kCityRandom};
        p.allySide = Side{"qBotMedium", BaseSize::Small, kArmyRandom, kCityRandom};
        p.enemySide = Side{"qBotMedium", BaseSize::Small, kArmyRandom, kCityRandom};
        const Result r = generate(p, city());
        REQUIRE(r.houses.size() == 6);
        // Every house set to Random has the same army budget and the same city target.
        for(const HouseSetup& h : r.houses) {
            INFO("seed " << seed << " house " << h.house);
            REQUIRE(h.armyBudget == r.houses[0].armyBudget);
            REQUIRE(h.cityTarget == r.houses[0].cityTarget);
        }
        const int army = r.houses[0].armyBudget, target = r.houses[0].cityTarget;
        REQUIRE((army >= kArmyRandomMin && army <= kArmyRandomMax && army % 100 == 0));
        REQUIRE((target >= kCityRandomMin && target <= kCityRandomMax && target % 1000 == 0));
        armies.insert(army);
        cities.insert(target);
        // Deterministic per seed.
        const Result again = generate(p, city());
        REQUIRE(again.ini == r.ini);
        // A side with a preset keeps it; the Random sides still share one draw.
        p.allySide.army = 3;
        p.allySide.city = 2;
        const Result mixed = generate(p, city());
        for(const HouseSetup& h : mixed.houses) {
            REQUIRE(h.armyBudget == (h.role == Role::Ally ? kArmyBudgets[3] : army));
            REQUIRE(h.cityTarget == (h.role == Role::Ally ? kCityTargets[2] : target));
        }
    }
    // ...and different seeds draw different sizes.
    REQUIRE(armies.size() >= 3);
    REQUIRE(cities.size() >= 3);
}

namespace {

/// Item counts of one start: buildings, units or tile entries owned by @a house.
std::map<int, int> contentsOf(const std::vector<Placement>& placements, int house) {
    std::map<int, int> counts;
    for(const Placement& p : placements) if(p.house == house) ++counts[p.item];
    return counts;
}

/// The generated map text as the engine reads it.
struct ParsedMap {
    explicit ParsedMap(const std::string& text) {
        SDL_RWops* rw = SDL_RWFromConstMem(text.data(), static_cast<int>(text.size()));
        ini = std::make_unique<INIFile>(rw);
        SDL_RWclose(rw);
    }
    std::unique_ptr<INIFile> ini;
};

Params multiplayerParams(int starts, int size, Mode mode, BaseSize base, int army, int city, bool symmetric, int tech, uint32_t seed) {
    Params p = params(size, 0, 1, mode, tech, seed);
    p.multiplayer = true;
    p.starts = starts;
    p.symmetric = symmetric;
    p.credits = 5000;
    p.startSide = Side{"", base, army, city};
    return p;
}

}

TEST_CASE("Random skirmish: terrain option hashes identify each mix in map provenance", "[skirmish][generator][terrain][determinism]") {
    const std::array<int, 3> mixes[] = {{75, 20, 5}, {70, 25, 5}, {70, 20, 10}, {75, 15, 10}};
    std::set<std::string> hashes;
    for(size_t i = 0; i < std::size(mixes); ++i) {
        Params p = params(96, 0, 1, Mode::Vanilla, 8, 20261103u);
        setTerrainShares(p, mixes[i][0], mixes[i][1], mixes[i][2]);
        p.multiplayer = i % 2 != 0;
        const std::string hash = p.terrainHash();
        CAPTURE(p.sandPercent, p.rockPercent, p.mountainPercent, p.multiplayer, hash);
        REQUIRE(hash.size() == 16);
        REQUIRE(hash.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos);
        REQUIRE(hashes.insert(hash).second);
        REQUIRE(p.terrainHash() == hash);
        Params unrelated = p;
        unrelated.seed += 1; unrelated.mapSize = 160; unrelated.spice = Spice::VeryHigh;
        unrelated.worms = 4; unrelated.symmetric = !p.symmetric;
        REQUIRE(unrelated.terrainHash() == hash);    // this hash is the three shares and their version

        const Result r = generate(p, vanilla());
        const ParsedMap parsed(r.ini);
        REQUIRE(parsed.ini->getIntValue("RANDOMSKIRMISH", "Sand", -1) == p.sandPercent);
        REQUIRE(parsed.ini->getIntValue("RANDOMSKIRMISH", "Rock", -1) == p.rockPercent);
        REQUIRE(parsed.ini->getIntValue("RANDOMSKIRMISH", "Mountain", -1) == p.mountainPercent);
        REQUIRE(parsed.ini->getStringValue("RANDOMSKIRMISH", "TerrainOptionsHash") == hash);
    }
}

TEST_CASE("Random skirmish: multiplayer maps have equal open starts the lobby assigns", "[skirmish][generator][multiplayer]") {
    struct Case { int starts, size; Mode mode; BaseSize base; int army, city; bool symmetric; int tech; };
    const Case cases[] = {
        {2, 64, Mode::Vanilla, BaseSize::Small, 2, 0, true, 8},
        {2, 128, Mode::DuneCity, BaseSize::Medium, kArmyRandom, kCityRandom, true, 9},
        {3, 128, Mode::Vanilla, BaseSize::Medium, 3, 0, false, 9},
        {4, 128, Mode::DuneCity, BaseSize::Medium, 3, 2, true, 9},
        {4, 160, Mode::Vanilla, BaseSize::Large, kArmyRandom, 0, false, 7},
        {6, 192, Mode::DuneCity, BaseSize::Small, 2, 1, false, 9},
        {8, 192, Mode::Vanilla, BaseSize::Medium, 3, 0, true, 9},
    };
    for(const Case& k : cases) for(uint32_t seed : {3u, 20261015u}) {
        const Params p = multiplayerParams(k.starts, k.size, k.mode, k.base, k.army, k.city, k.symmetric, k.tech, seed);
        const Catalogue& houses = k.mode == Mode::DuneCity ? city() : vanilla();
        const Catalogue starts = startCatalogue(houses, k.starts);
        const Result r = generate(p, houses);
        INFO("starts " << k.starts << " size " << k.size << " mode " << static_cast<int>(k.mode) << " seed " << seed);
        REQUIRE(static_cast<int>(r.houses.size()) == k.starts);
        REQUIRE(r.ini == generate(p, houses).ini);
        REQUIRE(r.name.find(std::to_string(k.starts) + " starts") != std::string::npos);
        requireLegal(r, starts);
        requireFairStartRock(r, k.mode);

        // Every start is open, numbered in order and holds exactly what start 1 holds.
        // Wall wings beside turrets stop where the rock does, so walls are the one terrain-shaped extra.
        auto withoutWalls = [](std::map<int, int> counts) { counts.erase(Structure_Wall); return counts; };
        const auto structures = contentsOf(r.structures, 0), units = contentsOf(r.units, 0), tiles = withoutWalls(contentsOf(r.tiles, 0));
        REQUIRE(structures.count(Structure_ConstructionYard) == 1);
        std::vector<Coord> centres;
        for(int i = 0; i < k.starts; ++i) {
            const HouseSetup& h = r.houses[i];
            INFO("start " << i);
            REQUIRE(h.house == i);
            REQUIRE(h.role == Role::Open);
            REQUIRE(h.playerClass.empty());
            REQUIRE(h.skipped == 0);
            REQUIRE(h.base == k.base);
            REQUIRE(contentsOf(r.structures, i) == structures);
            REQUIRE(contentsOf(r.units, i) == units);
            REQUIRE(withoutWalls(contentsOf(r.tiles, i)) == tiles);
            REQUIRE(h.armyBudget == r.houses[0].armyBudget);
            REQUIRE(h.armySpent == r.houses[0].armySpent);
            REQUIRE(h.cityTarget == r.houses[0].cityTarget);
            REQUIRE(h.cityEstimate == r.houses[0].cityEstimate);
            REQUIRE((h.armyBudget == 0 || h.armySpent * 10 >= h.armyBudget * 9));
            REQUIRE((h.cityTarget > 0) == (k.mode == Mode::DuneCity && k.city > 0));
            centres.push_back(h.start);
        }
        REQUIRE(groundConnected(r.map, centres));

        // Whatever house the lobby puts on a start can field everything placed there.
        for(const auto& [item, count] : structures) for(int house : houses.houses) {
            INFO("structure " << houses.itemNames[item] << " house " << house);
            REQUIRE(houses.available(item, house, k.tech));
        }
        for(const auto& [item, count] : units) for(int house : houses.houses) {
            if(item == Unit_MCV) continue;
            INFO("unit " << houses.itemNames[item] << " house " << house);
            REQUIRE(houses.available(item, house, k.tech));
        }

        // Lobby-assignable: generic [Player1]..[PlayerN] sections, no house of its own.
        const ParsedMap parsed(r.ini);
        const INIFile& ini = *parsed.ini;
        REQUIRE(MapMetadata::countPlayers(ini) == k.starts);
        REQUIRE(MapPlayerSectionUtils::countNumberedPlayerSections(8, [&](int n) { return ini.hasSection("Player" + std::to_string(n)); }) == k.starts);
        for(const char* house : {"Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar", "Mercenary", "Neutral", "Rebels"})
            REQUIRE_FALSE(ini.hasSection(house));
        for(int i = 1; i <= k.starts; ++i) REQUIRE(ini.getIntValue("Player" + std::to_string(i), "Credits") == 5000);
        for(const char* section : {"STRUCTURES", "UNITS"})
            for(const auto& key : ini.getSection(section)) {
                const std::string owner = key.getStringValue().substr(0, key.getStringValue().find(','));
                REQUIRE(owner.rfind("Player", 0) == 0);
                REQUIRE(std::stoi(owner.substr(6)) >= 1);
                REQUIRE(std::stoi(owner.substr(6)) <= k.starts);
            }
        REQUIRE(ini.getStringValue("RANDOMSKIRMISH", "Starts") == std::to_string(k.starts));

        // Mirrored starts: terrain and every start's plateau are mirror images of each other.
        if(k.symmetric) {
            const int s = k.size;
            for(int y = 0; y < s; ++y) for(int x = 0; x < s; ++x) {
                REQUIRE(r.map(x, y) == r.map(s - 1 - x, s - 1 - y));
                if(k.starts > 2) REQUIRE(r.map(x, y) == r.map(s - 1 - x, y));
            }
            const Rect& first = r.houses[0].plateau;
            std::set<std::pair<int, int>> corners;
            for(const HouseSetup& h : r.houses) {
                REQUIRE(h.plateau.w == first.w);
                REQUIRE(h.plateau.h == first.h);
                corners.insert({h.plateau.x, h.plateau.y});
            }
            REQUIRE(static_cast<int>(corners.size()) == k.starts);
            if(k.starts == 2) {
                REQUIRE(r.houses[1].plateau.x == s - first.x - first.w);
                REQUIRE(r.houses[1].plateau.y == s - first.y - first.h);
            }
            if(k.starts == 4) for(const HouseSetup& h : r.houses) {
                REQUIRE((h.plateau.x == first.x || h.plateau.x == s - first.x - first.w));
                REQUIRE((h.plateau.y == first.y || h.plateau.y == s - first.y - first.h));
            }
        }
    }
}

TEST_CASE("Random skirmish: no unit starts on a spice bloom", "[skirmish][generator][army]") {
    // A unit on a bloom sets it off while the map loads (Tile::triggerSpiceBloom, which crashed
    // a local-spectator load of the 64x64 Fortress / 20,000-credit map with seed 7101).
    auto bloom = [](TERRAINTYPE t) {
        return t == Terrain_SpiceBloom || t == Terrain_SpecialBloom || t == Terrain_GreenSpiceBloom || t == Terrain_RedSpiceBloom;
    };
    int maps = 0, mapsWithBlooms = 0;
    for(const int size : {64, 128, 192})
        for(uint32_t seed = 7100; seed < 7120; ++seed) {
            const Params p = multiplayerParams(2, size, Mode::Vanilla, BaseSize::Fortress, 5, 0, true, 9, seed);
            const Result r = generate(p, vanilla());
            INFO("size " << size << " seed " << seed);
            bool blooms = false;
            for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) blooms = blooms || bloom(r.map(x, y));
            mapsWithBlooms += blooms ? 1 : 0;
            for(const Placement& u : r.units) REQUIRE_FALSE(bloom(r.map(u.x, u.y)));
            ++maps;
        }
    REQUIRE(maps == 60);
    REQUIRE(mapsWithBlooms > 0);
}

TEST_CASE("Random skirmish: sandworms start on open sand away from every start", "[skirmish][generator][worms]") {
    auto withoutWorms = [](const std::string& ini) {
        std::istringstream in(ini);
        std::string out, line;
        while(std::getline(in, line)) if(line.find("Sandworm") == std::string::npos) out += line + "\n";
        return out;
    };
    std::vector<Params> cases;
    for(uint32_t seed : {11u, 20261101u}) {
        cases.push_back(params(128, 1, 2, Mode::Vanilla, 9, seed));
        cases.push_back(params(64, 0, 1, Mode::DuneCity, 9, seed));
        cases.push_back(multiplayerParams(4, 128, Mode::Vanilla, BaseSize::Medium, 3, 0, true, 9, seed));
    }
    for(Params p : cases) {
        const bool multiplayer = p.multiplayer;
        const Catalogue& houses = p.mode == Mode::DuneCity ? city() : vanilla();
        p.worms = 0;
        const Result none = generate(p, houses);
        REQUIRE(none.worms.empty());
        for(int count : {1, 4, 8}) {
            p.worms = count;
            const Result r = generate(p, houses);
            INFO("size " << p.mapSize << " multiplayer " << multiplayer << " seed " << p.seed << " worms " << count);
            REQUIRE(static_cast<int>(r.worms.size()) == count);
            // Worms never change the rest of the map.
            REQUIRE(withoutWorms(r.ini) == none.ini);
            const int clearance = std::min(kWormStartClearance, p.mapSize / 5);
            std::set<std::pair<int, int>> taken;
            for(const Placement& u : r.units) taken.insert({u.x, u.y});
            for(const Placement& w : r.worms) {
                const TERRAINTYPE t = r.map(w.x, w.y);
                REQUIRE((t == Terrain_Sand || t == Terrain_Dunes || t == Terrain_Spice || t == Terrain_ThickSpice));
                for(const HouseSetup& h : r.houses)
                    REQUIRE(std::max(std::abs(h.start.x - w.x), std::abs(h.start.y - w.y)) >= clearance / 2);
                REQUIRE(taken.insert({w.x, w.y}).second);
                for(const Placement& s : r.structures) {
                    const Coord size = houses.sizes[s.item];
                    REQUIRE_FALSE((w.x >= s.x && w.x < s.x + size.x && w.y >= s.y && w.y < s.y + size.y));
                }
            }
            // Written as units the loader hands to the Natives house, whichever starts are played.
            size_t lines = 0;
            for(size_t at = r.ini.find("=Fremen,Sandworm,256,"); at != std::string::npos; at = r.ini.find("=Fremen,Sandworm,256,", at + 1)) ++lines;
            REQUIRE(lines == r.worms.size());
            REQUIRE(r.ini.find("Sandworms=" + std::to_string(count)) != std::string::npos);
        }
    }

    // The choice is remembered and snaps to the menu's values.
    INIFile config(false, std::string());
    Params saved;
    saved.worms = 6;
    saved.save(config);
    Params loaded;
    loaded.load(config);
    REQUIRE(loaded.worms == 6);
    config.setIntValue("Random Skirmish", "Sandworms", 100);
    loaded.load(config);
    loaded.clamp(8);
    REQUIRE(loaded.worms == 8);
}

TEST_CASE("Random skirmish: start catalogue is what every house can field", "[skirmish][generator][multiplayer]") {
    for(const Catalogue* c : {&vanilla(), &city()}) {
        const Catalogue starts = startCatalogue(*c, 4);
        REQUIRE(starts.houses == std::vector<int>{0, 1, 2, 3});
        REQUIRE(starts.houseNames[0] == "Player1");
        REQUIRE(starts.houseNames[3] == "Player4");
        for(int item = ItemID_Invalid + 1; item < Num_ItemID; ++item) {
            if(c->itemNames[item].empty()) continue;
            int slowest = 0, price = 0;
            for(int house : c->houses) {
                const int level = c->unlockLevel(item, house);
                slowest = slowest < 0 || level < 0 ? -1 : std::max(slowest, level);
                price = std::max(price, c->rule(item, house).price);
            }
            INFO("item " << c->itemNames[item]);
            for(int start = 0; start < 4; ++start) {
                REQUIRE(starts.unlockLevel(item, start) == (starts.rule(item, start).enabled ? slowest : -1));
                REQUIRE(starts.rule(item, start).price == price);
            }
        }
    }
}

TEST_CASE("Random skirmish: the multiplayer screen keeps its own choices", "[skirmish][generator][multiplayer]") {
    INIFile config(false, std::string());
    Params single = params(128, 1, 2, Mode::DuneCity, 7, 99);
    single.mod = "dunecity"; single.symmetric = false;
    single.save(config);
    Params hosted;
    hosted.multiplayer = true; hosted.symmetric = true;
    hosted.load(config);
    // Mirrored starts default on for a hosted map although the single-player choice is off.
    REQUIRE(hosted.symmetric);
    hosted.mod = "vanilla"; hosted.mode = Mode::Vanilla;
    hosted.starts = 6; hosted.startSide = Side{"", BaseSize::Large, kArmyRandom, 3};
    hosted.save(config);
    // The host's mod is never written over the single-player choice.
    Params back;
    back.load(config);
    REQUIRE(back.mod == "dunecity"); REQUIRE(back.mode == Mode::DuneCity);
    REQUIRE_FALSE(back.symmetric);
    REQUIRE(back.starts == 6); REQUIRE(back.startSide.base == BaseSize::Large);
    REQUIRE(back.startSide.army == kArmyRandom); REQUIRE(back.startSide.city == 3);
    Params hostedAgain;
    hostedAgain.multiplayer = true; hostedAgain.symmetric = true;
    hostedAgain.load(config);
    REQUIRE(hostedAgain.symmetric);
    // Starts stay within 2-8 and the engine's house cap.
    hostedAgain.starts = 12; hostedAgain.clamp(8); REQUIRE(hostedAgain.starts == kMaxStarts);
    hostedAgain.starts = 12; hostedAgain.clamp(5); REQUIRE(hostedAgain.starts == 5);
    hostedAgain.starts = 0; hostedAgain.clamp(8); REQUIRE(hostedAgain.starts == kMinStarts);
}

namespace {
Catalogue familyCatalogue(int count) {
    Catalogue result = vanilla();
    result.tornieFamily = true;
    result.houses.clear();
    const char* names[] = {"Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar", "Mercenary",
                           "Neutral", "Rebels", "Corruptique", "Wildspade", "Kleshmersh", "Tharpique"};
    for(int house = 0; house < count; ++house) {
        result.houses.push_back(house);
        result.houseNames[house] = names[house];
        if(house >= NUM_LEGACY_HOUSES) {
            for(int item = 0; item < Num_ItemID; ++item)
                result.rules[item][house] = result.rules[item][HOUSE_HARKONNEN];
        }
    }
    return result;
}
}

TEST_CASE("Random skirmish: only family catalogues lift the original eight-house cap",
          "[skirmish][generator][tornie][isolation]") {
    REQUIRE(maxStarts(12) == 8);
    REQUIRE(maxOtherHouses(12) == 7);
    REQUIRE(maxStarts(12, true) == 12);
    REQUIRE(maxOtherHouses(12, true) == 11);
    Params requested = params(256, 4, 7, Mode::Vanilla, 9, 546);
    requested.house = HOUSE_WILDSPADE;
    requested.playerSide.army = 0;
    requested.allySide.army = 0;
    requested.enemySide.army = 0;
    const Catalogue full = familyCatalogue(12);
    const Result twelve = generate(requested, full);
    REQUIRE(twelve.houses.size() == 12);
    REQUIRE(twelve.houses.front().house == HOUSE_WILDSPADE);
    std::set<int> houses;
    for(const auto& house : twelve.houses) {
        REQUIRE(isPlayerHouseID(house.house));
        REQUIRE(houses.insert(house.house).second);
    }
    REQUIRE(houses.count(HOUSE_NATIVES) == 0);
    REQUIRE(twelve.ini == generate(requested, full).ini);
    const ParsedMap parsed(twelve.ini);
    REQUIRE(MapMetadata::countPlayers(*parsed.ini) == 12);
    requireLegal(twelve, full);

    const Catalogue lite = familyCatalogue(6);
    requested.house = HOUSE_ATREIDES;
    requested.allies = 0;
    requested.enemies = 11;
    const Result six = generate(requested, lite);
    REQUIRE(six.houses.size() == 6);
    for(const auto& house : six.houses) REQUIRE(house.house < HOUSE_NEUTRAL);

    requested.starts = 12;
    requested.clamp(12, false);
    REQUIRE(requested.starts == 8);
    REQUIRE(requested.allies + requested.enemies <= 7);
}

TEST_CASE("Random skirmish: ten through twelve mirrored starts have distinct legal positions",
          "[skirmish][generator][tornie][multiplayer]") {
    const Catalogue full = familyCatalogue(12);
    for(int count : {10, 11, 12}) {
        INFO(count);
        const auto requested = multiplayerParams(count, 256, Mode::Vanilla, BaseSize::Small, 0, 0, true, 9, 546);
        const Catalogue starts = startCatalogue(full, count);
        const Result result = generate(requested, full);
        REQUIRE(static_cast<int>(result.houses.size()) == count);
        REQUIRE(starts.tornieFamily);
        REQUIRE(result.ini == generate(requested, full).ini);
        requireLegal(result, starts);
        std::set<std::pair<int, int>> positions;
        std::vector<Coord> centres;
        for(int i = 0; i < count; ++i) {
            const auto& house = result.houses[i];
            REQUIRE(house.house == i);
            REQUIRE(house.house != HOUSE_NATIVES);
            REQUIRE(house.skipped == 0);
            REQUIRE(positions.insert({house.start.x, house.start.y}).second);
            centres.push_back(house.start);
        }
        REQUIRE(groundConnected(result.map, centres));
        const ParsedMap parsed(result.ini);
        REQUIRE(MapMetadata::countPlayers(*parsed.ini) == count);
        for(int i = 1; i <= count; ++i) REQUIRE(parsed.ini->hasSection("Player" + std::to_string(i)));
        for(int y = 0; y < 256; ++y) for(int x = 0; x < 256; ++x) {
            REQUIRE(result.map(x, y) == result.map(255 - x, y));
            REQUIRE(result.map(x, y) == result.map(255 - x, 255 - y));
        }
    }
}

TEST_CASE("Random skirmish: existing start counts keep their exact layout in family mode",
          "[skirmish][generator][tornie][isolation]") {
    Catalogue family = vanilla();
    family.tornieFamily = true;
    for(int count : {2, 8}) {
        const auto requested = multiplayerParams(count, 192, Mode::Vanilla, BaseSize::Small, 0, 0, true, 9, 546);
        REQUIRE(generate(requested, family).ini == generate(requested, vanilla()).ini);
    }
}

TEST_CASE("Random skirmish: Research Lab is a remembered rule with no free building",
          "[skirmish][research]") {
    Params options = params(64, 0, 1, Mode::DuneCity, 1, 20261102u);
    REQUIRE_FALSE(options.researchLab);
    const auto ordinary = generate(options, city());
    options.researchLab = true;
    INIFile config(false, std::string());
    options.save(config);
    Params restored;
    restored.load(config);
    REQUIRE(restored.researchLab);
    REQUIRE(restored.techLevel == 1);
    const auto research = generate(options, city());
    REQUIRE(research.ini == ordinary.ini);
    for(const auto& building : research.structures)
        REQUIRE(building.item != Structure_ResearchLab);
    for(int house : city().houses) {
        REQUIRE(city().available(Structure_ResearchLab, house, 1));
        REQUIRE(city().rule(Structure_ResearchLab, house).builder == Structure_ConstructionYard);
    }
}
