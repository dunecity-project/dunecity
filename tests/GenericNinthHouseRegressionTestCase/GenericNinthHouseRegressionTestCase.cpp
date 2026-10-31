#include <catch2/catch_all.hpp>

#include <INIMap/MapPlayerSectionUtils.h>
#include <mod/CustomHouseConfig.h>
#include <mod/ModInfo.h>
#include <mod/ModMentatConfig.h>
#include <globals.h>
#include <SpecialVehicle.h>

#include <array>
#include <string>
#include <vector>

TEST_CASE("CustomHouse config: valid numeric fields parse without throwing",
          "[custom-house][config]") {
    int value = -1;
    REQUIRE(CustomHouseConfig::parseInteger("144", value));
    REQUIRE(value == 144);

    REQUIRE(CustomHouseConfig::parseInteger("0", value));
    REQUIRE(value == 0);
}

TEST_CASE("CustomHouse config: malformed numeric fields are rejected without mutation",
          "[custom-house][config][regression]") {
    const std::array<std::string, 5> malformedValues = {
        "",
        "not-a-number",
        "144trailing",
        "144 ",
        "999999999999999999999999999999"
    };

    for(const std::string& malformedValue : malformedValues) {
        CAPTURE(malformedValue);
        int destination = 73;
        REQUIRE_FALSE(CustomHouseConfig::parseInteger(malformedValue, destination));
        REQUIRE(destination == 73);
    }
}

TEST_CASE("PlayerN-only map scans the fixed available-house capacity",
          "[map][players][regression]") {
    constexpr int availableHouseCapacity = 8;
    int sectionsScanned = 0;

    const int numberedPlayerCount = MapPlayerSectionUtils::countNumberedPlayerSections(
        availableHouseCapacity,
        [&sectionsScanned](int playerNumber) {
            ++sectionsScanned;
            return playerNumber == 1 || playerNumber == 8;
        });

    REQUIRE(sectionsScanned == availableHouseCapacity);
    REQUIRE(numberedPlayerCount == 2);
    REQUIRE(0 + numberedPlayerCount == 2);
}

TEST_CASE("Mod Mentat numeric parsing is nonthrowing and rejects malformed values",
          "[mod][mentat][config]") {
    double frameRate = 0.5;
    REQUIRE(ModMentatConfig::parseDouble("5.25", frameRate));
    REQUIRE(frameRate == Catch::Approx(5.25));

    const std::array<std::string, 8> malformedValues = {
        "", "not-a-number", "5.0trailing", "nan", "1e9999", "NaN", "+inf", "-INFINITY"
    };
    for(const std::string& malformedValue : malformedValues) {
        CAPTURE(malformedValue);
        double destination = 3.0;
        REQUIRE_FALSE(ModMentatConfig::parseDouble(malformedValue, destination));
        REQUIRE(destination == Catch::Approx(3.0));
    }
}

TEST_CASE("Mod Mentat assets must use portable relative paths",
          "[mod][mentat][config][security]") {
    REQUIRE(ModMentatConfig::isSafeAssetPath("mentat/custom/background.png"));
    REQUIRE(ModMentatConfig::isSafeAssetPath("MentatEyes.png"));
    REQUIRE_FALSE(ModMentatConfig::isSafeAssetPath("../other-mod/asset.png"));
    REQUIRE_FALSE(ModMentatConfig::isSafeAssetPath("mentat/../asset.png"));
    REQUIRE_FALSE(ModMentatConfig::isSafeAssetPath("C:/local/asset.png"));
    REQUIRE_FALSE(ModMentatConfig::isSafeAssetPath("mentat\\asset.png"));
}

TEST_CASE("Invalid optional Mentat fields disable the override safely",
          "[mod][mentat][config][fallback]") {
    ModMentatInfo valid;
    valid.enabled = true;
    valid.identityHouse = 1;
    valid.backgroundAsset = "mentat/background.png";
    valid.foregroundAsset = "mentat/foreground.png";
    valid.eyesAsset = "mentat/eyes.png";
    valid.mouthAsset = "mentat/mouth.png";
    REQUIRE(ModMentatConfig::isValid(valid));

    ModMentatInfo invalidIdentity = valid;
    invalidIdentity.identityHouse = 8;
    REQUIRE_FALSE(ModMentatConfig::isValid(invalidIdentity));

    ModMentatInfo invalidFrames = valid;
    invalidFrames.eyesFrames = 0;
    REQUIRE_FALSE(ModMentatConfig::isValid(invalidFrames));

    ModMentatInfo invalidPath = valid;
    invalidPath.backgroundAsset = "../leaked.png";
    REQUIRE_FALSE(ModMentatConfig::isValid(invalidPath));

    ModMentatInfo invalidForegroundPath = valid;
    invalidForegroundPath.foregroundAsset = "../other-mod/foreground.png";
    REQUIRE_FALSE(ModMentatConfig::isValid(invalidForegroundPath));
}

TEST_CASE("Animal Mentat patches retain anchors and reject malformed layouts",
          "[mod][mentat][config][tornie]") {
    ModMentatInfo info;
    info.backgroundAsset = "MentatCat_Wildspade.png";
    info.eyesX = 134; info.eyesY = 137;
    info.mouthX = 175; info.mouthY = 200;
    info.eyesWidth = 98; info.eyesHeight = 32; info.eyesCropY = 232; info.eyesCropHeight = 234;
    info.mouthWidth = 40; info.mouthHeight = 23; info.mouthCropY = 298; info.mouthCropHeight = 190;
    info.restFromBackground = true;
    info.doubleEyes = info.doubleMouth = false;
    REQUIRE(ModMentatConfig::parsePatch(
        "0,0,43,29;60,314,156,112;43,314,156,112;27,314,156,112;26,314,156,112;27,314,156,112",
        info.eyesPatches));
    REQUIRE(ModMentatConfig::parsePolygon("248,268,256,279,265,294,248,400", info.foregroundPolygon));
    REQUIRE(ModMentatConfig::isValid(info));
    REQUIRE(info.eyesPatches.front().sources.size() == 5);
    REQUIRE(info.eyesPatches.front().destination.w == 43);

    SECTION("one source is required for every atlas cell") {
        info.eyesPatches.front().sources.pop_back();
        REQUIRE_FALSE(ModMentatConfig::isValid(info));
    }
    SECTION("patch anchors must remain within the feature canvas") {
        info.eyesPatches.front().destination.x = 98;
        REQUIRE_FALSE(ModMentatConfig::isValid(info));
    }
    SECTION("partial and negative rectangles do not append a patch") {
        const auto count = info.eyesPatches.size();
        REQUIRE_FALSE(ModMentatConfig::parsePatch("0,0,1,1;-1,0,1,1", info.eyesPatches));
        REQUIRE_FALSE(ModMentatConfig::parsePatch("0,0,1,1;0,0,1,1;", info.eyesPatches));
        REQUIRE(info.eyesPatches.size() == count);
    }
    SECTION("an invalid polygon does not replace the silhouette") {
        const auto count = info.foregroundPolygon.size();
        REQUIRE_FALSE(ModMentatConfig::parsePolygon("1,2,3,4,5", info.foregroundPolygon));
        REQUIRE(info.foregroundPolygon.size() == count);
    }
}

TEST_CASE("Custom-house presentation numbers parse safely and remain bounded",
          "[custom-house][presentation][config]") {
    double value = 1.0;
    REQUIRE(CustomHouseConfig::parseDouble("1.15", value));
    REQUIRE(value == Catch::Approx(1.15));
    REQUIRE(CustomHouseConfig::isValidVoicePlaybackRate(1.06));
    REQUIRE(CustomHouseConfig::isValidVoiceGain(1.15));
    REQUIRE_FALSE(CustomHouseConfig::isValidVoicePlaybackRate(0.0));
    REQUIRE_FALSE(CustomHouseConfig::isValidVoiceGain(5.0));

    const std::array<std::string, 8> malformedValues = {
        "", "not-a-number", "1.0trailing", "nan", "1e9999", "NaN", "+inf", "-INFINITY"
    };
    for(const std::string& malformedValue : malformedValues) {
        CAPTURE(malformedValue);
        double destination = 1.0;
        REQUIRE_FALSE(CustomHouseConfig::parseDouble(malformedValue, destination));
        REQUIRE(destination == Catch::Approx(1.0));
    }
}

TEST_CASE("Custom-house presentation assets use portable mod-relative paths",
          "[custom-house][presentation][config][security]") {
    REQUIRE(CustomHouseConfig::isSafeAssetPath("presentation/herald.png"));
    REQUIRE(CustomHouseConfig::isSafeAssetPath("HouseName.VOC"));
    REQUIRE(CustomHouseConfig::isSafeAssetPath(""));
    REQUIRE_FALSE(CustomHouseConfig::isSafeAssetPath("../other-mod/herald.png"));
    REQUIRE_FALSE(CustomHouseConfig::isSafeAssetPath("presentation/../herald.png"));
    REQUIRE_FALSE(CustomHouseConfig::isSafeAssetPath("C:/local/voice.voc"));
    REQUIRE_FALSE(CustomHouseConfig::isSafeAssetPath("presentation\\voice.voc"));
}

TEST_CASE("Custom-house presentation defaults request safe fallbacks",
          "[custom-house][presentation][fallback]") {
    const CustomHouseInfo info;
    REQUIRE(info.heraldAsset.empty());
    REQUIRE(info.houseNameVoiceAsset.empty());
    REQUIRE(info.voicePlaybackRate == Catch::Approx(1.0));
    REQUIRE(info.voiceGain == Catch::Approx(1.0));
}

TEST_CASE("Tornie custom house prefers its ObjectData IX vehicles",
          "[custom-house][special-vehicle][tornie]") {
    std::vector<HouseSpecialVehicleCandidateData> objectData(ItemID_LastID + 1);
    const HouseSpecialVehicleCandidateData enabledIxVehicle{
        true,
        Structure_HeavyFactory,
        true
    };

    objectData[Unit_Deviator] = enabledIxVehicle;
    objectData[Unit_EliteLauncher] = enabledIxVehicle;
    objectData[Unit_Ornithopter] = enabledIxVehicle;
    objectData[Unit_Devastator] = { true, ItemID_Invalid, true };
    objectData[Unit_Trooper] = enabledIxVehicle;
    objectData[Unit_Harvester] = enabledIxVehicle;
    objectData[Unit_FlameTank] = { true, Structure_HeavyFactory, false };

    const auto tornieIxCandidates = discoverHouseSpecialVehicleCandidates(
        [&](int itemID) { return objectData[itemID]; });
    const std::vector<int> expectedCorruptiqueCandidates = {
        Unit_Devastator,
        Unit_EliteSiegeTank
    };

    REQUIRE(resolveSpecialVehiclePoolForHouse(
                HOUSE_CUSTOM, true, tornieIxCandidates)
            == expectedCorruptiqueCandidates);
    REQUIRE_FALSE(isHouseSpecialVehicleCandidate(
        Unit_Ornithopter, objectData[Unit_Ornithopter]));
    REQUIRE_FALSE(isHouseSpecialVehicleCandidate(
        Unit_Devastator, objectData[Unit_Devastator]));
    REQUIRE_FALSE(isHouseSpecialVehicleCandidate(
        Unit_Trooper, objectData[Unit_Trooper]));
    REQUIRE_FALSE(isHouseSpecialVehicleCandidate(
        Unit_Harvester, objectData[Unit_Harvester]));
}

TEST_CASE("House fallback follows the active faction plan when ObjectData has no candidates",
          "[custom-house][special-vehicle][fallback]") {
    const std::vector<int> noModOwnedCandidates;
    const std::vector<int> genericFallback = {
        Unit_SonicTank,
        Unit_Devastator
    };
    const std::vector<int> expectedTornieFremen = {
        Unit_EliteSiegeTank,
        Unit_FlameTank
    };

    REQUIRE(resolveSpecialVehiclePoolForHouse(
                HOUSE_FREMEN, false, noModOwnedCandidates)
            == genericFallback);
    REQUIRE(resolveSpecialVehiclePoolForHouse(
                HOUSE_FREMEN, true, noModOwnedCandidates)
            == expectedTornieFremen);
}
