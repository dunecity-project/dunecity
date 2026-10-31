#include <catch2/catch_all.hpp>
#include <GameInitSettings.h>
#include <SpiceIncome.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <mod/ModManager.h>
#include <Network/GameInitSettingsPolicy.h>

#include <vector>
#include <sstream>

namespace {
std::string activeTestMod = "vanilla";
constexpr std::size_t Mod12Bytes = 6;
struct ActiveTestMod {
    explicit ActiveTestMod(const std::string& name) { ModManager::instance().setActiveMod(name); }
    ~ActiveTestMod() { ModManager::instance().setActiveMod("vanilla"); }
};
}

// Standalone tests use mod identity and path naming without installing mods.
ModManager::ModManager() : checksumsDirty(false), initialized(false) {}
ModManager::~ModManager() = default;
ModManager& ModManager::instance() { static ModManager manager; return manager; }
bool ModManager::isInitialized() const { return activeTestMod != "vanilla"; }
std::string ModManager::getActiveModName() const { return activeTestMod; }
std::string ModManager::getModPath(const std::string& name) const {
    return name.empty() ? modsBasePath : modsBasePath + "/" + name;
}
std::string ModManager::getContentBase(const std::string& name) const {
    return name == "ws-test-tornie" ? "Tornie" : name;
}
bool ModManager::isTornieFamilyContent(const std::string& name) const {
    const auto base = getContentBase(name);
    return base == "Tornie" || base == "TornieLite" || base == "Jericho"
        || base == "JerichoLite" || base == "ArcaKev" || base == "ArcaKevLite";
}
bool ModManager::setActiveMod(const std::string& name) {
    activeTestMod = name;
    activeTornieFamily = isTornieFamilyContent(name);
    return true;
}
ModChecksums ModManager::getEffectiveChecksums() const { return {}; }
bool isHouseAvailable(HOUSETYPE house) { return house >= 0 && house < NUM_HOUSES; }
char getHouseScenarioLetter(HOUSETYPE house) {
    return house == HOUSE_ATREIDES ? 'A' : (house == HOUSE_ORDOS ? 'O' : 'H');
}

static GameInitSettings makeCoop(bool campaign, bool bot, const std::string& botClass = "qBotSupportBrutal") {
    GameInitSettings init(HOUSE_ATREIDES, 8, SettingsClass::GameOptionsClass{});
    init.enableCoop(campaign, "Test co-op");
    init.setCampaignGraphicsSkin(GameInitSettings::GraphicsSkin::Dune2);
    init.setScenarioData("[Atreides]\nBrain=Human\n[Sardaukar]\nBrain=CPU\n");
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.graphicsSkin = GameInitSettings::GraphicsSkin::Dune2;
    human.addPlayerInfo({"Host", HUMANPLAYERCLASS});
    human.addPlayerInfo({"Partner", bot ? botClass : std::string(HUMANPLAYERCLASS)});
    init.addHouseInfo(human);
    GameInitSettings::HouseInfo enemy(HOUSE_SARDAUKAR, 2);
    enemy.addPlayerInfo({"Sardaukar", "qBotHard"});
    init.addHouseInfo(enemy);
    return init;
}

TEST_CASE("Original damage is a shared rule preserved by setup, saves and campaigns", "[damage][network][save]") {
    SettingsClass::GameOptionsClass balanced;
    balanced.originalUnitDamage = false;
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

    SECTION("MOD5 defaults the new rule off without consuming following save bytes") {
        std::string bytes(out.getData(), out.getDataLength());
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = '5';
        // Everything MOD5 does not know about: the MOD6 rule byte, MOD7's count plus one
        // factor per house, MOD8's plan-sites byte, MOD9's ground-steps byte, MOD10's two
        // planned-construction bytes, MOD11's city-year byte and MOD12's six bytes. Removed so what follows is
        // unmistakably not part of the block.
        bytes.resize(bytes.size() - (Mod12Bytes + 1 + 4 + 4 * original.getHouseInfoList().size() + 1 + 1 + 2 + 1));
        bytes.append("tail");
        IMemoryStream oldInput(bytes.data(), bytes.size());
        GameInitSettings old(oldInput);
        REQUIRE_FALSE(old.getGameOptions().originalUnitDamage);
        REQUIRE(oldInput.readUint8() == static_cast<Uint8>('t'));
    }
}

TEST_CASE("Plan building sites participates in match identity and save compatibility",
          "[preplace][network][save]") {
    SettingsClass::GameOptionsClass planned;
    planned.planBuildingSites = true;
    auto buildFirst = planned;
    buildFirst.planBuildingSites = false;
    REQUIRE(buildFirst != planned);
    REQUIRE(buildFirst.getHash() != planned.getHash());

    auto original = makeCoop(true, false);
    original.setGameOptions(buildFirst);
    OMemoryStream out; out.open(); original.save(out);
    IMemoryStream input(out.getData(), out.getDataLength());
    GameInitSettings restored(input);
    REQUIRE_FALSE(restored.getGameOptions().planBuildingSites);
    REQUIRE(restored.getGameOptions().getHash() == buildFirst.getHash());

    SECTION("MOD7 and older default the rule on without consuming following bytes") {
        const char older = GENERATE('4', '5', '6', '7');
        std::string bytes(out.getData(), out.getDataLength());
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = older;
        std::size_t trailing = Mod12Bytes + 1 + 2 + 1 + 1;                   // MOD12 through MOD8
        if(older < '7') trailing += 4 + 4 * original.getHouseInfoList().size(); // MOD7
        if(older < '6') trailing += 1;                                       // MOD6 rule byte
        if(older < '5') trailing += 4;                                       // MOD5 yard limit
        bytes.resize(bytes.size() - trailing);
        bytes.append("tail");
        IMemoryStream oldInput(bytes.data(), bytes.size());
        GameInitSettings old(oldInput);
        REQUIRE(old.getGameOptions().planBuildingSites);
        REQUIRE(oldInput.readUint8() == static_cast<Uint8>('t'));
    }

    SECTION("an announced MOD8 rule byte that is not there is a truncated stream") {
        std::string bytes(out.getData(), out.getDataLength());
        bytes.resize(bytes.size() - Mod12Bytes - 5);   // MOD12 through the missing MOD8 byte
        IMemoryStream truncated(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(truncated); }());
    }
}

TEST_CASE("Continuous ground steps participates in match identity and save compatibility",
          "[movement][network][save]") {
    SettingsClass::GameOptionsClass legacy;
    legacy.continuousGroundSteps = false;
    auto continuous = legacy;
    continuous.continuousGroundSteps = true;
    REQUIRE(continuous != legacy);
    REQUIRE(continuous.getHash() != legacy.getHash());

    auto original = makeCoop(true, false);
    original.setGameOptions(continuous);
    OMemoryStream out; out.open(); original.save(out);
    IMemoryStream input(out.getData(), out.getDataLength());
    GameInitSettings restored(input);
    REQUIRE(restored.getGameOptions().continuousGroundSteps);
    REQUIRE(restored.getGameOptions().getHash() == continuous.getHash());

    SECTION("MOD8 streams keep the rule off without consuming following bytes") {
        std::string bytes(out.getData(), out.getDataLength());
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = '8';
        bytes.resize(bytes.size() - Mod12Bytes - 4);   // MOD12 through MOD9, which MOD8 does not own
        bytes.append("tail");
        IMemoryStream oldInput(bytes.data(), bytes.size());
        GameInitSettings old(oldInput);
        REQUIRE_FALSE(old.getGameOptions().continuousGroundSteps);
        REQUIRE(oldInput.readUint8() == static_cast<Uint8>('t'));
    }

    SECTION("an announced MOD9 rule byte that is not there is a truncated stream") {
        std::string bytes(out.getData(), out.getDataLength());
        bytes.resize(bytes.size() - Mod12Bytes - 4);   // MOD12 through the missing MOD9 byte
        IMemoryStream truncated(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(truncated); }());
    }
}

TEST_CASE("The planned-construction rules participate in match identity and save compatibility",
          "[preplace][network][save]") {
    SettingsClass::GameOptionsClass current;
    REQUIRE(current.retryPlannedPlacement);
    REQUIRE(current.sharedYardQueue);
    auto noRetry = current;
    noRetry.retryPlannedPlacement = false;
    REQUIRE(noRetry.getHash() != current.getHash());
    auto ownQueues = current;
    ownQueues.sharedYardQueue = false;
    REQUIRE(ownQueues.getHash() != current.getHash());
    REQUIRE(ownQueues != current);

    auto original = makeCoop(true, false);
    original.setGameOptions(current);
    OMemoryStream out; out.open(); original.save(out);

    SECTION("both rules round-trip independently") {
        auto mixed = makeCoop(true, false);
        auto options = current;
        options.retryPlannedPlacement = false;
        mixed.setGameOptions(options);
        OMemoryStream mixedOut; mixedOut.open(); mixed.save(mixedOut);
        IMemoryStream in(mixedOut.getData(), mixedOut.getDataLength());
        GameInitSettings restored(in);
        REQUIRE_FALSE(restored.getGameOptions().retryPlannedPlacement);
        REQUIRE(restored.getGameOptions().sharedYardQueue);
    }

    SECTION("MOD9 and older streams read both rules off without consuming following bytes") {
        // A save or replay recorded before the rules existed must replay with the legacy behaviour.
        const char older = GENERATE('8', '9');
        std::string bytes(out.getData(), out.getDataLength());
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = older;
        bytes.resize(bytes.size() - Mod12Bytes - (older == '8' ? 4 : 3));
        bytes.append("tail");
        IMemoryStream oldInput(bytes.data(), bytes.size());
        GameInitSettings old(oldInput);
        REQUIRE_FALSE(old.getGameOptions().retryPlannedPlacement);
        REQUIRE_FALSE(old.getGameOptions().sharedYardQueue);
        REQUIRE(oldInput.readUint8() == static_cast<Uint8>('t'));
    }

    SECTION("an announced MOD10 rule byte that is not there is a truncated stream") {
        std::string bytes(out.getData(), out.getDataLength());
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = 'A';
        bytes.resize(bytes.size() - Mod12Bytes - 2);   // MOD12, MOD11 and the missing last MOD10 byte
        IMemoryStream truncated(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(truncated); }());
    }
}

TEST_CASE("Slow City Year participates in match identity and save compatibility",
          "[citybudget][network][save]") {
    SettingsClass::GameOptionsClass current;
    REQUIRE(current.slowCityYear);
    auto classic = current;
    classic.slowCityYear = false;
    REQUIRE(classic != current);
    REQUIRE(classic.getHash() != current.getHash());

    auto original = makeCoop(true, false);
    original.setGameOptions(classic);
    OMemoryStream out; out.open(); original.save(out);

    SECTION("the rule round-trips") {
        IMemoryStream in(out.getData(), out.getDataLength());
        GameInitSettings restored(in);
        REQUIRE_FALSE(restored.getGameOptions().slowCityYear);
        REQUIRE(restored.getGameOptions().getHash() == classic.getHash());
    }

    SECTION("MOD10 streams (1.0.860) keep the 60 s year and their planned-construction rules") {
        auto planned = current;
        planned.retryPlannedPlacement = false;
        auto mod10 = makeCoop(true, false);
        mod10.setGameOptions(planned);
        OMemoryStream mod10Out; mod10Out.open(); mod10.save(mod10Out);
        std::string bytes(mod10Out.getData(), mod10Out.getDataLength());
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = 'A';
        bytes.resize(bytes.size() - Mod12Bytes);
        bytes.back() = 't';   // MOD10 does not own the MOD11 byte; it is the following data
        IMemoryStream oldInput(bytes.data(), bytes.size());
        GameInitSettings old(oldInput);
        REQUIRE_FALSE(old.getGameOptions().slowCityYear);
        REQUIRE_FALSE(old.getGameOptions().retryPlannedPlacement);
        REQUIRE(old.getGameOptions().sharedYardQueue);
        REQUIRE(oldInput.readUint8() == static_cast<Uint8>('t'));
    }

    SECTION("an announced MOD11 byte that is not there is a truncated stream") {
        std::string bytes(out.getData(), out.getDataLength());
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = 'B';
        bytes.resize(bytes.size() - Mod12Bytes - 1);
        IMemoryStream truncated(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(truncated); }());
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
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = older;
        // Strip what that older marker cannot account for, so the tail is unambiguous.
        std::size_t trailing = Mod12Bytes + 1 + 2 + 1 + 1 + 4 + 4 * host.getHouseInfoList().size(); // MOD12 through MOD7
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
        bytes.resize(bytes.size() - Mod12Bytes - 4 - 5);   // MOD12 through MOD8 and the last factor
        IMemoryStream truncated(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(truncated); }());
    }

    SECTION("an explicitly encoded factor outside the range is refused, never clamped") {
        const Uint32 bad = GENERATE(0u, SpiceIncome::kMax + 1, 0xFFFFFFFFu);
        std::string bytes(out.getData(), out.getDataLength());
        REQUIRE(bytes.size() > Mod12Bytes + 9);
        // The final row factor precedes the MOD8..MOD12 rule bytes.
        for(int shift = 0; shift < 4; ++shift) {
            bytes[bytes.size() - Mod12Bytes - 9 + shift] = static_cast<char>((bad >> (8 * shift)) & 0xFF);
        }
        IMemoryStream hostile(bytes.data(), bytes.size());
        REQUIRE_THROWS([&] { GameInitSettings parsed(hostile); }());
    }

    SECTION("a factor count beyond the lobby's rows is refused before anything is read") {
        std::string bytes(out.getData(), out.getDataLength());
        const std::size_t countAt = bytes.size() - (Mod12Bytes + 1 + 2 + 1 + 1 + 4 + 4 * host.getHouseInfoList().size());
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
    // The partner travels as its class whatever its role: Builder, Fighter or Builder + Fighter.
    const std::string botClass = GENERATE(as<std::string>{}, "qBotSupportBrutal", "qBotFighterBrutal", "qBotHard");
    const auto host = makeCoop(campaign, bot, botClass);
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
    REQUIRE(shared.playerInfoList.back().playerClass == (bot ? botClass : std::string(HUMANPLAYERCLASS)));
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
    out.writeUint32(SAVEMAGIC); out.writeUint32(version); out.writeString("DuneCity1.0.850");
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
    // The release that wrote the save, for its shipped mod's version stamp; never serialized.
    REQUIRE(parsed.getSavedGameVersion() == "1.0.850");
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

TEST_CASE("Legacy MOD3 remains readable and truncated MOD10 fails closed", "[workshop][save]") {
    auto original = makeCoop(false, true);
    OMemoryStream out; out.open(); original.save(out);
    std::string bytes(reinterpret_cast<const char*>(out.getData()), out.getDataLength());
    const auto marker = bytes.find("CDOM");
    REQUIRE(marker != std::string::npos);
    SECTION("old graphics marker") {
        bytes[marker] = '3';
        // three strings, two revision integers, yard limit, original damage,
        // then MOD7's factor count/array and the MOD8..MOD12 rule bytes.
        bytes.resize(bytes.size() - 25 - (4 + 4 * original.getHouseInfoList().size()) - 1 - 1 - 2 - 1 - Mod12Bytes);
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

static GameInitSettings makeSeparateFactionCoop(bool archon) {
    const std::vector<int> roster{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    auto session = coop::CoopCampaignSession::create("wire-test", "Tornie", roster,
        {HOUSE_ATREIDES, HOUSE_HARKONNEN}, {"Host", "Guest"}, 0x12345678u, true);
    if(archon) {
        session.enableExpandedAllies(true);
        session.configureAlly(0, HOUSE_ATREIDES, HUMANPLAYERCLASS, "Host", {"Host", "Second"});
        session.configureAlly(1, HOUSE_HARKONNEN, HUMANPLAYERCLASS, "Guest", {"Guest", "Fourth"});
        session.configureAlly(2, HOUSE_WILDSPADE, HUMANPLAYERCLASS, "Fifth", {"Fifth", "Sixth"});
        session.configureAlly(3, HOUSE_KLESHMERSH, HUMANPLAYERCLASS, "Seventh", {"Seventh", "Eighth"});
    }
    session.setAlliedControl(true);
    session.setAlliedBuilding(true);
    session.setEasyMode(true);
    session.setExtraEnemyForces(true);
    session.setExtraHardEnemyForces(true);
    session.skipIntroForEasyMode();
    const auto& context = session.context();
    std::ostringstream source;
    source << "[BASIC]\nTechLevel=" << context.stage << "\nCampaignRevision=2\nWinFlags="
        << WINLOSEFLAGS_QUOTA << "\n[COOP_TEMPLATE]\nSchema=1\nMod=Tornie\nStage=" << context.stage
        << "\nSourceFaction=" << context.sourceFaction << "\nOriginalMission="
        << coop::OriginalMissionNumbers[context.stage-1] + context.missionVariant
        << "\nParticipantCount=6\nEnemyFaction1=0\nEnemyFaction2=2\nEnemyFaction3=3\nEnemyFaction4=4\nSharedQuota=2000\n";
    for(int slot = 1; slot <= 6; ++slot) source << "[Player" << slot << "]\nCredits=1500\n";
    for(int ally = 3; ally <= 4; ++ally)
        source << "[COOP_EXTRA_PLAYER" << ally << "]\nCredits=1500\n[COOP_EXTRA_UNITS"
            << ally << "]\n[COOP_EXTRA_STRUCTURES" << ally << "]\n";
    const auto map = session.prepareMap(source.str());
    const auto prepared = coop::readContext(map);
    REQUIRE(prepared);
    SettingsClass::GameOptionsClass options;
    options.alliedControl = true;
    options.alliedBuilding = true;
    options.easyMode = true;
    options.chaosMode = true;
    options.sellingBuildings = false;
    GameInitSettings init("coop.ini", map, "Test separate-faction co-op", archon, options);
    init.setModIdentity("ws-test-tornie", "checksum");
    init.setRandomSeed(0x3456789au);
    for(const auto& slot : prepared->slots) {
        if(!slot.active()) continue;
        GameInitSettings::HouseInfo house(static_cast<HOUSETYPE>(slot.house), slot.team());
        if(slot.playerClass == HUMANPLAYERCLASS) {
            for(const auto& name : slot.humanNames()) house.addPlayerInfo({name, HUMANPLAYERCLASS});
        } else {
            house.addPlayerInfo({slot.playerName, slot.playerClass});
        }
        init.addHouseInfo(house);
    }
    return init;
}

TEST_CASE("MOD12 co-op settings retain faction roles, eight controllers and objectives over the wire",
          "[tornie][coop][network][save]") {
    const bool archon = GENERATE(false, true);
    auto original = makeSeparateFactionCoop(archon);
    OMemoryStream out; out.open(); original.save(out); out.writeUint32(0xabcdef01u);
    IMemoryStream input(out.getData(), out.getDataLength());
    GameInitSettings restored(input);
    REQUIRE(input.readUint32() == 0xabcdef01u);
    REQUIRE(restored.getFiledata() == original.getFiledata());
    REQUIRE(restored.getRandomSeed() == original.getRandomSeed());
    REQUIRE(restored.getGameOptions().getHash() == original.getGameOptions().getHash());
    REQUIRE(restored.getGameOptions().alliedControl);
    REQUIRE(restored.getGameOptions().alliedBuilding);
    REQUIRE(restored.getGameOptions().easyMode);
    REQUIRE(restored.getGameOptions().chaosMode);
    REQUIRE_FALSE(restored.getGameOptions().sellingBuildings);
    const auto& context = restored.getCoopCampaignContext();
    REQUIRE(context);
    REQUIRE(context->stage == 2);
    REQUIRE(context->extraEnemyForces);
    REQUIRE(context->extraHardEnemyForces);
    REQUIRE(context->humanNames().size() == (archon ? 8 : 2));
    REQUIRE(context->slots[1].faction == context->slots[2].faction);
    REQUIRE(context->slots[1].house != context->slots[2].house);
    REQUIRE(restored.getCoopHarvestObjective().quota == (archon ? 4000 : 2000));
    for(const int row : context->alliedSlots()) {
        REQUIRE(restored.isEasyModeHouse(context->slots[row].house));
        REQUIRE(restored.campaignPurchasePrice(100, context->slots[row].house) == 75);
        REQUIRE(restored.campaignStartingCredits(1500, context->slots[row].house) == 2000);
    }
    REQUIRE_FALSE(restored.isEasyModeHouse(context->slots[2].house));
    REQUIRE(restored.campaignPurchasePrice(100, context->slots[2].house) == 100);
    REQUIRE(restored.campaignStartingCredits(1500, context->slots[2].house) == 1500);
    std::string reason;
    REQUIRE(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(restored, reason));
    restored.setScenarioData("[BASIC]\n");
    REQUIRE_FALSE(restored.getCoopCampaignContext());
    REQUIRE(restored.getCoopHarvestObjective().quota == 0);
    REQUIRE_FALSE(restored.isEasyModeHouse(HOUSE_ATREIDES));
}

TEST_CASE("MOD12 is append-only and every announced rule byte is required",
          "[tornie][coop][save]") {
    const auto original = makeSeparateFactionCoop(false);
    OMemoryStream out; out.open(); original.save(out);
    const std::string encoded(out.getData(), out.getDataLength());
    SECTION("a MOD11 stream consumes none of the six new fields") {
        auto bytes = encoded;
        const auto marker = bytes.rfind("CDOM");
        REQUIRE(marker != std::string::npos);
        bytes[marker] = 'B';
        bytes.resize(bytes.size() - Mod12Bytes);
        bytes.append("tail");
        IMemoryStream input(bytes.data(), bytes.size());
        GameInitSettings restored(input);
        REQUIRE(input.readUint8() == static_cast<Uint8>('t'));
        REQUIRE_FALSE(restored.getGameOptions().alliedControl);
        REQUIRE_FALSE(restored.getGameOptions().alliedBuilding);
        REQUIRE_FALSE(restored.getGameOptions().easyMode);
        REQUIRE_FALSE(restored.getGameOptions().chaosMode);
        REQUIRE(restored.getGameOptions().sellingBuildings);
    }
    SECTION("truncating any part of MOD12 is not mistaken for an old save") {
        const int missing = GENERATE(1, 2, 3, 4, 5, 6);
        auto bytes = encoded;
        bytes.resize(bytes.size() - missing);
        IMemoryStream input(bytes.data(), bytes.size());
        REQUIRE_THROWS(GameInitSettings(input));
    }
}

TEST_CASE("Tornie-only player limits apply to writers and received direct lobby settings",
          "[tornie][network][playerlimit]") {
    GameInitSettings init("twelve.ini", "[BASIC]\n", "Twelve", false, SettingsClass::GameOptionsClass{});
    init.setModIdentity("ws-test-tornie", "checksum");
    for(int house = 0; house < NUM_PLAYER_HOUSES; ++house) {
        GameInitSettings::HouseInfo row(static_cast<HOUSETYPE>(house), house+1);
        row.addPlayerInfo({"Player " + std::to_string(house), house ? "qBotHard" : HUMANPLAYERCLASS});
        init.addHouseInfo(row);
    }
    std::string reason;
    REQUIRE(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(init, reason));
    OMemoryStream out; out.open();
    REQUIRE_NOTHROW(init.save(out));
    IMemoryStream input(out.getData(), out.getDataLength());
    GameInitSettings restored(input);
    REQUIRE(restored.getHouseInfoList().size() == NUM_PLAYER_HOUSES);
    restored.setModIdentity("vanilla", "");
    REQUIRE_FALSE(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(restored, reason));
    OMemoryStream forbidden; forbidden.open();
    REQUIRE_THROWS(restored.save(forbidden));
    init.clearHouseInfo();
    GameInitSettings::HouseInfo natives(HOUSE_NATIVES, 1);
    natives.addPlayerInfo({"Forbidden", HUMANPLAYERCLASS});
    init.addHouseInfo(natives);
    REQUIRE_FALSE(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(init, reason));
}

TEST_CASE("Tornie Easy campaign opening and Chaos eligibility survive saves without changing other mods",
          "[tornie][campaign][save]") {
    SettingsClass::GameOptionsClass options;
    options.easyMode = true;
    options.chaosMode = true;
    {
        ActiveTestMod active("Tornie");
        GameInitSettings original(HOUSE_ATREIDES, options);
        REQUIRE(original.getMission() == 2);
        REQUIRE(original.isChaosCampaignEligible());
        OMemoryStream out; out.open(); original.save(out);
        IMemoryStream input(out.getData(), out.getDataLength());
        GameInitSettings restored(input);
        REQUIRE(restored.isChaosCampaignEligible());
        GameInitSettings next(restored, 5, 3, 0);
        REQUIRE(next.isChaosCampaignEligible());
    }
    GameInitSettings vanilla(HOUSE_ATREIDES, options);
    REQUIRE(vanilla.getMission() == 1);
    REQUIRE_FALSE(vanilla.isEasyModeEnabled());
    REQUIRE_FALSE(vanilla.isChaosModeEnabled());
    REQUIRE(vanilla.campaignStartingCredits(1500, HOUSE_ATREIDES) == 1500);
}

TEST_CASE("Separate co-op saved setup retains every allied participant and controller",
          "[tornie][coop][save]") {
    const auto original = makeSeparateFactionCoop(true);
    const auto houses = original.getHouseInfoList();
    OMemoryStream saved;
    writeSavedSetup(saved, SAVEGAMEVERSION_TORNIE546, original, houses, std::vector<Uint32>(houses.size(), 1));
    IMemoryStream input(saved.getData(), saved.getDataLength());
    GameInitSettings::HouseInfoList loadedHouses;
    const auto loaded = GameInitSettings::readSaveSetup(input, loadedHouses);
    REQUIRE(input.readUint32() == 0xabcdef01u);
    REQUIRE(loadedHouses.size() == houses.size());
    std::size_t humanControllers = 0;
    for(const auto& house : loadedHouses)
        for(const auto& player : house.playerInfoList) humanControllers += player.playerClass == HUMANPLAYERCLASS;
    REQUIRE(humanControllers == 8);
    REQUIRE(loaded.getCoopCampaignContext()->humanNames().size() == 8);
    const auto snapshot = loaded.networkSnapshot("not an INI: binary checkpoint");
    REQUIRE_FALSE(snapshot.getCoopCampaignContext());
}

TEST_CASE("Choosing an authored support faction never replaces a family campaign opponent",
          "[tornie][coop][campaign]") {
    const std::string edition = GENERATE(as<std::string>{}, "Jericho", "JerichoLite", "ArcaKev", "ArcaKevLite");
    const std::vector<int> roster{0, 1, 2, 3, 4, 5};
    const auto session = coop::CoopCampaignSession::create("support", edition, roster,
        {HOUSE_ATREIDES, HOUSE_HARKONNEN}, {"Host", "Guest"}, 7, false);
    const std::string source = "[BASIC]\nTechLevel=1\n[COOP_TEMPLATE]\nSchema=1\nMod=" + edition
        + "\nStage=1\nSourceFaction=1\nOriginalMission=1\nParticipantCount=5\nEnemyFaction1=0\nEnemyFaction2=2\nEnemyFaction3=3\n"
          "[Player1]\n[Player2]\n[Player3]\n[Player4]\n[Player5]\n";
    const auto context = coop::readContext(session.prepareMap(source));
    REQUIRE(context);
    REQUIRE(context->slots[1].faction == HOUSE_HARKONNEN);
    REQUIRE(context->slots[2].faction == HOUSE_HARKONNEN);
    REQUIRE(context->slots[1].house != context->slots[2].house);
}
