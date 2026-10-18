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

// QuantBot strategies and the test-only play styles (docs/ai-play-styles.md).
//
// Strategies (production): the unit-mix strategy and tempo of QuantBot::strategy, picked once per
// game by QuantBotStrategy::pick from the game seed, the house and the hashed config, and saved.
// The mix is the prior the learned unit mix starts from (QuantBotBuild.cpp), the tempo the wave
// readiness and the smallest wave (QuantBotForce.cpp, QuantBotArmy.cpp).
//
// Play styles (test-only): the behaviours of QuantBot::PlayStyle that no QuantBot Config.ini key
// can express - a style composition, counter-composition and harvester raids. The harness
// (tests/ai/formation-duel.inc) sets QuantBot::playStyle; the game never does, so every real game
// keeps the inert defaults.
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

std::array<bool,8> QuantBot::fieldableMixSlots() const {
    const int houseID = getHouse()->getHouseID();
    auto buildable = [&](Uint32 item) {
        const auto& spec = currentGame->objectData.data[item][houseID];
        return spec.enabled && spec.price > 0 && spec.builder != ItemID_Invalid
            && spec.techLevel <= currentGame->techLevel;
    };
    std::array<bool, 8> slots{};
    const std::array<Uint32, 8> items = {Unit_Tank, Unit_SiegeTank, Unit_Launcher, Unit_Devastator,
        Unit_Ornithopter, Unit_Trike, Unit_RaiderTrike, Unit_Quad};
    for (size_t i = 0; i < items.size(); ++i) slots[i] = buildable(items[i]);
    slots[3] = buildable(Unit_Devastator) || buildable(Unit_SonicTank) || buildable(Unit_Deviator);
    return slots;
}

void QuantBot::pickStrategy() {
    if (strategyPicked || currentGame == nullptr || getHouse() == nullptr) return;
    const int houseID = getHouse()->getHouseID();
    auto buildable = [&](Uint32 item) {
        const auto& spec = currentGame->objectData.data[item][houseID];
        return spec.enabled && spec.price > 0 && spec.builder != ItemID_Invalid
            && spec.techLevel <= currentGame->techLevel;
    };
    const std::array<bool, 8> slots = fieldableMixSlots();
    const bool infantry = buildable(Unit_Soldier) || buildable(Unit_Infantry)
        || buildable(Unit_Trooper) || buildable(Unit_Troopers);
    const bool active = autonomousCustomHardBrutal();
    const Uint32 seed = currentGame->getGameInitSettings().getRandomSeed();
    strategy = active ? QuantBotStrategy::pick(botConfig().strategy, seed, houseID,
                                                QuantBotStrategy::eligibleMixes(slots, infantry))
                      : QuantBotStrategy::Choice{};
    // Tuned and Bounded resolve their starting ratios now, once: the house's tuned ratios or the
    // draw of seed and house, bounded over what the house can produce (saved, SAVEGAMEVERSION 9866).
    strategyPrior = {};
    if (strategy.mix == QuantBotStrategy::Mix::Tuned) {
        const auto& tuned = botConfig().getTunedRatios(houseID);
        if (*std::max_element(tuned.begin(), tuned.end()) > 0) strategyPrior = QuantBotStrategy::bound(tuned, slots);
    } else if (strategy.mix == QuantBotStrategy::Mix::Bounded) {
        strategyPrior = QuantBotStrategy::randomMix(seed, houseID, slots);
    }
    strategyPicked = true;
    resolveStrategyProfile();
    const std::string start = QuantBotStrategy::ratiosText(startingPrior(slots));
    SDL_Log("QUANTBOT_STRATEGY: house=%d mix=%s tempo=%s mode=%s active=%d start_mix=%s", houseID,
            QuantBotStrategy::name(strategy.mix), QuantBotStrategy::name(strategy.tempo),
            QuantBotStrategy::modeText(botConfig().strategy).c_str(), int(active), start.c_str());
    if (AITelemetry::log().enabled())
        traceDecision("strategy", AITelemetry::Record().set("mix", QuantBotStrategy::name(strategy.mix))
            .set("tempo", QuantBotStrategy::name(strategy.tempo))
            .set("mode", QuantBotStrategy::modeText(botConfig().strategy))
            .set("mix_weights", QuantBotStrategy::weightsText(botConfig().strategy.mixWeights))
            .set("tempo_weights", QuantBotStrategy::weightsText(botConfig().strategy.tempoWeights))
            .set("start_mix", start)
            .set("seed", int64_t(seed)).set("active", active));
}

void QuantBot::resolveStrategyProfile() {
    strategyProfile = QuantBotStrategy::profile(strategy.mix);
    if (strategy.mix != QuantBotStrategy::Mix::Tuned && strategy.mix != QuantBotStrategy::Mix::Bounded) return;
    for (size_t i = 0; i < strategyPrior.size(); ++i) strategyProfile.prior[i] = strategyPrior[i];
    // No tuned ratios for this house: the configured opening, as Learned.
    strategyProfile.priorBps = *std::max_element(strategyPrior.begin(), strategyPrior.end()) > 0 ? 10000 : 0;
}

const QuantBotStrategy::MixProfile* QuantBot::strategyMix() const {
    if (!strategyPicked || strategy.mix == QuantBotStrategy::Mix::Learned || !autonomousCustomHardBrutal()) return nullptr;
    return &strategyProfile;
}

UnitMixPolicy::Weights QuantBot::startingPrior(const std::array<bool,8>& available) const {
    const auto& ratios = botConfig().getRatios(getHouse()->getHouseID());
    UnitMixPolicy::Weights configured{};
    configured[0] = static_cast<int>(ratios.tank*10000);
    configured[1] = static_cast<int>(ratios.siegeTank*10000);
    configured[2] = static_cast<int>(ratios.launcher*10000);
    configured[3] = static_cast<int>(ratios.special*10000);
    configured[4] = static_cast<int>(ratios.ornithopter*10000);
    // Without the bounds (test style): the 1.0.847 opening over this pass's producible slots.
    if (!style().startBounds) {
        const auto opening = UnitMixPolicy::openingMix(currentGame->techLevel, configured, available);
        UnitMixPolicy::Weights prior{};
        for (size_t i = 0; i < 8; ++i) prior[i] = opening[i];
        if (const auto* mix = strategyMix())
            prior = QuantBotStrategy::applyPrior(prior, mix->counter ? enemyCounterPrior() : mix->prior, mix->priorBps, available);
        return prior;
    }
    // The starting mix is defined over the slots the house can produce in this game (ObjectData,
    // tech level; plus anything producible now) and bounded there: every such slot 2%-50%
    // (docs/ai-play-styles.md, section 8). A pass then plays it over the slots producible at that
    // pass, renormalised - the factories not yet standing do not re-shape it.
    std::array<bool,8> slots = fieldableMixSlots();
    for (size_t i = 0; i < 8; ++i) slots[i] = slots[i] || available[i];
    const auto opening = UnitMixPolicy::openingMix(currentGame->techLevel, configured, slots);
    UnitMixPolicy::Weights prior{};
    for (size_t i = 0; i < 8; ++i) prior[i] = opening[i];
    // A mix strategy replaces this prior with its own starting ratios (or, Counter, with the counter
    // of the remembered enemy army).
    if (const auto* mix = strategyMix())
        prior = QuantBotStrategy::applyPrior(prior, mix->counter ? enemyCounterPrior() : mix->prior, mix->priorBps, slots);
    const auto bounded = QuantBotStrategy::bound(prior, slots);
    UnitMixPolicy::Weights masked{};
    for (size_t i = 0; i < 8; ++i) masked[i] = available[i] ? bounded[i] : 0;
    const auto mix = UnitMixPolicy::normalize(masked);
    for (size_t i = 0; i < 8; ++i) prior[i] = mix[i];
    return prior;
}

const QuantBotStrategy::TempoProfile* QuantBot::strategyTempo() const {
    if (!strategyPicked || strategy.tempo == QuantBotStrategy::Tempo::Base || !autonomousCustomHardBrutal()) return nullptr;
    const auto& tempo = QuantBotStrategy::profile(strategy.tempo);
    if (tempo.minutes > 0 && getGameCycleCount() >= Uint32(MILLI2CYCLES(tempo.minutes * 60000))) return nullptr;
    return &tempo;
}

bool QuantBot::lightFirstOpening() const {
    if (style().active()) return style().lightFirst;
    const auto* tempo = strategyTempo();
    return tempo && tempo->lightFirst;
}

int QuantBot::minWaveUnits() const {
    if (style().active()) return style().minWaveUnits;
    const auto* tempo = strategyTempo();
    return tempo ? tempo->minWaveUnits : PlayStyle{}.minWaveUnits;
}

int QuantBot::minWaveValue() const {
    if (style().active()) return style().minWaveValue;
    const auto* tempo = strategyTempo();
    return tempo ? tempo->minWaveValue : PlayStyle{}.minWaveValue;
}

UnitMixPolicy::Weights QuantBot::enemyCounterPrior() const {
    // The remembered enemy ground army by class, plus the hostile wing in sight.
    int64_t light = 0, armour = 0, launchers = 0;
    for (const auto& [id, sighting] : enemySightings) {
        const auto* unit = dynamic_cast<const UnitBase*>(getObject(id));
        if (!unit) continue;
        const Uint32 item = unit->getItemID();
        if (launcherItem(item)) launchers += sighting.value;
        else if (lightItem(item)) light += sighting.value;
        else armour += sighting.value;
    }
    const int64_t air = int64_t(visibleHostileAircraft())
        * std::max(1, currentGame->objectData.data[Unit_Ornithopter][getHouse()->getHouseID()].price);
    return QuantBotStrategy::counterPrior(light, armour, launchers, air);
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
        const auto counter = enemyCounterPrior();
        if (*std::max_element(counter.begin(), counter.end()) > 0) blend(counter, s.counterBps);
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
