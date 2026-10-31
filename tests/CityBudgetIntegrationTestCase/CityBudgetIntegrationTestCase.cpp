/*
 *  CityBudgetIntegrationTestCase.cpp - Tests for city budget integration with construction
 *
 *  Validates:
 *  - spendCityFunds deducts correctly when sufficient funds exist
 *  - spendCityFunds returns false and deducts nothing when insufficient funds
 *  - Road and PowerLine structure prices are defined correctly
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>

// =============================================================================
// spendCityFunds: Sufficient Funds Tests
// =============================================================================

TEST_CASE("CityBudget: spendCityFunds succeeds with exact funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    REQUIRE(sim.getTotalFunds() == 0); // Starts at 0
    sim.setTotalFunds(100);
    REQUIRE(sim.getTotalFunds() == 100);

    // Exact match should succeed
    REQUIRE(sim.spendCityFunds(100) == true);
    REQUIRE(sim.getTotalFunds() == 0);
}

TEST_CASE("CityBudget: spendCityFunds succeeds with surplus funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(1000);

    REQUIRE(sim.spendCityFunds(25) == true);
    REQUIRE(sim.getTotalFunds() == 975);
}

TEST_CASE("CityBudget: spendCityFunds succeeds for small amounts", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(50);

    REQUIRE(sim.spendCityFunds(1) == true);
    REQUIRE(sim.getTotalFunds() == 49);
}

TEST_CASE("CityBudget: multiple spendCityFunds calls accumulate", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100);

    REQUIRE(sim.spendCityFunds(25) == true);   // Road cost
    REQUIRE(sim.getTotalFunds() == 75);
    REQUIRE(sim.spendCityFunds(15) == true);   // Power line cost
    REQUIRE(sim.getTotalFunds() == 60);
    REQUIRE(sim.spendCityFunds(25) == true);   // Another road
    REQUIRE(sim.getTotalFunds() == 35);
}

// =============================================================================
// spendCityFunds: Insufficient Funds Tests
// =============================================================================

TEST_CASE("CityBudget: spendCityFunds fails with zero funds", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(0);

    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 0);  // No deduction on failure
}

TEST_CASE("CityBudget: spendCityFunds fails when one credit short", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(24);

    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 24);  // No deduction on failure
}

TEST_CASE("CityBudget: spendCityFunds fails on negative balance request", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(50);

    REQUIRE(sim.spendCityFunds(0) == true);   // Zero spend should succeed (no-op)
    REQUIRE(sim.spendCityFunds(51) == false); // Overdraft should fail
    REQUIRE(sim.getTotalFunds() == 50);        // Unchanged
}

TEST_CASE("CityBudget: spendCityFunds fails when exact amount would zero out", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(24);

    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 24);  // Unchanged
}

// =============================================================================
// Structure ID Stability
// =============================================================================

TEST_CASE("CityBudget: Road and PowerLine enum IDs are preserved", "[citybudget][ids]") {
    // Enum values remain stable (no renumbering)
    REQUIRE(Structure_Road == 23);
    REQUIRE(Structure_PowerLine == 24);
    REQUIRE(Structure_LastID == 26);  // PoliceStation (26) is now the last structure
}

// =============================================================================
// City Budget Deduction Flow Tests
// =============================================================================

TEST_CASE("CityBudget: spending 25 credits deducted from city funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100);
    REQUIRE(sim.getTotalFunds() == 100);

    REQUIRE(sim.spendCityFunds(25) == true);
    REQUIRE(sim.getTotalFunds() == 75);
}

TEST_CASE("CityBudget: insufficient funds blocks spending", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(24);

    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 24);  // Funds unchanged
}

TEST_CASE("CityBudget: spending 15 credits deducted from city funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100);
    REQUIRE(sim.getTotalFunds() == 100);

    REQUIRE(sim.spendCityFunds(15) == true);
    REQUIRE(sim.getTotalFunds() == 85);
}

TEST_CASE("CityBudget: insufficient funds blocks small spending", "[citybudget][spend][insufficient]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(14);

    REQUIRE(sim.spendCityFunds(15) == false);
    REQUIRE(sim.getTotalFunds() == 14);  // Funds unchanged
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("CityBudget: spendCityFunds does not go negative", "[citybudget][edge]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(10);
    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 10);  // Still 10, never negative
}

TEST_CASE("CityBudget: setTotalFunds changes balance", "[citybudget][funds]") {
    DuneCity::CitySimulation sim;

    REQUIRE(sim.getTotalFunds() == 0);
    sim.setTotalFunds(5000);
    REQUIRE(sim.getTotalFunds() == 5000);
}

TEST_CASE("CityBudget: Large deduction succeeds with sufficient funds", "[citybudget][spend]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100000);
    REQUIRE(sim.spendCityFunds(99975) == true);
    REQUIRE(sim.getTotalFunds() == 25);
}

TEST_CASE("CityBudget: Multiple roads drain city budget progressively", "[citybudget][road]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(100);

    // Place 4 roads (4 x 25 = 100)
    REQUIRE(sim.spendCityFunds(25) == true);  // Road 1: 100 -> 75
    REQUIRE(sim.spendCityFunds(25) == true);  // Road 2: 75 -> 50
    REQUIRE(sim.spendCityFunds(25) == true);  // Road 3: 50 -> 25
    REQUIRE(sim.spendCityFunds(25) == true);  // Road 4: 25 -> 0
    REQUIRE(sim.getTotalFunds() == 0);

    // Fifth road blocked
    REQUIRE(sim.spendCityFunds(25) == false);
    REQUIRE(sim.getTotalFunds() == 0);
}

TEST_CASE("CityBudget: Alternating roads and power lines correct total", "[citybudget][road][powerline]") {
    DuneCity::CitySimulation sim;

    sim.setTotalFunds(200);

    // Road (25) + PowerLine (15) = 40 per pair
    REQUIRE(sim.spendCityFunds(25) == true);  // 200 -> 175
    REQUIRE(sim.spendCityFunds(15) == true);  // 175 -> 160
    REQUIRE(sim.spendCityFunds(25) == true);  // 160 -> 135
    REQUIRE(sim.spendCityFunds(15) == true);  // 135 -> 120
    REQUIRE(sim.getTotalFunds() == 120);
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
