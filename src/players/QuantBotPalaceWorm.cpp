// QuantBot's Fremen Palace worm (match rule Fremen Palace Worm; include/units/PalaceWormRules.h).
//
// Stefan, 2026-11-27: "it has full control over the worm and can eat enemy units with it via hunt,
// or can explode to create spice where the ai wants near its base".
//
// Each AI pass, for the house's own worm that is neither eating nor already on its way to burst:
//  1. Prey (PalaceWorm >= 1): the enemy ground units it can eat (on sand in its sand region, see
//     Sandworm::canAttack). Each one is valued at its price plus the prices of the other prey
//     within kPreyCluster tiles, divided by (1 + distance / 16). The best is hunted when it is
//     worth kMinPreyValue credits and the worm can get there before it has to leave; the worm is
//     set to Hunt, so after each meal it takes the next unit of its own accord.
//  2. Spice (PalaceWorm <= 1): with no prey worth having, with one meal left before it would
//     leave sated, or when the way to the bloom site would use up the rest of its life, it is
//     ordered to burst at the bloom site: the sand tile near one of our refineries (or the base
//     centre) whose spice field would cover the most plain sand, away from enemy units.
// Stateless: every decision is read from the world, so a loaded game decides as the original.

#include "QuantBotInternal.h"

#include <players/QuantBot.h>

#include <Game.h>
#include <House.h>
#include <Map.h>
#include <mmath.h>
#include <structures/StructureBase.h>
#include <units/SandWorm.h>
#include <units/PalaceWormRules.h>

#include <algorithm>
#include <limits>

namespace {

constexpr int kPreyCluster = 3;          ///< Tiles around a prey unit whose other prey adds to its value
constexpr int kMinPreyValue = 250;       ///< Credits: below this the worm is worth more as spice
constexpr int kBloomSearch = 14;         ///< Tiles around a refinery searched for a bloom site
constexpr int kBloomEnemyClearance = 8;  ///< No bloom site this near an enemy ground unit
constexpr int kBloomMarginMs = 15000;    ///< Life kept in hand for the trip to the bloom site
constexpr int kLateBloomMs = 60000;      ///< From here on a hunting worm checks its way back to spice

/// Game cycles the worm needs to cover DISTANCE tiles on sand (Sandworm::getTerrainDifficulty).
Sint32 travelCycles(const Sandworm& worm, int distance) {
    const FixPoint speed = worm.getMaxSpeed();
    if(speed <= 0) return std::numeric_limits<Sint32>::max();
    return (FixPoint(distance * TILESIZE) * 5 / (speed * 4)).lround();
}

/// Plain sand tiles a bloom at SPOT would turn into spice (Map::createSpiceField).
int bloomGain(const Coord& spot) {
    int gain = 0;
    for(int dy = -PalaceWorm::kBloomRadius; dy <= PalaceWorm::kBloomRadius; ++dy) {
        for(int dx = -PalaceWorm::kBloomRadius; dx <= PalaceWorm::kBloomRadius; ++dx) {
            const Coord tile(spot.x + dx, spot.y + dy);
            if(!currentGameMap->tileExists(tile) || distanceFrom(spot, tile) > PalaceWorm::kBloomRadius) continue;
            if(currentGameMap->getTile(tile)->isSand()) ++gain;
        }
    }
    return gain;
}

} // namespace

Coord QuantBot::palaceWormBloomSite(const Sandworm* worm) const {
    if(!currentGameMap->tileExists(worm->getLocation())) return Coord::Invalid();
    const Uint32 region = currentGameMap->getTile(worm->getLocation())->getSandRegion();
    const int team = getHouse()->getTeamID();

    std::vector<Coord> anchors;
    for(const auto* structure : getStructureList())
        if(structure->getOwner() == getHouse() && structure->getItemID() == Structure_Refinery && structure->getHealth() > 0)
            anchors.push_back(structure->getCenterPoint() / TILESIZE);
    if(anchors.empty()) {
        const Coord centre = findBaseCentre(getHouse()->getHouseID());
        if(centre.isValid()) anchors.push_back(centre);
    }

    std::vector<Coord> enemies;
    for(const auto* unit : getUnitList())
        if(unit->getOwner() && unit->getOwner()->getTeamID() != team && !unit->getOwner()->isNatives()
           && unit->isActive() && unit->isAGroundUnit() && unit->canAttack())
            enemies.push_back(unit->getLocation());

    Coord best = Coord::Invalid();
    int bestScore = 0;
    for(const Coord& anchor : anchors) {
        // Every second tile keeps the search to a few hundred sites a refinery.
        for(int dy = -kBloomSearch; dy <= kBloomSearch; dy += 2) {
            for(int dx = -kBloomSearch; dx <= kBloomSearch; dx += 2) {
                const Coord spot(anchor.x + dx, anchor.y + dy);
                if(!worm->canPass(spot.x, spot.y) || currentGameMap->getTile(spot)->getSandRegion() != region) continue;
                if(std::any_of(enemies.begin(), enemies.end(), [&](const Coord& enemy) {
                       return blockDistance(spot, enemy).lround() <= kBloomEnemyClearance; })) continue;
                const int gain = bloomGain(spot);
                if(gain == 0) continue;
                // Near the refinery counts: a tile's worth of sand for every two tiles closer.
                const int score = gain * 4 - std::max(std::abs(dx), std::abs(dy)) * 2;
                if(score > bestScore) {
                    bestScore = score;
                    best = spot;
                }
            }
        }
    }
    return best;
}

void QuantBot::managePalaceWorm(const Sandworm* worm) {
    if(!worm->isPalaceWorm() || !worm->isActive() || worm->isEating() || worm->hasBloomOrder()) return;
    const int level = botConfig().force.palaceWorm;
    const Sint32 cyclesLeft = worm->getPalaceWormCyclesLeft();
    const bool lastMeal = worm->getKills() >= PalaceWorm::kMeals - 1;

    // Near the end of its life a hunting worm (level 1) turns back for the bloom site while it can.
    if(level == 1 && cyclesLeft < MILLI2CYCLES(kLateBloomMs)) {
        const Coord site = palaceWormBloomSite(worm);
        if(site.isValid() && travelCycles(*worm, blockDistance(worm->getLocation(), site).lround())
                                 + MILLI2CYCLES(kBloomMarginMs) >= cyclesLeft) {
            traceDecision("palace_worm_bloom", AITelemetry::Record().set("worm", worm->getObjectID())
                .set("x", site.x).set("y", site.y).set("gain", bloomGain(site)).set("kills", worm->getKills())
                .set("cycles_left", cyclesLeft).set("hurry", 1).set("level", level));
            doSpiceBloom(worm, site.x, site.y);
            return;
        }
    }

    // 1. Prey on the worm's sand.
    const UnitBase* prey = nullptr;
    if(level >= 1 && !(lastMeal && level == 1)) {
        std::vector<const UnitBase*> edible;
        for(const auto* unit : getUnitList())
            if(unit->getOwner() && unit->getOwner()->getTeamID() != getHouse()->getTeamID()
               && !unit->getOwner()->isNatives() && unit->isActive() && worm->canAttack(unit))
                edible.push_back(unit);
        int64_t bestScore = 0;
        for(const auto* candidate : edible) {
            int value = 0;
            for(const auto* other : edible)
                if(blockDistance(candidate->getLocation(), other->getLocation()).lround() <= kPreyCluster)
                    value += currentGame->objectData.data[other->getItemID()][other->getProductionHouseID()].price;
            if(value < kMinPreyValue) continue;
            const int distance = blockDistance(worm->getLocation(), candidate->getLocation()).lround();
            if(travelCycles(*worm, distance) >= cyclesLeft) continue;
            const int64_t score = int64_t(value) * 16 / (16 + distance);
            if(score > bestScore || (score == bestScore && prey && candidate->getObjectID() < prey->getObjectID())) {
                bestScore = score;
                prey = candidate;
            }
        }
    }
    if(prey != nullptr) {
        if(worm->getAttackMode() != HUNT) doSetAttackMode(worm, HUNT);
        const auto* current = worm->getTarget();
        if(current == nullptr || !worm->canAttack(current)) {
            doAttackObject(worm, prey, false);
            traceDecision("palace_worm_hunt", AITelemetry::Record().set("worm", worm->getObjectID())
                .set("prey", prey->getObjectID()).set("item", prey->getItemID())
                .set("x", prey->getX()).set("y", prey->getY()).set("kills", worm->getKills()));
        }
        return;
    }
    // A worm already chewing through prey the score no longer lists keeps at it.
    if(level >= 1 && worm->getTarget() != nullptr && worm->canAttack(worm->getTarget()) && !lastMeal) return;
    if(level == 2) {
        if(worm->getAttackMode() != HUNT) doSetAttackMode(worm, HUNT);
        return;
    }

    // 2. Spice near the base.
    const Coord site = palaceWormBloomSite(worm);
    if(site.isInvalid()) {
        // No sand near the base it can reach: it may as well eat whatever it finds.
        if(worm->getAttackMode() != HUNT) doSetAttackMode(worm, HUNT);
        return;
    }
    const int distance = blockDistance(worm->getLocation(), site).lround();
    const bool hurry = travelCycles(*worm, distance) + MILLI2CYCLES(kBloomMarginMs) >= cyclesLeft;
    traceDecision("palace_worm_bloom", AITelemetry::Record().set("worm", worm->getObjectID())
        .set("x", site.x).set("y", site.y).set("gain", bloomGain(site)).set("kills", worm->getKills())
        .set("cycles_left", cyclesLeft).set("hurry", hurry ? 1 : 0).set("level", level));
    doSpiceBloom(worm, site.x, site.y);
}
