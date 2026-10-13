// Random Skirmish generator: pure behaviour against the shipped ObjectData defaults.
#include <MapEditor/SkirmishGenerator.h>
#include <FileClasses/INIFile.h>
#include <INIMap/ZoneStartLevel.h>
#include <dunecity/CityConstants.h>
#include <dunecity/ZonePower.h>
#include <misc/Random.h>

#include <catch2/catch_test_macros.hpp>

#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

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
    {Structure_Airport, "Airport", "Airport", {3,3}},
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
    for(const Placement& p : r.units) {
        INFO("unit " << c.itemNames[p.item] << " at " << p.x << "," << p.y);
        REQUIRE(r.map.isInsideMap(p.x, p.y));
        REQUIRE(r.map(p.x, p.y) != Terrain_Mountain);
        REQUIRE(owner[p.y * size + p.x] == 0);
        owner[p.y * size + p.x] = 3;
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
        p.allySide.city = 2; p.enemySide.city = 2;
        const Catalogue& c = mode == Mode::DuneCity ? city() : vanilla();
        const Result a = generate(p, c), b = generate(p, c);
        REQUIRE(a.ini == b.ini);
        REQUIRE(a.ini.size() > 128 * 128);
        p.seed = 4243;
        REQUIRE(generate(p, c).ini != a.ini);
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

    // 1 + 3 + 5 exceeds the eight houses a vanilla custom game can field.
    p.allies = 3; p.enemies = 5;
    p.clamp(8);
    REQUIRE(1 + p.allies + p.enemies == 8);
    REQUIRE(p.allies == 3);
    REQUIRE(generate(params(128, 3, 5, Mode::Vanilla, 8, 1), vanilla()).houses.size() == 8);

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
            addPower(items, house, 0, c);
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
            if(h.role != Role::Human) REQUIRE(powerBalance(items, h.house, zoneDraw, cat) > 0);
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
    REQUIRE(std::count(armyTypes(HOUSE_ATREIDES, 9, c).begin(), armyTypes(HOUSE_ATREIDES, 9, c).end(), Unit_Devastator) == 0);
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

    for(int rock = 0; rock <= 3; ++rock) for(uint32_t seed : {1u, 2u, 3u}) {
        Params p = params(128, 1, 2, Mode::Vanilla, 8, seed);
        p.rock = static_cast<Rock>(rock);
        const Result r = generate(p, vanilla());
        std::vector<Coord> starts;
        for(const auto& h : r.houses) starts.push_back(h.start);
        REQUIRE(groundConnected(r.map, starts));
    }

    // Rock share follows the preset.
    int previous = 0;
    for(int rock = 0; rock <= 3; ++rock) {
        Params p = params(128, 0, 1, Mode::Vanilla, 8, 11);
        p.rock = static_cast<Rock>(rock);
        const Result r = generate(p, vanilla());
        int tiles = 0;
        for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x)
            tiles += r.map(x, y) == Terrain_Rock || r.map(x, y) == Terrain_Mountain;
        const int percent = tiles * 100 / (128 * 128);
        INFO("rock preset " << rock << " share " << percent);
        REQUIRE(percent >= kRockPercent[rock]);
        REQUIRE(percent <= kRockPercent[rock] + 12);
        REQUIRE(percent > previous);
        previous = percent;
    }

    Params p = params(128, 1, 2, Mode::Vanilla, 8, 77);
    p.symmetric = true;
    const Result r = generate(p, vanilla());
    for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) {
        REQUIRE(r.map(x, y) == r.map(127 - x, y));
        REQUIRE(r.map(x, y) == r.map(x, 127 - y));
    }
    p.enemies = 1; p.allies = 0;
    const Result duel = generate(p, vanilla());
    for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) REQUIRE(duel.map(x, y) == duel.map(127 - x, 127 - y));

    // No spice at all when asked for none.
    p.spice = Spice::None;
    const Result dry = generate(p, vanilla());
    for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x)
        REQUIRE((dry.map(x, y) != Terrain_Spice && dry.map(x, y) != Terrain_ThickSpice && dry.map(x, y) != Terrain_SpiceBloom));
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
    p.rock = Rock::VeryHigh; p.spice = Spice::Low; p.symmetric = true; p.fixedSeed = true;
    p.credits = 7500; p.enemiesAllied = false; p.house = HOUSE_ORDOS;
    p.allySide = Side{"qBotHard", BaseSize::Large, 4, 3};
    p.enemySide = Side{"AIPlayerEasy", BaseSize::Outpost, kArmyRandom, kCityRandom};
    INIFile config(false, "");
    p.save(config);
    Params loaded;
    loaded.load(config);
    REQUIRE(loaded.mapSize == 192); REQUIRE(loaded.rock == Rock::VeryHigh); REQUIRE(loaded.spice == Spice::Low);
    REQUIRE(loaded.symmetric); REQUIRE(loaded.fixedSeed); REQUIRE(loaded.seed == 4000000000u);
    REQUIRE(loaded.house == HOUSE_ORDOS); REQUIRE(loaded.credits == 7500);
    REQUIRE(loaded.allies == 2); REQUIRE(loaded.enemies == 4); REQUIRE_FALSE(loaded.enemiesAllied);
    REQUIRE(loaded.mode == Mode::DuneCity); REQUIRE(loaded.techLevel == 6);
    REQUIRE(loaded.allySide.playerClass == "qBotHard"); REQUIRE(loaded.allySide.base == BaseSize::Large);
    REQUIRE(loaded.allySide.army == 4); REQUIRE(loaded.allySide.city == 3);
    REQUIRE(loaded.enemySide.playerClass == "AIPlayerEasy"); REQUIRE(loaded.enemySide.base == BaseSize::Outpost);
    REQUIRE(loaded.enemySide.army == kArmyRandom); REQUIRE(loaded.enemySide.city == kCityRandom);

    // Corrupt values clamp instead of escaping into the generator.
    INIFile bad(false, "");
    bad.setIntValue("Random Skirmish", "Map Size", 100);
    bad.setIntValue("Random Skirmish", "Tech Level", 42);
    bad.setIntValue("Random Skirmish", "Rock", 17);
    bad.setStringValue("Random Skirmish", "Seed", "-5");
    Params fallback;
    fallback.load(bad);
    fallback.clamp(8);
    REQUIRE(fallback.mapSize == 96);
    REQUIRE(fallback.techLevel == 9);
    REQUIRE(fallback.rock == Rock::VeryHigh);
    REQUIRE(fallback.seed == Params().seed);
}
