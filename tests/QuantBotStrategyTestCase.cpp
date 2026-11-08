#include <catch2/catch_test_macros.hpp>

#include <players/QuantBotBuildPolicy.h>

#include <players/QuantBotConfig.h>
#include <players/QuantBotStrategy.h>
#include <players/UnitMixPolicy.h>
#include <players/QuantBotSpendingPolicy.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    // Harkonnen: no ornithopters, trikes or raider trikes; the Devastator its one special.
    const UnitMixPolicy::Slots harkonnen = {true, true, true, true, false, false, false, false, false, true};
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

TEST_CASE("Strategic site guards distinguish multiplayer configurations", "[ai][config][strategic-sites]") {
    QuantBotConfig guarded, legacy;
    guarded.strategicSiteGuards = true;
    legacy.strategicSiteGuards = false;
    REQUIRE(guarded.getConfigHash() != legacy.getConfigHash());
}

TEST_CASE("Captured harvester return defaults on and round-trips both hashed states",
          "[ai][config][harvester][deviation]") {
    QuantBotConfig enabled, disabled;
    REQUIRE(enabled.deviatedHarvesterEarlyReturn);
    disabled.deviatedHarvesterEarlyReturn = false;
    REQUIRE(enabled.getConfigHash() != disabled.getConfigHash());
    const auto path = std::filesystem::temp_directory_path() / "quantbot-deviated-harvester-test.ini";
    QuantBotConfig restored;
    for (const auto* expected : {&disabled, &enabled}) {
        REQUIRE(expected->save(path.string()));
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.deviatedHarvesterEarlyReturn == expected->deviatedHarvesterEarlyReturn);
        REQUIRE(restored.getConfigHash() == expected->getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Civ Wars strategy defaults off, round-trips both states and is multiplayer hashed", "[ai][config][civ-wars]") {
    QuantBotConfig enabled, disabled;
    REQUIRE_FALSE(disabled.civWarsStrategy);
    enabled.civWarsStrategy = true;
    REQUIRE(enabled.getConfigHash() != disabled.getConfigHash());
    const auto path = std::filesystem::temp_directory_path() / "quantbot-civ-wars-test.ini";
    REQUIRE(enabled.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE(restored.civWarsStrategy);
    REQUIRE(restored.getConfigHash() == enabled.getConfigHash());
    REQUIRE(disabled.save(path.string()));
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.civWarsStrategy);
    REQUIRE(restored.getConfigHash() == disabled.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("Research Lab policy defaults on and is loaded saved and hashed",
          "[ai][research][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.researchLab);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-research-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nResearchLab=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.researchLab);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.researchLab);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("Required Research Lab progression defaults on and independently round-trips its hash",
          "[ai][research][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.researchLabProgression);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-research-progression-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nResearchLabProgression=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.researchLabProgression);
    REQUIRE(config.force.researchLab);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.researchLabProgression);
    REQUIRE(restored.force.researchLab);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("City growth power defaults on and is loaded saved and hashed", "[ai][city][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.cityGrowthPower);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-growth-power-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nCityGrowthPower=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.cityGrowthPower);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.cityGrowthPower);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("City power sizing defaults on and is loaded saved and hashed", "[ai][city][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.cityPowerSizing);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-power-sizing-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nCityPowerSizing=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.cityPowerSizing);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.cityPowerSizing);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("City opening capital defaults on and is loaded saved and hashed", "[ai][city][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.cityOpeningCapital);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-opening-capital-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nCityOpeningCapital=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.cityOpeningCapital);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.cityOpeningCapital);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("City construction capital defaults on and is loaded saved and hashed", "[ai][city][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.cityConstructionCapital);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-construction-capital-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nCityConstructionCapital=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.cityConstructionCapital);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.cityConstructionCapital);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("City production prerequisites default on and are loaded saved and hashed", "[ai][city][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.cityProductionPrerequisites);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-production-prerequisites-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nCityProductionPrerequisites=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.cityProductionPrerequisites);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.cityProductionPrerequisites);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("City first-factory saving is opt-in and independently loaded saved and hashed", "[ai][city][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.cityProductionPrerequisites);
    REQUIRE_FALSE(config.force.cityFirstFactorySaving);
    const auto disabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-first-factory-saving-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nCityFirstFactorySaving=true\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE(config.force.cityFirstFactorySaving);
    REQUIRE(config.force.cityProductionPrerequisites);
    REQUIRE(config.getConfigHash() != disabledHash);
    const auto enabledHash = config.getConfigHash();
    config.force.cityProductionPrerequisites = false;
    REQUIRE(config.getConfigHash() != enabledHash);
    config.force.cityProductionPrerequisites = true;
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE(restored.force.cityFirstFactorySaving);
    REQUIRE(restored.force.cityProductionPrerequisites);
    REQUIRE(restored.getConfigHash() == enabledHash);
    std::filesystem::remove(path);
}

TEST_CASE("City tuning candidates are opt-in and independently loaded saved and hashed", "[ai][city][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.cityThreatBudget);
    REQUIRE_FALSE(defaults.force.cityZoneDefence);
    REQUIRE_FALSE(defaults.force.cityAdvancedTech);
    REQUIRE_FALSE(defaults.force.cityContinuousResearch);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-tuning-test.ini";
    for (int mask = 0; mask < 16; ++mask) {
        const bool threat = (mask & 1) != 0;
        const bool zones = (mask & 2) != 0;
        const bool tech = (mask & 4) != 0;
        const bool research = (mask & 8) != 0;
        QuantBotConfig expected = defaults;
        expected.force.cityThreatBudget = threat;
        expected.force.cityZoneDefence = zones;
        expected.force.cityAdvancedTech = tech;
        expected.force.cityContinuousResearch = research;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCityThreatBudget=" << threat
                << "\nCityZoneDefence=" << zones << "\nCityAdvancedTech=" << tech
                << "\nCityContinuousResearch=" << research << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.cityThreatBudget == threat);
        REQUIRE(loaded.force.cityZoneDefence == zones);
        REQUIRE(loaded.force.cityAdvancedTech == tech);
        REQUIRE(loaded.force.cityContinuousResearch == research);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (mask == 0));
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.cityThreatBudget == threat);
        REQUIRE(restored.force.cityZoneDefence == zones);
        REQUIRE(restored.force.cityAdvancedTech == tech);
        REQUIRE(restored.force.cityContinuousResearch == research);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Storage capacity defaults on and is loaded saved and hashed", "[ai][city][config][storage]") {
    QuantBotConfig config;
    REQUIRE(config.force.storageCapacity);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-storage-capacity-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nStorageCapacity=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.storageCapacity);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.storageCapacity);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("Funded classic production defaults on and is loaded saved and hashed", "[ai][production][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.fundedClassicProduction);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-funded-classic-production-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nFundedClassicProduction=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.fundedClassicProduction);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.fundedClassicProduction);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("Simple tech savings defaults off and independently round-trips both hashed states",
          "[ai][production][technology][config][simple-tech-savings]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.simpleTechSavings);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-simple-tech-savings-test.ini";
    const auto savedPath = std::filesystem::temp_directory_path() / "quantbot-simple-tech-savings-saved-test.ini";
    const auto savedBytes = [&](const QuantBotConfig& config) {
        REQUIRE(config.save(savedPath.string()));
        std::ifstream ini(savedPath, std::ios::binary);
        REQUIRE(ini.good());
        return std::string(std::istreambuf_iterator<char>(ini), std::istreambuf_iterator<char>());
    };
    std::array<std::string, 8> independentHashes;
    size_t hashCount = 0;

    for (const bool trooperEnabled : {false, true}) {
        for (const bool fundedEnabled : {false, true}) {
            CAPTURE(trooperEnabled, fundedEnabled);
            QuantBotConfig baseline = defaults;
            baseline.force.trooperSupport = trooperEnabled;
            baseline.force.fundedClassicProduction = fundedEnabled;
            QuantBotConfig config = baseline;
            const auto disabledHash = baseline.getConfigHash();
            independentHashes[hashCount++] = disabledHash;
            QuantBotConfig enabledConfig = baseline;
            enabledConfig.force.simpleTechSavings = true;
            independentHashes[hashCount++] = enabledConfig.getConfigHash();

            for (const bool enabled : {false, true, false, true}) {
                QuantBotConfig expected = baseline;
                expected.force.simpleTechSavings = enabled;
                const auto expectedBytes = savedBytes(expected);
                {
                    std::ofstream ini(path);
                    ini << "[Force Concentration]\nSimpleTechSavings="
                        << (enabled ? "true" : "false") << '\n';
                }
                REQUIRE(config.applyOverrides(path.string()));
                REQUIRE(config.force.simpleTechSavings == enabled);
                REQUIRE(config.force.trooperSupport == trooperEnabled);
                REQUIRE(config.force.fundedClassicProduction == fundedEnabled);
                REQUIRE((config.getConfigHash() == disabledHash) == !enabled);
                REQUIRE(config.getConfigHash() == expected.getConfigHash());
                // A one-key override must preserve every other setting.
                REQUIRE(savedBytes(config) == expectedBytes);

                // Missing optional keys preserve either current state, not just OFF.
                {
                    std::ofstream ini(path);
                    ini << "[Force Concentration]\n";
                }
                REQUIRE(config.applyOverrides(path.string()));
                REQUIRE(config.force.simpleTechSavings == enabled);
                REQUIRE(config.getConfigHash() == expected.getConfigHash());
                REQUIRE(savedBytes(config) == expectedBytes);

                QuantBotConfig restored;
                restored.force.simpleTechSavings = !enabled;
                REQUIRE(restored.applyOverrides(savedPath.string()));
                REQUIRE(restored.force.simpleTechSavings == enabled);
                REQUIRE(restored.force.trooperSupport == trooperEnabled);
                REQUIRE(restored.force.fundedClassicProduction == fundedEnabled);
                REQUIRE(restored.getConfigHash() == expected.getConfigHash());
                REQUIRE(savedBytes(restored) == expectedBytes);
            }
        }
    }
    REQUIRE(hashCount == independentHashes.size());
    // The new switch cannot alias either causal-control flag.
    for (size_t first = 0; first < hashCount; ++first) {
        for (size_t second = first + 1; second < hashCount; ++second)
            REQUIRE(independentHashes[first] != independentHashes[second]);
    }
    std::filesystem::remove(path);
    std::filesystem::remove(savedPath);
}


TEST_CASE("Tech savings vehicle choices use current and queued value without future cheap quotas",
          "[ai][production][technology][composition]") {
    UnitMixPolicy::Mix target{};
    UnitMixPolicy::Weights current{}, prices{};
    UnitMixPolicy::Slots available{};
    target[0] = 8000;
    target[UnitMixPolicy::kQuadSlot] = 2000;
    prices[0] = 300;
    prices[UnitMixPolicy::kQuadSlot] = 200;
    available[0] = available[UnitMixPolicy::kQuadSlot] = true;
    current[0] = 300;
    REQUIRE(UnitMixPolicy::nextExistingSlot(target, current, prices, available) == UnitMixPolicy::kQuadSlot);
    // A queued Quad is a commitment, not a second empty army for another LF.
    current[UnitMixPolicy::kQuadSlot] += prices[UnitMixPolicy::kQuadSlot];
    REQUIRE(UnitMixPolicy::nextExistingSlot(target, current, prices, available) == 0);
    current[UnitMixPolicy::kQuadSlot] += 5 * prices[UnitMixPolicy::kQuadSlot];
    prices[0] = 800;
    const auto next = UnitMixPolicy::nextExistingSlot(target, current, prices, available);
    REQUIRE(next == 0);
    REQUIRE(prices[next] > 799);
    REQUIRE(prices[UnitMixPolicy::kQuadSlot] <= 799);
    // Cash cannot change this selection into another overweight cheap unit.
    // Acceptance waits for the selected legal type's real purchase budget.
    available[0] = false;
    REQUIRE(UnitMixPolicy::nextExistingSlot(target, current, prices, available) == UnitMixPolicy::kSlots);
    available.fill(false);
    REQUIRE(UnitMixPolicy::nextExistingSlot(target, current, prices, available) == UnitMixPolicy::kSlots);
    // A missing supplier is not permission to refill its overweight alternative.
    current.fill(0);
    available[UnitMixPolicy::kQuadSlot] = true;
    REQUIRE(UnitMixPolicy::nextExistingSlot(target, current, prices, available) == UnitMixPolicy::kQuadSlot);
    current[UnitMixPolicy::kQuadSlot] = prices[UnitMixPolicy::kQuadSlot];
    REQUIRE(UnitMixPolicy::nextExistingSlot(target, current, prices, available) == UnitMixPolicy::kSlots);
    // Do not deadlock growth at an exact ratio: permit one indivisible order.
    available[0] = true;
    current[0] = 800;
    REQUIRE(UnitMixPolicy::nextExistingSlot(target, current, prices, available) == 0);
}

TEST_CASE("Trooper support provisionally defaults on and is independently loaded saved and hashed", "[ai][production][troopers][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.trooperSupport);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-trooper-support-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nTrooperSupport=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.force.trooperSupport);
    REQUIRE(config.getConfigHash() != enabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.force.trooperSupport);
    REQUIRE(restored.getConfigHash() == config.getConfigHash());
    REQUIRE(restored.force.moreRocketTurrets == QuantBotConfig{}.force.moreRocketTurrets);
    std::filesystem::remove(path);
}

TEST_CASE("Launcher extra kite distance is independently loaded saved bounded and hashed", "[ai][launcher][config]") {
    QuantBotConfig config;
    REQUIRE(config.force.launcherKiteExtraDistance == 2);
    const auto enabledHash = config.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-launcher-kite-test.ini";
    for (const int input : {0, 1, 2, -1, 3}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nLauncherKiteExtraDistance=" << input << '\n';
        }
        REQUIRE(config.applyOverrides(path.string()));
        REQUIRE(config.force.launcherKiteExtraDistance == std::clamp(input, 0, 2));
        REQUIRE((config.getConfigHash() == enabledHash) == (input >= 2));
        REQUIRE(config.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.launcherKiteExtraDistance == config.force.launcherKiteExtraDistance);
        REQUIRE(restored.getConfigHash() == config.getConfigHash());
        REQUIRE(restored.force.trooperSupport == QuantBotConfig{}.force.trooperSupport);
    }
    std::filesystem::remove(path);
}

TEST_CASE("Exhausted army dispatch defaults on and independently round-trips both hashed states",
          "[ai][army][recovery][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.exhaustedArmyDispatch);
    REQUIRE(defaults.force.launcherKiteExtraDistance == 2);
    REQUIRE(defaults.force.trooperSupport);
    REQUIRE(defaults.deviatedHarvesterEarlyReturn);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-exhausted-army-test.ini";
    const auto savedPath = std::filesystem::temp_directory_path() / "quantbot-exhausted-army-saved-test.ini";
    const auto savedBytes = [&](const QuantBotConfig& config) {
        REQUIRE(config.save(savedPath.string()));
        std::ifstream ini(savedPath, std::ios::binary);
        REQUIRE(ini.good());
        return std::string(std::istreambuf_iterator<char>(ini), std::istreambuf_iterator<char>());
    };

    for (const bool customized : {false, true}) {
        QuantBotConfig baseline = defaults;
        if (customized) {
            baseline.force.waveReadinessPercent = 83;
            baseline.force.launcherKiteExtraDistance = 1;
            baseline.force.trooperSupport = false;
            baseline.deviatedHarvesterEarlyReturn = false;
        }
        QuantBotConfig config = baseline;
        const auto enabledHash = baseline.getConfigHash();
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\n";
        }
        REQUIRE(config.applyOverrides(path.string()));
        REQUIRE(config.force.exhaustedArmyDispatch);
        REQUIRE(config.getConfigHash() == enabledHash);

        for (const bool enabled : {false, true, false}) {
            QuantBotConfig expected = baseline;
            expected.force.exhaustedArmyDispatch = enabled;
            const auto expectedBytes = savedBytes(expected);
            {
                std::ofstream ini(path);
                ini << "[Force Concentration]\nExhaustedArmyDispatch="
                    << (enabled ? "true" : "false") << '\n';
            }
            REQUIRE(config.applyOverrides(path.string()));
            REQUIRE(config.force.exhaustedArmyDispatch == enabled);
            REQUIRE((config.getConfigHash() == enabledHash) == enabled);
            REQUIRE(config.getConfigHash() == expected.getConfigHash());
            // A one-key override must preserve every other serialized option byte.
            REQUIRE(savedBytes(config) == expectedBytes);

            QuantBotConfig restored;
            restored.force.exhaustedArmyDispatch = !enabled;
            REQUIRE(restored.applyOverrides(savedPath.string()));
            REQUIRE(restored.force.exhaustedArmyDispatch == enabled);
            REQUIRE(restored.getConfigHash() == expected.getConfigHash());
            REQUIRE(savedBytes(restored) == expectedBytes);
        }
    }
    std::filesystem::remove(path);
    std::filesystem::remove(savedPath);
}

TEST_CASE("Vanilla FFA switches default off and independently persist every hashed combination",
          "[ai][force][ffa][config]") {
    const QuantBotConfig defaults;
    const std::array<bool QuantBotConfig::ForceSettings::*, 4> members{
        &QuantBotConfig::ForceSettings::vanillaFfaWeakestTarget,
        &QuantBotConfig::ForceSettings::vanillaFfaNearbyTarget,
        &QuantBotConfig::ForceSettings::vanillaFfaReserve,
        &QuantBotConfig::ForceSettings::vanillaFfaFullWaves};
    const std::array<const char*, 4> names{
        "VanillaFfaWeakestTarget", "VanillaFfaNearbyTarget", "VanillaFfaReserve", "VanillaFfaFullWaves"};
    for (const auto member : members) REQUIRE_FALSE(defaults.force.*member);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-vanilla-ffa-config-test.ini";
    std::map<std::string, unsigned> hashes;
    for (unsigned mask = 0; mask < 16; ++mask) {
        QuantBotConfig expected = defaults;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\n";
            for (size_t i = 0; i < members.size(); ++i) {
                expected.force.*members[i] = (mask & (1u << i)) != 0;
                ini << names[i] << '=' << (expected.force.*members[i]) << '\n';
            }
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.emplace(loaded.getConfigHash(), mask).second);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        for (const auto member : members) REQUIRE((restored.force.*member) == (expected.force.*member));
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Campaign opening switches default off and independently round-trip their hashes",
          "[ai][campaign][opening][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.campaignMilitaryOpening);
    REQUIRE_FALSE(defaults.force.campaignAdaptiveReadiness);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-opening-test.ini";
    const auto savedPath = std::filesystem::temp_directory_path() / "quantbot-campaign-opening-saved-test.ini";
    for (const bool production : {false, true}) {
        for (const bool readiness : {false, true}) {
            QuantBotConfig expected = defaults;
            expected.force.campaignMilitaryOpening = production;
            expected.force.campaignAdaptiveReadiness = readiness;
            {
                std::ofstream ini(path);
                ini << "[Force Concentration]\nCampaignMilitaryOpening=" << production
                    << "\nCampaignAdaptiveReadiness=" << readiness << '\n';
            }
            QuantBotConfig loaded;
            REQUIRE(loaded.applyOverrides(path.string()));
            REQUIRE(loaded.force.campaignMilitaryOpening == production);
            REQUIRE(loaded.force.campaignAdaptiveReadiness == readiness);
            REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
            REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (!production && !readiness));
            REQUIRE(loaded.force.campaignPlayerHouse == defaults.force.campaignPlayerHouse);
            REQUIRE(loaded.save(savedPath.string()));
            QuantBotConfig restored;
            REQUIRE(restored.applyOverrides(savedPath.string()));
            REQUIRE(restored.force.campaignMilitaryOpening == production);
            REQUIRE(restored.force.campaignAdaptiveReadiness == readiness);
            REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
        }
    }
    std::filesystem::remove(path);
    std::filesystem::remove(savedPath);
}

TEST_CASE("Campaign threat production defaults off and independently round-trips both hashed states",
          "[ai][campaign][production][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.campaignThreatProduction);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-threat-test.ini";
    for (const bool enabled : {false, true, false}) {
        QuantBotConfig expected = defaults;
        expected.force.campaignThreatProduction = enabled;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCampaignThreatProduction=" << enabled << '\n';
        }
        QuantBotConfig config;
        REQUIRE(config.applyOverrides(path.string()));
        REQUIRE(config.force.campaignThreatProduction == enabled);
        REQUIRE(config.getConfigHash() == expected.getConfigHash());
        REQUIRE((config.getConfigHash() == defaults.getConfigHash()) == !enabled);
        REQUIRE_FALSE(config.force.campaignMilitaryOpening);
        REQUIRE_FALSE(config.force.campaignAdaptiveReadiness);
        REQUIRE(config.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.campaignThreatProduction == enabled);
        REQUIRE(restored.getConfigHash() == config.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Campaign player house defaults off and independently round-trips both hashed states",
          "[ai][campaign][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.campaignPlayerHouse);
    const auto disabledHash = defaults.getConfigHash();
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-player-house-test.ini";
    for (const bool enabled : {false, true, false}) {
        QuantBotConfig config = defaults;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCampaignPlayerHouse="
                << (enabled ? "true" : "false") << '\n';
        }
        REQUIRE(config.applyOverrides(path.string()));
        REQUIRE(config.force.campaignPlayerHouse == enabled);
        REQUIRE((config.getConfigHash() != disabledHash) == enabled);
        REQUIRE(config.force.campaignCombatBudget == defaults.force.campaignCombatBudget);
        REQUIRE(config.force.openingCombatBudget == defaults.force.openingCombatBudget);
        REQUIRE(config.save(path.string()));
        QuantBotConfig restored;
        restored.force.campaignPlayerHouse = !enabled;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.campaignPlayerHouse == enabled);
        REQUIRE(restored.getConfigHash() == config.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("More rocket turrets defaults off and is independent loaded saved and hashed", "[ai][defence][config]") {
    QuantBotConfig config;
    REQUIRE_FALSE(config.force.moreRocketTurrets);
    const auto disabledHash=config.getConfigHash();
    const auto path=std::filesystem::temp_directory_path()/"quantbot-more-rocket-turrets-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nMoreRocketTurrets=true\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE(config.force.moreRocketTurrets);
    REQUIRE(config.force.threatTurrets);
    REQUIRE(config.getConfigHash()!=disabledHash);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE(restored.force.moreRocketTurrets);
    REQUIRE(restored.force.threatTurrets);
    REQUIRE(restored.getConfigHash()==config.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("Ground threat and city supplier switches default on and are loaded saved and hashed", "[ai][defence][config]") {
    const QuantBotConfig baseline;
    REQUIRE(baseline.force.threatTurrets);
    REQUIRE(baseline.force.cityStarportSupply);
    for (const bool turret : {false,true}) {
        QuantBotConfig config;
        const auto path=std::filesystem::temp_directory_path()/"quantbot-ground-defence-test.ini";
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\n" << (turret ? "ThreatTurrets" : "CityStarportSupply") << "=false\n";
        }
        REQUIRE(config.applyOverrides(path.string()));
        REQUIRE(config.force.threatTurrets==!turret);
        REQUIRE(config.force.cityStarportSupply==turret);
        REQUIRE(config.getConfigHash()!=baseline.getConfigHash());
        REQUIRE(config.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.getConfigHash()==config.getConfigHash());
        std::filesystem::remove(path);
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

TEST_CASE("Police funding recovery is default-on, configurable and hashed", "[ai][city][budget][config]") {
    QuantBotConfig config;
    REQUIRE(config.policeFundingRecovery);
    const auto baseline=config.getConfigHash();
    const auto path=std::filesystem::temp_directory_path()/"quantbot-police-funding-test.ini";
    {
        std::ofstream ini(path);
        ini << "[General Behavior]\nPoliceFundingRecovery=false\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE_FALSE(config.policeFundingRecovery);
    REQUIRE(config.getConfigHash()!=baseline);
    REQUIRE(config.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE_FALSE(restored.policeFundingRecovery);
    REQUIRE(restored.getConfigHash()==config.getConfigHash());
    {
        std::ofstream ini(path);
        ini << "[General Behavior]\nPoliceFundingRecovery=true\n";
    }
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE(config.policeFundingRecovery);
    REQUIRE(config.getConfigHash()==baseline);
    std::filesystem::remove(path);
}

TEST_CASE("Strategy: Learned and Base are the 1.0.846 rules", "[ai][strategy]") {
    const MixProfile& learned = profile(Mix::Learned);
    REQUIRE(learned.priorBps == 0);
    REQUIRE(learned.infantryPercent == 0);
    REQUIRE_FALSE(learned.counter);
    const UnitMixPolicy::Weights opening = {3000, 2000, 2000, 1500, 0, 0, 1000, 0, 0, 500};
    UnitMixPolicy::Slots available{};
    available.fill(true);
    REQUIRE(applyPrior(opening, UnitMixPolicy::expandGroups(learned.prior, available), learned.priorBps, available) == opening);
    const TempoProfile& base = profile(Tempo::Base);
    REQUIRE(base.readinessPercent == 0);
    REQUIRE((base.minWaveUnits == 6 && base.minWaveValue == 3000));
    REQUIRE((!base.lightFirst && base.openingReadiness && base.minutes == 0));
    // Swarm is the measured swarm style (WaveReadinessPercent=35, waves from 4 units / 2,000).
    const TempoProfile& swarm = profile(Tempo::Swarm);
    REQUIRE((swarm.readinessPercent == 35 && swarm.minWaveUnits == 4 && swarm.minWaveValue == 2000));
}

TEST_CASE("Strategy: a mix is the prior the learning starts from, not a frozen mix", "[ai][strategy]") {
    UnitMixPolicy::Slots available{};
    available.fill(true);
    const auto quadMass = UnitMixPolicy::expandGroups(profile(Mix::QuadMass).prior, available);
    const UnitMixPolicy::Weights opening = {5000, 2000, 2000, 1000, 0, 0, 0, 0, 0, 0};
    const auto quads = applyPrior(opening, quadMass, profile(Mix::QuadMass).priorBps, available);
    REQUIRE(UnitMixPolicy::normalize(quads)[9] == 5000);
    REQUIRE(UnitMixPolicy::normalize(quads)[0] == 2667);
    // Unavailable slots are dropped from the prior; nothing available keeps the opening.
    UnitMixPolicy::Slots noQuads = available;
    noQuads[9] = false;
    const auto masked = UnitMixPolicy::normalize(applyPrior(opening, quadMass, 10000, noQuads));
    REQUIRE(masked[9] == 0);
    REQUIRE(masked[0] == 5334);
    UnitMixPolicy::Slots none{};
    REQUIRE(applyPrior(opening, quadMass, 10000, none) == opening);

    // Tanks performed, quads did not: with no evidence the prior stands; as the lost value grows
    // against the fielded army the allocation follows the performance (vanilla evidence blend).
    UnitMixPolicy::Weights scores{};
    scores[0] = 4000000;
    scores[9] = 500000;
    const auto start = UnitMixPolicy::allocate(scores, quads, false, true, 0, 10000);
    REQUIRE(start[9] == 5000);
    const auto early = UnitMixPolicy::allocate(scores, quads, true, true, 1000, 10000);
    const auto late = UnitMixPolicy::allocate(scores, quads, true, true, 30000, 10000);
    REQUIRE(early[9] < 5000);
    REQUIRE(late[9] < early[9]);
    REQUIRE(late[0] > early[0]);
    REQUIRE(late[0] > 5000);

    // Counter: nothing seen, no prior; armour seen, launchers and siege lead.
    REQUIRE(counterPrior(0, 0, 0, 0) == UnitMixPolicy::GroupWeights{});
    const auto counter = UnitMixPolicy::normalize(UnitMixPolicy::expandGroups(counterPrior(0, 10000, 0, 0), available));
    REQUIRE(counter[2] > counter[0]);
    REQUIRE(counter[1] > counter[9]);
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

UnitMixPolicy::Slots slotsOf(unsigned mask) {
    UnitMixPolicy::Slots slots{};
    for (size_t i = 0; i < slots.size(); ++i) slots[i] = (mask >> i) & 1u;
    return slots;
}

constexpr unsigned kEverySlot = (1u << UnitMixPolicy::kSlots) - 1;

// Mix slots: tank, siege, launcher, Devastator, Sonic Tank, Deviator, ornithopter, trike, raider, quad
// (vanilla ObjectData at tech 7+, tests/ai/tech-bands.py).
const UnitMixPolicy::Slots kAtreides = {true, true, true, false, true, false, true, true, false, true};
const UnitMixPolicy::Slots kHarkonnen = {true, true, true, true, false, false, false, false, false, true};
const UnitMixPolicy::Slots kOrdos = {true, true, true, false, false, true, true, false, true, true};
const UnitMixPolicy::Slots kFremen = {true, true, true, true, true, false, true, true, true, true};

UnitMixPolicy::Weights expanded(const UnitMixPolicy::GroupWeights& group, const UnitMixPolicy::Slots& slots) {
    return UnitMixPolicy::expandGroups(group, slots);
}

uint64_t lcg(uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state >> 33;
}

} // namespace

TEST_CASE("Strategy: bound() keeps every producible slot within 2%-50%", "[ai][strategy][bounds]") {
    // Every availability set and a spread of weights, zeros included.
    uint64_t state = 20261018;
    for (unsigned mask = 0; mask <= kEverySlot; ++mask) {
        const auto available = slotsOf(mask);
        int count = 0;
        for (bool slot : available) count += slot;
        for (int trial = 0; trial < 24; ++trial) {
            UnitMixPolicy::Weights weights{};
            for (auto& weight : weights) weight = lcg(state) % 4 == 0 ? 0 : int64_t(lcg(state) % 100000);
            const auto mix = bound(weights, available);
            REQUIRE(withinBounds(mix, available));
            int total = 0;
            for (size_t i = 0; i < mix.size(); ++i) {
                total += mix[i];
                if (!available[i]) REQUIRE(mix[i] == 0);   // excluded types stay out
                else if (count > 1) REQUIRE((mix[i] >= kStartMinBps && mix[i] <= kStartMaxBps));
            }
            REQUIRE(total == (count ? 10000 : 0));
            if (count == 1) REQUIRE(*std::max_element(mix.begin(), mix.end()) == 10000);
            // Idempotent: a bounded mix is its own bound.
            UnitMixPolicy::Weights again{};
            for (size_t i = 0; i < mix.size(); ++i) again[i] = mix[i];
            REQUIRE(bound(again, available) == mix);
            // The order of the weights within the bounds is kept (a heavier slot never gets less).
            for (size_t i = 0; i < mix.size(); ++i)
                for (size_t j = 0; j < mix.size(); ++j)
                    if (available[i] && available[j] && weights[i] > weights[j]) REQUIRE(mix[i] + 1 >= mix[j]);
        }
    }
    // Clamp and renormalise: the slots inside the bounds keep their proportions.
    const auto mix = bound({7000, 2000, 1000, 0, 0, 0, 0, 0, 0, 0}, slotsOf(0x0F));
    REQUIRE(mix == UnitMixPolicy::Mix{5000, 3200, 1600, 200, 0, 0, 0, 0, 0, 0});
    // Two slots: 50/50 whatever the weights.
    REQUIRE(bound({9000, 1000}, slotsOf(0x03)) == UnitMixPolicy::Mix{5000, 5000, 0, 0, 0, 0, 0, 0, 0, 0});
    // One weighted slot among zeros: it takes the cap, the zeros share the rest.
    REQUIRE(bound({0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, slotsOf(0x281)) == UnitMixPolicy::Mix{2500, 0, 0, 0, 0, 0, 0, 2500, 0, 5000});
    REQUIRE(bound({}, slotsOf(0x00)) == UnitMixPolicy::Mix{});
}

TEST_CASE("Strategy: every starting mix is within the bounds", "[ai][strategy][bounds]") {
    // The presets, over every house's slots: the bounds hold and no single type passes 50%.
    for (const auto& available : {kAtreides, kHarkonnen, kOrdos, kFremen, slotsOf(kEverySlot), slotsOf(0x380), slotsOf(0x7F)}) {
        for (size_t m = 0; m < kMixCount; ++m) {
            const MixProfile& mix = profile(static_cast<Mix>(m));
            if (mix.priorBps <= 0) continue;
            const auto prior = applyPrior({}, expanded(mix.prior, available), mix.priorBps, available);
            REQUIRE(withinBounds(bound(prior, available), available));
        }
    }
    // Each preset as written (the group form, one special share) is within the bounds over the eight
    // groups (Counter has no fixed prior).
    for (size_t m = 0; m < kMixCount; ++m) {
        const MixProfile& mix = profile(static_cast<Mix>(m));
        if (mix.priorBps <= 0 || mix.counter) continue;
        int64_t total = 0;
        for (const auto share : mix.prior) {
            REQUIRE((share >= kStartMinBps && share <= kStartMaxBps));
            total += share;
        }
        REQUIRE(total == 10000);
    }
    // The presets are the 1.0.847 presets clamped and renormalised: QuadMass was 70% quads, now 50%
    // (compared over every slot with one special, i.e. the eight slots of 1.0.847).
    const auto every = slotsOf(kEverySlot);
    const auto eight = slotsOf(kEverySlot & ~0x30u);
    auto same = [&](const UnitMixPolicy::GroupWeights& old, Mix preset) {
        return bound(expanded(old, eight), eight) == bound(expanded(profile(preset).prior, eight), eight);
    };
    REQUIRE(same({2000, 0, 0, 0, 0, 0, 1000, 7000}, Mix::QuadMass));
    REQUIRE(same({1000, 0, 0, 0, 0, 4500, 4500, 0}, Mix::TrikeSwarm));
    REQUIRE(same({7000, 1500, 1500, 0, 0, 0, 0, 0}, Mix::TankMass));
    REQUIRE(same({2000, 4500, 3500, 0, 0, 0, 0, 0}, Mix::Artillery));
    REQUIRE(same({2500, 1500, 1000, 5000, 0, 0, 0, 0}, Mix::Specials));
    REQUIRE(same({1500, 1000, 1500, 0, 6000, 0, 0, 0}, Mix::Air));
    REQUIRE(profile(Mix::QuadMass).prior[7] == kStartMaxBps);
    REQUIRE(bound(expanded(profile(Mix::QuadMass).prior, kHarkonnen), kHarkonnen)[9] == kStartMaxBps);
    // A house with one special plays a preset's special share on it, as before 9868.
    REQUIRE(expanded(profile(Mix::Specials).prior, kHarkonnen)[3] == 4600);
    REQUIRE(expanded(profile(Mix::Specials).prior, kAtreides)[4] == 4600);
    // Stefan's configured ratios (QuantBot Config.ini [Unit Ratios]) through the opening, tech 8:
    // Atreides Tank 0.05 stays (5% x 0.96 of the heavy share is above 2%), a 0 becomes 2%.
    const QuantBotConfig config;
    auto configured = [&](int house, const UnitMixPolicy::Slots& available) {
        const auto& ratios = config.getRatios(house);
        return expanded({int64_t(ratios.tank * 10000), int64_t(ratios.siegeTank * 10000), int64_t(ratios.launcher * 10000),
                         int64_t(ratios.special * 10000), int64_t(ratios.ornithopter * 10000), 0, 0, 0}, available);
    };
    auto opening = [&](int house, const UnitMixPolicy::Slots& available) {
        const auto open = UnitMixPolicy::openingMix(8, configured(house, available), available);
        UnitMixPolicy::Weights weights{};
        for (size_t i = 0; i < open.size(); ++i) weights[i] = open[i];
        return weights;
    };
    for (int house = 0; house < 6; ++house)
        for (const auto& available : {kAtreides, kHarkonnen, kOrdos, kFremen, slotsOf(kEverySlot)})
            REQUIRE(withinBounds(bound(opening(house, available), available), available));
    const auto bounded = bound(opening(1, kAtreides), kAtreides);
    REQUIRE(bounded[0] > 400);                            // the 5% tank share stays about 5%
    REQUIRE(bounded[7] == kStartMinBps);                  // trikes (1.3% of the opening) rise to 2%
    REQUIRE(bounded[8] == 0);                             // Atreides has no raider trikes
    // Ornithopter 0.00 with ornithopters producible (all slots): 2%.
    REQUIRE(bound(opening(0, every), every)[UnitMixPolicy::kOrnithopterSlot] == kStartMinBps);
    // A house with two specials splits its configured special share between them (Mercenary 5%).
    const auto mercenary = configured(5, kFremen);
    REQUIRE(mercenary[3] == mercenary[4]);
    REQUIRE(mercenary[3] + mercenary[4] == int64_t(config.getRatios(5).special * 10000));
    REQUIRE(mercenary[5] == 0);
}

TEST_CASE("Strategy: the Bounded draw is a pure, bounded function of seed and house", "[ai][strategy][bounds]") {
    constexpr int kSeeds = 20000;
    for (const auto& available : {kAtreides, kHarkonnen, kOrdos, kFremen, slotsOf(kEverySlot), slotsOf(0x03), slotsOf(0x01)}) {
        int count = 0;
        for (bool slot : available) count += slot;
        std::array<int64_t, UnitMixPolicy::kSlots> sum{};
        std::array<int, UnitMixPolicy::kSlots> lowest{}, highest{};
        lowest.fill(10000);
        int differ = 0;
        for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
            const auto mix = randomMix(seed, 1, available);
            REQUIRE(withinBounds(mix, available));
            REQUIRE(randomMix(seed, 1, available) == mix);
            differ += randomMix(seed, 2, available) != mix;
            for (size_t i = 0; i < mix.size(); ++i) {
                sum[i] += mix[i];
                if (available[i]) { lowest[i] = std::min(lowest[i], mix[i]); highest[i] = std::max(highest[i], mix[i]); }
            }
        }
        if (count < 3) continue;   // one slot: 100%; two: 50/50
        REQUIRE(differ > kSeeds * 99 / 100);
        for (size_t i = 0; i < sum.size(); ++i) {
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
    REQUIRE(parseRatios(" 10,20 ,30,10,10,0,10,5,0,5", ratios));
    REQUIRE(ratiosText(ratios) == "10,20,30,10,10,0,10,5,0,5");
    REQUIRE_FALSE(parseRatios("10,20,30", ratios));
    REQUIRE_FALSE(parseRatios("10,20,30,20,10,5,0,5", ratios));             // the eight-slot layout
    REQUIRE_FALSE(parseRatios("10,20,30,10,10,0,10,5,0,5,1", ratios));
    REQUIRE_FALSE(parseRatios("10,-20,30,10,10,0,10,5,0,5", ratios));
    REQUIRE_FALSE(parseRatios("0,0,0,0,0,0,0,0,0,0", ratios));
    REQUIRE(ratiosText(ratios) == "10,20,30,10,10,0,10,5,0,5");
    REQUIRE(parseRatios("", ratios));
    REQUIRE(ratios == UnitMixPolicy::Weights{});

    const QuantBotConfig baseline;
    const std::string base = baseline.getConfigHash();
    for (size_t house = 0; house < 6; ++house)
        for (size_t band = 0; band < QuantBotConfig::kTunedBands; ++band)
            for (size_t slot = 0; slot < UnitMixPolicy::kSlots; ++slot) {
                QuantBotConfig changed;
                changed.tunedRatios[house][band][slot] += 1;
                REQUIRE(changed.getConfigHash() != base);
            }
    {   // Moving weight between two adjacent slots cannot alias.
        QuantBotConfig a, b;
        a.tunedRatios[1][0] = {1, 23, 0, 0, 0, 0, 0, 0, 0, 0};
        b.tunedRatios[1][0] = {12, 3, 0, 0, 0, 0, 0, 0, 0, 0};
        REQUIRE(a.getConfigHash() != b.getConfigHash());
    }
    const auto path = std::filesystem::temp_directory_path() / "quantbot-tuned-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Unit Ratios Tuned]\nAtreides=10,10,30,0,25,0,10,5,0,10\nAtreides_Tech5=40,0,30,0,0,0,0,20,0,10\n"
               "Ordos=bogus\nOrdos_Tech4=1,2,3\nHarkonnen_Tech6= opening \n";
    }
    QuantBotConfig config;
    const auto ordos = config.getTunedRatios(2);
    REQUIRE(config.applyOverrides(path.string()));
    REQUIRE(ratiosText(config.getTunedRatios(1)) == "10,10,30,0,25,0,10,5,0,10");
    // A band's own mix; a band without one (Atreides 2: trikes only) plays the house's top mix.
    REQUIRE(ratiosText(config.getTunedRatios(1, 5)) == "40,0,30,0,0,0,0,20,0,10");
    REQUIRE(ratiosText(config.getTunedRatios(1, 2)) == "10,10,30,0,25,0,10,5,0,10");
    REQUIRE(ratiosText(config.getTunedRatios(1, 7)) == "10,10,30,0,25,0,10,5,0,10");
    REQUIRE(config.getTunedRatios(2) == ordos);   // malformed keeps the previous value
    REQUIRE(config.getTunedRatios(2, 4) == QuantBotConfig().getTunedRatios(2, 4));
    REQUIRE(config.getTunedRatios(7) == UnitMixPolicy::Weights{});
    // Opening: that band plays the configured opening (no tuned mix), the others the top mix.
    REQUIRE(config.tunedOpening[0][6]);
    REQUIRE(config.getTunedRatios(0, 6) == UnitMixPolicy::Weights{});
    REQUIRE(ratiosText(config.getTunedRatios(0, 3)) == "14,16,15,22,0,0,0,0,0,33");
    REQUIRE(config.getConfigHash() != base);
    QuantBotConfig opening;
    opening.tunedOpening[0][6] = true;
    REQUIRE(opening.getConfigHash() != base);
    std::filesystem::remove(path);
}

TEST_CASE("Strategy: the shipped Tuned ratios are the measured mixes, within the bounds", "[ai][strategy][bounds]") {
    // docs/ai-play-styles.md, section 8 (per house the best round-2 mix): percent of tank, siege, launcher,
    // Devastator, Sonic Tank, Deviator, ornithopter, trike, raider trike, quad; each single-special house
    // keeps its former special share on its special.
    const QuantBotConfig config;
    REQUIRE(ratiosText(config.getTunedRatios(0)) == "14,16,15,22,0,0,0,0,0,33");    // Harkonnen
    REQUIRE(ratiosText(config.getTunedRatios(1)) == "9,20,13,0,22,0,13,10,0,13");   // Atreides
    REQUIRE(ratiosText(config.getTunedRatios(2)) == "9,17,7,0,0,13,27,0,7,20");     // Ordos
    REQUIRE(ratiosText(config.getTunedRatios(3)) == "11,10,13,5,8,0,18,11,13,11");  // Fremen (section 8.10)
    REQUIRE(config.getTunedRatios(4) == UnitMixPolicy::Weights{});                  // Sardaukar: the opening
    REQUIRE(config.getTunedRatios(5) == UnitMixPolicy::Weights{});                  // Mercenary: the opening
    // Every shipped mix of every house and band, bounded over the slots the house fields in the vanilla
    // CY games they were measured in and over every slot, as a mod might allow: 2%-50% per fieldable
    // slot, 0 for the slots it cannot field, 100% in total. The top mixes already satisfy the bounds
    // over the house's own slots, so bound() keeps them.
    const std::array<UnitMixPolicy::Slots, 6> own = {kHarkonnen, kAtreides, kOrdos, kFremen, kFremen, kFremen};
    for (int house = 0; house < 6; ++house) {
        for (size_t band = 0; band < QuantBotConfig::kTunedBands; ++band) {
            const auto& ratios = config.tunedRatios[size_t(house)][band];
            if (*std::max_element(ratios.begin(), ratios.end()) == 0) continue;
            UnitMixPolicy::Weights weights{};
            for (size_t i = 0; i < weights.size(); ++i) weights[i] = ratios[i] * 100;
            for (const auto& available : {own[size_t(house)], slotsOf(kEverySlot)}) {
                const auto mix = bound(weights, available);
                REQUIRE(withinBounds(mix, available));
            }
            if (band != 0) continue;
            const auto kept = bound(weights, own[size_t(house)]);
            for (size_t i = 0; i < kept.size(); ++i) REQUIRE(kept[i] == (own[size_t(house)][i] ? weights[i] : 0));
        }
    }
}

TEST_CASE("Strategy: the shipped tech-band ratios are valid for their band", "[ai][strategy][bounds][tech]") {
    // Vanilla ObjectData (tests/ai/tech-bands.py): the slots each house fields in each band, as a mask of
    // tank 0x1, siege 0x2, launcher 0x4, Devastator 0x8, Sonic Tank 0x10, Deviator 0x20, ornithopter 0x40,
    // trike 0x80, raider trike 0x100, quad 0x200; 0 = no band starts at that level.
    // Harkonnen, Atreides, Ordos, Fremen, Sardaukar, Mercenary; levels 1-6 (7-9 is the top mix).
    constexpr unsigned fremen[7] = {0, 0, 0x180, 0x380, 0x381, 0x385, 0x387};
    constexpr unsigned bands[6][7] = {
        {0, 0, 0, 0x200, 0x201, 0x205, 0x207},
        {0, 0, 0x080, 0x280, 0x281, 0x285, 0x287},
        {0, 0, 0x100, 0x300, 0x301, 0x305, 0},
        {fremen[0], fremen[1], fremen[2], fremen[3], fremen[4], fremen[5], fremen[6]},
        {0, 0, 0, 0x380, 0x381, 0x385, 0x387},
        {fremen[0], fremen[1], fremen[2], fremen[3], fremen[4], fremen[5], fremen[6]}};
    const QuantBotConfig config;
    int designed = 0;
    for (int house = 0; house < 6; ++house) {
        for (int band = 1; band < 7; ++band) {
            const auto& ratios = config.tunedRatios[size_t(house)][size_t(band)];
            const unsigned mask = bands[house][band];
            const auto fieldable = slotsOf(mask);
            const int count = int(std::count(fieldable.begin(), fieldable.end(), true));
            CAPTURE(house, band, ratiosText(ratios));
            REQUIRE_FALSE(config.tunedOpening[size_t(house)][size_t(band)]);
            // Every band with two or more fieldable slots has its own mix; none elsewhere.
            if (count < 2) {
                REQUIRE(*std::max_element(ratios.begin(), ratios.end()) == 0);
                continue;
            }
            ++designed;
            int total = 0;
            for (size_t i = 0; i < ratios.size(); ++i) {
                total += ratios[i];
                if (fieldable[i]) REQUIRE((ratios[i] >= 2 && ratios[i] <= 50));   // buildable: 2%-50%
                else REQUIRE(ratios[i] == 0);                                    // only buildable slots
            }
            REQUIRE(total == 100);
            REQUIRE(config.getTunedRatios(house, band) == ratios);
            // Already within the bounds over the band's slots: bound() plays it unchanged.
            UnitMixPolicy::Weights weights{};
            for (size_t i = 0; i < weights.size(); ++i) weights[i] = ratios[i] * 100;
            const auto played = bound(weights, fieldable);
            REQUIRE(withinBounds(played, fieldable));
            for (size_t i = 0; i < played.size(); ++i) REQUIRE(played[i] == weights[i]);
        }
        // The top band is the house's measured mix (none for Fremen, Sardaukar, Mercenary: section 8.10).
        REQUIRE(config.getTunedRatios(house, 7) == config.tunedRatios[size_t(house)][0]);
    }
    REQUIRE(designed == 24);
    // By design: tanks lead at 4 (50%), tanks and launchers at 5, siege joins at 6.
    for (int house = 0; house < 6; ++house) {
        if (house != 0) REQUIRE(config.getTunedRatios(house, 4)[0] == 50);
        if (house != 2) REQUIRE(config.getTunedRatios(house, 6)[1] == 25);
    }
}

TEST_CASE("Strategy: a house that builds several specials splits the special share", "[ai][strategy][specials]") {
    // Devastator, Sonic Tank, Deviator. One special: the whole share, as before.
    REQUIRE(UnitMixPolicy::splitShare(2200, std::array<bool, 3>{true, false, false}) == std::array<int, 3>{2200, 0, 0});
    REQUIRE(UnitMixPolicy::splitShare(2200, std::array<bool, 3>{false, true, false}) == std::array<int, 3>{0, 2200, 0});
    // Two or three: the share is divided (remainder to the lowest buildable), never given to each.
    REQUIRE(UnitMixPolicy::splitShare(2200, std::array<bool, 3>{true, true, false}) == std::array<int, 3>{1100, 1100, 0});
    REQUIRE(UnitMixPolicy::splitShare(2201, std::array<bool, 3>{true, false, true}) == std::array<int, 3>{1101, 0, 1100});
    const auto three = UnitMixPolicy::splitShare(1000, std::array<bool, 3>{true, true, true});
    REQUIRE(three == std::array<int, 3>{334, 333, 333});
    REQUIRE(three[0] + three[1] + three[2] == 1000);
    REQUIRE(UnitMixPolicy::splitShare(1000, std::array<bool, 3>{}) == std::array<int, 3>{});
    REQUIRE(UnitMixPolicy::splitShare(0, std::array<bool, 3>{true, true, true}) == std::array<int, 3>{});
}

TEST_CASE("Strategy: each special has its own slot, share and learning", "[ai][strategy][specials]") {
    // The group form (one special share) expands over the specials a house can build.
    const UnitMixPolicy::GroupWeights group = {1000, 1000, 1000, 3000, 1000, 1000, 1000, 1000};
    const auto one = UnitMixPolicy::expandGroups(group, kHarkonnen);
    REQUIRE((one[3] == 3000 && one[4] == 0 && one[5] == 0));
    REQUIRE(one[6] == 1000);                                     // the ornithopter keeps its group value
    const auto two = UnitMixPolicy::expandGroups(group, kFremen);
    REQUIRE((two[3] == 1500 && two[4] == 1500 && two[5] == 0));
    const auto three = UnitMixPolicy::expandGroups(group, slotsOf(kEverySlot));
    REQUIRE((three[3] == 1000 && three[4] == 1000 && three[5] == 1000));
    REQUIRE(UnitMixPolicy::groupSlots(kFremen) == std::array<bool, 8>{true, true, true, true, true, true, true, true});
    REQUIRE(UnitMixPolicy::groupSlots(kHarkonnen) == std::array<bool, 8>{true, true, true, true, false, false, false, true});
    // Bounds apply per buildable special: every one of a three-special house gets 2%-50%.
    const auto bounded = bound(three, slotsOf(kEverySlot));
    REQUIRE(withinBounds(bounded, slotsOf(kEverySlot)));
    for (size_t s = 3; s < 6; ++s) REQUIRE(bounded[s] >= kStartMinBps);
    // The learning weighs each special by its own results: the Devastator performed, the Sonic Tank
    // did not, so the Devastator's share rises above the Sonic Tank's (one share each, not one for all).
    UnitMixPolicy::Weights baseline{};
    for (size_t i = 0; i < baseline.size(); ++i) baseline[i] = kFremen[i] ? 1000 : 0;
    UnitMixPolicy::Weights scores{};
    scores[0] = 1000000;
    scores[3] = 4000000;
    scores[4] = 200000;
    const auto mix = UnitMixPolicy::allocate(scores, baseline, true, true, 30000, 10000);
    REQUIRE(mix[3] > mix[4]);
    REQUIRE(mix[5] == 0);
    int total = 0;
    for (const int share : mix) total += share;
    REQUIRE(total == 10000);
}
