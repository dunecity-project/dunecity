// Included only by campaign-balance-probe.inc, never by the shipped game.
// Real menu generator/init, real House research, read-only cycle observations and elimination.
#include <Menu/CivWarsMenu.h>
#include <Menu/RandomSkirmishMenu.h>
#include <dunecity/CitySimulation.h>
#include <players/PlayerFactory.h>
#include <players/QuantBot.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "civ-wars-air-probe.inc"

namespace CivWarsMatch {

struct AiTiming {
    double cpuMs = 0.0, wallMs = 0.0;
    unsigned long long calls = 0;
};
inline std::array<AiTiming, NUM_HOUSES> aiTiming{};

// The factory still sets the original qBot class string and the base constructor receives
// exactly the ordinary difficulty/role. Only this private executable replaces creators;
// save loaders, roster, commands, RNG and all QuantBot decisions are unchanged.
class MeasuredQuantBot final : public QuantBot {
public:
    using QuantBot::QuantBot;
    void update() override {
        const House* house = getHouse();
        const int targetBefore = house->getTechResearchTarget();
        const int remainingBefore = house->getTechResearchRemaining();
        const Uint32 labBefore = house->getTechResearchLabID();
        const auto wallStart = GamePerf::Clock::now();
        const double cpuStart = GamePerf::threadCpuMs();
        QuantBot::update();
        const double cpuMs = GamePerf::threadCpuMs() - cpuStart;
        const double wallMs = GamePerf::msSince(wallStart);
        auto& timing = aiTiming[house->getHouseID()];
        timing.cpuMs += cpuMs;
        timing.wallMs += wallMs;
        ++timing.calls;
        // A lab can be destroyed later in this very cycle. Observe the paid start here,
        // immediately after the real bot acts, so that start/cancel pair is not missed by
        // the end-of-cycle sampler. Match the sampler's post-update cycle numbering.
        const int target = house->getTechResearchTarget();
        if (house->isTechResearchActive() && (target != targetBefore
                || house->getTechResearchLabID() != labBefore || house->getTechResearchRemaining() > remainingBefore))
            SDL_Log("CIV_WARS_RESEARCH: cycle=%u house=%d team=%d event=start tech=%d target=%d remaining=%d lab=%u cost=%d",
                currentGame->getGameCycleCount() + 1, house->getHouseID(), house->getTeamID(),
                house->getTechLevel(), target, house->getTechResearchRemaining(), house->getTechResearchLabID(),
                house->getTechResearchCost());
    }
};

class MeasuredFactories {
    using Create = std::function<std::unique_ptr<Player>(House*, const std::string&)>;
    std::vector<std::pair<PlayerFactory::PlayerData*, Create>> original;
public:
    explicit MeasuredFactories(bool enabled) {
        if (!enabled) return;
        PlayerFactory::getList(); // Register once before taking stable entry addresses.
        original.reserve(4);
        for (auto& entry : PlayerFactory::playerDataList) {
            const auto& name = entry.getPlayerClass();
            QuantBot::Difficulty difficulty;
            if (name == "qBotEasy") difficulty = QuantBot::Difficulty::Easy;
            else if (name == "qBotMedium") difficulty = QuantBot::Difficulty::Medium;
            else if (name == "qBotHard") difficulty = QuantBot::Difficulty::Hard;
            else if (name == "qBotBrutal") difficulty = QuantBot::Difficulty::Brutal;
            else continue;
            original.emplace_back(&entry, entry.pCreate);
            entry.pCreate = [difficulty](House* house, const std::string& name) -> std::unique_ptr<Player> {
                return std::make_unique<MeasuredQuantBot>(house, name, difficulty, AIRole::Both);
            };
        }
    }
    MeasuredFactories(const MeasuredFactories&) = delete;
    MeasuredFactories& operator=(const MeasuredFactories&) = delete;
    ~MeasuredFactories() {
        for (auto& [entry, create] : original) entry->pCreate = std::move(create);
        if (std::getenv("BALANCE_CIV_ORDINARY_GATE") && std::uncaught_exceptions() == 0 && currentGame)
            CivWarsGate::reportOrdinarySetup(*currentGame);
    }
};

// Absent on an ordinary run means leave its existing config/hash entirely alone. Civ games
// default off. A mixed-side diagnostic has a global off baseline and explicit per-bot copies.
inline std::string configureStrategy(bool civWars) {
    const char* requested = std::getenv("BALANCE_CIV_STRATEGY");
    const std::string strategy = requested ? requested : civWars ? "off" : "";
    if (strategy.empty()) return strategy;
    if (strategy != "off" && strategy != "on" && strategy != "left" && strategy != "right")
        throw std::runtime_error("Unknown Civilization Wars strategy: " + strategy);
    if (!civWars && (strategy == "left" || strategy == "right"))
        throw std::runtime_error("Mixed Civilization Wars strategy needs a generated Civ Wars match");
    getQuantBotConfig().civWarsStrategy = strategy == "on";
    return strategy;
}

inline GameInitSettings makeInit(Uint32 seed, const std::string& leftClass, const std::string& rightClass,
                                 const SettingsClass::GameOptionsClass& rules) {
    if (!RandomSkirmish::activateMod("CivWars")) throw std::runtime_error("Cannot activate Civilization Wars");
    CivWarsGenerator::Params params;
    params.teamSize = std::atoi(std::getenv("BALANCE_CIV_WARS"));
    params.size = std::getenv("BALANCE_CIV_SIZE") ? std::atoi(std::getenv("BALANCE_CIV_SIZE")) : 0;
    params.wormsPerLane = std::getenv("BALANCE_CIV_WORMS") ? std::atoi(std::getenv("BALANCE_CIV_WORMS")) : 1;
    params.spice = static_cast<SkirmishGenerator::Spice>(std::getenv("BALANCE_CIV_SPICE")
        ? std::atoi(std::getenv("BALANCE_CIV_SPICE")) : 2);
    params.seed = seed;
    params.fixedSeed = true;
    params.house = HOUSE_ATREIDES;
    params.spectate = true;
    params.spectateClass = params.allyClass = leftClass;
    params.enemyClass = rightClass;
    const auto catalogue = RandomSkirmish::loadCatalogue(SkirmishGenerator::Mode::DuneCity);
    const auto generated = CivWarsGenerator::generate(params, catalogue);
    return CivWars::makeInit(generated, rules, params);
}

inline void applyTeamStrategy(Game& game, const std::string& strategy) {
    if (strategy != "left" && strategy != "right") return;
    const int enabledTeam = strategy == "left" ? 1 : 2;
    std::array<std::shared_ptr<const QuantBotConfig>, 3> configs;
    for (int team : {1, 2}) {
        auto config = std::make_shared<QuantBotConfig>(getQuantBotConfig());
        config->civWarsStrategy = team == enabledTeam;
        configs[team] = std::move(config);
    }
    for (const House* house : EliminationMatch::playingHouses(game)) {
        if (house->getTeamID() != 1 && house->getTeamID() != 2)
            throw std::runtime_error("Civilization Wars generated an unexpected team");
        for (const auto& player : house->getPlayerList()) {
            auto* bot = dynamic_cast<QuantBot*>(player.get());
            if (!bot) throw std::runtime_error("Civilization Wars needs a QuantBot on every seat");
            bot->configOverride = configs[house->getTeamID()];
        }
        SDL_Log("CIV_WARS_OVERRIDE: house=%d team=%d strategy=%d source=test_only_unhashed global_config_hash=%s",
            house->getHouseID(), house->getTeamID(), int(house->getTeamID() == enabledTeam),
            getQuantBotConfig().getConfigHash().c_str());
    }
}

inline void reportSetup(Game& game, const std::string& left, const std::string& right, const std::string& strategy) {
    const auto& init = game.getGameInitSettings();
    const auto& rules = init.getGameOptions();
    if (!init.isCivilizationWars() || !init.isLocalSpectator() || !game.isObserving()
        || !rules.researchLab || !rules.sandwormsRespawn || !rules.trooperAntiAir || !rules.cityEffects)
        throw std::runtime_error("Civilization Wars did not retain its canonical marker, observer or required rules");
    GameStateDigest::Hasher mapHash;
    for (const unsigned char byte : init.getFiledata()) mapHash.mixUint8(byte);
    SDL_Log("CIV_WARS_SETUP: seed=%u team_size=%s size=%s worms=%s spice=%s left=%s right=%s strategy=%s "
            "config_hash=%s options_hash=%s map_hash=%016llx map_hash_algorithm=fnv1a64 "
            "observing=1 global_strategy=%d strategy_override=%s width=%d height=%d baseline_cycle=%u "
            "produced_scope=all_unit_build_notifications lost_scope=all_unit_decrements city_pop_scale=display "
            "islands_scope=live_yards_on_midpoint_rock reached_scope=distinct_ground_combat_on_enemy_end_rock "
            "waves_source=custom_wave_launch_changes waves_complete=0 research_cycle=post_update "
            "research_pace=%u",
        init.getRandomSeed(), std::getenv("BALANCE_CIV_WARS"),
        std::getenv("BALANCE_CIV_SIZE") ? std::getenv("BALANCE_CIV_SIZE") : "0",
        std::getenv("BALANCE_CIV_WORMS") ? std::getenv("BALANCE_CIV_WORMS") : "1",
        std::getenv("BALANCE_CIV_SPICE") ? std::getenv("BALANCE_CIV_SPICE") : "2",
        left.c_str(), right.c_str(), strategy.c_str(), getQuantBotConfig().getConfigHash().c_str(),
        rules.getHash().c_str(), static_cast<unsigned long long>(mapHash.value()),
        int(getQuantBotConfig().civWarsStrategy), strategy == "left" || strategy == "right" ? "test_only_unhashed" : "none",
        currentGameMap->getSizeX(), currentGameMap->getSizeY(), game.getGameCycleCount(),
        static_cast<unsigned>(rules.researchPace));
    // The existing performance runner's optional dump path works here too; the exact map hash
    // is always retained, without making each batch keep a copy of every generated map.
    if (const char* path = std::getenv("BALANCE_PERF_DUMP_MAP")) {
        std::ofstream out(path, std::ios::binary);
        out << init.getFiledata();
        if (!out) throw std::runtime_error("Cannot dump generated Civilization Wars map");
    }
}

inline int lostUnits(const House& house) {
    int lost = 0;
    for (int item = Unit_FirstID; item <= Unit_LastID; ++item) lost += house.getNumLostItems(item);
    return lost;
}

struct HouseTrack {
    const House* house = nullptr;
    const QuantBot* bot = nullptr;
    int initialBuilt = 0, initialLost = 0;
    int tech = 1;
    Uint32 researchLab = NONE_ID, lastLaunch = 0, eliminated = UINT32_MAX;
    std::array<Uint32, 10> techAt{};
    int waves = 0, reached = 0;
};

struct LiveCounts {
    int harvesters = 0, refineries = 0, islands = 0, armyUnits = 0;
    long long armyValue = 0;
};

class Measurement {
    std::array<HouseTrack, NUM_HOUSES> tracks{};
    std::vector<int> houses;
    std::vector<Uint32> worms;
    // Object IDs are sequential. A growing dense table avoids a map/set node per unit or
    // per-cycle scratch collections. Each bit counts an ID once for that owning house.
    std::vector<std::uint32_t> reachedByHouse;
public:
    explicit Measurement(Game& game) {
        static_assert(NUM_HOUSES <= 32, "Reach counters need one bit per house");
        houses.reserve(6);
        worms.reserve(12);
        for (const House* house : EliminationMatch::playingHouses(game)) {
            const int h = house->getHouseID();
            auto& track = tracks[h];
            track.house = house;
            if (house->getPlayerList().size() != 1)
                throw std::runtime_error("Civilization Wars needs exactly one generated controller per house");
            track.bot = dynamic_cast<const MeasuredQuantBot*>(house->getPlayerList().front().get());
            if (!track.bot) throw std::runtime_error("Civilization Wars AI CPU instrumentation missed a controller");
            track.initialBuilt = house->getNumBuiltUnits();
            track.initialLost = lostUnits(*house);
            track.tech = house->getTechLevel();
            track.researchLab = house->getTechResearchLabID();
            track.lastLaunch = track.bot->customWave.launched;
            track.techAt.fill(UINT32_MAX);
            track.techAt[track.tech] = game.getGameCycleCount();
            houses.push_back(h);
            SDL_Log("CIV_WARS_ROSTER: house=%d team=%d player_class=%s strategy=%d initial_built=%d initial_lost=%d",
                h, house->getTeamID(), track.bot->getPlayerclass().c_str(), int(track.bot->botConfig().civWarsStrategy),
                track.initialBuilt, track.initialLost);
        }
        if (houses.size() != 2u * static_cast<unsigned>(std::atoi(std::getenv("BALANCE_CIV_WARS"))))
            throw std::runtime_error("Civilization Wars loaded a different roster size than its generator requested");
        // All generated worms are registered before the first simulation update. Respawning
        // worms keep their object IDs while sleeping; no internal kill counter is read.
        observe(game);
        if (worms.size() != 3u * static_cast<unsigned>(std::getenv("BALANCE_CIV_WORMS")
                ? std::atoi(std::getenv("BALANCE_CIV_WORMS")) : 1))
            throw std::runtime_error("Civilization Wars did not load all generated worm IDs before the first cycle");
    }

    bool observe(Game& game) {
        const Uint32 cycle = game.getGameCycleCount();
        bool eliminated = false;
        for (int h : houses) {
            auto& track = tracks[h];
            const House& house = *track.house;
            const int tech = house.getTechLevel();
            for (int level = track.tech + 1; level <= tech; ++level) {
                track.techAt[level] = cycle;
                SDL_Log("CIV_WARS_RESEARCH: cycle=%u house=%d team=%d event=complete tech=%d target=%d remaining=0 lab=%u",
                    cycle, h, house.getTeamID(), level, level, track.researchLab);
            }
            track.tech = tech;
            track.researchLab = house.getTechResearchLabID();
            if (!house.isAlive() && track.eliminated == UINT32_MAX) {
                track.eliminated = cycle;
                eliminated = true;
            }
            const Uint32 launched = track.bot->customWave.launched;
            if (launched != 0 && launched != track.lastLaunch) {
                ++track.waves;
                track.lastLaunch = launched;
                SDL_Log("CIV_WARS_WAVE: cycle=%u house=%d team=%d launch_cycle=%u members=%zu waves=%d source=custom_wave_launch",
                    cycle, h, house.getTeamID(), launched, track.bot->customWave.members.size(), track.waves);
            }
        }
        const int width = currentGameMap->getSizeX(), end = width / 4;
        for (const auto* unit : unitList) {
            const Uint32 id = unit->getObjectID(), item = unit->getItemID();
            if (item == Unit_Sandworm || item == Unit_TameSandworm) {
                if (std::find(worms.begin(), worms.end(), id) == worms.end()) {
                    worms.push_back(id);
                    SDL_Log("CIV_WARS_WORM: cycle=%u id=%u object_id=%u item=%u house=%d",
                        cycle, id, id, item, unit->getOwner()->getHouseID());
                }
                continue;
            }
            if (!unit->isActive() || unit->getHealth() <= 0 || !unit->isAGroundUnit() || !unit->canAttack()) continue;
            const int h = unit->getOwner()->getHouseID();
            if (h < 0 || h >= NUM_HOUSES || !tracks[h].house) continue;
            const int team = tracks[h].house->getTeamID();
            const Coord at = unit->getLocation();
            if ((team == 1 ? at.x < width - end : at.x >= end) || !currentGameMap->tileExists(at)
                || !currentGameMap->getTile(at)->isRock()) continue;
            if (id >= reachedByHouse.size())
                reachedByHouse.resize(std::max<size_t>(id + 1, std::max<size_t>(256, reachedByHouse.size() * 2)), 0);
            const std::uint32_t mask = std::uint32_t(1) << h;
            if (!(reachedByHouse[id] & mask)) {
                reachedByHouse[id] |= mask;
                ++tracks[h].reached;
            }
        }
        return eliminated;
    }

    void sample(Game& game, bool final = false) const {
        std::array<LiveCounts, NUM_HOUSES> live{};
        for (const auto* unit : unitList) {
            const int h = unit->getOwner()->getHouseID();
            if (h < 0 || h >= NUM_HOUSES || !tracks[h].house || unit->getHealth() <= 0) continue;
            const Uint32 item = unit->getItemID();
            if (item == Unit_Harvester || item == Unit_RebelHarvester) ++live[h].harvesters;
            else if (unit->canAttack()) {
                ++live[h].armyUnits;
                live[h].armyValue += std::max(0, game.objectData.data[item][h].price);
            }
        }
        const int middle = currentGameMap->getSizeX() / 2, radius = currentGameMap->getSizeX() / 8;
        for (const auto* structure : structureList) {
            const int h = structure->getOwner()->getHouseID();
            if (h < 0 || h >= NUM_HOUSES || !tracks[h].house || structure->getHealth() <= 0) continue;
            if (structure->getItemID() == Structure_Refinery) ++live[h].refineries;
            if (structure->getItemID() != Structure_ConstructionYard) continue;
            const Coord first = structure->getLocation(), size = structure->getStructureSize();
            const Coord centre(first.x + size.x / 2, first.y + size.y / 2);
            if (std::abs(centre.x - middle) <= radius && currentGameMap->tileExists(centre)
                && currentGameMap->getTile(centre)->isRock()) ++live[h].islands;
        }
        const auto* city = game.getCitySimulation();
        for (int h : houses) {
            const auto& track = tracks[h];
            const House& house = *track.house;
            const auto& counts = live[h];
            const int population = city && game.isCitySimEnabled()
                ? city->getHouseState(h).getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier : 0;
            const int produced = std::max(0, house.getNumBuiltUnits() - track.initialBuilt);
            const int lost = std::max(0, lostUnits(house) - track.initialLost);
            std::string times;
            if (final) {
                times = " eliminated_s=" + std::to_string(track.eliminated == UINT32_MAX
                    ? -1.0 : FormationWaveMeasure::seconds(track.eliminated));
                for (int level = 2; level <= 9; ++level)
                    times += " tech" + std::to_string(level) + "_s=" + std::to_string(track.techAt[level] == UINT32_MAX
                        ? -1.0 : FormationWaveMeasure::seconds(track.techAt[level]));
            }
            SDL_Log("%s: cycle=%u minute=%u house=%d team=%d alive=%d credits=%d tech=%d research_target=%d "
                    "research_remaining=%d harvesters=%d refineries=%d islands=%d city_pop=%d army_value=%lld "
                    "army_units=%d produced=%d lost=%d waves=%d reached=%d waves_source=custom_wave_launch_changes "
                    "waves_complete=0%s",
                final ? "CIV_WARS_HOUSE" : "CIV_WARS_SAMPLE", game.getGameCycleCount(),
                game.getGameCycleCount() / MILLI2CYCLES(60000), h, house.getTeamID(), int(house.isAlive()),
                house.getCredits(), house.getTechLevel(), house.getTechResearchTarget(), house.getTechResearchRemaining(),
                counts.harvesters, counts.refineries, counts.islands, population, counts.armyValue, counts.armyUnits,
                produced, lost, track.waves, track.reached, times.c_str());
        }
    }

    void reportAi() const {
        for (int h : houses) {
            const auto& timing = aiTiming[h];
            SDL_Log("CIV_WARS_AI_PERF: house=%d team=%d update_thread_cpu_ms=%.3f update_wall_ms=%.3f update_calls=%llu",
                h, tracks[h].house->getTeamID(), timing.cpuMs, timing.wallMs, timing.calls);
        }
    }
    size_t wormCount() const { return worms.size(); }
};

inline std::string run(Game& game, Uint32 end) {
    aiTiming = {};
    Measurement measurement(game);
    measurement.sample(game);
    const Uint32 first = game.getGameCycleCount(), minute = MILLI2CYCLES(60000);
    std::optional<CivWarsGate::EarlyAirStrike> strike;
    if (std::getenv("BALANCE_CIV_AIR_PROBE")) strike.emplace(game, end);
    int winner = EliminationMatch::survivingTeam(game);
    const double aiWallBefore = game.frameTiming.aiMs;
    const auto loopStart = GamePerf::Clock::now();
    const double cpuStart = GamePerf::threadCpuMs();
    while (winner == 0 && game.getGameCycleCount() < end) {
        ProbeDigest::periodic(game);
        if (strike) strike->beforeUpdate();
        game.updateGameState();
        if (strike) strike->observe();
        // The shared scorer allocates its roster/set. Re-evaluate only when an observed
        // house dies, rather than rebuilding those containers on every simulation cycle.
        if (measurement.observe(game)) winner = EliminationMatch::survivingTeam(game);
        if (game.getGameCycleCount() % minute == 0) measurement.sample(game);
    }
    const double cpuMs = GamePerf::threadCpuMs() - cpuStart, wallMs = GamePerf::msSince(loopStart);
    const Uint32 cycles = game.getGameCycleCount() - first;
    measurement.sample(game, true);
    measurement.reportAi();
    if (strike) strike->finish();
    const char* outcome = winner == 0 ? "time_limit" : winner == -1 ? "extinct" : "won";
    SDL_Log("CIV_WARS_RESULT: winner_team=%d outcome=%s cycle=%u end_s=%.3f worm_ids_complete=1 worm_count=%zu",
        winner, outcome, game.getGameCycleCount(), FormationWaveMeasure::seconds(game.getGameCycleCount()),
        measurement.wormCount());
    AiTiming ai;
    for (const auto& timing : aiTiming) {
        ai.cpuMs += timing.cpuMs;
        ai.wallMs += timing.wallMs;
        ai.calls += timing.calls;
    }
    SDL_Log("CIV_WARS_PERF: loop_thread_cpu_ms=%.3f loop_wall_ms=%.3f cycles=%u cycles_per_s=%.3f "
            "ai_update_thread_cpu_ms=%.3f ai_update_wall_ms=%.3f ai_update_calls=%llu ai_wall_ms=%.3f "
            "thread=simulation metrics_included=1",
        cpuMs, wallMs, cycles, wallMs > 0 ? cycles * 1000.0 / wallMs : 0.0,
        ai.cpuMs, ai.wallMs, ai.calls, game.frameTiming.aiMs - aiWallBefore);
    // Keep the generic runner's tracked-left-team acceptance semantics. The Civ result above
    // reports any winning team as won, and never adjudicates the time limit on material.
    return winner == 0 ? "time_limit" : winner == 1 ? "won" : "lost";
}

} // namespace CivWarsMatch
