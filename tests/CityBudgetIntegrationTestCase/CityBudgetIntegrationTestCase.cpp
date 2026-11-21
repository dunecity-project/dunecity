/*
 *  CityBudgetIntegrationTestCase.cpp - City budget rules that need no running game
 *
 *  Validates:
 *  - Road and PowerLine keep their structure IDs
 *  - The tax census and the annual tax arithmetic
 *
 *  The treasury is the local house's credits (CityEffectsRuntime.cpp). That needs
 *  a House, so it is not covered here.
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>

// =============================================================================
// Structure ID Stability
// =============================================================================

TEST_CASE("CityBudget: Road and PowerLine enum IDs are preserved", "[citybudget][ids]") {
    // Enum values remain stable (no renumbering)
    REQUIRE(Structure_Road == 23);
    REQUIRE(Structure_PowerLine == 24);
    REQUIRE(Structure_LastID == 26);  // PoliceStation (26) is now the last structure
}

TEST_CASE("City tax census retains fractional housing and the Palace dual income", "[citybudget][tax]") {
    using namespace DuneCity;
    const int home=taxablePopulationEighths(Structure_ZoneResidential,40,3);
    const int shop=taxablePopulationEighths(Structure_ZoneCommercial,5,3);
    const int industry=taxablePopulationEighths(Structure_ZoneIndustrial,4,3);
    const int palace=taxablePopulationEighths(Structure_Palace,40,3);
    CHECK(home == shop);
    CHECK(2*palace == home+shop); // Zone boost excludes Palace.
    CHECK(computeAnnualTaxRevenue(home+shop+industry+palace,7,128) == 397);
    // Many small plots are aggregated before rounding; one house has R=2.
    CHECK(computeAnnualTaxRevenue(100*taxablePopulationEighths(Structure_ZoneResidential,2,0),7,128) == 522);
    // A year of fractional cycle payouts totals the annual bill, not one rounded bill per cycle.
    const int ticksPerYear=static_cast<int>(cyclesPerCityYear());
    const FixPoint tick=FixPoint(computeAnnualTaxRevenue(palace,7,128))/ticksPerYear;
    FixPoint paid=0;
    for (int n=0;n<ticksPerYear;++n) paid+=tick;
    CHECK(paid.toDouble() == Catch::Approx(104).margin(0.001));
}
