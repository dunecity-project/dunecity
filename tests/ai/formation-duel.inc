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
#include <players/SimpleArmyPolicy.h>
#include <FileClasses/INIFile.h>

#include <array>
#include <fstream>
#include <memory>

#include <sstream>
#include <string>
#include <vector>

namespace FormationDuel {

struct Side {
    int house = HOUSE_INVALID;
    int shape = QuantBotConfig::kWaveFormationNone;
    std::string label;          ///< as given: shape[+flags][/retreat|/noretreat][/weak][/k<percent>]
    int retreat = -1;           ///< QuantBot::tacticalRetreatOverride: -1 the game's (on with Blob)
    int variant = 0;            ///< QuantBot::blobVariantOverride bits from +v (vehicle pace), +r (real fight), +k (keep damaged),
                                ///< +s (stage near the enemy), +e (stage only with local superiority)
    int force = 0;              ///< QuantBot::forceVariantOverride bits from the uppercase flags (+F, +C, +P, +H, +M, +L, +Q,
                                ///< +N, +B, +O, +W, +S, +T, +D, +E, +G, +A, +R, +X, +Y, +U, +I)
    int strengthBps = 10000;    ///< QuantBot::forceStrengthBps from /k<percent> (A's k)
    bool weak = false;          ///< /weak: half of the starting army is removed (the defence scenario)
    bool aiPlayer = false;      ///< label "ai": the classic AI Player controls this house (vanilla matches only)
    bool configured = false;    ///< shape "default": the configured rules (no force or formation override)
    std::string style;          ///< @style: tests/ai/play-styles/<style>.ini (docs/ai-play-styles.md)
    std::string strategy;       ///< #strategy: "random" or "<Mix>-<Tempo>[-fixed]" (applyStrategy)
};

/// "house:shape[+flags][@style][#strategy][/variant...],house:...", houses by number, shapes by INI
/// name (None, Blob or a kept shape) or "default" (the configured rules as the game plays them: no
/// force or formation override, no flags). @style plays that test-only play style on top (its config
/// keys and [Play Style] behaviours, applyStyle); #strategy sets this bot's [Strategy] (applyStrategy).
/// Variants: /retreat turns the tactical fall-back on, /noretreat off (without one the game's: on
/// with Blob only); /weak halves the starting army. With allowAI the label "ai" is the classic AI
/// Player instead of a QuantBot (vanilla-match.py), and any number of sides (two or more) may be given.
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
                else if (variant.size() > 1 && variant[0] == 'k'
                         && variant.find_first_not_of("0123456789", 1) == std::string::npos)
                    side.strengthBps = std::stoi(variant.substr(1)) * 100;
                else throw std::runtime_error("Unknown duel variant: " + entry);
            }
        }
        const auto hash = shape.find('#');
        if (hash != std::string::npos) {
            side.strategy = shape.substr(hash + 1);
            shape = shape.substr(0, hash);
            if (side.strategy.empty() || side.strategy.find_first_not_of(
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
                throw std::runtime_error("Invalid strategy: " + entry);
        }
        const auto at = shape.find('@');
        if (at != std::string::npos) {
            side.style = shape.substr(at + 1);
            shape = shape.substr(0, at);
            if (side.style.empty() || side.style.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos)
                throw std::runtime_error("Invalid play style: " + entry);
        }
        if (shape == "default") {
            side.configured = true;
            sides.push_back(side);
            continue;
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
                else if (flag == 'E') side.force |= QuantBot::kForceEstimate;
                else if (flag == 'G') side.force |= QuantBot::kForceGarrison;
                else if (flag == 'A') side.force |= QuantBot::kForceStrengthWaves;
                else if (flag == 'R') side.force |= QuantBot::kForceReserve;
                else if (flag == 'X') side.force |= QuantBot::kForceSizedDefence;
                else if (flag == 'Y') side.force |= QuantBot::kForceFallBack;
                else if (flag == 'U') side.force |= QuantBot::kForceThreatTurrets;
                else if (flag == 'I') side.force |= QuantBot::kForceOpeningReadiness;
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

/// A play style (docs/ai-play-styles.md): BALANCE_STYLE_DIR/<style>.ini. Its QuantBot Config keys
/// apply over the global config in a copy only this bot plays by (QuantBot::configOverride), its
/// [Play Style] section sets the behaviours no config key expresses (QuantBot::PlayStyle). Test-only:
/// neither is saved or hashed, and the game never sets them.
inline void applyStyle(QuantBot& bot, const std::string& style) {
    const char* dir = std::getenv("BALANCE_STYLE_DIR");
    if (!dir) throw std::runtime_error("A play style needs BALANCE_STYLE_DIR");
    const std::string path = std::string(dir) + "/" + style + ".ini";
    auto config = std::make_shared<QuantBotConfig>(getQuantBotConfig());
    if (!config->applyOverrides(path)) throw std::runtime_error("Unknown play style: " + path);
    bot.configOverride = config;
    const INIFile ini(path);
    const std::string section = "Play Style";
    QuantBot::PlayStyle s;
    s.mixBps = ini.getIntValue(section, "MixBps", s.mixBps);
    {
        std::istringstream mix(ini.getStringValue(section, "Mix", ""));
        std::string value;
        for (size_t i = 0; i < s.mix.size() && std::getline(mix, value, ','); ++i) s.mix[i] = std::stoll(value);
    }
    s.mixMinutes = ini.getIntValue(section, "MixMinutes", s.mixMinutes);
    s.counterBps = ini.getIntValue(section, "CounterBps", s.counterBps);
    s.lightFirst = ini.getBoolValue(section, "LightFirst", s.lightFirst);
    s.minWaveUnits = ini.getIntValue(section, "MinWaveUnits", s.minWaveUnits);
    s.minWaveValue = ini.getIntValue(section, "MinWaveValue", s.minWaveValue);
    s.rocketTurrets = ini.getIntValue(section, "RocketTurrets", s.rocketTurrets);
    s.greedyHarvest = ini.getBoolValue(section, "GreedyHarvest", s.greedyHarvest);
    s.extraRefineries = ini.getIntValue(section, "ExtraRefineries", s.extraRefineries);
    s.minYards = ini.getIntValue(section, "MinYards", s.minYards);
    s.raidUnits = ini.getIntValue(section, "RaidUnits", s.raidUnits);
    s.learning = ini.getBoolValue(section, "Learning", s.learning);
    s.startBounds = ini.getBoolValue(section, "StartBounds", s.startBounds);
    bot.playStyle = s;
    // The constructor drew the first attack delay from the global config.
    if (config->attackTimerMs != getQuantBotConfig().attackTimerMs)
        bot.attackTimer = SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config->attackTimerMs),
            currentGame->getGameInitSettings().getRandomSeed(), currentGame->getGameCycleCount(),
            bot.getHouse()->getHouseID());
    SDL_Log("PLAY_STYLE: house=%d style=%s mix_bps=%d counter_bps=%d light_first=%d raid=%d turrets=%d greedy=%d "
            "extra_refineries=%d min_yards=%d wave=%d/%d readiness=%d opening=%d timer_ms=%d learning=%d start_bounds=%d",
            bot.getHouse()->getHouseID(), style.c_str(), s.mixBps, s.counterBps, int(s.lightFirst), s.raidUnits,
            s.rocketTurrets, int(s.greedyHarvest), s.extraRefineries, s.minYards, s.minWaveUnits, s.minWaveValue,
            config->force.waveReadinessPercent, int(config->force.openingReadiness), config->attackTimerMs,
            int(s.learning), int(s.startBounds));
}

/// A strategy (docs/ai-play-styles.md, "Strategies") for this bot only: "random" = [Strategy]
/// Mode=Random with the config's weights, "<Mix>-<Tempo>" = Mode=Fixed:<Mix>,<Tempo>, and
/// "<Mix>-<Tempo>-fixed" additionally holds the mix's ratios against the learning (the test-only
/// style mix at 100%, the "strategy fixed" arm) and plays the tempo's wave rules with it. Applied
/// over the bot's style config if it has one; test-only like applyStyle.
inline void applyStrategy(QuantBot& bot, const std::string& text) {
    auto config = std::make_shared<QuantBotConfig>(bot.configOverride ? *bot.configOverride : getQuantBotConfig());
    bool fixed = false;
    if (text == "random") {
        config->strategy.random = true;
    } else {
        std::vector<std::string> parts;
        std::istringstream input(text);
        std::string part;
        while (std::getline(input, part, '-')) parts.push_back(part);
        QuantBotStrategy::Mix mix;
        QuantBotStrategy::Tempo tempo;
        if (parts.size() < 2 || parts.size() > 3 || !QuantBotStrategy::parse(parts[0], mix)
            || !QuantBotStrategy::parse(parts[1], tempo) || (parts.size() == 3 && parts[2] != "fixed"))
            throw std::runtime_error("Unknown strategy: " + text);
        config->strategy.random = false;
        config->strategy.fixedMix = mix;
        config->strategy.fixedTempo = tempo;
        fixed = parts.size() == 3;
        if (fixed) {
            const auto& profile = QuantBotStrategy::profile(mix);
            const auto& waves = QuantBotStrategy::profile(tempo);
            QuantBot::PlayStyle s = bot.playStyle;
            if (profile.counter) s.counterBps = 10000;
            else if (profile.priorBps > 0) { s.mix = profile.prior; s.mixBps = 10000; }
            s.lightFirst = waves.lightFirst;
            s.minWaveUnits = waves.minWaveUnits;
            s.minWaveValue = waves.minWaveValue;
            bot.playStyle = s;
        }
    }
    bot.configOverride = config;
    SDL_Log("PLAY_STRATEGY: house=%d strategy=%s mode=%s fixed=%d", bot.getHouse()->getHouseID(), text.c_str(),
            QuantBotStrategy::modeText(config->strategy).c_str(), int(fixed));
}

// Genome phases are private probe inputs, not shipped configuration or multiplayer protocol.
// Each phase starts from pristine native config plus the explicitly expressed registry keys.
struct GenomeSide {
    QuantBot* bot = nullptr;
    std::string genome;
    int controller = 0;
    std::array<std::shared_ptr<const QuantBotConfig>, 3> configs;
    std::array<std::string, 3> hashes;
};

struct GenomePhases {
    bool enabled = false;
    int applied = -1;
    std::array<Uint32, 3> starts{};
    std::vector<GenomeSide> sides;
};

inline constexpr std::array<const char*, 3> kGenomePhases = {"opening", "mid", "late"};

inline GenomePhases loadGenomePhases(Game& game, const std::vector<Side>& sides, bool allowMultipleControllers = false) {
    GenomePhases result;
    const char* directory = std::getenv("BALANCE_GENOME_PHASE_DIR");
    if (!directory) return result;
    result.enabled = true;
    const std::string dir(directory);
    const INIFile schedule(dir + "/schedule.ini");
    const Uint32 origin = schedule.getBoolValue("Schedule", "elapsed", false) ? game.getGameCycleCount() : 0;
    for (size_t phase = 0; phase < kGenomePhases.size(); ++phase) {
        const int minute = schedule.getIntValue("Schedule", kGenomePhases[phase], -1);
        if (minute < 0 || minute > 10000 || (phase == 0 && minute != 0))
            throw std::runtime_error("Invalid genome phase start");
        result.starts[phase] = origin + static_cast<Uint32>(MILLI2CYCLES(minute * 60000));
        if (phase && result.starts[phase] <= result.starts[phase - 1])
            throw std::runtime_error("Genome phases must have increasing starts");
    }
    for (const Side& side : sides) {
        if (!side.configured || side.aiPlayer || !side.style.empty() || !side.strategy.empty()
            || side.retreat != -1 || side.weak)
            throw std::runtime_error("A genome must use an unmodified default QuantBot side");
        House* house = game.getHouse(side.house);
        if (!house) throw std::runtime_error("A genome house is missing");
        if (!allowMultipleControllers && house->getPlayerList().size() != 1)
            throw std::runtime_error("A genome match house needs exactly one QuantBot");
        int controller = 0;
        for (const auto& player : house->getPlayerList()) {
            auto* bot = dynamic_cast<QuantBot*>(player.get());
            if (!bot) throw std::runtime_error("A genome cannot override a human or shared non-QuantBot house");
            GenomeSide entry;
            entry.bot = bot;
            entry.controller = controller++;
            entry.genome = schedule.getStringValue("Genomes", std::to_string(side.house), "");
            if (entry.genome.empty()) throw std::runtime_error("A genome assignment is missing");
            const auto sibling = std::find_if(result.sides.begin(), result.sides.end(), [&](const auto& other) {
                return other.bot->getHouse() == house && other.bot->difficulty == bot->difficulty;
            });
            if (sibling != result.sides.end()) {
                entry.configs = sibling->configs;
                entry.hashes = sibling->hashes;
            }
            for (size_t phase = 0; phase < kGenomePhases.size(); ++phase) {
                if (!entry.configs[phase]) {
                    const std::string stem = dir + "/" + std::to_string(side.house) + "-";
                    const std::string specific = stem + std::to_string(static_cast<int>(bot->difficulty))
                        + "-" + kGenomePhases[phase] + ".ini";
                    const std::string path = std::ifstream(specific).good() ? specific : stem + kGenomePhases[phase] + ".ini";
                    auto config = std::make_shared<QuantBotConfig>(getQuantBotConfig());
                    if (!config->applyOverrides(path)) throw std::runtime_error("Cannot read genome phase: " + path);
                    entry.hashes[phase] = config->getConfigHash();
                    entry.configs[phase] = std::move(config);
                }
                SDL_Log("GENOME_CONFIG: house=%d genome=%s phase=%s start_cycle=%u config_hash=%s controller=%d difficulty=%d",
                    side.house, entry.genome.c_str(), kGenomePhases[phase], result.starts[phase],
                    entry.hashes[phase].c_str(), entry.controller, static_cast<int>(bot->difficulty));
            }
            result.sides.push_back(std::move(entry));
        }
    }
    return result;
}

inline void applyGenomePhase(Game& game, GenomePhases& phases) {
    if (!phases.enabled) return;
    const Uint32 cycle = game.getGameCycleCount();
    int phase = 0;
    while (phase < 2 && cycle >= phases.starts[phase + 1]) ++phase;
    if (phase == phases.applied) return;
    for (auto& side : phases.sides) {
        const auto& previous = side.bot->botConfig();
        const auto& config = *side.configs[phase];
        const bool timerChanged = previous.attackTimerMs != config.attackTimerMs;
        const bool strategyChanged = previous.strategy.random != config.strategy.random
            || previous.strategy.fixedMix != config.strategy.fixedMix
            || previous.strategy.fixedTempo != config.strategy.fixedTempo
            || previous.strategy.mixWeights != config.strategy.mixWeights
            || previous.strategy.tempoWeights != config.strategy.tempoWeights
            || previous.tunedRatios != config.tunedRatios || previous.tunedOpening != config.tunedOpening;
        side.bot->configOverride = side.configs[phase];
        if (timerChanged && !side.bot->builderOnly() && !side.bot->switchedOff())
            side.bot->attackTimer = SimpleArmyPolicy::attackDelay(MILLI2CYCLES(config.attackTimerMs),
                game.getGameInitSettings().getRandomSeed(), cycle, side.bot->getHouse()->getHouseID());
        if (strategyChanged) side.bot->strategyPicked = false;
        // Custom military limits are captured on the first AI update, unlike harvester
        // limits. Refresh just that cached result, never replay starting actions.
        if (side.bot->initialMilitaryValue >= 0 && side.bot->gameMode == QuantBot::GameMode::Custom) {
            const auto& settings = config.getSettings(static_cast<int>(side.bot->difficulty));
            const int area = currentGameMap->getSizeX() * currentGameMap->getSizeY();
            side.bot->militaryValueLimit = area <= 1024 ? settings.militaryValueLimitCustomSmallMap
                : area <= 4096 ? settings.militaryValueLimitCustomMediumMap
                : area <= 16384 ? settings.militaryValueLimitCustomLargeMap
                : settings.militaryValueLimitCustomHugeMap;
        }
        SDL_Log("GENOME_PHASE: house=%d genome=%s phase=%s cycle=%u config_hash=%s controller=%d difficulty=%d",
            side.bot->getHouse()->getHouseID(), side.genome.c_str(), kGenomePhases[phase], cycle,
            side.hashes[phase].c_str(), side.controller, static_cast<int>(side.bot->difficulty));
    }
    phases.applied = phase;
}

// Continuation probes need no replacement roster: only the explicitly assigned,
// exclusively QuantBot-controlled houses are eligible. Cached phases do no work
// between boundaries and never draw from the game RNG.
inline void applyGenomePhase(Game& game) {
    if (!std::getenv("BALANCE_GENOME_PHASE_DIR")) return;
    static Game* owner = nullptr;
    static Uint32 lastCycle = 0;
    static GenomePhases phases;
    if (owner != &game || game.getGameCycleCount() < lastCycle) {
        const INIFile schedule(std::string(std::getenv("BALANCE_GENOME_PHASE_DIR")) + "/schedule.ini");
        std::vector<Side> sides;
        for (int house = 0; house < NUM_HOUSES; ++house)
            if (!schedule.getStringValue("Genomes", std::to_string(house), "").empty()) {
                const House* assigned = game.getHouse(house);
                if (!assigned || assigned->getPlayerList().empty()) continue;
                bool onlyQuantBots = true;
                for (const auto& player : assigned->getPlayerList())
                    if (!dynamic_cast<const QuantBot*>(player.get())) onlyQuantBots = false;
                if (!onlyQuantBots) continue;
                Side side;
                side.house = house;
                side.label = "default";
                side.configured = true;
                sides.push_back(std::move(side));
            }
        if (sides.empty()) throw std::runtime_error("A genome continuation needs explicit house assignments");
        phases = loadGenomePhases(game, sides, true);
        owner = &game;
    }
    applyGenomePhase(game, phases);
    lastCycle = game.getGameCycleCount();
}

/// Gives each side's QuantBot its wave shape and its ablations (test-only), its play style, and
/// halves a /weak side's starting army: every second ground fighter by object id is removed
/// before the first cycle (the composition stays; the base, harvesters and MCVs are untouched).
inline void applyShapes(Game& game, const std::vector<Side>& sides) {
    for (const Side& side : sides) {
        House* house = game.getHouse(side.house);
        if (!house) throw std::runtime_error("A duel house is missing");
        int bots = 0;
        for (const auto& player : house->getPlayerList())
            if (auto* bot = dynamic_cast<QuantBot*>(player.get())) {
                if (!side.configured) {
                    bot->waveFormationOverride = side.shape;
                    bot->blobVariantOverride = side.variant;
                    bot->forceVariantOverride = side.force;
                    bot->forceStrengthBps = side.strengthBps;
                }
                bot->tacticalRetreatOverride = side.retreat;
                if (!side.style.empty()) applyStyle(*bot, side.style);
                if (!side.strategy.empty()) applyStrategy(*bot, side.strategy);
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
        std::string strategy;
        for (const auto& player : house->getPlayerList())
            if (const auto* bot = dynamic_cast<const QuantBot*>(player.get())) {
                stats = bot->blobStats; force = bot->forceStats;
                strategy = std::string(QuantBotStrategy::name(bot->strategy.mix)) + "-" + QuantBotStrategy::name(bot->strategy.tempo);
            }
        const auto home = FormationWaveMeasure::homeTrade()[side.house];
        SDL_Log("FORMATION_DUEL_HOUSE: house=%d shape=%s alive=%d kill_value=%d loss_value=%d structures_destroyed=%d "
                "units_destroyed=%d structures=%d army_value=%d army_units=%d parties=%d gathered=%d gather_timeouts=%d "
                "releases=%d reforms=%d fallbacks=%d fallback_returns=%d legs=%d dissolved=%d stalls=%d repairs=%d "
                "home_kill=%d home_loss=%d focus=%d converges=%d pooled=%d recalled=%d musters=%d muster_releases=%d "
                "stage_holds=%d staged=%d bundle_orders=%d gate_holds=%d gate_timeouts=%d splits=%d split_releases=%d "
                "split_timeouts=%d full_readiness=%d garrison_holds=%d strength_holds=%d reserve_kept=%d "
                "sized_responses=%d declined_responses=%d fall_backs=%d perimeter_turret_orders=%d turrets_built=%d "
                "turrets_lost=%d raid_orders=%d turret_targets=%d press_on_orders=%d finish_waves=%d "
                "fortified_holds=%d sonic_retargets=%d surplus_peak=%d siege_orders=%d siege_holds=%d "
                "assault_holds=%d assault_waves=%d strategy=%s",
                side.house, side.label.c_str(), int(house->isAlive()), house->getKillValue(),
                house->getLossValue(), house->getNumDestroyedStructures(), house->getNumDestroyedUnits(), structures, army, units,
                stats.parties, stats.gathered, stats.gatherTimeouts, stats.releases, stats.reforms, stats.fallbacks,
                stats.fallbackReturns, stats.legs, stats.dissolved, stats.stalls, stats.repairs,
                home.kill, home.loss, force.focusSwitches, force.converges, force.pooled, force.recalled, force.musters,
                force.musterReleases, force.stageHolds, force.staged, force.bundleOrders, force.gateHolds, force.gateTimeouts,
                force.splits, force.splitReleases, force.splitTimeouts, force.fullReadiness, force.garrisonHolds,
                force.strengthHolds, force.reserveKept, force.sizedResponses, force.declinedResponses, force.fallBacks,
                force.perimeterTurretOrders, force.turretsBuilt, force.turretsLost, force.raidOrders,
                force.turretTargets, force.pressOnOrders, force.finishWaves, force.fortifiedHolds, force.sonicRetargets,
                force.surplusPeak, force.siegeOrders, force.siegeHolds, force.assaultHolds, force.assaultWaves,
                strategy.empty() ? "none" : strategy.c_str());
    }
    SDL_Log("FORMATION_DUEL_RESULT: winner=%d end_s=%.1f", alive == 1 ? winner : -1,
            FormationWaveMeasure::seconds(game.getGameCycleCount()));
}

} // namespace FormationDuel
