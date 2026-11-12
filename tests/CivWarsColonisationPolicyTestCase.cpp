#include <catch2/catch_test_macros.hpp>

#include <players/QuantBotColonisationPolicy.h>
#include <players/RockExpansionPolicy.h>

#include <vector>

namespace {

struct IslandSurvey {
    static constexpr int w=80,h=50;
    std::vector<RockExpansionPolicy::Tile> tiles=std::vector<RockExpansionPolicy::Tile>(w*h);
    std::vector<int> starts{index(8,8)},enemies,reserved;

    IslandSurvey() {
        for(auto& tile:tiles) { tile.free=true; tile.walkable=true; }
        rock(6,6,4,4);
        auto& home=tiles[index(6,6)];
        home.owned=true; home.free=false; home.walkable=false;
    }

    static int index(int x,int y) { return y*w+x; }

    void rock(int x,int y,int width,int height) {
        for(int dy=0;dy<height;++dy) for(int dx=0;dx<width;++dx)
            tiles[index(x+dx,y+dy)].rock=true;
    }

    RockExpansionPolicy::Site legacyChoice() const {
        return RockExpansionPolicy::choose(w,h,tiles,starts,enemies,reserved,index(8,8));
    }

    RockExpansionPolicy::Site islandChoice() const {
        return RockExpansionPolicy::choose(w,h,tiles,starts,enemies,reserved,index(8,8),24);
    }
};

QuantBotColonisationPolicy::Demand roomyCity() {
    QuantBotColonisationPolicy::Demand demand;
    demand.citySim=true;
    demand.customGame=true;
    demand.siteAvailable=true;
    demand.freeFootprints=QuantBotColonisationPolicy::kCrampedFootprints+2;
    demand.yards=1;
    demand.yardsOnFormation=1;
    return demand;
}

}

TEST_CASE("Rock expansion keeps its legacy 48-tile minimum by default",
          "[quantbot][civwars][colonisation][expansion]") {
    IslandSurvey survey;
    survey.rock(40,20,6,8);
    const auto exact=survey.legacyChoice();
    REQUIRE(exact.valid());
    CHECK(exact.room==48);

    survey.tiles[IslandSurvey::index(45,27)].rock=false;
    CHECK_FALSE(survey.legacyChoice().valid());
    CHECK(survey.islandChoice().valid());
}

TEST_CASE("The Civ Wars room override admits the generated 5x5 island core",
          "[quantbot][civwars][colonisation][expansion]") {
    IslandSurvey survey;
    survey.rock(40,20,5,5);
    CHECK_FALSE(survey.legacyChoice().valid());
    const auto island=survey.islandChoice();
    REQUIRE(island.valid());
    CHECK(island.room==25);
    CHECK(island.x>=40);
    CHECK(island.x<44);
    CHECK(island.y>=20);
    CHECK(island.y<24);
}

TEST_CASE("The 24-tile override still requires enough contiguous and local room",
          "[quantbot][civwars][colonisation][expansion]") {
    IslandSurvey survey;
    SECTION("Exactly 24 free tiles are enough") {
        survey.rock(40,20,4,6);
        const auto site=survey.islandChoice();
        REQUIRE(site.valid());
        CHECK(site.room==24);
        survey.tiles[IslandSurvey::index(43,25)].rock=false;
        CHECK_FALSE(survey.islandChoice().valid());
    }
    SECTION("Separate tiny components cannot pool their room") {
        survey.rock(40,20,4,3);
        survey.rock(48,20,4,3);
        CHECK_FALSE(survey.islandChoice().valid());
    }
    SECTION("A long strip cannot host the yard footprint") {
        survey.rock(30,20,30,1);
        CHECK_FALSE(survey.islandChoice().valid());
    }
    SECTION("A large component with no roomy local footprint is insufficient") {
        survey.rock(20,20,45,1);
        survey.rock(20,20,2,2);
        CHECK_FALSE(survey.islandChoice().valid());
    }
}

TEST_CASE("Small strategic islands retain safety occupancy and reservation checks",
          "[quantbot][civwars][colonisation][expansion]") {
    IslandSurvey survey;
    survey.rock(40,20,5,5);
    REQUIRE(survey.islandChoice().valid());

    SECTION("Danger or recent-loss exclusions consume safe local room") {
        survey.tiles[IslandSurvey::index(44,24)].unsafe=true;
        survey.tiles[IslandSurvey::index(43,24)].unsafe=true;
        CHECK_FALSE(survey.islandChoice().valid());
    }
    SECTION("An owned or allied building claims its entire formation") {
        auto& building=survey.tiles[IslandSurvey::index(44,24)];
        building.owned=true; building.free=false; building.walkable=false;
        CHECK_FALSE(survey.islandChoice().valid());
    }
    SECTION("An allied or own MCV reservation claims its entire formation") {
        survey.reserved.push_back(IslandSurvey::index(44,24));
        CHECK_FALSE(survey.islandChoice().valid());
    }
    SECTION("A vehicle route must still exist") {
        for(int y=0;y<IslandSurvey::h;++y) survey.tiles[IslandSurvey::index(30,y)].walkable=false;
        CHECK_FALSE(survey.islandChoice().valid());
    }
    SECTION("Unsafe ground cannot supply a vehicle route") {
        for(int y=0;y<IslandSurvey::h;++y) survey.tiles[IslandSurvey::index(30,y)].unsafe=true;
        CHECK_FALSE(survey.islandChoice().valid());
    }
    SECTION("Observed enemies retain the twelve-tile clearance") {
        survey.enemies.push_back(IslandSurvey::index(36,20));
        CHECK_FALSE(survey.islandChoice().valid());
        survey.enemies.front()=IslandSurvey::index(28,20);
        const auto clear=survey.islandChoice();
        REQUIRE(clear.valid());
        CHECK(clear.clearance>=12);
    }
}

TEST_CASE("Island claims retain nearest-main-base ranking and eventually exhaust demand",
          "[quantbot][civwars][colonisation][expansion]") {
    IslandSurvey survey;
    survey.rock(40,2,5,5);
    survey.rock(40,20,5,5);
    survey.rock(40,38,5,5);
    const auto nearest=survey.islandChoice();
    REQUIRE(nearest.valid());
    CHECK(nearest.y<7);

    // A reservation on the far corner blocks the whole nearest island, not
    // merely the yard footprint: the next teammate must choose another lane.
    survey.reserved.push_back(IslandSurvey::index(44,6));
    const auto middle=survey.islandChoice();
    REQUIRE(middle.valid());
    CHECK(middle.y>=20);
    CHECK(middle.y<24);

    auto& held=survey.tiles[IslandSurvey::index(44,24)];
    held.owned=true; held.free=false; held.walkable=false;
    const auto last=survey.islandChoice();
    REQUIRE(last.valid());
    CHECK(last.y>=38);
    survey.reserved.push_back(IslandSurvey::index(last.x,last.y));
    CHECK_FALSE(survey.islandChoice().valid());

    auto demand=roomyCity();
    demand.strategicSiteWanted=true;
    demand.siteAvailable=survey.islandChoice().valid();
    CHECK_FALSE(QuantBotColonisationPolicy::due(demand));
    CHECK_FALSE(QuantBotColonisationPolicy::missionDue(demand));
}

TEST_CASE("A strategic site requests one colonist without declaring home built out",
          "[quantbot][civwars][colonisation]") {
    auto demand=roomyCity();
    REQUIRE_FALSE(demand.strategicSiteWanted);
    CHECK_FALSE(QuantBotColonisationPolicy::builtOut(demand));
    CHECK_FALSE(QuantBotColonisationPolicy::due(demand));
    CHECK_FALSE(QuantBotColonisationPolicy::missionDue(demand));

    demand.strategicSiteWanted=true;
    CHECK(QuantBotColonisationPolicy::due(demand));
    CHECK(QuantBotColonisationPolicy::missionDue(demand));
    CHECK_FALSE(QuantBotColonisationPolicy::builtOut(demand));
    CHECK(demand.yardsOnFormation<QuantBotColonisationPolicy::kYardsPerFormation);
}

TEST_CASE("Strategic colonists retain queue and game-option yard ceilings",
          "[quantbot][civwars][colonisation]") {
    auto demand=roomyCity();
    demand.strategicSiteWanted=true;
    demand.mcvsIncludingQueued=1;
    CHECK_FALSE(QuantBotColonisationPolicy::due(demand));
    // A bought MCV is the colonist, not a reason to cancel its existing mission.
    CHECK(QuantBotColonisationPolicy::missionDue(demand));
    demand.mcvsIncludingQueued=2;
    CHECK_FALSE(QuantBotColonisationPolicy::due(demand));

    demand.mcvsIncludingQueued=0;
    demand.yardLimit=demand.yards;
    CHECK_FALSE(QuantBotColonisationPolicy::due(demand));
    CHECK_FALSE(QuantBotColonisationPolicy::missionDue(demand));
    demand.yardLimit=demand.yards+1;
    CHECK(QuantBotColonisationPolicy::due(demand));
    CHECK(QuantBotColonisationPolicy::missionDue(demand));
}

TEST_CASE("Strategic demand cannot bypass a closed site or the custom city boundary",
          "[quantbot][civwars][colonisation]") {
    auto demand=roomyCity();
    demand.strategicSiteWanted=true;
    SECTION("No safe unoccupied unreserved reachable island") { demand.siteAvailable=false; }
    SECTION("No standing yard") { demand.yards=0; }
    SECTION("No city rules") { demand.citySim=false; }
    SECTION("No custom mode") { demand.customGame=false; }
    SECTION("Campaign game in custom mode") { demand.campaignGame=true; }
    SECTION("Helper controller") { demand.supportMode=true; }
    CHECK_FALSE(QuantBotColonisationPolicy::due(demand));
    CHECK_FALSE(QuantBotColonisationPolicy::missionDue(demand));
}

TEST_CASE("Default demand keeps ordinary space and formation colonisation unchanged",
          "[quantbot][civwars][colonisation]") {
    auto demand=roomyCity();
    CHECK_FALSE(demand.strategicSiteWanted);
    CHECK_FALSE(QuantBotColonisationPolicy::due(demand));
    CHECK_FALSE(QuantBotColonisationPolicy::missionDue(demand));

    SECTION("A cramped base still buys and sends a colonist") {
        demand.freeFootprints=QuantBotColonisationPolicy::kCrampedFootprints-1;
        CHECK(QuantBotColonisationPolicy::due(demand));
        CHECK(QuantBotColonisationPolicy::missionDue(demand));
    }
    SECTION("A failed production footprint still buys and sends a colonist") {
        demand.productionRoomBlocked=true;
        CHECK(QuantBotColonisationPolicy::due(demand));
        CHECK(QuantBotColonisationPolicy::missionDue(demand));
    }
    SECTION("A full formation sends an MCV without asking for another") {
        demand.yardsOnFormation=QuantBotColonisationPolicy::kYardsPerFormation;
        CHECK_FALSE(QuantBotColonisationPolicy::due(demand));
        CHECK(QuantBotColonisationPolicy::missionDue(demand));
    }
}
