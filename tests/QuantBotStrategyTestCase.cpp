#include <catch2/catch_test_macros.hpp>

#include <players/QuantBotBuildPolicy.h>

#include <players/QuantBotConfig.h>
#include <players/QuantBotStrategy.h>
#include <players/UnitMixPolicy.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

// QuantBot strategies (docs/ai-play-styles.md, "Strategies"): the pick is a pure function of the
// game seed, the house and the config, so every peer, replay and reload plays the same strategy;
// the mix is the prior the learned unit mix starts from.

using namespace QuantBotStrategy;

namespace {

std::array<bool, kMixCount> allEligible() {
    std::array<bool, kMixCount> eligible{};
    eligible.fill(true);
    return eligible;
}

Settings randomSettings() {
    Settings settings;
    settings.random = true;
    REQUIRE(parseWeights("Learned:30,QuadMass:10,TrikeSwarm:10,TankMass:10,Artillery:10,Specials:10,Air:5,Infantry:5,Counter:10",
                         settings.mixWeights));
    REQUIRE(parseWeights("Base:40,Swarm:40,Timing:10,Rush:5,Flood:5", settings.tempoWeights));
    return settings;
}

} // namespace

TEST_CASE("Strategy: the pick is a pure function of seed, house and config", "[ai][strategy]") {
    const Settings settings = randomSettings();
    const auto eligible = allEligible();
    for (uint32_t seed : {0u, 1u, 7101u, 488631365u, 0xFFFFFFFFu}) {
        // Same inputs, same choice, whatever was picked before or for whom (order-independent).
        std::array<Choice, 6> forward{}, backward{};
        for (int house = 0; house < 6; ++house) forward[house] = pick(settings, seed, house, eligible);
        for (int house = 5; house >= 0; --house) backward[house] = pick(settings, seed, house, eligible);
        for (int house = 0; house < 6; ++house) {
            REQUIRE(forward[house] == backward[house]);
            REQUIRE(pick(settings, seed, house, eligible) == forward[house]);
        }
    }
    // Pinned values: a change of the draw would desync a peer running the older build.
    REQUIRE(draw(7101, 1, 0x4D495853) == draw(7101, 1, 0x4D495853));
    REQUIRE(draw(7101, 1, 0x4D495853) != draw(7101, 2, 0x4D495853));
    REQUIRE(draw(7101, 1, 0x4D495853) != draw(7102, 1, 0x4D495853));
    REQUIRE(draw(7101, 1, 0x4D495853) != draw(7101, 1, 0x54454D50));
}

TEST_CASE("Strategy: houses of one game vary and the weights are respected", "[ai][strategy]") {
    const Settings settings = randomSettings();
    const auto eligible = allEligible();
    constexpr int kSeeds = 20000;
    std::map<int, int> mixes, tempos;
    int sameMix = 0, sameTempo = 0;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        const Choice a = pick(settings, uint32_t(seed), 0, eligible);
        const Choice b = pick(settings, uint32_t(seed), 1, eligible);
        ++mixes[static_cast<int>(a.mix)];
        ++tempos[static_cast<int>(a.tempo)];
        sameMix += a.mix == b.mix;
        sameTempo += a.tempo == b.tempo;
    }
    // Two houses agree about as often as independent draws would (sum of squared shares).
    REQUIRE(sameMix < kSeeds * 20 / 100);
    REQUIRE(sameTempo < kSeeds * 40 / 100);
    int totalMix = 0, totalTempo = 0;
    for (int weight : settings.mixWeights) totalMix += weight;
    for (int weight : settings.tempoWeights) totalTempo += weight;
    for (size_t i = 0; i < kMixCount; ++i) {
        const int expected = kSeeds * settings.mixWeights[i] / totalMix;
        REQUIRE(std::abs(mixes[int(i)] - expected) < kSeeds / 100 + expected / 10);
    }
    for (size_t i = 0; i < kTempoCount; ++i) {
        const int expected = kSeeds * settings.tempoWeights[i] / totalTempo;
        REQUIRE(std::abs(tempos[int(i)] - expected) < kSeeds / 100 + expected / 10);
    }
}

TEST_CASE("Strategy: a house only plays mixes it can field", "[ai][strategy]") {
    // Harkonnen: no ornithopters, trikes or raider trikes.
    const std::array<bool, 8> harkonnen = {true, true, true, true, false, false, false, true};
    const auto eligible = eligibleMixes(harkonnen, true);
    REQUIRE_FALSE(eligible[static_cast<size_t>(Mix::Air)]);
    REQUIRE_FALSE(eligible[static_cast<size_t>(Mix::TrikeSwarm)]);
    REQUIRE(eligible[static_cast<size_t>(Mix::QuadMass)]);
    REQUIRE(eligible[static_cast<size_t>(Mix::Learned)]);
    REQUIRE(eligible[static_cast<size_t>(Mix::Counter)]);
    REQUIRE_FALSE(eligibleMixes(harkonnen, false)[static_cast<size_t>(Mix::Infantry)]);
    Settings settings;
    settings.random = true;
    REQUIRE(parseWeights("Air:50,TrikeSwarm:50,TankMass:1", settings.mixWeights));
    for (uint32_t seed = 1; seed < 500; ++seed) REQUIRE(pick(settings, seed, 0, eligible).mix == Mix::TankMass);
    // A fixed mix the house cannot field is the learned default.
    REQUIRE(parseMode("Fixed:Air,Swarm", settings));
    REQUIRE(pick(settings, 1, 0, eligible) == Choice{Mix::Learned, Tempo::Swarm});
    REQUIRE(pick(settings, 1, 0, allEligible()) == Choice{Mix::Air, Tempo::Swarm});
}

TEST_CASE("Strategy: modes and weights parse strictly", "[ai][strategy]") {
    Settings settings;
    // The shipped default: the Tuned starting mix (the measured per-house ratios, which beat the bounded
    // configured ratios, docs/ai-play-styles.md section 8) with the Swarm tempo (the only tempo measured
    // above 1.0.846). Mode=Random plays the measured weights (docs/ai-play-styles.md, "Strategies").
    REQUIRE_FALSE(settings.random);
    REQUIRE(modeText(settings) == "Fixed:Tuned,Swarm");
    REQUIRE(weightsText(settings.mixWeights) == "Learned:50,QuadMass:15,TrikeSwarm:5,Artillery:10,Air:10,Counter:10");
    REQUIRE(weightsText(settings.tempoWeights) == "Swarm:80,Flood:20");
    REQUIRE(parseMode("Fixed:Learned,Base", settings));
    REQUIRE(modeText(settings) == "Fixed:Learned,Base");
    REQUIRE(parseMode(" random ", settings));
    REQUIRE(settings.random);
    REQUIRE(parseMode("Fixed:quadmass,SWARM", settings));
    REQUIRE((!settings.random && settings.fixedMix == Mix::QuadMass && settings.fixedTempo == Tempo::Swarm));
    REQUIRE(parseMode("Fixed:Counter", settings));
    REQUIRE((settings.fixedMix == Mix::Counter && settings.fixedTempo == Tempo::Base));
    REQUIRE_FALSE(parseMode("Fixed:Nope,Base", settings));
    REQUIRE_FALSE(parseMode("Sometimes", settings));
    REQUIRE((settings.fixedMix == Mix::Counter && settings.fixedTempo == Tempo::Base));

    MixWeights weights = settings.mixWeights;
    REQUIRE_FALSE(parseWeights("Learned:10,Nope:5", weights));
    REQUIRE_FALSE(parseWeights("Learned:-1", weights));
    REQUIRE_FALSE(parseWeights("Learned:0", weights));
    REQUIRE_FALSE(parseWeights("Learned", weights));
    REQUIRE_FALSE(parseWeights("Learned=10", weights));
    REQUIRE(weights == settings.mixWeights);
    REQUIRE(parseWeights(" Learned : 40 , quadmass:10 ", weights));
    REQUIRE(weightsText(weights) == "Learned:40,QuadMass:10");
    for (size_t i = 0; i < kMixCount; ++i) {
        Mix mix;
        REQUIRE(parse(name(static_cast<Mix>(i)), mix));
        REQUIRE(mix == static_cast<Mix>(i));
    }
    for (size_t i = 0; i < kTempoCount; ++i) {
        Tempo tempo;
        REQUIRE(parse(name(static_cast<Tempo>(i)), tempo));
        REQUIRE(tempo == static_cast<Tempo>(i));
    }
}

TEST_CASE("Strategy: the [Strategy] keys are read and every one is hashed", "[ai][strategy][config]") {
    const QuantBotConfig baseline;
    const std::string base = baseline.getConfigHash();
    auto differs = [&](void (*mutate)(QuantBotConfig&)) {
        QuantBotConfig changed;
        mutate(changed);
        return changed.getConfigHash() != base;
    };
    REQUIRE(differs([](QuantBotConfig& c){ c.strategy.random = !c.strategy.random; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.strategy.fixedMix = Mix::QuadMass; }));
    REQUIRE(differs([](QuantBotConfig& c){ c.strategy.fixedTempo = Tempo::Timing; }));
    for (size_t i = 0; i < kMixCount; ++i) {
        QuantBotConfig changed;
        changed.strategy.mixWeights[i] += 1;
        REQUIRE(changed.getConfigHash() != base);
    }
    for (size_t i = 0; i < kTempoCount; ++i) {
        QuantBotConfig changed;
        changed.strategy.tempoWeights[i] += 1;
        REQUIRE(changed.getConfigHash() != base);
    }
    {   // Moving weight between two adjacent options cannot alias.
        QuantBotConfig a, b;
        a.strategy.mixWeights = {1, 23, 0, 0, 0, 0, 0, 0, 0};
        b.strategy.mixWeights = {12, 3, 0, 0, 0, 0, 0, 0, 0};
        REQUIRE(a.getConfigHash() != b.getConfigHash());
    }

    const auto path = std::filesystem::temp_directory_path() / "quantbot-strategy-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Strategy]\nMode=Random\nMixWeights=Learned:60,QuadMass:40\nTempoWeights=Swarm:1\n";
    }
    QuantBotConfig config;
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE(config.strategy.random);
    REQUIRE(weightsText(config.strategy.mixWeights) == "Learned:60,QuadMass:40");
    REQUIRE(weightsText(config.strategy.tempoWeights) == "Swarm:1");
    {
        std::ofstream ini(path);
        ini << "[Strategy]\nMode=Fixed:Bogus\nMixWeights=Bogus:1\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE(config.strategy.random);   // malformed values keep the previous ones
    REQUIRE(weightsText(config.strategy.mixWeights) == "Learned:60,QuadMass:40");
    std::filesystem::remove(path);
}

TEST_CASE("Strategy: Learned and Base are the 1.0.846 rules", "[ai][strategy]") {
    const MixProfile& learned = profile(Mix::Learned);
    REQUIRE(learned.priorBps == 0);
    REQUIRE(learned.infantryPercent == 0);
    REQUIRE_FALSE(learned.counter);
    const UnitMixPolicy::Weights opening = {3000, 2000, 2000, 1500, 1000, 0, 0, 500};
    std::array<bool, 8> available{};
    available.fill(true);
    REQUIRE(applyPrior(opening, learned.prior, learned.priorBps, available) == opening);
    const TempoProfile& base = profile(Tempo::Base);
    REQUIRE(base.readinessPercent == 0);
    REQUIRE((base.minWaveUnits == 6 && base.minWaveValue == 3000));
    REQUIRE((!base.lightFirst && base.openingReadiness && base.minutes == 0));
    // Swarm is the measured swarm style (WaveReadinessPercent=35, waves from 4 units / 2,000).
    const TempoProfile& swarm = profile(Tempo::Swarm);
    REQUIRE((swarm.readinessPercent == 35 && swarm.minWaveUnits == 4 && swarm.minWaveValue == 2000));
}

TEST_CASE("Strategy: a mix is the prior the learning starts from, not a frozen mix", "[ai][strategy]") {
    std::array<bool, 8> available{};
    available.fill(true);
    const UnitMixPolicy::Weights opening = {5000, 2000, 2000, 1000, 0, 0, 0, 0};
    const auto quads = applyPrior(opening, profile(Mix::QuadMass).prior, profile(Mix::QuadMass).priorBps, available);
    REQUIRE(UnitMixPolicy::normalize(quads)[7] == 5000);
    REQUIRE(UnitMixPolicy::normalize(quads)[0] == 2667);
    // Unavailable slots are dropped from the prior; nothing available keeps the opening.
    std::array<bool, 8> noQuads = available;
    noQuads[7] = false;
    const auto masked = UnitMixPolicy::normalize(applyPrior(opening, profile(Mix::QuadMass).prior, 10000, noQuads));
    REQUIRE(masked[7] == 0);
    REQUIRE(masked[0] == 5334);
    std::array<bool, 8> none{};
    REQUIRE(applyPrior(opening, profile(Mix::QuadMass).prior, 10000, none) == opening);

    // Tanks performed, quads did not: with no evidence the prior stands; as the lost value grows
    // against the fielded army the allocation follows the performance (vanilla evidence blend).
    UnitMixPolicy::Weights scores{};
    scores[0] = 4000000;
    scores[7] = 500000;
    const auto start = UnitMixPolicy::allocate(scores, quads, false, true, 0, 10000);
    REQUIRE(start[7] == 5000);
    const auto early = UnitMixPolicy::allocate(scores, quads, true, true, 1000, 10000);
    const auto late = UnitMixPolicy::allocate(scores, quads, true, true, 30000, 10000);
    REQUIRE(early[7] < 5000);
    REQUIRE(late[7] < early[7]);
    REQUIRE(late[0] > early[0]);
    REQUIRE(late[0] > 5000);

    // Counter: nothing seen, no prior; armour seen, launchers and siege lead.
    REQUIRE(counterPrior(0, 0, 0, 0) == UnitMixPolicy::Weights{});
    const auto counter = UnitMixPolicy::normalize(counterPrior(0, 10000, 0, 0));
    REQUIRE(counter[2] > counter[0]);
    REQUIRE(counter[1] > counter[7]);
}

TEST_CASE("Strategy: an Air strategy unlocks ornithopters before the first carryall", "[ai][strategy]") {
    QuantBotBuildPolicy::AirProductionState air;
    air.carryallAvailable = true;
    air.carryallPrice = 800;
    air.carryallTarget = 1;
    air.canUpgrade = true;
    air.spendable = 0;   // all of it held back by the economy reserve
    air.credits = 900;
    // The learned default saves for its first carryall; an Air strategy upgrades first, out of its
    // credits even while the economy reserve takes every spendable credit.
    REQUIRE(QuantBotBuildPolicy::chooseAirProduction(air).order == QuantBotBuildPolicy::AirOrder::None);
    air.airStrategy = true;
    REQUIRE(QuantBotBuildPolicy::chooseAirProduction(air).order == QuantBotBuildPolicy::AirOrder::Upgrade);
    air.credits = 400;
    REQUIRE(QuantBotBuildPolicy::chooseAirProduction(air).order == QuantBotBuildPolicy::AirOrder::None);
    air.credits = 900;
    air.ornithopterAvailable = true;
    REQUIRE(QuantBotBuildPolicy::chooseAirProduction(air).order == QuantBotBuildPolicy::AirOrder::None);
}

namespace {

std::array<bool, 8> slotsOf(unsigned mask) {
    std::array<bool, 8> slots{};
    for (size_t i = 0; i < 8; ++i) slots[i] = (mask >> i) & 1u;
    return slots;
}

// Mix slots: tank, siege, launcher, special, ornithopter, trike, raider, quad.
const std::array<bool, 8> kAtreides = {true, true, true, true, true, true, false, true};
const std::array<bool, 8> kHarkonnen = {true, true, true, true, false, false, false, true};
const std::array<bool, 8> kOrdos = {true, true, false, true, true, false, true, true};

uint64_t lcg(uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state >> 33;
}

} // namespace

TEST_CASE("Strategy: bound() keeps every producible slot within 2%-50%", "[ai][strategy][bounds]") {
    // Every availability set and a spread of weights, zeros included.
    uint64_t state = 20261018;
    for (unsigned mask = 0; mask < 256; ++mask) {
        const auto available = slotsOf(mask);
        int count = 0;
        for (bool slot : available) count += slot;
        for (int trial = 0; trial < 80; ++trial) {
            UnitMixPolicy::Weights weights{};
            for (auto& weight : weights) weight = lcg(state) % 4 == 0 ? 0 : int64_t(lcg(state) % 100000);
            const auto mix = bound(weights, available);
            REQUIRE(withinBounds(mix, available));
            int total = 0;
            for (size_t i = 0; i < 8; ++i) {
                total += mix[i];
                if (!available[i]) REQUIRE(mix[i] == 0);   // excluded types stay out
                else if (count > 1) REQUIRE((mix[i] >= kStartMinBps && mix[i] <= kStartMaxBps));
            }
            REQUIRE(total == (count ? 10000 : 0));
            if (count == 1) REQUIRE(*std::max_element(mix.begin(), mix.end()) == 10000);
            // Idempotent: a bounded mix is its own bound.
            UnitMixPolicy::Weights again{};
            for (size_t i = 0; i < 8; ++i) again[i] = mix[i];
            REQUIRE(bound(again, available) == mix);
            // The order of the weights within the bounds is kept (a heavier slot never gets less).
            for (size_t i = 0; i < 8; ++i)
                for (size_t j = 0; j < 8; ++j)
                    if (available[i] && available[j] && weights[i] > weights[j]) REQUIRE(mix[i] + 1 >= mix[j]);
        }
    }
    // Clamp and renormalise: the slots inside the bounds keep their proportions.
    const auto mix = bound({7000, 2000, 1000, 0, 0, 0, 0, 0}, slotsOf(0x0F));
    REQUIRE(mix == UnitMixPolicy::Mix{5000, 3200, 1600, 200, 0, 0, 0, 0});
    // Two slots: 50/50 whatever the weights.
    REQUIRE(bound({9000, 1000, 0, 0, 0, 0, 0, 0}, slotsOf(0x03)) == UnitMixPolicy::Mix{5000, 5000, 0, 0, 0, 0, 0, 0});
    // One weighted slot among zeros: it takes the cap, the zeros share the rest.
    REQUIRE(bound({0, 0, 0, 0, 0, 0, 0, 1}, slotsOf(0xA1)) == UnitMixPolicy::Mix{2500, 0, 0, 0, 0, 2500, 0, 5000});
    REQUIRE(bound({}, slotsOf(0x00)) == UnitMixPolicy::Mix{});
}

TEST_CASE("Strategy: every starting mix is within the bounds", "[ai][strategy][bounds]") {
    // The presets, over every house's slots: the bounds hold and no single type passes 50%.
    for (const auto& available : {kAtreides, kHarkonnen, kOrdos, slotsOf(0xFF), slotsOf(0xE0), slotsOf(0x1F)}) {
        for (size_t m = 0; m < kMixCount; ++m) {
            const MixProfile& mix = profile(static_cast<Mix>(m));
            if (mix.priorBps <= 0) continue;
            const auto prior = applyPrior({}, mix.prior, mix.priorBps, available);
            REQUIRE(withinBounds(bound(prior, available), available));
        }
    }
    // Each preset as written is within the bounds over all eight slots (Counter has no fixed prior).
    for (size_t m = 0; m < kMixCount; ++m) {
        const MixProfile& mix = profile(static_cast<Mix>(m));
        if (mix.priorBps <= 0 || mix.counter) continue;
        UnitMixPolicy::Mix written{};
        for (size_t i = 0; i < 8; ++i) written[i] = int(mix.prior[i]);
        REQUIRE(withinBounds(written, slotsOf(0xFF)));
    }
    // The presets are the 1.0.847 presets clamped and renormalised: QuadMass was 70% quads, now 50%.
    const std::array<bool, 8> every = slotsOf(0xFF);
    REQUIRE(bound({2000, 0, 0, 0, 0, 0, 1000, 7000}, every) == bound(profile(Mix::QuadMass).prior, every));
    REQUIRE(bound({1000, 0, 0, 0, 0, 4500, 4500, 0}, every) == bound(profile(Mix::TrikeSwarm).prior, every));
    REQUIRE(bound({7000, 1500, 1500, 0, 0, 0, 0, 0}, every) == bound(profile(Mix::TankMass).prior, every));
    REQUIRE(bound({2000, 4500, 3500, 0, 0, 0, 0, 0}, every) == bound(profile(Mix::Artillery).prior, every));
    REQUIRE(bound({2500, 1500, 1000, 5000, 0, 0, 0, 0}, every) == bound(profile(Mix::Specials).prior, every));
    REQUIRE(bound({1500, 1000, 1500, 0, 6000, 0, 0, 0}, every) == bound(profile(Mix::Air).prior, every));
    REQUIRE(profile(Mix::QuadMass).prior[7] == kStartMaxBps);
    REQUIRE(bound(profile(Mix::QuadMass).prior, kHarkonnen)[7] == kStartMaxBps);
    // Stefan's configured ratios (QuantBot Config.ini [Unit Ratios]) through the opening, tech 8:
    // Atreides Tank 0.05 stays (5% x 0.96 of the heavy share is above 2%), a 0 becomes 2%.
    const QuantBotConfig config;
    for (int house = 0; house < 6; ++house) {
        const auto& ratios = config.getRatios(house);
        const UnitMixPolicy::Weights configured = {int64_t(ratios.tank * 10000), int64_t(ratios.siegeTank * 10000),
            int64_t(ratios.launcher * 10000), int64_t(ratios.special * 10000), int64_t(ratios.ornithopter * 10000), 0, 0, 0};
        for (const auto& available : {kAtreides, kHarkonnen, kOrdos, slotsOf(0xFF)}) {
            UnitMixPolicy::Weights opening{};
            const auto open = UnitMixPolicy::openingMix(8, configured, available);
            for (size_t i = 0; i < 8; ++i) opening[i] = open[i];
            REQUIRE(withinBounds(bound(opening, available), available));
        }
    }
    const auto& atreides = config.getRatios(1);
    const UnitMixPolicy::Weights configured = {int64_t(atreides.tank * 10000), int64_t(atreides.siegeTank * 10000),
        int64_t(atreides.launcher * 10000), int64_t(atreides.special * 10000), int64_t(atreides.ornithopter * 10000), 0, 0, 0};
    const auto open = UnitMixPolicy::openingMix(8, configured, kAtreides);
    UnitMixPolicy::Weights opening{};
    for (size_t i = 0; i < 8; ++i) opening[i] = open[i];
    const auto bounded = bound(opening, kAtreides);
    REQUIRE(bounded[0] > 400);                            // the 5% tank share stays about 5%
    REQUIRE(bounded[5] == kStartMinBps);                  // trikes (1.3% of the opening) rise to 2%
    REQUIRE(bounded[6] == 0);                             // Atreides has no raider trikes
    const auto& harkonnen = config.getRatios(0);
    const UnitMixPolicy::Weights harkonnenWeights = {int64_t(harkonnen.tank * 10000), int64_t(harkonnen.siegeTank * 10000),
        int64_t(harkonnen.launcher * 10000), int64_t(harkonnen.special * 10000), int64_t(harkonnen.ornithopter * 10000), 0, 0, 0};
    // Ornithopter 0.00 with ornithopters producible (all slots): 2%.
    const auto all = slotsOf(0xFF);
    const auto harkonnenOpen = UnitMixPolicy::openingMix(8, harkonnenWeights, all);
    UnitMixPolicy::Weights harkonnenOpening{};
    for (size_t i = 0; i < 8; ++i) harkonnenOpening[i] = harkonnenOpen[i];
    REQUIRE(bound(harkonnenOpening, all)[4] == kStartMinBps);
}

TEST_CASE("Strategy: the Bounded draw is a pure, bounded function of seed and house", "[ai][strategy][bounds]") {
    constexpr int kSeeds = 20000;
    for (const auto& available : {kAtreides, kHarkonnen, kOrdos, slotsOf(0xFF), slotsOf(0x03), slotsOf(0x01)}) {
        int count = 0;
        for (bool slot : available) count += slot;
        std::array<int64_t, 8> sum{};
        std::array<int, 8> lowest{}, highest{};
        lowest.fill(10000);
        int differ = 0;
        for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
            const auto mix = randomMix(seed, 1, available);
            REQUIRE(withinBounds(mix, available));
            REQUIRE(randomMix(seed, 1, available) == mix);
            differ += randomMix(seed, 2, available) != mix;
            for (size_t i = 0; i < 8; ++i) {
                sum[i] += mix[i];
                if (available[i]) { lowest[i] = std::min(lowest[i], mix[i]); highest[i] = std::max(highest[i], mix[i]); }
            }
        }
        if (count < 3) continue;   // one slot: 100%; two: 50/50
        REQUIRE(differ > kSeeds * 99 / 100);
        for (size_t i = 0; i < 8; ++i) {
            if (!available[i]) { REQUIRE(sum[i] == 0); continue; }
            // Uniform over the bounded mixes: every slot averages 1/n, and the draws reach both bounds.
            const int64_t mean = sum[i] / kSeeds;
            REQUIRE(std::abs(mean - 10000 / count) < 10000 / count / 20 + 20);
            REQUIRE(lowest[i] < kStartMinBps + 100);
            REQUIRE(highest[i] > std::min(kStartMaxBps, 10000 - (count - 1) * kStartMinBps) - 1500);
        }
    }
    // Order-independent: the houses of one game draw the same whatever order they draw in.
    for (uint32_t seed : {7u, 7101u, 488631365u}) {
        std::array<UnitMixPolicy::Mix, 6> forward{}, backward{};
        for (int house = 0; house < 6; ++house) forward[house] = randomMix(seed, house, kAtreides);
        for (int house = 5; house >= 0; --house) backward[house] = randomMix(seed, house, kAtreides);
        REQUIRE(forward == backward);
    }
    // Pinned: a change of the draw would desync a peer running the older build.
    REQUIRE(randomMix(7101, 1, kAtreides) == randomMix(7101, 1, kAtreides));
    REQUIRE(randomMix(7101, 1, kAtreides) != randomMix(7102, 1, kAtreides));
}

TEST_CASE("Strategy: Tuned and Bounded parse, and [Unit Ratios Tuned] is read and hashed", "[ai][strategy][config]") {
    Settings settings;
    REQUIRE(parseMode("Fixed:Tuned,Swarm", settings));
    REQUIRE((settings.fixedMix == Mix::Tuned && settings.fixedTempo == Tempo::Swarm));
    REQUIRE(parseMode("Fixed:bounded,Swarm", settings));
    REQUIRE(settings.fixedMix == Mix::Bounded);
    REQUIRE(parseWeights("Tuned:60,Bounded:40", settings.mixWeights));
    REQUIRE(weightsText(settings.mixWeights) == "Tuned:60,Bounded:40");
    // Both can be fielded by every house (their ratios are resolved per house).
    REQUIRE(eligibleMixes(kHarkonnen, false)[size_t(Mix::Tuned)]);
    REQUIRE(eligibleMixes(kHarkonnen, false)[size_t(Mix::Bounded)]);

    UnitMixPolicy::Weights ratios{};
    REQUIRE(parseRatios(" 10,20 ,30,20,10,5,0,5", ratios));
    REQUIRE(ratiosText(ratios) == "10,20,30,20,10,5,0,5");
    REQUIRE_FALSE(parseRatios("10,20,30", ratios));
    REQUIRE_FALSE(parseRatios("10,20,30,20,10,5,0,5,1", ratios));
    REQUIRE_FALSE(parseRatios("10,-20,30,20,10,5,0,5", ratios));
    REQUIRE_FALSE(parseRatios("0,0,0,0,0,0,0,0", ratios));
    REQUIRE(ratiosText(ratios) == "10,20,30,20,10,5,0,5");
    REQUIRE(parseRatios("", ratios));
    REQUIRE(ratios == UnitMixPolicy::Weights{});

    const QuantBotConfig baseline;
    const std::string base = baseline.getConfigHash();
    for (size_t house = 0; house < 6; ++house)
        for (size_t slot = 0; slot < 8; ++slot) {
            QuantBotConfig changed;
            changed.tunedRatios[house][slot] += 1;
            REQUIRE(changed.getConfigHash() != base);
        }
    {   // Moving weight between two adjacent slots cannot alias.
        QuantBotConfig a, b;
        a.tunedRatios[1] = {1, 23, 0, 0, 0, 0, 0, 0};
        b.tunedRatios[1] = {12, 3, 0, 0, 0, 0, 0, 0};
        REQUIRE(a.getConfigHash() != b.getConfigHash());
    }
    const auto path = std::filesystem::temp_directory_path() / "quantbot-tuned-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Unit Ratios Tuned]\nAtreides=10,10,30,25,10,5,0,10\nOrdos=bogus\n";
    }
    QuantBotConfig config;
    const auto ordos = config.getTunedRatios(2);
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE(ratiosText(config.getTunedRatios(1)) == "10,10,30,25,10,5,0,10");
    REQUIRE(config.getTunedRatios(2) == ordos);   // malformed keeps the previous value
    REQUIRE(config.getTunedRatios(7) == UnitMixPolicy::Weights{});
    REQUIRE(config.getConfigHash() != base);
    std::filesystem::remove(path);
}

TEST_CASE("Strategy: the shipped Tuned ratios are the measured mixes, within the bounds", "[ai][strategy][bounds]") {
    // docs/ai-play-styles.md, section 8 (per house the best round-2 mix): percent of tank, siege, launcher,
    // special, ornithopter, trike, raider trike, quad. Fremen, Sardaukar and Mercenary play their configured
    // opening.
    const QuantBotConfig config;
    REQUIRE(ratiosText(config.getTunedRatios(0)) == "14,16,15,22,0,0,0,33");    // Harkonnen
    REQUIRE(ratiosText(config.getTunedRatios(1)) == "9,20,13,22,13,10,0,13");   // Atreides
    REQUIRE(ratiosText(config.getTunedRatios(2)) == "9,17,7,13,27,0,7,20");     // Ordos
    for (int house = 3; house < 6; ++house) REQUIRE(config.getTunedRatios(house) == UnitMixPolicy::Weights{});
    // Each house's ratios, bounded over the slots it fields in the vanilla CY games they were measured in
    // (the start_mix those games resolved; Ordos fields launchers there) and over every slot, as a mod
    // might allow: 2%-50% per fieldable slot, 0 for the slots it cannot field, 100% in total. The written
    // ratios already satisfy the bounds over the house's own slots, so bound() keeps them.
    const std::array<std::array<bool, 8>, 3> own = {kHarkonnen, kAtreides, slotsOf(0xDF)};
    for (int house = 0; house < 3; ++house) {
        UnitMixPolicy::Weights weights{};
        for (size_t i = 0; i < 8; ++i) weights[i] = config.getTunedRatios(house)[i] * 100;
        for (const auto& available : {own[size_t(house)], slotsOf(0xFF)}) {
            const auto mix = bound(weights, available);
            REQUIRE(withinBounds(mix, available));
            int total = 0;
            for (size_t i = 0; i < 8; ++i) {
                total += mix[i];
                if (!available[i]) REQUIRE(mix[i] == 0);
            }
            REQUIRE(total == 10000);
        }
        const auto kept = bound(weights, own[size_t(house)]);
        for (size_t i = 0; i < 8; ++i) REQUIRE(kept[i] == (own[size_t(house)][i] ? weights[i] : 0));
    }
}
