// Rocket Turrets at the front (TurretFrontPolicy.h): the grids and the four switches' rules.

#include <players/TurretFrontPolicy.h>
#include <players/QuantBotConfig.h>

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>

namespace TF = TurretFrontPolicy;

TEST_CASE("TurretFront: the distance transform is the exact Chebyshev distance", "[ai][turret-front][policy]") {
    const int w = 9, h = 7;
    std::vector<uint8_t> seed(size_t(w * h), 0);
    seed[size_t(3 * w + 2)] = 1;   // (2,3)
    seed[size_t(0 * w + 8)] = 1;   // (8,0)
    const auto d = TF::chebyshevDistance(seed, w, h, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int expected = std::min(TF::tileDistance(x, y, 2, 3), TF::tileDistance(x, y, 8, 0));
            REQUIRE(int(d[size_t(y * w + x)]) == expected);
        }
    const std::vector<uint8_t> none(size_t(w * h), 0);
    const auto far = TF::chebyshevDistance(none, w, h, 40);
    REQUIRE(far.front() == 40);
    REQUIRE(far.back() == 40);
}

TEST_CASE("TurretFront: box sums count a Chebyshev box clipped to the map", "[ai][turret-front][policy]") {
    const int w = 10, h = 8;
    std::vector<int32_t> weight(size_t(w * h), 0);
    weight[size_t(2 * w + 2)] = 3;
    weight[size_t(5 * w + 7)] = 2;
    weight[size_t(0 * w + 0)] = 1;
    const TF::BoxSum sum(weight, w, h);
    REQUIRE(sum.around(2, 2, 0) == 3);
    REQUIRE(sum.around(3, 3, 1) == 3);
    REQUIRE(sum.around(4, 4, 1) == 0);
    REQUIRE(sum.around(0, 0, 2) == 4);
    REQUIRE(sum.around(5, 4, 3) == 5);
    REQUIRE(sum.around(5, 4, 20) == 6);
}

TEST_CASE("TurretFront: the four rules", "[ai][turret-front][policy]") {
    // (a) two turrets for any attack, one more per 400 credits, at most ten.
    REQUIRE(TF::reinforceGoal(0) == 0);
    REQUIRE(TF::reinforceGoal(100) == 2);
    REQUIRE(TF::reinforceGoal(1350) == 5);
    REQUIRE(TF::reinforceGoal(100000) == 10);
    // In range of the attack beats out of range; at the standoff beats in the attackers' midst.
    REQUIRE(TF::reinforceSiteScore(6, 3, 3, 8) > TF::reinforceSiteScore(9, 1, 3, 8));
    REQUIRE(TF::reinforceSiteScore(6, 3, 3, 8) > TF::reinforceSiteScore(1, 3, 3, 8));
    // Spacing: no own turret on the 8 neighbours.
    REQUIRE(TF::spaced(0));
    REQUIRE_FALSE(TF::spaced(1));
    // (b) the army must lead the edge by two tiles, and the belt stays two behind it.
    REQUIRE(TF::armyAhead(22, 24));
    REQUIRE_FALSE(TF::armyAhead(23, 24));
    REQUIRE(TF::behindArmy(22, 20));
    REQUIRE_FALSE(TF::behindArmy(21, 20));
    // The belt: along the edge while short of its width, forward once wide; behind the army when the
    // army is ahead; clear of enemy buildings and turrets. No army needed.
    REQUIRE(TF::beltSite(24, 24, 3, 6, 0, 4, 0));             // widening along the edge
    REQUIRE_FALSE(TF::beltSite(23, 24, 3, 6, 0, 4, 0));       // not a tile ahead (a spear)
    REQUIRE_FALSE(TF::beltSite(20, 24, 3, 6, 0, 4, 0));       // not forward while short
    REQUIRE(TF::beltSite(20, 24, 6, 6, 0, 4, 0));             // forward once wide, no army
    REQUIRE_FALSE(TF::beltSite(24, 24, 6, 6, 0, 4, 0));       // not sideways once wide
    REQUIRE_FALSE(TF::beltSite(25, 24, 0, 6, 0, 4, 0));       // never behind the edge
    REQUIRE_FALSE(TF::beltSite(16, 24, 6, 6, 17, 4, 0));      // in the army's midst
    REQUIRE_FALSE(TF::beltSite(3, 24, 6, 6, 0, 4, 0));        // beside an enemy building
    REQUIRE_FALSE(TF::beltSite(20, 24, 6, 6, 0, 4, 1));       // an enemy turret reaches it
    REQUIRE(TF::kBeltEnemyStandoff < 10);                     // 3waysplit's bases stand within 10
    REQUIRE(TF::advanceSiteScore(21, 24, 0) > TF::advanceSiteScore(23, 24, 2));   // three tiles beat two neighbours
    REQUIRE(TF::advanceSiteScore(23, 24, 2) > TF::advanceSiteScore(23, 24, 0));   // then touching the line
    // (e) the drill: no nearer than 4 tiles to an enemy building, at most one enemy turret reaching.
    REQUIRE(TF::drillSite(11, 20, 0));
    REQUIRE(TF::drillSite(4, 20, 1));
    REQUIRE_FALSE(TF::drillSite(3, 20, 0));
    REQUIRE_FALSE(TF::drillSite(11, 20, 2));
    REQUIRE_FALSE(TF::drillSite(21, 20, 0));
    REQUIRE(TF::drillSite(11, 5, 0));                                              // the mass widens behind its tip
    REQUIRE(TF::drillSite(12, 5, 0));
    REQUIRE_FALSE(TF::drillSite(13, 5, 0));                                        // but not past Stefan's stand-off
    REQUIRE_FALSE(TF::drillSite(11, 5, 2));
    REQUIRE(TF::drillFactoryHold(250) == 1250);
    REQUIRE(TF::drillSiteScore(9, 0, 0) > TF::drillSiteScore(12, 0, 4));          // nearer first, by 3 tiles
    REQUIRE(TF::drillSiteScore(10, 0, 3) > TF::drillSiteScore(9, 0, 0));          // touching 3 beats a tile nearer
    REQUIRE(TF::drillSiteScore(9, 0, 3) > TF::drillSiteScore(9, 0, 0));           // then the mass
    // (c) defenders' value in range first, a reaching enemy turret costs 3,000 credits of them.
    REQUIRE(TF::creepSiteScore(4000, 0, 2) > TF::creepSiteScore(4000, 1, 2));
    REQUIRE(TF::creepSiteScore(5000, 1, 2) > TF::creepSiteScore(1000, 0, 2));
    // Creep: in range of a defender (range 8: 1..7 tiles), at most one enemy turret reaching.
    REQUIRE(TF::creepSite(7, 8, 1));
    REQUIRE(TF::creepSite(1, 8, 0));
    REQUIRE_FALSE(TF::creepSite(8, 8, 0));
    REQUIRE_FALSE(TF::creepSite(0, 8, 0));
    REQUIRE_FALSE(TF::creepSite(5, 8, 2));
    // (d) rear only far from enemy buildings and contacts; four a review above the keep.
    REQUIRE(TF::rearTurret(28, 5, 0));
    REQUIRE_FALSE(TF::rearTurret(27, 5, 0));
    REQUIRE_FALSE(TF::rearTurret(40, 5, 300));
    // On a big map the whole base is far from the enemy: only 16+ tiles behind its edge retires.
    REQUIRE_FALSE(TF::rearTurret(90, 80, 0));
    REQUIRE(TF::rearTurret(96, 80, 0));
    REQUIRE(TF::retireAllowance(100, 20) == 4);
    REQUIRE(TF::retireAllowance(22, 20) == 2);
    REQUIRE(TF::retireAllowance(14, 3) == 2);   // the floor of twelve
    REQUIRE(TF::retireAllowance(10, 3) == 0);
}

TEST_CASE("TurretFront: police upkeep of one Rocket Turret a minute", "[ai][turret-front][policy]") {
    // 15 credits a city year: 7.5 a minute with Slow City Year (7500 cycles), 15 without.
    REQUIRE(TF::upkeepMilliPerMinute(100, 7500) == 7500);
    REQUIRE(TF::upkeepMilliPerMinute(100, 3750) == 15000);
    REQUIRE(TF::upkeepMilliPerMinute(50, 7500) == 3750);
    REQUIRE(TF::upkeepMilliPerMinute(0, 7500) == 0);
}

TEST_CASE("TurretFront switches default off, round trip and hash only when on", "[ai][turret-front][config]") {
    const QuantBotConfig defaults;
    REQUIRE_FALSE(defaults.force.frontTurrets);
    REQUIRE_FALSE(defaults.force.advancingTurrets);
    REQUIRE_FALSE(defaults.force.turretCreep);
    REQUIRE_FALSE(defaults.force.rearTurretRetire);
    REQUIRE_FALSE(defaults.force.turretDrill);
    const char* keys[] = {"FrontTurrets", "AdvancingTurrets", "TurretCreep", "RearTurretRetire", "TurretDrill"};
    std::vector<std::string> hashes{defaults.getConfigHash()};
    for (const char* key : keys) {
        const auto path = std::filesystem::temp_directory_path() / (std::string("quantbot-turret-front-") + key + ".ini");
        {
            std::ofstream file(path);
            file << "[Force Concentration]\n" << key << "=true\n";
        }
        QuantBotConfig loaded;
        REQUIRE(loaded.applyOverrides(path.string()));
        const int on = int(loaded.force.frontTurrets) + int(loaded.force.advancingTurrets)
            + int(loaded.force.turretCreep) + int(loaded.force.rearTurretRetire) + int(loaded.force.turretDrill);
        REQUIRE(on == 1);
        for (const auto& hash : hashes) REQUIRE(loaded.getConfigHash() != hash);
        hashes.push_back(loaded.getConfigHash());
        REQUIRE(loaded.save(path.string()));
        QuantBotConfig restored;
        REQUIRE(restored.applyOverrides(path.string()));
        REQUIRE(restored.getConfigHash() == loaded.getConfigHash());
        std::filesystem::remove(path);
    }
}
