/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

// Test-only QuantBot play styles (docs/ai-play-styles.md): the behaviours of QuantBot::PlayStyle
// that no QuantBot Config.ini key can express - a style composition, counter-composition and
// harvester raids. The harness (tests/ai/formation-duel.inc) sets QuantBot::playStyle; the game
// never does, so every real game keeps the inert defaults.
//
// Deterministic: scans run in the fixed unit list order, raiders are kept in id order, and only
// integer arithmetic decides.

#include "QuantBotInternal.h"

namespace {

int tiles(Coord a, Coord b) { return blockDistance(a, b).lround(); }

bool lightItem(Uint32 item) {
    return QuantBotBuildPolicy::isLightRaider(item) || item == Unit_Infantry || item == Unit_Soldier
        || item == Unit_Trooper || item == Unit_Troopers;
}

bool launcherItem(Uint32 item) {
    return item == Unit_Launcher || item == Unit_EliteLauncher || item == Unit_Deviator;
}

bool enemyHarvester(const ObjectBase* object) {
    return object && object->isAUnit()
        && (object->getItemID() == Unit_Harvester || object->getItemID() == Unit_RebelHarvester);
}

} // namespace

const QuantBot::PlayStyle& QuantBot::style() const {
    static const PlayStyle inert;
    return autonomousCustomHardBrutal() ? playStyle : inert;
}

UnitMixPolicy::Mix QuantBot::styledUnitMix(const UnitMixPolicy::Mix& mix, const std::array<bool,8>& available) const {
    const PlayStyle& s = style();
    UnitMixPolicy::Mix result = mix;
    auto blend = [&](const UnitMixPolicy::Weights& weights, int bps) {
        UnitMixPolicy::Weights masked{};
        for (size_t i = 0; i < masked.size(); ++i) masked[i] = available[i] ? std::max<int64_t>(0, weights[i]) : 0;
        const auto target = UnitMixPolicy::normalize(masked);
        if (*std::max_element(target.begin(), target.end()) == 0) return;
        const int share = std::clamp(bps, 0, 10000);
        UnitMixPolicy::Weights blended{};
        for (size_t i = 0; i < blended.size(); ++i)
            blended[i] = int64_t(result[i]) * (10000 - share) + int64_t(target[i]) * share;
        result = UnitMixPolicy::normalize(blended);
    };
    if (s.mixBps > 0 && (s.mixMinutes <= 0 || getGameCycleCount() < Uint32(MILLI2CYCLES(s.mixMinutes * 60000))))
        blend(s.mix, s.mixBps);
    if (s.counterBps > 0) {
        // The remembered enemy ground army by class, plus the hostile wing in sight.
        int64_t light = 0, armour = 0, launchers = 0, total = 0;
        for (const auto& [id, sighting] : enemySightings) {
            const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
            if (!unit) continue;
            const Uint32 item = unit->getItemID();
            if (launcherItem(item)) launchers += sighting.value;
            else if (lightItem(item)) light += sighting.value;
            else armour += sighting.value;
            total += sighting.value;
        }
        const int64_t air = int64_t(visibleHostileAircraft())
            * std::max(1, currentGame->objectData.data[Unit_Ornithopter][getHouse()->getHouseID()].price);
        total += air;
        if (total > 0) {
            auto share = [&](int64_t part, int64_t weight) { return part * weight / total; };
            UnitMixPolicy::Weights counter{};
            counter[0] = 2000 + share(light, 6000) + share(launchers, 3000);                  // tanks
            counter[1] = 1000 + share(armour, 3000) + share(light, 3000);                     // siege tanks
            counter[2] = 1000 + share(armour, 6000) + share(air, 6000);                       // launchers
            counter[3] = 1500;                                                                // special
            counter[4] = 500 + share(armour, 2000);                                           // ornithopters
            counter[7] = 500 + share(launchers, 4000);                                        // quads
            blend(counter, s.counterBps);
        }
    }
    return result;
}

void QuantBot::updateStyleRaiders() {
    const int wanted = style().raidUnits;
    if (wanted <= 0) { styleRaiders.clear(); return; }
    auto usable = [&](const UnitBase* unit) {
        return unit && unit->getOwner() == getHouse() && unit->getHealth() > 0 && unit->isActive()
            && unit->isRespondable() && unit->isAGroundUnit() && QuantBotBuildPolicy::isLightRaider(unit->getItemID())
            && unitRole(unit, {UnitRole::Human, UnitRole::Repair, UnitRole::BaseDefence, UnitRole::LiveDefence,
                               UnitRole::Escort}) == UnitRole::Free;
    };
    for (auto it = styleRaiders.begin(); it != styleRaiders.end();)
        it = usable(dynamic_cast<const UnitBase*>(getObject(*it))) ? std::next(it) : styleRaiders.erase(it);
    if (int(styleRaiders.size()) >= wanted) return;
    // Top up in object-id order: the unit list order is the same on every peer, ids decide.
    std::vector<Uint32> free;
    for (const auto* unit : getUnitList())
        if (usable(unit) && !styleRaiders.count(unit->getObjectID())) free.push_back(unit->getObjectID());
    std::sort(free.begin(), free.end());
    for (const Uint32 id : free) {
        if (int(styleRaiders.size()) >= wanted) break;
        styleRaiders.insert(id);
    }
}

bool QuantBot::raidWithStyle(const UnitBase* raider) {
    const int myTeam = getHouse()->getTeamID();
    const ObjectBase* current = raider->getTarget();
    auto hostile = [&](const ObjectBase* object) {
        return object && object->getOwner() && object->getOwner()->getTeamID() != myTeam
            && object->getHealth() > 0 && object->isActive() && object->isVisible(myTeam);
    };
    if (hostile(current) && (enemyHarvester(current) || current->getItemID() == Structure_Refinery)) return true;
    // Nearest visible enemy harvester; with none, the nearest enemy refinery.
    const ObjectBase* best = nullptr;
    int bestTiles = std::numeric_limits<int>::max();
    for (const auto* unit : getUnitList())
        if (enemyHarvester(unit) && hostile(unit) && raider->canAttack(unit)) {
            const int distance = tiles(raider->getLocation(), unit->getLocation());
            if (distance < bestTiles) { best = unit; bestTiles = distance; }
        }
    if (!best)
        for (const auto* structure : getStructureList())
            if (structure->getItemID() == Structure_Refinery && hostile(structure) && raider->canAttack(structure)) {
                const int distance = tiles(raider->getLocation(), structure->getClosestPoint(raider->getLocation()));
                if (distance < bestTiles) { best = structure; bestTiles = distance; }
            }
    if (!best) return false;
    doAttackObject(raider, best, true);
    ++forceStats.raidOrders;
    return true;
}
