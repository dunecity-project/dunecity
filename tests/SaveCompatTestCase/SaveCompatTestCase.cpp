/*
 *  SaveCompatTestCase.cpp
 *
 *  Regression tests for savegame backward compatibility across the
 *  Num_ItemID expansion history:
 *    Dune Legacy 0.99.x  → 41 items
 *    DuneCity 1.0.0–1.0.7 → 48 items
 *    DuneCity 1.0.8–1.0.10 → 52 items
 *    DuneCity 1.0.11+ (9811) → self-describing (count in stream)
 */

#include <catch2/catch_all.hpp>
#include <data.h>
#include <DataTypes.h>
#include <Definitions.h>
#include <misc/SaveCompat.h>
#include <mod/ModInfo.h>
#include <SpiceIncome.h>

// ---------- constant checks ----------

TEST_CASE("Save compat: LEGACY_NUM_ITEM_ID_DUNELEGACY matches original Dune Legacy",
          "[save-compat][regression]") {
    REQUIRE(LEGACY_NUM_ITEM_ID_DUNELEGACY == 41);
}

TEST_CASE("Save compat: LEGACY_NUM_ITEM_ID_9810 matches v1.0.7 value",
          "[save-compat][regression]") {
    REQUIRE(LEGACY_NUM_ITEM_ID_9810 == 48);
}

TEST_CASE("Save compat: current Num_ItemID >= legacy",
          "[save-compat][regression]") {
    REQUIRE(Num_ItemID >= LEGACY_NUM_ITEM_ID_9810);
    REQUIRE(Num_ItemID >= LEGACY_NUM_ITEM_ID_DUNELEGACY);
}

TEST_CASE("Save compat: SAVEGAMEVERSION is 9811 or higher",
          "[save-compat][regression]") {
    // 9852 adds the QuantBot army posture block (posture, entry cycle, attrition
    // ledger, tracked Custom wave, protected rally, recall cursor).
    // 9853 adds the Micropolis city-aircraft state: the helicopter's home pad,
    // patrol count, report cooldown, scan cooldown, report tally and
    // return-home flag, and the airplane's take-off frame and flight budget.
    REQUIRE(SAVEGAMEVERSION == 9853);
    REQUIRE(SAVEGAMEVERSION >= 9818);
}

TEST_CASE("Save compat: the spice income format version is the current one",
          "[save-compat][regression][spiceincome]") {
    // The SMUL setup block and the House field appear together, at this version. If a later
    // change bumps SAVEGAMEVERSION, this stays put: 9851 is when the field started existing,
    // and the gates that read it compare against that, not against "current".
    REQUIRE(SpiceIncome::kFirstSavegameVersion == 9851);
    REQUIRE(SAVEGAMEVERSION >= static_cast<int>(SpiceIncome::kFirstSavegameVersion));
}

TEST_CASE("Save compat: extended houses preserve legacy IDs",
          "[save-compat][regression]") {
    REQUIRE(NUM_LEGACY_HOUSES == 8);
    REQUIRE(HOUSE_REBELS == 7);
    REQUIRE(HOUSE_CUSTOM == 8);
    REQUIRE(NUM_HOUSES == 12);
    REQUIRE(NUM_TEAM_SLOTS == 10);
}

TEST_CASE("Save compat: old visual color slots retain their meaning",
          "[save-compat][regression]") {
    REQUIRE(migrateLegacyHouseColorSlot(7) == 7);
    REQUIRE(migrateLegacyHouseColorSlot(8) == 9);
    REQUIRE(migrateLegacyHouseColorSlot(13) == 14);
}

TEST_CASE("Save compat: generic custom house is disabled by default",
          "[save-compat][regression]") {
    const CustomHouseInfo info;
    REQUIRE_FALSE(info.enabled);
    REQUIRE(info.scenarioLetter == '?');
}

TEST_CASE("Save compat: new items are at indices >= LEGACY_NUM_ITEM_ID_9810",
          "[save-compat][regression]") {
    REQUIRE(Structure_Stadium == 48);
    REQUIRE(Structure_Airport == 49);
    REQUIRE(Unit_AmbientAirplane == 50);
    REQUIRE(Unit_AmbientHelicopter == 51);
}

TEST_CASE("Save compat: Unit_Troopers is last legacy item at index 47",
          "[save-compat][regression]") {
    REQUIRE(Unit_Troopers == 47);
    REQUIRE(Unit_Troopers == LEGACY_NUM_ITEM_ID_9810 - 1);
}

TEST_CASE("Save compat: extended items classified correctly",
          "[save-compat][regression]") {
    REQUIRE(isStructure(Structure_Stadium));
    REQUIRE(isStructure(Structure_Airport));
    REQUIRE_FALSE(isUnit(Structure_Stadium));

    REQUIRE(isUnit(Unit_AmbientAirplane));
    REQUIRE(isUnit(Unit_AmbientHelicopter));
    REQUIRE_FALSE(isStructure(Unit_AmbientAirplane));
}

// ---------- determineLegacySavedItemCount ----------

TEST_CASE("Save compat: determineLegacySavedItemCount maps correctly",
          "[save-compat][regression]") {

    SECTION("Dune Legacy 0.99.5 (v9806) → 41 items") {
        REQUIRE(determineLegacySavedItemCount(9806, "dunelegacy0.99.5") == 41);
    }

    SECTION("Dune Legacy 0.99.4 (v9805) → 41 items") {
        REQUIRE(determineLegacySavedItemCount(9805, "dunelegacy0.99.4") == 41);
    }

    SECTION("DuneCity 1.0.0 (v9810) → 48 items") {
        REQUIRE(determineLegacySavedItemCount(9810, "dunecity1.0.0") == 48);
    }

    SECTION("DuneCity 1.0.7 (v9810) → 48 items") {
        REQUIRE(determineLegacySavedItemCount(9810, "dunecity1.0.7") == 48);
    }

    SECTION("DuneCity 1.0.8 (v9810) → 52 items") {
        REQUIRE(determineLegacySavedItemCount(9810, "dunecity1.0.8") == 52);
    }

    SECTION("DuneCity 1.0.10 (v9810) → 52 items") {
        REQUIRE(determineLegacySavedItemCount(9810, "dunecity1.0.10") == 52);
    }

    SECTION("9811+ → 0 (self-describing)") {
        REQUIRE(determineLegacySavedItemCount(9811, "dunecity1.0.11") == 0);
        REQUIRE(determineLegacySavedItemCount(9812, "dunecity1.1.0") == 0);
    }
}
