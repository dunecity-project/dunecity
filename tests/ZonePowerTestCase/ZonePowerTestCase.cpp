/*
 *  ZonePowerTestCase.cpp
 *
 *  Locks in the per-density power consumption for city zones.
 *  Anchor: Industrial L3 = 24 ≈ 2/3 of Heavy Factory's 35 (Dune side).
 */

#include <catch2/catch_all.hpp>
#include <dunecity/ZonePower.h>
#include <data.h>

using DuneCity::getZonePower;

TEST_CASE("Zone power is zero when density is zero or zone-less", "[zonepower]") {
    REQUIRE(getZonePower(Structure_ZoneResidential, 0) == 0);
    REQUIRE(getZonePower(Structure_ZoneCommercial, 0) == 0);
    REQUIRE(getZonePower(Structure_ZoneIndustrial, 0) == 0);

    REQUIRE(getZonePower(Structure_Refinery, 1) == 0);
    REQUIRE(getZonePower(Structure_WindTrap, 3) == 0);
    REQUIRE(getZonePower(Structure_NuclearPlant, 3) == 0);
}

TEST_CASE("Residential zone power scales with density", "[zonepower]") {
    REQUIRE(getZonePower(Structure_ZoneResidential, 1) ==  3);
    REQUIRE(getZonePower(Structure_ZoneResidential, 2) ==  7);
    REQUIRE(getZonePower(Structure_ZoneResidential, 3) == 12);
}

TEST_CASE("Commercial zone power scales with density", "[zonepower]") {
    REQUIRE(getZonePower(Structure_ZoneCommercial, 1) ==  5);
    REQUIRE(getZonePower(Structure_ZoneCommercial, 2) == 11);
    REQUIRE(getZonePower(Structure_ZoneCommercial, 3) == 18);
}

TEST_CASE("Industrial zone power scales with density and L3 anchors to 2/3 of Heavy Factory", "[zonepower]") {
    REQUIRE(getZonePower(Structure_ZoneIndustrial, 1) ==  7);
    REQUIRE(getZonePower(Structure_ZoneIndustrial, 2) == 14);
    REQUIRE(getZonePower(Structure_ZoneIndustrial, 3) == 24);
    // Heavy Factory power is 35 (config/ObjectData.ini.default). 2/3 = 23.3 → 24.
    REQUIRE(getZonePower(Structure_ZoneIndustrial, 3) * 3 >= 35 * 2);
}

TEST_CASE("Density above 3 is clamped to density-3 value", "[zonepower]") {
    REQUIRE(getZonePower(Structure_ZoneResidential, 4) == 12);
    REQUIRE(getZonePower(Structure_ZoneCommercial, 5)  == 18);
    REQUIRE(getZonePower(Structure_ZoneIndustrial, 99) == 24);
}

TEST_CASE("Power ordering: Industrial > Commercial > Residential at every density", "[zonepower]") {
    for (int d = 1; d <= 3; ++d) {
        const int r = getZonePower(Structure_ZoneResidential, d);
        const int c = getZonePower(Structure_ZoneCommercial, d);
        const int i = getZonePower(Structure_ZoneIndustrial, d);
        INFO("density=" << d);
        REQUIRE(i > c);
        REQUIRE(c > r);
    }
}

TEST_CASE("Zone growth needs a live, sufficient grid under the vacancy rule", "[zonepower]") {
    REQUIRE_FALSE(DuneCity::hasZonePower(0, 0, true));
    REQUIRE_FALSE(DuneCity::hasZonePower(0, 10, true));
    REQUIRE_FALSE(DuneCity::hasZonePower(9, 10, true));
    REQUIRE(DuneCity::hasZonePower(10, 10, true));
    REQUIRE(DuneCity::hasZonePower(11, 10, true));
    REQUIRE(DuneCity::hasZonePower(10, 0, true));
    REQUIRE(DuneCity::hasZonePower(0, 0, false));
    REQUIRE_FALSE(DuneCity::hasZonePower(0, 10, false));
}
