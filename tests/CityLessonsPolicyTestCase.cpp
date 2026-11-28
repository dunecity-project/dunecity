// Lessons from Stefan's Metropolis save (CityLessonsPolicy.h): the Launcher army mix rule, the
// low-value lot rule, and the two switches' config keys (off by default, read and written).

#include <players/CityLessonsPolicy.h>
#include <players/QuantBotConfig.h>

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <numeric>

namespace L = CityLessonsPolicy;

namespace {
int total(const UnitMixPolicy::Mix& mix) { return std::accumulate(mix.begin(), mix.end(), 0); }
}

TEST_CASE("CityLauncherArmy: the Ornithopter share goes to Launchers, which are floored", "[ai][city-lessons][policy]") {
    // Tank, Siege, Launcher, Devastator, Sonic, Deviator, Ornithopter, Trike, Raider, Quad.
    const UnitMixPolicy::Mix mix{1000, 2000, 2400, 0, 1500, 0, 600, 500, 500, 1500};
    REQUIRE(total(mix) == 10000);
    const auto out = L::launcherArmy(mix, L::kLauncherArmyBps);
    REQUIRE(total(out) == 10000);
    REQUIRE(out[UnitMixPolicy::kOrnithopterSlot] == 0);
    REQUIRE(out[L::kLauncherSlot] == L::kLauncherArmyBps);
    // The rest shrink in proportion and none goes negative or grows.
    for (size_t i = 0; i < mix.size(); ++i) {
        if (i == L::kLauncherSlot) continue;
        REQUIRE(out[i] >= 0);
        REQUIRE(out[i] <= mix[i]);
    }
    REQUIRE(out[1] > out[0]);
}

TEST_CASE("CityLauncherArmy: a Launcher share above the floor keeps the Ornithopters' share too", "[ai][city-lessons][policy]") {
    const UnitMixPolicy::Mix mix{500, 500, 8000, 0, 0, 0, 500, 0, 0, 500};
    const auto out = L::launcherArmy(mix, L::kLauncherArmyBps);
    REQUIRE(out[L::kLauncherSlot] == 8500);
    REQUIRE(out[UnitMixPolicy::kOrnithopterSlot] == 0);
    REQUIRE(out[0] == 500);
    REQUIRE(out[9] == 500);
    REQUIRE(total(out) == 10000);
}

TEST_CASE("LowValuePalaces: low-value lots are those at or below the line", "[ai][city-lessons][policy]") {
    REQUIRE(L::lowLot(0));
    REQUIRE(L::lowLot(L::kLowLandValue));
    REQUIRE_FALSE(L::lowLot(L::kLowLandValue + 1));
    REQUIRE(L::kMinLowLots >= 4);
    REQUIRE(L::kEffectDelayMs < L::kPlanExpiryMs);
}

TEST_CASE("FrontPalaces: a centre 10-31 tiles from the enemy with three own turrets ahead", "[ai][city-lessons][policy]") {
    REQUIRE(L::frontPalaceCentre(10, 3));
    REQUIRE(L::frontPalaceCentre(31, 5));
    REQUIRE_FALSE(L::frontPalaceCentre(9, 5));
    REQUIRE_FALSE(L::frontPalaceCentre(32, 5));
    REQUIRE_FALSE(L::frontPalaceCentre(16, 2));
    REQUIRE(L::kFrontPalaceSurvivalMs > L::kEffectDelayMs);
}

TEST_CASE("City lessons: the switches default off, round trip and hash only when on", "[ai][city-lessons][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.cityLauncherArmy);
    REQUIRE_FALSE(defaults.force.lowValuePalaces);
    REQUIRE_FALSE(defaults.force.frontPalaces);
    REQUIRE(defaults.force.cityLauncherArmyBps == L::kLauncherArmyBps);
    std::vector<std::string> hashes{defaults.getConfigHash()};
    for (const char* body : {"CityLauncherArmy=true\n", "LowValuePalaces=true\n", "FrontPalaces=true\n",
                             "CityLauncherArmy=true\nCityLauncherArmyBps=6000\n"}) {
        const auto path = std::filesystem::temp_directory_path() / "quantbot-city-lessons.ini";
        {
            std::ofstream file(path);
            file << "[Force Concentration]\n" << body;
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(int(loaded.force.cityLauncherArmy) + int(loaded.force.lowValuePalaces) + int(loaded.force.frontPalaces) == 1);
        for (const auto& hash : hashes) REQUIRE(loaded.getConfigHash() != hash);
        hashes.push_back(loaded.getConfigHash());
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
        REQUIRE(restored.force.cityLauncherArmyBps == loaded.force.cityLauncherArmyBps);
        std::filesystem::remove(path);
    }
}

TEST_CASE("EconomyFirst: low / no spice maps only, his yard schedule, core assets", "[ai][city-lessons][policy]") {
    // Below 20,000 spice per living house the switch acts (his map had none).
    REQUIRE(L::lowSpiceMap(0, 4));
    REQUIRE(L::lowSpiceMap(79999, 4));
    REQUIRE_FALSE(L::lowSpiceMap(80000, 4));
    REQUIRE_FALSE(L::lowSpiceMap(780000, 4));
    REQUIRE(L::lowSpiceMap(59999, 3));
    REQUIRE(L::lowSpiceMap(19999, 0));
    // His yards: 4 by minute 2.2, 6 by 17.4, 9 by 22.7.
    REQUIRE(L::economyFirstYards(0) == 4);
    REQUIRE(L::economyFirstYards(14) == 4);
    REQUIRE(L::economyFirstYards(15) == 6);
    REQUIRE(L::economyFirstYards(20) == 9);
    REQUIRE(L::economyFirstYards(90) == 9);
    // His zone mix and share.
    REQUIRE(L::kEconomyFirstZoneMix[0] + L::kEconomyFirstZoneMix[1] + L::kEconomyFirstZoneMix[2] == 328);
    REQUIRE(L::kEconomyFirstZoneShareBps == 7000);
    // A threat releases the hold only near these.
    REQUIRE(L::economyFirstCoreAsset(Structure_ConstructionYard));
    REQUIRE(L::economyFirstCoreAsset(Structure_NuclearPlant));
    REQUIRE_FALSE(L::economyFirstCoreAsset(Structure_ZoneResidential));
    REQUIRE_FALSE(L::economyFirstCoreAsset(Structure_RocketTurret));
}

TEST_CASE("EconomyFirst: config key reads, writes and changes the hash", "[ai][city-lessons][config]") {
    const QuantBotConfig defaults;
    for (const bool value : {true, false}) {
        const auto path = std::filesystem::temp_directory_path() / "quantbot-economy-first.ini";
        {
            std::ofstream file(path);
            file << "[Force Concentration]\nEconomyFirst=" << (value ? "true" : "false") << "\n";
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(loaded.force.economyFirst == value);
        REQUIRE((loaded.getConfigHash() == defaults.getConfigHash()) == (value == defaults.force.economyFirst));
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.economyFirst == value);
        std::filesystem::remove(path);
    }
}

TEST_CASE("EconomyFirst: room, working cash and integral opening schedule boundaries", "[ai][city-lessons][policy]") {
    constexpr std::uint32_t minute=3750;
    REQUIRE(L::economyFirstYardsForRoom(0)==1);
    REQUIRE(L::economyFirstYardsForRoom(9)==1);
    REQUIRE(L::economyFirstYardsForRoom(10)==2);
    REQUIRE(L::economyFirstYardsForRoom(400)==9);
    REQUIRE(L::economyFirstHold(0)==0);
    REQUIRE(L::economyFirstHold(2000)==0);
    REQUIRE(L::economyFirstHold(4000)==1000);
    REQUIRE(L::economyFirstHold(22000)==10000);
    REQUIRE(L::economyFirstUnitAllowance(0,minute)==0);
    REQUIRE(L::economyFirstUnitAllowance(3*minute,minute)==3850);
    REQUIRE(L::economyFirstUnitAllowance(10*minute-1,minute)==8049);
    REQUIRE(L::economyFirstUnitAllowance(10*minute,minute)==std::numeric_limits<int>::max());
    REQUIRE(L::economyFirstTurretTarget(3*minute-1,minute)==0);
    REQUIRE(L::economyFirstTurretTarget(3*minute,minute)==0);
    REQUIRE(L::economyFirstTurretTarget(3*minute+1,minute)==1);
    REQUIRE(L::economyFirstTurretTarget(10*minute-1,minute)==29);
    REQUIRE(L::economyFirstTurretTarget(10*minute,minute)==29);
    REQUIRE(L::economyFirstTurretTarget(20*minute-1,minute)==50);
    REQUIRE(L::economyFirstTurretTarget(31*minute-1,minute)==77);
}

TEST_CASE("EconomyFirst: each opening variant key is independently hashed and round-trips", "[ai][city-lessons][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.economyFirst);
    REQUIRE(defaults.force.economyFirstUnitPacing==0);
    REQUIRE_FALSE(defaults.force.economyFirstTurrets);
    std::vector<std::string> hashes{defaults.getConfigHash()};
    for (const char* body : {"EconomyFirstUnitPacing=1\n", "EconomyFirstUnitPacing=2\n", "EconomyFirstTurrets=true\n"}) {
        const auto path=std::filesystem::temp_directory_path()/(
            "quantbot-opening-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ini");
        {std::ofstream file(path);file<<"[Force Concentration]\n"<<body;}
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        for (const auto& hash:hashes) REQUIRE(loaded.getConfigHash()!=hash);
        hashes.push_back(loaded.getConfigHash());
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.force.economyFirstUnitPacing==loaded.force.economyFirstUnitPacing);
        REQUIRE(restored.force.economyFirstTurrets==loaded.force.economyFirstTurrets);
        REQUIRE(restored.getConfigHash()==loaded.getConfigHash());
        std::filesystem::remove(path);
    }
}
