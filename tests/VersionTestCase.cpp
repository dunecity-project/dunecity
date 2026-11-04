#include <catch2/catch_test_macros.hpp>
#include <config.h>

#include <algorithm>
#include <string>

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
