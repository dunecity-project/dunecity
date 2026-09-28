#include <catch2/catch_all.hpp>
#include <GameInitSettings.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <mod/ModManager.h>

// GameInitSettings only needs mod identity; these tests never install mods.
ModManager::ModManager() : checksumsDirty(false), initialized(false) {}
ModManager::~ModManager() = default;
ModManager& ModManager::instance() { static ModManager manager; return manager; }
bool ModManager::isInitialized() const { return false; }
std::string ModManager::getActiveModName() const { return "vanilla"; }
ModChecksums ModManager::getEffectiveChecksums() const { return {}; }
bool isHouseAvailable(HOUSETYPE house) { return house >= 0 && house < NUM_HOUSES; }
char getHouseScenarioLetter(HOUSETYPE house) {
    return house == HOUSE_ATREIDES ? 'A' : (house == HOUSE_ORDOS ? 'O' : 'H');
}

static GameInitSettings makeCoop(bool campaign, bool bot) {
    GameInitSettings init(HOUSE_ATREIDES, 8, SettingsClass::GameOptionsClass{});
    init.enableCoop(campaign, "Test co-op");
    init.setCampaignGraphicsSkin(GameInitSettings::GraphicsSkin::Dune2);
    init.setScenarioData("[Atreides]\nBrain=Human\n[Sardaukar]\nBrain=CPU\n");
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.graphicsSkin = GameInitSettings::GraphicsSkin::Dune2;
    human.addPlayerInfo({"Host", HUMANPLAYERCLASS});
    human.addPlayerInfo({"Partner", bot ? "qBotSupportBrutal" : HUMANPLAYERCLASS});
    init.addHouseInfo(human);
    GameInitSettings::HouseInfo enemy(HOUSE_SARDAUKAR, 2);
    enemy.addPlayerInfo({"Sardaukar", "qBotHard"});
    init.addHouseInfo(enemy);
    return init;
}

TEST_CASE("Original damage is a shared rule preserved by setup, saves and campaigns", "[damage][network][save]") {
    SettingsClass::GameOptionsClass balanced;
    REQUIRE_FALSE(balanced.originalUnitDamage);
    auto classic = balanced;
    classic.originalUnitDamage = true;
    REQUIRE(classic != balanced);
    REQUIRE(classic.getHash() != balanced.getHash());

    auto original = makeCoop(true, false);
    original.setGameOptions(classic);
    OMemoryStream out; out.open(); original.save(out);
    IMemoryStream input(out.getData(), out.getDataLength());
    GameInitSettings restored(input);
    REQUIRE(restored.getGameOptions().originalUnitDamage);
    REQUIRE(restored.getGameOptions().getHash() == classic.getHash());
    REQUIRE(restored.networkSnapshot("saved simulation").getGameOptions().originalUnitDamage);
    GameInitSettings next(restored, 11, 0, 0);
    REQUIRE(next.getGameOptions().originalUnitDamage);

    SECTION("MOD5 defaults the new rule off without consuming following save bytes") {
        std::string bytes(out.getData(), out.getDataLength());
        const auto marker = bytes.rfind("6DOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = '5';
        bytes.pop_back();
        bytes.append("tail");
        IMemoryStream oldInput(bytes.data(), bytes.size());
        GameInitSettings old(oldInput);
        REQUIRE_FALSE(old.getGameOptions().originalUnitDamage);
        REQUIRE(oldInput.readUint8() == static_cast<Uint8>('t'));
    }
}

TEST_CASE("Co-op settings preserve shared control, scenario identity and seed over the wire", "[coop][network]") {
    const bool campaign = GENERATE(false, true);
    const bool bot = GENERATE(false, true);
    const auto host = makeCoop(campaign, bot);
    OMemoryStream out; out.open(); host.save(out);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings client(in);
    REQUIRE(client.getGameType() == host.getGameType());
    REQUIRE(isNetworkGameType(client.getGameType()));
    REQUIRE(client.isMultiplePlayersPerHouse());
    REQUIRE(client.getHouseID() == HOUSE_ATREIDES);
    REQUIRE(client.getMission() == 8);
    REQUIRE(client.getRandomSeed() == host.getRandomSeed());
    REQUIRE(client.getFiledata() == host.getFiledata());
    REQUIRE(client.getHouseInfoList().size() == 2);
    const auto& shared = client.getHouseInfoList().front();
    REQUIRE(shared.team == 1);
    REQUIRE(shared.graphicsSkin == GameInitSettings::GraphicsSkin::Dune2);
    REQUIRE(client.getHouseInfoList().back().graphicsSkin == GameInitSettings::GraphicsSkin::SimCity);
    REQUIRE(client.getCampaignGraphicsSkin() == GameInitSettings::GraphicsSkin::Dune2);
    REQUIRE(shared.playerInfoList.size() == 2);
    REQUIRE(shared.playerInfoList.back().playerClass == (bot ? "qBotSupportBrutal" : HUMANPLAYERCLASS));
    REQUIRE(client.getHouseInfoList().back().houseID == HOUSE_SARDAUKAR);
    REQUIRE(client.getHouseInfoList().back().team == 2);
}

TEST_CASE("Next co-op mission retains controllers and progress but discards old scenario bytes", "[coop][campaign]") {
    const bool bot = GENERATE(false, true);
    const auto previous = makeCoop(true, bot);
    GameInitSettings next(previous, 11, 0x123, 0x42);
    REQUIRE(next.getGameType() == GameType::CampaignCoop);
    REQUIRE(next.getCampaignGraphicsSkin() == GameInitSettings::GraphicsSkin::Dune2);
    REQUIRE(next.getFilename() == "SCENA011.INI");
    REQUIRE(next.getFiledata().empty());
    REQUIRE(next.getAlreadyPlayedRegions() == 0x123);
    REQUIRE(next.getAlreadyShownTutorialHints() == 0x42);
    REQUIRE(next.getHouseInfoList().front().playerInfoList.size() == 2);
    REQUIRE(next.getHouseInfoList().front().playerInfoList.back().playerClass
        == previous.getHouseInfoList().front().playerInfoList.back().playerClass);
    REQUIRE(next.getHouseInfoList().front().playerInfoList.back().playerName
        == previous.getHouseInfoList().front().playerInfoList.back().playerName);
}

TEST_CASE("Campaign save lobby reads mod header and setup colors without consuming game state", "[coop][save]") {
    const unsigned version = GENERATE(9806u, 9814u, 9836u);
    GameInitSettings saved(HOUSE_ATREIDES, SettingsClass::GameOptionsClass{});
    const auto setup = makeCoop(true, false).getHouseInfoList();
    for(const auto& house : setup) saved.addHouseInfo(house);
    OMemoryStream out; out.open();
    out.writeUint32(SAVEMAGIC); out.writeUint32(version); out.writeString("test");
    out.writeString("vanilla"); out.writeString("checksum");
    saved.save(out);
    out.writeUint32(setup.size());
    for(const auto& house : setup) house.save(out);
    if(version >= 9814) {
        out.writeUint32(0x53434F4C); out.writeUint32(setup.size());
        for(const auto& house : setup) out.writeSint32(house.houseID);
    }
    out.writeUint32(0xabcdef01);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings::HouseInfoList houses;
    const auto parsed = GameInitSettings::readSaveSetup(in, houses);
    REQUIRE(parsed.getGameType() == GameType::Campaign);
    REQUIRE(parsed.getHouseID() == HOUSE_ATREIDES);
    REQUIRE(houses.size() == 2);
    REQUIRE(houses.front().graphicsSkin == GameInitSettings::GraphicsSkin::Dune2);
    REQUIRE(houses.back().graphicsSkin == GameInitSettings::GraphicsSkin::SimCity);
    REQUIRE(houses.back().houseID == HOUSE_SARDAUKAR);
    REQUIRE(in.readUint32() == 0xabcdef01);
}

TEST_CASE("Malformed co-op save header is rejected", "[coop][save]") {
    OMemoryStream out; out.open(); out.writeUint32(0);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings::HouseInfoList houses;
    REQUIRE_THROWS(GameInitSettings::readSaveSetup(in, houses));
}

TEST_CASE("Hosting an existing campaign preserves progress and missing future enemy slots", "[coop][save]") {
    auto saved = makeCoop(true, false);
    GameInitSettings::HouseInfoList actual{saved.getHouseInfoList().front()};
    OMemoryStream header; header.open();
    header.writeUint32(SAVEMAGIC); header.writeUint32(SAVEGAMEVERSION); header.writeString("test");
    const std::string bytes(reinterpret_cast<const char*>(header.getData()), header.getDataLength());
    GameInitSettings loaded("campaign.dls", bytes, "Host");
    loaded.configureCoopSave(saved, actual);
    loaded.enableCoop(true, "Hosted campaign");
    REQUIRE(loaded.getGameType() == GameType::LoadCoop);
    REQUIRE(loaded.getCampaignGraphicsSkin() == GameInitSettings::GraphicsSkin::Dune2);
    REQUIRE(loaded.getHouseInfoList().front().graphicsSkin == GameInitSettings::GraphicsSkin::Dune2);
    REQUIRE(loaded.getHouseID() == HOUSE_ATREIDES);
    REQUIRE(loaded.getMission() == saved.getMission());
    REQUIRE(loaded.getFiledata() == bytes);
    REQUIRE(loaded.getHouseInfoList().size() == 2);
    REQUIRE(loaded.getHouseInfoList().back().houseID == HOUSE_SARDAUKAR);
    REQUIRE_FALSE(loaded.getGameOptions().immortalHumanPlayer);
}

TEST_CASE("Campaign start levels choose the first scenario and retain campaign progression", "[campaign][save]") {
    const int level = GENERATE(1, 2, 3, 4, 5, 6, 7, 8, 9);
    const int first[] = {1, 2, 5, 8, 11, 14, 17, 20, 22};
    GameInitSettings init(HOUSE_ATREIDES, SettingsClass::GameOptionsClass{}, level);
    REQUIRE(init.getGameType() == GameType::Campaign);
    REQUIRE(init.getMission() == first[level - 1]);
    const std::string filename = "SCENA0" + std::string(first[level - 1] < 10 ? "0" : "") + std::to_string(first[level - 1]) + ".INI";
    REQUIRE(init.getFilename() == filename);
    REQUIRE(init.getModName() == "vanilla");
    OMemoryStream out; out.open(); init.save(out);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings loaded(in);
    REQUIRE(loaded.getGameType() == GameType::Campaign);
    REQUIRE(loaded.getMission() == first[level - 1]);
    GameInitSettings next(loaded, level < 9 ? first[level] : 22, 0x42, 0x10);
    REQUIRE(next.getGameType() == GameType::Campaign);
    REQUIRE(next.getModName() == init.getModName());
    REQUIRE(next.getAlreadyPlayedRegions() == 0x42);
}

TEST_CASE("Invalid campaign start levels fail before loading a scenario", "[campaign]") {
    const int level = GENERATE(-1, 0, 10, 22);
    REQUIRE_THROWS_AS(GameInitSettings(HOUSE_ATREIDES, SettingsClass::GameOptionsClass{}, level), std::invalid_argument);
}

TEST_CASE("Workshop revisions survive game settings and checkpoint copies", "[workshop][network][save]") {
    auto original = makeCoop(true, false);
    original.setModRevision(std::string(64, 'a'), 7);
    original.setMapRevision(std::string(64, 'b'), 12, "map manifest");
    OMemoryStream out; out.open(); original.save(out);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings restored(in);
    REQUIRE(restored.getModRevisionHash() == original.getModRevisionHash());
    REQUIRE(restored.getModRevisionVersion() == 7);
    REQUIRE(restored.getMapRevisionHash() == original.getMapRevisionHash());
    REQUIRE(restored.getMapRevisionVersion() == 12);
    REQUIRE(restored.getMapRevisionManifest() == "map manifest");
    const auto checkpoint = restored.networkSnapshot("saved simulation bytes");
    REQUIRE(checkpoint.getGameType() == GameType::LoadMultiplayer);
    REQUIRE(checkpoint.getModRevisionHash() == original.getModRevisionHash());
    GameInitSettings next(original, 11, 0, 0);
    REQUIRE(next.getModRevisionHash() == original.getModRevisionHash());
}

TEST_CASE("Legacy MOD3 remains readable and truncated MOD6 fails closed", "[workshop][save]") {
    auto original = makeCoop(false, true);
    OMemoryStream out; out.open(); original.save(out);
    std::string bytes(reinterpret_cast<const char*>(out.getData()), out.getDataLength());
    const auto marker = bytes.find("6DOM");
    REQUIRE(marker != std::string::npos);
    SECTION("old graphics marker") {
        bytes[marker] = '3';
        bytes.resize(bytes.size() - 25); // three strings, two revision integers, yard limit, original damage
        IMemoryStream in(bytes.data(), bytes.size());
        GameInitSettings restored(in);
        REQUIRE(restored.getModRevisionHash().empty());
        REQUIRE(restored.getCampaignGraphicsSkin() == original.getCampaignGraphicsSkin());
    }
    SECTION("new descriptor truncated") {
        bytes.pop_back();
        IMemoryStream in(bytes.data(), bytes.size());
        REQUIRE_THROWS(GameInitSettings(in));
    }
    SECTION("invalid revision hash") {
        original.setModRevision("../../untrusted", 1);
        OMemoryStream invalid; invalid.open(); original.save(invalid);
        IMemoryStream in(invalid.getData(), invalid.getDataLength());
        REQUIRE_THROWS(GameInitSettings(in));
    }
}
