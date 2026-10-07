#ifndef SPECIAL_UNIT_POLICY_H
#define SPECIAL_UNIT_POLICY_H

/*
    Which special heavy unit to build, once the allocator has already decided
    that the special share is the group to spend on.

    The outer allocation is unchanged. Devastator, Sonic Tank and Deviator share
    one learned share and one committed value, which is correct - they compete
    for the same slot in the composition - but it also means the outer
    largest-deficit comparison sees three identical deficits and keeps the first
    candidate in array order. In the reviewed match that was the Devastator in
    every logged funded decision where both it and the Sonic Tank were available
    and affordable, which is an artefact of the array order rather than a
    judgement about either unit.

    This picks within the group on each type's own observed combat return per
    credit lost, with a bounded fading prior so an untried type is not starved by
    types that happen to have results already. With no evidence at all the three
    are genuinely tied, and the tie is broken towards the type the house owns
    least of: that rotates production across the available specials, which is
    what produces the evidence the score then uses. No type is globally
    preferred, and no counter weight is invented from a single match.
*/

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <players/UnitMixPolicy.h>

namespace SpecialUnitPolicy {

/// One special type as the allocator sees it. `rewardMilli` and `lossMilli` are
/// this house's lifetime measured combat reward and lost value for that type, in
/// milli units, exactly as the unit-mix learning uses them.
struct Candidate {
    uint32_t item = 0;
    int price = 0;
    int64_t rewardMilli = 0;
    int64_t lossMilli = 0;
    int64_t committedValue = 0; ///< Value of this type the house already owns.
    bool available = false;     ///< Buildable here, right now.
    bool affordable = false;    ///< Price fits the cash and the army budget.
};

constexpr size_t kSpecials = 3;
using Candidates = std::array<Candidate, kSpecials>;

/// Raw measured score, identical in form to the strategic unit-mix score so the
/// two cannot disagree about what "performing well" means.
inline int64_t rawScore(const Candidate& candidate) {
    return UnitMixPolicy::performanceScore(candidate.rewardMilli, candidate.lossMilli,
                                           std::max(1, candidate.price) * 1000);
}

/// Scores blended towards the average of the types that do have evidence, with
/// the weight of the prior fading as evidence accumulates. Mirrors
/// UnitMixPolicy::exploredScores for this three-wide group.
inline std::array<int64_t, kSpecials> exploredScores(const Candidates& candidates) {
    std::array<int64_t, kSpecials> scores{};
    int64_t prior = 0, counted = 0;
    for (size_t i = 0; i < kSpecials; ++i) {
        if (!candidates[i].available) continue;
        scores[i] = rawScore(candidates[i]);
        if (scores[i] > 0) { prior += scores[i]; ++counted; }
    }
    prior = counted ? prior / counted : 1000000;
    std::array<int64_t, kSpecials> result{};
    for (size_t i = 0; i < kSpecials; ++i) {
        if (!candidates[i].available) continue;
        const int64_t evidence = std::max<int64_t>(0, candidates[i].rewardMilli)
                               + std::max<int64_t>(0, candidates[i].lossMilli);
        const int64_t uncertainty = int64_t(std::max(1, candidates[i].price)) * 1000 * 4;
        result[i] = (scores[i] * evidence + prior * uncertainty) / (evidence + uncertainty);
    }
    return result;
}

/// Index of the special to build, or -1 when none is available and affordable.
///
/// Deterministic total order: higher explored score, then the type the house
/// owns least of (so an untested type is actually tried), then the cheaper
/// price, then the lower item id. Array position is never the deciding factor.
inline int select(const Candidates& candidates) {
    const auto scores = exploredScores(candidates);
    int best = -1;
    for (size_t i = 0; i < kSpecials; ++i) {
        const auto& candidate = candidates[i];
        if (!candidate.available || !candidate.affordable || candidate.price <= 0) continue;
        if (best < 0) { best = int(i); continue; }
        const auto& incumbent = candidates[best];
        if (scores[i] != scores[best]) {
            if (scores[i] > scores[best]) best = int(i);
            continue;
        }
        if (candidate.committedValue != incumbent.committedValue) {
            if (candidate.committedValue < incumbent.committedValue) best = int(i);
            continue;
        }
        if (candidate.price != incumbent.price) {
            if (candidate.price < incumbent.price) best = int(i);
            continue;
        }
        if (candidate.item < incumbent.item) best = int(i);
    }
    return best;
}

/// Why the chosen type won, for telemetry. Scores are reported alongside.
inline const char* selectionReason(const Candidates& candidates, int selected) {
    if (selected < 0) return "no_affordable_special";
    const auto scores = exploredScores(candidates);
    bool tiedScore = false;
    for (size_t i = 0; i < kSpecials; ++i) {
        if (int(i) == selected || !candidates[i].available || !candidates[i].affordable) continue;
        if (scores[i] == scores[selected]) tiedScore = true;
    }
    if (!tiedScore) return "measured_return_per_loss";
    return "exploration_least_owned";
}

} // namespace SpecialUnitPolicy

#endif // SPECIAL_UNIT_POLICY_H
