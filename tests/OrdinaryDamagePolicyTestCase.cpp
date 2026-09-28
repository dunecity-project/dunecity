// The splash contract for ordinary gun, shell and rocket impacts against ground victims, in
// both rule sets: the shipped City balance and the "original unit damage" option. These are the
// parts that need no live Game: the geometry, the quarter-tile bands, the payload category and
// which projectiles are in scope at all. Actual hit points removed by the production
// Map::damage, the option being read from the match settings, and the untouched
// building/air/special cases are measured by tests/units/damage-splash.inc (run from the
// projectile mechanics probe).
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

// Most cases below describe the shipped default, so the shorter calls mean CityBalance and the
// original-damage cases always name their mode.
int damageAt(Uint32 bulletID, Uint32 sourceItemID, int nominalDamage, int distance) {
    return OrdinaryDamagePolicy::damageAt(Mode::CityBalance, bulletID, sourceItemID, nominalDamage, distance);
}
int centreDamage(Uint32 bulletID, Uint32 sourceItemID, int nominalDamage) {
    return OrdinaryDamagePolicy::centreDamage(Mode::CityBalance, bulletID, sourceItemID, nominalDamage);
}

struct Profile { int centre, quarter, half, threeQuarter; };

Profile profileOf(Mode mode, Uint32 bulletID, Uint32 sourceItemID, int nominalDamage) {
    return {OrdinaryDamagePolicy::damageAt(mode, bulletID, sourceItemID, nominalDamage, kCentre),
            OrdinaryDamagePolicy::damageAt(mode, bulletID, sourceItemID, nominalDamage, kQuarter),
            OrdinaryDamagePolicy::damageAt(mode, bulletID, sourceItemID, nominalDamage, kHalf),
            OrdinaryDamagePolicy::damageAt(mode, bulletID, sourceItemID, nominalDamage, kThreeQuarter)};
}

Profile profileOf(Uint32 bulletID, Uint32 sourceItemID, int nominalDamage) {
    return profileOf(Mode::CityBalance, bulletID, sourceItemID, nominalDamage);
}

bool operator==(const Profile& a, const Profile& b) {
    return a.centre == b.centre && a.quarter == b.quarter
        && a.half == b.half && a.threeQuarter == b.threeQuarter;
}

} // namespace

TEST_CASE("Default ground splash matches the accepted per-weapon table", "[damage][splash]") {
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

// ---------------------------------------------------------------------------------------------
// The "original unit damage" option: Dune II / Dune Dynasty classic ground damage.

TEST_CASE("Original mode gives the whole payload to the centre of the blast", "[damage][splash][original]") {
    const auto classic = Mode::Original;

    // Heavy weapons, which the shipped balance halves, keep everything at the centre. The
    // numbers are Dune Dynasty's own (src/table/unitinfo.c damage fields), and the shipped
    // ObjectData configures the same payloads.
    CHECK(profileOf(classic, Bullet_ShellMedium, Unit_Tank, kTank) == Profile{25, 12, 6, 3});
    CHECK(profileOf(classic, Bullet_ShellLarge, Unit_SiegeTank, kSiege) == Profile{30, 15, 7, 3});
    CHECK(profileOf(classic, Bullet_ShellLarge, Unit_Devastator, kDevastatorCannon) == Profile{40, 20, 10, 5});
    CHECK(profileOf(classic, Bullet_Rocket, Unit_Launcher, kLauncher) == Profile{75, 37, 18, 9});
    CHECK(profileOf(classic, Bullet_ShellTurret, Structure_GunTurret, kGunTurret) == Profile{20, 10, 5, 2});
    CHECK(profileOf(classic, Bullet_ShellTurret, Structure_RocketTurret, kGunTurret) == Profile{20, 10, 5, 2});
    CHECK(profileOf(classic, Bullet_TurretRocket, Structure_RocketTurret, kRocketTurretMissile)
          == Profile{30, 15, 7, 3});

    // The light guns were already delivering everything, so they do not move at all.
    CHECK(profileOf(classic, Bullet_ShellSmall, Unit_Soldier, kSoldier) == Profile{3, 1, 0, 0});
    CHECK(profileOf(classic, Bullet_ShellSmall, Unit_Trooper, kTrooper) == Profile{5, 2, 1, 0});
    CHECK(profileOf(classic, Bullet_SmallRocket, Unit_Trooper, kTrooperRanged) == Profile{4, 2, 1, 0});
    CHECK(profileOf(classic, Bullet_ShellSmall, Unit_Trike, kTrike) == Profile{5, 2, 1, 0});
    CHECK(profileOf(classic, Bullet_ShellSmall, Unit_RaiderTrike, kRaider) == Profile{5, 2, 1, 0});
    CHECK(profileOf(classic, Bullet_ShellSmall, Unit_Quad, kQuad) == Profile{7, 3, 1, 0});

    // Weapons the original game never had follow the same rule rather than getting a table of
    // their own: whatever the content configures arrives whole at the centre.
    CHECK(profileOf(classic, Bullet_Rocket, Unit_EliteLauncher, kEliteLauncher) == Profile{94, 47, 23, 11});
    CHECK(profileOf(classic, Bullet_SmallRocket, Unit_RocketTrike, kRocketTrike) == Profile{5, 2, 1, 0});
    CHECK(profileOf(classic, Bullet_ShellLarge, Unit_EliteSiegeTank, 30) == Profile{30, 15, 7, 3});
    CHECK(profileOf(classic, Bullet_ShellSmall, Unit_RebelHarvester, 9) == Profile{9, 4, 2, 1});
}

TEST_CASE("Original mode restores the classic Ornithopter ground payload", "[damage][splash][original]") {
    // Dune Dynasty gives the Ornithopter damage 50 and fires it as a mini rocket, and every
    // mini rocket loses a quarter before launch: 50 - 12 = 38 arrives. The shipped ObjectData
    // configures 45, and the quarter reduction lives in the Trooper branch of the attack code,
    // so the classic figure is restored by scaling the configured payload 38/45.
    CHECK(kClassicOrnithopterGroundNumerator == 38);
    CHECK(kClassicOrnithopterGroundDenominator == 45);
    CHECK(50 - 50/4 == kClassicOrnithopterGroundNumerator);

    CHECK(profileOf(Mode::Original, Bullet_SmallRocket, Unit_Ornithopter, kOrnithopter)
          == Profile{38, 19, 9, 4});
    // The shipped balance is unchanged and still reads the configured 45.
    CHECK(profileOf(Bullet_SmallRocket, Unit_Ornithopter, kOrnithopter) == Profile{22, 11, 5, 2});

    // The correction is a ratio, so a mod that retunes the Ornithopter still moves with it
    // rather than being pinned to 38.
    CHECK(centreDamage(Mode::Original, Bullet_SmallRocket, Unit_Ornithopter, 2*kOrnithopter) == 76);
    CHECK(centreDamage(Mode::Original, Bullet_SmallRocket, Unit_Ornithopter, 90) == 76);
    CHECK(centreDamage(Mode::Original, Bullet_SmallRocket, Unit_Ornithopter, 9) == 7);   // floor(7.6)
    CHECK(centreDamage(Mode::Original, Bullet_SmallRocket, Unit_Ornithopter, 1) == 0);   // floor(0.84)
    CHECK(centreDamage(Mode::Original, Bullet_SmallRocket, Unit_Ornithopter, 0) == 0);

    // It is the Ornithopter's own rocket that is corrected, not the projectile type and not
    // every aircraft, so no other weapon is touched.
    CHECK(groundPayload(Mode::Original, Bullet_SmallRocket, Unit_Trooper, 45) == 45);
    CHECK(groundPayload(Mode::Original, Bullet_SmallRocket, Unit_RocketTrike, 45) == 45);
    CHECK(groundPayload(Mode::Original, Bullet_Rocket, Unit_Ornithopter, 45) == 45);
    CHECK(groundPayload(Mode::Original, Bullet_SmallRocket, NONE_ID, 45) == 45);
    // And nothing at all is rescaled in the shipped balance.
    CHECK(groundPayload(Mode::CityBalance, Bullet_SmallRocket, Unit_Ornithopter, 45) == 45);
}

TEST_CASE("Both modes share one geometry and one set of specials", "[damage][splash][original]") {
    // The mode changes how much arrives, never how far it reaches or what is in scope.
    for(auto mode : {Mode::CityBalance, Mode::Original}) {
        CHECK(OrdinaryDamagePolicy::damageAt(mode, Bullet_ShellMedium, Unit_Tank, kTank, TILESIZE-1)
              == (mode == Mode::Original ? 3 : 1));
        CHECK(OrdinaryDamagePolicy::damageAt(mode, Bullet_ShellMedium, Unit_Tank, kTank, TILESIZE) == 0);
        CHECK(OrdinaryDamagePolicy::damageAt(mode, Bullet_ShellMedium, Unit_Tank, kTank, 4*TILESIZE) == 0);
        // Zero tails, no rounding up, and falloff that never increases with distance.
        CHECK(OrdinaryDamagePolicy::damageAt(mode, Bullet_ShellSmall, Unit_Soldier, kSoldier, kHalf) == 0);
        CHECK(OrdinaryDamagePolicy::damageAt(mode, Bullet_ShellSmall, Unit_Soldier, 1, kQuarter) == 0);
        CHECK(OrdinaryDamagePolicy::damageAt(mode, Bullet_ShellMedium, Unit_Tank, -5, 0) == 0);
        int previous = OrdinaryDamagePolicy::centreDamage(mode, Bullet_Rocket, Unit_Launcher, kLauncher);
        for(int distance = 1; distance <= TILESIZE; ++distance) {
            const int current = OrdinaryDamagePolicy::damageAt(mode, Bullet_Rocket, Unit_Launcher,
                                                               kLauncher, distance);
            CHECK(current <= previous);
            previous = current;
        }
        // Specials stay out of scope in both modes; ordinaryBlast() carries no mode at all.
        for(Uint32 bullet : {Uint32{Bullet_DRocket}, Uint32{Bullet_Sonic}, Uint32{Bullet_SonicTrike},
                             Uint32{Bullet_Sandworm}, Uint32{Bullet_Flame}, Uint32{Bullet_Heal},
                             Uint32{Bullet_LargeRocket}, Uint32{Unit_Devastator}})
            CHECK_FALSE(ordinaryBlast(bullet));
    }

    // Original mode is exactly one band shift above the default for every heavy weapon, and
    // identical for every light one. Stated as shifts, because with a payload that does not
    // divide evenly the two floors are not simply double one another (200>>3 is 25, 200>>4 is
    // 12) - and that flooring is the contract, not an accident.
    CHECK(centreShift(Mode::CityBalance, Bullet_Rocket, Unit_Launcher) == 1);
    CHECK(centreShift(Mode::Original, Bullet_Rocket, Unit_Launcher) == 0);
    CHECK(centreShift(Mode::CityBalance, Bullet_ShellSmall, Unit_Quad) == 0);
    CHECK(centreShift(Mode::Original, Bullet_ShellSmall, Unit_Quad) == 0);
    CHECK(OrdinaryDamagePolicy::damageAt(Mode::Original, Bullet_Rocket, Unit_Launcher, 200, 48) == 25);
    CHECK(OrdinaryDamagePolicy::damageAt(Mode::CityBalance, Bullet_Rocket, Unit_Launcher, 200, 48) == 12);

    // With a payload that divides evenly the doubling is exact at every band.
    for(int distance : {0, 16, 32, 48, 63}) {
        CHECK(OrdinaryDamagePolicy::damageAt(Mode::Original, Bullet_Rocket, Unit_Launcher, 256, distance)
              == 2 * OrdinaryDamagePolicy::damageAt(Mode::CityBalance, Bullet_Rocket, Unit_Launcher, 256, distance));
        CHECK(OrdinaryDamagePolicy::damageAt(Mode::Original, Bullet_ShellSmall, Unit_Quad, kQuad, distance)
              == OrdinaryDamagePolicy::damageAt(Mode::CityBalance, Bullet_ShellSmall, Unit_Quad, kQuad, distance));
    }
}

TEST_CASE("The mode comes from the match option and defaults off", "[damage][splash][original]") {
    CHECK(modeOf(false) == Mode::CityBalance);
    CHECK(modeOf(true) == Mode::Original);
    // A default-constructed settings object is the unchecked box, so a match that never touches
    // the option runs the shipped balance.
    CHECK(modeOf(SettingsClass::GameOptionsClass().originalUnitDamage) == Mode::CityBalance);
    CHECK(centreDamage(modeOf(SettingsClass::GameOptionsClass().originalUnitDamage),
                       Bullet_ShellMedium, Unit_Tank, kTank) == 12);
}
