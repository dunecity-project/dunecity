// Real-engine QuantBot strategies (docs/ai-play-styles.md, "Strategies"). Included only by
// run-campaign-balance.py --strategy-probe, after campaign-balance-probe.inc (its helpers); never
// linked into the game. One process, real Game instances one after another (the engine's object
// lists are global, so two worlds cannot exist at once), production QuantBot code throughout:
//   1. Peers agree: the same Custom Hard four-house vanilla game (BALANCE_CUSTOM_MAP, a bundled
//      map, QuantBot Hard on every house, a fixed seed, [Strategy] Mode=Random with every mix and
//      tempo weighted) built twice, "host" and "guest". After the first update every bot has
//      picked, both worlds hold the same strategies, each equal to QuantBotStrategy::pick(seed,
//      house, eligible); every game minute to 16 both have the same state digest, and the same
//      QuantBot bytes at the end. The all-Learned game of the same seed has a different digest (the
//      strategies matter), and another seed picks a different strategy for some house.
//   2. Save/load: the host's minute-12 save loads with every strategy, its QuantBot blocks re-save
//      byte-identically and round-trip through QuantBot::save/load; a 9864 block (the same bytes
//      without the 3 strategy bytes) loads unpicked and pickStrategy() picks the original again.
//      Loaded with the network checkpoint runtime it continues to the host's digest and QuantBot
//      bytes; two ordinary loads continue identically (an ordinary load rebuilds the bots' plans,
//      so it is not the host's continuation, with or without strategies).
//   3. Each mix strategy's production follows its ratios while learning stays on: Custom Hard
//      Fortress Random Skirmishes (every factory standing from the start), each house
//      Fixed:<Mix>,Base in its own config copy, 12 game minutes. Every production pass before
//      combat evidence plays exactly the prior masked to the available slots (applyPrior over the
//      opening, plus the anti-air floor); the value built favours the prior's lead slots more than
//      the Learned game on the same map does; once learning is active the mix leaves the prior.
//   4. Learned/Base is inert: Fixed:Learned,Base and Random weighted only on Learned/Base play the
//      same game (every production pass's mix, digest and QuantBot bytes), the 1.0.846 rules.
// One STRATEGY_PROBE: line per check.
#include <players/QuantBotStrategy.h>
#include <Network/GameStateDigest.h>
#include <MapEditor/SkirmishGenerator.h>
#include <Menu/RandomSkirmishMenu.h>
#include <chrono>
#include <cstring>
#include <set>

namespace StrategyProbe {

using QuantBotStrategy::Choice;
using QuantBotStrategy::Mix;
using QuantBotStrategy::Tempo;
using Clock = std::chrono::steady_clock;

inline void require(bool ok, const std::string& why) {
    if (!ok) {
        SDL_Log("STRATEGY_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

inline std::string text(const Choice& choice) {
    return std::string(QuantBotStrategy::name(choice.mix)) + "-" + QuantBotStrategy::name(choice.tempo);
}

template<class Array> std::string join(const Array& values) {
    std::string out;
    for (const auto value : values) out += (out.empty() ? "" : "/") + std::to_string(value);
    return out;
}

inline long long millisecondsSince(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

inline std::unique_ptr<Game> start(const GameInitSettings& init) {
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;   // no briefing / analytics at setup
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    game->frameTiming.unitPhases = false;
    game->bPause = false;
    return game;
}

inline void end(std::unique_ptr<Game>& game) {
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
}

/// One cycle with the path budget pinned: single-player adapts it to the frame rate, which would
/// make two worlds of the same game differ by wall time instead of by simulation.
inline void tick(Game& game) {
    game.negotiatedBudget = 15000;
    game.carryOverTokens = 0;
    game.updateGameState();
}

inline QuantBot* botOf(Game& game, int house) {
    House* owner = game.getHouse(static_cast<HOUSETYPE>(house));
    if (owner) for (const auto& player : owner->getPlayerList())
        if (auto* bot = dynamic_cast<QuantBot*>(player.get())) return bot;
    return nullptr;
}

/// The mixes a house can field, exactly as QuantBot::pickStrategy() decides them.
inline std::array<bool, QuantBotStrategy::kMixCount> eligibleFor(int houseID) {
    auto buildable = [&](Uint32 item) {
        const auto& spec = currentGame->objectData.data[item][houseID];
        return spec.enabled && spec.price > 0 && spec.builder != ItemID_Invalid && spec.techLevel <= currentGame->techLevel;
    };
    std::array<bool, 8> slots{};
    const std::array<Uint32, 8> items = {Unit_Tank, Unit_SiegeTank, Unit_Launcher, Unit_Devastator,
        Unit_Ornithopter, Unit_Trike, Unit_RaiderTrike, Unit_Quad};
    for (size_t i = 0; i < items.size(); ++i) slots[i] = buildable(items[i]);
    slots[3] = buildable(Unit_Devastator) || buildable(Unit_SonicTank) || buildable(Unit_Deviator);
    const bool infantry = buildable(Unit_Soldier) || buildable(Unit_Infantry) || buildable(Unit_Trooper) || buildable(Unit_Troopers);
    return QuantBotStrategy::eligibleMixes(slots, infantry);
}

inline std::string botBytes(const QuantBot& bot) {
    OMemoryStream out;
    out.open();
    bot.save(out);
    return std::string(out.getData(), out.getDataLength());
}

inline std::uint64_t hashOf(const std::string& bytes) {
    GameStateDigest::Hasher hash;
    for (const unsigned char c : bytes) hash.mixUint8(c);
    return hash.value();
}

/// A config copy only this bot plays by (QuantBot::configOverride, as applyStrategy does).
inline void setStrategy(QuantBot& bot, const QuantBotStrategy::Settings& strategy) {
    auto config = std::make_shared<QuantBotConfig>(getQuantBotConfig());
    config->strategy = strategy;
    bot.configOverride = config;
}

inline QuantBotStrategy::Settings fixedStrategy(Mix mix, Tempo tempo = Tempo::Base) {
    QuantBotStrategy::Settings settings;
    settings.random = false;
    settings.fixedMix = mix;
    settings.fixedTempo = tempo;
    return settings;
}

// ---- Scenarios ------------------------------------------------------------------------------

struct Scenario {
    std::string mapPath, mapText;
    std::vector<std::pair<int, int>> roster;   ///< house, team (map slot order)
};

/// The bundled map of BALANCE_CUSTOM_MAP, every house of BALANCE_ROSTER QuantBot Hard, observed
/// by a local spectator (no human house), as the custom-game lobby starts it.
inline GameInitSettings mapInit(const Scenario& scenario, Uint32 seed) {
    GameInitSettings init(scenario.mapPath, scenario.mapText, true, settings.gameOptions);
    init.randomSeed = seed;   // Test-only access, provided by -fno-access-control.
    init.setLocalSpectator(true);
    init.setMultiplePlayersPerHouse(false);
    for (const auto& [house, team] : scenario.roster) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(house), team);
        info.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(house)), "qBotHard"});
        init.addHouseInfo(info);
    }
    return init;
}

/// A mirrored 64x64 Random Skirmish 1v1, both starts a Fortress (every factory, IX and Starport
/// standing) with a 1,000-credit army and 10,000 credits, two QuantBot Hard houses.
inline GameInitSettings fortressInit(int first, int second, Uint32 mapSeed, Uint32 seed) {
    using namespace SkirmishGenerator;
    Params params;
    params.multiplayer = true;
    params.starts = 2;
    params.mapSize = 64;
    params.seed = mapSeed;
    params.symmetric = true;
    params.mode = Mode::Vanilla;
    params.mod = "vanilla";
    params.techLevel = 9;
    params.credits = 10000;
    params.startSide = SkirmishGenerator::Side{"", BaseSize::Fortress, 1, 0};
    const Result result = generate(params, RandomSkirmish::loadCatalogue(Mode::Vanilla));
    GameInitSettings init(result.name, result.ini, false, settings.gameOptions);
    init.randomSeed = seed;
    init.setLocalSpectator(true);
    init.setMultiplePlayersPerHouse(false);
    int team = 1;
    for (const int house : {first, second}) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(house), team++);
        info.addPlayerInfo({getHouseNameByNumber(static_cast<HOUSETYPE>(house)), "qBotHard"});
        init.addHouseInfo(info);
    }
    return init;
}

// ---- 1. Peers agree ---------------------------------------------------------------------------

struct World {
    std::map<int, Choice> choices;
    std::vector<std::string> minutes;   ///< the state digest once a game minute
    std::map<int, std::string> bytesAtSave;
    std::string digestAtSave, digestAtEnd, botsAtEnd;
};

inline std::map<int, Choice> picksAfterFirstUpdate(Game& game, const Scenario& scenario, Uint32 seed, const char* role) {
    tick(game);
    std::map<int, Choice> choices;
    for (const auto& [house, team] : scenario.roster) {
        QuantBot* bot = botOf(game, house);
        require(bot != nullptr, std::string(role) + ": a house has no QuantBot");
        require(bot->autonomousCustomArmy(), std::string(role) + ": a bot is not an autonomous Custom army");
        require(bot->strategyPicked, std::string(role) + ": a bot had not picked its strategy after the first update");
        const Choice expected = QuantBotStrategy::pick(getQuantBotConfig().strategy, seed, house, eligibleFor(house));
        require(bot->strategy == expected, std::string(role) + ": a bot's strategy is not QuantBotStrategy::pick() of seed and house");
        choices[house] = bot->strategy;
    }
    return choices;
}

inline std::string botsText(const std::map<int, Choice>& choices) {
    std::string out;
    for (const auto& [house, choice] : choices) out += (out.empty() ? "" : ",") + std::to_string(house) + ":" + text(choice);
    return out;
}

inline std::string botsHash(Game& game, const Scenario& scenario) {
    std::string out;
    for (const auto& [house, team] : scenario.roster)
        if (const QuantBot* bot = botOf(game, house)) out += std::to_string(hashOf(botBytes(*bot))) + " ";
    return out;
}

inline void runPeers(const Scenario& scenario, Uint32 seed, Uint32 otherSeed, Uint32 saveCycle, Uint32 endCycle,
                     const std::string& savePath, std::string& runtime, World& host) {
    World guest;
    for (int peer = 0; peer < 2; ++peer) {
        World& world = peer == 0 ? host : guest;
        const char* role = peer == 0 ? "host" : "guest";
        const auto wall = Clock::now();
        auto game = start(mapInit(scenario, seed));
        world.choices = picksAfterFirstUpdate(*game, scenario, seed, role);
        auto playTo = [&](Uint32 cycle) {
            while (!game->finished && game->getGameCycleCount() < cycle) {
                tick(*game);
                if (game->getGameCycleCount() % MILLI2CYCLES(60000) == 0)
                    world.minutes.push_back(GameStateDigest::describe(game->computeStateDigest()));
            }
        };
        playTo(saveCycle);
        require(game->getGameCycleCount() == saveCycle, std::string(role) + ": the game ended before its save point");
        world.digestAtSave = GameStateDigest::describe(game->computeStateDigest());
        for (const auto& [house, team] : scenario.roster) world.bytesAtSave[house] = botBytes(*botOf(*game, house));
        if (peer == 0) {
            require(game->saveGame(savePath), "The host could not save its game");
            runtime = game->saveObserverRuntime();
        }
        playTo(endCycle);
        require(game->getGameCycleCount() == endCycle, std::string(role) + ": the game ended early");
        world.digestAtEnd = GameStateDigest::describe(game->computeStateDigest());
        world.botsAtEnd = botsHash(*game, scenario);
        SDL_Log("STRATEGY_PROBE: check=peer role=%s seed=%u strategies=%s digest_cycle=%u digest=%s wall_ms=%lld", role, seed,
                botsText(world.choices).c_str(), endCycle, world.digestAtEnd.c_str(), millisecondsSince(wall));
        end(game);
    }
    require(host.choices == guest.choices, "Host and guest picked different strategies");
    require(host.digestAtSave == guest.digestAtSave && host.bytesAtSave == guest.bytesAtSave,
            "Host and guest diverged before the save point");
    require(host.minutes == guest.minutes && host.digestAtEnd == guest.digestAtEnd, "Host and guest state digests differ");
    require(host.botsAtEnd == guest.botsAtEnd, "Host and guest QuantBot bytes differ");
    std::set<std::string> distinct;
    for (const auto& [house, choice] : host.choices) distinct.insert(text(choice));
    SDL_Log("STRATEGY_PROBE: check=peers_agree seed=%u houses=%zu distinct_strategies=%zu strategies=%s cycle=%u digest=%s",
            seed, host.choices.size(), distinct.size(), botsText(host.choices).c_str(), endCycle, host.digestAtEnd.c_str());

    // The same game with every bot Fixed:Learned,Base (1.0.846) is a different game: the digest
    // the peers agree on covers what the strategies changed.
    const QuantBotStrategy::Settings spread = getQuantBotConfig().strategy;
    getQuantBotConfig().strategy = fixedStrategy(Mix::Learned);
    auto reference = start(mapInit(scenario, seed));
    std::vector<std::string> learnedMinutes;
    while (!reference->finished && reference->getGameCycleCount() < endCycle) {
        tick(*reference);
        if (reference->getGameCycleCount() % MILLI2CYCLES(60000) == 0)
            learnedMinutes.push_back(GameStateDigest::describe(reference->computeStateDigest()));
    }
    const std::string learnedDigest = GameStateDigest::describe(reference->computeStateDigest());
    end(reference);
    getQuantBotConfig().strategy = spread;
    size_t firstDifference = 0;
    while (firstDifference < learnedMinutes.size() && firstDifference < host.minutes.size()
           && learnedMinutes[firstDifference] == host.minutes[firstDifference]) ++firstDifference;
    SDL_Log("STRATEGY_PROBE: check=strategies_matter seed=%u cycle=%u first_differing_minute=%zu strategies_digest=%s learned_digest=%s",
            seed, endCycle, firstDifference + 1, host.digestAtEnd.c_str(), learnedDigest.c_str());
    require(learnedDigest != host.digestAtEnd, "The picked strategies did not change the game");

    // Another seed: the real engine picks QuantBotStrategy::pick() of that seed, and some house differs.
    auto other = start(mapInit(scenario, otherSeed));
    const auto otherChoices = picksAfterFirstUpdate(*other, scenario, otherSeed, "other seed");
    end(other);
    int differing = 0;
    for (const auto& [house, choice] : otherChoices) differing += choice == host.choices.at(house) ? 0 : 1;
    SDL_Log("STRATEGY_PROBE: check=seeds_differ seed=%u other_seed=%u strategies=%s other=%s differing_houses=%d", seed, otherSeed,
            botsText(host.choices).c_str(), botsText(otherChoices).c_str(), differing);
    require(differing > 0, "Two seeds picked the same strategy for every house");
}

// ---- 2. Save/load -----------------------------------------------------------------------------

inline void checkBotBlocks(const Scenario& scenario, const Uint32 seed, const std::string& savePath, const std::string& runtime,
                           const World& host, Uint32 saveCycle, Uint32 endCycle) {
    // Ordinary load: every strategy survives, and the QuantBot blocks of the loaded world are the bytes the host saved.
    auto game = start(GameInitSettings(savePath));
    require(game->getGameCycleCount() == saveCycle, "The loaded game is not at the save cycle");
    require(game->getLoadedSavegameVersion() == SAVEGAMEVERSION, "The host's save is not a current save");
    size_t blocks = 0, blockBytes = 0;
    for (const auto& [house, team] : scenario.roster) {
        QuantBot* bot = botOf(*game, house);
        House* owner = game->getHouse(static_cast<HOUSETYPE>(house));
        require(bot && bot->strategyPicked && bot->strategy == host.choices.at(house), "A loaded bot lost its strategy");
        const std::string& bytes = host.bytesAtSave.at(house);
        require(botBytes(*bot) == bytes, "A loaded bot re-saves different bytes");
        // QuantBot::save/load directly, current version.
        const Uint32 savedVersion = currentGame->loadedSavegameVersion;
        currentGame->loadedSavegameVersion = SAVEGAMEVERSION;
        {
            IMemoryStream in(bytes.data(), bytes.size());
            QuantBot restored(in, owner);
            require(restored.strategyPicked && restored.strategy == bot->strategy, "A QuantBot block lost its strategy");
            require(botBytes(restored) == bytes, "A QuantBot block does not round-trip byte-identically");
        }
        // A 9864 block: the same bytes without the 9865 strategy (picked flag, mix, tempo).
        constexpr size_t kStrategyBytes = 3;
        require(bytes.size() > kStrategyBytes && Uint8(bytes[bytes.size() - 3]) == 1
                && Uint8(bytes[bytes.size() - 2]) == Uint8(bot->strategy.mix) && Uint8(bytes[bytes.size() - 1]) == Uint8(bot->strategy.tempo),
                "The strategy is not the last 3 bytes of the QuantBot block");
        const std::string old = bytes.substr(0, bytes.size() - kStrategyBytes);
        currentGame->loadedSavegameVersion = 9864;
        {
            IMemoryStream in(old.data(), old.size());
            QuantBot restored(in, owner);
            require(!restored.strategyPicked && restored.strategy == Choice{}, "A 9864 block loaded with a strategy");
            require(restored.strategyMix() == nullptr && restored.strategyTempo() == nullptr, "An unpicked 9864 bot plays a strategy");
            restored.pickStrategy();
            require(restored.strategyPicked && restored.strategy == host.choices.at(house),
                    "A 9864 block picked a different strategy than the game did");
            currentGame->loadedSavegameVersion = SAVEGAMEVERSION;
            require(botBytes(restored).size() == bytes.size(), "A picked 9864 bot does not re-save as a current block");
        }
        currentGame->loadedSavegameVersion = savedVersion;
        ++blocks;
        blockBytes = bytes.size();
    }
    SDL_Log("STRATEGY_PROBE: check=bot_block_roundtrip seed=%u blocks=%zu bytes=%zu strategies=%s identical=1 v9864_repicked=1",
            seed, blocks, blockBytes, botsText(host.choices).c_str());
    // An ordinary load rebuilds the bots' supplemental plans (QuantBot::saveObserverRuntime: "normal
    // disk saves intentionally rebuild these plans"), so it is a different continuation than the
    // host's - with every bot on Learned/Base as well - but a repeatable one.
    auto continueTo = [&](std::unique_ptr<Game>& world) {
        while (!world->finished && world->getGameCycleCount() < endCycle) tick(*world);
        require(world->getGameCycleCount() == endCycle, "A loaded game ended early");
        for (const auto& [house, team] : scenario.roster)
            require(botOf(*world, house)->strategy == host.choices.at(house), "A loaded bot changed its strategy");
        return GameStateDigest::describe(world->computeStateDigest()) + " bots " + botsHash(*world, scenario);
    };
    const std::string ordinary = continueTo(game);
    end(game);
    game = start(GameInitSettings(savePath));
    const std::string ordinaryAgain = continueTo(game);
    end(game);
    // The network checkpoint path (a late joiner, a spectator): the same save plus the supplemental
    // runtime the host keeps, which must continue exactly as the host did.
    game = start(GameInitSettings(savePath));
    game->loadObserverRuntime(runtime);
    for (const auto& [house, team] : scenario.roster)
        require(botOf(*game, house)->strategy == host.choices.at(house), "A checkpoint-loaded bot lost its strategy");
    const std::string checkpoint = continueTo(game);
    end(game);
    const std::string live = host.digestAtEnd + " bots " + host.botsAtEnd;
    SDL_Log("STRATEGY_PROBE: check=save_load_digest save_cycle=%u cycle=%u live=%s checkpoint_load=%s ordinary_load=%s "
            "ordinary_repeat_equal=%d ordinary_equals_live=%d", saveCycle, endCycle, live.c_str(), checkpoint.c_str(), ordinary.c_str(),
            int(ordinary == ordinaryAgain), int(ordinary == live));
    require(checkpoint == live, "The checkpoint-loaded game did not continue to the host's state digest");
    require(ordinary == ordinaryAgain, "Two ordinary loads of the same save continued differently");
}

// ---- 3./4. Production --------------------------------------------------------------------------

constexpr std::array<Uint32, 8> kSlotItems = {Unit_Tank, Unit_SiegeTank, Unit_Launcher, Unit_SonicTank,
    Unit_Ornithopter, Unit_Trike, Unit_RaiderTrike, Unit_Quad};

/// What one house did over a production game.
struct Production {
    int house = -1;
    Choice strategy;
    bool inert = false;           ///< strategyMix() and strategyTempo() both null at the end
    int passes = 0, priorPasses = 0, priorMatches = 0, learningPasses = 0, learningOffPrior = 0, airFloorPasses = 0;
    Uint32 firstPriorCycle = 0, firstLearningCycle = 0, firstOffPriorCycle = 0;
    UnitMixPolicy::Mix firstPriorMix{}, offPriorMix{}, offPriorExpected{};
    int counterPasses = 0;        ///< prior passes that played a non-empty counter of the seen enemy
    UnitMixPolicy::Mix firstCounterMix{};
    std::string mismatch;
    std::uint64_t mixTrace = 0;   ///< every production pass's cycle and mix, in order
    std::array<int, 8> built{};   ///< units built per mix slot (specials grouped)
    std::array<int, 8> valueBps{};///< share of the built vehicle and air value per slot
    std::array<int, 8> earlyValueBps{};///< the same, of what was built before the first learning pass
    int builtInfantry = 0;
    int infantryQuotaBps = 0;
    std::array<Uint32, 8> firstAllocated{};   ///< cycle of the first pass each slot was producible (0 = never)
};

/// The mix the bot plays with no combat evidence at this pass: the configured opening over the
/// available slots with the strategy's prior in place of priorBps of it, then the mobile anti-air
/// floor if that pass applied one (QuantBotBuild.cpp).
inline UnitMixPolicy::Mix priorMix(const QuantBot& bot, const std::array<bool, 8>& available, int antiAirFloorBps,
                                   bool& counterPlayed) {
    const int houseID = bot.getHouse()->getHouseID();
    const auto& ratios = bot.botConfig().getRatios(houseID);
    UnitMixPolicy::Weights configured{};
    configured[0] = static_cast<int>(ratios.tank * 10000);
    configured[1] = static_cast<int>(ratios.siegeTank * 10000);
    configured[2] = static_cast<int>(ratios.launcher * 10000);
    configured[3] = static_cast<int>(ratios.special * 10000);
    configured[4] = static_cast<int>(ratios.ornithopter * 10000);
    const auto opening = UnitMixPolicy::openingMix(currentGame->techLevel, configured, available);
    UnitMixPolicy::Weights defaults{};
    for (size_t i = 0; i < 8; ++i) defaults[i] = opening[i];
    counterPlayed = false;
    if (const auto* profile = bot.strategyMix()) {
        const UnitMixPolicy::Weights prior = profile->counter ? bot.enemyCounterPrior() : profile->prior;
        counterPlayed = profile->counter && *std::max_element(prior.begin(), prior.end()) > 0;
        defaults = QuantBotStrategy::applyPrior(defaults, prior, profile->priorBps, available);
    }
    const auto mix = UnitMixPolicy::normalize(defaults);
    return antiAirFloorBps > 0 ? UnitMixPolicy::applyFloor(mix, 2, antiAirFloorBps) : mix;
}

/// The share of the value of the vehicles and aircraft \a house has built (factories and
/// Starport), per mix slot; \a counts gets the numbers.
inline std::array<int, 8> builtValueBps(Game& game, int house, std::array<int, 8>* counts = nullptr) {
    const House* owner = game.getHouse(static_cast<HOUSETYPE>(house));
    auto valueOf = [&](Uint32 item) { return int64_t(owner->getNumBuiltItems(item)) * game.objectData.data[item][house].price; };
    std::array<int64_t, 8> value{};
    for (size_t i = 0; i < 8; ++i) {
        value[i] = valueOf(kSlotItems[i]);
        if (counts) (*counts)[i] = owner->getNumBuiltItems(kSlotItems[i]);
    }
    value[3] = valueOf(Unit_Devastator) + valueOf(Unit_SonicTank) + valueOf(Unit_Deviator);
    if (counts) (*counts)[3] = owner->getNumBuiltItems(Unit_Devastator) + owner->getNumBuiltItems(Unit_SonicTank)
        + owner->getNumBuiltItems(Unit_Deviator);
    int64_t total = 0;
    for (const auto v : value) total += v;
    std::array<int, 8> bps{};
    for (size_t i = 0; i < 8; ++i) bps[i] = total > 0 ? static_cast<int>(value[i] * 10000 / total) : 0;
    return bps;
}

struct GameResult {
    std::map<int, Production> houses;
    std::vector<std::string> digests;   ///< once a game minute and at the end
    std::string bots;
    long long wallMs = 0;
};

/// Plays a Fortress 1v1 to \a endCycle, each house with \a strategies (none = the global config),
/// checking every production pass against the prior.
inline GameResult playProduction(int first, int second, const std::map<int, QuantBotStrategy::Settings>& strategies,
                                 Uint32 mapSeed, Uint32 seed, Uint32 endCycle) {
    const auto wall = Clock::now();
    auto game = start(fortressInit(first, second, mapSeed, seed));
    GameResult result;
    for (const int house : {first, second}) {
        QuantBot* bot = botOf(*game, house);
        require(bot != nullptr, "A production house has no QuantBot");
        const auto found = strategies.find(house);
        if (found != strategies.end()) setStrategy(*bot, found->second);
        result.houses[house].house = house;
    }
    std::map<int, Uint32> lastPass;
    while (!game->finished && game->getGameCycleCount() < endCycle) {
        tick(*game);
        const Uint32 now = game->getGameCycleCount();
        if (now % MILLI2CYCLES(60000) == 0) result.digests.push_back(GameStateDigest::describe(game->computeStateDigest()));
        for (const int house : {first, second}) {
            QuantBot* bot = botOf(*game, house);
            Production& production = result.houses[house];
            const auto& snapshot = bot->getAllocationSnapshot();
            // A production pass ran in this update (the snapshot carries the cycle it ran at).
            if (!snapshot.ready || snapshot.cycle + 1 != now || lastPass[house] == now) continue;
            lastPass[house] = now;
            ++production.passes;
            const auto& mix = bot->getLastUnitMixBps();
            production.infantryQuotaBps = snapshot.infantryQuotaBps;
            GameStateDigest::Hasher pass;
            pass.mixUint32(snapshot.cycle);
            for (const int share : mix) pass.mixInt32(share);
            production.mixTrace = production.mixTrace * 1000003 + pass.value();
            std::array<bool, 8> available{};
            bool any = false;
            for (size_t i = 0; i < 8; ++i) {
                any |= available[i] = snapshot.slots[i].allocated;
                if (available[i] && production.firstAllocated[i] == 0) production.firstAllocated[i] = snapshot.cycle;
            }
            if (!any) continue;
            production.airFloorPasses += snapshot.antiAirFloorBps > 0 ? 1 : 0;
            bool counterPlayed = false;
            const auto expected = priorMix(*bot, available, snapshot.antiAirFloorBps, counterPlayed);
            const bool same = std::equal(mix.begin(), mix.end(), expected.begin());
            if (!snapshot.learning) {
                if (production.priorPasses++ == 0) { production.firstPriorCycle = snapshot.cycle; production.firstPriorMix = expected; }
                if (counterPlayed && production.counterPasses++ == 0) production.firstCounterMix = expected;
                if (same) ++production.priorMatches;
                else if (production.mismatch.empty())
                    production.mismatch = "cycle=" + std::to_string(snapshot.cycle) + " mix=" + join(mix) + " expected=" + join(expected);
            } else {
                if (production.learningPasses++ == 0) {
                    production.firstLearningCycle = snapshot.cycle;
                    production.earlyValueBps = builtValueBps(*game, house);
                }
                if (!same && production.learningOffPrior++ == 0) {
                    production.firstOffPriorCycle = snapshot.cycle;
                    for (size_t i = 0; i < 8; ++i) production.offPriorMix[i] = mix[i];
                    production.offPriorExpected = expected;
                }
            }
        }
    }
    require(game->getGameCycleCount() == endCycle, "A production game ended before its time");
    for (const int house : {first, second}) {
        const QuantBot* bot = botOf(*game, house);
        Production& production = result.houses[house];
        production.strategy = bot->strategy;
        production.inert = bot->strategyMix() == nullptr && bot->strategyTempo() == nullptr;
        production.valueBps = builtValueBps(*game, house, &production.built);
        const House* owner = game->getHouse(static_cast<HOUSETYPE>(house));
        production.builtInfantry = owner->getNumBuiltItems(Unit_Soldier) + owner->getNumBuiltItems(Unit_Infantry)
            + owner->getNumBuiltItems(Unit_Trooper) + owner->getNumBuiltItems(Unit_Troopers);
    }
    result.bots = std::to_string(hashOf(botBytes(*botOf(*game, first)))) + " " + std::to_string(hashOf(botBytes(*botOf(*game, second))));
    result.digests.push_back(GameStateDigest::describe(game->computeStateDigest()));
    end(game);
    result.wallMs = millisecondsSince(wall);
    return result;
}

inline void logProduction(const char* check, const Production& p, const Production* learned, long long wallMs,
                          const std::string& extra = "") {
    SDL_Log("STRATEGY_PROBE: check=%s house=%d strategy=%s passes=%d prior_passes=%d prior_matches=%d first_prior_cycle=%u "
            "first_prior_mix=%s counter_passes=%d first_counter_mix=%s learning_passes=%d first_learning_cycle=%u off_prior=%d "
            "first_off_prior_cycle=%u off_prior_mix=%s prior_then=%s slot_first_cycles=%s air_floor_passes=%d infantry_quota_bps=%d built=%s "
            "value_bps=%s early_value_bps=%s infantry=%d learned_built=%s learned_value_bps=%s learned_early_value_bps=%s learned_infantry=%d%s wall_ms=%lld%s%s",
            check, p.house, text(p.strategy).c_str(), p.passes, p.priorPasses, p.priorMatches, p.firstPriorCycle,
            join(p.firstPriorMix).c_str(), p.counterPasses, join(p.firstCounterMix).c_str(), p.learningPasses, p.firstLearningCycle,
            p.learningOffPrior, p.firstOffPriorCycle, join(p.offPriorMix).c_str(), join(p.offPriorExpected).c_str(),
            join(p.firstAllocated).c_str(), p.airFloorPasses, p.infantryQuotaBps, join(p.built).c_str(), join(p.valueBps).c_str(), join(p.earlyValueBps).c_str(), p.builtInfantry,
            learned ? join(learned->built).c_str() : "-", learned ? join(learned->valueBps).c_str() : "-",
            learned ? join(learned->earlyValueBps).c_str() : "-",
            learned ? learned->builtInfantry : -1, extra.c_str(), wallMs, p.mismatch.empty() ? "" : " mismatch=", p.mismatch.c_str());
}

/// The mix slots a strategy leads with (the largest share of its prior; Artillery and TrikeSwarm
/// lead with two). Counter leads with the largest share of the counter it first played.
inline std::vector<size_t> leadSlots(Mix mix, const Production& p) {
    switch (mix) {
        case Mix::QuadMass: return {7};
        case Mix::TrikeSwarm: return {5, 6};
        case Mix::TankMass: return {0};
        case Mix::Artillery: return {1, 2};
        case Mix::Specials: return {3};
        case Mix::Air: return {4};
        case Mix::Counter:
            return {size_t(std::max_element(p.firstCounterMix.begin(), p.firstCounterMix.end()) - p.firstCounterMix.begin())};
        default: return {};
    }
}

/// The lead slots must get a larger share of the built value than in the Learned game. Measured
/// (12 game minutes, mapSeed 7, seed 486409243; lead share vs Learned's): QuadMass quads 2413 vs
/// 960, Artillery siege+launchers 5318 vs 4183, TankMass tanks 4571 vs 1703, TrikeSwarm trikes 1578
/// vs 385, Specials 2253 vs 1048, Counter (tanks, its first counter's lead) 2161 vs 660; Air is
/// checked by its ornithopters (checkMix). A fixed margin is not asserted: one 12-minute game per
/// mix is one trajectory of the whole fight, and over six seeds (486409243, 11, 22, 33, 44, 55),
/// with and without the perimeter hold (V), the margins ranged +88..+2974 (mean ~+1250), and
/// the 500-bps margin this check used to require failed 3 of 36 checks with the hold and 5 of 36
/// without it, spread over all six mixes (docs/formations.md, "Free-for-all: finishing the
/// game"). The largest-share slot is not asserted either: Starport ornithopters and the mobile
/// anti-air floor's launchers are bought outside the mix and can outweigh a lead slot.
constexpr int kLeadMarginBps = 1;

/// Section 3's checks for one house playing Fixed:<mix>,Base, against the Learned game.
inline void checkMix(Mix mix, const Production& p, const Production& learned, long long wallMs) {
    const std::vector<size_t> lead = leadSlots(mix, p);
    int leadBps = 0, learnedLeadBps = 0;
    for (size_t i = 0; i < 8; ++i) {
        const bool isLead = std::find(lead.begin(), lead.end(), i) != lead.end();
        if (isLead) { leadBps += p.valueBps[i]; learnedLeadBps += learned.valueBps[i]; }
    }
    std::string leadText;
    for (const size_t slot : lead) leadText += (leadText.empty() ? "" : "+") + std::to_string(slot);
    logProduction("mix_production", p, &learned, wallMs, " lead_slots=" + (leadText.empty() ? std::string("-") : leadText)
        + " lead_value_bps=" + std::to_string(leadBps) + " learned_lead_value_bps=" + std::to_string(learnedLeadBps));
    const std::string name = QuantBotStrategy::name(mix);
    require(p.strategy == Choice{mix, Tempo::Base} && !p.inert, name + ": the house did not play its fixed strategy");
    // Before combat evidence every pass is the prior, exactly.
    require(p.priorPasses > 0 && p.priorMatches == p.priorPasses, name + ": a pass before combat evidence was not the prior");
    // The learning stays on: once it is active the mix leaves the prior.
    require(p.learningPasses > 0 && p.learningOffPrior > 0, name + ": learning never moved the mix off the prior");
    if (mix == Mix::Infantry) {
        require(p.infantryQuotaBps == QuantBotStrategy::profile(mix).infantryPercent * 100,
                "Infantry: the infantry quota is not the strategy's share");
        require(p.builtInfantry > 0 && p.builtInfantry > learned.builtInfantry, "Infantry: no more infantry than the Learned game");
        return;
    }
    if (mix == Mix::Counter)
        require(p.counterPasses > 0 && p.firstCounterMix != learned.firstPriorMix, "Counter: the counter of the seen enemy was never played");
    // What was built leads with the prior's lead slots, and more so than the Learned game did.
    if (mix == Mix::Air) {
        // Every house buys Starport ornithopters (Learned's first minutes are 51% ornithopter value),
        // so the share hardly separates them. The Air strategy upgrades its Hi-Tech Factory to make
        // ornithopters producible while its prior holds, and builds more of them. Measured: slot 4
        // producible from cycle 4579 (learning from 15879), 12 ornithopters vs Learned's 9; value
        // share 2271 vs 1981.
        require(p.firstAllocated[4] > 0 && p.firstAllocated[4] < p.firstLearningCycle,
                "Air: ornithopters were not producible while the prior held");
        require(p.built[4] > learned.built[4], "Air: no more ornithopters than the Learned game");
        return;
    }
    require(leadBps >= learnedLeadBps + kLeadMarginBps, name + ": the lead slots got no larger share of the built value than in the Learned game");
}

} // namespace StrategyProbe

int runQuantBotStrategyProbe() {
    using namespace StrategyProbe;
    const auto probeStart = Clock::now();
    const std::string requestedMod = std::getenv("BALANCE_MOD") ? std::getenv("BALANCE_MOD") : "vanilla";
    if (!ModManager::instance().setActiveMod(requestedMod) || ModManager::instance().getActiveModName() != requestedMod)
        throw std::runtime_error("Requested probe mod did not activate: " + requestedMod);
    settings.general.playerName = "Strategy probe observer";
    settings.network.metaServer = "http://127.0.0.1:1/";
    settings.gameOptions.fogOfWar = false;
    settings.gameOptions.immortalHumanPlayer = false;
    const auto seed = static_cast<Uint32>(std::stoul(std::getenv("BALANCE_SEED")));
    // Calibration only: BALANCE_STRATEGY_SECTIONS=34 runs just those numbered checks; CTest runs all.
    const char* sections = std::getenv("BALANCE_STRATEGY_SECTIONS");
    auto wanted = [&](char section) { return !sections || std::strchr(sections, section) != nullptr; };

    // The shipped configuration plays the compiled [Strategy] default (Fixed:Learned,Swarm; Mode=Random
    // plays the measured weights); every Learned/Base reference below is set explicitly.
    const QuantBotStrategy::Settings defaults;
    require(QuantBotStrategy::modeText(getQuantBotConfig().strategy) == QuantBotStrategy::modeText(defaults)
            && getQuantBotConfig().strategy.mixWeights == defaults.mixWeights
            && getQuantBotConfig().strategy.tempoWeights == defaults.tempoWeights,
            "The shipped QuantBot configuration does not play the compiled [Strategy] default");
    SDL_Log("STRATEGY_PROBE: check=default mode=%s mix_weights=%s tempo_weights=%s", QuantBotStrategy::modeText(defaults).c_str(),
            QuantBotStrategy::weightsText(defaults.mixWeights).c_str(), QuantBotStrategy::weightsText(defaults.tempoWeights).c_str());
    const QuantBotStrategy::Settings shipped = getQuantBotConfig().strategy;

    if (wanted('1') || wanted('2')) {
        Scenario scenario;
        scenario.mapPath = std::getenv("BALANCE_CUSTOM_MAP");
        std::ifstream file(scenario.mapPath);
        require(bool(file), "Cannot read the custom map");
        scenario.mapText.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        std::istringstream input(std::getenv("BALANCE_ROSTER") ? std::getenv("BALANCE_ROSTER") : "");
        std::string slot;
        while (std::getline(input, slot, ',')) {
            const auto colon = slot.find(':');
            require(colon != std::string::npos, "Invalid roster");
            scenario.roster.emplace_back(std::stoi(slot.substr(0, colon)), std::stoi(slot.substr(colon + 1)));
        }
        require(scenario.roster.size() >= 2, "The strategy probe needs a multi-house map");
        // Every mix and tempo weighted, so the houses spread over the strategies.
        QuantBotStrategy::Settings spread;
        spread.random = true;
        spread.mixWeights.fill(10);
        spread.tempoWeights.fill(10);
        getQuantBotConfig().strategy = spread;
        // Measured on Four Chambers, seed 486409243: the strategies first change the state digest in game
        // minute 12 (no house attacks earlier), so the save is taken there and the peers play to 16.
        const Uint32 saveCycle = MILLI2CYCLES(12 * 60000), endCycle = MILLI2CYCLES(16 * 60000);
        const std::string savePath = "strategy-probe.sav";
        std::string runtime;
        World host;
        runPeers(scenario, seed, seed + 1, saveCycle, endCycle, savePath, runtime, host);
        if (wanted('2')) checkBotBlocks(scenario, seed, savePath, runtime, host, saveCycle, endCycle);
        getQuantBotConfig().strategy = shipped;
    }

    if (wanted('3') || wanted('4')) {
        const Uint32 mapSeed = 7, endCycle = MILLI2CYCLES(12 * 60000);
        const int harkonnen = HOUSE_HARKONNEN, atreides = HOUSE_ATREIDES;
        // The Learned game every mix game is compared with: both houses Fixed:Learned,Base (1.0.846).
        const GameResult learned = playProduction(harkonnen, atreides,
            {{harkonnen, fixedStrategy(Mix::Learned)}, {atreides, fixedStrategy(Mix::Learned)}}, mapSeed, seed, endCycle);
        for (const auto& [house, production] : learned.houses) {
            logProduction("learned_reference", production, nullptr, learned.wallMs);
            require(production.strategy == Choice{} && production.inert, "The shipped default did not play Learned/Base");
            require(production.priorPasses > 0 && production.priorMatches == production.priorPasses,
                    "A Learned pass before combat evidence was not the configured opening mix");
        }
        if (wanted('4')) {
            // Fixed:Learned,Base explicitly, and Random weighted only on Learned/Base: both the
            // configured default's game, pass for pass.
            QuantBotStrategy::Settings onlyLearned;
            onlyLearned.random = true;
            onlyLearned.mixWeights = {};
            onlyLearned.mixWeights[size_t(Mix::Learned)] = 1;
            onlyLearned.tempoWeights = {};
            onlyLearned.tempoWeights[size_t(Tempo::Base)] = 1;
            const GameResult explicitly = playProduction(harkonnen, atreides,
                {{harkonnen, fixedStrategy(Mix::Learned)}, {atreides, onlyLearned}}, mapSeed, seed, endCycle);
            for (const int house : {harkonnen, atreides}) {
                const Production& a = learned.houses.at(house);
                const Production& b = explicitly.houses.at(house);
                SDL_Log("STRATEGY_PROBE: check=learned_base_inert house=%d config=%s strategy=%s inert=%d passes=%d/%d "
                        "mix_trace=%llu/%llu built=%s/%s minute_digests_equal=%d/%zu bots_equal=%d wall_ms=%lld", house,
                        house == harkonnen ? "Fixed:Learned,Base" : "Random,MixWeights=Learned:1,TempoWeights=Base:1",
                        text(b.strategy).c_str(), int(b.inert), a.passes, b.passes, static_cast<unsigned long long>(a.mixTrace),
                        static_cast<unsigned long long>(b.mixTrace), join(a.built).c_str(), join(b.built).c_str(),
                        int(learned.digests == explicitly.digests), explicitly.digests.size(), int(learned.bots == explicitly.bots),
                        explicitly.wallMs);
                require(b.strategy == Choice{} && b.inert, "An explicit Learned/Base config played a strategy");
                require(a.passes == b.passes && a.mixTrace == b.mixTrace, "Learned/Base changed a production pass's mix");
            }
            require(learned.digests == explicitly.digests && learned.bots == explicitly.bots,
                    "Learned/Base played a different game than the configured default");
        }
        if (wanted('3')) {
            // Two mix strategies a game, one a house (Harkonnen fields no trikes or ornithopters).
            const std::vector<std::pair<Mix, Mix>> games = {
                {Mix::QuadMass, Mix::Artillery}, {Mix::TankMass, Mix::TrikeSwarm}, {Mix::Specials, Mix::Air}, {Mix::Infantry, Mix::Counter}};
            for (const auto& [mixH, mixA] : games) {
                const GameResult played = playProduction(harkonnen, atreides,
                    {{harkonnen, fixedStrategy(mixH)}, {atreides, fixedStrategy(mixA)}}, mapSeed, seed, endCycle);
                checkMix(mixH, played.houses.at(harkonnen), learned.houses.at(harkonnen), played.wallMs);
                checkMix(mixA, played.houses.at(atreides), learned.houses.at(atreides), played.wallMs);
            }
        }
    }
    SDL_Log("STRATEGY_PROBE: check=done wall_ms=%lld", millisecondsSince(probeStart));
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=quantbot_strategy outcome=passed");
    return MENU_QUIT_DEFAULT;
}
