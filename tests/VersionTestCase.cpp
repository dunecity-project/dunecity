#include <catch2/catch_test_macros.hpp>
#include <config.h>
#include <misc/DisplayVersion.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {
struct VersionResources {
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("dunecity-version-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path resources = root / "DuneCity.app/Contents/Resources";

    VersionResources() {
        std::filesystem::create_directories(resources);
    }
    ~VersionResources() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void writeVersion(const std::string& text) const {
        std::ofstream output(resources / "air-dev-version.txt", std::ios::binary);
        output << text;
        output.close();
        REQUIRE(output.good());
    }
};
} // namespace

TEST_CASE("Development display labels preserve numeric compatibility identifiers", "[version][regression]") {
    const std::string numeric = VERSION;
    REQUIRE(std::count(numeric.begin(), numeric.end(), '.') == 2);
    REQUIRE(std::all_of(numeric.begin(), numeric.end(), [](char c) {
        return (c >= '0' && c <= '9') || c == '.';
    }));
    REQUIRE(std::string(VERSIONSTRING) == std::string(PACKAGE) + numeric);
#if DUNECITY_RELEASE_BUILD
    REQUIRE(std::string(DUNECITY_DISPLAY_VERSION) == numeric);
#else
    REQUIRE(std::string(DUNECITY_DISPLAY_VERSION) == numeric + "dev");
#endif
    REQUIRE(std::string(VERSIONSTRING).find("dev") == std::string::npos);
}

TEST_CASE("Display version falls back without an app-bundle override", "[version][regression]") {
    VersionResources fixture;
    REQUIRE(loadDisplayVersion(fixture.resources.string()) == DUNECITY_DISPLAY_VERSION);
    REQUIRE(loadDisplayVersion("") == DUNECITY_DISPLAY_VERSION);
    // Immutable provenance and mod files are not display-version overrides.
    std::ofstream(fixture.resources / "air-dev-source.txt") << "21ef5e7b08ae41a099bc1360de35f0bbbf1cd3b8\n";
    std::filesystem::create_directories(fixture.resources / "mods/dunecity");
    std::ofstream(fixture.resources / "mods/dunecity/air-dev-version.txt") << "not-the-app-version\n";
    REQUIRE(loadDisplayVersion(fixture.resources.string()) == DUNECITY_DISPLAY_VERSION);
    for(const std::string empty : {"", "\n", "\r\n"}) {
        fixture.writeVersion(empty);
        REQUIRE(loadDisplayVersion(fixture.resources.string()) == DUNECITY_DISPLAY_VERSION);
    }
}

TEST_CASE("The real display loader uses the exact Air variant only in development builds", "[version][regression]") {
    VersionResources fixture;
    const std::string variant = std::string(VERSION) + "-t20261128.174500-3153b71";
    for(const std::string ending : {"", "\n", "\r\n"}) {
        fixture.writeVersion(variant + ending);
#if DUNECITY_RELEASE_BUILD
        REQUIRE(loadDisplayVersion(fixture.resources.string()) == DUNECITY_DISPLAY_VERSION);
        REQUIRE(loadDisplayVersion(fixture.resources.string()) == VERSION);
#else
        REQUIRE(loadDisplayVersion(fixture.resources.string()) == variant);
#endif
    }
    REQUIRE(std::string(VERSIONSTRING) == std::string(PACKAGE) + VERSION);
}

TEST_CASE("Application display lookups reuse one immutable cached value", "[version][regression]") {
    const std::string& version = getDisplayVersion();
    const char* const data = version.c_str();
    REQUIRE_FALSE(version.empty());
#if DUNECITY_RELEASE_BUILD
    REQUIRE(version == VERSION);
#endif
    for(int lookup = 0; lookup < 10; ++lookup) {
        REQUIRE(&getDisplayVersion() == &version);
        REQUIRE(getDisplayVersion().c_str() == data);
    }
}
