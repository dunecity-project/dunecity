#include <catch2/catch_all.hpp>
#include <GameInitSettings.h>
#include <SpiceIncome.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <mod/ModManager.h>

#include <vector>

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
        const auto marker = bytes.rfind("7DOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = '5';
        // Everything MOD5 does not know about: the MOD6 rule byte and MOD7's count plus one
        // factor per house. Removed so what follows is unmistakably not part of the block.
        bytes.resize(bytes.size() - (1 + 4 + 4 * original.getHouseInfoList().size()));
        bytes.append("tail");
        IMemoryStream oldInput(bytes.data(), bytes.size());
        GameInitSettings old(oldInput);
        REQUIRE_FALSE(old.getGameOptions().originalUnitDamage);
        REQUIRE(oldInput.readUint8() == static_cast<Uint8>('t'));
    }
}

/// A two-row lobby with explicit factors. The first row's house can be left on Random, which
/// is exactly the case house-identity matching cannot serve.
static GameInitSettings makeSpiceLobby(Uint32 firstFactor, Uint32 secondFactor,
                                       bool firstIsRandom = false) {
    GameInitSettings init(HOUSE_ATREIDES, 8, SettingsClass::GameOptionsClass{});
    init.enableCoop(false, "Test spice");
    init.setScenarioData("[Atreides]\nBrain=Human\n[Sardaukar]\nBrain=CPU\n");
    GameInitSettings::HouseInfo first(firstIsRandom ? HOUSE_INVALID : HOUSE_ATREIDES, 1);
    first.graphicsSkin = GameInitSettings::GraphicsSkin::Dune2;
    first.spiceIncomeMultiplier = firstFactor;
    first.addPlayerInfo({"Host", HUMANPLAYERCLASS});
    first.addPlayerInfo({"Partner", HUMANPLAYERCLASS});
    init.addHouseInfo(first);
    GameInitSettings::HouseInfo second(HOUSE_SARDAUKAR, 2);
    second.spiceIncomeMultiplier = secondFactor;
    second.addPlayerInfo({"Sardaukar", "qBotHard"});
    init.addHouseInfo(second);
    return init;
}

TEST_CASE("Spice income factors survive the wire and default to 1x in every older format",
          "[spiceincome][network][save]") {
    // Row order, not house identity: these two rows are a shared house and an enemy, and a
    // custom lobby can leave either on Random.
    const auto host = makeSpiceLobby(4, 2);

    OMemoryStream out; out.open(); host.save(out);
    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings client(in);
    REQUIRE(client.getHouseInfoList().at(0).spiceIncomeMultiplier == 4);
    REQUIRE(client.getHouseInfoList().at(1).spiceIncomeMultiplier == 2);
    // The rest of the row is untouched by the new block.
    REQUIRE(client.getHouseInfoList().at(0).graphicsSkin == GameInitSettings::GraphicsSkin::Dune2);
    REQUIRE(client.getHouseInfoList().at(0).team == 1);
    // A snapshot handed to a joining peer keeps the factors it was built with.
    GameInitSettings next(client, 11, 0, 0);
    REQUIRE(next.getHouseInfoList().at(0).spiceIncomeMultiplier == 4);

    SECTION("a row that is still Random carries its own factor") {
        const auto random = makeSpiceLobby(5, 1, true);
        OMemoryStream randomOut; randomOut.open(); random.save(randomOut);
        IMemoryStream randomIn(randomOut.getData(), randomOut.getDataLength());
        GameInitSettings parsed(randomIn);
        REQUIRE(parsed.getHouseInfoList().at(0).houseID == HOUSE_INVALID);
        REQUIRE(parsed.getHouseInfoList().at(0).spiceIncomeMultiplier == 5);
    }

    SECTION("every marker before MOD7 means 1x and leaves the following bytes alone") {
        const char older = GENERATE('!', '2', '3', '4', '5', '6');
        std::string bytes(out.getData(), out.getDataLength());
        const auto marker = bytes.rfind("7DOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = older;
        // Strip what that older marker cannot account for, so the tail is unambiguous.
        std::size_t trailing = 4 + 4 * host.getHouseInfoList().size();   // MOD7
        if(older < '6') trailing += 1;                                   // MOD6 rule byte
        if(older < '5') trailing += 4;                                   // MOD5 yard limit
        bytes.resize(bytes.size() - trailing);
        bytes.append("tail");
        IMemoryStream oldInput(bytes.data(), bytes.size());
        if(older == '!' || older == '2' || older == '3') {
            // MOD1..MOD3 stop before the Workshop descriptors, so the strings that follow are
            // not theirs to read; this format is only reachable from a genuinely old stream.
            // What matters here is that nothing claims a factor.
            GameInitSettings old(oldInput);
            for(const auto& houseInfo : old.getHouseInfoList()) {
                REQUIRE(houseInfo.spiceIncomeMultiplier == SpiceIncome::kDefault);
            }
        } else {
            GameInitSettings old(oldInput);
            for(const auto& houseInfo : old.getHouseInfoList()) {
                REQUIRE(houseInfo.spiceIncomeMultiplier == SpiceIncome::kDefault);
            }
            REQUIRE(oldInput.readUint8() == static_cast<Uint8>('t'));
        }
    }

    SECTION("an announced MOD7 block that is not there is a truncated stream, not an old one") {
        std::string bytes(out.getData(), out.getDataLength());
        bytes.resize(bytes.size() - 4);   // lose the last factor
        IMemoryStream truncated(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(truncated); }());
    }

    SECTION("an explicitly encoded factor outside the range is refused, never clamped") {
        const Uint32 bad = GENERATE(0u, SpiceIncome::kMax + 1, 0xFFFFFFFFu);
        std::string bytes(out.getData(), out.getDataLength());
        REQUIRE(bytes.size() > 4);
        // The last four bytes are the final row's factor.
        for(int shift = 0; shift < 4; ++shift) {
            bytes[bytes.size() - 4 + shift] = static_cast<char>((bad >> (8 * shift)) & 0xFF);
        }
        IMemoryStream hostile(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(hostile); }());
    }

    SECTION("a factor count beyond the lobby's rows is refused before anything is read") {
        std::string bytes(out.getData(), out.getDataLength());
        const std::size_t countAt = bytes.size() - (4 + 4 * host.getHouseInfoList().size());
        // Shortened lists must not silently default the omitted rows; extra entries must not
        // be consumed as if they belonged to nonexistent rows either.
        const Uint32 absurd = GENERATE(0u, 1u, 3u, 9u, 0x7FFFFFFFu);
        for(int shift = 0; shift < 4; ++shift) {
            bytes[countAt + shift] = static_cast<char>((absurd >> (8 * shift)) & 0xFF);
        }
        IMemoryStream hostile(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(hostile); }());
    }

    SECTION("the writer refuses to persist a factor it would not accept back") {
        const auto broken = makeSpiceLobby(1, SpiceIncome::kMax + 1);
        OMemoryStream brokenOut; brokenOut.open();
        REQUIRE_THROWS(broken.save(brokenOut));
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

/**
    Writes the setup metadata of a savegame: the resolved rows, their colours and - from
    SAVEGAMEVERSION 9851 - their spice factors, followed by a sentinel so a test can prove the
    reader stopped where it should.
*/
static void writeSavedSetup(OMemoryStream& out, Uint32 version,
                            const GameInitSettings& init,
                            const GameInitSettings::HouseInfoList& resolved,
                            const std::vector<Uint32>& factors,
                            Uint32 setupMarker = SpiceIncome::kSetupMarker,
                            Uint32 announcedCount = 0xFFFFFFFFu) {
    out.open();
    out.writeUint32(SAVEMAGIC); out.writeUint32(version); out.writeString("test");
    out.writeString("vanilla"); out.writeString("checksum");
    init.save(out);
    out.writeUint32(resolved.size());
    for(const auto& house : resolved) house.save(out);
    out.writeUint32(0x53434F4C); out.writeUint32(resolved.size());
    for(const auto& house : resolved) out.writeSint32(house.houseID);
    if(version >= SpiceIncome::kFirstSavegameVersion) {
        out.writeUint32(setupMarker);
        out.writeUint32(announcedCount == 0xFFFFFFFFu
            ? static_cast<Uint32>(factors.size()) : announcedCount);
        for(const Uint32 factor : factors) out.writeUint32(factor);
    }
    out.writeUint32(0xabcdef01);
}

TEST_CASE("A saved lobby reads the resolved spice factor of a row that was played as Random",
          "[spiceincome][coop][save]") {
    // The match that was saved: the lobby row said Random, the engine bound it to Ordos, and
    // that row earned at 3x. The init data still says HOUSE_INVALID - which is exactly why
    // the factor cannot be recovered by matching houses, and why SMUL exists.
    const auto init = makeSpiceLobby(3, 2, true);
    GameInitSettings::HouseInfoList resolved;
    GameInitSettings::HouseInfo bound(HOUSE_ORDOS, 1);
    bound.addPlayerInfo({"Host", HUMANPLAYERCLASS});
    resolved.push_back(bound);
    GameInitSettings::HouseInfo enemy(HOUSE_SARDAUKAR, 2);
    enemy.addPlayerInfo({"Sardaukar", "qBotHard"});
    resolved.push_back(enemy);

    // 9850 is the format before the block existed. Its GameInitSettings is still written by
    // this build, so it does carry MOD7 - and the setup rows must *still* come out at 1x,
    // proving the reader never falls back to matching them against the init rows.
    const Uint32 version = GENERATE(9850u, SpiceIncome::kFirstSavegameVersion);
    OMemoryStream out;
    writeSavedSetup(out, version, init, resolved, {3, 2});

    IMemoryStream in(out.getData(), out.getDataLength());
    GameInitSettings::HouseInfoList houses;
    const auto parsed = GameInitSettings::readSaveSetup(in, houses);
    REQUIRE(houses.size() == 2);
    REQUIRE(houses.front().houseID == HOUSE_ORDOS);
    REQUIRE(parsed.getHouseInfoList().front().houseID == HOUSE_INVALID);
    if(version >= SpiceIncome::kFirstSavegameVersion) {
        REQUIRE(houses.front().spiceIncomeMultiplier == 3);
        REQUIRE(houses.back().spiceIncomeMultiplier == 2);
    } else {
        REQUIRE(houses.front().spiceIncomeMultiplier == SpiceIncome::kDefault);
        REQUIRE(houses.back().spiceIncomeMultiplier == SpiceIncome::kDefault);
    }
    REQUIRE(in.readUint32() == 0xabcdef01);
}

TEST_CASE("A saved lobby refuses malformed spice setup metadata", "[spiceincome][coop][save]") {
    const auto init = makeSpiceLobby(2, 2);
    GameInitSettings::HouseInfoList resolved;
    GameInitSettings::HouseInfo first(HOUSE_ATREIDES, 1);
    first.addPlayerInfo({"Host", HUMANPLAYERCLASS});
    resolved.push_back(first);
    GameInitSettings::HouseInfo second(HOUSE_SARDAUKAR, 2);
    second.addPlayerInfo({"Sardaukar", "qBotHard"});
    resolved.push_back(second);

    const auto refuses = [&](const OMemoryStream& bytes) {
        IMemoryStream in(bytes.getData(), bytes.getDataLength());
        GameInitSettings::HouseInfoList houses;
        REQUIRE_THROWS(GameInitSettings::readSaveSetup(in, houses));
    };

    SECTION("a missing marker") {
        OMemoryStream out;
        writeSavedSetup(out, SpiceIncome::kFirstSavegameVersion, init, resolved, {2, 2}, 0x4C554D53);
        refuses(out);
    }
    SECTION("a count that disagrees with the saved rows") {
        OMemoryStream out;
        writeSavedSetup(out, SpiceIncome::kFirstSavegameVersion, init, resolved, {2, 2},
                        SpiceIncome::kSetupMarker, 1);
        refuses(out);
    }
    SECTION("a factor outside the accepted range") {
        const Uint32 bad = GENERATE(0u, SpiceIncome::kMax + 1, 0xFFFFFFFFu);
        OMemoryStream out;
        writeSavedSetup(out, SpiceIncome::kFirstSavegameVersion, init, resolved, {2, bad});
        refuses(out);
    }
}

TEST_CASE("Hosting a saved co-op game keeps the rows it was handed", "[spiceincome][coop][save]") {
    // configureCoopSave takes the resolved rows as supplied, so whatever SMUL produced has to
    // reach the hosted lobby unchanged, and a planned future enemy keeps its own factor.
    const auto saved = makeSpiceLobby(4, 5);
    GameInitSettings::HouseInfoList actual{saved.getHouseInfoList().front()};
    OMemoryStream header; header.open();
    header.writeUint32(SAVEMAGIC); header.writeUint32(SAVEGAMEVERSION); header.writeString("test");
    const std::string bytes(reinterpret_cast<const char*>(header.getData()), header.getDataLength());
    GameInitSettings loaded("campaign.dls", bytes, "Host");
    loaded.configureCoopSave(saved, actual);
    REQUIRE(loaded.getHouseInfoList().size() == 2);
    REQUIRE(loaded.getHouseInfoList().front().spiceIncomeMultiplier == 4);
    REQUIRE(loaded.getHouseInfoList().back().spiceIncomeMultiplier == 5);
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
    const auto marker = bytes.find("7DOM");
    REQUIRE(marker != std::string::npos);
    SECTION("old graphics marker") {
        bytes[marker] = '3';
        // three strings, two revision integers, yard limit, original damage,
        // then MOD7's factor count and one factor per house
        bytes.resize(bytes.size() - 25 - (4 + 4 * original.getHouseInfoList().size()));
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
