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
#include <misc/Random.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <deque>
#include <numeric>

namespace SkirmishGenerator {

namespace {

constexpr int kUnitTypes[] = {
    Unit_Soldier, Unit_Trooper, Unit_Trike, Unit_RaiderTrike, Unit_Quad, Unit_Tank,
    Unit_Launcher, Unit_SiegeTank, Unit_SonicTank, Unit_Deviator, Unit_Devastator
};
constexpr int kSpiceFieldsPer128[] = {0, 2, 4, 6, 9};
constexpr int kSpiceBlobRadius[] = {0, 3, 4, 5, 7};
constexpr int kThickSpicePercent[] = {0, 10, 25, 40, 55};
constexpr int kMargin = 2;          ///< tiles kept between a plateau and the map edge
constexpr int kCityBlock = 5;       ///< 4x4 block of four 2x2 lots plus one road line
constexpr int kZonesPerPolice = 16;
constexpr int kMountainDepth = 4;   ///< rock this far from any other terrain becomes mountain

int ceilDiv(int a, int b) { return (a + b - 1) / b; }
int isqrt(int value) { int r = 0; while((r + 1) * (r + 1) <= value) ++r; return r; }

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

bool isBuildable(TERRAINTYPE t) { return t == Terrain_Rock || t == Terrain_Slab; }

bool isSpiceTerrain(TERRAINTYPE t) {
    return t == Terrain_Spice || t == Terrain_ThickSpice || t == Terrain_SpiceBloom || t == Terrain_SpecialBloom
        || t == Terrain_GreenSpice || t == Terrain_ThickGreenSpice || t == Terrain_GreenSpiceBloom
        || t == Terrain_RedSpice || t == Terrain_ThickRedSpice || t == Terrain_RedSpiceBloom;
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

/// Everything decided about one house before terrain exists.
struct Plan {
    HouseSetup setup;
    std::vector<int> base;          ///< buildings packed on the base side of the plateau
    std::vector<Placement> lots;    ///< city-grid contents (zones with level, police, windtraps)
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
/// R,C,R,I pattern that keeps jobs close to one per eight residents.
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

int zonePowerDraw(const std::vector<Placement>& lots, int house, const Catalogue& catalogue) {
    int draw = 0;
    for(const auto& lot : lots) {
        if(isZone(lot.item)) draw += DuneCity::getZonePower(lot.item, lot.level);
        else if(lot.item != Structure_WindTrap) draw += std::max(0, catalogue.rule(lot.item, house).power);
    }
    return draw;
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

    const int stations = catalogue.available(Structure_PoliceStation, house, techLevel)
        ? ceilDiv(static_cast<int>(zones.size()), kZonesPerPolice) : 0;
    int placedStations = 0;
    for(size_t i = 0; i < zones.size(); ++i) {
        if(i % kZonesPerPolice == 0 && placedStations < stations) {
            Placement police; police.house = house; police.item = Structure_PoliceStation;
            plan.lots.push_back(police);
            ++placedStations;
        }
        plan.lots.push_back(zones[i]);
    }
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
    std::vector<Rect> plateaus;
    std::vector<Coord> centers;     ///< plateau centres in ring order
};

void carve(MapData& map, const std::vector<Rect>& plateaus) {
    for(const Rect& r : plateaus)
        for(int y = std::max(0, r.y); y < std::min(map.getSizeY(), r.y + r.h); ++y)
            for(int x = std::max(0, r.x); x < std::min(map.getSizeX(), r.x + r.w); ++x)
                map(x, y) = Terrain_Rock;
}

int rockPercent(const MapData& map) {
    int rock = 0;
    for(int y = 0; y < map.getSizeY(); ++y)
        for(int x = 0; x < map.getSizeX(); ++x)
            if(map(x, y) == Terrain_Rock || map(x, y) == Terrain_Mountain) ++rock;
    return rock * 100 / (map.getSizeX() * map.getSizeY());
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

MapData buildTerrain(const TerrainSpec& spec, uint32_t mapSeed) {
    const int size = spec.size;
    const int area = size * size;
    const int spiceIndex = indexOf(spec.spice);
    const int spiceFields = spiceIndex == 0 ? 0 : std::max(1, area * kSpiceFieldsPer128[spiceIndex] / 16384);
    auto make = [&](int rockFields) {
        MapData map = generateRandomMap(size, size, static_cast<int>(mapSeed), rockFields, spiceFields, spec.mirror);
        carve(map, spec.plateaus);
        return map;
    };

    // Smallest rock-field count that reaches the requested rock share.
    const int target = kRockPercent[indexOf(spec.rock)];
    int lo = 0, hi = 64;
    if(rockPercent(make(hi)) < target) lo = hi;
    while(lo < hi) {
        const int mid = (lo + hi) / 2;
        if(rockPercent(make(mid)) >= target) hi = mid; else lo = mid + 1;
    }
    MapData map = make(lo);
    const auto mirror = MapMirror::createMapMirror(spec.mirror, size, size);
    auto onSand = [](TERRAINTYPE t) { return t == Terrain_Sand || t == Terrain_Dunes || t == Terrain_Spice; };

    if(spec.spice == Spice::None) {
        for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x)
            if(isSpiceTerrain(map(x, y))) map(x, y) = Terrain_Sand;
    } else {
        // A field between every pair of neighbouring starts, so expansion is contested.
        const int radius = std::max(2, kSpiceBlobRadius[spiceIndex] * size / 128);
        const size_t n = spec.centers.size();
        for(size_t i = 0; i < n && n > 1; ++i) {
            if(n == 2 && i == 1) break;
            const Coord a = spec.centers[i], b = spec.centers[(i + 1) % n];
            const Coord mid((a.x + b.x) / 2, (a.y + b.y) / 2);
            for(int dy = -radius; dy <= radius; ++dy) for(int dx = -radius; dx <= radius; ++dx) {
                const int d = std::abs(dx) + std::abs(dy);
                if(d > radius) continue;
                paintMirrored(map, *mirror, mid.x + dx, mid.y + dy,
                              d <= radius / 2 ? Terrain_ThickSpice : Terrain_Spice, onSand);
            }
        }
        // Thick spice share: interior spice tiles, chosen by a mirror-invariant hash.
        const int percent = kThickSpicePercent[spiceIndex];
        MapData before = map;
        for(int y = 1; y < size - 1; ++y) for(int x = 1; x < size - 1; ++x) {
            if(before(x, y) != Terrain_Spice) continue;
            bool interior = true;
            for(const Coord& d : {Coord(1,0), Coord(-1,0), Coord(0,1), Coord(0,-1)}) {
                const TERRAINTYPE t = before(x + d.x, y + d.y);
                interior = interior && (t == Terrain_Spice || t == Terrain_ThickSpice);
            }
            if(interior && static_cast<int>(mix(mapSeed, canonicalIndex(*mirror, size, x, y)) % 100) < percent)
                map(x, y) = Terrain_ThickSpice;
        }
    }

    // Mountain cores deep inside large rock fields, never on or next to a plateau.
    std::vector<int> depth(area, INT_MAX);
    std::deque<int> queue;
    for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x)
        if(map(x, y) != Terrain_Rock) { depth[y * size + x] = 0; queue.push_back(y * size + x); }
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
    for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) {
        if(depth[y * size + x] < kMountainDepth || depth[y * size + x] == INT_MAX) continue;
        bool nearPlateau = false;
        for(const Rect& r : spec.plateaus)
            nearPlateau = nearPlateau || Rect{r.x - 5, r.y - 5, r.w + 10, r.h + 10}.contains(x, y);
        if(!nearPlateau) map(x, y) = Terrain_Mountain;
    }
    return map;
}

// ---- Layout -------------------------------------------------------------------------------

enum Occupancy : char { Free = 0, Building = 1, City = 2, UnitTile = 3, WallTile = 4 };

struct Layout {
    const Catalogue& catalogue;
    const MapData& map;
    int size;
    std::vector<char> occupied;
    std::vector<int> owner;     ///< plateau owner (index into plans) or -1
    Result& result;

    Layout(const Catalogue& c, const MapData& m, Result& r)
     : catalogue(c), map(m), size(m.getSizeX()), occupied(size * size, Free), owner(size * size, -1), result(r) {}

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size; }
    char& at(int x, int y) { return occupied[y * size + x]; }

    void tile(int house, int item, int x, int y) {
        Placement p; p.house = house; p.item = item; p.x = x; p.y = y;
        result.tiles.push_back(p);
    }

    bool fits(const Rect& region, int x, int y, Coord dim) {
        for(int dy = -1; dy <= dim.y; ++dy) for(int dx = -1; dx <= dim.x; ++dx) {
            const int tx = x + dx, ty = y + dy;
            const bool footprint = dx >= 0 && dy >= 0 && dx < dim.x && dy < dim.y;
            if(footprint) {
                if(!region.contains(tx, ty) || !inside(tx, ty) || !isBuildable(map(tx, ty)) || at(tx, ty) != Free) return false;
            } else if(inside(tx, ty) && (at(tx, ty) == Building || at(tx, ty) == WallTile)) {
                return false;   // keep a one-tile lane around every building
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

    /// Nearest legal spot to @a anchor (top-left of a centred footprint), spiral order.
    bool place(int house, int item, const Rect& region, Coord anchor, bool concrete) {
        const Coord dim = catalogue.sizes[item];
        const int ax = anchor.x - dim.x / 2, ay = anchor.y - dim.y / 2;
        const int reach = std::max(region.w, region.h);
        for(int r = 0; r <= reach; ++r) {
            // Ring r around the anchor: top and bottom rows, then the left and right columns.
            for(int k = 0; k < 8 * r || (r == 0 && k == 0); ++k) {
                int dx, dy;
                if(r == 0) { dx = 0; dy = 0; }
                else if(k < 2 * r + 1) { dx = k - r; dy = -r; }
                else if(k < 4 * r + 2) { dx = k - (2 * r + 1) - r; dy = r; }
                else if(k < 6 * r + 1) { dx = -r; dy = k - (4 * r + 2) - r + 1; }
                else { dx = r; dy = k - (6 * r + 1) - r + 1; }
                if(fits(region, ax + dx, ay + dy, dim)) { build(house, item, ax + dx, ay + dy, concrete); return true; }
            }
        }
        return false;
    }

    void layoutCity(Plan& plan, const Rect& city) {
        const int house = plan.setup.house;
        for(int y = city.y; y < city.y + city.h; ++y) for(int x = city.x; x < city.x + city.w; ++x) {
            if(!inside(x, y)) continue;
            const bool road = (x - city.x) % kCityBlock == 0 || (y - city.y) % kCityBlock == 0;
            at(x, y) = City;
            if(road && isBuildable(map(x, y))) tile(house, Structure_Road, x, y);
        }
        size_t next = 0;
        for(int by = 0; by < plan.blocksY; ++by) for(int bx = 0; bx < plan.blocksX; ++bx) for(int q = 0; q < 4; ++q) {
            if(next >= plan.lots.size()) return;
            const int x = city.x + 1 + kCityBlock * bx + 2 * (q % 2);
            const int y = city.y + 1 + kCityBlock * by + 2 * (q / 2);
            bool ok = true;
            for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
                ok = ok && inside(x + dx, y + dy) && isBuildable(map(x + dx, y + dy));
            if(!ok) continue;
            const Placement& lot = plan.lots[next++];
            Placement p = lot; p.x = x; p.y = y;
            result.structures.push_back(p);
            for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx) at(x + dx, y + dy) = Building;
        }
        plan.setup.skipped += static_cast<int>(plan.lots.size() - next);
    }

    /// Turrets evenly spaced on a ring two tiles outside the packed base, walls beside them.
    void layoutTurrets(Plan& plan, const Rect& region, const std::vector<int>& turrets, Rect bounds) {
        const int house = plan.setup.house;
        if(turrets.empty()) return;
        const Rect ring{bounds.x - 2, bounds.y - 2, bounds.w + 4, bounds.h + 4};
        std::vector<Coord> path;
        for(int x = ring.x; x < ring.x + ring.w; ++x) path.emplace_back(x, ring.y);
        for(int y = ring.y + 1; y < ring.y + ring.h; ++y) path.emplace_back(ring.x + ring.w - 1, y);
        for(int x = ring.x + ring.w - 2; x >= ring.x; --x) path.emplace_back(x, ring.y + ring.h - 1);
        for(int y = ring.y + ring.h - 2; y > ring.y; --y) path.emplace_back(ring.x, y);
        const int n = static_cast<int>(path.size());
        auto freeRock = [&](const Coord& c) {
            return region.contains(c.x, c.y) && inside(c.x, c.y) && isBuildable(map(c.x, c.y)) && at(c.x, c.y) == Free;
        };
        for(size_t t = 0; t < turrets.size(); ++t) {
            const int start = static_cast<int>(t) * n / static_cast<int>(turrets.size());
            int chosen = -1;
            for(int k = 0; k < n / static_cast<int>(turrets.size()) && chosen < 0; ++k)
                if(freeRock(path[(start + k) % n])) chosen = (start + k) % n;
            if(chosen < 0) { ++plan.setup.skipped; continue; }
            build(house, turrets[t], path[chosen].x, path[chosen].y, true);
            for(int side : {-1, 1}) for(int w = 1; w <= plan.wallsPerSide; ++w) {
                const Coord c = path[((chosen + side * w) % n + n) % n];
                if(!freeRock(c)) break;
                at(c.x, c.y) = WallTile;
                tile(house, Structure_Wall, c.x, c.y);
            }
        }
    }

    void layoutBase(Plan& plan, const Rect& region) {
        const int house = plan.setup.house;
        const Coord anchor(region.x + region.w / 2, region.y + region.h / 2);
        std::vector<int> buildings, turrets;
        for(int item : plan.base)
            (item == Structure_GunTurret || item == Structure_RocketTurret ? turrets : buildings).push_back(item);
        // Construction Yard in the middle, then the largest footprints first.
        std::stable_sort(buildings.begin(), buildings.end(), [&](int a, int b) {
            if((a == Structure_ConstructionYard) != (b == Structure_ConstructionYard)) return a == Structure_ConstructionYard;
            const Coord da = catalogue.sizes[a], db = catalogue.sizes[b];
            return da.x * da.y > db.x * db.y;
        });
        Rect bounds{anchor.x, anchor.y, 1, 1};
        for(int item : buildings) {
            const size_t before = result.structures.size();
            if(!place(house, item, region, anchor, true)) { ++plan.setup.skipped; continue; }
            const Placement& p = result.structures[before];
            const Coord dim = catalogue.sizes[item];
            const int x0 = std::min(bounds.x, p.x), y0 = std::min(bounds.y, p.y);
            const int x1 = std::max(bounds.x + bounds.w, p.x + dim.x), y1 = std::max(bounds.y + bounds.h, p.y + dim.y);
            bounds = Rect{x0, y0, x1 - x0, y1 - y0};
        }
        layoutTurrets(plan, region, turrets, bounds);
    }

    /// Ground units on the nearest open tiles around the base, never touching a building,
    /// a road or another house's plateau.
    void layoutUnits(Plan& plan, int planIndex, Coord from, const std::vector<int>& items) {
        if(items.empty()) return;
        const int house = plan.setup.house;
        std::vector<char> seen(size * size, 0);
        std::deque<Coord> queue{from};
        seen[from.y * size + from.x] = 1;
        size_t next = 0;
        auto clear = [&](int x, int y) {
            if(at(x, y) != Free || map(x, y) == Terrain_Mountain) return false;
            if(owner[y * size + x] >= 0 && owner[y * size + x] != planIndex) return false;
            for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx)
                if(inside(x + dx, y + dy) && (at(x + dx, y + dy) == Building || at(x + dx, y + dy) == WallTile
                                              || at(x + dx, y + dy) == City)) return false;
            return true;
        };
        while(!queue.empty() && next < items.size()) {
            const Coord c = queue.front(); queue.pop_front();
            if(clear(c.x, c.y)) {
                at(c.x, c.y) = UnitTile;
                Placement p; p.house = house; p.item = items[next++]; p.x = c.x; p.y = c.y;
                result.units.push_back(p);
            }
            for(const Coord& d : {Coord(1,0), Coord(0,1), Coord(-1,0), Coord(0,-1)}) {
                const int nx = c.x + d.x, ny = c.y + d.y;
                if(!inside(nx, ny) || seen[ny * size + nx] || map(nx, ny) == Terrain_Mountain) continue;
                seen[ny * size + nx] = 1;
                queue.emplace_back(nx, ny);
            }
        }
        plan.setup.skipped += static_cast<int>(items.size() - next);
    }
};

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
    line(std::string("EnemiesAllied=") + (params.enemiesAllied ? "true" : "false"));
    for(const HouseSetup& h : result.houses) {
        line(house(h.house) + "=" + (h.role == Role::Human ? "Human" : h.role == Role::Ally ? "Ally" : "Enemy")
             + ",Team" + std::to_string(h.team) + "," + h.playerClass
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
    rock = static_cast<Rock>(std::clamp(indexOf(rock), 0, indexOf(Rock::VeryHigh)));
    spice = static_cast<Spice>(std::clamp(indexOf(spice), 0, indexOf(Spice::VeryHigh)));
    mode = static_cast<Mode>(std::clamp(indexOf(mode), 0, indexOf(Mode::DuneCity)));
    credits = snapTo(credits, kCreditPresets.data(), kCreditPresets.data() + kCreditPresets.size());
    techLevel = std::clamp(techLevel, 1, 9);
    for(Side* side : {&allySide, &enemySide, &playerSide}) {
        side->base = static_cast<BaseSize>(std::clamp(indexOf(side->base), 0, indexOf(BaseSize::Fortress)));
        side->army = std::clamp(side->army, 0, kArmyRandom);
        side->city = std::clamp(side->city, 0, kCityRandom);
        if(side->playerClass.empty() && side != &playerSide) side->playerClass = "qBotMedium";
    }
    allies = std::clamp(allies, 0, kMaxAllies);
    enemies = std::clamp(enemies, kMinEnemies, kMaxEnemies);
    houseCount = std::max(2, houseCount);
    if(1 + allies + enemies > houseCount) enemies = std::max(kMinEnemies, houseCount - 1 - allies);
    if(1 + allies + enemies > houseCount) allies = std::max(0, houseCount - 1 - enemies);
}

namespace {
constexpr const char* kSection = "Random Skirmish";
}

void Params::load(const INIFile& config) {
    mapSize = config.getIntValue(kSection, "Map Size", mapSize);
    rock = static_cast<Rock>(config.getIntValue(kSection, "Rock", indexOf(rock)));
    spice = static_cast<Spice>(config.getIntValue(kSection, "Spice", indexOf(spice)));
    symmetric = config.getBoolValue(kSection, "Symmetric", symmetric);
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
    mode = static_cast<Mode>(config.getIntValue(kSection, "Mode", indexOf(mode)));
    // Before the mod choice existed the mode named the mod: DuneCity or vanilla.
    mod = config.getStringValue(kSection, "Mod", mode == Mode::DuneCity ? "dunecity" : "vanilla");
    techLevel = config.getIntValue(kSection, "Tech Level", techLevel);
    for(auto [side, prefix] : {std::pair<Side*, std::string>{&allySide, "Ally "}, {&enemySide, "Enemy "}, {&playerSide, "Player "}}) {
        side->playerClass = config.getStringValue(kSection, prefix + "AI", side->playerClass);
        side->base = static_cast<BaseSize>(config.getIntValue(kSection, prefix + "Base", indexOf(side->base)));
        side->army = config.getIntValue(kSection, prefix + "Army", side->army);
        side->city = config.getIntValue(kSection, prefix + "City", side->city);
    }
}

void Params::save(INIFile& config) const {
    config.setIntValue(kSection, "Map Size", mapSize);
    config.setIntValue(kSection, "Rock", indexOf(rock));
    config.setIntValue(kSection, "Spice", indexOf(spice));
    config.setBoolValue(kSection, "Symmetric", symmetric);
    config.setStringValue(kSection, "Seed", std::to_string(seed));
    config.setBoolValue(kSection, "Fixed Seed", fixedSeed);
    config.setIntValue(kSection, "House", house);
    config.setIntValue(kSection, "Credits", credits);
    config.setIntValue(kSection, "Allies", allies);
    config.setIntValue(kSection, "Enemies", enemies);
    config.setBoolValue(kSection, "Enemies Allied", enemiesAllied);
    config.setIntValue(kSection, "Mode", indexOf(mode));
    config.setStringValue(kSection, "Mod", mod);
    config.setIntValue(kSection, "Tech Level", techLevel);
    for(auto [side, prefix] : {std::pair<const Side*, std::string>{&allySide, "Ally "}, {&enemySide, "Enemy "}, {&playerSide, "Player "}}) {
        config.setStringValue(kSection, prefix + "AI", side->playerClass);
        config.setIntValue(kSection, prefix + "Base", indexOf(side->base));
        config.setIntValue(kSection, prefix + "Army", side->army);
        config.setIntValue(kSection, prefix + "City", side->city);
    }
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

// ---- Building blocks ----------------------------------------------------------------------

std::vector<int> baseTemplate(BaseSize size, int house, int techLevel, const Catalogue& catalogue) {
    struct Entry { int first; int second; int count; };
    const int none = ItemID_Invalid;
    static const std::vector<Entry> tiers[] = {
        {},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 1}, {Structure_Radar, none, 1}},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 1}, {Structure_Silo, none, 1},
         {Structure_Radar, none, 1}, {Structure_Barracks, Structure_WOR, 1}, {Structure_LightFactory, none, 1}},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 2}, {Structure_Silo, none, 1},
         {Structure_Radar, none, 1}, {Structure_Barracks, Structure_WOR, 1}, {Structure_LightFactory, none, 1},
         {Structure_HeavyFactory, none, 1}, {Structure_RepairYard, none, 1}, {Structure_GunTurret, none, 2}},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 2}, {Structure_Silo, none, 2},
         {Structure_Radar, none, 1}, {Structure_Barracks, none, 1}, {Structure_WOR, none, 1},
         {Structure_LightFactory, none, 1}, {Structure_HeavyFactory, none, 1}, {Structure_HighTechFactory, none, 1},
         {Structure_RepairYard, none, 1}, {Structure_StarPort, none, 1}, {Structure_IX, none, 1},
         {Structure_GunTurret, none, 2}, {Structure_RocketTurret, none, 4}},
        {{Structure_ConstructionYard, none, 1}, {Structure_Refinery, none, 3}, {Structure_Silo, none, 3},
         {Structure_Radar, none, 1}, {Structure_Barracks, none, 1}, {Structure_WOR, none, 1},
         {Structure_LightFactory, none, 1}, {Structure_HeavyFactory, none, 2}, {Structure_HighTechFactory, none, 1},
         {Structure_RepairYard, none, 1}, {Structure_StarPort, none, 1}, {Structure_IX, none, 1},
         {Structure_Palace, none, 1}, {Structure_GunTurret, none, 4}, {Structure_RocketTurret, none, 8}},
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

void addPower(std::vector<int>& items, int house, int extraDraw, const Catalogue& catalogue) {
    const ItemRule& trap = catalogue.rule(Structure_WindTrap, house);
    if(!trap.enabled || trap.power >= 0) return;
    int required = extraDraw;
    for(int item : items) required += std::max(0, catalogue.rule(item, house).power);
    if(required <= 0) return;
    while(powerBalance(items, house, extraDraw, catalogue) <= 0) items.push_back(Structure_WindTrap);
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

int displayedPopulation(const std::vector<Placement>& placements, int house) {
    int population = 0;
    for(const Placement& p : placements)
        if(p.house == house) population += populationOf(p.item, p.level);
    return population * QuantBotCityPolicy::kPopDisplayMultiplier;
}

// ---- Generator ----------------------------------------------------------------------------

Result generate(const Params& requested, const Catalogue& catalogue) {
    Params params = requested;
    params.clamp(static_cast<int>(catalogue.houses.size()));
    const int size = params.mapSize;
    Random random(params.seed);
    Result result;

    // Houses: the player's choice (or a seeded one), then a seeded draw for everyone else.
    std::vector<int> pool = catalogue.houses;
    int human = params.house;
    if(std::find(pool.begin(), pool.end(), human) == pool.end())
        human = pool.empty() ? HOUSE_HARKONNEN : pool[random.rand(0, static_cast<int>(pool.size()) - 1)];
    pool.erase(std::remove(pool.begin(), pool.end(), human), pool.end());
    for(int i = static_cast<int>(pool.size()) - 1; i > 0; --i) std::swap(pool[i], pool[random.rand(0, i)]);
    const int allies = std::min(params.allies, static_cast<int>(pool.size()) - 1);
    const int enemies = std::min(params.enemies, static_cast<int>(pool.size()) - allies);

    std::vector<Plan> plans;
    auto addPlan = [&](int house, Role role, int team, const Side* side) {
        Plan plan;
        plan.setup.house = house;
        plan.setup.role = role;
        plan.setup.team = team;
        Random own(mix(params.seed, static_cast<uint32_t>(house) + 1));
        // Every house, the player's included, starts with its side's base, army and city.
        const Side& start = role == Role::Human ? params.playerSide : *side;
        if(role == Role::Human) {
            plan.setup.playerClass = "HumanPlayer";
            plan.setup.partnerClass = start.playerClass;
        } else {
            plan.setup.playerClass = start.playerClass;
        }
        plan.setup.base = start.base;
        plan.setup.armyBudget = start.army == kArmyRandom
            ? own.rand(kArmyRandomMin / 100, kArmyRandomMax / 100) * 100 : kArmyBudgets[start.army];
        if(params.mode == Mode::DuneCity)
            plan.setup.cityTarget = start.city == kCityRandom
                ? own.rand(kCityRandomMin / 1000, kCityRandomMax / 1000) * 1000 : kCityTargets[start.city];
        plan.base = baseTemplate(start.base, house, params.techLevel, catalogue);
        plan.mcv = start.base == BaseSize::None && catalogue.rule(Unit_MCV, house).enabled;
        plan.wallsPerSide = start.base == BaseSize::Fortress ? 2 : start.base == BaseSize::Large ? 1 : 0;
        if(!catalogue.available(Structure_Wall, house, params.techLevel)) plan.wallsPerSide = 0;
        planCity(plan, params.techLevel, catalogue);
        plan.army = buyArmy(plan.setup.armyBudget, house, params.techLevel, catalogue, own);
        // Power: the base's own Windtraps on the base side, the city's in its grid.
        std::vector<int> baseOnly = plan.base;
        addPower(baseOnly, house, 0, catalogue);
        std::vector<int> all = plan.base;
        addPower(all, house, zonePowerDraw(plan.lots, house, catalogue), catalogue);
        const int cityTraps = static_cast<int>(all.size() - baseOnly.size());
        plan.base = baseOnly;
        for(int i = 0; i < cityTraps; ++i) {
            Placement trap; trap.house = house; trap.item = Structure_WindTrap;
            plan.lots.push_back(trap);
        }

        // Footprint: packed base (with slack and a turret ring) beside the city grid.
        int area = 0;
        bool turrets = false;
        for(int item : plan.base) {
            const Coord d = catalogue.sizes[item];
            area += (d.x + 1) * (d.y + 1);
            turrets = turrets || item == Structure_GunTurret || item == Structure_RocketTurret;
        }
        plan.baseSide = std::max(8, isqrt(area * 16 / 10) + 3 + (turrets ? 6 : 0));
        if(!plan.lots.empty()) {
            const int blocks = ceilDiv(static_cast<int>(plan.lots.size()), 4);
            plan.blocksX = std::max(1, isqrt(blocks));
            if(plan.blocksX * plan.blocksX < blocks) ++plan.blocksX;
            plan.blocksY = ceilDiv(blocks, plan.blocksX);
        }
        const int cityW = plan.blocksX ? kCityBlock * plan.blocksX + 1 : 0;
        const int cityH = plan.blocksY ? kCityBlock * plan.blocksY + 1 : 0;
        plan.width = plan.baseSide + cityW;
        plan.height = std::max(plan.baseSide, cityH);
        plans.push_back(std::move(plan));
    };
    addPlan(human, Role::Human, 1, nullptr);
    for(int i = 0; i < allies; ++i) addPlan(pool[i], Role::Ally, 1, &params.allySide);
    for(int i = 0; i < enemies; ++i)
        addPlan(pool[allies + i], Role::Enemy, params.enemiesAllied ? 2 : 2 + i, &params.enemySide);
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
    // The player takes slot 0, allies the slots nearest to it, enemies the rest.
    std::vector<int> order(houses - 1);
    std::iota(order.begin(), order.end(), 1);
    auto dist2 = [&](int a, int b) {
        const int dx = slotCenters[a].x - slotCenters[b].x, dy = slotCenters[a].y - slotCenters[b].y;
        return dx * dx + dy * dy;
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return dist2(0, a) < dist2(0, b); });
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
                spec.plateaus.push_back(Rect{c.x, c.y, r.w, r.h});
            }
    } else {
        spec.plateaus = plateaus;
    }

    std::vector<Coord> starts;
    for(const Plan& plan : plans) starts.push_back(plan.setup.start);
    for(int attempt = 0; attempt < kMaxTerrainAttempts; ++attempt) {
        result.mapSeed = params.seed + static_cast<uint32_t>(attempt);
        result.attempts = attempt + 1;
        result.map = buildTerrain(spec, result.mapSeed);
        if(groundConnected(result.map, starts)) break;
    }
    if(!groundConnected(result.map, starts)) {
        // Only mountains block ground units; levelling them always connects the starts.
        for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x)
            if(result.map(x, y) == Terrain_Mountain) result.map(x, y) = Terrain_Rock;
        result.flattenedMountains = true;
    }

    Layout layout(catalogue, result.map, result);
    for(int i = 0; i < houses; ++i) {
        const Rect& r = plateaus[i];
        for(int y = r.y; y < r.y + r.h; ++y) for(int x = r.x; x < r.x + r.w; ++x)
            if(layout.owner[y * size + x] < 0) layout.owner[y * size + x] = i;
    }
    for(int i = 0; i < houses; ++i) {
        Plan& plan = plans[i];
        const Rect& p = plateaus[i];
        const int cityW = plan.blocksX ? std::min(p.w, kCityBlock * plan.blocksX + 1) : 0;
        const int cityH = plan.blocksY ? std::min(p.h, kCityBlock * plan.blocksY + 1) : 0;
        // The city sits on the map-edge side of the plateau, behind the base.
        const bool cityLeft = plan.setup.start.x < size / 2;
        Rect base = p, city{0, 0, 0, 0};
        if(cityW > 0) {
            city = Rect{cityLeft ? p.x : p.x + p.w - cityW, p.y + (p.h - cityH) / 2, cityW, cityH};
            base = Rect{cityLeft ? p.x + cityW : p.x, p.y, p.w - cityW, p.h};
            layout.layoutCity(plan, city);
        }
        layout.layoutBase(plan, base);
        std::vector<int> units = plan.army;
        if(plan.mcv) units.insert(units.begin(), Unit_MCV);
        layout.layoutUnits(plan, i, Coord(base.x + base.w / 2, base.y + base.h / 2), units);
        // The army is what actually found a tile, at the prices the match uses.
        plan.setup.armySpent = 0;
        for(const Placement& u : result.units)
            if(u.house == plan.setup.house && u.item != Unit_MCV) plan.setup.armySpent += catalogue.rule(u.item, u.house).price;
        plan.setup.cityEstimate = params.mode == Mode::DuneCity
            ? displayedPopulation(result.structures, plan.setup.house) : 0;
        result.houses.push_back(plan.setup);
    }

    // Name: "<N>P - <S>x<S> - Random <a>v<e> <Mode> T<tech> <seed>".
    const std::string teams = std::to_string(1 + allies) + "v" + std::to_string(enemies)
        + (params.enemiesAllied || enemies < 2 ? "" : " FFA");
    result.name = std::to_string(houses) + "P - " + std::to_string(size) + "x" + std::to_string(size)
        + " - Random " + teams + (params.mode == Mode::DuneCity ? " DuneCity" : " Vanilla")
        + " T" + std::to_string(params.techLevel) + " " + std::to_string(params.seed);
    result.ini = writeIni(result, params, catalogue);
    return result;
}

}
