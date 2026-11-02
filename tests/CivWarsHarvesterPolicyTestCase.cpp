#include <catch2/catch_test_macros.hpp>

#include <players/TacticalSafetyPolicy.h>

namespace {
struct ObservedOwner {
    int team = 2;
    int getTeamID() const { return team; }
};

struct ObservedPosition {
    bool valid = true;
    bool isInvalid() const { return !valid; }
};

struct ObservedEnemy {
    const ObservedOwner* owner = nullptr;
    int health = 100;
    bool visible = true;
    ObservedPosition position;
    mutable int ownerReads = 0, visibilityReads = 0, locationReads = 0;

    const ObservedOwner* getOwner() const { ++ownerReads; return owner; }
    int getHealth() const { return health; }
    bool isVisible(int) const { ++visibilityReads; return visible; }
    ObservedPosition getLocation() const { ++locationReads; return position; }
};
}

TEST_CASE("Danger observes only live visible enemies and never reads sleeping worm positions",
          "[quantbot][civwars][harvester][sandworm][visibility]") {
    using TacticalSafetyPolicy::observedThreat;
    ObservedOwner owner;
    ObservedEnemy enemy;
    enemy.owner = &owner;
    REQUIRE(observedThreat(&enemy,1,true));
    REQUIRE_FALSE(observedThreat(static_cast<const ObservedEnemy*>(nullptr),1,true));

    enemy.ownerReads = enemy.visibilityReads = enemy.locationReads = 0;
    REQUIRE_FALSE(observedThreat(&enemy,1,false));
    REQUIRE(enemy.ownerReads == 0);
    REQUIRE(enemy.visibilityReads == 0);
    REQUIRE(enemy.locationReads == 0);

    enemy.visible = false;
    REQUIRE_FALSE(observedThreat(&enemy,1,true));
    REQUIRE(enemy.visibilityReads == 1);
    REQUIRE(enemy.locationReads == 0);
    enemy.visible = true;
    owner.team = 1;
    REQUIRE_FALSE(observedThreat(&enemy,1,true));
    REQUIRE(enemy.visibilityReads == 1);
    owner.team = 2;
    enemy.health = 0;
    REQUIRE_FALSE(observedThreat(&enemy,1,true));
    REQUIRE(enemy.visibilityReads == 1);
    enemy.health = 100;
    enemy.owner = nullptr;
    REQUIRE_FALSE(observedThreat(&enemy,1,true));
    REQUIRE(enemy.visibilityReads == 1);
    enemy.owner = &owner;
    enemy.position.valid = false;
    REQUIRE_FALSE(observedThreat(&enemy,1,true));
    REQUIRE(enemy.locationReads == 1);
}

TEST_CASE("Civ Wars worm warning leaves disabled and ordinary enemy radii unchanged",
          "[quantbot][civwars][harvester]") {
    using namespace TacticalSafetyPolicy;
    for (const int item : {Unit_Sandworm, Unit_TameSandworm, Unit_Launcher, Unit_Tank,
                           Unit_Trooper, Unit_Harvester, Structure_Palace}) {
        for (const int range : {0, 1, 4, 9}) {
            const int tactical = std::max(1,range)+2;
            const int harvest = isInfantryUnit(item) ? -1 : std::max(1,range)+(item==Unit_Launcher ? 3 : 0);
            REQUIRE(tacticalThreatRadius(item,range) == tactical);
            REQUIRE(tacticalThreatRadius(item,range,false) == tactical);
            REQUIRE(harvesterThreatRadius(item,range) == harvest);
            REQUIRE(harvesterThreatRadius(item,range,false) == harvest);
            REQUIRE(threatReachesRock(item));
            REQUIRE(threatReachesRock(item,false));
            if (!isSandwormUnit(item)) {
                REQUIRE(tacticalThreatRadius(item,range,true) == tactical);
                REQUIRE(harvesterThreatRadius(item,range,true) == harvest);
                REQUIRE(threatReachesRock(item,true));
            }
        }
    }
}

TEST_CASE("An observed Civ Wars worm has eight tiles of warning in both danger fields",
          "[quantbot][civwars][harvester][sandworm]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE(tacticalThreatRadius(Unit_Sandworm,0) == 3);
    REQUIRE(harvesterThreatRadius(Unit_Sandworm,0) == 1);
    REQUIRE(tacticalThreatRadius(Unit_Sandworm,0,true) == 8);
    REQUIRE(harvesterThreatRadius(Unit_Sandworm,0,true) == 8);
    REQUIRE(tacticalThreatRadius(Unit_TameSandworm,0,true) == 8);
    REQUIRE(harvesterThreatRadius(Unit_TameSandworm,0,true) == 8);
    REQUIRE_FALSE(threatReachesRock(Unit_TameSandworm,true));
    const int radius = harvesterThreatRadius(Unit_Sandworm,0,true);
    auto danger = [=](int x,int y) { return x*x+y*y <= radius*radius ? 100 : 0; };
    REQUIRE(danger(8,0) == 100);
    REQUIRE(danger(9,0) == 0);
    REQUIRE(danger(5,6) == 100);
    REQUIRE(danger(6,6) == 0); // Keep the circular harvester stamp, not a square.
    REQUIRE_FALSE(escapeCorridor(10,0,8,0,danger));
    REQUIRE(escapeCorridor(8,0,10,0,danger));
}

TEST_CASE("Worm warning permits existing dispersal to a rock refuge without sheltering other threats",
          "[quantbot][civwars][harvester][sandworm]") {
    using namespace TacticalSafetyPolicy;
    REQUIRE_FALSE(threatReachesRock(Unit_Sandworm,true));
    REQUIRE(threatReachesRock(Unit_Sandworm,false));
    REQUIRE(threatReachesRock(Unit_Tank,true));
    REQUIRE(threatReachesRock(Unit_Launcher,true));
    // Tile::isRock() is also true for concrete and mountains. Only the refuge
    // is passable; the ordinary dispersal caller still rejects mountain cells.
    auto rock = [](int x,int y) { return x==4 && y==0; };
    auto danger = [&](int x,int y) {
        const int radius = harvesterThreatRadius(Unit_Sandworm,0,true);
        return x*x+y*y <= radius*radius
            && (threatReachesRock(Unit_Sandworm,true) || !rock(x,y)) ? 100 : 0;
    };
    REQUIRE(danger(3,0) == 100);
    REQUIRE(danger(4,0) == 0);
    REQUIRE(escapeCorridor(2,0,4,0,danger));
    REQUIRE_FALSE(escapeCorridor(4,0,6,0,danger));
    const std::vector<int> scores{3, 6, 9};
    const std::vector<int> destinations{3, 4, 5};
    int rejected = -1;
    REQUIRE(lowestSafeCandidate(scores,[&](int k) {
        const int x = destinations[k];
        return danger(x,0)==0 && escapeCorridor(2,0,x,0,danger);
    },rejected) == 1);
    REQUIRE(rejected == 1);
}

TEST_CASE("Only generated Civ Wars spice corridors inspect mountain terrain",
          "[quantbot][civwars][harvester][terrain]") {
    using TacticalSafetyPolicy::spiceCorridor;
    int reads = 0;
    auto ridge = [&](int x,int) { ++reads; return x==5; };
    REQUIRE(spiceCorridor(0,2,10,2,ridge));
    REQUIRE(spiceCorridor(0,2,10,2,ridge,false));
    REQUIRE(reads == 0); // Ordinary maps and the disabled switch do no terrain work.
    REQUIRE_FALSE(spiceCorridor(0,2,10,2,ridge,true));
    REQUIRE(reads > 0);
    REQUIRE_FALSE(spiceCorridor(10,2,0,2,ridge,true));
    REQUIRE(spiceCorridor(0,2,4,2,ridge,true));
    REQUIRE_FALSE(spiceCorridor(4,2,5,2,ridge,true)); // Existing jobs include their destination.
    REQUIRE(spiceCorridor(4,2,4,2,ridge,true));
    REQUIRE_FALSE(spiceCorridor(5,2,5,2,ridge,true));
}

TEST_CASE("Mountain spice corridors cover thin ridges and diagonal corner cells in both directions",
          "[quantbot][civwars][harvester][terrain]") {
    using TacticalSafetyPolicy::spiceCorridor;
    auto horizontal = [](int,int y) { return y==5; };
    REQUIRE_FALSE(spiceCorridor(2,0,2,10,horizontal,true));
    REQUIRE_FALSE(spiceCorridor(2,10,2,0,horizontal,true));
    auto thinRidge = [](int x,int y) { return x==5 && y>=2 && y<=4; };
    REQUIRE_FALSE(spiceCorridor(0,0,10,6,thinRidge,true));
    REQUIRE_FALSE(spiceCorridor(10,6,0,0,thinRidge,true));
    auto corner = [](int x,int y) { return x==1 && y==0; };
    REQUIRE_FALSE(spiceCorridor(0,0,2,2,corner,true));
    REQUIRE_FALSE(spiceCorridor(2,2,0,0,corner,true));
    auto open = [](int,int) { return false; }; // Passable rock is not mountain.
    REQUIRE(spiceCorridor(0,0,10,6,open,true));
    REQUIRE(spiceCorridor(10,6,0,0,open,true));
}

TEST_CASE("Spice terrain filtering does not reject a safe refinery detour",
          "[quantbot][civwars][harvester][terrain]") {
    using namespace TacticalSafetyPolicy;
    auto ridge = [](int x,int y) { return x==5 && y>=1 && y<=6; };
    auto noDanger = [](int,int) { return 0; };
    REQUIRE_FALSE(spiceCorridor(2,3,8,3,ridge,true));
    // Returns retain the danger-only corridor: pathfinding can go around y=0.
    REQUIRE(escapeCorridor(2,3,8,3,noDanger));
    REQUIRE(spiceCorridor(2,0,8,0,ridge,true));
}

TEST_CASE("Mountain spice scanning is bounded by corridor length without off-map probes",
          "[quantbot][civwars][harvester][terrain]") {
    using TacticalSafetyPolicy::spiceCorridor;
    for (const int endY : {0, 1, 127, 382, 383}) {
        int reads = 0;
        auto open = [&](int x,int y) {
            REQUIRE(x>=0);
            REQUIRE(x<=383);
            REQUIRE(y>=0);
            REQUIRE(y<=endY);
            ++reads;
            return false;
        };
        REQUIRE(spiceCorridor(0,0,383,endY,open,true));
        REQUIRE(reads <= 1+3*(383+endY));
        reads = 0;
        REQUIRE(spiceCorridor(383,endY,0,0,open,true));
        REQUIRE(reads <= 1+3*(383+endY));
    }
}
