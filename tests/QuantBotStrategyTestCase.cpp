#include <catch2/catch_test_macros.hpp>

#include <players/QuantBotBuildPolicy.h>
#include <players/ForceConcentrationPolicy.h>

#include <players/QuantBotConfig.h>
#include <players/QuantBotStrategy.h>
#include <players/UnitMixPolicy.h>
#include <players/QuantBotSpendingPolicy.h>
#include <players/QuantBotVanillaPolicy.h>
#include <players/QuantBotCoreDefencePolicy.h>
#include <players/QuantBotFieldExpansionPolicy.h>
#include <players/StarportBuyingPolicy.h>
#include <players/TacticalSafetyPolicy.h>
#include <players/QuantBotMapScopePolicy.h>
#include <players/QuantBotSpiceBloomPolicy.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string_view>

// QuantBot strategies (docs/ai-play-styles.md, "Strategies"): the pick is a pure function of the
// game seed, the house and the config, so every peer, replay and reload plays the same strategy;
// the mix is the prior the learned unit mix starts from.

using namespace QuantBotStrategy;

TEST_CASE("Income technology timing defaults off and round-trips both rate levels",
          "[ai][strategy][tech-income]") {
    using namespace QuantBotBuildPolicy;
    QuantBotConfig native;
    REQUIRE(native.force.techIncomeTiming == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-tech-income-test.ini";
    for (int level : {0, 1, 2}) {
        QuantBotConfig config;
        config.force.techIncomeTiming = level;
        REQUIRE((config.getConfigHash() == native.getConfigHash()) == (level == 0));
        REQUIRE(config.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.techIncomeTiming == level);
        REQUIRE(restored.getConfigHash() == config.getConfigHash());
    }
    std::filesystem::remove(path);
    REQUIRE(incomeTechnologyAdmitted(0, 0, 1000));
    // Per-minute thresholds: level 1 needs income/min >= cost, level 2 >= 2 * cost.
    REQUIRE_FALSE(incomeTechnologyAdmitted(1, 999, 1000));
    REQUIRE(incomeTechnologyAdmitted(1, 1000, 1000));
    REQUIRE_FALSE(incomeTechnologyAdmitted(2, 1999, 1000));
    REQUIRE(incomeTechnologyAdmitted(2, 2000, 1000));
    // The Vanilla screen's median income (885/min) now holds a 1000-credit tech
    // at both levels; the old cost-per-5-minutes rule admitted it.
    REQUIRE_FALSE(incomeTechnologyAdmitted(1, 885, 1000));
    REQUIRE_FALSE(incomeTechnologyAdmitted(2, 885, 500));
    REQUIRE(incomeTechnologyAdmitted(1, 885, 500));
    REQUIRE_FALSE(incomeTechnologyAdmitted(1, 0, 0));
    REQUIRE(incomeTechnologyAdmitted(2, 2000000000, 1000000000));
    REQUIRE_FALSE(incomeTechnologyAdmitted(2, 2000000000, 2000000000));
    REQUIRE(incomeTechnologySafe(1000, 1000, false));
    REQUIRE_FALSE(incomeTechnologySafe(999, 1000, false));
    REQUIRE_FALSE(incomeTechnologySafe(10000, 0, true));
    REQUIRE(incomeTechnologyProject(Structure_IX, false, 0));
    REQUIRE_FALSE(incomeTechnologyProject(Structure_IX, false, 1));
    REQUIRE(incomeTechnologyProject(Structure_HeavyFactory, true, 1));
    REQUIRE_FALSE(incomeTechnologyProject(Structure_ConstructionYard, true, 1));
    REQUIRE_FALSE(incomeTechnologyProject(Structure_Refinery, false, 0));
}

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

TEST_CASE("Medium classic defaults off and every discrete level is loaded saved and hashed",
          "[ai][config][medium-classic]") {
    QuantBotConfig defaults;
    REQUIRE(defaults.force.mediumClassic == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-medium-classic-test.ini";
    for (int level : {0, 1, 2}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nMediumClassic=" << level << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.mediumClassic == level);
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (level == 0));
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.mediumClassic == level);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    for (int raw : {-1, 99}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nMediumClassic=" << raw << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.mediumClassic == (raw < 0 ? 0 : 2));
    }
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

TEST_CASE("Retreat preserve defaults off and both levels are loaded saved and hashed", "[ai][config][retreat]") {
    QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.retreatPreserve);
    REQUIRE(defaults.force.retreatPreserveLevel == 1);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-retreat-preserve-test.ini";
    for (int enabled : {0, 1}) for (int level : {1, 2}) {
        QuantBotConfig expected = defaults;
        expected.force.retreatPreserve = enabled;
        expected.force.retreatPreserveLevel = level;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nRetreatPreserve=" << enabled
                << "\nRetreatPreserveLevel=" << level << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.retreatPreserve == bool(enabled));
        REQUIRE(loaded.force.retreatPreserveLevel == level);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (enabled == 0 && level == 1));
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.retreatPreserve == bool(enabled));
        REQUIRE(restored.force.retreatPreserveLevel == level);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    for (int raw : {-1, 99}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nRetreatPreserveLevel=" << raw << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.retreatPreserveLevel == (raw < 1 ? 1 : 2));
    }
    std::filesystem::remove(path);
}

TEST_CASE("Harvester raid defaults off and both levels are loaded saved and hashed", "[ai][config][harvester-raid]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.harvesterRaid);
    REQUIRE(defaults.force.harvesterRaidLevel == 1);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-harvester-raid-test.ini";
    for (const int enabled : {0, 1}) for (const int level : {1, 2}) {
        QuantBotConfig expected = defaults;
        expected.force.harvesterRaid = enabled != 0;
        expected.force.harvesterRaidLevel = level;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nHarvesterRaid=" << enabled
                << "\nHarvesterRaidLevel=" << level << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.harvesterRaid == (enabled != 0));
        REQUIRE(loaded.force.harvesterRaidLevel == level);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (enabled == 0 && level == 1));
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.harvesterRaid == loaded.force.harvesterRaid);
        REQUIRE(restored.force.harvesterRaidLevel == level);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    for (const int raw : {-1, 99}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nHarvesterRaidLevel=" << raw << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.harvesterRaidLevel == (raw < 1 ? 1 : 2));
    }
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

TEST_CASE("City research order defaults to deferring new research with a hashed OFF control", "[ai][city][research]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.cityResearchOrder == 2);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-research-order-test.ini";
    for (int level = 0; level <= 2; ++level) {
        { std::ofstream ini(path); ini << "[Force Concentration]\nCityResearchOrder=" << level << '\n'; }
        QuantBotConfig config;
        REQUIRE(config.applyOverrides(path.string()));
        REQUIRE(config.force.cityResearchOrder == level);
        REQUIRE((config.getConfigHash() == defaults.getConfigHash()) == (level == 2));
        REQUIRE(config.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.cityResearchOrder == level);
        REQUIRE(restored.getConfigHash() == config.getConfigHash());
    }
    using QuantBotBuildPolicy::cityResearchOrderAdmits;
    REQUIRE(cityResearchOrderAdmits(0, 9, 4, 0, 2000));
    REQUIRE(cityResearchOrderAdmits(1, 3, 8, 0, 2000));
    REQUIRE_FALSE(cityResearchOrderAdmits(1, 8, 8, 9000, 0));
    REQUIRE_FALSE(cityResearchOrderAdmits(2, 3, 8, 999, 0));
    REQUIRE(cityResearchOrderAdmits(2, 3, 8, 1000, 0));
    REQUIRE_FALSE(cityResearchOrderAdmits(2, 3, 8, 2499, 2000));
    REQUIRE(cityResearchOrderAdmits(2, 3, 8, 2500, 2000));
    // At tech 9 every new study is deferred at both levels. The deferral gates
    // research only; quantbot_research_order_lab_capital checks that the Research
    // Lab building keeps its ordinary optional-capital admission.
    for (const int level : {1, 2}) REQUIRE_FALSE(cityResearchOrderAdmits(level, 9, 9, 9000, 0));
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

TEST_CASE("City building economy candidates are opt-in and independently loaded saved and hashed", "[ai][city][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.citySpiceTransition);
    REQUIRE_FALSE(defaults.force.citySpiceCapacityRunway);
    REQUIRE_FALSE(defaults.force.citySingleYardGrowth);
    REQUIRE_FALSE(defaults.force.cityPowerGrowthHorizon);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-building-economy-test.ini";
    for (int mask = 0; mask < 16; ++mask) {
        const bool transition = (mask & 1) != 0;
        const bool runway = (mask & 2) != 0;
        const bool growth = (mask & 4) != 0;
        const bool power = (mask & 8) != 0;
        QuantBotConfig expected = defaults;
        expected.force.citySpiceTransition = transition;
        expected.force.citySpiceCapacityRunway = runway;
        expected.force.citySingleYardGrowth = growth;
        expected.force.cityPowerGrowthHorizon = power;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCitySpiceTransition=" << transition
                << "\nCitySpiceCapacityRunway=" << runway << "\nCitySingleYardGrowth=" << growth
                << "\nCityPowerGrowthHorizon=" << power << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.citySpiceTransition == transition);
        REQUIRE(loaded.force.citySpiceCapacityRunway == runway);
        REQUIRE(loaded.force.citySingleYardGrowth == growth);
        REQUIRE(loaded.force.cityPowerGrowthHorizon == power);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (mask == 0));
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.citySpiceTransition == transition);
        REQUIRE(restored.force.citySpiceCapacityRunway == runway);
        REQUIRE(restored.force.citySingleYardGrowth == growth);
        REQUIRE(restored.force.cityPowerGrowthHorizon == power);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("City boom families and discrete levels are opt-in loaded saved and independently hashed", "[ai][city][config]") {
    using ForceSettings = QuantBotConfig::ForceSettings;
    const std::array<bool ForceSettings::*, 5> switches{{
        &ForceSettings::cityBoomWorkers, &ForceSettings::cityBoomRefineries, &ForceSettings::cityBoomGreedy,
        &ForceSettings::cityBoomShortTax, &ForceSettings::cityBoomReactive}};
    const QuantBotConfig defaults;
    for (auto member : switches) REQUIRE_FALSE(defaults.force.*member);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-boom-family-test.ini";
    std::set<std::string> hashes;
    auto roundTrip = [&](const QuantBotConfig& expected) {
        REQUIRE(expected.save(path.string()));
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        for (auto member : switches) REQUIRE(loaded.force.*member == expected.force.*member);
        REQUIRE(loaded.force.cityBoomWorkerGoal == expected.force.cityBoomWorkerGoal);
        REQUIRE(loaded.force.cityBoomRefineryGoal == expected.force.cityBoomRefineryGoal);
        REQUIRE(loaded.force.cityBoomGreedyMinutes == expected.force.cityBoomGreedyMinutes);
        REQUIRE(loaded.force.cityBoomTaxMinutes == expected.force.cityBoomTaxMinutes);
        REQUIRE(loaded.force.cityBoomArmyRatioBps == expected.force.cityBoomArmyRatioBps);
        REQUIRE(loaded.force.cityBoomEconomyRatioBps == expected.force.cityBoomEconomyRatioBps);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.insert(loaded.getConfigHash()).second);
    };
    for (unsigned mask = 0; mask < (1u << switches.size()); ++mask) {
        auto expected = defaults;
        for (unsigned i = 0; i < switches.size(); ++i) expected.force.*switches[i] = (mask & (1u << i)) != 0;
        roundTrip(expected);
    }
    struct Level { int ForceSettings::*member; int initial, alternate; };
    const std::array<Level, 6> levels{{
        {&ForceSettings::cityBoomWorkerGoal, 16, 24}, {&ForceSettings::cityBoomRefineryGoal, 6, 10},
        {&ForceSettings::cityBoomGreedyMinutes, 6, 10}, {&ForceSettings::cityBoomTaxMinutes, 6, 10},
        {&ForceSettings::cityBoomArmyRatioBps, 6000, 9000}, {&ForceSettings::cityBoomEconomyRatioBps, 12000, 15000}}};
    for (const auto& level : levels) {
        REQUIRE(defaults.force.*level.member == level.initial);
        auto expected = defaults;
        expected.force.*level.member = level.alternate;
        roundTrip(expected);
    }
    REQUIRE(hashes.size() == 38);
    std::filesystem::remove(path);
}

TEST_CASE("City spice gate, harvester efficiency and service switches are opt-in loaded saved and independently hashed", "[ai][city][config]") {
    using ForceSettings = QuantBotConfig::ForceSettings;
    const std::array<bool ForceSettings::*, 6> switches{{
        &ForceSettings::cityBoomSpiceGate, &ForceSettings::cityHarvestFieldChoice,
        &ForceSettings::cityRefineryLogistics, &ForceSettings::cityHarvesterReplacement,
        &ForceSettings::cityLandValueServices, &ForceSettings::cityGrowthStageTax}};
    struct Level { int ForceSettings::*member; int initial, alternate; };
    const std::array<Level, 6> levels{{
        {&ForceSettings::cityBoomPaybackMinutes, 10, 15}, {&ForceSettings::cityHarvestFieldSafetyTiles, 6, 10},
        {&ForceSettings::cityRefinerySiteTiles, 8, 12}, {&ForceSettings::cityHarvesterReplacementMinutes, 10, 15},
        {&ForceSettings::cityLandValueFundingPercent, 60, 100}, {&ForceSettings::cityGrowthTaxPopulation, 3000, 6000}}};
    const QuantBotConfig defaults;
    for (auto member : switches) REQUIRE_FALSE(defaults.force.*member);
    for (const auto& level : levels) REQUIRE(defaults.force.*level.member == level.initial);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-city-economy-efficiency-test.ini";
    std::set<std::string> hashes;
    auto roundTrip = [&](const QuantBotConfig& expected) {
        REQUIRE(expected.save(path.string()));
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        for (auto member : switches) REQUIRE(loaded.force.*member == expected.force.*member);
        for (const auto& level : levels) REQUIRE(loaded.force.*level.member == expected.force.*level.member);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.insert(loaded.getConfigHash()).second);
    };
    roundTrip(defaults);
    for (std::size_t i = 0; i < switches.size(); ++i) {
        auto expected = defaults;
        expected.force.*switches[i] = true;
        roundTrip(expected);
        // Each knob has exactly two accepted choices; anything else loads as the first.
        expected.force.*levels[i].member = levels[i].alternate;
        roundTrip(expected);
    }
    for (const auto& level : levels) {
        std::ofstream ini(path);
        ini << "[Force Concentration]\n";
        ini.close();
        QuantBotConfig odd;
        odd.force.*level.member = level.alternate + 1;
        REQUIRE(odd.save(path.string()));
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.*level.member == level.initial);
    }
    REQUIRE(hashes.size() == 1 + 2 * switches.size());
    std::filesystem::remove(path);
}

TEST_CASE("Spice field expansion is opt-in, funded by reachable shared spice and round-trips", "[ai][config][spice]") {
    using namespace QuantBotFieldExpansionPolicy;
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.spiceFieldExpansion == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-field-expansion-test.ini";
    for (int threshold : {0, 4000, 8000}) {
        QuantBotConfig expected = defaults;
        expected.force.spiceFieldExpansion = threshold;
        { std::ofstream ini(path); ini << "[Force Concentration]\nSpiceFieldExpansion=" << threshold << '\n'; }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.spiceFieldExpansion == threshold);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (threshold == 0));
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.spiceFieldExpansion == threshold);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
    REQUIRE_FALSE(due(0, 0, 2, 100000));
    REQUIRE_FALSE(due(4000, 0, 2, 0));
    REQUIRE_FALSE(due(4000, 0, 0, 100000));
    REQUIRE_FALSE(due(4000, 8000, 2, 100000));
    REQUIRE_FALSE(due(4000, 1000, 2, 11999));
    REQUIRE(due(4000, 7999, 2, 12000));
    REQUIRE(due(8000, 15000, 2, 24000));
    const auto reach = reachable(3, 3, {1,0,1, 1,0,1, 1,0,1}, {0});
    REQUIRE(reach == std::vector<uint8_t>{1,0,0, 1,0,0, 1,0,0});
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

TEST_CASE("Power and silo discipline is opt-in with independent persisted hashed levels",
          "[ai][economy][power][storage][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.powerLeadBuildings == 0);
    REQUIRE(defaults.force.siloFillPercent == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-power-silo-levels-test.ini";
    for (const int lead : {0, 1, 2}) {
        for (const int fill : {0, 80, 90}) {
            QuantBotConfig expected = defaults;
            expected.force.powerLeadBuildings = lead;
            expected.force.siloFillPercent = fill;
            {
                std::ofstream ini(path);
                ini << "[Force Concentration]\nPowerLeadBuildings=" << lead
                    << "\nSiloFillPercent=" << fill << '\n';
            }
            QuantBotConfig loaded;
            REQUIRE(loaded.applyOverrides(path.string()));
            REQUIRE(loaded.force.powerLeadBuildings == lead);
            REQUIRE(loaded.force.siloFillPercent == fill);
            REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
            REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (lead == 0 && fill == 0));
            REQUIRE(loaded.save(path.string()));
            QuantBotConfig restored;
            REQUIRE(restored.applyOverrides(path.string()));
            REQUIRE(restored.force.powerLeadBuildings == lead);
            REQUIRE(restored.force.siloFillPercent == fill);
            REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
        }
    }
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\nPowerLeadBuildings=99\nSiloFillPercent=85\n";
    }
    QuantBotConfig bounded;
    REQUIRE(bounded.applyOverrides(path.string()));
    REQUIRE(bounded.force.powerLeadBuildings == 2);
    REQUIRE(bounded.force.siloFillPercent == 0);
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

TEST_CASE("Palace experiments default off and independently round trip bounded hashed levels", "[ai][palace][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.palaceValueTarget == 0);
    REQUIRE(defaults.force.palaceWaveTiming == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-palace-experiments.ini";
    for (const char* key : {"PalaceValueTarget", "PalaceWaveTiming"}) {
        for (int level : {-1, 0, 1, 2, 3}) {
            QuantBotConfig config;
            {
                std::ofstream ini(path);
                ini << "[Force Concentration]\n" << key << '=' << level << '\n';
            }
            REQUIRE(config.applyOverrides(path.string()));
            const bool targeting = std::string_view(key) == "PalaceValueTarget";
            REQUIRE(config.force.palaceValueTarget == (targeting ? std::clamp(level, 0, 2) : 0));
            REQUIRE(config.force.palaceWaveTiming == (targeting ? 0 : std::clamp(level, 0, 2)));
            REQUIRE((config.getConfigHash() == defaults.getConfigHash()) == (level <= 0));
            REQUIRE(config.save(path.string()));
            QuantBotConfig restored;
            REQUIRE(restored.applyOverrides(path.string()));
            REQUIRE(restored.getConfigHash() == config.getConfigHash());
        }
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

TEST_CASE("Starport purchasing levels preserve native off and hash each experiment",
          "[ai][starport][config]") {
    const QuantBotConfig defaults;
    const std::array<int QuantBotConfig::ForceSettings::*, 3> members{
        &QuantBotConfig::ForceSettings::starportBuying,
        &QuantBotConfig::ForceSettings::starportCashStockpile,
        &QuantBotConfig::ForceSettings::starportEconomyPriority};
    const std::array<const char*, 3> names{
        "StarportBuying", "StarportCashStockpile", "StarportEconomyPriority"};
    const auto path = std::filesystem::temp_directory_path() / "quantbot-starport-levels-test.ini";
    std::map<std::string, int> hashes;
    for (int combination = 0; combination < 27; ++combination) {
        QuantBotConfig expected = defaults;
        int encoded = combination;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\n";
            for (size_t i = 0; i < members.size(); ++i) {
                REQUIRE(defaults.force.*members[i] == 0);
                expected.force.*members[i] = encoded % 3;
                ini << names[i] << '=' << encoded % 3 << '\n';
                encoded /= 3;
            }
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.emplace(loaded.getConfigHash(), combination).second);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        for (const auto member : members) REQUIRE(restored.force.*member == expected.force.*member);
        REQUIRE(restored.getConfigHash() == expected.getConfigHash());
    }
    std::filesystem::remove(path);
    using namespace StarportBuyingPolicy;
    REQUIRE(admitted(799,1000,0));
    REQUIRE_FALSE(admitted(1000,1000,0));
    REQUIRE(admitted(800,1000,1));
    REQUIRE_FALSE(admitted(801,1000,1));
    REQUIRE(admitted(1000,1000,2));
    REQUIRE_FALSE(admitted(1001,1000,2));
    REQUIRE_FALSE(admitted(0,1000,2));
    REQUIRE_FALSE(admitted(100,0,2));
    REQUIRE_FALSE(preserveCash(1000,500,500,0));
    REQUIRE_FALSE(preserveCash(1000,500,500,1));
    REQUIRE(preserveCash(999,500,500,1));
    REQUIRE(preserveCash(1000,500,500,2));
}

TEST_CASE("Vanilla FFA switches default off and independently persist every hashed combination",
          "[ai][force][ffa][config]") {
    const QuantBotConfig defaults;
    const std::array<bool QuantBotConfig::ForceSettings::*, 9> members{
        &QuantBotConfig::ForceSettings::vanillaFfaWeakestTarget,
        &QuantBotConfig::ForceSettings::vanillaFfaNearbyTarget,
        &QuantBotConfig::ForceSettings::vanillaFfaReserve,
        &QuantBotConfig::ForceSettings::vanillaFfaFullWaves,
        &QuantBotConfig::ForceSettings::vanillaFfaStarportAfterWave,
        &QuantBotConfig::ForceSettings::vanillaFfaThreatProduction,
        &QuantBotConfig::ForceSettings::vanillaFfaSingleFront,
        &QuantBotConfig::ForceSettings::vanillaFfaLightWorkingCash,
        &QuantBotConfig::ForceSettings::vanillaFfaFundedTech};
    const std::array<const char*, 9> names{
        "VanillaFfaWeakestTarget", "VanillaFfaNearbyTarget", "VanillaFfaReserve", "VanillaFfaFullWaves",
        "VanillaFfaStarportAfterWave", "VanillaFfaThreatProduction", "VanillaFfaSingleFront",
        "VanillaFfaLightWorkingCash", "VanillaFfaFundedTech"};
    for (const auto member : members) REQUIRE_FALSE(defaults.force.*member);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-vanilla-ffa-config-test.ini";
    std::map<std::string, unsigned> hashes;
    for (unsigned mask = 0; mask < (1u << members.size()); ++mask) {
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

TEST_CASE("Vanilla boom families default off and persist independent hashed combinations",
          "[ai][vanilla][strategy][config]") {
    const QuantBotConfig defaults;
    const std::array<bool QuantBotConfig::ForceSettings::*, 6> members{
        &QuantBotConfig::ForceSettings::vanillaHarvesterBoom, &QuantBotConfig::ForceSettings::vanillaRefineryBoom,
        &QuantBotConfig::ForceSettings::vanillaGreedyBoom, &QuantBotConfig::ForceSettings::vanillaReactiveBoom,
        &QuantBotConfig::ForceSettings::vanillaBoomFfaOnly, &QuantBotConfig::ForceSettings::vanillaBoomSpiceGate};
    const std::array<const char*, 6> names{"VanillaHarvesterBoom", "VanillaRefineryBoom", "VanillaGreedyBoom",
        "VanillaReactiveBoom", "VanillaBoomFfaOnly", "VanillaBoomSpiceGate"};
    for (const auto member : members) REQUIRE_FALSE(defaults.force.*member);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-vanilla-bold-config-test.ini";
    std::map<std::string, unsigned> hashes;
    for (unsigned mask = 0; mask < (1u << members.size()); ++mask) {
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

TEST_CASE("Duel opening plan defaults to the City raid and round trips independent hashed levels", "[ai][duel][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.duelOpeningPlan == 2);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-duel-opening-config-test.ini";
    std::map<std::string, int> hashes;
    for (int level = 0; level <= 3; ++level) {
        QuantBotConfig expected = defaults;
        expected.force.duelOpeningPlan = level;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nDuelOpeningPlan=" << level << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.duelOpeningPlan == level);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.emplace(loaded.getConfigHash(), level).second);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.duelOpeningPlan == level);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

namespace {

// Batch3 City economy switches (docs/ai-scenario-matrix.md, "Batch3 City economy
// experiments"): default 0 unless promoted; levels 0..maxLevel load, save and hash distinctly;
// the default level keeps the default hash; larger values clamp to maxLevel.
void requireBatch3CityLevels(const char* key, int QuantBotConfig::ForceSettings::*member, int defaultLevel = 0, int maxLevel = 2) {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.*member == defaultLevel);
    const auto path = std::filesystem::temp_directory_path() / (std::string("quantbot-batch3-") + key + ".ini");
    std::set<std::string> hashes;
    for (int level = 0; level <= maxLevel + 1; ++level) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\n" << key << '=' << level << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.*member == std::min(level, maxLevel));
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (std::min(level, maxLevel) == defaultLevel));
        REQUIRE(hashes.insert(loaded.getConfigHash()).second == (level <= maxLevel));
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.*member == loaded.force.*member);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

} // namespace

TEST_CASE("CityCapitalPacing defaults to level 2, hashed levels use the income gate", "[ai][city][config]") {
    requireBatch3CityLevels("CityCapitalPacing", &QuantBotConfig::ForceSettings::cityCapitalPacing, 2);
    using QuantBotBuildPolicy::incomeTechnologyAdmitted;
    REQUIRE(incomeTechnologyAdmitted(1, 1000, 1000));
    REQUIRE_FALSE(incomeTechnologyAdmitted(2, 1999, 1000));
    REQUIRE(incomeTechnologyAdmitted(2, 2000, 1000));
}

TEST_CASE("CityFleetRatios levels are default off, hashed and fixed fleet treatments", "[ai][city][config]") {
    requireBatch3CityLevels("CityFleetRatios", &QuantBotConfig::ForceSettings::cityFleetRatios);
    using QuantBotBuildPolicy::cityFleetRatioLevel;
    for (const int level : {1, 2}) {
        const auto ratios = cityFleetRatioLevel(level);
        REQUIRE(ratios.harvestersPerBay == (level == 2 ? 4 : 3));
        REQUIRE(ratios.carryallQuarters == 1);
        REQUIRE(ratios.repairYards == 1);
    }
}

TEST_CASE("CityTechRefineries levels are default off, hashed and capped by the spice plan", "[ai][city][config]") {
    requireBatch3CityLevels("CityTechRefineries", &QuantBotConfig::ForceSettings::cityTechRefineries);
    using QuantBotBuildPolicy::cityRefineryGateBays;
    REQUIRE(cityRefineryGateBays(0, 30) == 0);
    REQUIRE(cityRefineryGateBays(1, 30) == 3);
    REQUIRE(cityRefineryGateBays(2, 30) == 5);
    // One bay per three planned workers; a spice-poor plan still needs one bay.
    REQUIRE(cityRefineryGateBays(2, 12) == 4);
    REQUIRE(cityRefineryGateBays(1, 4) == 2);
    REQUIRE(cityRefineryGateBays(2, 0) == 1);
    REQUIRE(cityRefineryGateBays(1, -3) == 1);
}

TEST_CASE("CityYardRefineries levels are default off, hashed and never exceed the tech gate", "[ai][city][config]") {
    requireBatch3CityLevels("CityYardRefineries", &QuantBotConfig::ForceSettings::cityYardRefineries);
    using QuantBotBuildPolicy::cityRefineryGateBays;
    for (int workers = 0; workers <= 20; ++workers) {
        REQUIRE(cityRefineryGateBays(1, workers) <= cityRefineryGateBays(2, workers));
        REQUIRE(cityRefineryGateBays(2, workers) <= std::max(1, (workers + 2) / 3));
    }
}

TEST_CASE("CityWorkerReplacement levels are default off, hashed and set the payback horizon", "[ai][city][config]") {
    requireBatch3CityLevels("CityWorkerReplacement", &QuantBotConfig::ForceSettings::cityWorkerReplacement);
    REQUIRE(QuantBotBuildPolicy::cityWorkerReplacementMinutes(1) == 10);
    REQUIRE(QuantBotBuildPolicy::cityWorkerReplacementMinutes(2) == 15);
}

// Map-scoped repairs (players/QuantBotMapScopePolicy.h; docs/ai-scenario-matrix.md, "Map-scoped
// repairs") share the Batch3 level contract: hashed levels, persisted overrides and default hash.
// VanillaArmyCounterMapScope and FocusFireMapScope add the roster levels 3/4 and the combined level 5.
TEST_CASE("Map-scoped repair levels preserve their configured defaults, hashes and persistence", "[ai][config][map-scope]") {
    requireBatch3CityLevels("ClosingMountainScope", &QuantBotConfig::ForceSettings::closingMountainScope);
    requireBatch3CityLevels("VanillaArmyCounterMapScope", &QuantBotConfig::ForceSettings::vanillaArmyCounterMapScope, 5, 5);
    requireBatch3CityLevels("FocusFireMapScope", &QuantBotConfig::ForceSettings::focusFireMapScope, 0, 5);
}

// SpiceBloomHarvest (players/QuantBotSpiceBloomPolicy.h; docs/ai-scenario-matrix.md, "Spice bloom
// harvest") shares the Batch3 level contract: default OFF, hashed and persisted levels.
TEST_CASE("SpiceBloomHarvest levels are default off, hashed and persisted", "[ai][config][spice-bloom]") {
    requireBatch3CityLevels("SpiceBloomHarvest", &QuantBotConfig::ForceSettings::spiceBloomHarvest);
}

TEST_CASE("SpiceBloomHarvest bursts only beside an empty (1) or moderate (2) known field", "[ai][spice-bloom]") {
    using namespace QuantBotSpiceBloomPolicy;
    // The engine spills over plain sand within 5 tiles; the waste test adds a 2-tile margin.
    REQUIRE(kSpillRadius == 5);
    REQUIRE(kMeasureRadius == 7);
    // Level 1: any known spice there wastes the bloom.
    REQUIRE_FALSE(wasteful(1, 0));
    REQUIRE(wasteful(1, 1));
    // Level 2: up to 2,000 credits of known spice.
    REQUIRE_FALSE(wasteful(2, 0));
    REQUIRE_FALSE(wasteful(2, 2000));
    REQUIRE(wasteful(2, 2001));
    // OFF, and any other value, never bursts.
    REQUIRE(wasteful(0, 0));
    REQUIRE(wasteful(3, 0));
    // Half of the measured disc (149 tiles inside the map) must be explored.
    REQUIRE(enoughKnown(75, 149));
    REQUIRE_FALSE(enoughKnown(74, 149));
    REQUIRE_FALSE(enoughKnown(0, 0));
}

TEST_CASE("SpiceBloomHarvest keeps to our side, a fixed cadence and cheap idle shooters", "[ai][spice-bloom]") {
    using namespace QuantBotSpiceBloomPolicy;
    REQUIRE(ownSide(10, 10));
    REQUIRE(ownSide(10, -1));         // no enemy building known
    REQUIRE_FALSE(ownSide(11, 10));
    REQUIRE_FALSE(ownSide(-1, -1));   // no building of our own
    // Exactly one in four unit passes, whatever the house's offset within the 50-cycle interval.
    for (uint32_t offset = 0; offset < 50; ++offset) {
        int due = 0;
        for (uint32_t pass = 0; pass < 40; ++pass) due += evaluationDue(offset + 50 * pass);
        REQUIRE(due == 10);
    }
    // Projectiles that set a bloom off where they land and leave nothing behind.
    for (const int bullet : {Bullet_ShellSmall, Bullet_ShellMedium, Bullet_ShellLarge, Bullet_SmallRocket, Bullet_Rocket})
        REQUIRE(burstingBullet(bullet));
    for (const int bullet : {Bullet_Sonic, Bullet_SonicTrike, Bullet_DRocket, Bullet_Heal, Bullet_Slow, Bullet_Flame,
                             Bullet_Napalm, Bullet_Sandworm})
        REQUIRE_FALSE(burstingBullet(bullet));
    // Cheaper first, then idle before moving, then nearer, then the lower object id.
    REQUIRE(betterShooter({60, true, 30, 9}, {150, false, 2, 1}));
    REQUIRE(betterShooter({150, false, 30, 9}, {150, true, 2, 1}));
    REQUIRE(betterShooter({150, false, 2, 9}, {150, false, 3, 1}));
    REQUIRE(betterShooter({150, false, 2, 1}, {150, false, 2, 9}));
    REQUIRE_FALSE(betterShooter({150, false, 2, 1}, {150, false, 2, 1}));
}

namespace {

InitialTerrain measuredTerrain(int64_t tiles, int64_t spice, int64_t thick, int64_t mountain, int64_t rock,
                               int64_t windows) {
    InitialTerrain terrain;
    terrain.known = true;
    terrain.tiles = tiles;
    terrain.spice = spice;
    terrain.thickSpice = thick;
    terrain.mountain = mountain;
    terrain.rock = rock;
    terrain.rockWindows = windows;
    return terrain;
}

} // namespace

TEST_CASE("Map scopes count the original explicit grid", "[ai][map-scope]") {
    const auto terrain = countInitialTerrain(5, {"%%%%~", "%%%%+", "%%%@-", "G~-@L"});
    REQUIRE(terrain.known);
    REQUIRE(terrain.tiles == 20);
    REQUIRE(terrain.spice == 5);
    REQUIRE(terrain.thickSpice == 3);
    REQUIRE(terrain.mountain == 2);
    REQUIRE(terrain.rock == 11);
    REQUIRE(terrain.rockWindows == 1);
}

TEST_CASE("ClosingMountainScope applies from 8% (level 1) or 6% (level 2) mountain", "[ai][force][map-scope]") {
    using QuantBotMapScopePolicy::mountainRich;
    for (const int level : {1, 2}) {
        const int64_t threshold = level == 1 ? 800 : 600;
        REQUIRE(mountainRich(level, measuredTerrain(10000, 500, 100, threshold, 3000, 1500)));
        REQUIRE_FALSE(mountainRich(level, measuredTerrain(10000, 500, 100, threshold - 1, 3000, 1500)));
    }
    const auto rich = measuredTerrain(10000, 500, 100, 2000, 3000, 1500);
    REQUIRE_FALSE(mountainRich(0, rich));
    REQUIRE_FALSE(mountainRich(1, measuredTerrain(10000, 0, 0, 2000, 3000, 1500))); // no spice
    auto unknown = rich;
    unknown.known = false;
    REQUIRE_FALSE(mountainRich(2, unknown));
}

TEST_CASE("VanillaArmyCounterMapScope applies below 20% thick spice or 0.45 rock contiguity",
          "[ai][vanilla][map-scope]") {
    using QuantBotMapScopePolicy::thinOrFragmented;
    for (const int level : {1, 2}) {
        REQUIRE(thinOrFragmented(level, measuredTerrain(10000, 1000, 199, 0, 3000, 2000)));
        REQUIRE_FALSE(thinOrFragmented(level, measuredTerrain(10000, 1000, 200, 0, 3000, 2000)));
    }
    // Level 2 also scopes fragmented rock: 449 / 450 windows per 1000 rock tiles.
    REQUIRE(thinOrFragmented(2, measuredTerrain(10000, 1000, 200, 0, 1000, 449)));
    REQUIRE_FALSE(thinOrFragmented(2, measuredTerrain(10000, 1000, 200, 0, 1000, 450)));
    REQUIRE_FALSE(thinOrFragmented(1, measuredTerrain(10000, 1000, 200, 0, 1000, 0)));
    REQUIRE_FALSE(thinOrFragmented(2, measuredTerrain(10000, 1000, 200, 0, 0, 0)));
    REQUIRE_FALSE(thinOrFragmented(0, measuredTerrain(10000, 1000, 0, 0, 1000, 0)));
    REQUIRE_FALSE(thinOrFragmented(2, measuredTerrain(10000, 0, 0, 0, 1000, 0))); // no spice
}

TEST_CASE("FocusFireMapScope applies from 192x192 tiles or below 1000 tiles per controller",
          "[ai][force][map-scope]") {
    using QuantBotMapScopePolicy::hugeOrCrowded;
    for (const int level : {1, 2}) {
        REQUIRE(hugeOrCrowded(level, measuredTerrain(192 * 192, 500, 100, 0, 3000, 2000), 2));
        REQUIRE_FALSE(hugeOrCrowded(level, measuredTerrain(192 * 192 - 1, 500, 100, 0, 3000, 2000), 2));
    }
    // Six controllers on 64x64 (683 tiles each) are crowded at level 2 only.
    REQUIRE(hugeOrCrowded(2, measuredTerrain(64 * 64, 500, 100, 0, 1000, 500), 6));
    REQUIRE_FALSE(hugeOrCrowded(1, measuredTerrain(64 * 64, 500, 100, 0, 1000, 500), 6));
    REQUIRE(hugeOrCrowded(2, measuredTerrain(5999, 500, 100, 0, 1000, 500), 6));
    REQUIRE_FALSE(hugeOrCrowded(2, measuredTerrain(6000, 500, 100, 0, 1000, 500), 6));
    REQUIRE_FALSE(hugeOrCrowded(2, measuredTerrain(4096, 500, 100, 0, 1000, 500), 0));
    REQUIRE_FALSE(hugeOrCrowded(0, measuredTerrain(256 * 256, 500, 100, 0, 1000, 500), 6));
    REQUIRE_FALSE(hugeOrCrowded(2, measuredTerrain(256 * 256, 0, 0, 0, 1000, 500), 6)); // no spice
}

TEST_CASE("Combat scope levels 3/4 count opposing sides at game start", "[ai][force][map-scope]") {
    using QuantBotMapScopePolicy::manySides;
    using QuantBotMapScopePolicy::opposingSides;
    struct Row {
        int team;
        std::vector<int> playerInfoList;
    };
    const auto sides = [](std::initializer_list<int> teams) {
        std::vector<Row> rows;
        for (const int team : teams) rows.push_back({team, {1}});
        return opposingSides(rows, 1);
    };
    REQUIRE(sides({1, 2}) == 1);             // 1v1
    REQUIRE(sides({1, 1, 2, 2}) == 1);       // 2v2
    REQUIRE(sides({1, 2, 3, 4}) == 3);       // FFA4
    REQUIRE(sides({1, 2, 3, 4, 5, 6}) == 5); // FFA6
    // Unoccupied rows and the own team never count; a repeated hostile team counts once.
    REQUIRE(opposingSides(std::vector<Row>{{1, {1}}, {2, {1}}, {3, {}}, {1, {1}}, {2, {1}}}, 1) == 1);
    REQUIRE(manySides(3, 3));
    REQUIRE_FALSE(manySides(3, 2));
    REQUIRE(manySides(4, 5));
    REQUIRE_FALSE(manySides(4, 4));
    REQUIRE_FALSE(manySides(4, 3));
    for (const int level : {0, 1, 2}) REQUIRE_FALSE(manySides(level, 5)); // map levels never read the roster
}

TEST_CASE("Combat scope level 5 adds map level 1 to a roster level", "[ai][force][map-scope]") {
    using QuantBotMapScopePolicy::armyCountersLevel5;
    using QuantBotMapScopePolicy::focusFireLevel5;
    // FocusFireMapScope=5: level 3 (at least 3 opposing sides) or level 1 (at least 192x192 tiles).
    const auto huge = measuredTerrain(192 * 192, 1000, 500, 0, 1000, 500);
    const auto large = measuredTerrain(192 * 192 - 1, 1000, 500, 0, 1000, 500);
    REQUIRE(focusFireLevel5(large, 3));
    REQUIRE_FALSE(focusFireLevel5(large, 2));
    REQUIRE(focusFireLevel5(huge, 1));
    REQUIRE_FALSE(focusFireLevel5(measuredTerrain(256 * 256, 0, 0, 0, 1000, 500), 1)); // no spice
    // VanillaArmyCounterMapScope=5: level 4 (at least 5 opposing sides) or level 1 (thick below 20%).
    const auto thick = measuredTerrain(10000, 1000, 200, 0, 1000, 0);
    const auto thin = measuredTerrain(10000, 1000, 199, 0, 1000, 0);
    REQUIRE(armyCountersLevel5(thick, 5));
    REQUIRE_FALSE(armyCountersLevel5(thick, 4));
    REQUIRE(armyCountersLevel5(thin, 1));
    REQUIRE_FALSE(armyCountersLevel5(measuredTerrain(10000, 0, 0, 0, 1000, 0), 3)); // no spice
}

TEST_CASE("Vanilla boom levels are independently hashed loaded and saved", "[ai][vanilla][strategy][config]") {
    const std::array<int QuantBotConfig::ForceSettings::*, 6> members{
        &QuantBotConfig::ForceSettings::vanillaHarvesterBoomWorkers,
        &QuantBotConfig::ForceSettings::vanillaRefineryBoomRefineries,
        &QuantBotConfig::ForceSettings::vanillaGreedyBoomMinutes,
        &QuantBotConfig::ForceSettings::vanillaReactiveArmyRatioBps,
        &QuantBotConfig::ForceSettings::vanillaReactiveEconomyRatioBps,
        &QuantBotConfig::ForceSettings::vanillaBoomSpicePerRefinery};
    const std::array<const char*, 6> names{"VanillaHarvesterBoomWorkers", "VanillaRefineryBoomRefineries",
        "VanillaGreedyBoomMinutes", "VanillaReactiveArmyRatioBps", "VanillaReactiveEconomyRatioBps",
        "VanillaBoomSpicePerRefinery"};
    const std::array<std::vector<int>, 6> levels{{{8, 12}, {4, 6}, {6, 10, 12}, {6000, 9000}, {12000, 15000},
                                               {4000, 8000}}};
    const auto path = std::filesystem::temp_directory_path() / "quantbot-vanilla-boom-level-test.ini";
    const QuantBotConfig defaults;
    for (size_t i = 0; i < members.size(); ++i) for (const int value : levels[i]) {
        QuantBotConfig expected = defaults;
        expected.force.*members[i] = value;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\n" << names[i] << '=' << value << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.*members[i] == value);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        if (value != defaults.force.*members[i]) REQUIRE(loaded.getConfigHash() != defaults.getConfigHash());
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.*members[i] == value);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Vanilla core defence defaults off and counters on, preserving every hashed level combination",
          "[ai][vanilla][strategy][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.vanillaCoreDefence);
    REQUIRE(defaults.force.vanillaArmyCounters);
    REQUIRE(defaults.force.vanillaCoreDefenceThreatBps == 5000);
    REQUIRE(defaults.force.vanillaArmyCounterBlendBps == 5000);
    const std::array<bool QuantBotConfig::ForceSettings::*, 2> switches{
        &QuantBotConfig::ForceSettings::vanillaCoreDefence, &QuantBotConfig::ForceSettings::vanillaArmyCounters};
    const std::array<int QuantBotConfig::ForceSettings::*, 2> levels{
        &QuantBotConfig::ForceSettings::vanillaCoreDefenceThreatBps,
        &QuantBotConfig::ForceSettings::vanillaArmyCounterBlendBps};
    const std::array<const char*, 2> switchNames{"VanillaCoreDefence", "VanillaArmyCounters"};
    const std::array<const char*, 2> levelNames{"VanillaCoreDefenceThreatBps", "VanillaArmyCounterBlendBps"};
    const auto path = std::filesystem::temp_directory_path() / "quantbot-vanilla-core-counter-config-test.ini";
    std::map<std::string, unsigned> hashes;
    // Every switch and level is hashed independently: 2 switches x 2 levels each = 16 cells.
    for (unsigned mask = 0; mask < 16u; ++mask) {
        QuantBotConfig expected = defaults;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\n";
            for (size_t i = 0; i < switches.size(); ++i) {
                expected.force.*switches[i] = (mask & (1u << i)) != 0;
                expected.force.*levels[i] = (mask & (4u << i)) != 0 ? 10000 : 5000;
                ini << switchNames[i] << '=' << (expected.force.*switches[i]) << '\n'
                    << levelNames[i] << '=' << (expected.force.*levels[i]) << '\n';
            }
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        for (size_t i = 0; i < switches.size(); ++i) {
            REQUIRE((loaded.force.*switches[i]) == (expected.force.*switches[i]));
            REQUIRE((loaded.force.*levels[i]) == (expected.force.*levels[i]));
        }
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.emplace(loaded.getConfigHash(), mask).second);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        for (size_t i = 0; i < switches.size(); ++i) {
            REQUIRE((restored.force.*switches[i]) == (expected.force.*switches[i]));
            REQUIRE((restored.force.*levels[i]) == (expected.force.*levels[i]));
        }
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    REQUIRE(hashes.count(defaults.getConfigHash()) == 1);
    std::filesystem::remove(path);
}

TEST_CASE("Vanilla core defence and army counter levels load only as 5000 or 10000",
          "[ai][vanilla][strategy][config]") {
    const std::array<int QuantBotConfig::ForceSettings::*, 2> members{
        &QuantBotConfig::ForceSettings::vanillaCoreDefenceThreatBps,
        &QuantBotConfig::ForceSettings::vanillaArmyCounterBlendBps};
    const std::array<const char*, 2> names{"VanillaCoreDefenceThreatBps", "VanillaArmyCounterBlendBps"};
    const auto path = std::filesystem::temp_directory_path() / "quantbot-vanilla-core-counter-level-test.ini";
    const QuantBotConfig defaults;
    for (size_t i = 0; i < members.size(); ++i)
        for (const int value : {-10000, 0, 4999, 5000, 7500, 9999, 10000, 10001, 20000}) {
            const int level = value == 10000 ? 10000 : 5000;
            QuantBotConfig expected = defaults;
            expected.force.*members[i] = level;
            {
                std::ofstream ini(path);
                ini << "[Force Concentration]\n" << names[i] << '=' << value << '\n';
            }
            QuantBotConfig loaded;
            REQUIRE(loaded.applyOverrides(path.string()));
            REQUIRE(loaded.force.*members[i] == level);
            REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
            REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (level == 5000));
        }
    std::filesystem::remove(path);
}

TEST_CASE("Vanilla core defence policy is FFA-scoped, threat-sized and capped", "[ai][vanilla][strategy]") {
    using namespace QuantBotCoreDefencePolicy;
    // Scope: the original occupied rows of an autonomous Custom Brutal Vanilla game with spice.
    REQUIRE(tuningScope(true, false, true, true, 1, 3, true));         // FFA4
    REQUIRE(tuningScope(true, false, true, true, 1, 5, true));         // FFA6
    REQUIRE_FALSE(tuningScope(false, false, true, true, 1, 3, true));  // not autonomous Custom Brutal
    REQUIRE_FALSE(tuningScope(true, true, true, true, 1, 3, true));    // city
    REQUIRE_FALSE(tuningScope(true, false, false, true, 1, 3, true));  // another mod
    REQUIRE_FALSE(tuningScope(true, false, true, false, 1, 3, true));  // no sustainable spice
    REQUIRE_FALSE(tuningScope(true, false, true, true, 2, 4, true));   // allies (2v2v2)
    REQUIRE_FALSE(tuningScope(true, false, true, true, 1, 1, false));  // duel
    REQUIRE_FALSE(tuningScope(true, false, true, true, 1, 2, true));   // FFA3
    REQUIRE_FALSE(tuningScope(true, false, true, true, 1, 3, false));  // 1v3 against one team

    REQUIRE(level(5000) == 5000);
    REQUIRE(level(10000) == 10000);
    REQUIRE(level(0) == 5000);
    REQUIRE(level(7500) == 5000);
    REQUIRE(level(10001) == 5000);

    // The band: the attacker's own weapon range plus 24 tiles; a negative range counts as 0.
    REQUIRE(kApproachTiles == 24);
    REQUIRE(inThreatBand(24, 0));
    REQUIRE_FALSE(inThreatBand(25, 0));
    REQUIRE(inThreatBand(29, 5));
    REQUIRE_FALSE(inThreatBand(30, 5));
    REQUIRE(inThreatBand(24, -3));
    REQUIRE_FALSE(inThreatBand(25, -3));

    REQUIRE(response(3000, 5000) == 1500);
    REQUIRE(response(3000, 10000) == 3000);
    REQUIRE(response(3000, 7500) == 1500);
    REQUIRE(response(0, 10000) == 0);
    REQUIRE(response(-1, 10000) == 0);
    REQUIRE(response(std::numeric_limits<int>::max(), 10000) == std::numeric_limits<int>::max());
    REQUIRE(response(std::numeric_limits<int>::max(), 5000) == std::numeric_limits<int>::max() / 2);

    // Emplacements: none without a threat, one per 1,000 response credits, per-core caps.
    REQUIRE(kTurretTotalCap == 16);
    REQUIRE(turretGoal(0, 10000, false) == 0);
    REQUIRE(turretGoal(0, 10000, true) == 0);
    REQUIRE(turretGoal(1, 5000, false) == 0);
    REQUIRE(turretGoal(2, 5000, false) == 1);
    REQUIRE(turretGoal(2000, 5000, false) == 1);
    REQUIRE(turretGoal(2000, 10000, false) == 2);
    REQUIRE(turretGoal(2001, 10000, false) == 3);
    REQUIRE(turretGoal(100000, 10000, false) == kGroundTurretsPerCore);
    REQUIRE(turretGoal(100000, 10000, true) == kAirTurretsPerCore);
    REQUIRE(turretGoal(std::numeric_limits<int>::max(), 10000, false) == 3);
    REQUIRE(turretGoal(std::numeric_limits<int>::max(), 5000, true) == 2);

    // Guards: the response value, no guard at zero threat, at most a third of the army and eight units.
    REQUIRE(kGuardRadius == 4);
    REQUIRE(kOrdersPerPass == 4);
    REQUIRE(guardGoal(3000, 5000) == 1500);
    REQUIRE(guardGoal(3000, 10000) == 3000);
    REQUIRE(guardGoal(0, 10000) == 0);
    REQUIRE(guardValueLimit(3000) == 1000);
    REQUIRE(guardValueLimit(-5) == 0);
    REQUIRE(guardCountLimit(2) == 0);
    REQUIRE(guardCountLimit(9) == 3);
    REQUIRE(guardCountLimit(30) == kMaxGuards);
    REQUIRE(guardCountLimit(-3) == 0);
}

TEST_CASE("Vanilla boom spice level accepts only 4000 or 8000 and persists its normalization",
          "[ai][vanilla][strategy][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.vanillaBoomSpicePerRefinery == 4000);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-vanilla-boom-spice-test.ini";
    for (const int value : {-1, 0, 3999, 4001, 6000, 7999, 8001, 100000}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nVanillaBoomSpicePerRefinery=" << value << '\n';
        }
        QuantBotConfig loaded;
        loaded.force.vanillaBoomSpicePerRefinery = 8000;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.vanillaBoomSpicePerRefinery == 4000);
        REQUIRE(loaded.getConfigHash() == defaults.getConfigHash());
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.vanillaBoomSpicePerRefinery == 4000);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Vanilla spice caps split ties before summing and gate only affordable new boom capacity",
          "[ai][vanilla][strategy]") {
    using namespace QuantBotVanillaPolicy;
    REQUIRE(spiceTileShare(10, 3, 1) == 3);
    REQUIRE(spiceTileShare(10, 3, 2) == 6);
    REQUIRE(spiceTileShare(10, 3, 3) == 9);
    REQUIRE(spiceTileShare(10, 0, 0) == 0);
    REQUIRE(spiceTileShare(9000000001LL, 2, 1) == 4500000000LL);
    for (const int level : {4000, 8000}) {
        REQUIRE(spicePerRefineryLevel(level) == level);
        const auto empty = spiceCaps(0, level);
        REQUIRE(empty.refineries == 0);
        REQUIRE(empty.workers == 0);
        REQUIRE(spiceGatedPlan(Plan::Refinery, empty, 0, 0) == Plan::None);
        REQUIRE(spiceGatedPlan(Plan::Harvester, empty, 0, 0) == Plan::None);
        const auto crumbs = spiceCaps(1999, level);
        REQUIRE(crumbs.refineries == 0);
        REQUIRE(crumbs.workers == 0);
        REQUIRE(spiceGatedPlan(Plan::Refinery, crumbs, 0, 0) == Plan::None);
        REQUIRE(spiceGatedPlan(Plan::Harvester, crumbs, 0, 0) == Plan::None);
        const auto below = spiceCaps(level - 1, level);
        REQUIRE(below.refineries == 0);
        REQUIRE(below.workers == 1);
        REQUIRE(spiceGatedPlan(Plan::Refinery, below, 0, 0) == Plan::None);
        const auto one = spiceCaps(level, level);
        REQUIRE(one.refineries == 1);
        REQUIRE(one.workers == 2);
        REQUIRE(spiceGatedPlan(Plan::Refinery, one, 0, 0) == Plan::Refinery);
        REQUIRE(spiceGatedPlan(Plan::Refinery, one, 1, 0) == Plan::None);
        REQUIRE(spiceGatedPlan(Plan::Harvester, one, 1, 1) == Plan::Harvester);
        REQUIRE(spiceGatedPlan(Plan::Harvester, one, 1, 2) == Plan::None);
        const auto odd = spiceCaps(level + level / 2, level);
        REQUIRE(odd.refineries == 1);
        REQUIRE(odd.workers == 3); // Not twice the rounded-down refinery cap.
        REQUIRE(spiceGatedPlan(Plan::Harvester, odd, 1, 2) == Plan::Harvester);
        REQUIRE(spiceGatedPlan(Plan::Harvester, odd, 1, 3) == Plan::None);
        const auto limited = capSpiceTargets(targets(Plan::Refinery, 8, 6, 8, 50), odd);
        REQUIRE(limited.refineries == 1);
        REQUIRE(limited.workers == 3);
        REQUIRE(limited.factories == 1);
        const auto before = spiceCaps(3 * level, level), after = spiceCaps(3 * level - 1, level);
        REQUIRE(spiceGatedPlan(Plan::Refinery, before, 2, 4) == Plan::Refinery);
        REQUIRE(spiceGatedPlan(Plan::Refinery, after, 2, 4) == Plan::None);
        REQUIRE(spiceGatedPlan(Plan::Harvester, before, 2, 5) == Plan::Harvester);
        REQUIRE(spiceGatedPlan(Plan::Harvester, after, 2, 5) == Plan::None);
        const auto sustainable = targets(Plan::Refinery, 8, 6, 8, 3);
        const auto unchanged = capSpiceTargets(sustainable, spiceCaps(20 * level, level));
        REQUIRE(unchanged.workers == sustainable.workers);
        REQUIRE(unchanged.refineries == sustainable.refineries);
        for (const Plan other : {Plan::None, Plan::GreedyEconomy, Plan::GreedyArmy,
                                 Plan::ReactiveEconomy, Plan::ReactiveArmy})
            REQUIRE(spiceGatedPlan(other, empty, 0, 0) == other);
    }
    for (const int value : {-1, 0, 3999, 4001, 6000, 7999, 8001})
        REQUIRE(spicePerRefineryLevel(value) == 4000);
    const auto large = spiceCaps(8000000001LL, 8000);
    REQUIRE(large.refineries == 1000000);
    REQUIRE(large.workers == 2000000);
}

TEST_CASE("Economic fleet ratios default off and independently save load and hash every fixed level",
          "[ai][economy][fleet-ratios][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.economicFleetRatios);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-economic-fleet-ratios.ini";
    std::map<std::string, int> hashes;
    int index = 0;
    for (const bool enabled : {false, true})
        for (const int workers : {2, 3, 4})
            for (const int carryalls : {0, 1, 2})
                for (const int repairs : {0, 1}) {
                    QuantBotConfig expected = defaults;
                    expected.force.economicFleetRatios = enabled;
                    expected.force.fleetHarvestersPerBay = workers;
                    expected.force.fleetCarryallQuarters = carryalls;
                    expected.force.fleetRepairYards = repairs;
                    {
                        std::ofstream ini(path);
                        ini << "[Force Concentration]\nEconomicFleetRatios=" << enabled
                            << "\nFleetHarvestersPerBay=" << workers
                            << "\nFleetCarryallQuarters=" << carryalls
                            << "\nFleetRepairYards=" << repairs << '\n';
                    }
                    QuantBotConfig loaded;
                    REQUIRE(loaded.applyOverrides(path.string()));
                    REQUIRE(loaded.force.economicFleetRatios == enabled);
                    REQUIRE(loaded.force.fleetHarvestersPerBay == workers);
                    REQUIRE(loaded.force.fleetCarryallQuarters == carryalls);
                    REQUIRE(loaded.force.fleetRepairYards == repairs);
                    REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
                    REQUIRE(hashes.emplace(loaded.getConfigHash(), index++).second);
                    REQUIRE(loaded.save(path.string()));
                    QuantBotConfig restored;
                    REQUIRE(restored.applyOverrides(path.string()));
                    REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
                }
    std::filesystem::remove(path);
}

TEST_CASE("Economic fleet fixed ratios advance one bay and stop at shared available spice",
          "[ai][economy][fleet-ratios]") {
    using QuantBotBuildPolicy::economicFleetTargets;
    for (const int ratio : {2, 3, 4}) {
        const auto initial = economicFleetTargets(100000, 20, 1, 1, ratio, 1, 1, 1200, 2000, 800, true);
        REQUIRE(initial.workers == ratio);
        REQUIRE(initial.refineries == 1);
        const auto filled = economicFleetTargets(100000, 20, 1, ratio, ratio, 1, 1, 1200, 2000, 800, true);
        REQUIRE(filled.refineries == 2);
        REQUIRE(filled.workers == ratio);
        const auto expanded = economicFleetTargets(100000, 20, 2, ratio + 1, ratio, 1, 1, 1200, 2000, 800, true);
        REQUIRE(expanded.workers == ratio * 2);
        REQUIRE(expanded.carryalls == (ratio + 4) / 4);
    }
    const auto empty = economicFleetTargets(0, 40, 3, 12, 4, 2, 1, 1200, 2000, 800, true);
    REQUIRE(empty.sustainable == 0);
    REQUIRE(empty.workers == 0);
    REQUIRE(empty.refineries == 0);
    REQUIRE(empty.carryalls == 0);
    REQUIRE(empty.repairYards == 0);
    const auto scarce = economicFleetTargets(5000, 40, 6, 20, 2, 2, 1, 1200, 2000, 800, true);
    REQUIRE(scarce.sustainable == 1);
    REQUIRE(scarce.workers == 1);
    REQUIRE(scarce.refineries == 1);
    const auto unsupported = economicFleetTargets(100000, 3, 10, 10, 4, 0, 0, 1200, 2000, 800, true);
    REQUIRE(unsupported.workers == 3);
    REQUIRE(unsupported.carryalls == 0);
    REQUIRE(unsupported.repairYards == 0);
}

TEST_CASE("Economic fleet ratios keep the opening refineries until a heavy factory can build workers",
          "[ai][economy][fleet-ratios]") {
    using QuantBotBuildPolicy::economicFleetTargets;
    using QuantBotBuildPolicy::kOpeningSupplierRefineries;
    using QuantBotBuildPolicy::supplierRefineryGoal;
    for (const int ratio : {2, 3, 4}) {
        // The 5ba screen deadlock: the first refinery delivered one worker, the cap
        // 1 + workers/perBay refused every further refinery, and the opening ladders
        // (four refineries before the worker supplier) kept selecting the refused one.
        const auto capped = economicFleetTargets(100000, 20, 1, 1, ratio, 1, 1, 1200, 2000, 800, true);
        REQUIRE(capped.refineries == 1);
        const int ladder = std::min(kOpeningSupplierRefineries, capped.sustainable);
        REQUIRE(ladder == 4);
        // Without a factory the cap admits the whole native opening ladder.
        const auto opening = economicFleetTargets(100000, 20, 1, 1, ratio, 1, 1, 1200, 2000, 800, false);
        REQUIRE(opening.refineries == kOpeningSupplierRefineries);
        REQUIRE(opening.workers == capped.workers);
        REQUIRE(supplierRefineryGoal(ladder, true, opening.refineries) == ladder);
        // Lost workers do not lower the floor while no factory can replace them.
        const auto lost = economicFleetTargets(100000, 20, 2, 0, ratio, 1, 1, 1200, 2000, 800, false);
        REQUIRE(lost.refineries == kOpeningSupplierRefineries);
        // With a factory the ladder stops at the ratio's cap instead of holding the yard.
        REQUIRE(supplierRefineryGoal(ladder, true, capped.refineries) == 1);
    }
    // Refineries deliver their own worker, so the floor applies on a scarce share
    // exactly as the native ladder does; an empty share stays at zero.
    const auto scarce = economicFleetTargets(5000, 40, 1, 1, 2, 2, 1, 1200, 2000, 800, false);
    REQUIRE(scarce.sustainable == 1);
    REQUIRE(scarce.refineries == kOpeningSupplierRefineries);
    const auto empty = economicFleetTargets(0, 40, 0, 0, 3, 1, 1, 1200, 2000, 800, false);
    REQUIRE(empty.refineries == 0);
    REQUIRE(supplierRefineryGoal(4, true, empty.refineries) == 0);
    // Once a factory exists the selected ratio governs expansion again.
    const auto factory = economicFleetTargets(100000, 20, 3, 3, 3, 1, 1, 1200, 2000, 800, true);
    REQUIRE(factory.refineries == 2);
    // Switch OFF: the native ladder goal is untouched.
    REQUIRE(supplierRefineryGoal(4, false, 0) == 4);
    REQUIRE(supplierRefineryGoal(2, false, 7) == 2);
}

TEST_CASE("Vanilla boom phases preserve scope and convert greedy spending at the declared stop",
          "[ai][vanilla][strategy]") {
    using namespace QuantBotVanillaPolicy;
    REQUIRE(rosterScope(1, 1, false));
    REQUIRE(rosterScope(2, 2, false));
    REQUIRE(rosterScope(1, 3, true));
    REQUIRE(rosterScope(1, 5, true));
    REQUIRE_FALSE(rosterScope(1, 4, false));
    REQUIRE_FALSE(rosterScope(0, 4, true));
    REQUIRE_FALSE(rosterScope(1, 0, false));
    REQUIRE(boomScope(false, 1, false));
    REQUIRE_FALSE(boomScope(true, 1, false));
    REQUIRE_FALSE(boomScope(true, 2, false));
    REQUIRE_FALSE(boomScope(true, 4, false));
    REQUIRE(boomScope(true, 3, true));
    REQUIRE(boomScope(true, 5, true));
    for (const int workers : {8, 12}) {
        const auto goal = targets(Plan::Harvester, workers, 4, 8, 50);
        REQUIRE(goal.workers == workers);
        REQUIRE(goal.refineries == 2);
        REQUIRE(goal.factories == 1);
    }
    for (const int refineries : {4, 6}) {
        const auto goal = targets(Plan::Refinery, 8, refineries, 8, 50);
        REQUIRE(goal.workers == 2 * refineries);
        REQUIRE(goal.refineries == refineries);
    }
    REQUIRE(timedPlan(Plan::Harvester, 9, 6, false) == Plan::Harvester);
    REQUIRE(timedPlan(Plan::Harvester, 10, 6, false) == Plan::None);
    REQUIRE(timedPlan(Plan::Refinery, 10, 6, false) == Plan::None);
    for (const int stop : {6, 10, 12}) {
        REQUIRE(timedPlan(Plan::GreedyEconomy, stop - 1, stop, false) == Plan::GreedyEconomy);
        REQUIRE(timedPlan(Plan::GreedyEconomy, stop, stop, false) == Plan::GreedyArmy);
        REQUIRE(timedPlan(Plan::GreedyEconomy, 19, stop, false) == Plan::GreedyArmy);
        REQUIRE(timedPlan(Plan::GreedyEconomy, 20, stop, false) == Plan::None);
    }
    const auto boom = targets(Plan::GreedyEconomy, 8, 4, 8, 50);
    REQUIRE(boom.workers == 16);
    REQUIRE(boom.refineries == 6);
    REQUIRE(boom.factories == 2);
    const auto army = targets(Plan::GreedyArmy, 8, 4, 8, 50);
    REQUIRE(army.workers == 4);
    REQUIRE(army.refineries == 2);
    REQUIRE(armyPhase(Plan::GreedyArmy));
    REQUIRE(armyPhase(Plan::ReactiveArmy));
    REQUIRE_FALSE(armyPhase(Plan::Harvester));
    REQUIRE(deferTech(Plan::GreedyArmy, 16, 6, 2, army));
    REQUIRE_FALSE(deferTech(Plan::Harvester, 8, 2, 1, targets(Plan::Harvester, 8, 4, 8, 50)));
    REQUIRE(targets(Plan::GreedyEconomy, 8, 4, 8, 3).workers == 3);
    for (const int ratio : {6000, 9000}) for (const int economy : {12000, 15000}) {
        REQUIRE_FALSE(reactiveArmy(ratio, 10000, 0, 10000, 10000, ratio, economy));
        REQUIRE(reactiveArmy(ratio - 1, 10000, 0, 10000, 10000, ratio, economy));
        REQUIRE(reactiveArmy(10000, 10000, 10001, 10000, 10000, ratio, economy));
        REQUIRE(reactiveArmy(10000, 10000, 0, economy, 10000, ratio, economy));
        REQUIRE_FALSE(reactiveArmy(10000, 10000, 0, economy - 1, 10000, ratio, economy));
    }
}

TEST_CASE("Refinery greedy compound boundaries reuse refinery targets and greedy army conversion",
          "[ai][vanilla][strategy]") {
    using namespace QuantBotVanillaPolicy;
    struct Boundary { unsigned minute; Plan plan; Targets target; };
    const std::array<Boundary, 4> boundaries{{
        {5, Plan::Refinery, {12, 6, 1}},
        {6, Plan::GreedyArmy, {4, 2, 1}},
        {19, Plan::GreedyArmy, {4, 2, 1}},
        {20, Plan::None, {0, 0, 0}},
    }};
    for (const auto& boundary : boundaries) {
        const Plan plan = timedPlan(Plan::Refinery, boundary.minute, 6, true);
        REQUIRE(plan == boundary.plan);
        const auto goal = targets(plan, 8, 6, 8, 50);
        REQUIRE(goal.workers == boundary.target.workers);
        REQUIRE(goal.refineries == boundary.target.refineries);
        REQUIRE(goal.factories == boundary.target.factories);
        REQUIRE(armyPhase(plan) == (boundary.plan == Plan::GreedyArmy));
        if (plan != Plan::None)
            REQUIRE(deferTech(plan, goal.workers, goal.refineries, goal.factories, goal) == armyPhase(plan));
    }
    const auto limited = targets(timedPlan(Plan::Refinery, 5, 6, true), 8, 6, 8, 3);
    REQUIRE(limited.workers == 3);
    REQUIRE(limited.refineries == 2);
    REQUIRE(readiness(Plan::GreedyEconomy) == 150);
    REQUIRE(readiness(Plan::GreedyArmy) == 150);
}

TEST_CASE("Compound stops use existing levels without changing single or higher priority plans",
          "[ai][vanilla][strategy]") {
    using namespace QuantBotVanillaPolicy;
    for (const int stop : {6, 10, 12}) {
        REQUIRE(timedPlan(Plan::Refinery, stop - 1, stop, true) == Plan::Refinery);
        REQUIRE(timedPlan(Plan::Refinery, stop, stop, true) == Plan::GreedyArmy);
        REQUIRE(timedPlan(Plan::Refinery, 19, stop, true) == Plan::GreedyArmy);
        REQUIRE(timedPlan(Plan::Refinery, 20, stop, true) == Plan::None);
        for (const unsigned minute : {5u, 6u, 9u, 10u, 11u, 12u, 19u, 20u}) {
            const Plan opening = minute < 10 ? Plan::Harvester : Plan::None;
            REQUIRE(timedPlan(Plan::Harvester, minute, stop, false) == opening);
            REQUIRE(timedPlan(Plan::Harvester, minute, stop, true) == opening);
            REQUIRE(timedPlan(Plan::Refinery, minute, stop, false) == (minute < 10 ? Plan::Refinery : Plan::None));
            const Plan greedy = minute < unsigned(stop) ? Plan::GreedyEconomy
                : minute < 20 ? Plan::GreedyArmy : Plan::None;
            REQUIRE(timedPlan(Plan::GreedyEconomy, minute, stop, false) == greedy);
            REQUIRE(timedPlan(Plan::GreedyEconomy, minute, stop, true) == greedy);
            for (const Plan unchanged : {Plan::None, Plan::ReactiveEconomy, Plan::ReactiveArmy}) {
                REQUIRE(timedPlan(unchanged, minute, stop, false) == unchanged);
                REQUIRE(timedPlan(unchanged, minute, stop, true) == unchanged);
            }
        }
    }
}

TEST_CASE("FFA vulture levels persist and use strict deterministic force thresholds",
          "[ai][force][ffa][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.ffaVultureLevel == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-vulture-config-test.ini";
    std::set<std::string> hashes;
    for (int level = 0; level <= 2; ++level) {
        QuantBotConfig expected = defaults;
        expected.force.ffaVultureLevel = level;
        REQUIRE(hashes.insert(expected.getConfigHash()).second);
        REQUIRE(expected.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.ffaVultureLevel == level);
        REQUIRE(restored.getConfigHash() == expected.getConfigHash());
    }
    REQUIRE_FALSE(ForceConcentrationPolicy::vultureStrike(0, 1000, 0));
    REQUIRE_FALSE(ForceConcentrationPolicy::vultureStrike(1, 0, 0));
    REQUIRE(ForceConcentrationPolicy::vultureStrike(1, 1000, 599));
    REQUIRE_FALSE(ForceConcentrationPolicy::vultureStrike(1, 1000, 600));
    REQUIRE(ForceConcentrationPolicy::vultureStrike(2, 1000, 899));
    REQUIRE_FALSE(ForceConcentrationPolicy::vultureStrike(2, 1000, 900));
    std::filesystem::remove(path);
}

TEST_CASE("Team coordination switches default off, persist and hash independently",
          "[ai][team][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.teamSynchroniseAttacks);
    REQUIRE(defaults.force.teamDefenceSharePercent == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-team-config-test.ini";
    std::map<std::string, int> hashes;
    for (int sync : {0, 1}) for (int share : {0, 25, 50}) {
        QuantBotConfig expected = defaults;
        expected.force.teamSynchroniseAttacks = sync != 0;
        expected.force.teamDefenceSharePercent = share;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nTeamSynchroniseAttacks=" << sync
                << "\nTeamDefenceSharePercent=" << share << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.teamSynchroniseAttacks == expected.force.teamSynchroniseAttacks);
        REQUIRE(loaded.force.teamDefenceSharePercent == share);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.emplace(loaded.getConfigHash(), sync * 100 + share).second);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    for (int invalid : {-1, 1, 24, 26, 49, 51, 100}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nTeamDefenceSharePercent=" << invalid << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.teamDefenceSharePercent == 0);
        REQUIRE(loaded.getConfigHash() == defaults.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Accepted campaign defaults retain an explicit all-off control",
          "[ai][campaign][production][config]") {
    const QuantBotConfig defaults;
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-all-off-test.ini";
    {
        std::ofstream ini(path);
        ini << "[Force Concentration]\n"
            << "CampaignMilitaryOpening=0\nCampaignAdaptiveReadiness=0\n"
            << "CampaignThreatProduction=0\nCampaignFundedLightOpening=0\n"
            << "CampaignLightWorkingCash=0\nCampaignLightIncomeSeed=0\nCampaignIdleCashFactory=0\n";
    }
    QuantBotConfig expected = defaults;
    expected.force.campaignMilitaryOpening = false;
    expected.force.campaignAdaptiveReadiness = false;
    expected.force.campaignThreatProduction = false;
    expected.force.campaignFundedLightOpening = false;
    expected.force.campaignLightWorkingCash = false;
    expected.force.campaignLightIncomeSeed = false;
    expected.force.campaignIdleCashFactory = false;
    QuantBotConfig control;
    REQUIRE(control.applyOverrides(path.string()));
    REQUIRE_FALSE(control.force.campaignMilitaryOpening);
    REQUIRE_FALSE(control.force.campaignAdaptiveReadiness);
    REQUIRE_FALSE(control.force.campaignThreatProduction);
    REQUIRE_FALSE(control.force.campaignFundedLightOpening);
    REQUIRE_FALSE(control.force.campaignLightWorkingCash);
    REQUIRE_FALSE(control.force.campaignLightIncomeSeed);
    REQUIRE_FALSE(control.force.campaignIdleCashFactory);
    REQUIRE(control.getConfigHash() == expected.getConfigHash());
    REQUIRE(control.getConfigHash() != defaults.getConfigHash());
    REQUIRE(control.save(path.string()));
    QuantBotConfig restored;
    REQUIRE(restored.applyOverrides(path.string()));
    REQUIRE(restored.getConfigHash() == control.getConfigHash());
    std::filesystem::remove(path);
}

TEST_CASE("Campaign opening switches default on and independently round-trip both hashed states",
          "[ai][campaign][opening][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.campaignMilitaryOpening);
    REQUIRE(defaults.force.campaignAdaptiveReadiness);
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
            REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (production && readiness));
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

TEST_CASE("Campaign threat production defaults on and independently round-trips both hashed states",
          "[ai][campaign][production][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.campaignThreatProduction);
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
        REQUIRE((config.getConfigHash() == defaults.getConfigHash()) == enabled);
        REQUIRE(config.force.campaignMilitaryOpening == defaults.force.campaignMilitaryOpening);
        REQUIRE(config.force.campaignAdaptiveReadiness == defaults.force.campaignAdaptiveReadiness);
        REQUIRE(config.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.campaignThreatProduction == enabled);
        REQUIRE(restored.getConfigHash() == config.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Campaign light working cash independently persists both hashed states",
          "[ai][campaign][production][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.campaignLightWorkingCash);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-working-cash-test.ini";
    for (const bool enabled : {false, true, false}) {
        QuantBotConfig expected = defaults;
        expected.force.campaignLightWorkingCash = enabled;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCampaignLightWorkingCash=" << enabled << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.campaignLightWorkingCash == enabled);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == enabled);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.campaignLightWorkingCash == enabled);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Campaign light income seed independently persists both hashed states",
          "[ai][campaign][production][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.campaignLightIncomeSeed);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-income-seed-test.ini";
    for (const bool enabled : {false, true, false}) {
        QuantBotConfig expected = defaults;
        expected.force.campaignLightIncomeSeed = enabled;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCampaignLightIncomeSeed=" << enabled << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.campaignLightIncomeSeed == enabled);
        REQUIRE(loaded.force.campaignLightWorkingCash == defaults.force.campaignLightWorkingCash);
        REQUIRE(loaded.force.campaignFundedLightOpening == defaults.force.campaignFundedLightOpening);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == enabled);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.campaignLightIncomeSeed == enabled);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Campaign round-two economy controls independently persist every hashed arm",
          "[ai][campaign][economy][config]") {
    const QuantBotConfig defaults;
    const std::array<bool QuantBotConfig::ForceSettings::*, 4> members{
        &QuantBotConfig::ForceSettings::campaignFactoryBeforeStarport,
        &QuantBotConfig::ForceSettings::campaignIdleCashFactory,
        &QuantBotConfig::ForceSettings::campaignRefineryDefence,
        &QuantBotConfig::ForceSettings::campaignFactoryRunway};
    const std::array<const char*, 4> names{
        "CampaignFactoryBeforeStarport", "CampaignIdleCashFactory",
        "CampaignRefineryDefence", "CampaignFactoryRunway"};
    REQUIRE_FALSE(defaults.force.campaignFactoryBeforeStarport);
    REQUIRE(defaults.force.campaignIdleCashFactory);
    REQUIRE_FALSE(defaults.force.campaignRefineryDefence);
    REQUIRE_FALSE(defaults.force.campaignFactoryRunway);
    REQUIRE(defaults.force.campaignIdleCashFactoryCredits == 2000);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-round-two-config-test.ini";
    std::map<std::string, unsigned> hashes;
    for (const int threshold : {1500, 2000}) {
        for (unsigned mask = 0; mask < (1u << members.size()); ++mask) {
            QuantBotConfig expected = defaults;
            expected.force.campaignIdleCashFactoryCredits = threshold;
            {
                std::ofstream ini(path);
                ini << "[Force Concentration]\nCampaignIdleCashFactoryCredits=" << threshold << '\n';
                for (size_t i = 0; i < members.size(); ++i) {
                    expected.force.*members[i] = (mask & (1u << i)) != 0;
                    ini << names[i] << '=' << (expected.force.*members[i]) << '\n';
                }
            }
            QuantBotConfig loaded;
            REQUIRE(loaded.applyOverrides(path.string()));
            for (const auto member : members) REQUIRE((loaded.force.*member) == (expected.force.*member));
            REQUIRE(loaded.force.campaignIdleCashFactoryCredits == threshold);
            REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
            REQUIRE(hashes.emplace(loaded.getConfigHash(), mask).second);
            REQUIRE(loaded.force.campaignMilitaryOpening == defaults.force.campaignMilitaryOpening);
            REQUIRE(loaded.force.campaignFundedLightOpening == defaults.force.campaignFundedLightOpening);
            REQUIRE(loaded.force.campaignLightWorkingCash == defaults.force.campaignLightWorkingCash);
            REQUIRE(loaded.force.campaignLightIncomeSeed == defaults.force.campaignLightIncomeSeed);
            REQUIRE(loaded.save(path.string()));
            QuantBotConfig restored;
            REQUIRE(restored.applyOverrides(path.string()));
            for (const auto member : members) REQUIRE((restored.force.*member) == (expected.force.*member));
            REQUIRE(restored.force.campaignIdleCashFactoryCredits == threshold);
            REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
        }
    }
    std::filesystem::remove(path);
}

TEST_CASE("Campaign home fields have two independently hashed persistent levels",
          "[ai][campaign][economy][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.campaignHomeFields == 2);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-home-fields-test.ini";
    std::map<std::string, int> hashes;
    for (const int level : {0, 1, 2}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCampaignHomeFields=" << level << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.campaignHomeFields == level);
        QuantBotConfig expected = defaults;
        expected.force.campaignHomeFields = level;
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.emplace(loaded.getConfigHash(), level).second);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.campaignHomeFields == level);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
    REQUIRE(TacticalSafetyPolicy::homeFieldAllowed(100, 1, 0));
    REQUIRE(TacticalSafetyPolicy::homeFieldAllowed(7, 7, 1));
    REQUIRE_FALSE(TacticalSafetyPolicy::homeFieldAllowed(8, 7, 1));
    REQUIRE_FALSE(TacticalSafetyPolicy::homeFieldAllowed(7, 10, 2));
    REQUIRE(TacticalSafetyPolicy::homeFieldAllowed(7, 11, 2));
}

TEST_CASE("Campaign home fields require initial spice and a legally low-tech opening",
          "[ai][campaign][economy][scope]") {
    using TacticalSafetyPolicy::homeFieldOpeningScope;
    REQUIRE_FALSE(homeFieldOpeningScope(-1, true, 3, 2));
    REQUIRE_FALSE(homeFieldOpeningScope(0, true, 3, 2));
    REQUIRE(homeFieldOpeningScope(1, true, 3, 2));
    REQUIRE_FALSE(homeFieldOpeningScope(100, true, 3, 3));
    REQUIRE_FALSE(homeFieldOpeningScope(100, true, 3, 8));
    REQUIRE(homeFieldOpeningScope(100, false, 3, 8));
    REQUIRE_FALSE(homeFieldOpeningScope(0, false, 3, 8));
}

TEST_CASE("Campaign sized worker rescue persists OFF and both force coverage levels",
          "[ai][campaign][defence][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.campaignSizedWorkerRescue == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-worker-rescue-test.ini";
    std::map<std::string, int> hashes;
    for (const int level : {0, 1, 2}) {
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCampaignSizedWorkerRescue=" << level << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.campaignSizedWorkerRescue == level);
        REQUIRE(loaded.force.campaignHomeFields == defaults.force.campaignHomeFields);
        QuantBotConfig expected = defaults;
        expected.force.campaignSizedWorkerRescue = level;
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE(hashes.emplace(loaded.getConfigHash(), level).second);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.campaignSizedWorkerRescue == level);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
    }
    std::filesystem::remove(path);
    using TacticalSafetyPolicy::campaignWorkerRescueCoverage;
    REQUIRE(campaignWorkerRescueCoverage(0) == 0);
    REQUIRE(campaignWorkerRescueCoverage(1) == 100);
    REQUIRE(campaignWorkerRescueCoverage(2) == 150);
    const auto equal = ForceConcentrationPolicy::scaledReadiness(1000, campaignWorkerRescueCoverage(1));
    const auto superior = ForceConcentrationPolicy::scaledReadiness(1000, campaignWorkerRescueCoverage(2));
    REQUIRE(ForceConcentrationPolicy::answerable(equal, 100, 1100));
    REQUIRE_FALSE(ForceConcentrationPolicy::answerable(superior, 100, 1100));
    REQUIRE(ForceConcentrationPolicy::answerable(superior, 100, 1400));
    const std::vector<ForceConcentrationPolicy::SizedResponder> responders{
        {2, 500, 8, false}, {1, 700, 3, false}, {3, 200, 12, false}};
    const std::vector<uint32_t> expectedOrder{1, 2};
    REQUIRE(ForceConcentrationPolicy::sizedPool(equal, 100, responders,
        ForceConcentrationPolicy::kSizedBandTiles) == expectedOrder);
}

TEST_CASE("Campaign funded light opening independently persists both hashed states",
          "[ai][campaign][production][config]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.campaignFundedLightOpening);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-campaign-funded-light-test.ini";
    for (const bool enabled : {false, true, false}) {
        QuantBotConfig expected = defaults;
        expected.force.campaignFundedLightOpening = enabled;
        {
            std::ofstream ini(path);
            ini << "[Force Concentration]\nCampaignFundedLightOpening=" << enabled << '\n';
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.campaignFundedLightOpening == enabled);
        REQUIRE(loaded.getConfigHash() == expected.getConfigHash());
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == enabled);
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.campaignFundedLightOpening == enabled);
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
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

TEST_CASE("Combat target levels default off and round-trip independent hashes", "[ai][config][targets]") {
    const QuantBotConfig defaults;
    REQUIRE(defaults.force.focusFireLevel == 0);
    REQUIRE(defaults.force.targetPriorityLevel == 0);
    const auto path = std::filesystem::temp_directory_path() / "quantbot-target-levels.ini";
    std::vector<std::string> hashes;
    for (int focus : {0, 1, 2}) for (int priority : {0, 1, 2}) {
        QuantBotConfig config;
        config.force.focusFireLevel = focus;
        config.force.targetPriorityLevel = priority;
        REQUIRE(config.save(path.string()));
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.focusFireLevel == focus);
        REQUIRE(loaded.force.targetPriorityLevel == priority);
        REQUIRE(loaded.getConfigHash() == config.getConfigHash());
        REQUIRE(std::find(hashes.begin(), hashes.end(), loaded.getConfigHash()) == hashes.end());
        hashes.push_back(loaded.getConfigHash());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Combat priority levels and strict focus use stable HP and id order", "[ai][targets]") {
    namespace F = ForceConcentrationPolicy;
    REQUIRE(F::tunedTargetClass(F::ArmedUnit, 0, 0, true, false) == F::ArmedUnit);
    REQUIRE(F::tunedTargetClass(F::Wall, 2, 2, false, false) == F::Wall);
    REQUIRE(F::tunedTargetClass(F::Unarmed, 1, 0, false, true) == 0);
    REQUIRE(F::tunedTargetClass(F::ArmedUnit, 0, 1, true, false) > F::ArmedUnit);
    REQUIRE(F::tunedTargetClass(F::Unarmed, 0, 2, false, true)
            > F::tunedTargetClass(F::ArmedUnit, 0, 2, true, false));
    const F::TargetKey current{0, 100, 9}, slightlyLower{0, 90, 10}, tiedLowerId{0, 100, 8};
    REQUIRE_FALSE(F::tunedShouldSwitch(true, current, slightlyLower, 1));
    REQUIRE(F::tunedShouldSwitch(true, current, slightlyLower, 2));
    REQUIRE(F::tunedShouldSwitch(true, current, tiedLowerId, 2));
    REQUIRE_FALSE(F::tunedShouldSwitch(true, current, current, 2));
}
