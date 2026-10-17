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
    {Structure_Airport, "Airport", "Airport", {3,3}}, {Structure_NuclearPlant, "Nuclear Plant", "Nuclear", {3,3}},
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

} // namespace

TEST_CASE("Random skirmish: pinned seeds give the same map on every platform", "[skirmish][generator][determinism]") {
    // Digests of the 1.0.845 macOS generator, where clang evaluates function arguments left to
    // right. MSVC evaluates them right to left, so Coord(random.rand(..), random.rand(..)) built
    // a different map from the same seed on Windows; macOS, the browser and Windows must agree.
    struct Pin { int size; uint32_t seed; Mode mode; uint64_t terrain, ini; };
    const Pin pins[] = {
        {64, 1u, Mode::Vanilla, 0x83c3c700c756d591ull, 0xbf83cb4fc15448faull},
        {64, 1u, Mode::DuneCity, 0x4331038d9e4e6229ull, 0xae8359fdcf7f5154ull},
        {64, 7101u, Mode::Vanilla, 0x1f4a047aa11fec05ull, 0x2a56672130a7eaa2ull},
        {64, 7101u, Mode::DuneCity, 0xaf295ea1269c0d65ull, 0x8811eb11bf1fbe5dull},
        {64, 7202u, Mode::Vanilla, 0x94f4a45973d513e6ull, 0xe6eed494d2659e93ull},
        {64, 7202u, Mode::DuneCity, 0xd4013a0680085eeaull, 0x510d6dedde40027bull},
        {64, 7303u, Mode::Vanilla, 0x57d49737c3a65b55ull, 0xb2c50f0efb1d7deeull},
        {64, 7303u, Mode::DuneCity, 0xa8cb2f47655bd3c1ull, 0xf6fdb0c72eb9865full},
        {64, 4000000000u, Mode::Vanilla, 0x61c7cd10b13acaebull, 0xd981122ff4235673ull},
        {64, 4000000000u, Mode::DuneCity, 0x967b3520c6a00460ull, 0xbd265224d27672ddull},
        {128, 1u, Mode::Vanilla, 0x2b644ba5152c9eadull, 0x5b4437232501fc25ull},
        {128, 1u, Mode::DuneCity, 0x1718f1fcb46416c5ull, 0xbf5c56369d3d72d4ull},
        {128, 7101u, Mode::Vanilla, 0x16be56325988f5fdull, 0x428d662a502c333full},
        {128, 7101u, Mode::DuneCity, 0x6cab022adbcfb22dull, 0x9c8f49445c1b001cull},
        {128, 7202u, Mode::Vanilla, 0x42c2676e49455511ull, 0x81d3eb09c0b0c585ull},
        {128, 7202u, Mode::DuneCity, 0xa221b75fd4d35734ull, 0x824dd4f62dbde9a2ull},
        {128, 7303u, Mode::Vanilla, 0x5ab457a674fac1a9ull, 0x6f11398377eedb1dull},
        {128, 7303u, Mode::DuneCity, 0x09c4f7bccae6a8f9ull, 0x5cececd4826bdf92ull},
        {128, 4000000000u, Mode::Vanilla, 0x08cee2efe7c6286full, 0x68e7469d8e83bf34ull},
        {128, 4000000000u, Mode::DuneCity, 0x448eb306f7cae475ull, 0xf7c64dcda9ce6d1aull},
        {192, 1u, Mode::Vanilla, 0xa168a478eef24995ull, 0x35cb2f70e07a1550ull},
        {192, 1u, Mode::DuneCity, 0x26d45d8bd26a5cc1ull, 0x491f09310c92fd06ull},
        {192, 7101u, Mode::Vanilla, 0x01ffa14de9f47df9ull, 0x7a01e3699ff7c0efull},
        {192, 7101u, Mode::DuneCity, 0xefcc1416919ddff1ull, 0x540067a35eea6fa7ull},
        {192, 7202u, Mode::Vanilla, 0x81eef02bb000223full, 0x680dc741e425025bull},
        {192, 7202u, Mode::DuneCity, 0xe41e988186f9dfc5ull, 0xfbc6ff417bda8142ull},
        {192, 7303u, Mode::Vanilla, 0x6bc5ee8f43404f6dull, 0xe98bd11de45c4f6aull},
        {192, 7303u, Mode::DuneCity, 0x7a6ab4ab4ab1c89dull, 0xd7af6c741d6d14d0ull},
        {192, 4000000000u, Mode::Vanilla, 0x0f69ae590efaf89cull, 0x242d2afda6c4af5full},
        {192, 4000000000u, Mode::DuneCity, 0x6b12df167779cabdull, 0xcc79c837501cc0e2ull},
    };
    for(const Pin& pin : pins) {
        Params p = params(pin.size, 1, 2, pin.mode, 8, pin.seed);
        p.symmetric = pin.seed % 2 == 1;            // odd seeds mirror the starts
        p.allySide.city = p.enemySide.city = 2;
        const Result r = generate(p, pin.mode == Mode::DuneCity ? city() : vanilla());
        CAPTURE(pin.size, pin.seed, pin.mode == Mode::DuneCity);
        CHECK(terrainDigest(r.map) == pin.terrain);
        CHECK(fnv1a(r.ini) == pin.ini);
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

    // Rock share follows the preset, all the way to 100%.
    int previous = 0;
    for(int rock = 0; rock < static_cast<int>(kRockPercent.size()); ++rock) {
        Params p = params(128, 0, 1, Mode::Vanilla, 8, 11);
        p.rock = static_cast<Rock>(rock);
        const Result r = generate(p, vanilla());
        int tiles = 0;
        for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x)
            tiles += r.map(x, y) == Terrain_Rock || r.map(x, y) == Terrain_Mountain;
        const int percent = tiles * 100 / (128 * 128);
        INFO("rock preset " << rock << " share " << percent);
        REQUIRE(percent >= kRockPercent[rock]);
        REQUIRE(percent <= std::min(100, kRockPercent[rock] + 12));
        REQUIRE(percent > previous);
        if(kRockPercent[rock] == 100) REQUIRE(tiles == 128 * 128);    // no sand, so no spice either
        std::vector<Coord> starts;
        for(const auto& h : r.houses) starts.push_back(h.start);
        REQUIRE(groundConnected(r.map, starts));
        requireLegal(r, vanilla());
        previous = percent;
    }
    // A crowded, mirrored all-rock map stays symmetric, connected and legal.
    {
        Params p = params(128, 3, 4, Mode::DuneCity, 9, 5);
        p.rock = Rock::All; p.symmetric = true;
        const Result r = generate(p, city());
        for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) REQUIRE(r.map(x, y) == r.map(127 - x, 127 - y));
        std::vector<Coord> starts;
        for(const auto& h : r.houses) starts.push_back(h.start);
        REQUIRE(groundConnected(r.map, starts));
        requireLegal(r, city());
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
    INIFile config(false, std::string());
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
    INIFile bad(false, std::string());
    bad.setIntValue("Random Skirmish", "Map Size", 100);
    bad.setIntValue("Random Skirmish", "Tech Level", 42);
    bad.setIntValue("Random Skirmish", "Rock", 17);
    bad.setStringValue("Random Skirmish", "Seed", "-5");
    Params fallback;
    fallback.load(bad);
    fallback.clamp(8);
    REQUIRE(fallback.mapSize == 96);
    REQUIRE(fallback.techLevel == 9);
    REQUIRE(fallback.rock == Rock::All);
    REQUIRE(fallback.seed == Params().seed);
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
    // 150 seeded configurations over every size, mode, mirror, rock, spice, base, army and city
    // choice. Crowded small maps may skip items (counted in HouseSetup::skipped); they must
    // never place anything illegally, cut a start off, or need the mountains levelled.
    Random random(99);
    for(int i = 0; i < 150; ++i) {
        Params p = params(kMapSizes[random.rand(0, 5)], random.rand(0, 3), random.rand(1, 4),
                          random.rand(0, 1) ? Mode::DuneCity : Mode::Vanilla, random.rand(1, 9), random.rand());
        p.symmetric = random.rand(0, 1);
        p.rock = static_cast<Rock>(random.rand(0, 3));
        p.spice = static_cast<Spice>(random.rand(0, 4));
        for(Side* side : {&p.allySide, &p.enemySide, &p.playerSide}) {
            side->base = static_cast<BaseSize>(random.rand(0, 5));
            side->city = random.rand(0, 5);
            side->army = random.rand(0, 6);
        }
        const Catalogue& c = p.mode == Mode::DuneCity ? city() : vanilla();
        const Result r = generate(p, c);
        INFO("configuration " << i << " size " << p.mapSize << " houses " << r.houses.size() << " mirrored " << p.symmetric);
        requireLegal(r, c);
        REQUIRE_FALSE(r.flattenedMountains);
        std::vector<Coord> starts;
        for(const auto& h : r.houses) starts.push_back(h.start);
        REQUIRE(groundConnected(r.map, starts));
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
