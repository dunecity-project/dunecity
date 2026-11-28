/*
 *  AnalyticsInstallIDTestCase.cpp - the anonymous installation id of the match analytics
 *
 *  The id is 32 lowercase hex characters, stays the same while the settings file keeps it, is
 *  replaced when it is missing or malformed, and goes only on the reporting client's own human
 *  player's row (metaserver/ANALYTICS.md, "Installation id", in the website repo). The real
 *  payload is checked in a running game by the natives probe (played, spectated and lockstep cases).
 */

#include <catch2/catch_test_macros.hpp>
#include <FileClasses/INIFile.h>
#include <misc/AnalyticsInstallID.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct TemporaryConfig {
    std::filesystem::path dir;
    std::filesystem::path file;

    explicit TemporaryConfig(const std::string& contents) {
        dir = std::filesystem::temp_directory_path()
            / ("dunecity-install-id-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        file = dir / "Dune City.ini";
        std::ofstream(file, std::ios::binary) << contents;
    }
    ~TemporaryConfig() {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

    std::string text() const {
        std::ifstream input(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    /// One game start: read the settings file, keep or create the id, save when it was created.
    std::string start(bool& created) const {
        INIFile config(file.string());
        const std::string id = AnalyticsInstallID::loadOrCreate(config, created);
        if(created) REQUIRE(config.saveChangesTo(file.string()));
        return id;
    }
};

bool isLowerHex(const std::string& id) {
    if(id.size() != 32) return false;
    for(const char c : id) if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

size_t count(const std::string& text, const std::string& what) {
    size_t n = 0;
    for(size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

struct SamplePlayer {
    std::string name;
    std::string controller;   // as Game::buildMatchAnalyticsPayload reports it
    bool reportingClientsOwn; // player == pLocalPlayer
};

/// The shape of Game::buildMatchAnalyticsPayload's start report, with the same install_id rule.
std::string samplePayload(const std::vector<SamplePlayer>& players, const std::string& id) {
    std::string rows;
    int slot = 0;
    for(const SamplePlayer& player : players) {
        if(!rows.empty()) rows += ',';
        rows += "{\"slot\":" + std::to_string(slot++) + ",\"player_name\":\"" + player.name
              + "\",\"controller\":\"" + player.controller + "\",\"shared_house_players\":1"
              + AnalyticsInstallID::playerRowField(player.reportingClientsOwn && player.controller == "human", id)
              + '}';
    }
    return "{\"schema_version\":3,\"client_runtime\":\"browser\",\"game_type\":\"multiplayer\",\"players\":["
         + rows + "]}";
}

/// The row (from its opening brace to its closing one) that names @a playerName.
std::string rowOf(const std::string& payload, const std::string& playerName) {
    const size_t named = payload.find("\"player_name\":\"" + playerName + '"');
    REQUIRE(named != std::string::npos);
    const size_t start = payload.rfind("{\"slot\":", named);
    const size_t end = payload.find('}', named);
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    return payload.substr(start, end - start + 1);
}

}

TEST_CASE("A new installation id is 32 lowercase hex characters and never repeats", "[analytics]") {
    std::set<std::string> seen;
    for(int i = 0; i < 64; ++i) {
        const std::string id = AnalyticsInstallID::generate();
        REQUIRE(isLowerHex(id));
        REQUIRE(AnalyticsInstallID::isValid(id));
        seen.insert(id);
    }
    REQUIRE(seen.size() == 64);
}

TEST_CASE("Only 32 lowercase hex characters are a valid installation id", "[analytics]") {
    REQUIRE(AnalyticsInstallID::isValid("0123456789abcdef0123456789abcdef"));
    for(const std::string bad : {"", "0123456789abcdef0123456789abcde", "0123456789abcdef0123456789abcdef0",
                                 "0123456789ABCDEF0123456789ABCDEF", "0123456789abcdef0123456789abcdeg",
                                 " 0123456789abcdef0123456789abcde", "0123456789abcdef-0123456789abcde",
                                 "Player"}) {
        INFO(bad);
        REQUIRE_FALSE(AnalyticsInstallID::isValid(bad));
    }
}

TEST_CASE("The installation id is created once and kept across reloads of the settings file", "[analytics]") {
    const TemporaryConfig config("[General]\nPlay Intro = false\nPlayer Name = Player\n\n[Video]\nWidth = 1280\n");
    bool created = false;
    const std::string first = config.start(created);
    REQUIRE(created);
    REQUIRE(isLowerHex(first));
    // Stored under [General] with the other player settings, and nothing else changed.
    INIFile saved(config.file.string());
    REQUIRE(saved.getStringValue("General", "Analytics Install ID") == first);
    REQUIRE(saved.getStringValue("General", "Player Name") == "Player");
    REQUIRE(saved.getIntValue("Video", "Width") == 1280);
    REQUIRE(count(config.text(), first) == 1);

    for(int reload = 0; reload < 3; ++reload) {
        const std::string again = config.start(created);
        REQUIRE_FALSE(created);
        REQUIRE(again == first);
    }
}

TEST_CASE("A missing or malformed installation id is replaced by a new one", "[analytics]") {
    for(const std::string bad : {"", "0123456789ABCDEF0123456789ABCDEF", "0123456789abcdef", "not an id",
                                 "0123456789abcdef0123456789abcdefff", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"}) {
        INFO(bad);
        const TemporaryConfig config("[General]\nPlayer Name = Mobile\nAnalytics Install ID = " + bad + "\n");
        bool created = false;
        const std::string id = config.start(created);
        REQUIRE(created);
        REQUIRE(isLowerHex(id));
        REQUIRE(id != bad);
        REQUIRE(INIFile(config.file.string()).getStringValue("General", "Analytics Install ID") == id);
        const std::string kept = config.start(created);
        REQUIRE_FALSE(created);
        REQUIRE(kept == id);
    }
    // No [General] section at all (a hand-trimmed file).
    const TemporaryConfig empty("[Video]\nWidth = 640\n");
    bool created = false;
    REQUIRE(isLowerHex(empty.start(created)));
    REQUIRE(created);
}

TEST_CASE("Only the reporting client's own human row carries the installation id", "[analytics]") {
    const std::string id = AnalyticsInstallID::generate();
    REQUIRE(isLowerHex(id));
    const std::string payload = samplePayload({
        {"Host", "human", true},
        {"Guest", "human", false},
        {"Harkonnen", "qbot", false},
        {"Ordos", "ai", false},
        {"Watcher", "spectator", false},
    }, id);
    REQUIRE(count(payload, "\"install_id\"") == 1);
    REQUIRE(count(payload, id) == 1);
    REQUIRE(rowOf(payload, "Host").find(",\"install_id\":\"" + id + '"') != std::string::npos);
    for(const char* other : {"Guest", "Harkonnen", "Ordos", "Watcher"}) {
        INFO(other);
        REQUIRE(rowOf(payload, other).find("install_id") == std::string::npos);
    }
    // Never at the root: everything before the player list is id-free.
    REQUIRE(payload.substr(0, payload.find("\"players\":")).find("install_id") == std::string::npos);

    // A game with no local human (all AI, or spectated) sends no id; nor does an invalid one.
    REQUIRE(samplePayload({{"Atreides", "qbot", false}, {"Ordos", "ai", false}}, id).find("install_id") == std::string::npos);
    REQUIRE(samplePayload({{"Host", "human", true}}, "").find("install_id") == std::string::npos);
    REQUIRE(samplePayload({{"Host", "human", true}}, "0123456789ABCDEF0123456789ABCDEF").find("install_id") == std::string::npos);
    REQUIRE(AnalyticsInstallID::playerRowField(false, id).empty());
    REQUIRE(AnalyticsInstallID::playerRowField(true, id) == ",\"install_id\":\"" + id + '"');
}
