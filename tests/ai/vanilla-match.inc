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

namespace VanillaMatch {

struct State {
    std::vector<FormationDuel::Side> sides;
    std::map<int, Uint32> eliminated;   ///< house -> first cycle it was no longer alive
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
/// city, Rock High and Spice Medium, not mirrored - Stefan's 4P DuneCity FFA settings
/// ([Random Skirmish] Base 5, Army 5, City 4). Every side its own team; the engine assigns the
/// starts from the match seed as in any custom game.
inline GameInitSettings makeSkirmishInit(int mapSize, Uint32 mapSeed, const std::vector<FormationDuel::Side>& sides,
                                         const std::string& difficulty, const std::string& mod,
                                         const SettingsClass::GameOptionsClass& options) {
    using namespace SkirmishGenerator;
    const bool city = mod == "dunecity";
    Params params;
    params.multiplayer = true;
    params.starts = static_cast<int>(sides.size());
    params.mapSize = mapSize;
    params.seed = mapSeed;
    params.symmetric = false;
    params.rock = Rock::High;
    params.spice = Spice::Medium;
    params.mode = city ? Mode::DuneCity : Mode::Vanilla;
    params.mod = mod;
    params.techLevel = 9;
    params.startSide = SkirmishGenerator::Side{"", BaseSize::Fortress, 5, city ? 4 : 0};
    const Result result = generate(params, RandomSkirmish::loadCatalogue(params.mode));
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

/// Once per cycle: per-minute samples and newly eliminated houses. True once one house is left.
inline bool tick(Game& game, State& state) {
    const Uint32 now = game.getGameCycleCount();
    EliminationMatch::crushCount.observe();
    if (now % MILLI2CYCLES(60000) == 0) EliminationMatch::sample(game);
    for (const auto& side : state.sides) {
        const House* house = game.getHouse(side.house);
        if (state.eliminated.count(side.house) || (house && house->isAlive())) continue;
        state.eliminated[side.house] = now;
        SDL_Log("VANILLA_MATCH_ELIMINATED: house=%d label=%s cycle=%u end_s=%.1f", side.house, side.label.c_str(), now,
                FormationWaveMeasure::seconds(now));
    }
    return FormationDuel::decided(game, state.sides);
}

inline void run(Game& game, Uint32 end, const std::vector<FormationDuel::Side>& sides) {
    State state{sides, {}};
    FormationDuel::applyShapes(game, sides);
    FormationWaveMeasure::run(game, end, [&]() {
        ProbeDigest::periodic(game);
        return tick(game, state);
    });
    EliminationMatch::sample(game, true);
    FormationDuel::report(game, sides);
}

} // namespace VanillaMatch
