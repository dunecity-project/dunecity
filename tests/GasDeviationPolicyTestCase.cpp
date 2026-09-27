// Gas deviation is one shared contract: the Deviator's weapon selection and the
// gas impact read the same eligibility list and the same fixed chance, for every
// house, player type and game mode. These are the parts that need no live Game;
// the real projectile flight, the airborne conversion and the ownership
// reversion are measured by tests/units/projectile-combat.inc (CTest target
// projectile_combat_probe).
#include <catch2/catch_test_macros.hpp>

#include <GasDeviationPolicy.h>
#include <players/AirStrikePolicy.h>

TEST_CASE("Gas deviation uses one fixed conversion chance", "[deviator][gas]") {
    // Exactly 0.80 in fixed point, so lockstep peers compare the same number.
    CHECK(GasDeviationPolicy::conversionChance() == 0.80_fix);
    CHECK(GasDeviationPolicy::conversionChance() > 0.79_fix);
    CHECK(GasDeviationPolicy::conversionChance() < 0.81_fix);

    // The chance carries no house, player or mode parameter at all, which is how
    // the shared behaviour is guaranteed rather than asserted case by case.
}

TEST_CASE("Gas converts crewed aircraft and vehicles", "[deviator][gas]") {
    // The user-visible change: both aircraft with a crew are eligible.
    CHECK(GasDeviationPolicy::eligibleTarget(Unit_Ornithopter));
    CHECK(GasDeviationPolicy::eligibleTarget(Unit_Carryall));
    CHECK(GasDeviationPolicy::eligibleTarget(Unit_ChemicalCarryall));

    // Ground and infantry eligibility is unchanged.
    for(int item : {Unit_Tank, Unit_SiegeTank, Unit_Quad, Unit_Trike, Unit_Harvester,
                    Unit_MCV, Unit_Launcher, Unit_Devastator, Unit_SonicTank,
                    Unit_Deviator, Unit_Soldier, Unit_Trooper, Unit_Saboteur})
        CHECK(GasDeviationPolicy::eligibleTarget(item));
}

TEST_CASE("Gas never converts Frigates, Sandworms or city scenery", "[deviator][gas]") {
    CHECK_FALSE(GasDeviationPolicy::eligibleTarget(Unit_Frigate));
    CHECK_FALSE(GasDeviationPolicy::eligibleTarget(Unit_Sandworm));
    CHECK_FALSE(GasDeviationPolicy::eligibleTarget(Unit_AmbientAirplane));
    CHECK_FALSE(GasDeviationPolicy::eligibleTarget(Unit_AmbientHelicopter));
}

TEST_CASE("Deviator anti-air classification is now truthful", "[deviator][gas][air]") {
    // AirStrikePolicy already treated a Deviator as anti-air while its weapon
    // refused every flyer. The classification is kept and is now accurate.
    CHECK(AirStrikePolicy::antiAir(Unit_Deviator));
    CHECK(GasDeviationPolicy::eligibleTarget(Unit_Ornithopter));
    CHECK(GasDeviationPolicy::eligibleTarget(Unit_Carryall));
}

TEST_CASE("Gas eligibility rejects structures outright", "[deviator][gas]") {
    // eligibleTarget() is isUnit()-gated, so the gas impact's separate structure
    // branch is not the only thing keeping buildings out.
    for(int item : {Structure_RocketTurret, Structure_ConstructionYard, Structure_Refinery,
                    Structure_Wall, Structure_Palace, Structure_HeavyFactory})
        CHECK_FALSE(GasDeviationPolicy::eligibleTarget(item));
}
