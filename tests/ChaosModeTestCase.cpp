// SPDX-License-Identifier: GPL-2.0-or-later
#include <catch2/catch_all.hpp>
#include <mod/ChaosMode.h>
#include <misc/Random.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <cstring>

namespace {
void populate(ChaosMode::TechnologyTable& table) {
    for(int item = 0; item < Num_ItemID; ++item) for(int house = 0; house < NUM_HOUSES; ++house) {
        auto& data = table[item][house];
        data = {};
        data.enabled = true;
        data.builder = Structure_ConstructionYard;
        data.techLevel = 1 + item % 9;
        data.price = item * 100 + house;
        data.hitpoints = 1000 + house;
    }
}
}

TEST_CASE("Chaos exchanges preserve tiers and exclude Natives", "[tornie][chaos]") {
    ChaosMode::TechnologyTable table{};
    populate(table);
    table[Structure_HeavyFactory][HOUSE_ATREIDES].techLevel = 9;
    ChaosMode chaos;
    Random random(89123);
    chaos.generate(table, true, random);
    REQUIRE(chaos.isEnabled());
    for(int item = 0; item < Num_ItemID; ++item) for(int house = 0; house < NUM_HOUSES; ++house) {
        const int donor = chaos.getTechnologyHouse(house, item);
        INFO("item " << item << ", house " << house);
        if(house == HOUSE_NATIVES || !ChaosMode::includesStructure(item)
           || (item == Structure_HeavyFactory && house == HOUSE_ATREIDES)) {
            REQUIRE(donor == house);
        } else {
            REQUIRE(isPlayerHouseID(donor));
            REQUIRE(donor != house);
        }
        REQUIRE(table[item][house].price == item * 100 + donor);
        REQUIRE(table[item][house].techLevel == (item == Structure_HeavyFactory && house == HOUSE_ATREIDES ? 9 : 1 + item % 9));
    }
    REQUIRE(ChaosMode::includesStructure(Structure_WindTrap));
    REQUIRE_FALSE(ChaosMode::includesStructure(Structure_AdvancedWindTrap));
    REQUIRE_FALSE(ChaosMode::includesStructure(Structure_ConstructionYard));
    REQUIRE_FALSE(ChaosMode::includesStructure(Structure_Refinery));
}

TEST_CASE("Chaos persists the exact synchronized donor draw", "[tornie][chaos][save-compat]") {
    ChaosMode::TechnologyTable first{}, second{}, third{};
    populate(first); populate(second); populate(third);
    Random firstRandom(777), secondRandom(777), thirdRandom(778);
    ChaosMode a, b, c;
    a.generate(first, true, firstRandom);
    b.generate(second, true, secondRandom);
    c.generate(third, true, thirdRandom);
    REQUIRE(firstRandom.getSeed() == secondRandom.getSeed());
    OMemoryStream saved;
    saved.open();
    a.save(saved);
    IMemoryStream input(saved.getData(), saved.getDataLength());
    ChaosMode loaded;
    loaded.load(input);
    REQUIRE(loaded.isEnabled());
    bool differs = false;
    for(int house = 0; house < NUM_HOUSES; ++house) for(int item : ChaosMode::Structures) {
        REQUIRE(a.getTechnologyHouse(house, item) == b.getTechnologyHouse(house, item));
        REQUIRE(a.getTechnologyHouse(house, item) == loaded.getTechnologyHouse(house, item));
        differs |= a.getTechnologyHouse(house, item) != c.getTechnologyHouse(house, item);
    }
    REQUIRE(differs);
    OMemoryStream resaved;
    resaved.open();
    loaded.save(resaved);
    REQUIRE(resaved.getDataLength() == saved.getDataLength());
    REQUIRE(std::memcmp(resaved.getData(), saved.getData(), saved.getDataLength()) == 0);
    loaded.reset();
    REQUIRE_FALSE(loaded.isEnabled());
    REQUIRE(loaded.getTechnologyHouse(HOUSE_ATREIDES, Structure_WindTrap) == HOUSE_ATREIDES);
}

TEST_CASE("Disabled Chaos leaves data and synchronized RNG untouched", "[tornie][chaos]") {
    ChaosMode::TechnologyTable table{};
    populate(table);
    Random random(444);
    ChaosMode chaos;
    chaos.generate(table, false, random);
    REQUIRE_FALSE(chaos.isEnabled());
    REQUIRE(random.getSeed() == 444);
    REQUIRE(table[Structure_HeavyFactory][HOUSE_ATREIDES].price == Structure_HeavyFactory * 100 + HOUSE_ATREIDES);
    OMemoryStream saved;
    saved.open();
    chaos.save(saved);
    REQUIRE(saved.getDataLength() == 1);
    SettingsClass::GameOptionsClass options;
    const auto normal = options;
    const auto hash = options.getHash();
    options.chaosMode = true;
    REQUIRE(options != normal);
    REQUIRE(options.getHash() != hash);
}

TEST_CASE("Chaos save refuses Natives technology donors", "[tornie][chaos][save-compat]") {
    for(bool nativeRecipient : {false, true}) {
        OMemoryStream invalid;
        invalid.open();
        invalid.writeBool(true);
        invalid.writeUint32(NUM_HOUSES);
        invalid.writeUint32(ChaosMode::Structures.size());
        for(int item : ChaosMode::Structures) {
            invalid.writeUint32(item);
            for(int house = 0; house < NUM_HOUSES; ++house)
                invalid.writeUint8(nativeRecipient
                    ? (house == HOUSE_NATIVES ? HOUSE_ATREIDES : house)
                    : (house == HOUSE_ATREIDES ? HOUSE_NATIVES : house));
        }
        IMemoryStream input(invalid.getData(), invalid.getDataLength());
        ChaosMode chaos;
        REQUIRE_THROWS(chaos.load(input));
    }
}
