#include <catch2/catch_all.hpp>

#include <GameInitSettings.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>

#include <string>

namespace {
constexpr const char* kCivWarsMap = "[BASIC]\nName=Lane fixture\n[CIVILIZATIONWARS]\nVersion=1\n";

std::string saveSettings(const GameInitSettings& settings) {
    OMemoryStream output;
    output.open();
    settings.save(output);
    return std::string(output.getData(), output.getDataLength());
}
}

TEST_CASE("Civ Wars lane respawn requires its complete generated-map version marker",
          "[civwars][sandworm][map]") {
    const std::string mod = GENERATE("vanilla", "dunecity", "CivWars");
    GameInitSettings generated("ordinary-name.ini", kCivWarsMap, false, {});
    generated.setModIdentity(mod, "");
    REQUIRE(generated.isCivilizationWars());
    REQUIRE(generated.isCivilizationWars());

    GameInitSettings multiplayer("ordinary-name.ini", kCivWarsMap, "Lane fixture", false, {});
    multiplayer.setModIdentity(mod, "");
    REQUIRE(multiplayer.isCivilizationWars());

    SECTION("unmarked scenarios keep the ordinary policy even under the CivWars mod") {
        const std::string data = GENERATE(
            "",
            "[BASIC]\nName=Civilization Wars\nVersion=1\n",
            "; [CIVILIZATIONWARS]\n; Version=1\n",
            "[OTHER_CIVILIZATIONWARS]\nVersion=1\n",
            "[CIVILIZATIONWARS_EXAMPLE]\nVersion=1\n",
            "[CIVILIZATIONWARS]\n",
            "[CIVILIZATIONWARS]\nVersion=0\n",
            "[CIVILIZATIONWARS]\nVersion=2\n",
            "[CIVILIZATIONWARS]\nVersion=11\n",
            "[CIVILIZATIONWARS]\nVersion=1suffix\n",
            "[CIVILIZATIONWARS]\nVersion=1.0\n",
            "[CIVILIZATIONWARS]\nVersion=01\n",
            "[CIVILIZATIONWARS]\nVersion=+1\n",
            "[CIVILIZATIONWARS]\nOtherVersion=1\n");
        GameInitSettings ordinary("Civilization Wars.ini", data, false, {});
        ordinary.setModIdentity(mod, "");
        REQUIRE_FALSE(ordinary.isCivilizationWars());
        REQUIRE_FALSE(ordinary.isCivilizationWars());
    }

    SECTION("the normal INI whitespace and comment syntax remain supported") {
        generated.setMapData("; generated lanes\n[CIVILIZATIONWARS]\nVersion = 1 ; format version\n");
        REQUIRE(generated.isCivilizationWars());
    }
}

TEST_CASE("Civ Wars mode caching follows scenario replacement without adding serialized state",
          "[civwars][sandworm][save]") {
    GameInitSettings generated("lanes.ini", kCivWarsMap, false, {});
    const auto coldBytes = saveSettings(generated);
    REQUIRE(generated.isCivilizationWars());
    REQUIRE(saveSettings(generated) == coldBytes);
    const auto originalHash = generated.getGameOptions().getHash();

    auto copied = generated;
    generated.setScenarioData("[BASIC]\nName=Own scenario\n");
    REQUIRE_FALSE(generated.isCivilizationWars());
    REQUIRE(copied.isCivilizationWars());
    generated.setMapData(kCivWarsMap);
    REQUIRE(generated.isCivilizationWars());
    REQUIRE(generated.getGameOptions().getHash() == originalHash);

    IMemoryStream input(coldBytes.data(), coldBytes.size());
    GameInitSettings restored(input);
    REQUIRE(restored.isCivilizationWars());
    REQUIRE(restored.getFiledata() == kCivWarsMap);
    REQUIRE(restored.getGameOptions().getHash() == originalHash);
    REQUIRE(input.getRemainingLength() == 0);

    SECTION("an old negative cache cannot survive map replacement") {
        copied.setMapData("[CIVILIZATIONWARS]\nVersion=2\n");
        REQUIRE_FALSE(copied.isCivilizationWars());
        copied.setScenarioData(kCivWarsMap);
        REQUIRE(copied.isCivilizationWars());
    }
}

TEST_CASE("Civ Wars markers are interpreted only in executable custom-map settings",
          "[civwars][sandworm][network]") {
    GameInitSettings generated("lanes.ini", kCivWarsMap, false, {});
    REQUIRE(generated.isCivilizationWars());
    // A checkpoint may contain the marker inside its saved map string, but the
    // outer LoadMultiplayer payload is binary and is not itself that map.
    const std::string binary = std::string("binary\0checkpoint\n", 18) + kCivWarsMap;
    auto checkpoint = generated.networkSnapshot(binary);
    REQUIRE(checkpoint.getGameType() == GameType::LoadMultiplayer);
    REQUIRE_FALSE(checkpoint.isCivilizationWars());

    GameInitSettings campaign(HOUSE_ATREIDES, SettingsClass::GameOptionsClass{});
    campaign.setScenarioData(kCivWarsMap);
    REQUIRE_FALSE(campaign.isCivilizationWars());
    GameInitSettings skirmish(HOUSE_ATREIDES, 1, SettingsClass::GameOptionsClass{});
    skirmish.setScenarioData(kCivWarsMap);
    REQUIRE_FALSE(skirmish.isCivilizationWars());
}
