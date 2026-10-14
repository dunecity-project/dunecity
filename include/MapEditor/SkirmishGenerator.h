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

#ifndef SKIRMISHGENERATOR_H
#define SKIRMISHGENERATOR_H

#include <MapEditor/MapData.h>
#include <DataTypes.h>
#include <data.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class INIFile;
class ObjectData;
class Random;

/**
    Random Skirmish: a seeded generator that turns a handful of menu choices into a complete
    custom map (terrain, start plateaus, pre-built bases, armies and DuneCity cities) in the
    same INI format the map editor saves, plus the house/team roster to start it with.

    Everything in this namespace except makeCatalogue() is pure: no engine globals, no
    floating point and no wall clock, so the same seed and parameters always produce the same
    INI text on every machine. makeCatalogue() is the only engine-facing piece; it reads the
    live ObjectData so prices, power, tech levels and faction availability are the ones the
    match will actually use.
*/
namespace SkirmishGenerator {

enum class Rock { Low, Medium, High, VeryHigh };
enum class Spice { None, Low, Medium, High, VeryHigh };
enum class BaseSize { None, Outpost, Small, Medium, Large, Fortress };
enum class Mode { Vanilla, DuneCity };
enum class Role { Human, Ally, Enemy };

constexpr std::array<int, 6> kMapSizes{64, 96, 128, 160, 192, 256};
/// Target share (percent) of rock + mountain terrain per Rock choice.
constexpr std::array<int, 4> kRockPercent{15, 25, 35, 50};
/// Target share (percent) of spice terrain (thick spice and blooms included) per Spice choice.
/// Very high matches the map editor's default random map (7 fields, about 24%).
constexpr std::array<int, 5> kSpicePercent{0, 6, 12, 18, 24};
/// Every start's clean core of rock grows an irregular rim between these widths (tiles).
constexpr int kRimMin = 2;
constexpr int kRimMax = 7;
/// Tiles kept between every Industrial lot and every Residential lot of a city.
constexpr int kIndustrialBuffer = 5;
/// Wind traps that top up nuclear power at most; a larger remainder takes another reactor.
constexpr int kMaxTopUpTraps = 4;
/// Army budgets in credits; index kArmyRandom draws uniformly from [kArmyRandomMin, kArmyRandomMax].
constexpr std::array<int, 6> kArmyBudgets{0, 1000, 2500, 5000, 10000, 20000};
constexpr int kArmyRandom = 6;
constexpr int kArmyRandomMin = 1000;
constexpr int kArmyRandomMax = 20000;
/// Displayed city population targets; index kCityRandom draws uniformly from [kCityRandomMin, kCityRandomMax].
constexpr std::array<int, 5> kCityTargets{0, 1000, 5000, 15000, 40000};
constexpr int kCityRandom = 5;
constexpr int kCityRandomMin = 1000;
constexpr int kCityRandomMax = 40000;
constexpr std::array<int, 7> kCreditPresets{1000, 2000, 3000, 5000, 7500, 10000, 20000};
constexpr int kMaxAllies = 3;
constexpr int kMinEnemies = 1;
constexpr int kMaxEnemies = 5;
/// Seeds tried (seed, seed+1, ...) before giving up on a ground-connected map.
constexpr int kMaxTerrainAttempts = 8;
/// No unit type may take more than this share of an army budget (raised to 100/n when
/// fewer than three types are available, so a budget can still be spent).
constexpr int kArmyTypeCapPercent = 40;

/// Choices for one opposing side (all allies share one, all enemies share one).
struct Side {
    std::string playerClass = "qBotMedium";
    BaseSize base = BaseSize::Medium;
    int army = 3;   ///< index into kArmyBudgets, or kArmyRandom
    int city = 0;   ///< index into kCityTargets, or kCityRandom (DuneCity mode only)
};

struct Params {
    int mapSize = 128;
    Rock rock = Rock::Medium;
    Spice spice = Spice::Medium;
    bool symmetric = false;
    uint32_t seed = 1;
    bool fixedSeed = false;         ///< menu only: keep the seed instead of rolling a new one
    int house = HOUSE_INVALID;      ///< HOUSE_INVALID = random
    int credits = 3000;
    int allies = 0;
    int enemies = 1;
    bool enemiesAllied = true;
    Side allySide;
    Side enemySide;
    /// The player's own start: base, army and city like any side. Its playerClass is the AI
    /// partner sharing the player's house ("" = none, e.g. "qBotSupportMedium" or "qBotHard").
    Side playerSide{"", BaseSize::Medium, 3, 0};
    Mode mode = Mode::Vanilla;      ///< DuneCity when the chosen mod runs the city sim
    std::string mod = "vanilla";    ///< menu only: the mod the match is played with
    int techLevel = 9;              ///< everything unlocked by default

    /// Clamp every field to its legal range; @a houseCount is the number of houses the
    /// engine can field in a custom game (player + allies + enemies never exceed it).
    void clamp(int houseCount);
    /// Last-used parameters, section "Random Skirmish" of the configuration file.
    void load(const INIFile& config);
    void save(INIFile& config) const;
};

/// What one house may field of one item, taken from ObjectData.
struct ItemRule {
    bool enabled = false;
    int builder = ItemID_Invalid;
    int price = 0;
    int power = 0;              ///< negative = produces power
    int techLevel = -1;
    std::vector<int> prerequisites;
};

struct Catalogue {
    std::vector<int> houses;                    ///< houses a custom game can field, menu order
    std::array<std::string, NUM_HOUSES> houseNames;
    std::array<std::string, Num_ItemID> itemNames;
    std::array<Coord, Num_ItemID> sizes;
    std::vector<std::array<ItemRule, NUM_HOUSES>> rules = std::vector<std::array<ItemRule, NUM_HOUSES>>(Num_ItemID);

    const ItemRule& rule(int item, int house) const { return rules[item][house]; }
    /// Lowest tech level at which @a house can produce @a item, counting its builder and
    /// prerequisites; -1 if never. The Construction Yard is the start building (level 0).
    int unlockLevel(int item, int house) const;
    bool available(int item, int house, int techLevel) const;
};

/// Engine side: read prices, power, tech levels and availability from loaded ObjectData.
Catalogue makeCatalogue(const ObjectData& data, bool cityMode);

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

/// A [STRUCTURES] entry. Concrete, Wall and Road are tile entries (GEN###), the rest ID###.
struct Placement {
    int house = HOUSE_INVALID;
    int item = ItemID_Invalid;
    int x = 0, y = 0;
    int level = -1;             ///< zones only: starting development level 0..3
};

struct HouseSetup {
    int house = HOUSE_INVALID;
    int team = 1;
    Role role = Role::Enemy;
    std::string playerClass;
    std::string partnerClass;   ///< AI sharing the player's house ("" = none; Human only)
    BaseSize base = BaseSize::None;
    int armyBudget = 0;
    int armySpent = 0;
    int cityTarget = 0;         ///< displayed population requested
    int cityEstimate = 0;       ///< displayed population the placed city will report
    Coord start;
    Rect plateau;
    int skipped = 0;            ///< planned items that did not fit (crowded map)
};

struct Result {
    MapData map;
    uint32_t mapSeed = 0;       ///< seed that produced the connected terrain
    int attempts = 0;
    bool flattenedMountains = false;
    std::vector<HouseSetup> houses;     ///< [0] is the human player
    std::vector<Placement> tiles;       ///< concrete, walls, roads
    std::vector<Placement> structures;
    std::vector<Placement> units;
    std::string name;           ///< descriptive file stem
    std::string ini;
};

Result generate(const Params& params, const Catalogue& catalogue);

// ---- Building blocks, exposed for tests -------------------------------------------------

/// Buildings of a base tier for @a house at @a techLevel, power plants not included.
std::vector<int> baseTemplate(BaseSize size, int house, int techLevel, const Catalogue& catalogue);
/// Add power plants to @a items until production exceeds requirement (+ @a extraDraw) by a
/// tenth: nuclear plants plus at most kMaxTopUpTraps wind traps where @a house can build
/// reactors at @a techLevel, wind traps alone otherwise.
void addPower(std::vector<int>& items, int house, int techLevel, int extraDraw, const Catalogue& catalogue);
/// Produced minus required power of @a items (+ @a extraDraw required).
int powerBalance(const std::vector<int>& items, int house, int extraDraw, const Catalogue& catalogue);
/// Ground combat units @a house can field at @a techLevel, cheapest first.
std::vector<int> armyTypes(int house, int techLevel, const Catalogue& catalogue);
/// Spend at most @a budget on a seeded unit mix (each type capped, see kArmyTypeCapPercent).
std::vector<int> buyArmy(int budget, int house, int techLevel, const Catalogue& catalogue, Random& random);
/// True if every coordinate is reachable from the first by ground (mountains block).
bool groundConnected(const MapData& map, const std::vector<Coord>& points);
/// Displayed city population @a house reports for @a placements (zones at their level,
/// other city-role buildings at their initial occupancy).
int displayedPopulation(const std::vector<Placement>& placements, int house);

}

#endif // SKIRMISHGENERATOR_H
