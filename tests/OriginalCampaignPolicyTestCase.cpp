#include <players/OriginalCampaignPolicy.h>
#include <misc/Random.h>
#include <catch2/catch_test_macros.hpp>
#include <set>

TEST_CASE("Original campaign production uses original order and random replacement", "[campaign-ai]") {
    using namespace OriginalCampaignPolicy;
    const auto available = [](int item) { return item == Unit_Launcher || item == Unit_Tank || item == Unit_SiegeTank; };
    REQUIRE(pickUnit(available, [] { return 1; }) == Unit_SiegeTank);
    // Original order is Launcher, Tank, SiegeTank. A late low-priority roll can
    // replace a Devastator with a Quad, unlike a simple weighted-best selector.
    REQUIRE(pickUnit([](int item) { return item == Unit_Devastator || item == Unit_Quad; }, [] { return 0; }) == Unit_Quad);
    REQUIRE(pickUnit([](int) { return false; }, [] { return 0; }) == ItemID_Invalid);
    REQUIRE(buildPriority(Unit_Soldier) == 10);
    REQUIRE(buildPriority(Unit_Trooper) == 20);
    Random first(9602), replay(9602);
    std::set<int> produced;
    for (int n = 0; n < 1000; ++n) {
        const auto choices = [](int item) { return item == Unit_Devastator || item == Unit_Quad; };
        const int item = pickUnit(choices, [&] { return first.rand(0, 255); });
        REQUIRE(item == pickUnit(choices, [&] { return replay.rand(0, 255); }));
        produced.insert(item);
    }
    REQUIRE(produced == std::set<int>{Unit_Devastator, Unit_Quad});
}

TEST_CASE("Original campaign structure and unit targeting differ at distance", "[campaign-ai]") {
    REQUIRE(OriginalCampaignPolicy::targetPriority(300, 3, true) == 100);
    REQUIRE(OriginalCampaignPolicy::targetPriority(300, 3, false) == 101);
    REQUIRE(OriginalCampaignPolicy::targetPriority(300, 0, true) == 300);
}
