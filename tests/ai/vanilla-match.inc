// Included only by campaign-balance-probe.inc (run-campaign-balance.py --match-sides, driven by
// tests/ai/vanilla-match.py).
//
// Vanilla matches from a Construction Yard start (docs/formations.md, "Scenario tests"): a bundled
// map file as the lobby would start it (its own MCV/yard, units and credits, the lobby's default
// options), every house on its own team and controlled by a QuantBot with its test-only force
// flags (FormationDuel::parseSides labels) or by the classic AI Player (label "ai"). The match
// runs until one house is left or the time limit, alongside the wave measurement
// (formation-wave-measure.inc). Once a game minute every playing house is sampled
// (EliminationMatch::sample: credits, army, harvesters, structures, factories), each house's
// elimination is logged with its cycle, and the end reports every side (FormationDuel::report).
// Read-only apart from the overrides set before the first cycle.
#include <units/MCV.h>

namespace VanillaMatch {

struct State {
    std::vector<FormationDuel::Side> sides;
    std::map<int, Uint32> eliminated;   ///< house -> first cycle it was no longer alive
    FormationDuel::GenomePhases genome;
};

/// BALANCE_MATCH_SIDES ("house:label,...", slot order), empty when the variable is unset.
inline std::vector<FormationDuel::Side> sidesFromEnv() {
    const char* text = std::getenv("BALANCE_MATCH_SIDES");
    return text ? FormationDuel::parseSides(text, true) : std::vector<FormationDuel::Side>{};
}

/// The controller class of a side: the QuantBot of the enemy difficulty, or the AI Player of it.
inline std::string playerClass(const FormationDuel::Side& side, const std::string& difficulty) {
    if (side.aiPlayer)
        return difficulty == "hard" ? "AIPlayerHard" : difficulty == "medium" ? "AIPlayerMedium" : "AIPlayerEasy";
    return difficulty == "hard" ? "qBotHard" : difficulty == "medium" ? "qBotMedium"
        : difficulty == "brutal" ? "qBotBrutal" : "qBotEasy";
}

/// A generated free-for-all (vanilla-match.py --skirmish, docs/formations.md "Free-for-all: the
/// army deploys"): a multiplayer Random Skirmish map of \a mapSize from \a mapSeed with one start
/// per side, each a Fortress with the 20,000-credit army and, in the DuneCity mod, the 40,000
/// city, Sand/Rock/Mountain 65/30/5 and Spice Medium, not mirrored - Stefan's 4P DuneCity FFA
/// ([Random Skirmish] Base 5, Army 5, City 4). Every side its own team; the engine assigns the
/// starts from the match seed as in any custom game.
inline GameInitSettings makeSkirmishInit(int mapSize, Uint32 mapSeed, const std::vector<FormationDuel::Side>& sides,
                                         const std::string& difficulty, const std::string& mod,
                                         const SettingsClass::GameOptionsClass& options) {
    using namespace SkirmishGenerator;
    const bool genome = std::getenv("BALANCE_GENOME_PHASE_DIR") != nullptr;
    const bool city = genome ? RandomSkirmish::modeForMod(mod) == Mode::DuneCity : mod == "dunecity";
    Params params;
    params.multiplayer = true;
    params.starts = static_cast<int>(sides.size());
    params.mapSize = mapSize;
    params.seed = mapSeed;
    params.symmetric = false;
    params.sandPercent = 65; params.rockPercent = 30; params.mountainPercent = 5;
    params.spice = Spice::Medium;
    params.mode = city ? Mode::DuneCity : Mode::Vanilla;
    params.mod = mod;
    params.techLevel = 9;
    params.startSide = SkirmishGenerator::Side{"", BaseSize::Fortress, 5, city ? 4 : 0};
    if (genome) {
        const INIFile inputs(std::string(std::getenv("BALANCE_GENOME_PHASE_DIR")) + "/schedule.ini");
        const int base = inputs.getIntValue("Map", "base", 5);
        params.startSide.army = inputs.getIntValue("Map", "army", 5);
        params.startSide.city = inputs.getIntValue("Map", "city", city ? 4 : 0);
        params.techLevel = inputs.getIntValue("Map", "tech", 9);
        const int spice = inputs.getIntValue("Map", "spice", 2);
        if (spice < 0 || spice > 4) throw std::runtime_error("Invalid genome spice level");
        params.spice = static_cast<Spice>(spice);
        params.researchLab = options.researchLab;
        if (base < 0 || base > 5 || params.startSide.army < 0 || params.startSide.army > 6
            || params.startSide.city < 0 || params.startSide.city > 5
            || params.techLevel < 1 || params.techLevel > 9 || (!city && params.startSide.city))
            throw std::runtime_error("Invalid genome skirmish settings");
        params.startSide.base = static_cast<BaseSize>(base);
    }
    const Result result = generate(params, RandomSkirmish::loadCatalogue(params.mode));
    if (genome) {
        int skipped = 0;
        for (const auto& house : result.houses) skipped += house.skipped;
        SDL_Log("GENOME_MAP: mod=%s size=%d seed=%u map_seed=%u attempts=%d skipped=%d starts=%d",
            mod.c_str(), mapSize, mapSeed, result.mapSeed, result.attempts, skipped, params.starts);
    }
    SDL_Log("VANILLA_MATCH_SKIRMISH: name=%s size=%d map_seed=%u attempts=%d starts=%d mode=%s",
            result.name.c_str(), mapSize, result.mapSeed, result.attempts, params.starts, city ? "dunecity" : "vanilla");
    GameInitSettings init(result.name, result.ini, false, options);
    for (size_t i = 0; i < sides.size(); ++i) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(sides[i].house), static_cast<int>(i) + 1);
        info.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(sides[i].house)), playerClass(sides[i], difficulty)});
        init.addHouseInfo(info);
    }
    return init;
}

/// Record each defeated playing house once, including a death on the game's final cycle.
inline void recordEliminations(Game& game, State& state) {
    const Uint32 now = game.getGameCycleCount();

    for (const auto& side : state.sides) {
        const House* house = game.getHouse(side.house);
        // An absent house is unknown, not an observed elimination. Natives are never entrants.
        if (!house || house->isNatives() || state.eliminated.count(side.house) || house->isAlive()) continue;
        state.eliminated[side.house] = now;
        SDL_Log("VANILLA_MATCH_ELIMINATED: house=%d label=%s cycle=%u end_s=%.1f", side.house, side.label.c_str(), now,
                FormationWaveMeasure::seconds(now));
    }
}

// Supplemental native stocks/ledgers, not detectors. These fields are also used
// by the canonical baseline scorecard; the ordinary no-genome match is unchanged.
inline void genomeSample(Game& game) {
    const Uint32 cycle = game.getGameCycleCount();
    std::array<std::array<int, ItemID_LastID + 1>, NUM_HOUSES> actual{};
    for (const auto* unit : unitList)
        if (unit->getOwner() && unit->getHealth() > 0)
            ++actual[unit->getOwner()->getHouseID()][unit->getItemID()];
    for (const House* house : EliminationMatch::playingHouses(game)) {
        const int h = house->getHouseID();
        SDL_Log("GENOME_STOCK: minute=%u cycle=%u house=%d refineries=%d zone_residential=%d zone_industrial=%d "
                "zone_commercial=%d tech_level=%d light=%d light_built=%d palace=%d palace_built=%d",
            cycle / MILLI2CYCLES(60000), cycle, h, house->getNumItems(Structure_Refinery),
            house->getNumItems(Structure_ZoneResidential), house->getNumItems(Structure_ZoneIndustrial),
            house->getNumItems(Structure_ZoneCommercial), house->getTechLevel(),
            house->getNumItems(Structure_LightFactory), house->getNumBuiltItems(Structure_LightFactory),
            house->getNumItems(Structure_Palace), house->getNumBuiltItems(Structure_Palace));
        for (int item = Unit_FirstID; item <= Unit_LastID; ++item)
            if (isUnit(item))
                SDL_Log("GENOME_UNIT: minute=%u cycle=%u house=%d item=%d built=%d lost=%d actual=%d live_value=%lld",
                    cycle / MILLI2CYCLES(60000), cycle, h, item, house->getNumBuiltItems(item),
                    house->getNumLostItems(item), actual[h][item],
                    static_cast<long long>(actual[h][item]) * game.objectData.data[item][h].price);
    }
}

/// Once per cycle: per-minute samples and newly eliminated houses. True once one house is left.
inline bool tick(Game& game, State& state) {
    const Uint32 now = game.getGameCycleCount();
    EliminationMatch::crushCount.observe();
    if (state.genome.enabled) {
        FormationDuel::applyGenomePhase(game, state.genome);
        DuneCityAudit::observe(game);
    }
    if (now % MILLI2CYCLES(60000) == 0) {
        EliminationMatch::sample(game);
        if (state.genome.enabled) genomeSample(game);
    }
    recordEliminations(game, state);
    return FormationDuel::decided(game, state.sides);
}

inline void run(Game& game, Uint32 end, const std::vector<FormationDuel::Side>& sides) {
    State state{sides, {}, {}};
    FormationDuel::applyShapes(game, sides);
    state.genome = FormationDuel::loadGenomePhases(game, sides);
    if (state.genome.enabled) {
        const std::string directory(std::getenv("BALANCE_GENOME_PHASE_DIR"));
        const INIFile inputs(directory + "/schedule.ini");
        if (inputs.getBoolValue("Map", "deploy_mcvs", false)) {
            std::vector<MCV*> mcvs;
            for (auto* unit : unitList)
                if (auto* mcv = dynamic_cast<MCV*>(unit)) mcvs.push_back(mcv);
            for (auto* mcv : mcvs)
                if (!mcv->doDeploy()) throw std::runtime_error("Genome Construction Yard start cannot deploy");
        }
        const auto& init = game.getGameInitSettings();
        const auto checksums = ModManager::instance().getEffectiveChecksums();
        GameStateDigest::Hasher mapHash;
        for (const unsigned char byte : init.getFiledata()) mapHash.mixUint8(byte);
        SDL_Log("GENOME_SETUP: config_hash=%s options_hash=%s map_hash=%016llx content_hash=%s "
                "object_data_hash=%s city_sim=%d observing=%d",
            getQuantBotConfig().getConfigHash().c_str(), init.getGameOptions().getHash().c_str(),
            static_cast<unsigned long long>(mapHash.value()), checksums.combined.c_str(),
            checksums.objectData.c_str(), int(game.isCitySimEnabled()), int(game.isObserving()));
        std::ofstream(directory + "/map.ini", std::ios::binary) << init.getFiledata();
        INIFile options(true, "Complete effective genome-match options");
        writeGameOptionsToConfig(options, "Game Options", init.getGameOptions());
        if (!options.saveChangesTo(directory + "/effective-options.ini"))
            throw std::runtime_error("Cannot retain genome game options");
        FormationDuel::applyGenomePhase(game, state.genome);
    }
    FormationWaveMeasure::run(game, end, [&]() {
        ProbeDigest::periodic(game);
        return tick(game, state);
    });
    // House::lose may set game.finished in updateGameState. The wave loop then exits before
    // invoking tick again. Record the final defeat in raw elimination/placement rows too;
    // placings() already repairs this omission when producing the standard report.
    recordEliminations(game, state);
    EliminationMatch::sample(game, true);
    if (state.genome.enabled) genomeSample(game);
    FormationDuel::report(game, sides);
}

} // namespace VanillaMatch
