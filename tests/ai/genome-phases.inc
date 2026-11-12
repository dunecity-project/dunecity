// Test-only genome expression shared by match and frozen-reference probes.
#ifndef DUNECITY_TEST_GENOME_PHASES_INC
#define DUNECITY_TEST_GENOME_PHASES_INC

#include <Game.h>
#include <House.h>
#include <Map.h>
#include <globals.h>
#include <players/QuantBot.h>
#include <players/SimpleArmyPolicy.h>
#include <FileClasses/INIFile.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
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

} // namespace FormationDuel
#endif
