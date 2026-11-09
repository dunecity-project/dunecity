/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QUANTBOTCITYCAMPAIGNPOLICY_H
#define QUANTBOTCITYCAMPAIGNPOLICY_H

#include <algorithm>

/// Pure decision table for the DuneCity city-sim CAMPAIGN ENEMY economy.
///
/// Vanilla, custom and human-allied behaviour never consults this policy.
/// Nothing here grants buildings, units or cash: every value is a ceiling or
/// a planning reference. Zone counts are TOTAL R+I+C (placed plus queued);
/// the bot keeps choosing its own mix inside that single shared allowance.
namespace QuantBotCityCampaignPolicy {

/// Mirrors QuantBot::Difficulty so the table stays testable without the engine.
enum Difficulty { Easy = 0, Medium = 1, Hard = 2, Brutal = 3, Defend = 4 };

/// Design reference only: the accepted matrix is expressed at 300 credits per
/// minute per funded worker. It is never paid out and never applied per zone.
constexpr int kReferenceIncomePerHarvester = 300;
/// The accepted campaign goal is 150% of the original combined budget.
constexpr int kGoalIncomePerOriginalHarvester = 450;

/// Confirmation window before the map counts as spice-exhausted.
constexpr int kPostSpiceConfirmSeconds = 30;

/// Post-spice TOTAL R+I+C ceilings (not extra zones). Easy/Medium unchanged.
constexpr int kPostSpiceHardZoneCap = 24;
constexpr int kPostSpiceBrutalZoneCap = 40;

struct Limits {
    int harvesters = 0;    ///< worker target ceiling, never above the original allowance
    int sharedZoneCap = 0; ///< TOTAL residential+commercial+industrial, queued included
};

/// Original mission economy, captured before any AI top-up or grant.
struct Baseline {
    int refineries = 0;    ///< refineries present at mission start (0 => scripted, no economy)
    int allowance = 0;     ///< original vanilla harvester allowance for this house
};

/// The matrix is indexed by the mission's display level 1..9.
inline int displayLevel(int techLevel) {
    return std::min(9, std::max(1, techLevel));
}

/// Accepted normal (spice still on the map) matrix.
inline Limits normalLimits(int difficulty, int level) {
    const int band = level <= 1 ? 1 : level <= 2 ? 2 : level <= 3 ? 3 : level <= 4 ? 4 : 5;
    switch (difficulty) {
        case Easy:
            return band == 1 ? Limits{0, 0} : band == 5 ? Limits{1, 8} : Limits{1, 4};
        case Medium:
            return band == 1 ? Limits{0, 0} : band == 2 ? Limits{2, 5}
                 : band == 5 ? Limits{2, 16} : Limits{1, 8};
        case Hard:
            return band == 1 ? Limits{0, 0} : band <= 3 ? Limits{3, 12}
                 : band == 4 ? Limits{2, 16} : Limits{1, 20};
        case Brutal:
            return band == 1 ? Limits{0, 0} : band == 2 ? Limits{6, 18}
                 : band == 3 ? Limits{5, 20} : band == 4 ? Limits{4, 24} : Limits{3, 28};
        default:
            return Limits{0, 0};
    }
}

/// Levels 5-9 also host houses with an original small budget (one Easy or two
/// Medium workers). Those keep the smaller row instead of the large-house row.
inline Limits smallBudgetLimits(int difficulty, int level, int allowance) {
    Limits limits = normalLimits(difficulty, level);
    if (level < 5) return limits;
    if (difficulty == Easy && allowance > 0 && allowance <= 1) return Limits{1, 4};
    if (difficulty == Medium && allowance > 0 && allowance <= 2) return Limits{1, 8};
    return limits;
}

/// CampaignEnemyHarvesterRatio (hashed, default 0): which tiers use their original
/// refinery-ratio allowance as the worker ceiling instead of the matrix row.
/// 0 = none (accepted matrix), 1 = Hard, 2 = Hard and Brutal, 3 = see mediumFloor().
/// Easy and Medium never change.
inline bool ratioRestored(int difficulty, int ratioLevel) {
    return ((ratioLevel == 1 || ratioLevel == 2) && difficulty == Hard) || (ratioLevel == 2 && difficulty == Brutal);
}

/// Level 3: Hard keeps its matrix row but never plans fewer workers than the Medium
/// row at the same level and allowance (the accepted matrix gives Hard 1 and Medium 2
/// at levels 5-9). Brutal, Easy and Medium never change.
inline bool mediumFloor(int difficulty, int ratioLevel) {
    return ratioLevel == 3 && difficulty == Hard;
}

/// Full ceiling for one campaign enemy house.
///
/// @param postSpice   map-wide spice confirmed exhausted (see kPostSpiceConfirmSeconds)
/// @param optionsHarvesterLimit  Game Options / engine worker ceiling, <=0 when unset
/// @param ratioLevel  CampaignEnemyHarvesterRatio level, 0 = accepted matrix
inline Limits limits(int difficulty, int level, const Baseline& baseline,
                     bool postSpice, int optionsHarvesterLimit = 0, int ratioLevel = 0) {
    // Scripted houses with no original economic base stay at zero: no workers,
    // no zones, no free buildings. Level 1 has no working enemy economy at all.
    if (baseline.refineries <= 0 || baseline.allowance <= 0 || level <= 1) return Limits{0, 0};

    Limits result = smallBudgetLimits(difficulty, level, baseline.allowance);
    if (ratioRestored(difficulty, ratioLevel)) result.harvesters = baseline.allowance;
    else if (mediumFloor(difficulty, ratioLevel))
        result.harvesters = std::max(result.harvesters, smallBudgetLimits(Medium, level, baseline.allowance).harvesters);

    // A target may never exceed what the original mission already allowed, nor
    // the resource/options ceilings the engine applies.
    result.harvesters = std::min(result.harvesters, baseline.allowance);
    if (optionsHarvesterLimit > 0) result.harvesters = std::min(result.harvesters, optionsHarvesterLimit);

    if (postSpice) {
        result.harvesters = 0;
        // Totals, not additions. Easy/Medium receive no extra zone allowance.
        if (difficulty == Hard) result.sharedZoneCap = std::max(result.sharedZoneCap, kPostSpiceHardZoneCap);
        else if (difficulty == Brutal) result.sharedZoneCap = std::max(result.sharedZoneCap, kPostSpiceBrutalZoneCap);
    }
    return result;
}

/// Planning references. The budget baseline is fixed from the ORIGINAL mission
/// allowance and never rebases as spice depletes.
inline int baselineIncomePerMinute(const Baseline& baseline) {
    return baseline.refineries <= 0 ? 0 : kReferenceIncomePerHarvester * std::max(0, baseline.allowance);
}

inline int totalIncomeGoalPerMinute(const Baseline& baseline) {
    return baseline.refineries <= 0 ? 0 : kGoalIncomePerOriginalHarvester * std::max(0, baseline.allowance);
}

/// Goal with restored workers: each worker above the matrix row adds its
/// reference harvest, so the zone/tax plan keeps the matrix's tax share and the
/// extra harvest is additive. Equals totalIncomeGoalPerMinute() at level 0.
inline int incomeGoalPerMinute(const Baseline& baseline, int matrixHarvesters, int restoredHarvesters) {
    const int goal = totalIncomeGoalPerMinute(baseline);
    return goal <= 0 ? 0 : goal + kReferenceIncomePerHarvester * std::max(0, restoredHarvesters - matrixHarvesters);
}

/// Tax share of the goal: the whole goal minus the planned harvest share.
/// Post-spice the worker target is zero, so the same original goal remains,
/// without a second 50% and without rebasing against zero.
inline int netTaxGoalPerMinute(const Baseline& baseline, int harvesterTarget) {
    return std::max(0, totalIncomeGoalPerMinute(baseline)
        - kReferenceIncomePerHarvester * std::max(0, harvesterTarget));
}

/// Discretionary growth stops on the first of: the shared physical ceiling, or
/// a measured/forecast income rate that already meets the goal. Developing and
/// queued income counts through the caller's measured rate and zone count.
/// Maintenance, power, roads and services are not discretionary and are never
/// gated here.
inline bool allowsDiscretionaryExpansion(int zonesIncludingQueued, int sharedZoneCap,
                                         int measuredIncomePerMinute, int goalPerMinute) {
    if (sharedZoneCap <= 0 || zonesIncludingQueued >= sharedZoneCap) return false;
    return goalPerMinute <= 0 || measuredIncomePerMinute < goalPerMinute;
}

/// Map-wide spice is only "gone" after a fresh zero has held for the
/// confirmation window. Blooms that restore spice end the post-spice state, so
/// new expansion stops; zones already built are never demolished.
inline bool postSpiceConfirmed(int lastCalculatedSpice, int ownedHarvesterCargo,
                               unsigned zeroSinceCycle, unsigned nowCycle, unsigned confirmationCycles) {
    if (lastCalculatedSpice > 0 || ownedHarvesterCargo > 0) return false;
    if (zeroSinceCycle > nowCycle) return false;
    return nowCycle - zeroSinceCycle >= confirmationCycles;
}

} // namespace QuantBotCityCampaignPolicy

#endif // QUANTBOTCITYCAMPAIGNPOLICY_H
