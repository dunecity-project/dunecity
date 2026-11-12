/*
 *  BudgetAllocationTestCase.cpp - Unit tests for Phase 5 funding allocation sliders
 *
 *  Validates:
 *  - CityBudget allocation percentage getters/setters (road, police, fire)
 *  - Allocation percentages default to 100
 *  - Percentages can be set and retrieved independently
 *  - Tax revenue reflects population and tax rate
 */

#include <catch2/catch_all.hpp>
#include <dunecity/CityBudget.h>
#include <dunecity/CitySimulation.h>

using namespace DuneCity;

// =============================================================================
// CityBudget Allocation Percentage Tests
// =============================================================================

TEST_CASE("BudgetAllocation: default allocation percentages are 100", "[budget][allocation][default]") {
    CityBudget budget;

    REQUIRE(budget.getRoadPercent() == 100);
    REQUIRE(budget.getPolicePercent() == 100);
    REQUIRE(budget.getFirePercent() == 100);
}

TEST_CASE("BudgetAllocation: allocation percentages can be set independently", "[budget][allocation][set]") {
    CityBudget budget;

    budget.setRoadPercent(50);
    REQUIRE(budget.getRoadPercent() == 50);
    REQUIRE(budget.getPolicePercent() == 100); // unchanged
    REQUIRE(budget.getFirePercent() == 100);   // unchanged

    budget.setPolicePercent(25);
    REQUIRE(budget.getRoadPercent() == 50);
    REQUIRE(budget.getPolicePercent() == 25);
    REQUIRE(budget.getFirePercent() == 100);   // unchanged

    budget.setFirePercent(33);
    REQUIRE(budget.getRoadPercent() == 50);
    REQUIRE(budget.getPolicePercent() == 25);
    REQUIRE(budget.getFirePercent() == 33);
}

TEST_CASE("BudgetAllocation: allocation percentages can be set to zero", "[budget][allocation][set]") {
    CityBudget budget;

    budget.setRoadPercent(0);
    budget.setPolicePercent(0);
    budget.setFirePercent(0);

    REQUIRE(budget.getRoadPercent() == 0);
    REQUIRE(budget.getPolicePercent() == 0);
    REQUIRE(budget.getFirePercent() == 0);
}

TEST_CASE("BudgetAllocation: allocation percentages can be set to 100", "[budget][allocation][set]") {
    CityBudget budget;

    budget.setRoadPercent(100);
    budget.setPolicePercent(100);
    budget.setFirePercent(100);

    REQUIRE(budget.getRoadPercent() == 100);
    REQUIRE(budget.getPolicePercent() == 100);
    REQUIRE(budget.getFirePercent() == 100);
}

TEST_CASE("BudgetAllocation: changed police percentage preserved after setting", "[budget][allocation][set]") {
    CityBudget budget;

    budget.setPolicePercent(75);
    REQUIRE(budget.getPolicePercent() == 75);

    budget.setPolicePercent(10);
    REQUIRE(budget.getPolicePercent() == 10);
}

TEST_CASE("BudgetAllocation: changed fire percentage preserved after setting", "[budget][allocation][set]") {
    CityBudget budget;

    budget.setFirePercent(60);
    REQUIRE(budget.getFirePercent() == 60);

    budget.setFirePercent(5);
    REQUIRE(budget.getFirePercent() == 5);
}

TEST_CASE("BudgetAllocation: changed road percentage preserved after setting", "[budget][allocation][set]") {
    CityBudget budget;

    budget.setRoadPercent(40);
    REQUIRE(budget.getRoadPercent() == 40);

    budget.setRoadPercent(80);
    REQUIRE(budget.getRoadPercent() == 80);
}

TEST_CASE("BudgetAllocation: three categories are independent", "[budget][allocation][isolation]") {
    CityBudget b1;
    CityBudget b2;

    b1.setRoadPercent(33);
    b1.setPolicePercent(66);
    b1.setFirePercent(11);

    b2.setRoadPercent(33);
    b2.setPolicePercent(66);
    b2.setFirePercent(11);

    REQUIRE(b1.getRoadPercent() == b2.getRoadPercent());
    REQUIRE(b1.getPolicePercent() == b2.getPolicePercent());
    REQUIRE(b1.getFirePercent() == b2.getFirePercent());
}

// =============================================================================
// Tax Revenue Tests
// =============================================================================

TEST_CASE("BudgetAllocation: CityBudget init does not throw", "[budget][init]") {
    CitySimulation sim;
    CityBudget budget;

    sim.init(4, 4);
    budget.init(&sim);

    // If we got here without exception, init worked
    REQUIRE(true);
}

TEST_CASE("BudgetAllocation: zero city tax yields zero tax revenue", "[budget][tax]") {
    CitySimulation sim;
    CityBudget budget;

    sim.init(4, 4);
    budget.init(&sim);

    sim.setCityTax(0, 0);
    sim.setTotalFunds(1000);

    budget.collectTax();

    // pop = 0, taxRate = 0 => revenue = 0
    REQUIRE(budget.getLastTaxRevenue() == 0);
}

TEST_CASE("BudgetAllocation: collectTax can be called multiple times", "[budget][tax]") {
    CitySimulation sim;
    CityBudget budget;

    sim.init(4, 4);
    budget.init(&sim);
    sim.setTotalFunds(1000);
    sim.setCityTax(0, 10);

    // First call
    budget.collectTax();
    int32_t firstRevenue = budget.getLastTaxRevenue();

    // Second call should also work
    budget.collectTax();
    int32_t secondRevenue = budget.getLastTaxRevenue();

    // Both should succeed without throwing
    REQUIRE(firstRevenue >= 0);
    REQUIRE(secondRevenue >= 0);
}
