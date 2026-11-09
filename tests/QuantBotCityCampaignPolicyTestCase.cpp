#include <players/QuantBotCityCampaignPolicy.h>
#include <catch2/catch_test_macros.hpp>

using namespace QuantBotCityCampaignPolicy;

namespace {

/// Confirmation window expressed in cycles, as passed to postSpiceConfirmed().
constexpr unsigned kConfirmCycles = 1875;

/// A house that really did own an economy in the original mission.
constexpr Baseline funded(int allowance, int refineries = 2) {
    return Baseline{refineries, allowance};
}

/// Large enough that the original-allowance clamp never hides a matrix value.
constexpr int kUncappedAllowance = 99;

struct Cell {
    int difficulty;
    int level;
    int harvesters;
    int sharedZoneCap;
};

/// The accepted matrix written out cell by cell, independent of the banding
/// arithmetic in the policy header. Every level 1..9 for every campaign tier.
constexpr Cell kAcceptedMatrix[] = {
    {Easy, 1, 0, 0},   {Easy, 2, 1, 4},   {Easy, 3, 1, 4},
    {Easy, 4, 1, 4},   {Easy, 5, 1, 8},   {Easy, 6, 1, 8},
    {Easy, 7, 1, 8},   {Easy, 8, 1, 8},   {Easy, 9, 1, 8},

    {Medium, 1, 0, 0}, {Medium, 2, 2, 5},  {Medium, 3, 1, 8},
    {Medium, 4, 1, 8}, {Medium, 5, 2, 16}, {Medium, 6, 2, 16},
    {Medium, 7, 2, 16}, {Medium, 8, 2, 16}, {Medium, 9, 2, 16},

    {Hard, 1, 0, 0},   {Hard, 2, 3, 12},  {Hard, 3, 3, 12},
    {Hard, 4, 2, 16},  {Hard, 5, 1, 20},  {Hard, 6, 1, 20},
    {Hard, 7, 1, 20},  {Hard, 8, 1, 20},  {Hard, 9, 1, 20},

    {Brutal, 1, 0, 0}, {Brutal, 2, 6, 18}, {Brutal, 3, 5, 20},
    {Brutal, 4, 4, 24}, {Brutal, 5, 3, 28}, {Brutal, 6, 3, 28},
    {Brutal, 7, 3, 28}, {Brutal, 8, 3, 28}, {Brutal, 9, 3, 28},
};

} // namespace

TEST_CASE("Campaign city matrix matches the accepted table in every cell", "[quantbot][campaign][city]") {
    static_assert(sizeof(kAcceptedMatrix) / sizeof(kAcceptedMatrix[0]) == 36, "4 tiers x 9 levels");

    for (const Cell& cell : kAcceptedMatrix) {
        INFO("difficulty " << cell.difficulty << " level " << cell.level);
        const Limits row = normalLimits(cell.difficulty, cell.level);
        REQUIRE(row.harvesters == cell.harvesters);
        REQUIRE(row.sharedZoneCap == cell.sharedZoneCap);

        // A funded house with a generous original allowance sees the same row.
        const Limits applied = limits(cell.difficulty, cell.level, funded(kUncappedAllowance), false);
        REQUIRE(applied.harvesters == cell.harvesters);
        REQUIRE(applied.sharedZoneCap == cell.sharedZoneCap);
    }
}

TEST_CASE("Defend and unknown tiers get no campaign city economy", "[quantbot][campaign][city]") {
    for (int level = 1; level <= 9; ++level) {
        INFO("level " << level);
        REQUIRE(normalLimits(Defend, level).harvesters == 0);
        REQUIRE(normalLimits(Defend, level).sharedZoneCap == 0);
        const Limits applied = limits(Defend, level, funded(kUncappedAllowance), false);
        REQUIRE(applied.harvesters == 0);
        REQUIRE(applied.sharedZoneCap == 0);
    }
}

TEST_CASE("Missions index the matrix by clamped display level", "[quantbot][campaign][city]") {
    REQUIRE(displayLevel(-5) == 1);
    REQUIRE(displayLevel(0) == 1);
    REQUIRE(displayLevel(1) == 1);
    REQUIRE(displayLevel(5) == 5);
    REQUIRE(displayLevel(9) == 9);
    REQUIRE(displayLevel(12) == 9);
}

TEST_CASE("Scripted houses without an original economy stay at zero", "[quantbot][campaign][city]") {
    // No original refinery: nothing is granted, at any tier, spice or not.
    for (const Cell& cell : kAcceptedMatrix) {
        INFO("difficulty " << cell.difficulty << " level " << cell.level);
        const Limits noRefinery = limits(cell.difficulty, cell.level, Baseline{0, 4}, false);
        REQUIRE(noRefinery.harvesters == 0);
        REQUIRE(noRefinery.sharedZoneCap == 0);

        const Limits noAllowance = limits(cell.difficulty, cell.level, Baseline{2, 0}, false);
        REQUIRE(noAllowance.harvesters == 0);
        REQUIRE(noAllowance.sharedZoneCap == 0);
    }

    // Negative or exhausted baselines are treated exactly like absent ones,
    // and post-spice never resurrects a house that never had an economy.
    REQUIRE(limits(Brutal, 9, Baseline{-1, 6}, true).sharedZoneCap == 0);
    REQUIRE(limits(Brutal, 9, Baseline{2, -3}, true).sharedZoneCap == 0);
    REQUIRE(limits(Hard, 5, Baseline{0, 0}, true).harvesters == 0);

    // Level 1 has no working enemy economy at all, even fully funded.
    REQUIRE(limits(Brutal, 1, funded(kUncappedAllowance), true).harvesters == 0);
    REQUIRE(limits(Brutal, 1, funded(kUncappedAllowance), true).sharedZoneCap == 0);
}

TEST_CASE("Worker targets never exceed the original mission allowance", "[quantbot][campaign][city]") {
    // Brutal level 2 asks for six; a two-worker house still only plans two.
    REQUIRE(limits(Brutal, 2, funded(2), false).harvesters == 2);
    REQUIRE(limits(Brutal, 2, funded(2), false).sharedZoneCap == 18);
    REQUIRE(limits(Brutal, 2, funded(6), false).harvesters == 6);
    REQUIRE(limits(Brutal, 2, funded(7), false).harvesters == 6);
    REQUIRE(limits(Hard, 3, funded(1), false).harvesters == 1);
}

TEST_CASE("Game Options worker ceiling applies only when positive", "[quantbot][campaign][city]") {
    const Baseline base = funded(kUncappedAllowance);

    // Unset (0) and default (-1) impose no ceiling of their own.
    REQUIRE(limits(Brutal, 2, base, false, 0).harvesters == 6);
    REQUIRE(limits(Brutal, 2, base, false, -1).harvesters == 6);
    REQUIRE(limits(Brutal, 2, base, false).harvesters == 6);

    // A positive ceiling clamps, but never raises the matrix value.
    REQUIRE(limits(Brutal, 2, base, false, 4).harvesters == 4);
    REQUIRE(limits(Brutal, 2, base, false, 6).harvesters == 6);
    REQUIRE(limits(Brutal, 2, base, false, 20).harvesters == 6);
    REQUIRE(limits(Easy, 5, base, false, 10).harvesters == 1);

    // The ceiling is a worker ceiling only: zones are untouched.
    REQUIRE(limits(Brutal, 2, base, false, 1).sharedZoneCap == 18);
}

TEST_CASE("Small original budgets keep the small row on levels 5-9", "[quantbot][campaign][city]") {
    for (int level = 5; level <= 9; ++level) {
        INFO("level " << level);
        // One Easy worker: the small row instead of the large-house row.
        REQUIRE(smallBudgetLimits(Easy, level, 1).harvesters == 1);
        REQUIRE(smallBudgetLimits(Easy, level, 1).sharedZoneCap == 4);
        REQUIRE(smallBudgetLimits(Easy, level, 2).sharedZoneCap == 8);

        // Up to two Medium workers keep the smaller row.
        REQUIRE(smallBudgetLimits(Medium, level, 1).harvesters == 1);
        REQUIRE(smallBudgetLimits(Medium, level, 1).sharedZoneCap == 8);
        REQUIRE(smallBudgetLimits(Medium, level, 2).harvesters == 1);
        REQUIRE(smallBudgetLimits(Medium, level, 2).sharedZoneCap == 8);
        REQUIRE(smallBudgetLimits(Medium, level, 3).harvesters == 2);
        REQUIRE(smallBudgetLimits(Medium, level, 3).sharedZoneCap == 16);

        // An absent allowance is not a small budget; it is handled by limits().
        REQUIRE(smallBudgetLimits(Easy, level, 0).sharedZoneCap == 8);
        REQUIRE(smallBudgetLimits(Medium, level, 0).sharedZoneCap == 16);

        // Hard and Brutal keep their own rows regardless of budget size.
        REQUIRE(smallBudgetLimits(Hard, level, 1).sharedZoneCap == 20);
        REQUIRE(smallBudgetLimits(Brutal, level, 1).sharedZoneCap == 28);
    }

    // Below level 5 the small-budget rule does not apply at all.
    for (int level = 1; level <= 4; ++level) {
        INFO("level " << level);
        REQUIRE(smallBudgetLimits(Medium, level, 1).sharedZoneCap == normalLimits(Medium, level).sharedZoneCap);
        REQUIRE(smallBudgetLimits(Easy, level, 1).sharedZoneCap == normalLimits(Easy, level).sharedZoneCap);
    }

    // Reached through limits(), the allowance clamp and the small row agree.
    REQUIRE(limits(Medium, 7, funded(2), false).harvesters == 1);
    REQUIRE(limits(Medium, 7, funded(2), false).sharedZoneCap == 8);
    REQUIRE(limits(Medium, 7, funded(3), false).sharedZoneCap == 16);
}

TEST_CASE("Post-spice raises totals for Hard and Brutal only", "[quantbot][campaign][city]") {
    const Baseline base = funded(kUncappedAllowance);

    for (int level = 2; level <= 9; ++level) {
        INFO("level " << level);

        // Easy and Medium receive no extra zone allowance once spice is gone.
        REQUIRE(limits(Easy, level, base, true).sharedZoneCap
                == limits(Easy, level, base, false).sharedZoneCap);
        REQUIRE(limits(Medium, level, base, true).sharedZoneCap
                == limits(Medium, level, base, false).sharedZoneCap);

        // Hard and Brutal move to a TOTAL ceiling, never an addition.
        REQUIRE(limits(Hard, level, base, true).sharedZoneCap == kPostSpiceHardZoneCap);
        REQUIRE(limits(Brutal, level, base, true).sharedZoneCap == kPostSpiceBrutalZoneCap);

        // With no spice left there is nothing to fund workers, at every tier.
        REQUIRE(limits(Easy, level, base, true).harvesters == 0);
        REQUIRE(limits(Medium, level, base, true).harvesters == 0);
        REQUIRE(limits(Hard, level, base, true).harvesters == 0);
        REQUIRE(limits(Brutal, level, base, true).harvesters == 0);
    }

    REQUIRE(kPostSpiceHardZoneCap == 24);
    REQUIRE(kPostSpiceBrutalZoneCap == 40);

    // The post-spice cap is a floor on the total, so a larger normal row wins.
    REQUIRE(limits(Brutal, 5, base, false).sharedZoneCap == 28);
    REQUIRE(limits(Hard, 5, base, true).sharedZoneCap >= limits(Hard, 5, base, false).sharedZoneCap);

    // A house with no original economy gains nothing post-spice.
    REQUIRE(limits(Brutal, 5, Baseline{0, 4}, true).sharedZoneCap == 0);
    REQUIRE(limits(Hard, 5, Baseline{2, 0}, true).sharedZoneCap == 0);
}

TEST_CASE("Income goal is 450 per original worker and never rebases", "[quantbot][campaign][city]") {
    REQUIRE(kReferenceIncomePerHarvester == 300);
    REQUIRE(kGoalIncomePerOriginalHarvester == 450);

    const Baseline base = funded(4);
    REQUIRE(baselineIncomePerMinute(base) == 1200);
    REQUIRE(totalIncomeGoalPerMinute(base) == 1800);

    // 150% of the original combined budget, for any original allowance.
    REQUIRE(totalIncomeGoalPerMinute(funded(1)) == 450);
    REQUIRE(totalIncomeGoalPerMinute(funded(6)) == 2700);

    // Houses without an original economic base have no goal to chase.
    REQUIRE(baselineIncomePerMinute(Baseline{0, 4}) == 0);
    REQUIRE(totalIncomeGoalPerMinute(Baseline{0, 4}) == 0);
    REQUIRE(totalIncomeGoalPerMinute(Baseline{-1, 4}) == 0);
    REQUIRE(totalIncomeGoalPerMinute(Baseline{2, -4}) == 0);
    REQUIRE(baselineIncomePerMinute(Baseline{2, -4}) == 0);

    // Tax carries the remainder of the goal beyond the planned harvest share.
    REQUIRE(netTaxGoalPerMinute(base, 4) == 600);
    REQUIRE(netTaxGoalPerMinute(base, 2) == 1200);

    // Post-spice the worker target is zero: the SAME original goal remains,
    // without a second 50% and without rebasing against zero income.
    REQUIRE(netTaxGoalPerMinute(base, 0) == 1800);
    REQUIRE(netTaxGoalPerMinute(base, 0) == totalIncomeGoalPerMinute(base));
    REQUIRE(netTaxGoalPerMinute(base, limits(Brutal, 5, base, true).harvesters) == 1800);

    // A target beyond the goal clamps at zero rather than going negative.
    REQUIRE(netTaxGoalPerMinute(base, 6) == 0);
    REQUIRE(netTaxGoalPerMinute(base, 99) == 0);
    REQUIRE(netTaxGoalPerMinute(base, -3) == 1800);
    REQUIRE(netTaxGoalPerMinute(Baseline{0, 4}, 0) == 0);
}

TEST_CASE("Discretionary expansion stops at the shared zone boundary", "[quantbot][campaign][city]") {
    const int goal = totalIncomeGoalPerMinute(funded(4)); // 1800

    // The count is TOTAL R+I+C including queued orders: one below the cap is
    // still discretionary, the cap itself is not.
    REQUIRE(allowsDiscretionaryExpansion(23, 24, 0, goal));
    REQUIRE_FALSE(allowsDiscretionaryExpansion(24, 24, 0, goal));
    REQUIRE_FALSE(allowsDiscretionaryExpansion(25, 24, 0, goal));

    // A queued order that reaches the cap closes expansion immediately, even
    // though only 23 zones are physically placed.
    const int placed = 23;
    const int queued = 1;
    REQUIRE_FALSE(allowsDiscretionaryExpansion(placed + queued, 24, 0, goal));
    REQUIRE(allowsDiscretionaryExpansion(placed, 24, 0, goal));

    // No allowance at all means no discretionary growth.
    REQUIRE_FALSE(allowsDiscretionaryExpansion(0, 0, 0, goal));
    REQUIRE_FALSE(allowsDiscretionaryExpansion(0, -4, 0, goal));

    // Income boundary: at or above the goal growth stops, below it continues.
    REQUIRE(allowsDiscretionaryExpansion(4, 24, 1799, goal));
    REQUIRE_FALSE(allowsDiscretionaryExpansion(4, 24, 1800, goal));
    REQUIRE_FALSE(allowsDiscretionaryExpansion(4, 24, 5000, goal));

    // Without a goal the physical ceiling is the only stop.
    REQUIRE(allowsDiscretionaryExpansion(4, 24, 5000, 0));
    REQUIRE(allowsDiscretionaryExpansion(4, 24, 5000, -1));
    REQUIRE_FALSE(allowsDiscretionaryExpansion(24, 24, 0, 0));

    // The post-spice Brutal total is the boundary that actually applies there.
    const Limits postSpice = limits(Brutal, 5, funded(4), true);
    REQUIRE(allowsDiscretionaryExpansion(39, postSpice.sharedZoneCap, 0, goal));
    REQUIRE_FALSE(allowsDiscretionaryExpansion(40, postSpice.sharedZoneCap, 0, goal));
}

TEST_CASE("Spice exhaustion is confirmed only after a held zero window", "[quantbot][campaign][city]") {
    const unsigned zeroSince = 10000;

    // The fresh zero must hold for the full confirmation window.
    REQUIRE_FALSE(postSpiceConfirmed(0, 0, zeroSince, zeroSince, kConfirmCycles));
    REQUIRE_FALSE(postSpiceConfirmed(0, 0, zeroSince, zeroSince + kConfirmCycles - 1, kConfirmCycles));
    REQUIRE(postSpiceConfirmed(0, 0, zeroSince, zeroSince + kConfirmCycles, kConfirmCycles));
    REQUIRE(postSpiceConfirmed(0, 0, zeroSince, zeroSince + kConfirmCycles + 1, kConfirmCycles));

    // Spice back on the map (a bloom) ends the post-spice state at once.
    REQUIRE_FALSE(postSpiceConfirmed(1, 0, zeroSince, zeroSince + 10 * kConfirmCycles, kConfirmCycles));
    REQUIRE_FALSE(postSpiceConfirmed(5000, 0, zeroSince, zeroSince + 10 * kConfirmCycles, kConfirmCycles));

    // Cargo still riding in a harvester also means the economy is not done.
    REQUIRE_FALSE(postSpiceConfirmed(0, 1, zeroSince, zeroSince + 10 * kConfirmCycles, kConfirmCycles));
    REQUIRE_FALSE(postSpiceConfirmed(0, 700, zeroSince, zeroSince + kConfirmCycles, kConfirmCycles));

    // A reversed window (zero recorded in the future, e.g. after a reload)
    // never confirms and never underflows into a huge elapsed count.
    REQUIRE_FALSE(postSpiceConfirmed(0, 0, zeroSince, zeroSince - 1, kConfirmCycles));
    REQUIRE_FALSE(postSpiceConfirmed(0, 0, zeroSince + 10 * kConfirmCycles, zeroSince, kConfirmCycles));
    REQUIRE_FALSE(postSpiceConfirmed(0, 0, 1, 0, kConfirmCycles));

    // From cycle zero the window is the same length.
    REQUIRE_FALSE(postSpiceConfirmed(0, 0, 0, kConfirmCycles - 1, kConfirmCycles));
    REQUIRE(postSpiceConfirmed(0, 0, 0, kConfirmCycles, kConfirmCycles));

    REQUIRE(kPostSpiceConfirmSeconds == 30);
}

TEST_CASE("CampaignEnemyHarvesterRatio restores the refinery-ratio allowance for selected tiers only",
          "[ai][campaign][city][policy]") {
    // Level 0 is the accepted matrix in every cell.
    for (const auto& cell : kAcceptedMatrix) {
        const Limits off = limits(cell.difficulty, cell.level, funded(kUncappedAllowance), false, 0, 0);
        REQUIRE(off.harvesters == cell.harvesters);
        REQUIRE(off.sharedZoneCap == cell.sharedZoneCap);
    }
    // H017: Hard level 7 with two refineries, allowance 2 x 2 = 4.
    const Baseline h017 = funded(4);
    REQUIRE(limits(Hard, 7, h017, false, 0, 0).harvesters == 1);
    REQUIRE(limits(Hard, 7, h017, false, 0, 1).harvesters == 4);
    REQUIRE(limits(Hard, 7, h017, false, 0, 2).harvesters == 4);
    // Brutal changes only at level 2; Easy and Medium never change.
    const Baseline brutal = funded(7);
    REQUIRE(limits(Brutal, 7, brutal, false, 0, 1).harvesters == 3);
    REQUIRE(limits(Brutal, 7, brutal, false, 0, 2).harvesters == 7);
    for (const int difficulty : {int(Easy), int(Medium)})
        for (int level = 1; level <= 9; ++level)
            for (const int ratio : {1, 2}) {
                const Limits on = limits(difficulty, level, h017, false, 0, ratio);
                const Limits off = limits(difficulty, level, h017, false, 0, 0);
                REQUIRE(on.harvesters == off.harvesters);
                REQUIRE(on.sharedZoneCap == off.sharedZoneCap);
            }
    // Zones are unchanged; level 1, scripted houses, post-spice and options ceilings still win.
    REQUIRE(limits(Hard, 7, h017, false, 0, 1).sharedZoneCap == 20);
    REQUIRE(limits(Hard, 1, h017, false, 0, 2).harvesters == 0);
    REQUIRE(limits(Hard, 7, Baseline{0, 4}, false, 0, 2).harvesters == 0);
    REQUIRE(limits(Hard, 7, h017, true, 0, 2).harvesters == 0);
    REQUIRE(limits(Hard, 7, h017, true, 0, 2).sharedZoneCap == kPostSpiceHardZoneCap);
    REQUIRE(limits(Hard, 7, h017, false, 3, 1).harvesters == 3);
    REQUIRE_FALSE(ratioRestored(Hard, 0));
    REQUIRE(ratioRestored(Hard, 1));
    REQUIRE_FALSE(ratioRestored(Brutal, 1));
    REQUIRE(ratioRestored(Brutal, 2));
    REQUIRE_FALSE(ratioRestored(Medium, 2));
    REQUIRE_FALSE(ratioRestored(Easy, 2));
}

TEST_CASE("CampaignEnemyHarvesterRatio level 3 floors Hard workers at the Medium row only",
          "[ai][campaign][city][policy]") {
    // H017: Hard level 7, allowance 4: matrix 1, Medium row 2, restored allowance 4.
    const Baseline h017 = funded(4);
    REQUIRE(limits(Hard, 7, h017, false, 0, 3).harvesters == 2);
    REQUIRE(limits(Hard, 7, h017, false, 0, 3).sharedZoneCap == 20);
    // A small original budget keeps Medium's small row, so Hard stays at 1.
    REQUIRE(limits(Hard, 7, funded(2), false, 0, 3).harvesters == 1);
    // Levels 2-4 already give Hard at least Medium's workers: unchanged.
    for (int level = 1; level <= 4; ++level) {
        const Limits on = limits(Hard, level, funded(kUncappedAllowance), false, 0, 3);
        const Limits off = limits(Hard, level, funded(kUncappedAllowance), false, 0, 0);
        REQUIRE(on.harvesters == off.harvesters);
        REQUIRE(on.sharedZoneCap == off.sharedZoneCap);
    }
    // Hard is never below Medium, never above its own allowance; Easy, Medium and Brutal never change.
    for (int allowance = 1; allowance <= 8; ++allowance)
        for (int level = 1; level <= 9; ++level) {
            const Baseline baseline = funded(allowance);
            const Limits hard = limits(Hard, level, baseline, false, 0, 3);
            REQUIRE(hard.harvesters >= limits(Medium, level, baseline, false, 0, 0).harvesters);
            REQUIRE(hard.harvesters <= allowance);
            REQUIRE(hard.sharedZoneCap == limits(Hard, level, baseline, false, 0, 0).sharedZoneCap);
            for (const int difficulty : {int(Easy), int(Medium), int(Brutal)}) {
                const Limits on = limits(difficulty, level, baseline, false, 0, 3);
                const Limits off = limits(difficulty, level, baseline, false, 0, 0);
                REQUIRE(on.harvesters == off.harvesters);
                REQUIRE(on.sharedZoneCap == off.sharedZoneCap);
            }
        }
    // Options ceiling and post-spice still win.
    REQUIRE(limits(Hard, 7, h017, false, 1, 3).harvesters == 1);
    REQUIRE(limits(Hard, 7, h017, true, 0, 3).harvesters == 0);
    REQUIRE_FALSE(ratioRestored(Hard, 3));
    REQUIRE_FALSE(ratioRestored(Brutal, 3));
    REQUIRE(mediumFloor(Hard, 3));
    REQUIRE_FALSE(mediumFloor(Hard, 1));
    REQUIRE_FALSE(mediumFloor(Brutal, 3));
    REQUIRE_FALSE(mediumFloor(Medium, 3));
}

TEST_CASE("Restored workers raise the income goal by their reference harvest",
          "[ai][campaign][city][policy]") {
    const Baseline h017 = funded(4);
    REQUIRE(incomeGoalPerMinute(h017, 1, 1) == totalIncomeGoalPerMinute(h017));
    REQUIRE(incomeGoalPerMinute(h017, 1, 4) == totalIncomeGoalPerMinute(h017) + 3 * kReferenceIncomePerHarvester);
    // The tax share of the goal is the matrix's: unchanged by the extra workers.
    REQUIRE(incomeGoalPerMinute(h017, 1, 4) - kReferenceIncomePerHarvester * 4
            == netTaxGoalPerMinute(h017, 1));
    REQUIRE(incomeGoalPerMinute(h017, 0, 0) == totalIncomeGoalPerMinute(h017));
    REQUIRE(incomeGoalPerMinute(Baseline{0, 4}, 1, 4) == 0);
}
