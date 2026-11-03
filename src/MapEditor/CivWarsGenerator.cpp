/*
 * This file is part of Dune Legacy, licensed under the GNU GPL version 2 or later.
 */
#include <MapEditor/CivWarsGenerator.h>

#include <FileClasses/INIFile.h>
#include <INIMap/ZoneStartLevel.h>
#include <dunecity/CityConstants.h>
#include <dunecity/ZonePower.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace CivWarsGenerator {
namespace {

using SkirmishGenerator::Catalogue;
using SkirmishGenerator::HouseSetup;
using SkirmishGenerator::Placement;
using SkirmishGenerator::Result;
using SkirmishGenerator::Role;
using SkirmishGenerator::kSpicePercent;
constexpr const char* kSection = "Civilization Wars";
constexpr int kLanes = 3;
constexpr int kMouthTurrets = 2;
constexpr int kCoreTurrets = 2;
constexpr int kCityLevel = ZoneStartLevel::kMaxLevel;

uint32_t mix(uint32_t seed, uint32_t value) {
    uint32_t h = seed * 0x9E3779B1u ^ (value + 0x7F4A7C15u + (seed << 6) + (seed >> 2));
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}

uint32_t noise(uint32_t seed, int x, int y, uint32_t tag) {
    return mix(seed ^ tag, static_cast<uint32_t>(y) * 257u + static_cast<uint32_t>(x));
}

int laneY(int height, int lane) { return (2 * lane + 1) * height / (2 * kLanes); }
int laneOwner(int teamSize, int lane) { return teamSize == 1 ? 0 : teamSize == 2 ? (lane == 2 ? 1 : 0) : lane; }
int seatY(int height, int teamSize, int seat) { return (2 * seat + 1) * height / (2 * teamSize); }

struct Island {
    int rx;
    int ry;
};

Island islandShape(uint32_t seed, int lane) {
    return {9 + static_cast<int>(mix(seed, 100u + lane) % 3),
            5 + static_cast<int>(mix(seed, 200u + lane) % 2)};
}

// The bounded, linearly interpolated ridge moves at most one row per column.
// Seven-tile thickness makes the territory boundaries substantial without cutting the lanes.
int ridgeY(uint32_t seed, int x, int width, int height, int ridge) {
    const int local = std::min(x, width - 1 - x) - width / 4;
    const int cell = local / 8, fraction = local % 8;
    const int a = static_cast<int>(noise(seed, cell, ridge, 0x52494447u) % 7) - 3;
    const int b = static_cast<int>(noise(seed, cell + 1, ridge, 0x52494447u) % 7) - 3;
    return (ridge + 1) * height / kLanes + (a * (8 - fraction) + b * fraction) / 8;
}

void makeTerrain(Result& result, const Params& params) {
    MapData& map = result.map;
    const int width = map.getSizeX(), height = map.getSizeY();
    const int end = width / 4, middle = width / 2;
    const int spice = static_cast<int>(params.spice);
    constexpr int thickPercent[] = {0, 10, 25, 40, 55};
    for(int y = 0; y < height; ++y) for(int x = 0; x < end; ++x)
        map(x, y) = map(width - 1 - x, y) = Terrain_Rock;

    for(int x = end; x <= middle; ++x) for(int ridge = 0; ridge < 2; ++ridge) {
        const int y = ridgeY(params.seed, x, width, height, ridge);
        for(int dy = -3; dy <= 3; ++dy)
            map(x, y + dy) = map(width - 1 - x, y + dy) = Terrain_Mountain;
    }
    // Half the map is sand: twice the normal share there keeps the familiar spice
    // presets. Coarse seeded fields, with a little edge noise, remain nested as the
    // spice setting increases. Terrain, unlike the roster, ignores menu-only choices.
    for(int y = 0; y < height; ++y) for(int x = end; x <= middle; ++x) {
        if(map(x, y) != Terrain_Sand) continue;
        const uint32_t detail = noise(params.seed, x, y, 0x53504943u);
        const int field = static_cast<int>((noise(params.seed, x / 5, y / 5, 0x4649454Cu) % 100 + detail % 9) % 100);
        TERRAINTYPE terrain = detail % 11 == 0 ? Terrain_Dunes : Terrain_Sand;
        if(field < 2 * kSpicePercent[spice]) {
            terrain = (detail / 11) % 100 < static_cast<unsigned>(thickPercent[spice]) ? Terrain_ThickSpice : Terrain_Spice;
            if((detail / 1100) % 97 == 0) terrain = detail & 1u ? Terrain_SpiceBloom : Terrain_SpecialBloom;
        }
        map(x, y) = map(width - 1 - x, y) = terrain;
    }

    for(int lane = 0; lane < kLanes; ++lane) {
        const int cy = laneY(height, lane);
        const Island shape = islandShape(params.seed, lane);
        // A plain-sand moat keeps the buildable 7x7 island core distinct from both
        // team ends and the ridges, even with Very High spice and blooms.
        for(int dy = -shape.ry - 2; dy <= shape.ry + 2; ++dy)
            for(int dx = -shape.rx - 2; dx <= shape.rx + 2; ++dx) map(middle + dx, cy + dy) = Terrain_Sand;
        for(int dy = -shape.ry; dy <= shape.ry; ++dy) for(int dx = -shape.rx; dx <= shape.rx; ++dx) {
            const int ax = std::abs(dx), ay = std::abs(dy);
            const int edge = static_cast<int>(noise(params.seed, ax, ay, 0x49534C41u + lane) % 3) - 1;
            if((ax <= 3 && ay <= 3) || ax * shape.ry + ay * shape.rx <= shape.rx * shape.ry + edge)
                map(middle + dx, cy + dy) = Terrain_Rock;
        }

        // Reserve every possible permanent origin, not only this worm count's
        // subset: changing worms never changes the generated terrain or the kit.
        const int sign = mix(params.seed, 300u + lane) & 1u ? 1 : -1;
        const int wormY = cy + sign * (shape.ry + 3);
        map(middle, wormY) = Terrain_Sand;
        for(int pair = 0; pair < kMaxWormsPerLane / 2; ++pair) {
            const int x = end + (pair + 1) * (middle - end) / (kMaxWormsPerLane / 2 + 1);
            map(x, wormY) = map(width - 1 - x, wormY) = Terrain_Sand;
        }
    }
}

struct RequiredItem { int item; Coord size; };

void requireKit(const Catalogue& catalogue) {
    const RequiredItem kit[] = {
        {Structure_ConstructionYard, {2, 2}}, {Structure_ResearchLab, {3, 2}}, {Structure_Refinery, {3, 2}},
        {Structure_WindTrap, {2, 2}}, {Structure_RocketTurret, {1, 1}}, {Structure_PoliceStation, {2, 2}},
        {Structure_ZoneResidential, {2, 2}}, {Structure_ZoneCommercial, {2, 2}}, {Structure_ZoneIndustrial, {2, 2}},
        {Structure_Slab1, {1, 1}}, {Structure_Road, {1, 1}}, {Unit_Carryall, {1, 1}}
    };
    if(catalogue.houses.size() < 2) throw std::invalid_argument("Civilization Wars requires at least two houses");
    for(const RequiredItem& entry : kit) {
        if(catalogue.itemNames[entry.item].empty() || catalogue.sizes[entry.item] != entry.size)
            throw std::invalid_argument("Civilization Wars requires the standard starting-kit names and footprints");
        for(int house : catalogue.houses) {
            if(!catalogue.rule(entry.item, house).enabled)
                throw std::invalid_argument("Civilization Wars starting kit is disabled for a house");
        }
    }
}

// Plan against the least favourable house, including every lobby-selectable
// house in multiplayer. Opposing seats therefore have identical windtrap counts
// even when their factions have different configured power rules.
int windtrapsFor(int mouths, const Catalogue& catalogue) {
    int draw = 3 * DuneCity::getZonePower(Structure_ZoneResidential, kCityLevel)
        + DuneCity::getZonePower(Structure_ZoneCommercial, kCityLevel)
        + DuneCity::getZonePower(Structure_ZoneIndustrial, kCityLevel);
    for(int item : {Structure_ConstructionYard, Structure_ResearchLab, Structure_Refinery, Structure_PoliceStation, Structure_RocketTurret}) {
        int worst = 0;
        for(int house : catalogue.houses) worst = std::max(worst, catalogue.rule(item, house).power);
        draw += worst * (item == Structure_RocketTurret ? kCoreTurrets + kMouthTurrets * mouths : 1);
    }
    int supply = 0;
    for(int house : catalogue.houses) {
        const int output = -catalogue.rule(Structure_WindTrap, house).power;
        if(output <= 0) throw std::invalid_argument("Civilization Wars requires power-producing windtraps");
        supply = supply == 0 ? output : std::min(supply, output);
    }
    const int needed = draw + draw / 10 + 1;
    const int traps = (needed + supply - 1) / supply;
    if(traps > 10) throw std::invalid_argument("Civilization Wars starting-kit power does not fit the rock end");
    return traps;
}

void addKit(Result& result, const Params& params, const Catalogue& catalogue, int seat) {
    const int width = result.map.getSizeX(), height = result.map.getSizeY();
    const int end = width / 4, shift = end - 32, cy = seatY(height, params.teamSize, seat);
    const int leftHouse = result.houses[seat].house, rightHouse = result.houses[params.teamSize + seat].house;
    auto pair = [&](std::vector<Placement>& to, int item, int x, int y, int level = -1) {
        const int footprint = catalogue.sizes[item].x;
        to.push_back({leftHouse, item, x, y, level});
        to.push_back({rightHouse, item, width - x - footprint, y, level});
    };
    auto building = [&](int item, int x, int y, int level = -1) {
        pair(result.structures, item, x, y, level);
        const Coord size = catalogue.sizes[item];
        for(int dy = 0; dy < size.y; ++dy) for(int dx = 0; dx < size.x; ++dx)
            pair(result.tiles, Structure_Slab1, x + dx, y + dy);
    };
    auto road = [&](int x, int y) { pair(result.tiles, Structure_Road, shift + x, y); };

    // R -> C -> I districts share their top and bottom frontage roads. The last
    // R footprint ends at x=11 and I begins at x=20: nine clear tiles exceed the
    // existing IndustrialBuffer. Every directed R/C/I trip is at most 20 roads.
    for(int x : {3, 6, 9}) building(Structure_ZoneResidential, shift + x, cy - 5, kCityLevel);
    building(Structure_ZoneCommercial, shift + 13, cy - 5, kCityLevel);
    building(Structure_ZoneIndustrial, shift + 20, cy - 5, kCityLevel);
    building(Structure_PoliceStation, shift + 13, cy - 2);
    building(Structure_ConstructionYard, shift + 22, cy - 2);
    building(Structure_ResearchLab, shift + 25, cy - 2);
    building(Structure_Refinery, shift + 20, cy + 4);
    building(Structure_RocketTurret, shift + 21, cy + 1);
    building(Structure_RocketTurret, shift + 28, cy - 4);

    int mouths = 0;
    for(int lane = 0; lane < kLanes; ++lane) if(laneOwner(params.teamSize, lane) == seat) {
        ++mouths;
        for(int dy : {-3, 3}) building(Structure_RocketTurret, end - 2, laneY(height, lane) + dy);
    }
    const int traps = windtrapsFor(mouths, catalogue);
    for(int i = 0; i < traps; ++i)
        building(Structure_WindTrap, shift + 3 + 3 * (i % 5), cy + 1 + 3 * (i / 5));

    for(int x = 2; x <= 22; ++x) road(x, cy - 6);
    for(int x = 2; x <= 29; ++x) road(x, cy - 3);
    for(int y = cy - 5; y <= cy + 3; ++y) {
        if(y != cy - 3) road(2, y);
        if(y != cy - 3 && y != cy + 3) road(18, y);
    }
    // The top refinery frontage joins the city through x=18. Its entire bottom
    // exit row and both side exits remain free of buildings and road overlays.
    for(int x = 18; x <= 29; ++x) road(x, cy + 3);
    pair(result.units, Unit_Carryall, shift + 26, cy + 6);
}

char terrainChar(TERRAINTYPE terrain) {
    switch(terrain) {
        case Terrain_Rock: return '%';
        case Terrain_Mountain: return '@';
        case Terrain_Dunes: return '^';
        case Terrain_Spice: return '~';
        case Terrain_ThickSpice: return '+';
        case Terrain_SpiceBloom: return 'O';
        case Terrain_SpecialBloom: return 'Q';
        default: return '-';
    }
}

std::string writeIni(const Result& result, const Params& params, const Catalogue& catalogue) {
    const int width = result.map.getSizeX(), height = result.map.getSizeY();
    std::string out;
    out.reserve(static_cast<std::size_t>(width) * height + result.tiles.size() * 40 + result.structures.size() * 60 + 2048);
    auto line = [&](const std::string& text) { out += text; out += '\n'; };
    auto house = [&](int id) { return params.multiplayer ? "Player" + std::to_string(id + 1) : catalogue.houseNames[id]; };
    auto position = [&](const Placement& p) { return std::to_string(p.y * width + p.x); };
    line("; Civilization Wars map generated by Dune City.");
    line("[BASIC]");
    for(const char* value : {"Version=2", "MapVersion=1"}) line(value);
    line("Name=" + result.name);
    for(const char* value : {"Author=Civilization Wars", "TechLevel=1", "WinFlags=3", "LoseFlags=1", "LosePicture=LOSTVEHC.WSA",
                             "WinPicture=WIN2.WSA", "BriefPicture=SARDUKAR.WSA", "TimeOut=0", "Mod=dunecity"}) line(value);
    line("");
    line("[MAP]");
    line("SizeX=" + std::to_string(width));
    line("SizeY=" + std::to_string(height));
    std::string row;
    row.reserve(width);
    for(int y = 0; y < height; ++y) {
        char key[8];
        std::snprintf(key, sizeof(key), "%03d", y);
        row.clear();
        for(int x = 0; x < width; ++x) row += terrainChar(result.map(x, y));
        line(std::string(key) + "=" + row);
    }
    line("");
    line("[CHOAM]");
    for(const char* value : {"Carryall=2", "Harvester=4", "Launcher=5", "MCV=2", "'Thopter=5", "Quad=5", "Siege Tank=6", "Tank=6", "Trike=5"}) line(value);
    for(const HouseSetup& h : result.houses) {
        line("");
        line("[" + house(h.house) + "]");
        line("Credits=0");
        line("Brain=Team" + std::to_string(h.team));
    }
    line("");
    line("[STRUCTURES]");
    for(const Placement& p : result.tiles) line("GEN" + position(p) + "=" + house(p.house) + "," + catalogue.itemNames[p.item]);
    int id = 0;
    for(const Placement& p : result.structures) {
        char key[16];
        std::snprintf(key, sizeof(key), "ID%04d", ++id);
        line(std::string(key) + "=" + house(p.house) + "," + catalogue.itemNames[p.item] + ",256," + position(p)
             + (DuneCity::isCityZoneStructure(p.item) ? ZoneStartLevel::format(p.level) : ""));
    }
    line("");
    line("[UNITS]");
    id = 0;
    for(const Placement& p : result.units) {
        char key[16];
        std::snprintf(key, sizeof(key), "ID%04d", ++id);
        line(std::string(key) + "=" + house(p.house) + "," + catalogue.itemNames[p.item] + ",256," + position(p) + ",64,Area Guard");
    }
    // Fremen guarantees placement even without a matching start section. The
    // normal loader transfers every Sandworm to Natives, as in Random Skirmish.
    for(const Placement& p : result.worms) {
        char key[16];
        std::snprintf(key, sizeof(key), "ID%04d", ++id);
        line(std::string(key) + "=Fremen,Sandworm,256," + position(p) + ",64,Area Guard");
    }
    line("");
    line("[CIVILIZATIONWARS]");
    line("Version=1");
    line("Seed=" + std::to_string(params.seed));
    line("Size=" + std::to_string(params.size));
    line("TeamSize=" + std::to_string(params.teamSize));
    line("WormsPerLane=" + std::to_string(params.wormsPerLane));
    line("Spice=" + std::to_string(static_cast<int>(params.spice)));
    for(const HouseSetup& h : result.houses) line(house(h.house) + "=Team" + std::to_string(h.team));
    return out;
}

} // namespace

void Params::clamp(int houseCount) {
    teamSize = std::clamp(teamSize, 1, std::clamp(houseCount / 2, 1, 3));
    size = std::clamp(size, 0, static_cast<int>(kMapSizes.size()) - 1);
    wormsPerLane = std::clamp(wormsPerLane, 0, kMaxWormsPerLane);
    spice = static_cast<SkirmishGenerator::Spice>(std::clamp(static_cast<int>(spice), 0, 4));
    if(house < 0 || house >= NUM_PLAYER_HOUSES) house = HOUSE_INVALID;
    if(allyClass.empty()) allyClass = "qBotMedium";
    if(enemyClass.empty()) enemyClass = "qBotMedium";
    if(spectateClass.empty()) spectateClass = "qBotMedium";
    if(multiplayer) spectate = false;
}

void Params::load(const INIFile& config) {
    teamSize = config.getIntValue(kSection, "Team Size", teamSize);
    size = config.getIntValue(kSection, "Size", size);
    wormsPerLane = config.getIntValue(kSection, "Worms Per Lane", wormsPerLane);
    spice = static_cast<SkirmishGenerator::Spice>(config.getIntValue(kSection, "Spice", static_cast<int>(spice)));
    const std::string text = config.getStringValue(kSection, "Seed", std::to_string(seed));
    uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if(parsed.ec == std::errc() && parsed.ptr == text.data() + text.size()) seed = value;
    fixedSeed = config.getBoolValue(kSection, "Fixed Seed", fixedSeed);
    house = config.getIntValue(kSection, "House", house);
    allyClass = config.getStringValue(kSection, "Ally AI", allyClass);
    enemyClass = config.getStringValue(kSection, "Enemy AI", enemyClass);
    spectate = config.getBoolValue(kSection, "Spectate", spectate);
    spectateClass = config.getStringValue(kSection, "Spectate AI", spectateClass);
    multiplayer = config.getBoolValue(kSection, "Multiplayer", multiplayer);
}

void Params::save(INIFile& config) const {
    config.setIntValue(kSection, "Team Size", teamSize);
    config.setIntValue(kSection, "Size", size);
    config.setIntValue(kSection, "Worms Per Lane", wormsPerLane);
    config.setIntValue(kSection, "Spice", static_cast<int>(spice));
    config.setStringValue(kSection, "Seed", std::to_string(seed));
    config.setBoolValue(kSection, "Fixed Seed", fixedSeed);
    config.setIntValue(kSection, "House", house);
    config.setStringValue(kSection, "Ally AI", allyClass);
    config.setStringValue(kSection, "Enemy AI", enemyClass);
    config.setBoolValue(kSection, "Spectate", spectate);
    config.setStringValue(kSection, "Spectate AI", spectateClass);
    config.setBoolValue(kSection, "Multiplayer", multiplayer);
}

SkirmishGenerator::Result generate(const Params& requested, const SkirmishGenerator::Catalogue& catalogue) {
    requireKit(catalogue);
    Params params = requested;
    params.clamp(static_cast<int>(catalogue.houses.size()));
    const Coord dimensions = kMapSizes[params.size];
    Result result;
    result.map = MapData(dimensions.x, dimensions.y);
    result.mapSeed = params.seed;
    result.attempts = 1;
    result.name = "Civilization Wars " + std::to_string(params.teamSize) + "v" + std::to_string(params.teamSize)
        + " - " + std::to_string(dimensions.x) + "x" + std::to_string(dimensions.y) + " - " + std::to_string(params.seed);
    makeTerrain(result, params);

    std::vector<int> order;
    if(!params.multiplayer) {
        order = catalogue.houses;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const uint32_t ha = mix(params.seed ^ 0x484F5553u, a), hb = mix(params.seed ^ 0x484F5553u, b);
            return ha != hb ? ha < hb : a < b;
        });
        const auto chosen = std::find(order.begin(), order.end(), params.house);
        if(chosen != order.end()) std::rotate(order.begin(), chosen, chosen + 1);
    }
    result.houses.reserve(2 * params.teamSize);
    for(int side = 0; side < 2; ++side) for(int seat = 0; seat < params.teamSize; ++seat) {
        const int index = side * params.teamSize + seat;
        HouseSetup h;
        h.house = params.multiplayer ? index : order[index];
        h.team = side + 1;
        h.role = params.multiplayer ? Role::Open : side ? Role::Enemy : seat ? Role::Ally : Role::Human;
        h.playerClass = params.multiplayer ? "" : side ? params.enemyClass : seat ? params.allyClass : "HumanPlayer";
        const int cy = seatY(dimensions.y, params.teamSize, seat);
        const int leftX = dimensions.x / 4 - 9;
        h.start = Coord(side ? dimensions.x - 1 - leftX : leftX, cy - 1);
        const int top = seat * dimensions.y / params.teamSize, bottom = (seat + 1) * dimensions.y / params.teamSize;
        h.plateau = {side ? dimensions.x - dimensions.x / 4 : 0, top, dimensions.x / 4, bottom - top};
        result.houses.push_back(std::move(h));
    }
    for(int seat = 0; seat < params.teamSize; ++seat) addKit(result, params, catalogue, seat);
    for(HouseSetup& h : result.houses) {
        h.cityEstimate = SkirmishGenerator::displayedPopulation(result.structures, h.house);
        h.cityTarget = h.cityEstimate;
    }
    result.worms.reserve(kLanes * params.wormsPerLane);
    for(int lane = 0; lane < kLanes; ++lane) {
        const int sign = mix(params.seed, 300u + lane) & 1u ? 1 : -1;
        const int y = laneY(dimensions.y, lane) + sign * (islandShape(params.seed, lane).ry + 3);
        const int end = dimensions.x / 4, middle = dimensions.x / 2;
        if(params.wormsPerLane & 1) result.worms.push_back({HOUSE_FREMEN, Unit_Sandworm, middle, y});
        for(int pair = 0; pair < params.wormsPerLane / 2; ++pair) {
            const int x = end + (pair + 1) * (middle - end) / (kMaxWormsPerLane / 2 + 1);
            result.worms.push_back({HOUSE_FREMEN, Unit_Sandworm, x, y});
            result.worms.push_back({HOUSE_FREMEN, Unit_Sandworm, dimensions.x - 1 - x, y});
        }
    }
    result.ini = writeIni(result, params, catalogue);
    return result;
}

} // namespace CivWarsGenerator
