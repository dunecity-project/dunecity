#pragma once

#include <array>

/// DuneCity campaign skill ladder (Stefan, 2026-11-20: "Yes, build it").
///
/// On the low-spice campaign maps no difficulty reaches its army or city ceiling, so the tiers played
/// the same game: a Brutal helper beat a Hard enemy 114 times in 160 where a Hard helper won 116.
/// Same-tier games losing 60% and one-tier-lower games winning half cannot both hold while the helper
/// tiers are equal (docs/city-campaign.md, "The skill ladder"). Each skill below is a decision that
/// turns the same income into more power; a tier plays with every skill whose tier is at or below its
/// own. Scope: DuneCity campaign missions only, helpers and enemies alike, while
/// [Force Concentration] CityCampaignSkillLadder is on. Everywhere else every QuantBot keeps its
/// native behaviour (nativeSkill).
namespace QuantBotCityCampaignSkills {

/// Mirrors QuantBot::Difficulty for Easy..Brutal; Never keeps a skill from every tier.
enum Tier { Easy = 0, Medium = 1, Hard = 2, Brutal = 3, Never = 4 };

enum Skill {
    TreasuryRelease = 0, ///< a house saving for a credits goal spends the bank when its base is threatened
    CityServices,        ///< crime or land-value goal: police beside homes and the land-value police budget
    LandValueSiting,     ///< a Palace or Stadium is sited among its own homes and shops
    PalaceForTax,        ///< a Palace is ordered as a land-value investment once 12 homes and shops stand
    FactoriesFirst,      ///< a funded factory backlog takes a construction yard before zoning does
    EarlyAttack,         ///< attacks at CityCampaignBrutalAttackPercent of its army ceiling, not 25%
    GrowthTax,           ///< 3% tax while the city is below the growth population, then the default
    HarvesterTargets,    ///< ground combat shoots harvesters first, then artillery (TargetPriorityLevel 2)
    kSkillCount
};

/// Lowest tier that plays each skill. Easy: none; Medium: keeps itself safe; Hard: plans the city;
/// Brutal: plays the whole map.
constexpr std::array<int, kSkillCount> kDefaultTiers{{
    Medium, // TreasuryRelease
    Medium, // CityServices
    Hard,   // LandValueSiting
    Hard,   // PalaceForTax
    Hard,   // FactoriesFirst
    Brutal, // EarlyAttack
    Never,  // GrowthTax
    Never,  // HarvesterTargets
}};

/// [Force Concentration] keys holding each skill's tier (0 Easy .. 3 Brutal, 4 never).
constexpr std::array<const char*, kSkillCount> kTierKeys{{
    "CityCampaignSkillTreasuryRelease", "CityCampaignSkillCityServices", "CityCampaignSkillLandValueSiting",
    "CityCampaignSkillPalaceForTax", "CityCampaignSkillFactoriesFirst", "CityCampaignSkillEarlyAttack",
    "CityCampaignSkillGrowthTax", "CityCampaignSkillHarvesterTargets",
}};

/// Behaviour outside the ladder. Skills the AI already had stay on for every tier; EarlyAttack stays
/// Brutal's own; GrowthTax and HarvesterTargets are new and stay off.
inline bool nativeSkill(Skill skill, int difficulty) {
    switch (skill) {
        case EarlyAttack: return difficulty == Brutal;
        case GrowthTax:
        case HarvesterTargets: return false;
        default: return true;
    }
}

/// Whether a QuantBot of this difficulty plays the skill. A tier outside Easy..Brutal (Defend) keeps
/// its native behaviour; so does a ladder that is switched off.
inline bool hasSkill(Skill skill, int difficulty, int tier, bool ladder) {
    if (!ladder || difficulty < Easy || difficulty > Brutal) return nativeSkill(skill, difficulty);
    return tier <= difficulty;
}

} // namespace QuantBotCityCampaignSkills
