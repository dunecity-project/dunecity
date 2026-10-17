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
    std::string label;          ///< as given: shape[+flags][/retreat|/noretreat][/weak]
    int retreat = -1;           ///< QuantBot::tacticalRetreatOverride: -1 the game's (on with Blob)
    int variant = 0;            ///< QuantBot::blobVariantOverride bits from +v (vehicle pace), +r (real fight), +k (keep damaged),
                                ///< +s (stage near the enemy), +e (stage only with local superiority)
    int force = 0;              ///< QuantBot::forceVariantOverride bits from the uppercase flags (+F, +C, +P, +H, +M, +L, +Q,
                                ///< +N, +B, +O, +W, +S, +T, +D, +E)
    bool weak = false;          ///< /weak: half of the starting army is removed (the defence scenario)
    bool aiPlayer = false;      ///< label "ai": the classic AI Player controls this house (vanilla matches only)
};

/// "house:shape[+flags][/variant...],house:...", houses by number, shapes by INI name (None, Blob
/// or a kept shape). Variants: /retreat turns the tactical fall-back on, /noretreat off (without
/// one the game's: on with Blob only); /weak halves the starting army. With allowAI the label "ai"
/// is the classic AI Player instead of a QuantBot (vanilla-match.py), and any number of sides
/// (two or more) may be given.
inline std::vector<Side> parseSides(const std::string& text, bool allowAI) {
    std::vector<Side> sides;
    std::istringstream input(text);
    std::string entry;
    while (std::getline(input, entry, ',')) {
        const auto colon = entry.find(':');
        if (colon == std::string::npos) throw std::runtime_error("Invalid duel side: " + entry);
        Side side;
        side.house = std::stoi(entry.substr(0, colon));
        side.label = entry.substr(colon + 1);
        if (allowAI && side.label == "ai") {
            side.aiPlayer = true;
            sides.push_back(side);
            continue;
        }
        std::string shape = side.label;
        auto slash = shape.find('/');
        if (slash != std::string::npos) {
            std::istringstream variants(shape.substr(slash + 1));
            shape = shape.substr(0, slash);
            std::string variant;
            while (std::getline(variants, variant, '/')) {
                if (variant == "retreat") side.retreat = 1;
                else if (variant == "noretreat") side.retreat = 0;
                else if (variant == "weak") side.weak = true;
                else throw std::runtime_error("Unknown duel variant: " + entry);
            }
        }
        const auto plus = shape.find('+');
        if (plus != std::string::npos) {
            for (const char flag : shape.substr(plus + 1)) {
                if (flag == 'v') side.variant |= QuantBot::kBlobVehiclePace;
                else if (flag == 'r') side.variant |= QuantBot::kBlobRealFight;
                else if (flag == 'k') side.variant |= QuantBot::kBlobKeepDamaged;
                else if (flag == 's') side.variant |= QuantBot::kBlobStage;
                else if (flag == 'e') side.variant |= QuantBot::kBlobSuperiority;
                else if (flag == 'F') side.force |= QuantBot::kForceFocusFire;
                else if (flag == 'C') side.force |= QuantBot::kForceConverge;
                else if (flag == 'P') side.force |= QuantBot::kForcePool;
                else if (flag == 'H') side.force |= QuantBot::kForceRecall;
                else if (flag == 'M') side.force |= QuantBot::kForceMuster;
                else if (flag == 'L') side.force |= QuantBot::kForceBigWaves;
                else if (flag == 'Q') side.force |= QuantBot::kForceSmallWaves;
                else if (flag == 'N') side.force |= QuantBot::kForceNoOverkill;
                else if (flag == 'B') side.force |= QuantBot::kForceBundle;
                else if (flag == 'O') side.force |= QuantBot::kForceBundle | QuantBot::kForceBundleOld;
                else if (flag == 'W') side.force |= QuantBot::kForceBundle | QuantBot::kForceBundleGate;
                else if (flag == 'S') side.force |= QuantBot::kForceSplit;
                else if (flag == 'T') side.force |= QuantBot::kForceFlank;
                else if (flag == 'D') side.force |= QuantBot::kForceStrengthReadiness;
                else if (flag == 'E') side.force |= QuantBot::kForceOpeningReadiness;
                else throw std::runtime_error("Unknown duel variant flag: " + entry);
            }
            shape = shape.substr(0, plus);
        }
        side.shape = QuantBotConfig::parseWaveFormation(shape, INT_MIN);
        if (side.shape == INT_MIN) throw std::runtime_error("Unknown duel wave formation: " + entry);
        sides.push_back(side);
    }
    for (size_t i = 0; i < sides.size(); ++i)
        for (size_t j = i + 1; j < sides.size(); ++j)
            if (sides[i].house == sides[j].house) throw std::runtime_error("Two sides share a house");
    if (sides.size() < 2 || (!allowAI && sides.size() != 2)) throw std::runtime_error("A duel needs two different houses");
    return sides;
}

/// BALANCE_DUEL_SIDES: the two sides of a generated-map duel (parseSides).
inline std::vector<Side> sidesFromEnv() {
    return parseSides(std::getenv("BALANCE_DUEL_SIDES") ? std::getenv("BALANCE_DUEL_SIDES") : "", false);
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

/// Gives each side's QuantBot its wave shape and its ablations (test-only), and halves a /weak
/// side's starting army: every second ground fighter by object id is removed before the first
/// cycle (the composition stays; the base, harvesters and MCVs are untouched).
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
                bot->forceVariantOverride = side.force;
                ++bots;
            }
        if (bots != (side.aiPlayer ? 0 : 1))
            throw std::runtime_error(side.aiPlayer ? "An AI Player side has a QuantBot" : "A duel house does not have exactly one QuantBot");
        if (!side.weak) continue;
        std::vector<Uint32> fighters;
        for (const auto* unit : unitList)
            if (unit->getOwner() == house && unit->getHealth() > 0 && unit->isAGroundUnit() && unit->canAttack()
                && unit->getItemID() != Unit_Saboteur && unit->getItemID() != Unit_MCV) fighters.push_back(unit->getObjectID());
        std::sort(fighters.begin(), fighters.end());
        int removed = 0, value = 0;
        for (size_t i = 1; i < fighters.size(); i += 2)
            if (auto* unit = dynamic_cast<UnitBase*>(game.getObjectManager().getObject(fighters[i]))) {
                value += FormationWaveMeasure::priceOf(unit);
                unit->destroy();
                ++removed;
            }
        SDL_Log("FORMATION_DUEL_WEAK: house=%d removed=%d value=%d of=%zu", side.house, removed, value, fighters.size());
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
        QuantBot::ForceStats force;
        for (const auto& player : house->getPlayerList())
            if (const auto* bot = dynamic_cast<const QuantBot*>(player.get())) { stats = bot->blobStats; force = bot->forceStats; }
        const auto home = FormationWaveMeasure::homeTrade()[side.house];
        SDL_Log("FORMATION_DUEL_HOUSE: house=%d shape=%s alive=%d kill_value=%d loss_value=%d structures_destroyed=%d "
                "units_destroyed=%d structures=%d army_value=%d army_units=%d parties=%d gathered=%d gather_timeouts=%d "
                "releases=%d reforms=%d fallbacks=%d fallback_returns=%d legs=%d dissolved=%d stalls=%d repairs=%d "
                "home_kill=%d home_loss=%d focus=%d converges=%d pooled=%d recalled=%d musters=%d muster_releases=%d "
                "stage_holds=%d staged=%d bundle_orders=%d gate_holds=%d gate_timeouts=%d splits=%d split_releases=%d "
                "split_timeouts=%d full_readiness=%d",
                side.house, side.label.c_str(), int(house->isAlive()), house->getKillValue(),
                house->getLossValue(), house->getNumDestroyedStructures(), house->getNumDestroyedUnits(), structures, army, units,
                stats.parties, stats.gathered, stats.gatherTimeouts, stats.releases, stats.reforms, stats.fallbacks,
                stats.fallbackReturns, stats.legs, stats.dissolved, stats.stalls, stats.repairs,
                home.kill, home.loss, force.focusSwitches, force.converges, force.pooled, force.recalled, force.musters,
                force.musterReleases, force.stageHolds, force.staged, force.bundleOrders, force.gateHolds, force.gateTimeouts,
                force.splits, force.splitReleases, force.splitTimeouts, force.fullReadiness);
    }
    SDL_Log("FORMATION_DUEL_RESULT: winner=%d end_s=%.1f", alive == 1 ? winner : -1,
            FormationWaveMeasure::seconds(game.getGameCycleCount()));
}

} // namespace FormationDuel
