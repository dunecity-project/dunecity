// The accepted default splash contract for ordinary gun, shell and rocket impacts against
// ground victims. These are the parts that need no live Game: the geometry, the quarter-tile
// bands, the payload category and which projectiles are in scope at all. Actual hit points
// removed by the production Map::damage, and the untouched building/air/special cases, are
// measured by tests/units/damage-splash.inc (run from the projectile mechanics probe).
#include <catch2/catch_test_macros.hpp>

#include <OrdinaryDamagePolicy.h>

using namespace OrdinaryDamagePolicy;

namespace {

// Stock nominal payloads as shipped in config/ObjectData.ini.default. The policy never reads
// these; they are inputs here so the accepted per-weapon table can be checked end to end.
constexpr int kSoldier = 3;
constexpr int kTrooper = 5;              // close range; the ranged rocket loses a quarter
constexpr int kTrooperRanged = kTrooper - kTrooper/4;
constexpr int kTrike = 5;
constexpr int kRaider = 5;
constexpr int kQuad = 7;
constexpr int kTank = 25;
constexpr int kSiege = 30;
constexpr int kDevastatorCannon = 40;
constexpr int kLauncher = 75;
constexpr int kEliteLauncher = 94;
constexpr int kOrnithopter = 45;
constexpr int kRocketTrike = 5;
constexpr int kGunTurret = 20;           // the Rocket Turret's close cannon reads this too
constexpr int kRocketTurretMissile = 30;

// The four band representatives: centre, quarter, half and three-quarter tile.
constexpr int kCentre = 0;
constexpr int kQuarter = TILESIZE/4;
constexpr int kHalf = TILESIZE/2;
constexpr int kThreeQuarter = (TILESIZE*3)/4;

struct Profile { int centre, quarter, half, threeQuarter; };

Profile profileOf(Uint32 bulletID, Uint32 sourceItemID, int nominalDamage) {
    return {damageAt(bulletID, sourceItemID, nominalDamage, kCentre),
            damageAt(bulletID, sourceItemID, nominalDamage, kQuarter),
            damageAt(bulletID, sourceItemID, nominalDamage, kHalf),
            damageAt(bulletID, sourceItemID, nominalDamage, kThreeQuarter)};
}

bool operator==(const Profile& a, const Profile& b) {
    return a.centre == b.centre && a.quarter == b.quarter
        && a.half == b.half && a.threeQuarter == b.threeQuarter;
}

} // namespace

TEST_CASE("Ordinary ground splash matches the accepted per-weapon table", "[damage][splash]") {
    // Light guns deliver their whole nominal payload at the centre.
    CHECK(profileOf(Bullet_ShellSmall, Unit_Soldier, kSoldier) == Profile{3, 1, 0, 0});
    CHECK(profileOf(Bullet_ShellSmall, Unit_Trooper, kTrooper) == Profile{5, 2, 1, 0});
    CHECK(profileOf(Bullet_SmallRocket, Unit_Trooper, kTrooperRanged) == Profile{4, 2, 1, 0});
    CHECK(profileOf(Bullet_ShellSmall, Unit_Trike, kTrike) == Profile{5, 2, 1, 0});
    CHECK(profileOf(Bullet_ShellSmall, Unit_RaiderTrike, kRaider) == Profile{5, 2, 1, 0});
    CHECK(profileOf(Bullet_ShellSmall, Unit_Quad, kQuad) == Profile{7, 3, 1, 0});

    // Heavy shells and turret shells deliver half.
    CHECK(profileOf(Bullet_ShellMedium, Unit_Tank, kTank) == Profile{12, 6, 3, 1});
    CHECK(profileOf(Bullet_ShellLarge, Unit_SiegeTank, kSiege) == Profile{15, 7, 3, 1});
    CHECK(profileOf(Bullet_ShellLarge, Unit_Devastator, kDevastatorCannon) == Profile{20, 10, 5, 2});
    CHECK(profileOf(Bullet_ShellTurret, Structure_GunTurret, kGunTurret) == Profile{10, 5, 2, 1});
    // The Rocket Turret's close cannon fires a turret shell carrying the Gun-Turret payload.
    CHECK(profileOf(Bullet_ShellTurret, Structure_RocketTurret, kGunTurret) == Profile{10, 5, 2, 1});

    // The rocket families deliver half as well.
    CHECK(profileOf(Bullet_Rocket, Unit_Launcher, kLauncher) == Profile{37, 18, 9, 4});
    CHECK(profileOf(Bullet_Rocket, Unit_EliteLauncher, kEliteLauncher) == Profile{47, 23, 11, 5});
    CHECK(profileOf(Bullet_SmallRocket, Unit_Ornithopter, kOrnithopter) == Profile{22, 11, 5, 2});
    CHECK(profileOf(Bullet_SmallRocket, Unit_RocketTrike, kRocketTrike) == Profile{2, 1, 0, 0});
    CHECK(profileOf(Bullet_TurretRocket, Structure_RocketTurret, kRocketTurretMissile) == Profile{15, 7, 3, 1});
}

TEST_CASE("The same small rocket is light from a Trooper and heavy from a vehicle", "[damage][splash]") {
    // One projectile type, one payload, two categories: the source decides, not the bullet.
    const int payload = 44;  // even, so halving is exact and the doubling below is testable
    CHECK(lightPayload(Bullet_SmallRocket, Unit_Trooper));
    CHECK_FALSE(lightPayload(Bullet_SmallRocket, Unit_Ornithopter));
    CHECK_FALSE(lightPayload(Bullet_SmallRocket, Unit_RocketTrike));
    CHECK(centreDamage(Bullet_SmallRocket, Unit_Trooper, payload)
          == 2 * centreDamage(Bullet_SmallRocket, Unit_Ornithopter, payload));

    // A Trooper's close-range shell and its ranged rocket are both light.
    CHECK(lightPayload(Bullet_ShellSmall, Unit_Trooper));

    // No other infantry-carried rocket exists, and no heavy shell is ever light.
    for(Uint32 bullet : {Uint32{Bullet_ShellMedium}, Uint32{Bullet_ShellLarge},
                         Uint32{Bullet_ShellTurret}, Uint32{Bullet_Rocket}, Uint32{Bullet_TurretRocket}})
        CHECK_FALSE(lightPayload(bullet, Unit_Trooper));
}

TEST_CASE("The payload comes from the content, never from a per-unit table", "[damage][splash][mod]") {
    // A mod that doubles a weapon doubles every band; one that weakens it weakens every band.
    CHECK(profileOf(Bullet_ShellMedium, Unit_Tank, 2*kTank) == Profile{25, 12, 6, 3});
    CHECK(profileOf(Bullet_Rocket, Unit_Launcher, 200) == Profile{100, 50, 25, 12});
    CHECK(profileOf(Bullet_ShellSmall, Unit_Soldier, 100) == Profile{100, 50, 25, 12});
    CHECK(profileOf(Bullet_SmallRocket, Unit_Trooper, 40) == Profile{40, 20, 10, 5});
    CHECK(profileOf(Bullet_SmallRocket, Unit_Ornithopter, 40) == Profile{20, 10, 5, 2});

    // A modded unit type the engine has never seen is scored by its projectile alone.
    CHECK(centreDamage(Bullet_ShellSmall, Unit_RebelHarvester, 9) == 9);
    CHECK(centreDamage(Bullet_ShellLarge, Unit_ChemicalSiegeTank, 30) == 15);

    // A weapon configured to nothing removes nothing rather than wrapping.
    CHECK(centreDamage(Bullet_ShellMedium, Unit_Tank, 0) == 0);
    CHECK(centreDamage(Bullet_ShellMedium, Unit_Tank, -5) == 0);
}

TEST_CASE("Ordinary splash reaches one full tile, strictly", "[damage][splash][geometry]") {
    const int payload = kTank;
    // Inside the last band right up to the tile edge...
    CHECK(damageAt(Bullet_ShellMedium, Unit_Tank, payload, TILESIZE-1) == 1);
    // ...and nothing at exactly one tile or beyond.
    CHECK(damageAt(Bullet_ShellMedium, Unit_Tank, payload, TILESIZE) == 0);
    CHECK(damageAt(Bullet_ShellMedium, Unit_Tank, payload, TILESIZE+1) == 0);
    CHECK(damageAt(Bullet_ShellMedium, Unit_Tank, payload, 4*TILESIZE) == 0);
    CHECK_FALSE(withinBlast(TILESIZE));
    CHECK(withinBlast(TILESIZE-1));

    // Bands are floored quarter tiles: the step happens on the boundary, not before it.
    for(int distance = 0; distance < TILESIZE; ++distance)
        CHECK(band(distance) == distance/16);
    CHECK(damageAt(Bullet_Rocket, Unit_Launcher, kLauncher, 15) == 37);
    CHECK(damageAt(Bullet_Rocket, Unit_Launcher, kLauncher, 16) == 18);
    CHECK(damageAt(Bullet_Rocket, Unit_Launcher, kLauncher, 31) == 18);
    CHECK(damageAt(Bullet_Rocket, Unit_Launcher, kLauncher, 32) == 9);
    CHECK(damageAt(Bullet_Rocket, Unit_Launcher, kLauncher, 47) == 9);
    CHECK(damageAt(Bullet_Rocket, Unit_Launcher, kLauncher, 48) == 4);
    CHECK(damageAt(Bullet_Rocket, Unit_Launcher, kLauncher, 63) == 4);

    // Falloff never increases with distance, for any payload.
    for(int payloadUnderTest : {1, 3, 7, 25, 75, 94, 255}) {
        int previous = centreDamage(Bullet_ShellMedium, Unit_Tank, payloadUnderTest);
        for(int distance = 1; distance <= TILESIZE; ++distance) {
            const int current = damageAt(Bullet_ShellMedium, Unit_Tank, payloadUnderTest, distance);
            CHECK(current <= previous);
            previous = current;
        }
    }
}

TEST_CASE("Blast distance is the Dynasty max-axis plus half-min-axis metric", "[damage][splash][geometry]") {
    const Coord origin(1000, 1000);
    CHECK(distance(origin, origin) == 0);
    // Along an axis the metric is the plain separation.
    CHECK(distance(origin + Coord(40, 0), origin) == 40);
    CHECK(distance(origin + Coord(0, -40), origin) == 40);
    // On the diagonal the shorter axis counts half. This differs from Euclidean distance.
    CHECK(distance(origin + Coord(40, 40), origin) == 60);
    CHECK(distance(origin + Coord(-40, 40), origin) == 60);
    CHECK(distance(origin + Coord(48, 32), origin) == 64);   // out: exactly one tile
    CHECK(distance(origin + Coord(47, 32), origin) == 63);   // in, by one world unit
    CHECK(distance(origin + Coord(63, 0), origin) == 63);

    // The metric is symmetric and sign-independent, which is what keeps peers in lockstep.
    for(int dx : {-63, -17, 0, 5, 44}) for(int dy : {-51, -8, 0, 23, 62}) {
        const Coord point = origin + Coord(dx, dy);
        CHECK(distance(point, origin) == distance(origin, point));
        CHECK(distance(point, origin) == distance(origin + Coord(-dx, -dy), origin));
    }

    // A tank shell landing on the far diagonal corner of an adjacent tile still bites.
    CHECK(damageAt(Bullet_ShellMedium, Unit_Tank, kTank,
                   distance(Coord(0, 0), Coord(40, 40))) == 1);
}

TEST_CASE("Zero tails stay zero", "[damage][splash]") {
    // A soldier is spent by the half-tile band and nothing rounds it back up.
    CHECK(damageAt(Bullet_ShellSmall, Unit_Soldier, kSoldier, kHalf) == 0);
    CHECK(damageAt(Bullet_ShellSmall, Unit_Soldier, kSoldier, kThreeQuarter) == 0);
    for(int distance = 32; distance < TILESIZE; ++distance)
        CHECK(damageAt(Bullet_ShellSmall, Unit_Soldier, kSoldier, distance) == 0);
    // The Rocket Trike's rocket is spent one band earlier still.
    for(int distance = 32; distance < TILESIZE; ++distance)
        CHECK(damageAt(Bullet_SmallRocket, Unit_RocketTrike, kRocketTrike, distance) == 0);
    // A one-point weapon has nothing left outside the centre band.
    CHECK(damageAt(Bullet_ShellSmall, Unit_Soldier, 1, 0) == 1);
    CHECK(damageAt(Bullet_ShellSmall, Unit_Soldier, 1, 16) == 0);
}

TEST_CASE("Weapon category survives the source disappearing", "[damage][splash][provenance]") {
    // damageAt() reads only the snapshotted source type, so a Trooper's rocket in flight is
    // scored the same whether the Trooper is alive, dead, reverted or captured by a third
    // house - nothing about the present owner is an input.
    const int payload = kTrooperRanged;
    const int expected = 4;
    CHECK(centreDamage(Bullet_SmallRocket, Unit_Trooper, payload) == expected);
    CHECK(damageAt(Bullet_SmallRocket, Unit_Trooper, payload, kQuarter) == 2);

    // Without any recorded source the shot falls back to the heavy family rather than
    // inventing a light one.
    CHECK_FALSE(lightPayload(Bullet_SmallRocket, NONE_ID));
    CHECK(centreDamage(Bullet_SmallRocket, NONE_ID, payload) == payload/2);
    // A small shell needs no source at all to be light; the projectile already says so.
    CHECK(centreDamage(Bullet_ShellSmall, NONE_ID, kQuad) == kQuad);
}

TEST_CASE("Special paths are not ordinary blasts", "[damage][splash][specials]") {
    for(Uint32 bullet : {Uint32{Bullet_ShellSmall}, Uint32{Bullet_ShellMedium}, Uint32{Bullet_ShellLarge},
                         Uint32{Bullet_ShellTurret}, Uint32{Bullet_SmallRocket}, Uint32{Bullet_Rocket},
                         Uint32{Bullet_TurretRocket}})
        CHECK(ordinaryBlast(bullet));

    // Gas conversion, Sonic waves, flame and persistent flame, the Sandworm swallow, the
    // Chemipost heal and the Death Hand all keep their own rules and radii.
    for(Uint32 bullet : {Uint32{Bullet_DRocket}, Uint32{Bullet_Sonic}, Uint32{Bullet_SonicTrike},
                         Uint32{Bullet_Sandworm}, Uint32{Bullet_Flame}, Uint32{Bullet_Heal},
                         Uint32{Bullet_LargeRocket}})
        CHECK_FALSE(ordinaryBlast(bullet));

    // The Devastator death blast passes its item id where a bullet id is expected and must keep
    // falling through to the untouched path: half of 150 inside its 16-unit radius.
    CHECK_FALSE(ordinaryBlast(Unit_Devastator));
    CHECK((150 >> (0/(TILESIZE/4) + 1)) == 75);

    // That fall-through only works because every unit id is numerically above every bullet id.
    // The low structure ids do alias bullet ids (Structure_Palace is Bullet_SmallRocket's
    // number), so a new caller passing a structure id as a bullet id would be misread - keep
    // the item-id route to Map::damage restricted to units, as it is today.
    CHECK(Unit_FirstID > Bullet_Heal);
    for(int item = Unit_FirstID; item <= Unit_LastID; ++item)
        CHECK_FALSE(ordinaryBlast(item));
    for(Uint32 item : {Uint32{Unit_AmbientAirplane}, Uint32{Unit_RocketTrike},
                       Uint32{Unit_EliteLauncher}, Uint32{Unit_RebelHarvester}})
        CHECK_FALSE(ordinaryBlast(item));
}
