// Included only by campaign-balance-probe.inc (run-campaign-balance.py --duel-map-size, driven by
// tests/ai/formation-duel.py).
//
// Formation duels (docs/formations.md, Formation research): a seeded Random Skirmish 1v1 on a
// mirrored (point-symmetric) map, both starts with a Fortress base and a 20,000-credit army,
// two QuantBots whose attack waves march in different shapes. The shape of each side is the
// test-only QuantBot::waveFormationOverride; nothing else differs from a real custom game.
// The match runs until one house is eliminated or the time limit; the wave measurement
// (formation-wave-measure.inc) runs alongside, and the end reports each side's result.
#include <Menu/RandomSkirmishMenu.h>
#include <MapEditor/SkirmishGenerator.h>
#include <players/QuantBot.h>

#include <sstream>
#include <string>
#include <vector>

namespace FormationDuel {

struct Side {
    int house = HOUSE_INVALID;
    int shape = QuantBotConfig::kWaveFormationNone;
    std::string label;          ///< as given: shape[+flags][/retreat|/noretreat]
    int retreat = -1;           ///< QuantBot::tacticalRetreatOverride: -1 the game's (on with Blob)
    int variant = 0;            ///< QuantBot::blobVariantOverride bits from +v (vehicle pace), +r (real fight), +k (keep damaged)
};

/// BALANCE_DUEL_SIDES: "house:shape[/variant],house:shape[/variant]", houses by number, shapes
/// by INI name (None, Blob or a kept shape). Variants (ablations of Blob waves): /retreat turns
/// the tactical fall-back on, /noretreat off; without one the game's (on with Blob only).
inline std::vector<Side> sidesFromEnv() {
    std::vector<Side> sides;
    std::istringstream input(std::getenv("BALANCE_DUEL_SIDES") ? std::getenv("BALANCE_DUEL_SIDES") : "");
    std::string entry;
    while (std::getline(input, entry, ',')) {
        const auto colon = entry.find(':');
        if (colon == std::string::npos) throw std::runtime_error("Invalid duel side: " + entry);
        Side side;
        side.house = std::stoi(entry.substr(0, colon));
        side.label = entry.substr(colon + 1);
        std::string shape = side.label;
        const auto slash = shape.find('/');
        if (slash != std::string::npos) {
            const std::string variant = shape.substr(slash + 1);
            shape = shape.substr(0, slash);
            if (variant == "retreat") side.retreat = 1;
            else if (variant == "noretreat") side.retreat = 0;
            else throw std::runtime_error("Unknown duel variant: " + entry);
        }
        const auto plus = shape.find('+');
        if (plus != std::string::npos) {
            for (const char flag : shape.substr(plus + 1)) {
                if (flag == 'v') side.variant |= QuantBot::kBlobVehiclePace;
                else if (flag == 'r') side.variant |= QuantBot::kBlobRealFight;
                else if (flag == 'k') side.variant |= QuantBot::kBlobKeepDamaged;
                else throw std::runtime_error("Unknown duel Blob variant flag: " + entry);
            }
            shape = shape.substr(0, plus);
        }
        side.shape = QuantBotConfig::parseWaveFormation(shape, INT_MIN);
        if (side.shape == INT_MIN) throw std::runtime_error("Unknown duel wave formation: " + entry);
        sides.push_back(side);
    }
    if (sides.size() != 2 || sides[0].house == sides[1].house) throw std::runtime_error("A duel needs two different houses");
    return sides;
}

/// The generated map and the two-QuantBot roster (teams 1 and 2, the starts assigned by the
/// match seed as in any custom game).
inline GameInitSettings makeInit(int mapSize, Uint32 mapSeed, const std::vector<Side>& sides, const std::string& playerClass,
                                 const SettingsClass::GameOptionsClass& options) {
    using namespace SkirmishGenerator;
    Params params;
    params.multiplayer = true;
    params.starts = 2;
    params.mapSize = mapSize;
    params.seed = mapSeed;
    params.symmetric = true;
    params.mode = Mode::Vanilla;
    params.mod = "vanilla";
    params.techLevel = 9;
    // Fortress base and the 20,000-credit army (kArmyBudgets[5]) on both starts.
    params.startSide = SkirmishGenerator::Side{"", BaseSize::Fortress, 5, 0};
    const Result result = generate(params, RandomSkirmish::loadCatalogue(Mode::Vanilla));
    int skipped = 0;
    for (const HouseSetup& house : result.houses) skipped += house.skipped;
    SDL_Log("FORMATION_DUEL_MAP: name=%s size=%d seed=%u map_seed=%u attempts=%d flattened=%d skipped=%d army=%d/%d",
            result.name.c_str(), mapSize, mapSeed, result.mapSeed, result.attempts, int(result.flattenedMountains), skipped,
            result.houses.empty() ? 0 : result.houses[0].armySpent, result.houses.empty() ? 0 : result.houses[0].armyBudget);
    GameInitSettings init(result.name, result.ini, false, options);
    for (size_t i = 0; i < sides.size(); ++i) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(sides[i].house), static_cast<int>(i) + 1);
        info.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(sides[i].house)), playerClass});
        init.addHouseInfo(info);
    }
    return init;
}

/// Gives each side's QuantBot its wave shape and its tactical-retreat ablation (test-only).
inline void applyShapes(Game& game, const std::vector<Side>& sides) {
    for (const Side& side : sides) {
        House* house = game.getHouse(side.house);
        if (!house) throw std::runtime_error("A duel house is missing");
        int bots = 0;
        for (const auto& player : house->getPlayerList())
            if (auto* bot = dynamic_cast<QuantBot*>(player.get())) {
                bot->waveFormationOverride = side.shape;
                bot->tacticalRetreatOverride = side.retreat;
                bot->blobVariantOverride = side.variant;
                ++bots;
            }
        if (bots != 1) throw std::runtime_error("A duel house does not have exactly one QuantBot");
    }
}

/// True once fewer than two of the sides are still alive.
inline bool decided(Game& game, const std::vector<Side>& sides) {
    int alive = 0;
    for (const Side& side : sides) alive += game.getHouse(side.house) && game.getHouse(side.house)->isAlive() ? 1 : 0;
    return alive < 2;
}

inline void report(Game& game, const std::vector<Side>& sides) {
    int winner = -1, alive = 0;
    for (const Side& side : sides) {
        const House* house = game.getHouse(side.house);
        int army = 0, units = 0, structures = 0;
        for (const auto* unit : unitList)
            if (unit->getOwner() == house && unit->getHealth() > 0 && unit->canAttack()) { army += FormationWaveMeasure::priceOf(unit); ++units; }
        for (const auto* structure : structureList)
            if (structure->getOwner() == house && structure->getHealth() > 0) ++structures;
        if (house->isAlive()) { ++alive; winner = side.house; }
        QuantBot::BlobStats stats;
        for (const auto& player : house->getPlayerList())
            if (const auto* bot = dynamic_cast<const QuantBot*>(player.get())) stats = bot->blobStats;
        SDL_Log("FORMATION_DUEL_HOUSE: house=%d shape=%s alive=%d kill_value=%d loss_value=%d structures_destroyed=%d "
                "units_destroyed=%d structures=%d army_value=%d army_units=%d parties=%d gathered=%d gather_timeouts=%d "
                "releases=%d reforms=%d fallbacks=%d fallback_returns=%d legs=%d dissolved=%d stalls=%d repairs=%d",
                side.house, side.label.c_str(), int(house->isAlive()), house->getKillValue(),
                house->getLossValue(), house->getNumDestroyedStructures(), house->getNumDestroyedUnits(), structures, army, units,
                stats.parties, stats.gathered, stats.gatherTimeouts, stats.releases, stats.reforms, stats.fallbacks,
                stats.fallbackReturns, stats.legs, stats.dissolved, stats.stalls, stats.repairs);
    }
    SDL_Log("FORMATION_DUEL_RESULT: winner=%d end_s=%.1f", alive == 1 ? winner : -1,
            FormationWaveMeasure::seconds(game.getGameCycleCount()));
}

} // namespace FormationDuel
