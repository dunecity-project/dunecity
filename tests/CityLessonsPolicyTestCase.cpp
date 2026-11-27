// Lessons from Stefan's Metropolis save (CityLessonsPolicy.h): the Launcher army mix rule, the
// low-value lot rule, and the two switches' config keys (off by default, read and written).

#include <players/CityLessonsPolicy.h>
#include <players/QuantBotConfig.h>

#include <catch2/catch_all.hpp>

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

TEST_CASE("City lessons: both switches default off, round trip and hash only when on", "[ai][city-lessons][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.cityLauncherArmy);
    REQUIRE_FALSE(defaults.force.lowValuePalaces);
    REQUIRE(defaults.force.cityLauncherArmyBps == L::kLauncherArmyBps);
    std::vector<std::string> hashes{defaults.getConfigHash()};
    for (const char* body : {"CityLauncherArmy=true\n", "LowValuePalaces=true\n", "CityLauncherArmy=true\nCityLauncherArmyBps=6000\n"}) {
        const auto path = std::filesystem::temp_directory_path() / "quantbot-city-lessons.ini";
        {
            std::ofstream file(path);
            file << "[Force Concentration]\n" << body;
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        REQUIRE(int(loaded.force.cityLauncherArmy) + int(loaded.force.lowValuePalaces) == 1);
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
