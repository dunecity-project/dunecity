#include <players/QuantBotCityCampaignSkills.h>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <set>
#include <string>

using namespace QuantBotCityCampaignSkills;

namespace {

constexpr int kDefend = 4;

bool ladderSkill(Skill skill, int difficulty) {
    return hasSkill(skill, difficulty, kDefaultTiers[skill], true);
}

} // namespace

TEST_CASE("City campaign skill ladder: each tier keeps the skills of the tier below", "[quantbot][citycampaign]") {
    for (int skill = 0; skill < kSkillCount; ++skill)
        for (int difficulty = Easy; difficulty < Brutal; ++difficulty)
            if (ladderSkill(Skill(skill), difficulty)) CHECK(ladderSkill(Skill(skill), difficulty + 1));
}

TEST_CASE("City campaign skill ladder: the default tiers", "[quantbot][citycampaign]") {
    // Easy: none of the ladder's skills.
    for (int skill = 0; skill < kSkillCount; ++skill) CHECK_FALSE(ladderSkill(Skill(skill), Easy));
    // Medium keeps itself safe.
    CHECK(ladderSkill(TreasuryRelease, Medium));
    CHECK(ladderSkill(CityServices, Medium));
    CHECK_FALSE(ladderSkill(LandValueSiting, Medium));
    CHECK_FALSE(ladderSkill(FactoriesFirst, Medium));
    // Hard plans the city.
    CHECK(ladderSkill(LandValueSiting, Hard));
    CHECK(ladderSkill(PalaceForTax, Hard));
    CHECK(ladderSkill(FactoriesFirst, Hard));
    CHECK_FALSE(ladderSkill(EarlyAttack, Hard));
    // Brutal plays the whole map.
    CHECK(ladderSkill(EarlyAttack, Brutal));
    // Never-tier skills stay off for every tier.
    CHECK_FALSE(ladderSkill(GrowthTax, Brutal));
    CHECK_FALSE(ladderSkill(HarvesterTargets, Brutal));
}

TEST_CASE("City campaign skill ladder: native behaviour outside the ladder", "[quantbot][citycampaign]") {
    for (int difficulty = Easy; difficulty <= kDefend; ++difficulty) {
        for (Skill skill : {TreasuryRelease, CityServices, LandValueSiting, PalaceForTax}) {
            CHECK(hasSkill(skill, difficulty, Never, false));
            CHECK(hasSkill(skill, difficulty, Brutal, false));
        }
        CHECK(hasSkill(EarlyAttack, difficulty, Easy, false) == (difficulty == Brutal));
        // A campaign fix: other city games keep the dedicated growth yard zoning.
        CHECK_FALSE(hasSkill(FactoriesFirst, difficulty, Easy, false));
        CHECK_FALSE(hasSkill(GrowthTax, difficulty, Easy, false));
        CHECK_FALSE(hasSkill(HarvesterTargets, difficulty, Easy, false));
    }
    // Defend is outside Easy..Brutal: native even with the ladder on.
    CHECK(hasSkill(TreasuryRelease, kDefend, Never, true));
    CHECK_FALSE(hasSkill(GrowthTax, kDefend, Easy, true));
}

TEST_CASE("City campaign skill ladder: a configured tier moves a skill", "[quantbot][citycampaign]") {
    CHECK(hasSkill(GrowthTax, Hard, Hard, true));
    CHECK_FALSE(hasSkill(GrowthTax, Medium, Hard, true));
    CHECK(hasSkill(EarlyAttack, Easy, Easy, true));
    CHECK_FALSE(hasSkill(TreasuryRelease, Brutal, Never, true));
}

TEST_CASE("City campaign skill ladder: one distinct config key per skill", "[quantbot][citycampaign]") {
    std::set<std::string> keys;
    for (const char* key : kTierKeys) {
        REQUIRE(key != nullptr);
        CHECK(std::strncmp(key, "CityCampaignSkill", 17) == 0);
        keys.insert(key);
    }
    CHECK(keys.size() == size_t(kSkillCount));
    for (int tier : kDefaultTiers) {
        CHECK(tier >= Easy);
        CHECK(tier <= Never);
    }
}

TEST_CASE("City campaign skill ladder: thinking speed", "[quantbot][citycampaign]") {
    const std::array<int, 4> table{{4, 3, 2, 1}};
    CHECK(thinkPasses(Easy, table, true) == 4);
    CHECK(thinkPasses(Brutal, table, true) == 1);
    CHECK(thinkPasses(Easy, table, false) == 1);
    CHECK(thinkPasses(4, table, true) == 1); // Defend keeps every pass
    const std::array<int, 4> wild{{0, 99, 2, -3}};
    CHECK(thinkPasses(Easy, wild, true) == 1);
    CHECK(thinkPasses(Medium, wild, true) == kMaxThinkPasses);
    // Each tier thinks at least as fast as the one below; Brutal on every pass.
    for (int tier = Easy; tier < Brutal; ++tier) CHECK(kDefaultThinkPasses[tier] >= kDefaultThinkPasses[tier + 1]);
    CHECK(kDefaultThinkPasses[Brutal] == 1);
}
