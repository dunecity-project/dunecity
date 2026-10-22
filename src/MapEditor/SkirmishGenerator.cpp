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

#include <MapEditor/SkirmishGenerator.h>

#include <MapEditor/MapGenerator.h>
#include <MapEditor/MapMirror.h>
#include <FileClasses/INIFile.h>
#include <INIMap/ZoneStartLevel.h>
#include <INIMap/MapMetadata.h>
#include <dunecity/CityEffects.h>
#include <dunecity/ZonePower.h>
#include <players/QuantBotCityPolicy.h>
#include <players/TacticalSafetyPolicy.h>
#include <misc/Random.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <deque>
#include <numeric>
#include <optional>

namespace SkirmishGenerator {

namespace {

/// Units an army may buy. No light infantry: foot soldiers come as Troopers only.
constexpr int kUnitTypes[] = {
    Unit_Trooper, Unit_Trike, Unit_RaiderTrike, Unit_Quad, Unit_Tank,
    Unit_Launcher, Unit_SiegeTank, Unit_SonicTank, Unit_Deviator, Unit_Devastator
};
/// Infantry share a tile up to the engine's limit (NUM_INFANTRY_PER_TILE).
constexpr int kInfantryPerTile = 5;
bool isFootUnit(int item) { return item == Unit_Trooper || item == Unit_Soldier; }
constexpr int kThickSpicePercent[] = {0, 10, 25, 40, 55};
constexpr int kMargin = 2;          ///< tiles kept between a start's clean core and the map edge
constexpr int kCityBlock = 5;       ///< 4x4 block of four 2x2 lots plus one road line
constexpr int kZonesPerPolice = 16;
constexpr int kMaxCityRows = 8;     ///< city blocks across the base-to-edge axis
constexpr int kNoiseCell = 6;       ///< lattice spacing of the rim noise
constexpr int kCoreClearance = 2;   ///< no mountain this close to a start's clean core
constexpr int kMassifDepth = 5;     ///< rock this deep inside a rock field becomes a mountain massif
constexpr int kRidgeEvery = 60;     ///< one mountain ridge per this many rock tiles outside the cores
constexpr int kOutcropsPerStart = 6;  ///< rim outcrops per 40 tiles of core width + height
constexpr int kBlastGap = 5;        ///< footprint distance a reactor keeps from protected buildings

int ceilDiv(int a, int b) { return (a + b - 1) / b; }
int isqrt(int value) { int r = 0; while((r + 1) * (r + 1) <= value) ++r; return r; }
/// Octagonal distance: integer, no square root, within 8% of Euclidean.
int octagonal(int dx, int dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}

template<class Enum> int indexOf(Enum value) { return static_cast<int>(value); }

int snapTo(int value, const int* begin, const int* end) {
    int best = *begin;
    for(const int* it = begin; it != end; ++it)
        if(std::abs(*it - value) < std::abs(best - value)) best = *it;
    return best;
}

bool isZone(int item) {
    return item == Structure_ZoneResidential || item == Structure_ZoneCommercial || item == Structure_ZoneIndustrial;
}

bool isTurret(int item) { return item == Structure_GunTurret || item == Structure_RocketTurret; }

bool isBuildable(TERRAINTYPE t) { return t == Terrain_Rock || t == Terrain_Slab; }

bool isSpiceTerrain(TERRAINTYPE t) {
    return t == Terrain_Spice || t == Terrain_ThickSpice || t == Terrain_SpiceBloom || t == Terrain_SpecialBloom
        || t == Terrain_GreenSpice || t == Terrain_ThickGreenSpice || t == Terrain_GreenSpiceBloom
        || t == Terrain_RedSpice || t == Terrain_ThickRedSpice || t == Terrain_RedSpiceBloom;
}

/// A unit placed on a bloom would set it off while the map loads (Tile::triggerSpiceBloom).
bool isSpiceBloomTerrain(TERRAINTYPE t) {
    return t == Terrain_SpiceBloom || t == Terrain_SpecialBloom || t == Terrain_GreenSpiceBloom || t == Terrain_RedSpiceBloom;
}

char terrainChar(TERRAINTYPE t) {
    switch(t) {
        case Terrain_Dunes:         return '^';
        case Terrain_Spice:         return '~';
        case Terrain_ThickSpice:    return '+';
        case Terrain_Rock:
        case Terrain_Slab:          return '%';
        case Terrain_Mountain:      return '@';
        case Terrain_SpiceBloom:    return 'O';
        case Terrain_SpecialBloom:  return 'Q';
        default:                    return '-';
    }
}

int populationOf(int item, int level) {
    // Zones report their placed level; every other city-role building starts at occupancy 1.
    const int effective = isZone(item) ? std::max(0, level) : 1;
    return DuneCity::getZonePopulation(item, effective)
        + (item == Structure_Palace ? DuneCity::getPalaceCommercialPopulation(effective) : 0);
}

const char* baseName(BaseSize size) {
    static const char* names[] = {"None", "Outpost", "Small", "Medium", "Large", "Fortress"};
    return names[indexOf(size)];
}

/// Tiles strictly between two footprints (0 when they touch, also diagonally).
int footprintGap(int ax, int ay, Coord ad, int bx, int by, Coord bd) {
    return std::max({0, bx - (ax + ad.x), ax - (bx + bd.x), by - (ay + ad.y), ay - (by + bd.y)});
}

/// Everything decided about one house before terrain exists.
struct Plan {
    HouseSetup setup;
    std::vector<int> base;          ///< buildings, turrets and power plants of the base
    std::vector<Placement> lots;    ///< city lots, x/y relative to the city rect with the base on its right
    std::vector<int> army;
    bool mcv = false;
    int wallsPerSide = 0;
    int baseSide = 0;
    int blocksX = 0, blocksY = 0;
    int width = 0, height = 0;
    int slot = 0;
};

// ---- City planning ----------------------------------------------------------------------

/// R/C/I zones at the highest level that still fits @a need (internal population), in the
/// R,C,R,I proportions that keep jobs close to one per eight residents.
std::vector<Placement> composeZones(int need, int house) {
    static constexpr int pattern[] = {Structure_ZoneResidential, Structure_ZoneCommercial,
                                      Structure_ZoneResidential, Structure_ZoneIndustrial};
    std::vector<Placement> zones;
    int misses = 0;
    for(int i = 0; need > 0 && misses < 4; ++i) {
        const int item = pattern[i % 4];
        int chosen = 0;
        for(int level = ZoneStartLevel::kMaxLevel; level >= 1 && chosen == 0; --level)
            if(populationOf(item, level) <= need) chosen = level;
        if(chosen == 0) { ++misses; continue; }
        misses = 0;
        Placement zone; zone.house = house; zone.item = item; zone.level = chosen;
        need -= populationOf(item, chosen);
        zones.push_back(zone);
    }
    return zones;
}

int cityPowerDraw(const std::vector<Placement>& lots, int house, const Catalogue& catalogue) {
    int draw = 0;
    for(const auto& lot : lots)
        draw += isZone(lot.item) ? DuneCity::getZonePower(lot.item, lot.level) : std::max(0, catalogue.rule(lot.item, house).power);
    return draw;
}

/// City-local top-left of lot @a index in block column @a column (0 = the base side, the
/// right edge) of a grid @a blocksX wide: top to bottom, base-side lot of each pair first.
Coord citySlot(int blocksX, int column, int index) {
    const int block = index / 4, q = index % 4;
    return Coord(1 + kCityBlock * (blocksX - 1 - column) + (q % 2 == 0 ? 2 : 0), 1 + kCityBlock * block + 2 * (q / 2));
}

/**
    Districts in whole columns of blocks (four 2x2 lots each, roads on every fifth line),
    from the base side outwards: Industrial, Commercial, Residential. A Commercial column always
    separates Industrial from Residential, so they stay at least kIndustrialBuffer tiles apart.
    Industrial is packed into one block-connected district, centred along its column;
    Commercial and Residential are spread evenly along theirs, so every lot has its trip
    destination a column away (the simulation drives at most 20 road tiles). Police stations
    take the empty lots that cover the most homes and shops.
*/
void arrangeCity(Plan& plan, const std::vector<Placement>& zones, int stations) {
    std::vector<Placement> districts[3];       // Industrial, Commercial, Residential
    for(const Placement& zone : zones)
        districts[zone.item == Structure_ZoneIndustrial ? 0 : zone.item == Structure_ZoneCommercial ? 1 : 2].push_back(zone);
    const int blocks = ceilDiv(static_cast<int>(zones.size()) + stations, 4);
    // Each of Industrial and Commercial fits one column where possible: then every trip
    // crosses at most a column or two, and a short Industrial district never sits far from
    // the far end of a tall Commercial column.
    const int tallest = std::max(ceilDiv(static_cast<int>(districts[0].size()), 4), ceilDiv(static_cast<int>(districts[1].size()), 4));
    const int rows = std::clamp(std::max(isqrt(blocks - 1) + 1, tallest), 1, std::max(1, std::min(kMaxCityRows, tallest + 2)));
    const int capacity = 4 * rows;
    std::vector<int> columns[3];
    int blocksX = 0;
    for(int d = 0; d < 3; ++d) {
        int count = ceilDiv(static_cast<int>(districts[d].size()), capacity);
        if(d == 1 && count == 0 && !districts[0].empty() && !districts[2].empty()) count = 1;
        for(int c = 0; c < count; ++c) columns[d].push_back(blocksX++);
    }
    // Room for the police: an empty column on the far side if the districts leave too few lots.
    if(blocksX * capacity - static_cast<int>(zones.size()) < stations) ++blocksX;

    plan.lots.clear();
    for(int d = 0; d < 3; ++d) {
        const int n = static_cast<int>(districts[d].size());
        for(size_t c = 0; c < columns[d].size(); ++c) {
            const int first = static_cast<int>(c) * capacity, inColumn = std::min(capacity, n - first);
            for(int i = 0; i < inColumn; ++i) {
                const int index = d == 0 ? ((capacity - inColumn) / 2 & ~1) + i : (2 * i + 1) * capacity / (2 * inColumn);
                Placement lot = districts[d][first + i];
                const Coord at = citySlot(blocksX, columns[d][c], index);
                lot.x = at.x; lot.y = at.y;
                plan.lots.push_back(lot);
            }
        }
    }

    std::vector<Coord> free;
    for(int column = 0; column < blocksX && stations > 0; ++column) for(int index = 0; index < capacity; ++index) {
        const Coord at = citySlot(blocksX, column, index);
        if(std::none_of(plan.lots.begin(), plan.lots.end(), [&](const Placement& p) { return p.x == at.x && p.y == at.y; }))
            free.push_back(at);
    }
    std::vector<char> covered(plan.lots.size(), 0);
    std::vector<Placement> placedStations;
    for(int s = 0; s < stations && !free.empty(); ++s) {
        int best = 0, bestScore = -1;
        for(size_t f = 0; f < free.size(); ++f) {
            int cover = 0, spread = 999;
            for(size_t l = 0; l < plan.lots.size(); ++l)
                cover += !covered[l] && plan.lots[l].item != Structure_ZoneIndustrial
                    && std::abs(plan.lots[l].x - free[f].x) + std::abs(plan.lots[l].y - free[f].y) <= 18;
            for(const Placement& p : placedStations) spread = std::min(spread, std::abs(p.x - free[f].x) + std::abs(p.y - free[f].y));
            const int score = cover * 1000 + spread;
            if(score > bestScore) { bestScore = score; best = static_cast<int>(f); }
        }
        Placement police; police.house = plan.setup.house; police.item = Structure_PoliceStation;
        police.x = free[best].x; police.y = free[best].y;
        for(size_t l = 0; l < plan.lots.size(); ++l)
            if(std::abs(plan.lots[l].x - police.x) + std::abs(plan.lots[l].y - police.y) <= 18) covered[l] = 1;
        placedStations.push_back(police);
        free.erase(free.begin() + best);
    }
    plan.lots.insert(plan.lots.end(), placedStations.begin(), placedStations.end());
    plan.blocksX = blocksX;
    plan.blocksY = rows;
}

void planCity(Plan& plan, int techLevel, const Catalogue& catalogue) {
    const int house = plan.setup.house;
    const int target = plan.setup.cityTarget / QuantBotCityPolicy::kPopDisplayMultiplier;
    if(target <= 0 || !catalogue.available(Structure_ZoneResidential, house, techLevel)) return;

    std::vector<Placement> zones;
    for(int round = 0; round < 3; ++round) {
        int basePopulation = 0;
        int res = 0, com = 0, ind = 0;
        for(int item : plan.base) basePopulation += populationOf(item, 1);
        zones = composeZones(target - basePopulation, house);
        auto tally = [&](int item, int level) {
            const int pop = populationOf(item, level);
            switch(DuneCity::getStructureCityRole(item)) {
                case DuneCity::CityRole::Residential: res += pop; break;
                case DuneCity::CityRole::Commercial:  com += pop; break;
                case DuneCity::CityRole::Industrial:  ind += pop; break;
                default: break;
            }
        };
        for(int item : plan.base) tally(item, 1);
        for(const auto& zone : zones) tally(zone.item, zone.level);

        // The same civic gates the simulation applies (missingDemandCivics): without them
        // the matching demand valve is held at zero and the city cannot keep its size.
        bool added = false;
        auto require = [&](bool needed, int item) {
            if(needed && catalogue.available(item, house, techLevel)
               && std::find(plan.base.begin(), plan.base.end(), item) == plan.base.end()) {
                plan.base.push_back(item);
                added = true;
            }
        };
        require(res > 500, Structure_Stadium);
        require(com > 100, Structure_Airport);
        require(ind > 70, Structure_StarPort);
        if(!added) break;
    }
    if(zones.empty()) return;

    const int stations = catalogue.available(Structure_PoliceStation, house, techLevel)
        ? ceilDiv(static_cast<int>(zones.size()), kZonesPerPolice) : 0;
    arrangeCity(plan, zones, stations);
}

// ---- Army ---------------------------------------------------------------------------------

/// Largest spend <= @a remaining from bounded copies of each type (0/1 knapsack over
/// binary-split bundles). Adds the chosen copies to @a counts.
int fillExactly(int remaining, const std::vector<int>& prices, const std::vector<int>& limits, std::vector<int>& counts) {
    if(remaining <= 0) return 0;
    int unit = 0;
    for(int price : prices) unit = std::gcd(unit, price);
    if(unit <= 0) return 0;
    const int states = remaining / unit;
    struct Bundle { int type; int copies; int weight; };
    std::vector<Bundle> bundles;
    for(size_t t = 0; t < prices.size(); ++t) {
        int left = limits[t];
        for(int copies = 1; left > 0; copies *= 2) {
            const int take = std::min(copies, left);
            bundles.push_back({static_cast<int>(t), take, take * prices[t] / unit});
            left -= take;
        }
    }
    std::vector<char> reached(states + 1, 0);
    std::vector<int> from(states + 1, -1), previous(states + 1, -1);
    reached[0] = 1;
    for(size_t b = 0; b < bundles.size(); ++b) {
        const int w = bundles[b].weight;
        for(int s = states; s >= w; --s) {
            if(!reached[s] && reached[s - w]) { reached[s] = 1; from[s] = static_cast<int>(b); previous[s] = s - w; }
        }
    }
    int best = states;
    while(best > 0 && !reached[best]) --best;
    for(int s = best; s > 0; s = previous[s]) counts[bundles[from[s]].type] += bundles[from[s]].copies;
    return best * unit;
}

// ---- Terrain ------------------------------------------------------------------------------

struct TerrainSpec {
    int size = 128;
    Rock rock = Rock::Medium;
    Spice spice = Spice::Medium;
    MirrorMode mirror = MirrorModeNone;
    std::vector<Rect> cores;        ///< every start's clean rock, mirror images included
    std::vector<Coord> centers;     ///< start centres in ring order
};

int rockPercent(const MapData& map) {
    int rock = 0;
    for(int y = 0; y < map.getSizeY(); ++y)
        for(int x = 0; x < map.getSizeX(); ++x)
            if(map(x, y) == Terrain_Rock || map(x, y) == Terrain_Mountain) ++rock;
    return rock * 100 / (map.getSizeX() * map.getSizeY());
}

int spiceTiles(const MapData& map) {
    int spice = 0;
    for(int y = 0; y < map.getSizeY(); ++y)
        for(int x = 0; x < map.getSizeX(); ++x) spice += isSpiceTerrain(map(x, y));
    return spice;
}

/// Apply @a type at (x,y) and at every mirror image, so post-processing keeps symmetry.
template<class Accept>
void paintMirrored(MapData& map, const MapMirror& mirror, int x, int y, TERRAINTYPE type, Accept accept) {
    if(!map.isInsideMap(x, y)) return;
    for(int i = 0; i < mirror.getSize(); ++i) {
        const Coord c = mirror.getCoord(Coord(x, y), i);
        if(map.isInsideMap(c.x, c.y) && accept(map(c.x, c.y))) map(c.x, c.y) = type;
    }
}

int canonicalIndex(const MapMirror& mirror, int size, int x, int y) {
    int best = INT_MAX;
    for(int i = 0; i < mirror.getSize(); ++i) {
        const Coord c = mirror.getCoord(Coord(x, y), i);
        best = std::min(best, c.y * size + c.x);
    }
    return best;
}

/// Seeded value noise, bilinear between lattice points kNoiseCell apart; the result lies in
/// [lo, hi] scaled by kNoiseCell^2. Integer only.
int valueNoise(uint32_t seed, int x, int y, int lo, int hi) {
    const int c = kNoiseCell, gx = x / c, gy = y / c, fx = x % c, fy = y % c;
    auto lattice = [&](int i, int j) {
        return lo + static_cast<int>(mix(seed, static_cast<uint32_t>(i) * 40503u + static_cast<uint32_t>(j) * 9973u + 1u)
                                     % static_cast<uint32_t>(hi - lo + 1));
    };
    return lattice(gx, gy) * (c - fx) * (c - fy) + lattice(gx + 1, gy) * fx * (c - fy)
         + lattice(gx, gy + 1) * (c - fx) * fy + lattice(gx + 1, gy + 1) * fx * fy;
}

/// Squared distance from (x,y) to the nearest tile of @a r (0 inside).
int distanceToRect2(const Rect& r, int x, int y) {
    const int dx = std::max({r.x - x, 0, x - (r.x + r.w - 1)});
    const int dy = std::max({r.y - y, 0, y - (r.y + r.h - 1)});
    return dx * dx + dy * dy;
}

/// A random tile, x drawn before y. Not Coord(random.rand(..), random.rand(..)): the order in
/// which function arguments are evaluated is unspecified (clang: left to right, MSVC: right to
/// left), so the same seed generated a different map on Windows than on macOS and the browser.
Coord randomTile(Random& random, int size) {
    const int x = static_cast<int>(random.rand(0, size - 1));
    const int y = static_cast<int>(random.rand(0, size - 1));
    return Coord(x, y);
}

/// The map editor's spot walk: @a steps random unit steps from @a at, clamped to the map.
template<class Visit>
void walk(Random& random, int size, Coord at, int steps, Visit visit) {
    int x = std::clamp(at.x, 0, size - 1), y = std::clamp(at.y, 0, size - 1);
    for(int i = 0; i < steps; ++i) {
        switch(random.rand(0, 3)) {
            case 0: --x; break;
            case 1: ++x; break;
            case 2: --y; break;
            default: ++y; break;
        }
        x = std::clamp(x, 0, size - 1);
        y = std::clamp(y, 0, size - 1);
        visit(x, y);
    }
}

int side4(const MapData& map, int x, int y, bool (*match)(TERRAINTYPE)) {
    int n = 0;
    for(const Coord& d : {Coord(1,0), Coord(-1,0), Coord(0,1), Coord(0,-1)}) {
        const int nx = std::clamp(x + d.x, 0, map.getSizeX() - 1), ny = std::clamp(y + d.y, 0, map.getSizeY() - 1);
        n += match(map(nx, ny));
    }
    return n;
}

bool isRockTile(TERRAINTYPE t) { return t == Terrain_Rock; }
bool isSandTile(TERRAINTYPE t) { return t == Terrain_Sand || t == Terrain_Dunes; }
bool isThinSpice(TERRAINTYPE t) { return t == Terrain_Spice; }
bool isFieldSpice(TERRAINTYPE t) { return t == Terrain_Spice || t == Terrain_ThickSpice; }
bool isThickSpice(TERRAINTYPE t) { return t == Terrain_ThickSpice; }

/**
    The map editor's fill-in pass (thickSpots / thickThickSpiceSpots): a tile with three
    matching neighbours joins, one with two joins on a coin flip. Decided on a snapshot with a
    coin keyed to the mirror-canonical tile, so mirrored maps stay mirrored.
*/
template<class Eligible>
void fillIn(MapData& map, const MapMirror& mirror, uint32_t seed, TERRAINTYPE type, bool (*match)(TERRAINTYPE), Eligible eligible) {
    const int size = map.getSizeX();
    const MapData before = map;
    for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) {
        if(before(x, y) == type || !eligible(x, y, before(x, y))) continue;
        const int n = side4(before, x, y, match);
        if(n >= 3 || (n == 2 && (mix(seed, static_cast<uint32_t>(canonicalIndex(mirror, size, x, y))) & 1u)))
            map(x, y) = type;
    }
}

/// Distance (8-neighbour steps) from every rock tile to the nearest tile that is not rock; the
/// map border counts as an edge of the rock, so massifs never run along it.
std::vector<int> rockDepth(const MapData& map) {
    const int size = map.getSizeX();
    std::vector<int> depth(size * size, INT_MAX);
    std::deque<int> queue;
    for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x)
        if(map(x, y) != Terrain_Rock) { depth[y * size + x] = 0; queue.push_back(y * size + x); }
    for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x)
        if(map(x, y) == Terrain_Rock && (x == 0 || y == 0 || x == size - 1 || y == size - 1)) {
            depth[y * size + x] = 1;
            queue.push_back(y * size + x);
        }
    while(!queue.empty()) {
        const int at = queue.front(); queue.pop_front();
        const int x = at % size, y = at / size;
        for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx, ny = y + dy;
            if(!map.isInsideMap(nx, ny) || depth[ny * size + nx] <= depth[at] + 1) continue;
            depth[ny * size + nx] = depth[at] + 1;
            queue.push_back(ny * size + nx);
        }
    }
    return depth;
}

/**
    Rock and mountains, no spice yet. The map editor's random map with @a rockFields rock
    fields (organic fields, dunes, rock bits) as the base; then scattered rock islands; then
    every start's clean core with an irregular rim grown around it (noise between kRimMin and
    kRimMax tiles, smoothed with the editor's fill-in rule); then mountains: massifs deep inside
    large rock fields, wandering ridges through rock, and a few outcrops on every start's rim.
    No mountain comes within kCoreClearance tiles of a core.
*/
MapData shapeRock(const TerrainSpec& spec, uint32_t mapSeed, int rockFields) {
    const int size = spec.size, area = size * size;
    MapData map = generateRandomMap(size, size, static_cast<int>(mapSeed), rockFields, 0, spec.mirror);
    const auto mirror = MapMirror::createMapMirror(spec.mirror, size, size);
    Random random(mix(mapSeed, 0x2F6B1u));
    auto onSand = [](TERRAINTYPE t) { return isSandTile(t); };
    auto onRock = [](TERRAINTYPE t) { return t == Terrain_Rock; };

    // Rock islands scattered over the sand.
    const int islands = std::max(2, area / 2048);
    for(int i = 0; i < islands; ++i) {
        const Coord at = randomTile(random, size);
        const int steps = static_cast<int>(random.rand(15, 70)) * size / 128 + 8;
        walk(random, size, at, steps, [&](int x, int y) { paintMirrored(map, *mirror, x, y, Terrain_Rock, onSand); });
    }

    // Start plateaus: the clean core, then a rim of irregular width around it.
    std::vector<char> nearCore(area, 0), rimZone(area, 0);
    const uint32_t rimSeed = mix(mapSeed, 0x7A11u);
    const int c2 = kNoiseCell * kNoiseCell;
    for(const Rect& r : spec.cores) {
        for(int y = std::max(0, r.y - kRimMax - 1); y < std::min(size, r.y + r.h + kRimMax + 1); ++y)
            for(int x = std::max(0, r.x - kRimMax - 1); x < std::min(size, r.x + r.w + kRimMax + 1); ++x) {
                const int d2 = distanceToRect2(r, x, y);
                const int canonical = canonicalIndex(*mirror, size, x, y);
                // Smooth noise for the shape, a tile of per-tile jitter for a ragged edge.
                const int jitter = static_cast<int>(mix(rimSeed ^ 0x51u, static_cast<uint32_t>(canonical)) % 3) - 1;
                const int rim = std::max(kRimMin * c2, valueNoise(rimSeed, canonical % size, canonical / size, kRimMin, kRimMax)
                                                        + jitter * c2);
                rimZone[y * size + x] = 1;
                if(d2 <= kCoreClearance * kCoreClearance * 2) nearCore[y * size + x] = 1;
                if(d2 * c2 * c2 <= rim * rim) map(x, y) = Terrain_Rock;
            }
    }
    for(int pass = 0; pass < 2; ++pass)
        fillIn(map, *mirror, mix(rimSeed, pass), Terrain_Rock, isRockTile,
               [&](int x, int y, TERRAINTYPE t) { return rimZone[y * size + x] && isSandTile(t); });
    // Lobes: editor-style rock walks out from beyond every rim, so no start is a rounded box.
    for(const Rect& r : spec.cores) {
        for(int lobe = 0; lobe < 3; ++lobe) {
            const int along = static_cast<int>(random.rand(0, 2 * (r.w + r.h) - 1));
            const int reach = kRimMax - 1;
            Coord at = along < r.w ? Coord(r.x + along, r.y - reach)
                     : along < 2 * r.w ? Coord(r.x + along - r.w, r.y + r.h - 1 + reach)
                     : along < 2 * r.w + r.h ? Coord(r.x - reach, r.y + along - 2 * r.w)
                     : Coord(r.x + r.w - 1 + reach, r.y + along - 2 * r.w - r.h);
            walk(random, size, at, (r.w + r.h) * 2, [&](int x, int y) { paintMirrored(map, *mirror, x, y, Terrain_Rock, onSand); });
        }
    }

    // Mountains: massifs deep inside rock fields away from every start, wandering ridges, and
    // outcrops on the start rims. The cores stay clean.
    const std::vector<int> depth = rockDepth(map);
    auto mountainOk = [&](int x, int y) {
        return map.isInsideMap(x, y) && map(x, y) == Terrain_Rock && !nearCore[y * size + x];
    };
    int outer = 0;      // rock outside every clean core
    const uint32_t massifSeed = mix(mapSeed, 0x3A55u);
    for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) {
        const int d = depth[y * size + x];
        if(!mountainOk(x, y)) continue;
        // Massifs keep a ragged berth of 10-16 tiles from every start's core.
        const int canonical = canonicalIndex(*mirror, size, x, y);
        const int berth = valueNoise(massifSeed, canonical % size, canonical / size, kRimMax + 3, kRimMax + 9);
        bool open = !rimZone[y * size + x];
        for(const Rect& r : spec.cores) open = open && distanceToRect2(r, x, y) * c2 * c2 > berth * berth;
        if(d >= kMassifDepth && open) map(x, y) = Terrain_Mountain;
        else ++outer;
    }
    // Ridges wander through rock fields at least two tiles in from the sand, outside the start
    // rims (whose edges get outcrops instead), so they never trace an outline.
    auto ridgeOk = [&](int x, int y) { return mountainOk(x, y) && depth[y * size + x] >= 2 && !rimZone[y * size + x]; };
    auto ridge = [&](Coord at, int length) {
        int dir = static_cast<int>(random.rand(0, 3));
        static const Coord steps[] = {Coord(1,0), Coord(0,1), Coord(-1,0), Coord(0,-1)};
        for(int i = 0; i < length; ++i) {
            paintMirrored(map, *mirror, at.x, at.y, Terrain_Mountain, onRock);
            if(random.rand(0, 1) == 0) {
                const Coord side = steps[(dir + 1) % 4];
                if(ridgeOk(at.x + side.x, at.y + side.y))
                    paintMirrored(map, *mirror, at.x + side.x, at.y + side.y, Terrain_Mountain, onRock);
            }
            if(random.rand(0, 1) == 0) dir = (dir + (random.rand(0, 1) ? 1 : 3)) % 4;
            // Wander on through rock; a ridge ends only where every way on is blocked.
            bool moved = false;
            for(int turn : {0, 1, 3, 2}) {
                const int d = (dir + turn) % 4;
                const Coord next(at.x + steps[d].x, at.y + steps[d].y);
                if(!ridgeOk(next.x, next.y)) continue;
                dir = d; at = next; moved = true;
                break;
            }
            if(!moved) return;
        }
    };
    const int ridges = outer / kRidgeEvery;
    for(int i = 0, tries = 0; i < ridges && tries < ridges * 20; ++tries) {
        const int x = static_cast<int>(random.rand(0, size - 1)), y = static_cast<int>(random.rand(0, size - 1));
        if(!ridgeOk(x, y)) continue;
        ridge(Coord(x, y), static_cast<int>(random.rand(8, 12 + size / 8)));
        ++i;
    }
    // Outcrops on the rims, where a plateau meets the sand: clumps like the editor's spots, more
    // of them along a longer outline.
    // Every outcrop is painted at each mirror image, so a mirrored map draws fewer per core.
    // They stay within three tiles of the sand: inside a crowded rock plain they would wall
    // starts off from each other.
    auto outcropOk = [&](const Rect& r, int x, int y) {
        return mountainOk(x, y) && distanceToRect2(r, x, y) >= 9 && depth[y * size + x] <= 3;
    };
    for(const Rect& r : spec.cores) {
        const int outcrops = std::max(1, kOutcropsPerStart * (r.w + r.h) / 40 / mirror->getSize());
        for(int i = 0, tries = 0; i < outcrops && tries < 400; ++tries) {
            const int x = r.x - kRimMax + static_cast<int>(random.rand(0, r.w + 2 * kRimMax));
            const int y = r.y - kRimMax + static_cast<int>(random.rand(0, r.h + 2 * kRimMax));
            if(!outcropOk(r, x, y)) continue;
            walk(random, size, Coord(x, y), static_cast<int>(random.rand(15, 40)), [&](int wx, int wy) {
                if(outcropOk(r, wx, wy)) paintMirrored(map, *mirror, wx, wy, Terrain_Mountain, onRock);
            });
            ++i;
        }
    }
    return map;
}

/**
    Spice fields grown like the map editor's (random walks that thicken where they cross
    themselves, then the editor's fill-in for spice and thick spice) until the spice share
    reaches the Spice setting. The first fields go beside every start, then between
    neighbouring starts, then anywhere on the sand.
*/
void addSpice(MapData& map, const TerrainSpec& spec, uint32_t mapSeed) {
    const int size = spec.size, area = size * size;
    const auto mirror = MapMirror::createMapMirror(spec.mirror, size, size);
    const int spiceIndex = indexOf(spec.spice);
    if(spec.spice == Spice::None) {
        for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x)
            if(isSpiceTerrain(map(x, y))) map(x, y) = Terrain_Sand;
        return;
    }
    Random random(mix(mapSeed, 0x5B1CEu));
    const int target = kSpicePercent[spiceIndex] * area / 100;
    // Walks stop a little short of the target: the fill-in passes grow every field a bit.
    const int walkTarget = target * 85 / 100;
    const int spotSize = 640 * area / 4096;     // the editor's walk length

    std::vector<Coord> seeds;
    const Coord middle(size / 2, size / 2);
    for(size_t i = 0; i < spec.centers.size(); ++i) {
        const Coord s = spec.centers[i];
        const int dx = middle.x - s.x, dy = middle.y - s.y, length = std::max(1, octagonal(dx, dy));
        int reach = 0;
        for(const Rect& r : spec.cores)
            if(r.contains(s.x, s.y)) reach = std::max(reach, (r.w + r.h) / 4 + kRimMax + 6);
        reach = std::min(reach, length);
        seeds.emplace_back(s.x + dx * reach / length, s.y + dy * reach / length);
    }
    const size_t n = spec.centers.size();
    for(size_t i = 0; i < n && n > 1; ++i) {
        if(n == 2 && i == 1) break;
        const Coord a = spec.centers[i], b = spec.centers[(i + 1) % n];
        seeds.emplace_back((a.x + b.x) / 2, (a.y + b.y) / 2);
    }

    auto onSand = [](TERRAINTYPE t) { return isSandTile(t); };
    auto onThin = [](TERRAINTYPE t) { return t == Terrain_Spice; };
    int spice = spiceTiles(map);
    for(size_t field = 0; spice < walkTarget && field < 96; ++field) {
        const Coord at = field < seeds.size() ? seeds[field] : randomTile(random, size);
        const int steps = spotSize * static_cast<int>(random.rand(2, 5)) / 8;
        walk(random, size, at, steps, [&](int x, int y) {
            if(isSandTile(map(x, y))) paintMirrored(map, *mirror, x, y, Terrain_Spice, onSand);
            else if(map(x, y) == Terrain_Spice && side4(map, x, y, isFieldSpice) >= 4)
                paintMirrored(map, *mirror, x, y, Terrain_ThickSpice, onThin);
        });
        spice = spiceTiles(map);
    }
    for(int pass = 0; pass < 2; ++pass)
        fillIn(map, *mirror, mix(mapSeed, 0x51CE0u + pass), Terrain_Spice, isThinSpice,
               [](int, int, TERRAINTYPE t) { return isSandTile(t); });
    for(int pass = 0; pass < 2; ++pass)
        fillIn(map, *mirror, mix(mapSeed, 0x7C1CEu + pass), Terrain_ThickSpice, isThickSpice,
               [&](int x, int y, TERRAINTYPE t) { return t == Terrain_Spice && side4(map, x, y, isFieldSpice) >= 4; });

    // Thick spice share: interior spice tiles, chosen by a mirror-invariant hash.
    const int percent = kThickSpicePercent[spiceIndex];
    const MapData before = map;
    for(int y = 1; y < size - 1; ++y) for(int x = 1; x < size - 1; ++x) {
        if(before(x, y) != Terrain_Spice || side4(before, x, y, isFieldSpice) < 4) continue;
        if(static_cast<int>(mix(mapSeed, canonicalIndex(*mirror, size, x, y)) % 100) < percent)
            map(x, y) = Terrain_ThickSpice;
    }
}

/**
    Above what the editor's rock fields reach, rock spreads outward from its edges ring by ring,
    each sand tile taking a seeded coin flip per ring, so coastlines stay ragged. Every tile is
    painted with its mirror images, so mirrored maps stay symmetric. At 100% every tile ends as
    rock (mountains included).
*/
void growRock(MapData& map, const TerrainSpec& spec, uint32_t mapSeed, int target) {
    const int size = spec.size, area = size * size;
    const auto mirror = MapMirror::createMapMirror(spec.mirror, size, size);
    const auto isRock = [](TERRAINTYPE t) { return t == Terrain_Rock || t == Terrain_Mountain; };
    int rock = 0;
    for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) rock += isRock(map(x, y));
    const auto paint = [&](int x, int y) {
        for(int i = 0; i < mirror->getSize(); ++i) {
            const Coord c = mirror->getCoord(Coord(x, y), i);
            if(map.isInsideMap(c.x, c.y) && !isRock(map(c.x, c.y))) { map(c.x, c.y) = Terrain_Rock; ++rock; }
        }
    };
    for(int ring = 0; rock * 100 < target * area && ring < 4 * size; ++ring) {
        const MapData before = map;
        const bool lastRings = ring >= 2 * size;   // nothing left to chance: finish the job
        for(int y = 0; y < size && rock * 100 < target * area; ++y) for(int x = 0; x < size && rock * 100 < target * area; ++x) {
            if(isRock(before(x, y))) continue;
            bool edge = false;
            for(const Coord d : {Coord(1, 0), Coord(-1, 0), Coord(0, 1), Coord(0, -1)})
                edge = edge || (before.isInsideMap(x + d.x, y + d.y) && isRock(before(x + d.x, y + d.y)));
            if(!edge && !lastRings) continue;
            if(!lastRings && mix(mix(mapSeed, 0x6C0Bu + static_cast<uint32_t>(ring)),
                                 static_cast<uint32_t>(canonicalIndex(*mirror, size, x, y))) % 100 >= 55) continue;
            paint(x, y);
        }
    }
}

/// Smallest editor rock-field count whose terrain reaches the Rock setting (rock then grows
/// further for the high settings), then spice on whatever sand is left.
MapData buildTerrain(const TerrainSpec& spec, uint32_t mapSeed) {
    const int target = kRockPercent[indexOf(spec.rock)];
    int lo = 0, hi = 64;
    if(rockPercent(shapeRock(spec, mapSeed, hi)) < target) lo = hi;
    while(lo < hi) {
        const int mid = (lo + hi) / 2;
        if(rockPercent(shapeRock(spec, mapSeed, mid)) >= target) hi = mid; else lo = mid + 1;
    }
    MapData map = shapeRock(spec, mapSeed, lo);
    if(rockPercent(map) < target) growRock(map, spec, mapSeed, target);
    addSpice(map, spec, mapSeed);
    return map;
}

// ---- Layout -------------------------------------------------------------------------------

enum Occupancy : char { Free = 0, Building = 1, City = 2, UnitTile = 3, WallTile = 4 };

struct Layout {
    const Catalogue& catalogue;
    const MapData& map;
    int size;
    std::vector<char> occupied;
    std::vector<int> owner;     ///< start owner (index into plans) or -1
    Result& result;

    Layout(const Catalogue& c, const MapData& m, Result& r)
     : catalogue(c), map(m), size(m.getSizeX()), occupied(size * size, Free), owner(size * size, -1), result(r) {}

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size; }
    char& at(int x, int y) { return occupied[y * size + x]; }
    bool ownable(int x, int y, int planIndex) const { return owner[y * size + x] < 0 || owner[y * size + x] == planIndex; }

    void tile(int house, int item, int x, int y) {
        Placement p; p.house = house; p.item = item; p.x = x; p.y = y;
        result.tiles.push_back(p);
    }

    /// Footprint on free buildable rock of this start, with a one-tile lane around it.
    bool fits(const Rect& region, int planIndex, int x, int y, Coord dim) {
        for(int dy = -1; dy <= dim.y; ++dy) for(int dx = -1; dx <= dim.x; ++dx) {
            const int tx = x + dx, ty = y + dy;
            const bool footprint = dx >= 0 && dy >= 0 && dx < dim.x && dy < dim.y;
            if(footprint) {
                if(!region.contains(tx, ty) || !inside(tx, ty) || !isBuildable(map(tx, ty)) || at(tx, ty) != Free
                   || !ownable(tx, ty, planIndex)) return false;
            } else if(inside(tx, ty) && (at(tx, ty) == Building || at(tx, ty) == WallTile)) {
                return false;
            }
        }
        return true;
    }

    void build(int house, int item, int x, int y, bool concrete, int level = -1) {
        const Coord dim = catalogue.sizes[item];
        for(int dy = 0; dy < dim.y; ++dy) for(int dx = 0; dx < dim.x; ++dx) {
            at(x + dx, y + dy) = Building;
            if(concrete) tile(house, Structure_Slab1, x + dx, y + dy);
        }
        Placement p; p.house = house; p.item = item; p.x = x; p.y = y; p.level = level;
        result.structures.push_back(p);
    }

    /// The city's road grid and its lots; @a flip mirrors the plan when the base is on the left.
    /// On a crowded map the city only takes tiles nobody else has built on or owns.
    void layoutCity(Plan& plan, int planIndex, const Rect& city, bool flip) {
        const int house = plan.setup.house;
        std::vector<char> mine(size * size, 0);
        for(int y = city.y; y < city.y + city.h; ++y) for(int x = city.x; x < city.x + city.w; ++x) {
            if(!inside(x, y) || at(x, y) != Free || !ownable(x, y, planIndex)) continue;
            const bool road = (x - city.x) % kCityBlock == 0 || (y - city.y) % kCityBlock == 0;
            at(x, y) = City;
            mine[y * size + x] = 1;
            if(road && isBuildable(map(x, y))) tile(house, Structure_Road, x, y);
        }
        for(const Placement& lot : plan.lots) {
            const int x = city.x + (flip ? city.w - lot.x - 2 : lot.x), y = city.y + lot.y;
            bool ok = true;
            for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
                ok = ok && inside(x + dx, y + dy) && city.contains(x + dx, y + dy) && mine[(y + dy) * size + x + dx]
                     && isBuildable(map(x + dx, y + dy));
            if(!ok) { ++plan.setup.skipped; continue; }
            Placement p = lot; p.x = x; p.y = y;
            result.structures.push_back(p);
            for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx) at(x + dx, y + dy) = Building;
        }
    }

    /**
        A base grown around its Construction Yard: every building takes the free spot with the
        best score (close to the yard, refineries toward the nearest spice, silos beside
        refineries, factories toward the enemy, power plants behind, reactors blast-spaced from
        protected buildings) plus seeded jitter, so spacing and outline vary. Turrets then take
        spots just outside the buildings, spread along the side facing the enemy, with wall runs
        across the approach.
    */
    void layoutBase(Plan& plan, int planIndex, const Rect& region, Coord heart, Coord approach, Coord spice, uint32_t seed) {
        const int house = plan.setup.house;
        std::vector<int> buildings, turrets;
        for(int item : plan.base) (isTurret(item) ? turrets : buildings).push_back(item);
        auto rank = [](int item) {
            switch(item) {
                case Structure_ConstructionYard: return 0;
                case Structure_Refinery:         return 2;
                case Structure_Silo:             return 3;
                case Structure_WindTrap:         return 4;
                case Structure_NuclearPlant:     return 5;
                default:                         return 1;
            }
        };
        std::stable_sort(buildings.begin(), buildings.end(), [&](int a, int b) {
            if(rank(a) != rank(b)) return rank(a) < rank(b);
            const Coord da = catalogue.sizes[a], db = catalogue.sizes[b];
            return da.x * da.y > db.x * db.y;
        });
        const int alen = std::max(1, octagonal(approach.x, approach.y));
        int hx = 2 * heart.x, hy = 2 * heart.y;     // doubled coordinates: footprint centres are exact
        std::vector<Placement> placed;
        auto score = [&](int item, int x, int y, Coord dim) {
            const int cx = 2 * x + dim.x, cy = 2 * y + dim.y;
            const int dist = octagonal(cx - hx, cy - hy);
            const int along = ((cx - hx) * approach.x + (cy - hy) * approach.y) / alen;
            int nearest = INT_MAX;
            for(const Placement& p : placed) if(p.item == Structure_Refinery) {
                const Coord d = catalogue.sizes[p.item];
                nearest = std::min(nearest, octagonal(cx - 2 * p.x - d.x, cy - 2 * p.y - d.y));
            }
            int s = 0;
            switch(item) {
                case Structure_ConstructionYard: s = dist; break;
                case Structure_Refinery:
                    s = 3 * dist + (spice.x >= 0 ? octagonal(cx - 2 * spice.x, cy - 2 * spice.y) : -along); break;
                case Structure_Silo:         s = 3 * dist + (nearest == INT_MAX ? 0 : 2 * nearest); break;
                case Structure_WindTrap:     s = 3 * dist + along; break;
                case Structure_NuclearPlant: s = 3 * dist + 2 * along; break;
                case Structure_LightFactory: case Structure_HeavyFactory: case Structure_HighTechFactory:
                case Structure_Barracks: case Structure_WOR: case Structure_RepairYard:
                    s = 3 * dist - along; break;
                default: s = 3 * dist; break;
            }
            // Seeded jitter of up to two tiles keeps the packing irregular.
            return s + 3 * static_cast<int>(mix(seed, static_cast<uint32_t>((y * size + x) * 64 + item)) % 5);
        };
        auto blastSpaced = [&](int item, int x, int y, Coord dim) {
            const auto critical = TacticalSafetyPolicy::protectedReactorNeighbour;
            if(!critical(item)) return true;
            for(const Placement& p : placed) {
                if(!((item == Structure_NuclearPlant && critical(p.item)) || (p.item == Structure_NuclearPlant && critical(item)))) continue;
                const Coord d = catalogue.sizes[p.item];
                if(footprintGap(x, y, dim, p.x, p.y, d) + 1 < kBlastGap) return false;
            }
            return true;
        };
        for(int item : buildings) {
            const Coord dim = catalogue.sizes[item];
            int bestScore = INT_MAX, bestX = 0, bestY = 0;
            for(bool spaced : {true, false}) {
                for(int y = region.y; y + dim.y <= region.y + region.h; ++y)
                    for(int x = region.x; x + dim.x <= region.x + region.w; ++x) {
                        if(!fits(region, planIndex, x, y, dim) || (spaced && !blastSpaced(item, x, y, dim))) continue;
                        const int s = score(item, x, y, dim);
                        if(s < bestScore) { bestScore = s; bestX = x; bestY = y; }
                    }
                if(bestScore != INT_MAX) break;
            }
            if(bestScore == INT_MAX) { ++plan.setup.skipped; continue; }
            build(house, item, bestX, bestY, true);
            placed.push_back(result.structures.back());
            if(item == Structure_ConstructionYard) { hx = 2 * bestX + dim.x; hy = 2 * bestY + dim.y; }
        }
        layoutTurrets(plan, planIndex, region, turrets, Coord(hx / 2, hy / 2), approach, seed);
    }

    void layoutTurrets(Plan& plan, int planIndex, const Rect& region, const std::vector<int>& turrets,
                       Coord heart, Coord approach, uint32_t seed) {
        if(turrets.empty()) return;
        const int house = plan.setup.house;
        // Chebyshev distance from every tile to the nearest building, up to five.
        std::vector<int> reach(size * size, INT_MAX);
        std::deque<int> queue;
        for(int i = 0; i < size * size; ++i) if(occupied[i] == Building) { reach[i] = 0; queue.push_back(i); }
        while(!queue.empty()) {
            const int at = queue.front(); queue.pop_front();
            if(reach[at] >= 5) continue;
            for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx) {
                const int nx = at % size + dx, ny = at / size + dy;
                if(!inside(nx, ny) || reach[ny * size + nx] <= reach[at] + 1) continue;
                reach[ny * size + nx] = reach[at] + 1;
                queue.push_back(ny * size + nx);
            }
        }
        const int alen = std::max(1, octagonal(approach.x, approach.y));
        const Coord across = std::abs(approach.x) >= std::abs(approach.y) ? Coord(0, 1) : Coord(1, 0);
        std::vector<Coord> placedTurrets;
        auto freeRock = [&](int x, int y) {
            return region.contains(x, y) && inside(x, y) && isBuildable(map(x, y)) && at(x, y) == Free && ownable(x, y, planIndex);
        };
        for(int item : turrets) {
            int bestScore = INT_MAX;
            Coord best(-1, -1);
            // Just outside the buildings and three apart; a crowded base relaxes both.
            for(const auto [farthest, apart] : {std::pair<int, int>{3, 3}, {5, 2}}) {
                if(best.x >= 0) break;
                for(int y = region.y; y < region.y + region.h; ++y) for(int x = region.x; x < region.x + region.w; ++x) {
                    if(!inside(x, y) || reach[y * size + x] < 2 || reach[y * size + x] > farthest
                       || !fits(region, planIndex, x, y, Coord(1, 1))) continue;
                    int spacing = 8;
                    for(const Coord& t : placedTurrets) spacing = std::min(spacing, std::max(std::abs(t.x - x), std::abs(t.y - y)));
                    if(spacing < apart) continue;
                    const int along = ((x - heart.x) * approach.x + (y - heart.y) * approach.y) / alen;
                    const int s = -4 * along - 3 * spacing + static_cast<int>(mix(seed, static_cast<uint32_t>(y * size + x)) % 5);
                    if(s < bestScore) { bestScore = s; best = Coord(x, y); }
                }
            }
            if(best.x < 0) { ++plan.setup.skipped; continue; }
            build(house, item, best.x, best.y, true);
            placedTurrets.push_back(best);
            for(int side : {-1, 1}) for(int w = 1; w <= plan.wallsPerSide; ++w) {
                const int x = best.x + side * w * across.x, y = best.y + side * w * across.y;
                if(!freeRock(x, y) || reach[y * size + x] < 2) break;
                at(x, y) = WallTile;
                tile(house, Structure_Wall, x, y);
            }
        }
    }

    /// Ground units on the nearest open tiles to @a from, never touching a building, a road or
    /// another house's start: one vehicle per tile, foot soldiers bundled up to kInfantryPerTile.
    void layoutUnits(Plan& plan, int planIndex, Coord from, const std::vector<int>& items) {
        if(items.empty()) return;
        const int house = plan.setup.house;
        // Each slot is one tile: a vehicle, or a squad of up to five foot soldiers (in army order).
        std::vector<std::vector<int>> slots;
        int openSquad = -1;
        for(int item : items) {
            if(isFootUnit(item)) {
                if(openSquad < 0 || static_cast<int>(slots[openSquad].size()) >= kInfantryPerTile) {
                    openSquad = static_cast<int>(slots.size());
                    slots.emplace_back();
                }
                slots[openSquad].push_back(item);
            } else {
                slots.push_back({item});
            }
        }
        std::vector<char> seen(size * size, 0);
        std::deque<Coord> queue{from};
        seen[from.y * size + from.x] = 1;
        size_t next = 0;
        int placed = 0;
        auto clear = [&](int x, int y) {
            if(at(x, y) != Free || map(x, y) == Terrain_Mountain || isSpiceBloomTerrain(map(x, y)) || !ownable(x, y, planIndex)) return false;
            for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx)
                if(inside(x + dx, y + dy) && (at(x + dx, y + dy) == Building || at(x + dx, y + dy) == WallTile
                                              || at(x + dx, y + dy) == City)) return false;
            return true;
        };
        while(!queue.empty() && next < slots.size()) {
            const Coord c = queue.front(); queue.pop_front();
            if(clear(c.x, c.y)) {
                at(c.x, c.y) = UnitTile;
                for(int item : slots[next]) {
                    Placement p; p.house = house; p.item = item; p.x = c.x; p.y = c.y;
                    result.units.push_back(p);
                    ++placed;
                }
                ++next;
            }
            for(const Coord& d : {Coord(1,0), Coord(0,1), Coord(-1,0), Coord(0,-1)}) {
                const int nx = c.x + d.x, ny = c.y + d.y;
                if(!inside(nx, ny) || seen[ny * size + nx] || map(nx, ny) == Terrain_Mountain) continue;
                seen[ny * size + nx] = 1;
                queue.emplace_back(nx, ny);
            }
        }
        plan.setup.skipped += static_cast<int>(items.size()) - placed;
    }
};

/// Nearest spice tile to @a from by ground, or (-1,-1).
Coord nearestSpice(const MapData& map, Coord from) {
    const int size = map.getSizeX();
    std::vector<char> seen(size * size, 0);
    std::deque<Coord> queue{from};
    seen[from.y * size + from.x] = 1;
    while(!queue.empty()) {
        const Coord c = queue.front(); queue.pop_front();
        if(isSpiceTerrain(map(c.x, c.y))) return c;
        for(const Coord& d : {Coord(1,0), Coord(0,1), Coord(-1,0), Coord(0,-1)}) {
            const int nx = c.x + d.x, ny = c.y + d.y;
            if(!map.isInsideMap(nx, ny) || seen[ny * size + nx] || map(nx, ny) == Terrain_Mountain) continue;
            seen[ny * size + nx] = 1;
            queue.emplace_back(nx, ny);
        }
    }
    return Coord(-1, -1);
}

std::string writeIni(const Result& result, const Params& params, const Catalogue& catalogue) {
    std::string out;
    auto line = [&](const std::string& text) { out += text; out += '\n'; };
    const int size = result.map.getSizeX();
    auto pos = [&](const Placement& p) { return std::to_string(p.y * size + p.x); };
    auto house = [&](int h) { return catalogue.houseNames[h]; };

    line("; Random Skirmish map generated by Dune City (seed " + std::to_string(params.seed) + ").");
    line("");
    line("[BASIC]");
    line("Version=2");
    line("MapVersion=1");
    line("Name=" + result.name);
    line("Author=Random Skirmish");
    line("TechLevel=" + std::to_string(params.techLevel));
    line("WinFlags=3");
    line("LoseFlags=1");
    line("LosePicture=LOSTVEHC.WSA");
    line("WinPicture=WIN2.WSA");
    line("BriefPicture=SARDUKAR.WSA");
    line("TimeOut=0");
    // The map's content category: city zones make it a DuneCity map; otherwise the chosen mod's
    // category (Tornie or plain vanilla content).
    line(std::string("Mod=") + (params.mode == Mode::DuneCity ? std::string(MapMetadata::ModDuneCity)
                                                              : MapMetadata::canonicalCategory(params.mod)));
    line("");
    line("[MAP]");
    line("SizeX=" + std::to_string(size));
    line("SizeY=" + std::to_string(size));
    for(int y = 0; y < size; ++y) {
        char key[8];
        std::snprintf(key, sizeof(key), "%.3d", y);
        std::string row;
        for(int x = 0; x < size; ++x) row += terrainChar(result.map(x, y));
        line(std::string(key) + "=" + row);
    }
    line("");
    line("[CHOAM]");
    for(const char* entry : {"Carryall=2", "Harvester=4", "Launcher=5", "MCV=2", "'Thopter=5", "Quad=5",
                             "Siege Tank=6", "Tank=6", "Trike=5"}) line(entry);
    for(const HouseSetup& h : result.houses) {
        line("");
        line("[" + house(h.house) + "]");
        line("Credits=" + std::to_string(params.credits));
    }
    line("");
    line("[STRUCTURES]");
    for(const Placement& p : result.tiles)
        line("GEN" + pos(p) + "=" + house(p.house) + "," + catalogue.itemNames[p.item]);
    int id = 0;
    for(const Placement& p : result.structures) {
        char key[16];
        std::snprintf(key, sizeof(key), "ID%04d", ++id);
        line(std::string(key) + "=" + house(p.house) + "," + catalogue.itemNames[p.item] + ",256," + pos(p)
             + (isZone(p.item) && p.level >= 0 ? ZoneStartLevel::format(p.level) : ""));
    }
    line("");
    line("[UNITS]");
    id = 0;
    for(const Placement& p : result.units) {
        char key[16];
        std::snprintf(key, sizeof(key), "ID%04d", ++id);
        line(std::string(key) + "=" + house(p.house) + "," + catalogue.itemNames[p.item] + ",256," + pos(p) + ",64,Area Guard");
    }
    line("");
    // Provenance only; the loader ignores this section.
    line("[RANDOMSKIRMISH]");
    line("Seed=" + std::to_string(params.seed));
    line("MapSeed=" + std::to_string(result.mapSeed));
    line("Rock=" + std::to_string(kRockPercent[indexOf(params.rock)]));
    line("Spice=" + std::to_string(indexOf(params.spice)));
    line(std::string("Symmetric=") + (params.symmetric ? "true" : "false"));
    // A multiplayer map's starts are open: who plays them, and on which team, is the lobby's choice.
    if(params.multiplayer) line("Starts=" + std::to_string(result.houses.size()));
    else line(std::string("EnemiesAllied=") + (params.enemiesAllied ? "true" : "false"));
    for(const HouseSetup& h : result.houses) {
        const char* role = h.role == Role::Human ? "Human" : h.role == Role::Ally ? "Ally" : h.role == Role::Open ? "Open" : "Enemy";
        line(house(h.house) + "=" + role
             + (h.role == Role::Open ? std::string() : ",Team" + std::to_string(h.team) + "," + h.playerClass)
             + (h.partnerClass.empty() ? "" : "+" + h.partnerClass) + "," + baseName(h.base)
             + ",Army" + std::to_string(h.armySpent) + "/" + std::to_string(h.armyBudget)
             + ",City" + std::to_string(h.cityEstimate) + "/" + std::to_string(h.cityTarget)
             + (h.skipped ? ",Skipped" + std::to_string(h.skipped) : ""));
    }
    return out;
}

} // namespace

// ---- Params -------------------------------------------------------------------------------

void Params::clamp(int houseCount) {
    if(std::find(kMapSizes.begin(), kMapSizes.end(), mapSize) == kMapSizes.end())
        mapSize = snapTo(mapSize, kMapSizes.data(), kMapSizes.data() + kMapSizes.size());
    rock = static_cast<Rock>(std::clamp(indexOf(rock), 0, indexOf(Rock::All)));
    spice = static_cast<Spice>(std::clamp(indexOf(spice), 0, indexOf(Spice::VeryHigh)));
    mode = static_cast<Mode>(std::clamp(indexOf(mode), 0, indexOf(Mode::DuneCity)));
    credits = snapTo(credits, kCreditPresets.data(), kCreditPresets.data() + kCreditPresets.size());
    techLevel = std::clamp(techLevel, 1, 9);
    for(Side* side : {&allySide, &enemySide, &playerSide, &startSide}) {
        side->base = static_cast<BaseSize>(std::clamp(indexOf(side->base), 0, indexOf(BaseSize::Fortress)));
        side->army = std::clamp(side->army, 0, kArmyRandom);
        side->city = std::clamp(side->city, 0, kCityRandom);
        if(side->playerClass.empty() && side != &playerSide && side != &startSide) side->playerClass = "qBotMedium";
    }
    startSide.playerClass.clear();     // a start's controller is chosen in the lobby
    // Watching is a single-player choice; a hosted map's players and observers are the lobby's.
    if(multiplayer) spectate = false;
    if(spectateClass.empty()) spectateClass = "qBotMedium";
    allies = std::clamp(allies, 0, kMaxAllies);
    enemies = std::clamp(enemies, kMinEnemies, kMaxEnemies);
    houseCount = std::max(2, houseCount);
    starts = std::clamp(starts, kMinStarts, std::max(kMinStarts, maxStarts(houseCount)));
    const int others = maxOtherHouses(houseCount);
    if(allies + enemies > others) enemies = std::max(kMinEnemies, others - allies);
    if(allies + enemies > others) allies = std::max(0, others - enemies);
}

namespace {
constexpr const char* kSection = "Random Skirmish";
}

void Params::load(const INIFile& config) {
    mapSize = config.getIntValue(kSection, "Map Size", mapSize);
    rock = static_cast<Rock>(config.getIntValue(kSection, "Rock", indexOf(rock)));
    spice = static_cast<Spice>(config.getIntValue(kSection, "Spice", indexOf(spice)));
    // The multiplayer screen keeps its own layout choice, mirrored unless the host changed it.
    symmetric = config.getBoolValue(kSection, multiplayer ? "Multiplayer Symmetric" : "Symmetric", symmetric);
    const std::string seedText = config.getStringValue(kSection, "Seed", std::to_string(seed));
    if(!seedText.empty() && seedText.find_first_not_of("0123456789") == std::string::npos && seedText.size() <= 10) {
        const unsigned long long value = std::stoull(seedText);
        if(value <= 0xFFFFFFFFull) seed = static_cast<uint32_t>(value);
    }
    fixedSeed = config.getBoolValue(kSection, "Fixed Seed", fixedSeed);
    house = config.getIntValue(kSection, "House", house);
    credits = config.getIntValue(kSection, "Credits", credits);
    allies = config.getIntValue(kSection, "Allies", allies);
    enemies = config.getIntValue(kSection, "Enemies", enemies);
    enemiesAllied = config.getBoolValue(kSection, "Enemies Allied", enemiesAllied);
    if(!multiplayer) {
        // A hosted game plays the host's mod; only the single-player screen remembers one.
        mode = static_cast<Mode>(config.getIntValue(kSection, "Mode", indexOf(mode)));
        // Before the mod choice existed the mode named the mod: DuneCity or vanilla.
        mod = config.getStringValue(kSection, "Mod", mode == Mode::DuneCity ? "dunecity" : "vanilla");
        spectate = config.getBoolValue(kSection, "Spectate", spectate);
        spectateClass = config.getStringValue(kSection, "Spectate AI", spectateClass);
    }
    techLevel = config.getIntValue(kSection, "Tech Level", techLevel);
    for(auto [side, prefix] : {std::pair<Side*, std::string>{&allySide, "Ally "}, {&enemySide, "Enemy "}, {&playerSide, "Player "}}) {
        side->playerClass = config.getStringValue(kSection, prefix + "AI", side->playerClass);
        side->base = static_cast<BaseSize>(config.getIntValue(kSection, prefix + "Base", indexOf(side->base)));
        side->army = config.getIntValue(kSection, prefix + "Army", side->army);
        side->city = config.getIntValue(kSection, prefix + "City", side->city);
    }
    starts = config.getIntValue(kSection, "Multiplayer Starts", starts);
    startSide.base = static_cast<BaseSize>(config.getIntValue(kSection, "Start Base", indexOf(startSide.base)));
    startSide.army = config.getIntValue(kSection, "Start Army", startSide.army);
    startSide.city = config.getIntValue(kSection, "Start City", startSide.city);
}

void Params::save(INIFile& config) const {
    config.setIntValue(kSection, "Map Size", mapSize);
    config.setIntValue(kSection, "Rock", indexOf(rock));
    config.setIntValue(kSection, "Spice", indexOf(spice));
    config.setBoolValue(kSection, multiplayer ? "Multiplayer Symmetric" : "Symmetric", symmetric);
    config.setStringValue(kSection, "Seed", std::to_string(seed));
    config.setBoolValue(kSection, "Fixed Seed", fixedSeed);
    config.setIntValue(kSection, "House", house);
    config.setIntValue(kSection, "Credits", credits);
    config.setIntValue(kSection, "Allies", allies);
    config.setIntValue(kSection, "Enemies", enemies);
    config.setBoolValue(kSection, "Enemies Allied", enemiesAllied);
    if(!multiplayer) {
        config.setIntValue(kSection, "Mode", indexOf(mode));
        config.setStringValue(kSection, "Mod", mod);
        config.setBoolValue(kSection, "Spectate", spectate);
        config.setStringValue(kSection, "Spectate AI", spectateClass);
    }
    config.setIntValue(kSection, "Tech Level", techLevel);
    for(auto [side, prefix] : {std::pair<const Side*, std::string>{&allySide, "Ally "}, {&enemySide, "Enemy "}, {&playerSide, "Player "}}) {
        config.setStringValue(kSection, prefix + "AI", side->playerClass);
        config.setIntValue(kSection, prefix + "Base", indexOf(side->base));
        config.setIntValue(kSection, prefix + "Army", side->army);
        config.setIntValue(kSection, prefix + "City", side->city);
    }
    config.setIntValue(kSection, "Multiplayer Starts", starts);
    config.setIntValue(kSection, "Start Base", indexOf(startSide.base));
    config.setIntValue(kSection, "Start Army", startSide.army);
    config.setIntValue(kSection, "Start City", startSide.city);
}

// ---- Catalogue ----------------------------------------------------------------------------

int Catalogue::unlockLevel(int item, int house) const {
    // Iterative depth bound: builder/prerequisite chains are a handful deep; a malformed
    // mod with a cycle simply never unlocks.
    struct Walker {
        const Catalogue& c;
        int operator()(int id, int h, int depth) const {
            if(id <= ItemID_Invalid || id >= Num_ItemID || depth > 8) return -1;
            const ItemRule& r = c.rule(id, h);
            if(id == Structure_ConstructionYard) return r.enabled ? 0 : -1;
            if(!r.enabled || r.techLevel < 0 || r.builder == ItemID_Invalid) return -1;
            int level = r.techLevel;
            if(isStructure(r.builder)) {
                const int builder = (*this)(r.builder, h, depth + 1);
                if(builder < 0) return -1;
                level = std::max(level, builder);
            }
            for(int prerequisite : r.prerequisites) {
                const int needed = (*this)(prerequisite, h, depth + 1);
                if(needed < 0) return -1;
                level = std::max(level, needed);
            }
            return level;
        }
    };
    if(house < 0 || house >= NUM_HOUSES) return -1;
    return Walker{*this}(item, house, 0);
}

bool Catalogue::available(int item, int house, int techLevel) const {
    const int level = unlockLevel(item, house);
    return level >= 0 && level <= techLevel;
}

Catalogue startCatalogue(const Catalogue& catalogue, int starts) {
    Catalogue shared;
    shared.itemNames = catalogue.itemNames;
    shared.sizes = catalogue.sizes;
    // A start becomes a house slot; the Natives slot (HOUSE_NATIVES) is never a start.
    starts = std::clamp(starts, 0, NUM_PLAYER_HOUSES);
    for(int i = 0; i < starts; ++i) {
        shared.houses.push_back(i);
        shared.houseNames[i] = "Player" + std::to_string(i + 1);
    }
    for(int item = ItemID_Invalid + 1; item < Num_ItemID; ++item) {
        ItemRule merged;
        merged.enabled = !catalogue.houses.empty();
        int level = 0;
        bool first = true;
        for(int house : catalogue.houses) {
            const ItemRule& rule = catalogue.rule(item, house);
            const int unlock = catalogue.unlockLevel(item, house);
            merged.enabled = merged.enabled && rule.enabled;
            level = level < 0 || unlock < 0 ? -1 : std::max(level, unlock);
            // The dearest price and the least favourable power (largest draw, smallest output).
            merged.price = first ? rule.price : std::max(merged.price, rule.price);
            merged.power = first ? rule.power : std::max(merged.power, rule.power);
            first = false;
        }
        // Built straight from the Construction Yard at the level the slowest house needs, so
        // unlockLevel() of a start is exactly the highest unlock level among the houses.
        merged.techLevel = merged.enabled ? level : -1;
        merged.builder = item == Structure_ConstructionYard || level < 0 ? ItemID_Invalid : Structure_ConstructionYard;
        for(int house = 0; house < NUM_HOUSES; ++house) shared.rules[item][house] = merged;
    }
    return shared;
}

// ---- Building blocks ----------------------------------------------------------------------

std::vector<int> baseTemplate(BaseSize size, int house, int techLevel, const Catalogue& catalogue) {
    struct Entry { int first; int second; int count; };
    const int none = ItemID_Invalid;
    // Defences: every tier from Small up gets rocket turrets, falling back to gun turrets where
    // the house or tech level cannot build them yet.
    static const std::vector<Entry> tiers[] = {
        {},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 1}, {Structure_Radar, none, 1}},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 1}, {Structure_Silo, none, 1},
         {Structure_Radar, none, 1}, {Structure_Barracks, Structure_WOR, 1}, {Structure_LightFactory, none, 1},
         {Structure_RocketTurret, Structure_GunTurret, 1}},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 2}, {Structure_Silo, none, 1},
         {Structure_Radar, none, 1}, {Structure_Barracks, Structure_WOR, 1}, {Structure_LightFactory, none, 1},
         {Structure_HeavyFactory, none, 1}, {Structure_RepairYard, none, 1}, {Structure_GunTurret, none, 2},
         {Structure_RocketTurret, Structure_GunTurret, 2}},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 2}, {Structure_Silo, none, 2},
         {Structure_Radar, none, 1}, {Structure_Barracks, none, 1}, {Structure_WOR, none, 1},
         {Structure_LightFactory, none, 1}, {Structure_HeavyFactory, none, 1}, {Structure_HighTechFactory, none, 1},
         {Structure_RepairYard, none, 1}, {Structure_StarPort, none, 1}, {Structure_IX, none, 1},
         {Structure_GunTurret, none, 2}, {Structure_RocketTurret, Structure_GunTurret, 4}},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 3}, {Structure_Silo, none, 3},
         {Structure_Radar, none, 1}, {Structure_Barracks, none, 1}, {Structure_WOR, none, 1},
         {Structure_LightFactory, none, 1}, {Structure_HeavyFactory, none, 2}, {Structure_HighTechFactory, none, 1},
         {Structure_RepairYard, none, 1}, {Structure_StarPort, none, 1}, {Structure_IX, none, 1},
         {Structure_Palace, none, 1}, {Structure_GunTurret, none, 4}, {Structure_RocketTurret, Structure_GunTurret, 8}},
    };
    std::vector<int> items;
    for(const Entry& entry : tiers[indexOf(size)]) {
        int chosen = none;
        for(int candidate : {entry.first, entry.second})
            if(chosen == none && candidate != none && catalogue.available(candidate, house, techLevel)) chosen = candidate;
        for(int i = 0; chosen != none && i < entry.count; ++i) items.push_back(chosen);
    }
    return items;
}

int powerBalance(const std::vector<int>& items, int house, int extraDraw, const Catalogue& catalogue) {
    int balance = -extraDraw;
    for(int item : items) balance -= catalogue.rule(item, house).power;
    return balance;
}

void addPower(std::vector<int>& items, int house, int techLevel, int extraDraw, const Catalogue& catalogue) {
    int required = extraDraw;
    for(int item : items) required += std::max(0, catalogue.rule(item, house).power);
    // A tenth spare, the buffer the AI's own city-power rule keeps (MentatBuildOrder), so the
    // first growth does not black the city out.
    const int deficit = 1 + std::max(0, required) / 10 - powerBalance(items, house, extraDraw, catalogue);
    if(required <= 0 || deficit <= 0) return;
    const ItemRule& trap = catalogue.rule(Structure_WindTrap, house);
    const ItemRule& reactor = catalogue.rule(Structure_NuclearPlant, house);
    const int trapOutput = trap.enabled && trap.power < 0 ? -trap.power : 0;
    const int reactorOutput = reactor.power < 0 && catalogue.available(Structure_NuclearPlant, house, techLevel) ? -reactor.power : 0;
    int reactors = 0, traps = 0;
    if(reactorOutput > 0) {
        // Reactors carry the load; a few wind traps top up the remainder.
        reactors = deficit / reactorOutput;
        const int rest = deficit - reactors * reactorOutput;
        traps = trapOutput > 0 ? ceilDiv(rest, trapOutput) : 0;
        if(trapOutput > 0 ? traps > kMaxTopUpTraps : rest > 0) { ++reactors; traps = 0; }
        // One wind trap stays: the reactor's prerequisite, so the base can rebuild either.
        if(reactors > 0 && trapOutput > 0) traps = std::max(traps, 1);
    } else if(trapOutput > 0) {
        traps = ceilDiv(deficit, trapOutput);
    }
    items.insert(items.end(), reactors, Structure_NuclearPlant);
    items.insert(items.end(), traps, Structure_WindTrap);
}

std::vector<int> armyTypes(int house, int techLevel, const Catalogue& catalogue) {
    std::vector<int> types;
    for(int item : kUnitTypes)
        if(catalogue.rule(item, house).price > 0 && catalogue.available(item, house, techLevel)) types.push_back(item);
    std::stable_sort(types.begin(), types.end(), [&](int a, int b) {
        return catalogue.rule(a, house).price < catalogue.rule(b, house).price;
    });
    return types;
}

std::vector<int> buyArmy(int budget, int house, int techLevel, const Catalogue& catalogue, Random& random) {
    const std::vector<int> types = armyTypes(house, techLevel, catalogue);
    if(types.empty() || budget <= 0) return {};
    const int n = static_cast<int>(types.size());
    const int cap = budget * std::max(kArmyTypeCapPercent, ceilDiv(100, n)) / 100;
    std::vector<int> prices(n), counts(n, 0), limits(n);
    for(int i = 0; i < n; ++i) prices[i] = catalogue.rule(types[i], house).price;

    // Half the budget as seeded variety, the rest spent as fully as the caps allow.
    int spent = 0;
    while(spent < budget / 2) {
        std::vector<int> candidates;
        for(int i = 0; i < n; ++i)
            if(spent + prices[i] <= budget && (counts[i] + 1) * prices[i] <= cap) candidates.push_back(i);
        if(candidates.empty()) break;
        const int pick = candidates[random.rand(0, static_cast<int>(candidates.size()) - 1)];
        ++counts[pick];
        spent += prices[pick];
    }
    for(int i = 0; i < n; ++i) limits[i] = cap / prices[i] - counts[i];
    spent += fillExactly(budget - spent, prices, limits, counts);
    if(spent * 10 < budget * 9) {
        // The random half boxed the fill in; the plain optimum is the floor we promise.
        std::fill(counts.begin(), counts.end(), 0);
        for(int i = 0; i < n; ++i) limits[i] = cap / prices[i];
        fillExactly(budget, prices, limits, counts);
    }
    std::vector<int> army;
    for(int i = 0; i < n; ++i) army.insert(army.end(), counts[i], types[i]);
    for(int i = static_cast<int>(army.size()) - 1; i > 0; --i) std::swap(army[i], army[random.rand(0, i)]);
    return army;
}

bool groundConnected(const MapData& map, const std::vector<Coord>& points) {
    if(points.empty()) return true;
    const int w = map.getSizeX(), h = map.getSizeY();
    std::vector<char> seen(w * h, 0);
    std::deque<Coord> queue{points.front()};
    if(!map.isInsideMap(points.front().x, points.front().y) || map(points.front().x, points.front().y) == Terrain_Mountain) return false;
    seen[points.front().y * w + points.front().x] = 1;
    while(!queue.empty()) {
        const Coord c = queue.front(); queue.pop_front();
        for(const Coord& d : {Coord(1,0), Coord(0,1), Coord(-1,0), Coord(0,-1)}) {
            const int nx = c.x + d.x, ny = c.y + d.y;
            if(!map.isInsideMap(nx, ny) || seen[ny * w + nx] || map(nx, ny) == Terrain_Mountain) continue;
            seen[ny * w + nx] = 1;
            queue.emplace_back(nx, ny);
        }
    }
    return std::all_of(points.begin(), points.end(), [&](const Coord& p) {
        return map.isInsideMap(p.x, p.y) && seen[p.y * w + p.x];
    });
}

/// Every start reaches spice by ground within three times the distance its own field was
/// seeded at (addSpice: (w + h) / 4 + kRimMax + 6 toward the middle). That seed lands on rock
/// or behind a massif on some maps, and the start then has no field of its own at all (7P
/// FFA seed 44452: 91 tiles round a massif; its house refined 20 credits in 27 minutes).
bool spiceNearEveryStart(const MapData& map, const std::vector<Rect>& plateaus) {
    const int w = map.getSizeX(), h = map.getSizeY();
    for(const Rect& r : plateaus) {
        const int limit = 3 * ((r.w + r.h) / 4 + kRimMax + 6);
        const Coord start(r.x + r.w / 2, r.y + r.h / 2);
        std::vector<int> steps(w * h, -1);
        std::deque<Coord> queue{start};
        steps[start.y * w + start.x] = 0;
        bool found = false;
        while(!queue.empty() && !found) {
            const Coord c = queue.front(); queue.pop_front();
            if(isSpiceTerrain(map(c.x, c.y))) { found = true; break; }
            if(steps[c.y * w + c.x] >= limit) continue;
            for(const Coord& d : {Coord(1,0), Coord(0,1), Coord(-1,0), Coord(0,-1)}) {
                const int nx = c.x + d.x, ny = c.y + d.y;
                if(!map.isInsideMap(nx, ny) || steps[ny * w + nx] >= 0 || map(nx, ny) == Terrain_Mountain) continue;
                steps[ny * w + nx] = steps[c.y * w + c.x] + 1;
                queue.emplace_back(nx, ny);
            }
        }
        if(!found) return false;
    }
    return true;
}

int displayedPopulation(const std::vector<Placement>& placements, int house) {
    int population = 0;
    for(const Placement& p : placements)
        if(p.house == house) population += populationOf(p.item, p.level);
    return population * QuantBotCityPolicy::kPopDisplayMultiplier;
}

// ---- Generator ----------------------------------------------------------------------------

Result generate(const Params& requested, const Catalogue& houseCatalogue) {
    Params params = requested;
    params.clamp(static_cast<int>(houseCatalogue.houses.size()));
    const bool multiplayer = params.multiplayer;
    // Multiplayer starts are planned as Player1..PlayerN with what every house can field, so
    // every start holds the same base, army and city whichever house the lobby puts there.
    const std::optional<Catalogue> startRules = multiplayer
        ? std::optional<Catalogue>(startCatalogue(houseCatalogue, params.starts)) : std::nullopt;
    const Catalogue& catalogue = multiplayer ? *startRules : houseCatalogue;
    const int size = params.mapSize;
    Random random(params.seed);
    Result result;
    // Random army and city sizes are drawn once per map: every house set to Random gets the same.
    Random draws(mix(params.seed, 0x5A3D1u));
    const int randomArmy = draws.rand(kArmyRandomMin / 100, kArmyRandomMax / 100) * 100;
    const int randomCity = draws.rand(kCityRandomMin / 1000, kCityRandomMax / 1000) * 1000;

    // Houses: the player's choice (or a seeded one), then a seeded draw for everyone else.
    // A multiplayer map has open starts instead; the lobby decides who plays where.
    std::vector<int> pool = catalogue.houses;
    int human = params.house;
    int allies = 0, enemies = 0;
    if(!multiplayer) {
        if(std::find(pool.begin(), pool.end(), human) == pool.end())
            human = pool.empty() ? HOUSE_HARKONNEN : pool[random.rand(0, static_cast<int>(pool.size()) - 1)];
        pool.erase(std::remove(pool.begin(), pool.end(), human), pool.end());
        for(int i = static_cast<int>(pool.size()) - 1; i > 0; --i) std::swap(pool[i], pool[random.rand(0, i)]);
        allies = std::min(params.allies, static_cast<int>(pool.size()) - 1);
        enemies = std::min(params.enemies, static_cast<int>(pool.size()) - allies);
    }

    std::vector<Plan> plans;
    auto addPlan = [&](int house, Role role, int team, const Side* side) {
        Plan plan;
        plan.setup.house = house;
        plan.setup.role = role;
        plan.setup.team = team;
        // Multiplayer starts share one stream, so every start buys the same army.
        Random own(mix(params.seed, multiplayer ? 1u : static_cast<uint32_t>(house) + 1));
        // Every house, the player's included, starts with its side's base, army and city.
        const Side& start = role == Role::Human ? params.playerSide : *side;
        if(role == Role::Human) {
            plan.setup.playerClass = "HumanPlayer";
            plan.setup.partnerClass = start.playerClass;
        } else {
            plan.setup.playerClass = start.playerClass;
        }
        plan.setup.base = start.base;
        plan.setup.armyBudget = start.army == kArmyRandom ? randomArmy : kArmyBudgets[start.army];
        if(params.mode == Mode::DuneCity)
            plan.setup.cityTarget = start.city == kCityRandom ? randomCity : kCityTargets[start.city];
        plan.base = baseTemplate(start.base, house, params.techLevel, catalogue);
        plan.mcv = start.base == BaseSize::None && catalogue.rule(Unit_MCV, house).enabled;
        plan.wallsPerSide = start.base == BaseSize::Fortress ? 2 : start.base == BaseSize::Large ? 1 : 0;
        if(!catalogue.available(Structure_Wall, house, params.techLevel)) plan.wallsPerSide = 0;
        planCity(plan, params.techLevel, catalogue);
        plan.army = buyArmy(plan.setup.armyBudget, house, params.techLevel, catalogue, own);
        // Power for base and city together, all of it on the base side: reactors where the
        // house can build them, wind traps otherwise and as top-ups.
        addPower(plan.base, house, params.techLevel, cityPowerDraw(plan.lots, house, catalogue), catalogue);

        // Footprint: the base with slack, a turret ring and blast spacing, beside the city grid.
        int area = 0;
        bool turrets = false;
        for(int item : plan.base) {
            const Coord d = catalogue.sizes[item];
            area += (d.x + 1) * (d.y + 1) + (item == Structure_NuclearPlant ? 40 : 0);
            turrets = turrets || isTurret(item);
        }
        plan.baseSide = std::max(8, isqrt(area * 16 / 10) + 3 + (turrets ? 6 : 0));
        const int cityW = plan.blocksX ? kCityBlock * plan.blocksX + 1 : 0;
        const int cityH = plan.blocksY ? kCityBlock * plan.blocksY + 1 : 0;
        plan.width = plan.baseSide + cityW;
        plan.height = std::max(plan.baseSide, cityH);
        plans.push_back(std::move(plan));
    };
    if(multiplayer) {
        // Every start its own team for the layout (bases face the nearest other start).
        for(int i = 0; i < static_cast<int>(catalogue.houses.size()); ++i) addPlan(i, Role::Open, i + 1, &params.startSide);
    } else {
        addPlan(human, Role::Human, 1, nullptr);
        for(int i = 0; i < allies; ++i) addPlan(pool[i], Role::Ally, 1, &params.allySide);
        for(int i = 0; i < enemies; ++i)
            addPlan(pool[allies + i], Role::Enemy, params.enemiesAllied ? 2 : 2 + i, &params.enemySide);
    }
    const int houses = static_cast<int>(plans.size());

    // The player's start is never the cramped one: as much rock as the largest start.
    for(const Plan& plan : plans) {
        plans[0].width = std::max(plans[0].width, plan.width);
        plans[0].height = std::max(plans[0].height, plan.height);
    }
    // Start slots: mirrored corners/edges when symmetric, an evenly spaced ring otherwise.
    int maxW = 0, maxH = 0;
    for(const Plan& plan : plans) { maxW = std::max(maxW, plan.width); maxH = std::max(maxH, plan.height); }
    maxW = std::min(maxW + (maxW & 1), size - 2 * kMargin);
    maxH = std::min(maxH + (maxH & 1), size - 2 * kMargin);
    std::vector<Coord> slotCenters;
    MirrorMode mirror = MirrorModeNone;
    if(params.symmetric) {
        mirror = houses == 2 ? MirrorModePoint : MirrorModeBoth;
        for(Plan& plan : plans) { plan.width = maxW; plan.height = maxH; }
        const int left = kMargin, right = size - kMargin - maxW, midX = (size - maxW) / 2;
        const int top = kMargin, bottom = size - kMargin - maxH, midY = (size - maxH) / 2;
        const Coord ring[] = {{left, top}, {midX, top}, {right, top}, {right, midY},
                              {right, bottom}, {midX, bottom}, {left, bottom}, {left, midY}};
        for(int i = 0; i < houses; ++i) {
            const Coord tl = i == 8 ? Coord(midX, midY) : ring[houses >= 9 ? i : i * 8 / houses];
            slotCenters.emplace_back(tl.x + maxW / 2, tl.y + maxH / 2);
        }
    } else {
        const int x0 = kMargin + maxW / 2, y0 = kMargin + maxH / 2;
        const int lx = std::max(0, size - 1 - kMargin - maxW / 2 - x0), ly = std::max(0, size - 1 - kMargin - maxH / 2 - y0);
        const int perimeter = std::max(1, 2 * (lx + ly));
        const int offset = random.rand(0, std::max(0, perimeter / houses - 1));
        for(int i = 0; i < houses; ++i) {
            int s = (offset + i * perimeter / houses) % perimeter;
            Coord c;
            if(s < lx) c = Coord(x0 + s, y0);
            else if((s -= lx) < ly) c = Coord(x0 + lx, y0 + s);
            else if((s -= ly) < lx) c = Coord(x0 + lx - s, y0 + ly);
            else c = Coord(x0, y0 + ly - (s - lx));
            slotCenters.push_back(c);
        }
    }
    // The player takes slot 0, allies the slots nearest to it, enemies the rest; multiplayer
    // starts keep the slot order.
    std::vector<int> order(houses - 1);
    std::iota(order.begin(), order.end(), 1);
    auto dist2 = [&](int a, int b) {
        const int dx = slotCenters[a].x - slotCenters[b].x, dy = slotCenters[a].y - slotCenters[b].y;
        return dx * dx + dy * dy;
    };
    if(!multiplayer) std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return dist2(0, a) < dist2(0, b); });
    plans[0].slot = 0;
    for(int i = 1; i < houses; ++i) plans[i].slot = order[i - 1];

    TerrainSpec spec;
    spec.size = size;
    spec.rock = params.rock;
    spec.spice = params.spice;
    spec.mirror = mirror;
    spec.centers = slotCenters;
    std::vector<Rect> plateaus(houses);
    for(int i = 0; i < houses; ++i) {
        const Plan& plan = plans[i];
        const Coord c = slotCenters[plan.slot];
        Rect r{c.x - plan.width / 2, c.y - plan.height / 2, std::min(plan.width, size), std::min(plan.height, size)};
        r.x = std::clamp(r.x, 0, size - r.w);
        r.y = std::clamp(r.y, 0, size - r.h);
        plateaus[i] = r;
        plans[i].setup.plateau = r;
        plans[i].setup.start = c;
    }
    // Symmetric maps carve every mirror image of every start, so empty images are fair
    // expansion sites and the terrain stays exactly mirrored.
    if(params.symmetric) {
        const auto images = MapMirror::createMapMirror(mirror, size, size);
        for(const Rect& r : plateaus)
            for(int m = 0; m < images->getSize(); ++m) {
                const Coord c = images->getCoord(Coord(r.x, r.y), m, Coord(r.w, r.h));
                spec.cores.push_back(Rect{c.x, c.y, r.w, r.h});
            }
    } else {
        spec.cores = plateaus;
    }

    std::vector<Coord> starts;
    for(const Plan& plan : plans) starts.push_back(plan.setup.start);
    // The first ground-connected terrain whose every start has spice of its own; failing that,
    // the first ground-connected one, as before. A symmetric map gives every start the same
    // surroundings, so it is fair by construction and not checked.
    int fallback = -1;
    for(int attempt = 0; attempt < kMaxTerrainAttempts; ++attempt) {
        result.mapSeed = params.seed + static_cast<uint32_t>(attempt);
        result.attempts = attempt + 1;
        result.map = buildTerrain(spec, result.mapSeed);
        if(!groundConnected(result.map, starts)) continue;
        if(spec.spice == Spice::None || params.symmetric || spiceNearEveryStart(result.map, plateaus)) { fallback = -1; break; }
        if(fallback < 0) fallback = attempt;
    }
    if(fallback >= 0) {
        result.mapSeed = params.seed + static_cast<uint32_t>(fallback);
        result.map = buildTerrain(spec, result.mapSeed);
    }
    if(!groundConnected(result.map, starts)) {
        // Only mountains block ground units; levelling them always connects the starts.
        for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x)
            if(result.map(x, y) == Terrain_Mountain) result.map(x, y) = Terrain_Rock;
        result.flattenedMountains = true;
    }

    Layout layout(catalogue, result.map, result);
    // Each start owns its core, then two tiles around it where no other core lies: nobody else
    // builds or parks there.
    for(int margin : {0, 2}) for(int i = 0; i < houses; ++i) {
        const Rect& r = plateaus[i];
        for(int y = std::max(0, r.y - margin); y < std::min(size, r.y + r.h + margin); ++y)
            for(int x = std::max(0, r.x - margin); x < std::min(size, r.x + r.w + margin); ++x)
                if(layout.owner[y * size + x] < 0) layout.owner[y * size + x] = i;
    }
    for(int i = 0; i < houses; ++i) {
        Plan& plan = plans[i];
        const Rect& p = plateaus[i];
        const int cityW = plan.blocksX ? std::min(p.w, kCityBlock * plan.blocksX + 1) : 0;
        const int cityH = plan.blocksY ? std::min(p.h, kCityBlock * plan.blocksY + 1) : 0;
        // The city sits on the map-edge side of the start, behind the base, its Industrial
        // district facing the base.
        const bool cityLeft = plan.setup.start.x < size / 2;
        Rect base = p, city{0, 0, 0, 0};
        // The base may spread onto the rim, but not around the city.
        Rect region{p.x - kRimMax, p.y - kRimMax, p.w + 2 * kRimMax, p.h + 2 * kRimMax};
        if(cityW > 0) {
            city = Rect{cityLeft ? p.x : p.x + p.w - cityW, p.y + (p.h - cityH) / 2, cityW, cityH};
            base = Rect{cityLeft ? p.x + cityW : p.x, p.y, p.w - cityW, p.h};
            region = Rect{cityLeft ? base.x : base.x - kRimMax, base.y - kRimMax, base.w + kRimMax, base.h + 2 * kRimMax};
            layout.layoutCity(plan, i, city, !cityLeft);
        }
        const Coord heart(base.x + base.w / 2, base.y + base.h / 2);
        // The likely approach: toward the nearest start of another team (the map centre if none).
        Coord approach(size / 2 - heart.x, size / 2 - heart.y);
        int nearest = INT_MAX;
        for(const Plan& other : plans) {
            if(other.setup.team == plan.setup.team) continue;
            const int d = octagonal(other.setup.start.x - heart.x, other.setup.start.y - heart.y);
            if(d < nearest) { nearest = d; approach = Coord(other.setup.start.x - heart.x, other.setup.start.y - heart.y); }
        }
        if(approach.x == 0 && approach.y == 0) approach = Coord(1, 0);
        layout.layoutBase(plan, i, region, heart, approach, nearestSpice(result.map, heart),
                          mix(result.mapSeed, static_cast<uint32_t>(plan.setup.house) + 17u));
        std::vector<int> units = plan.army;
        if(plan.mcv) units.insert(units.begin(), Unit_MCV);
        // The army waits in front of the base, between it and the approach.
        Coord muster = heart;
        if(!plan.base.empty()) {
            const int reach = std::max(base.w, base.h) / 2 + 3, length = std::max(1, octagonal(approach.x, approach.y));
            muster = Coord(std::clamp(heart.x + approach.x * reach / length, 0, size - 1),
                           std::clamp(heart.y + approach.y * reach / length, 0, size - 1));
            if(result.map(muster.x, muster.y) == Terrain_Mountain) muster = heart;
        }
        layout.layoutUnits(plan, i, muster, units);
        // The army is what actually found a tile, at the prices the match uses.
        plan.setup.armySpent = 0;
        for(const Placement& u : result.units)
            if(u.house == plan.setup.house && u.item != Unit_MCV) plan.setup.armySpent += catalogue.rule(u.item, u.house).price;
        plan.setup.cityEstimate = params.mode == Mode::DuneCity
            ? displayedPopulation(result.structures, plan.setup.house) : 0;
        result.houses.push_back(plan.setup);
    }

    // Name: "<N>P - <S>x<S> - Random <a>v<e> <Mode> T<tech> <seed>", multiplayer "... Random <N> starts ...".
    const std::string teams = multiplayer ? std::to_string(houses) + " starts"
        : std::to_string(1 + allies) + "v" + std::to_string(enemies) + (params.enemiesAllied || enemies < 2 ? "" : " FFA");
    result.name = std::to_string(houses) + "P - " + std::to_string(size) + "x" + std::to_string(size)
        + " - Random " + teams + (params.mode == Mode::DuneCity ? " DuneCity" : " Vanilla")
        + " T" + std::to_string(params.techLevel) + " " + std::to_string(params.seed);
    result.ini = writeIni(result, params, catalogue);
    return result;
}

}
