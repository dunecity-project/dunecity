// Civilization Wars: pure generator acceptance against the shipped ObjectData rules.
#include <MapEditor/CivWarsGenerator.h>
#include <FileClasses/INIFile.h>
#include <INIMap/MapMetadata.h>
#include <INIMap/ZoneStartLevel.h>
#include <dunecity/CityConstants.h>
#include <dunecity/CityEffects.h>
#include <dunecity/ZonePower.h>

#include <catch2/catch_test_macros.hpp>
#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace {
using CivParams = CivWarsGenerator::Params;
using SkirmishGenerator::Catalogue;
using SkirmishGenerator::HouseSetup;
using SkirmishGenerator::Placement;
using SkirmishGenerator::Result;
using SkirmishGenerator::Role;
using SkirmishGenerator::Spice;

static_assert(Structure_ResearchLab == 88, "Research Lab is the shared item-88 contract, not a generator-local enum");
const std::array<uint32_t, 12> kSeeds{{0u, 1u, 2u, 3u, 7u, 19u, 42u, 12345u, 20261102u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}};

struct ItemSpec { int item; const char* section; const char* name; Coord size; };
const ItemSpec kKit[] = {
    {Structure_ConstructionYard, "Construction Yard", "Const Yard", {2, 2}},
    {Structure_ResearchLab, "Research Lab", "Research Lab", {3, 2}},
    {Structure_Refinery, "Refinery", "Refinery", {3, 2}},
    {Structure_WindTrap, "Windtrap", "Windtrap", {2, 2}},
    {Structure_RocketTurret, "Rocket-Turret", "R-Turret", {1, 1}},
    {Structure_PoliceStation, "Police Station", "Police Station", {2, 2}},
    {Structure_ZoneResidential, "Residential Zone", "Residential Zone", {2, 2}},
    {Structure_ZoneCommercial, "Commercial Zone", "Commercial Zone", {2, 2}},
    {Structure_ZoneIndustrial, "Industrial Zone", "Industrial Zone", {2, 2}},
    {Structure_Slab1, "Slab1", "Concrete", {1, 1}},
    {Structure_Road, "Road", "Road", {1, 1}},
    {Unit_Carryall, "Carryall", "Carryall", {1, 1}}
};

// Same source, house-letter override order, and fallback rules as ObjectData and
// the existing Random Skirmish tests. No guessed prices, power, or tech levels.
Catalogue shippedCatalogue() {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    INIFile ini(std::string(root ? root : ".") + "/config/ObjectData.ini.default");
    REQUIRE(ini.hasSection("Research Lab"));
    REQUIRE(ini.hasSection("Windtrap"));
    REQUIRE(ini.getIntValue("Rocket-Turret", "WeaponRange") == 8);
    const char* names[] = {"Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar", "Mercenary", "Neutral", "Rebels"};
    const char* letters = "HAOFSMNR";
    std::map<std::string, int> bySection;
    for(const ItemSpec& spec : kKit) bySection[spec.section] = spec.item;
    auto toId = [&](std::string name) {
        const auto begin = name.find_first_not_of(" \t");
        if(begin == std::string::npos) return static_cast<int>(ItemID_Invalid);
        name = name.substr(begin, name.find_last_not_of(" \t") - begin + 1);
        const auto found = bySection.find(name);
        return found == bySection.end() ? static_cast<int>(ItemID_Invalid) : found->second;
    };
    Catalogue c;
    for(int house = 0; house < 8; ++house) { c.houses.push_back(house); c.houseNames[house] = names[house]; }
    for(const ItemSpec& spec : kKit) {
        c.itemNames[spec.item] = spec.name;
        c.sizes[spec.item] = spec.size;
        for(int house : c.houses) {
            const std::string fallback = isStructure(spec.item) ? "default structure" : "default unit";
            auto value = [&](const std::string& key) {
                const std::string overrideKey = key + "(" + letters[house] + ")";
                if(ini.hasKey(spec.section, overrideKey)) return ini.getStringValue(spec.section, overrideKey);
                if(ini.hasKey(spec.section, key)) return ini.getStringValue(spec.section, key);
                return ini.getStringValue(fallback, key);
            };
            auto& rule = c.rules[spec.item][house];
            rule.enabled = value("Enabled") == "true";
            rule.price = std::atoi(value("Price").c_str());
            rule.power = std::atoi(value("Power").c_str());
            const std::string tech = value("TechLevel");
            rule.techLevel = tech.empty() ? -1 : std::atoi(tech.c_str());
            rule.builder = toId(value("Builder"));
            std::istringstream prerequisites(value("Prerequisite"));
            for(std::string name; std::getline(prerequisites, name, ',');) {
                const int item = toId(name);
                if(item != ItemID_Invalid) rule.prerequisites.push_back(item);
            }
        }
    }
    return c;
}

const Catalogue& rules() { static const Catalogue c = shippedCatalogue(); return c; }
int laneCentre(int height, int lane) { return (2 * lane + 1) * height / 6; }
int ownerSeat(int team, int lane) { return team == 1 ? 0 : team == 2 ? (lane == 2 ? 1 : 0) : lane; }

std::unique_ptr<INIFile> parse(const std::string& text) {
    SDL_RWops* stream = SDL_RWFromConstMem(text.data(), static_cast<int>(text.size()));
    REQUIRE(stream != nullptr);
    auto ini = std::make_unique<INIFile>(stream);
    SDL_RWclose(stream);
    return ini;
}

using PlacementKey = std::tuple<int, int, int, int, int>;
PlacementKey key(const Placement& p) { return {p.house, p.item, p.x, p.y, p.level}; }

struct Occupancy {
    int width, height;
    std::vector<int> building, road, foundation, unit;
    Occupancy(int w, int h) : width(w), height(h), building(w * h, -1), road(w * h, -1), foundation(w * h, -1), unit(w * h, -1) {}
    int index(int x, int y) const { return y * width + x; }
};

Occupancy requirePlacementAndMirror(const Result& r, const Catalogue& c, int teamSize) {
    const int width = r.map.getSizeX(), height = r.map.getSizeY();
    Occupancy occupied(width, height);
    std::array<int, NUM_HOUSES> opponent;
    opponent.fill(HOUSE_INVALID);
    for(int i = 0; i < teamSize; ++i) {
        const HouseSetup& left = r.houses[i];
        const HouseSetup& right = r.houses[teamSize + i];
        opponent[left.house] = right.house;
        opponent[right.house] = left.house;
        REQUIRE(left.start.x + right.start.x == width - 1);
        REQUIRE(left.start.y == right.start.y);
        REQUIRE(left.plateau.x + left.plateau.w + right.plateau.x == width);
        REQUIRE(left.plateau.y == right.plateau.y);
        REQUIRE(left.plateau.w == right.plateau.w);
        REQUIRE(left.plateau.h == right.plateau.h);
    }
    for(const auto* placements : {&r.structures, &r.tiles, &r.units}) {
        std::set<PlacementKey> entries;
        for(const Placement& p : *placements) REQUIRE(entries.insert(key(p)).second);
        for(const Placement& p : *placements) {
            INFO("item=" << p.item << " house=" << p.house << " x=" << p.x << " y=" << p.y);
            REQUIRE(opponent[p.house] != HOUSE_INVALID);
            const Coord size = c.sizes[p.item];
            REQUIRE(size.x > 0);
            REQUIRE(size.y > 0);
            REQUIRE(r.map.isInsideMap(p.x, p.y));
            REQUIRE(r.map.isInsideMap(p.x + size.x - 1, p.y + size.y - 1));
            REQUIRE(entries.count({opponent[p.house], p.item, width - p.x - size.x, p.y, p.level}) == 1);
        }
    }
    for(const Placement& p : r.structures) {
        const Coord size = c.sizes[p.item];
        for(int dy = 0; dy < size.y; ++dy) for(int dx = 0; dx < size.x; ++dx) {
            const int x = p.x + dx, y = p.y + dy, index = occupied.index(x, y);
            REQUIRE(r.map(x, y) == Terrain_Rock);
            REQUIRE(occupied.building[index] == -1);
            occupied.building[index] = p.house;
        }
        if(DuneCity::isCityZoneStructure(p.item)) REQUIRE(p.level == ZoneStartLevel::kMaxLevel);
        else REQUIRE(p.level == -1);
    }
    std::set<int> genKeys;
    for(const Placement& p : r.tiles) {
        const int index = occupied.index(p.x, p.y);
        REQUIRE(genKeys.insert(index).second);
        REQUIRE(r.map(p.x, p.y) == Terrain_Rock);
        if(p.item == Structure_Slab1) {
            REQUIRE(occupied.building[index] == p.house);
            occupied.foundation[index] = p.house;
        } else {
            REQUIRE(p.item == Structure_Road);
            REQUIRE(occupied.building[index] == -1);
            occupied.road[index] = p.house;
        }
    }
    for(int index = 0; index < width * height; ++index)
        if(occupied.building[index] != -1) REQUIRE(occupied.foundation[index] == occupied.building[index]);
    for(const Placement& p : r.units) {
        const int index = occupied.index(p.x, p.y);
        REQUIRE(p.item == Unit_Carryall);
        REQUIRE(occupied.building[index] == -1);
        REQUIRE(occupied.road[index] == -1);
        REQUIRE(occupied.unit[index] == -1);
        occupied.unit[index] = p.house;
    }
    // All rock behind each front is unbroken, including the back-route between
    // allies. Mountain walls exist ONLY in the central sand span.
    bool terrainMirror = true, endsRock = true;
    const int end = width / 4;
    for(int y = 0; y < height; ++y) for(int x = 0; x < width; ++x) {
        terrainMirror = terrainMirror && r.map(x, y) == r.map(width - 1 - x, y);
        if(x < end || x >= width - end) endsRock = endsRock && r.map(x, y) == Terrain_Rock;
    }
    REQUIRE(terrainMirror);
    REQUIRE(endsRock);
    return occupied;
}

// TrafficSimulation's first perimeter road, then a directed trip of at most
// twenty cardinal road steps to a complementary city-role structure.
int tripLength(const Result& r, const Catalogue& c, const Occupancy& occupied, const Placement& from, DuneCity::CityRole want) {
    std::vector<char> destination(occupied.width * occupied.height, 0);
    for(const Placement& p : r.structures) if(p.house == from.house && DuneCity::getStructureCityRole(p.item) == want) {
        const Coord size = c.sizes[p.item];
        for(int dy = 0; dy < size.y; ++dy) for(int dx = 0; dx < size.x; ++dx) destination[occupied.index(p.x + dx, p.y + dy)] = 1;
    }
    const Coord size = c.sizes[from.item];
    std::vector<Coord> ring;
    for(int dx = -1; dx <= size.x; ++dx) ring.emplace_back(from.x + dx, from.y - 1);
    for(int dx = -1; dx <= size.x; ++dx) ring.emplace_back(from.x + dx, from.y + size.y);
    for(int dy = 0; dy < size.y; ++dy) ring.emplace_back(from.x - 1, from.y + dy);
    for(int dy = 0; dy < size.y; ++dy) ring.emplace_back(from.x + size.x, from.y + dy);
    const auto start = std::find_if(ring.begin(), ring.end(), [&](Coord p) {
        return r.map.isInsideMap(p.x, p.y) && occupied.road[occupied.index(p.x, p.y)] == from.house;
    });
    if(start == ring.end()) return -1;
    std::vector<int> distance(occupied.width * occupied.height, -1);
    std::vector<Coord> queue{*start};
    distance[occupied.index(start->x, start->y)] = 0;
    for(std::size_t i = 0; i < queue.size(); ++i) {
        const Coord p = queue[i];
        const int steps = distance[occupied.index(p.x, p.y)];
        for(Coord d : {Coord(1, 0), Coord(-1, 0), Coord(0, 1), Coord(0, -1)}) {
            const Coord n = p + d;
            if(!r.map.isInsideMap(n.x, n.y)) continue;
            const int index = occupied.index(n.x, n.y);
            if(destination[index]) return steps;
            if(steps < 20 && occupied.road[index] == from.house && distance[index] < 0) {
                distance[index] = steps + 1;
                queue.push_back(n);
            }
        }
    }
    return -2;
}

bool coversCore(const Placement& turret, const Placement& building, const Catalogue& c) {
    // Doubled tile centres: no floating point, including odd-width Research Lab.
    const Coord size = c.sizes[building.item];
    const int dx = 2 * turret.x + 1 - (2 * building.x + size.x);
    const int dy = 2 * turret.y + 1 - (2 * building.y + size.y);
    return dx * dx + dy * dy <= 16 * 16; // Shipped rocket range: eight tiles.
}

void requireSetup(const Result& r, const CivParams& params, const Catalogue& c, const Occupancy& occupied) {
    const int width = r.map.getSizeX(), height = r.map.getSizeY(), end = width / 4;
    const std::set<int> allowed{Structure_ConstructionYard, Structure_ResearchLab, Structure_Refinery, Structure_WindTrap,
        Structure_RocketTurret, Structure_PoliceStation, Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial};
    for(std::size_t index = 0; index < r.houses.size(); ++index) {
        const HouseSetup& h = r.houses[index];
        const int seat = static_cast<int>(index) % params.teamSize;
        INFO("house=" << h.house << " seat=" << seat);
        std::map<int, int> counts;
        std::vector<Placement> homes, industries, coreTurrets;
        const Placement* yard = nullptr;
        const Placement* lab = nullptr;
        int required = 0, produced = 0, taxBase = 0, upkeep = 0;
        for(const Placement& p : r.structures) if(p.house == h.house) {
            REQUIRE(allowed.count(p.item) == 1);
            ++counts[p.item];
            if(p.item == Structure_ConstructionYard) yard = &p;
            if(p.item == Structure_ResearchLab) lab = &p;
            if(p.item == Structure_ZoneResidential) homes.push_back(p);
            if(p.item == Structure_ZoneIndustrial) industries.push_back(p);
            if(p.item == Structure_PoliceStation) upkeep += 100;
            if(p.item == Structure_RocketTurret) {
                upkeep += 15;
                if(std::min(p.x, width - 1 - p.x) != end - 2) coreTurrets.push_back(p);
            }
            if(DuneCity::isCityZoneStructure(p.item)) {
                required += DuneCity::getZonePower(p.item, p.level);
                taxBase += DuneCity::taxablePopulationEighths(p.item, DuneCity::getZonePopulation(p.item, p.level), p.level);
            } else {
                const int power = c.rule(p.item, h.house).power;
                required += std::max(0, power);
                produced += std::max(0, -power);
            }
            const auto role = DuneCity::getStructureCityRole(p.item);
            if(role != DuneCity::CityRole::None) {
                const auto want = role == DuneCity::CityRole::Residential ? DuneCity::CityRole::Commercial
                    : role == DuneCity::CityRole::Commercial ? DuneCity::CityRole::Industrial : DuneCity::CityRole::Residential;
                INFO("traffic from item=" << p.item << " x=" << p.x << " y=" << p.y);
                REQUIRE(tripLength(r, c, occupied, p, want) >= 0);
            }
        }
        REQUIRE(counts[Structure_ConstructionYard] == 1);
        REQUIRE(counts[Structure_ResearchLab] == 1);
        REQUIRE(counts[Structure_Refinery] == 1);
        REQUIRE(counts[Structure_PoliceStation] == 1);
        REQUIRE(counts[Structure_ZoneResidential] == 3);
        REQUIRE(counts[Structure_ZoneCommercial] == 1);
        REQUIRE(counts[Structure_ZoneIndustrial] == 1);
        REQUIRE(counts[Structure_WindTrap] > 0);
        REQUIRE(coreTurrets.size() == 2);
        REQUIRE(yard != nullptr);
        REQUIRE(lab != nullptr);
        for(const Placement& turret : coreTurrets) {
            REQUIRE(coversCore(turret, *yard, c));
            REQUIRE(coversCore(turret, *lab, c));
        }
        int mouths = 0;
        for(int lane = 0; lane < 3; ++lane) if(ownerSeat(params.teamSize, lane) == seat) {
            ++mouths;
            const int x = index < static_cast<std::size_t>(params.teamSize) ? end - 2 : width - end + 1;
            for(int dy : {-3, 3}) {
                const int y = laneCentre(height, lane) + dy;
                REQUIRE(std::count_if(r.structures.begin(), r.structures.end(), [&](const Placement& p) {
                    return p.house == h.house && p.item == Structure_RocketTurret && p.x == x && p.y == y;
                }) == 1);
            }
        }
        REQUIRE(counts[Structure_RocketTurret] == 2 + 2 * mouths);
        REQUIRE(produced - required >= required / 10 + 1);
        // At deliberately modest land value 80, mature private tax still pays
        // police, ALL turrets, and eight 15-second power charges per slow year.
        // The parent engine fixture additionally measures actual loaded income.
        REQUIRE(DuneCity::computeAnnualTaxRevenue(taxBase, 7, 80) > upkeep + (required + 3) / 4);
        REQUIRE(std::count_if(r.units.begin(), r.units.end(), [&](const Placement& p) { return p.house == h.house; }) == 1);
        REQUIRE(h.skipped == 0);
        REQUIRE(h.armyBudget == 0);
        REQUIRE(h.armySpent == 0);
        REQUIRE(h.cityEstimate == SkirmishGenerator::displayedPopulation(r.structures, h.house));
        REQUIRE(h.cityTarget == h.cityEstimate);
        REQUIRE(occupied.building[occupied.index(h.start.x, h.start.y)] == h.house);
        for(const Placement& home : homes) for(const Placement& industry : industries) {
            const int gap = std::max({0, industry.x - (home.x + 2), home.x - (industry.x + 2),
                industry.y - (home.y + 2), home.y - (industry.y + 2)});
            REQUIRE(gap >= SkirmishGenerator::kIndustrialBuffer);
        }
        for(const Placement& p : r.structures) if(p.house == h.house && (p.item == Structure_ZoneResidential || p.item == Structure_ZoneCommercial)) {
            int policeDistance = width + height;
            for(const Placement& police : r.structures) if(police.house == h.house && police.item == Structure_PoliceStation)
                policeDistance = std::min(policeDistance, std::abs(p.x - police.x) + std::abs(p.y - police.y));
            REQUIRE(policeDistance <= 18);
        }
        for(const Placement& refinery : r.structures) if(refinery.house == h.house && refinery.item == Structure_Refinery) {
            const Coord size = c.sizes[refinery.item];
            auto clearExit = [&](int x, int y) {
                REQUIRE(occupied.building[occupied.index(x, y)] == -1);
                REQUIRE(occupied.road[occupied.index(x, y)] == -1);
                REQUIRE(occupied.unit[occupied.index(x, y)] == -1);
            };
            for(int dx = -1; dx <= size.x; ++dx) clearExit(refinery.x + dx, refinery.y + size.y);
            for(int dy = 0; dy < size.y; ++dy) {
                clearExit(refinery.x - 1, refinery.y + dy);
                clearExit(refinery.x + size.x, refinery.y + dy);
            }
        }
    }
}

void requireLanesIslandsAndWorms(const Result& r, const CivParams& params, const Occupancy& occupied) {
    const int width = r.map.getSizeX(), height = r.map.getSizeY(), end = width / 4, middle = width / 2;
    bool ridgesContinuous = true;
    std::array<int, 2> previous{{-1, -1}};
    for(int x = end; x < width - end; ++x) {
        std::array<int, 2> rows{{-1, -1}};
        int runs = 0;
        for(int y = 0; y < height; ++y) if(r.map(x, y) == Terrain_Mountain) {
            const int top = y;
            while(y + 1 < height && r.map(x, y + 1) == Terrain_Mountain) ++y;
            if(runs < 2) rows[runs] = top;
            ridgesContinuous = ridgesContinuous && y - top + 1 == 3;
            ++runs;
        }
        ridgesContinuous = ridgesContinuous && runs == 2;
        for(int ridge = 0; ridge < 2; ++ridge) {
            ridgesContinuous = ridgesContinuous && (previous[ridge] < 0 || std::abs(rows[ridge] - previous[ridge]) <= 1);
            previous[ridge] = rows[ridge];
        }
    }
    REQUIRE(ridgesContinuous);
    // Eight-neighbour flooding is deliberately stronger than a cardinal-path
    // check: even diagonal vehicle moves cannot switch lanes inside the sand.
    std::vector<int> region(width * height, -1);
    int regions = 0;
    for(int y = 0; y < height; ++y) for(int x = end; x < width - end; ++x) {
        const int origin = occupied.index(x, y);
        if(region[origin] >= 0 || r.map(x, y) == Terrain_Mountain) continue;
        std::vector<Coord> queue{{x, y}};
        region[origin] = regions;
        bool reachesLeft = false, reachesRight = false;
        for(std::size_t i = 0; i < queue.size(); ++i) {
            const Coord p = queue[i];
            reachesLeft = reachesLeft || p.x == end;
            reachesRight = reachesRight || p.x == width - end - 1;
            for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx) {
                const Coord n(p.x + dx, p.y + dy);
                if(n.x < end || n.x >= width - end || n.y < 0 || n.y >= height || r.map(n.x, n.y) == Terrain_Mountain) continue;
                const int index = occupied.index(n.x, n.y);
                if(region[index] >= 0) continue;
                region[index] = regions;
                queue.push_back(n);
            }
        }
        REQUIRE(reachesLeft);
        REQUIRE(reachesRight);
        ++regions;
    }
    REQUIRE(regions == 3);
    std::set<int> islandRegions;
    int islandRock = 0;
    for(int lane = 0; lane < 3; ++lane) {
        const int cy = laneCentre(height, lane);
        REQUIRE(islandRegions.insert(region[occupied.index(middle, cy)]).second);
        // A straight vehicle route across every mouth and lane, with no unit,
        // structure or mountain obstructing it; the central island is passable rock.
        bool clearLane = true;
        for(int x = end - 1; x <= width - end; ++x) clearLane = clearLane
            && r.map(x, cy) != Terrain_Mountain && occupied.building[occupied.index(x, cy)] == -1;
        REQUIRE(clearLane);
        for(int dy = -2; dy <= 2; ++dy) for(int dx = -2; dx <= 2; ++dx) REQUIRE(r.map(middle + dx, cy + dy) == Terrain_Rock);
        std::vector<Coord> queue{{middle, cy}};
        std::set<std::pair<int, int>> seen{{middle, cy}};
        bool sandMoat = true;
        for(std::size_t i = 0; i < queue.size(); ++i) {
            const Coord p = queue[i];
            for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx) {
                const Coord n(p.x + dx, p.y + dy);
                if(!r.map.isInsideMap(n.x, n.y)) { sandMoat = false; continue; }
                if(r.map(n.x, n.y) != Terrain_Rock) { sandMoat = sandMoat && r.map(n.x, n.y) == Terrain_Sand; continue; }
                if(dx != 0 && dy != 0) continue;
                if(seen.insert({n.x, n.y}).second) queue.push_back(n);
            }
        }
        REQUIRE(sandMoat);
        REQUIRE(queue.size() >= 25);
        REQUIRE(queue.size() <= 15 * 11);
        for(Coord p : queue) {
            REQUIRE(p.x > end + 1);
            REQUIRE(p.x < width - end - 2);
            REQUIRE(region[occupied.index(p.x, p.y)] == region[occupied.index(middle, cy)]);
        }
        islandRock += static_cast<int>(queue.size());
    }
    int centralRock = 0;
    for(int y = 0; y < height; ++y) for(int x = end; x < width - end; ++x) centralRock += r.map(x, y) == Terrain_Rock;
    REQUIRE(centralRock == islandRock); // Exactly three islands, no stray rock bridges.

    REQUIRE(r.worms.size() == static_cast<std::size_t>(3 * params.wormsPerLane));
    std::array<int, 3> counts{}, centres{};
    std::set<std::pair<int, int>> worms;
    for(const Placement& p : r.worms) {
        REQUIRE(p.item == Unit_Sandworm);
        REQUIRE(p.house == HOUSE_FREMEN);
        REQUIRE(r.map.isInsideMap(p.x, p.y));
        REQUIRE(p.x >= end);
        REQUIRE(p.x < width - end);
        REQUIRE(r.map(p.x, p.y) == Terrain_Sand);
        REQUIRE(occupied.building[occupied.index(p.x, p.y)] == -1);
        REQUIRE(occupied.road[occupied.index(p.x, p.y)] == -1);
        REQUIRE(occupied.unit[occupied.index(p.x, p.y)] == -1);
        REQUIRE(worms.insert({p.x, p.y}).second);
        const int lane = std::min(2, p.y * 3 / height);
        REQUIRE(region[occupied.index(p.x, p.y)] == region[occupied.index(middle, laneCentre(height, lane))]);
        ++counts[lane];
        centres[lane] += p.x == middle;
        for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx) REQUIRE(r.map(p.x + dx, p.y + dy) != Terrain_Rock);
    }
    for(const auto& worm : worms) REQUIRE(worms.count({width - 1 - worm.first, worm.second}) == 1);
    for(int lane = 0; lane < 3; ++lane) {
        REQUIRE(counts[lane] == params.wormsPerLane);
        REQUIRE(centres[lane] == params.wormsPerLane % 2);
    }
}

void requireSerialized(const Result& r, const CivParams& params, const Catalogue& c) {
    const auto ini = parse(r.ini);
    REQUIRE(ini->getIntValue("BASIC", "Version") == 2);
    REQUIRE(ini->getIntValue("BASIC", "TechLevel") == 1);
    REQUIRE(ini->getStringValue("BASIC", "Mod") == "dunecity");
    REQUIRE(ini->getIntValue("MAP", "SizeX") == r.map.getSizeX());
    REQUIRE(ini->getIntValue("MAP", "SizeY") == r.map.getSizeY());
    REQUIRE(r.ini.find("[CIVILIZATIONWARS]\nVersion=1\n") != std::string::npos);
    REQUIRE(ini->getIntValue("CIVILIZATIONWARS", "Version") == 1);
    REQUIRE(ini->getStringValue("CIVILIZATIONWARS", "Seed") == std::to_string(params.seed));
    REQUIRE(ini->getIntValue("CIVILIZATIONWARS", "TeamSize") == params.teamSize);
    REQUIRE(ini->getIntValue("CIVILIZATIONWARS", "WormsPerLane", -1) == params.wormsPerLane);
    REQUIRE_FALSE(ini->hasSection("RANDOMSKIRMISH"));
    REQUIRE(MapMetadata::read(*ini, r.name).mod == MapMetadata::ModDuneCity);
    REQUIRE(MapMetadata::countPlayers(*ini) == 2 * params.teamSize);
    std::set<int> houses;
    for(std::size_t index = 0; index < r.houses.size(); ++index) {
        const HouseSetup& h = r.houses[index];
        REQUIRE(houses.insert(h.house).second);
        const std::string section = params.multiplayer ? "Player" + std::to_string(index + 1) : c.houseNames[h.house];
        REQUIRE(ini->getIntValue(section, "Credits", -1) == 0);
        REQUIRE(ini->getStringValue(section, "Brain") == "Team" + std::to_string(h.team));
        REQUIRE(ini->getStringValue("CIVILIZATIONWARS", section) == "Team" + std::to_string(h.team));
        REQUIRE(h.team == (index < static_cast<std::size_t>(params.teamSize) ? 1 : 2));
        if(params.multiplayer) {
            REQUIRE(h.house == static_cast<int>(index));
            REQUIRE(h.role == Role::Open);
            REQUIRE(h.playerClass.empty());
        } else if(index == 0) {
            REQUIRE(h.house == HOUSE_ATREIDES);
            REQUIRE(h.role == Role::Human);
            REQUIRE(h.playerClass == "HumanPlayer");
        } else if(index < static_cast<std::size_t>(params.teamSize)) {
            REQUIRE(h.role == Role::Ally);
            REQUIRE(h.playerClass == params.allyClass);
        } else {
            REQUIRE(h.role == Role::Enemy);
            REQUIRE(h.playerClass == params.enemyClass);
        }
        if(index > 0 && index != static_cast<std::size_t>(params.teamSize)) REQUIRE(h.start.y > r.houses[index - 1].start.y);
    }
    auto owner = [&](int house) { return params.multiplayer ? "Player" + std::to_string(house + 1) : c.houseNames[house]; };
    auto position = [&](const Placement& p) { return std::to_string(p.y * r.map.getSizeX() + p.x); };
    auto idKey = [](int id) {
        std::string number = std::to_string(id);
        number.insert(0, 4 - number.size(), '0');
        return "ID" + number;
    };
    std::set<std::string> expectedStructures, expectedUnits;
    for(const Placement& p : r.tiles)
        expectedStructures.insert("GEN" + position(p) + "=" + owner(p.house) + "," + c.itemNames[p.item]);
    int id = 0;
    for(const Placement& p : r.structures)
        expectedStructures.insert(idKey(++id) + "=" + owner(p.house) + "," + c.itemNames[p.item] + ",256," + position(p)
            + (DuneCity::isCityZoneStructure(p.item) ? "," + std::to_string(p.level) : ""));
    id = 0;
    for(const Placement& p : r.units)
        expectedUnits.insert(idKey(++id) + "=" + owner(p.house) + "," + c.itemNames[p.item] + ",256," + position(p) + ",64,Area Guard");
    for(const Placement& p : r.worms)
        expectedUnits.insert(idKey(++id) + "=Fremen,Sandworm,256," + position(p) + ",64,Area Guard");
    int structures = 0, tiles = 0, zones = 0, units = 0, worms = 0;
    for(const auto& entry : ini->getSection("STRUCTURES")) {
        const std::string name = entry.getKeyName(), value = entry.getStringValue();
        REQUIRE(expectedStructures.erase(name + "=" + value) == 1);
        if(name.rfind("GEN", 0) == 0) ++tiles;
        else {
            ++structures;
            const bool zone = value.find(" Zone,") != std::string::npos;
            if(zone) { ++zones; REQUIRE(ZoneStartLevel::parse(value) == 3); }
            else REQUIRE(ZoneStartLevel::parse(value) == -1);
        }
    }
    REQUIRE(expectedStructures.empty());
    REQUIRE(structures == static_cast<int>(r.structures.size()));
    REQUIRE(tiles == static_cast<int>(r.tiles.size()));
    REQUIRE(zones == 5 * 2 * params.teamSize);
    for(const auto& entry : ini->getSection("UNITS")) {
        const std::string value = entry.getStringValue();
        REQUIRE(expectedUnits.erase(entry.getKeyName() + "=" + value) == 1);
        if(value.find(",Sandworm,") != std::string::npos) {
            ++worms;
            REQUIRE(value.rfind("Fremen,Sandworm,256,", 0) == 0);
        } else { ++units; REQUIRE(value.find(",Carryall,") != std::string::npos); }
        REQUIRE(value.find(",Harvester,") == std::string::npos);
        const auto first = value.find(',', value.find(",256,") + 1);
        REQUIRE(first != std::string::npos);
        const int position = std::stoi(value.substr(first + 1));
        REQUIRE(position >= 0);
        REQUIRE(position < r.map.getSizeX() * r.map.getSizeY());
    }
    REQUIRE(expectedUnits.empty());
    REQUIRE(units == 2 * params.teamSize);
    REQUIRE(worms == 3 * params.wormsPerLane);
    bool rowsMatch = true;
    for(int y = 0; y < r.map.getSizeY(); ++y) {
        std::string rowKey = std::to_string(y);
        rowKey.insert(0, 3 - rowKey.size(), '0');
        const std::string row = ini->getStringValue("MAP", rowKey);
        rowsMatch = rowsMatch && row.size() == static_cast<std::size_t>(r.map.getSizeX());
        for(int x = 0; x < static_cast<int>(row.size()); ++x) {
            const char encoded = row[x];
            const auto terrain = r.map(x, y);
            rowsMatch = rowsMatch && ((encoded == '%' && terrain == Terrain_Rock) || (encoded == '@' && terrain == Terrain_Mountain)
                || (encoded == '^' && terrain == Terrain_Dunes) || (encoded == '~' && terrain == Terrain_Spice)
                || (encoded == '+' && terrain == Terrain_ThickSpice) || (encoded == 'O' && terrain == Terrain_SpiceBloom)
                || (encoded == 'Q' && terrain == Terrain_SpecialBloom) || (encoded == '-' && terrain == Terrain_Sand));
        }
    }
    REQUIRE(rowsMatch);
}

uint64_t digestTerrain(const MapData& map, int kind) {
    uint64_t hash = 14695981039346656037ull;
    for(int y = 0; y < map.getSizeY(); ++y) for(int x = 0; x < map.getSizeX(); ++x) {
        const auto terrain = map(x, y);
        const bool include = kind == 0 ? terrain == Terrain_Mountain : kind == 1 ? terrain == Terrain_Rock && x >= map.getSizeX() / 4 && x < map.getSizeX() - map.getSizeX() / 4
            : kind == 2 ? terrain == Terrain_Spice || terrain == Terrain_ThickSpice
            : terrain == Terrain_SpiceBloom || terrain == Terrain_SpecialBloom;
        hash ^= static_cast<uint64_t>(include);
        hash *= 1099511628211ull;
    }
    return hash;
}
} // namespace

TEST_CASE("Civilization Wars: fixed API and shared Research Lab contract", "[civwars][generator]") {
    const CivParams p;
    REQUIRE(p.teamSize == 1);
    REQUIRE(p.size == 0);
    REQUIRE(p.wormsPerLane == 1);
    REQUIRE(p.spice == Spice::Medium);
    REQUIRE(p.seed == 1);
    REQUIRE_FALSE(p.fixedSeed);
    REQUIRE(p.house == HOUSE_INVALID);
    REQUIRE(p.allyClass == "qBotMedium");
    REQUIRE(p.enemyClass == "qBotMedium");
    REQUIRE_FALSE(p.spectate);
    REQUIRE(p.spectateClass == "qBotMedium");
    REQUIRE_FALSE(p.multiplayer);
    const std::array<Coord, 4> expected{{{129, 96}, {161, 112}, {193, 128}, {255, 160}}};
    REQUIRE(CivWarsGenerator::kMapSizes == expected);
    const Catalogue& c = rules();
    REQUIRE(c.sizes[Structure_ResearchLab] == Coord(3, 2));
    REQUIRE(c.itemNames[Structure_ResearchLab] == "Research Lab");
    for(int house : c.houses) {
        REQUIRE(c.rule(Structure_ResearchLab, house).enabled);
        REQUIRE(c.rule(Structure_ResearchLab, house).power == 30);
        REQUIRE(c.rule(Structure_ResearchLab, house).techLevel == 1);
        REQUIRE(c.rule(Structure_ResearchLab, house).builder == Structure_ConstructionYard);
        REQUIRE(c.rule(Structure_ResearchLab, house).prerequisites.empty());
    }
}

TEST_CASE("Civilization Wars: every size, team and worm count stays playable and mirrored over many seeds", "[civwars][generator][matrix]") {
    const Catalogue& c = rules();
    for(std::size_t seedIndex = 0; seedIndex < kSeeds.size(); ++seedIndex) for(int size = 0; size < 4; ++size)
        for(int team = 1; team <= 3; ++team) for(int worms = 0; worms <= 4; ++worms) {
            CivParams p;
            p.seed = kSeeds[seedIndex]; p.size = size; p.teamSize = team; p.wormsPerLane = worms; p.house = HOUSE_ATREIDES;
            p.spice = static_cast<Spice>((seedIndex + size + team + worms) % 5);
            p.allyClass = "qBotEasy"; p.enemyClass = "qBotHard";
            INFO("seed=" << p.seed << " size=" << size << " team=" << team << " worms=" << worms << " spice=" << static_cast<int>(p.spice));
            const Result r = CivWarsGenerator::generate(p, c);
            REQUIRE(r.map.getSizeX() == CivWarsGenerator::kMapSizes[size].x);
            REQUIRE(r.map.getSizeY() == CivWarsGenerator::kMapSizes[size].y);
            REQUIRE(r.houses.size() == static_cast<std::size_t>(2 * team));
            REQUIRE(r.mapSeed == p.seed);
            REQUIRE(r.attempts == 1);
            REQUIRE_FALSE(r.flattenedMountains);
            const Occupancy occupied = requirePlacementAndMirror(r, c, team);
            requireSetup(r, p, c, occupied);
            requireLanesIslandsAndWorms(r, p, occupied);
            requireSerialized(r, p, c);
            REQUIRE(CivWarsGenerator::generate(p, c).ini == r.ini);
        }
}

TEST_CASE("Civilization Wars: generic multiplayer starts retain mirrored kits and team metadata", "[civwars][generator][multiplayer]") {
    for(int size = 0; size < 4; ++size) for(int team = 1; team <= 3; ++team) {
        CivParams p;
        p.size = size; p.teamSize = team; p.seed = 9101u + size; p.wormsPerLane = 4; p.multiplayer = true;
        p.house = HOUSE_ATREIDES;
        const Result r = CivWarsGenerator::generate(p, rules());
        const Occupancy occupied = requirePlacementAndMirror(r, rules(), team);
        requireSetup(r, p, rules(), occupied);
        requireLanesIslandsAndWorms(r, p, occupied);
        requireSerialized(r, p, rules());
        REQUIRE(CivWarsGenerator::generate(p, rules()).ini == r.ini);
        p.house = HOUSE_HARKONNEN;
        p.allyClass = "qBotEasy"; p.enemyClass = "qBotBrutal"; p.spectate = true;
        REQUIRE(CivWarsGenerator::generate(p, rules()).ini == r.ini);
    }
}

TEST_CASE("Civilization Wars: every chosen faction takes the first left seat and random remains seeded", "[civwars][generator][roster]") {
    for(int house : rules().houses) {
        CivParams p;
        p.teamSize = 3; p.house = house; p.seed = 713u;
        const Result r = CivWarsGenerator::generate(p, rules());
        REQUIRE(r.houses.front().house == house);
        REQUIRE(r.houses.front().role == Role::Human);
        REQUIRE(r.houses.front().team == 1);
        std::set<int> assigned;
        for(const HouseSetup& h : r.houses) REQUIRE(assigned.insert(h.house).second);
        requirePlacementAndMirror(r, rules(), p.teamSize);
    }
    for(uint32_t seed : kSeeds) {
        CivParams p;
        p.seed = seed; p.house = HOUSE_INVALID;
        const Result a = CivWarsGenerator::generate(p, rules()), b = CivWarsGenerator::generate(p, rules());
        REQUIRE(a.houses.front().house == b.houses.front().house);
        REQUIRE(std::find(rules().houses.begin(), rules().houses.end(), a.houses.front().house) != rules().houses.end());
        REQUIRE(a.ini == b.ini);
    }
}

TEST_CASE("Civilization Wars: seed changes ridges, island outlines, spice and blooms", "[civwars][generator][terrain]") {
    std::array<std::set<uint64_t>, 4> digests;
    for(uint32_t seed : kSeeds) {
        CivParams p;
        p.seed = seed; p.spice = Spice::VeryHigh;
        const Result r = CivWarsGenerator::generate(p, rules());
        for(int kind = 0; kind < 4; ++kind) digests[kind].insert(digestTerrain(r.map, kind));
    }
    for(const auto& samples : digests) REQUIRE(samples.size() > 1);
    for(int size = 0; size < 4; ++size) {
        int previous = -1;
        for(int spice = 0; spice <= 4; ++spice) {
            CivParams p;
            p.size = size; p.spice = static_cast<Spice>(spice); p.seed = 20261102u;
            const Result r = CivWarsGenerator::generate(p, rules());
            int count = 0;
            for(int y = 0; y < r.map.getSizeY(); ++y) for(int x = 0; x < r.map.getSizeX(); ++x) {
                const auto terrain = r.map(x, y);
                count += terrain == Terrain_Spice || terrain == Terrain_ThickSpice || terrain == Terrain_SpiceBloom || terrain == Terrain_SpecialBloom;
            }
            if(spice == 0) REQUIRE(count == 0);
            else REQUIRE(count > previous);
            previous = count;
        }
    }
}

TEST_CASE("Civilization Wars: permanent worm slots and map content ignore menu-only choices", "[civwars][generator][determinism]") {
    for(int size = 0; size < 4; ++size) for(int team = 1; team <= 3; ++team) {
        CivParams p;
        p.size = size; p.teamSize = team; p.seed = 0xFFFFFFFFu; p.wormsPerLane = 4;
        const Result baseline = CivWarsGenerator::generate(p, rules());
        p.fixedSeed = true; p.spectate = true; p.spectateClass = "qBotHard";
        REQUIRE(CivWarsGenerator::generate(p, rules()).ini == baseline.ini);
        for(int worms = 0; worms <= 4; ++worms) {
            p.wormsPerLane = worms;
            const Result r = CivWarsGenerator::generate(p, rules());
            bool same = true;
            for(int y = 0; y < r.map.getSizeY(); ++y) for(int x = 0; x < r.map.getSizeX(); ++x) same = same && r.map(x, y) == baseline.map(x, y);
            REQUIRE(same);
            REQUIRE(r.structures.size() == baseline.structures.size());
            for(std::size_t i = 0; i < r.structures.size(); ++i) REQUIRE(key(r.structures[i]) == key(baseline.structures[i]));
            const Result again = CivWarsGenerator::generate(p, rules());
            REQUIRE(r.worms.size() == again.worms.size());
            for(std::size_t i = 0; i < r.worms.size(); ++i) REQUIRE(key(r.worms[i]) == key(again.worms[i]));
        }
    }
}

TEST_CASE("Civilization Wars: power planning uses the worst faction without breaking symmetry", "[civwars][generator][power]") {
    Catalogue c = rules();
    c.rules[Structure_WindTrap][HOUSE_ORDOS].power = -50;
    c.rules[Structure_ResearchLab][HOUSE_HARKONNEN].power = 45;
    for(bool multiplayer : {false, true}) {
        CivParams p;
        p.seed = 81; p.teamSize = 3; p.multiplayer = multiplayer; p.house = HOUSE_ATREIDES;
        const Result r = CivWarsGenerator::generate(p, c);
        requirePlacementAndMirror(r, c, p.teamSize);
        // Custom power values are intentionally outside the shipped-kit fiscal
        // assertion; inspect actual power for every possible lobby faction.
        for(const HouseSetup& start : r.houses) for(int house : c.houses) {
            int draw = 0, supply = 0;
            for(const Placement& item : r.structures) if(item.house == start.house) {
                if(DuneCity::isCityZoneStructure(item.item)) draw += DuneCity::getZonePower(item.item, item.level);
                else { draw += std::max(0, c.rule(item.item, house).power); supply += std::max(0, -c.rule(item.item, house).power); }
            }
            REQUIRE(supply - draw >= draw / 10 + 1);
        }
    }
}

TEST_CASE("Civilization Wars: parameter persistence is isolated and preserves the full uint32 seed", "[civwars][generator][config]") {
    for(uint32_t seed : {0u, 1u, 0xFFFFFFFFu}) for(bool multiplayer : {false, true}) {
        INIFile config(false, std::string{});
        config.setIntValue("Random Skirmish", "Credits", 7500);
        CivParams p;
        p.teamSize = 3; p.size = 3; p.wormsPerLane = 4; p.spice = Spice::VeryHigh;
        p.seed = seed; p.fixedSeed = true; p.house = HOUSE_ATREIDES;
        p.allyClass = "qBotEasy"; p.enemyClass = "qBotHard"; p.spectate = !multiplayer;
        p.spectateClass = "qBotBrutal"; p.multiplayer = multiplayer;
        p.save(config);
        REQUIRE(config.hasSection("Civilization Wars"));
        REQUIRE(config.getIntValue("Random Skirmish", "Credits") == 7500);
        CivParams loaded;
        loaded.load(config);
        REQUIRE(loaded.teamSize == p.teamSize);
        REQUIRE(loaded.size == p.size);
        REQUIRE(loaded.wormsPerLane == p.wormsPerLane);
        REQUIRE(loaded.spice == p.spice);
        REQUIRE(loaded.seed == p.seed);
        REQUIRE(loaded.fixedSeed == p.fixedSeed);
        REQUIRE(loaded.house == p.house);
        REQUIRE(loaded.allyClass == p.allyClass);
        REQUIRE(loaded.enemyClass == p.enemyClass);
        REQUIRE(loaded.spectate == p.spectate);
        REQUIRE(loaded.spectateClass == p.spectateClass);
        REQUIRE(loaded.multiplayer == p.multiplayer);
    }
    INIFile invalid(false, std::string{});
    for(const char* text : {"", "-1", "+1", "4294967296", "999999999999999999999", "0x10", "1x", " 3", "3 "}) {
        invalid.setStringValue("Civilization Wars", "Seed", text);
        CivParams p;
        p.seed = 17;
        p.load(invalid);
        REQUIRE(p.seed == 17);
    }
}

TEST_CASE("Civilization Wars: clamping and incomplete catalogues cannot produce partial starts", "[civwars][generator][config]") {
    CivParams p;
    p.teamSize = 99; p.size = 99; p.wormsPerLane = 99; p.spice = static_cast<Spice>(99);
    p.house = NUM_PLAYER_HOUSES; p.allyClass.clear(); p.enemyClass.clear(); p.spectateClass.clear(); p.multiplayer = true; p.spectate = true;
    p.clamp(8);
    REQUIRE(p.teamSize == 3);
    REQUIRE(p.size == 3);
    REQUIRE(p.wormsPerLane == 4);
    REQUIRE(p.spice == Spice::VeryHigh);
    REQUIRE(p.house == HOUSE_INVALID);
    REQUIRE(p.allyClass == "qBotMedium");
    REQUIRE(p.enemyClass == "qBotMedium");
    REQUIRE(p.spectateClass == "qBotMedium");
    REQUIRE_FALSE(p.spectate);
    p.teamSize = 3; p.clamp(5); REQUIRE(p.teamSize == 2);
    p.teamSize = 3; p.clamp(3); REQUIRE(p.teamSize == 1);
    p.teamSize = -1; p.size = -1; p.wormsPerLane = -1; p.spice = static_cast<Spice>(-1);
    p.clamp(8);
    REQUIRE(p.teamSize == 1);
    REQUIRE(p.size == 0);
    REQUIRE(p.wormsPerLane == 0);
    REQUIRE(p.spice == Spice::None);
    Catalogue c = rules();
    c.houses.resize(5);
    p.teamSize = 3;
    REQUIRE(CivWarsGenerator::generate(p, c).houses.size() == 4);
    c.rules[Structure_ResearchLab][HOUSE_ATREIDES].enabled = false;
    REQUIRE_THROWS_AS(CivWarsGenerator::generate(p, c), std::invalid_argument);
    c = rules(); c.sizes[Structure_ResearchLab] = Coord(2, 2);
    REQUIRE_THROWS_AS(CivWarsGenerator::generate(p, c), std::invalid_argument);
    c = rules(); c.rules[Structure_WindTrap][HOUSE_ATREIDES].power = 0;
    REQUIRE_THROWS_AS(CivWarsGenerator::generate(p, c), std::invalid_argument);
    c = rules(); c.houses.resize(1);
    REQUIRE_THROWS_AS(CivWarsGenerator::generate(p, c), std::invalid_argument);
}
