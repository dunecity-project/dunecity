// SPDX-License-Identifier: GPL-2.0-or-later
#include <MissionObjectives.h>

#include <FileClasses/INIFile.h>
#include <Game.h>
#include <House.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CitySimulation.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <misc/format.h>
#include <FileClasses/TextManager.h>
#include <globals.h>

#include <algorithm>


using namespace MissionObjectivePolicy;

int MissionObjectivePolicy::enemyStrengthPermille(const INIFile& inifile, const std::string& difficulty) {
    if(difficulty.empty() || !inifile.hasKey("DIFFICULTY", difficulty)) return 1000;
    const double value = inifile.getDoubleValue("DIFFICULTY", difficulty, 1.0);
    return std::clamp(static_cast<int>(value * 1000.0 + 0.5), 250, 10000);
}

namespace {

constexpr const char* kSection = "OBJECTIVES";

struct KeySpec { Goal goal; const char* key; int low; int high; };
constexpr KeySpec kKeys[] = {
    {Population, "Population", 1, 10000000},
    {Credits, "Credits", 1, House::MAX_GAME_CREDITS},
    {Crime, "MaxCrime", 0, 255},
    {LandValue, "MinLandValue", 1, 250},
    {Pollution, "MaxPollution", 0, 255},
};

std::string clock(Uint32 cycles) {
    const Uint32 seconds = cycles * GAMESPEED_DEFAULT / 1000;
    return fmt::sprintf("%d:%02d", seconds / 60, seconds % 60);
}

std::string goalName(Goal goal, int target) {
    switch(goal) {
        case Population: return fmt::sprintf(_("a population of %s"), groupedNumber(target));
        case Credits:    return fmt::sprintf(_("%s credits in the treasury"), groupedNumber(target));
        case Crime:      return fmt::sprintf(_("crime of at most %d"), target);
        case LandValue:  return fmt::sprintf(_("land value of at least %d"), target);
        case Pollution:  return fmt::sprintf(_("pollution of at most %d"), target);
        default:         return _("every enemy house defeated");
    }
}

} // namespace

void MissionObjectives::loadFromINI(const INIFile& inifile) {
    targets = Targets{};
    enemyFirstAttackCycle = 0;
    startCredits = -1;
    holdingSinceCycle = INVALID_GAMECYCLE;
    announcedMask = 0;
    if(!inifile.hasSection(kSection)) return;
    for(const auto& spec : kKeys) {
        if(!inifile.hasKey(kSection, spec.key)) continue;
        targets.used[spec.goal] = true;
        targets.value[spec.goal] = std::clamp(inifile.getIntValue(kSection, spec.key, spec.low), spec.low, spec.high);
    }
    if(inifile.getBoolValue(kSection, "DefeatEnemies", false)) targets.used[DefeatEnemies] = true;
    targets.holdSeconds = std::clamp(inifile.getIntValue(kSection, "HoldSeconds", 30), 0, 600);
    enemyFirstAttackCycle = MILLI2CYCLES(static_cast<Uint32>(std::clamp(inifile.getIntValue("EVENTS", "FirstEnemyAttack", 0), 0, 120)) * 60 * 1000);
}

void MissionObjectives::save(OutputStream& stream) const {
    Uint32 used = 0;
    for(int g = 0; g < GoalCount; ++g) if(targets.used[g]) used |= 1u << g;
    stream.writeUint32(used);
    for(int g = 0; g < GoalCount; ++g) stream.writeSint32(targets.value[g]);
    stream.writeSint32(targets.holdSeconds);
    stream.writeUint32(holdingSinceCycle);
    stream.writeUint32(announcedMask);
    stream.writeUint32(deadlineCycle);
    stream.writeBool(deadlineWins);
    stream.writeUint32(enemyFirstAttackCycle);
    stream.writeSint32(startCredits);
    stream.writeBool(conquestWins);
}

void MissionObjectives::load(InputStream& stream) {
    const Uint32 used = stream.readUint32();
    for(int g = 0; g < GoalCount; ++g) targets.used[g] = (used & (1u << g)) != 0;
    for(int g = 0; g < GoalCount; ++g) targets.value[g] = stream.readSint32();
    targets.holdSeconds = stream.readSint32();
    holdingSinceCycle = stream.readUint32();
    announcedMask = stream.readUint32();
    deadlineCycle = stream.readUint32();
    deadlineWins = stream.readBool();
    enemyFirstAttackCycle = stream.readUint32();
    startCredits = stream.readSint32();
    conquestWins = stream.readBool();
}

Metrics MissionObjectives::measure(const Game& constGame, const House& house) {
    auto& game = const_cast<Game&>(constGame);
    Metrics m;
    m.credits = house.getCredits();
    const auto* sim = game.isCitySimEnabled() ? game.getCitySimulation() : nullptr;
    const int houseID = house.getHouseID();
    if(sim && sim->isInitialized() && houseID >= 0 && houseID < DuneCity::kMaxCityHouses) {
        m.population = sim->getHouseState(houseID).getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier;
        const auto& env = sim->getEnvironmentStatus(houseID);
        m.sampled = env.sampledStructures > 0;
        m.crime = env.averageCrime;
        m.landValue = env.averageLandValue;
        m.pollution = env.averagePollution;
    }
    for(int h = 0; h < NUM_HOUSES; ++h) {
        const House* other = game.getHouse(h);
        if(other && other->isAlive() && other->getTeamID() != 0 && !other->isNatives()
           && other->getTeamID() != house.getTeamID()) ++m.enemiesLeft;
    }
    return m;
}

void MissionObjectives::update(Game& game) {
    if(!isActive() || game.isGameFinished()) return;
    const Uint32 now = game.getGameCycleCount();
    if(now % kEvaluateEveryCycles != 0) return;
    House* house = game.getOutcomeHouse();
    if(house == nullptr) return;
    const Metrics m = measure(game, *house);
    if(startCredits < 0) startCredits = m.credits;

    constexpr Uint32 kSummaryShown = 1u << 31;
    if((announcedMask & kSummaryShown) == 0) {
        announcedMask |= kSummaryShown;
        game.addToNewsTicker(summary());
    }

    // Announce each goal the first time it is reached; a goal that slips is shown in the panel.
    for(int g = 0; g < GoalCount; ++g) {
        if(!targets.used[g] || (announcedMask & (1u << g)) != 0 || !met(targets, m, static_cast<Goal>(g))) continue;
        announcedMask |= 1u << g;
        if(g == DefeatEnemies) continue; // House::lose() already reports each defeat.
        game.addToNewsTicker(_("Objective reached: ") + goalName(static_cast<Goal>(g), targets.value[g]));
    }

    // A compact trace every two game minutes (read by tests/ai/scenario-matrix.py). The bucket
    // is runtime-only: it decides nothing but a log line.
    if(const Uint32 bucket = now / MILLI2CYCLES(120000); bucket != lastSampleBucket) {
        lastSampleBucket = bucket;
        SDL_Log("MISSION_OBJECTIVES_SAMPLE: minute=%u population=%d credits=%d crime=%d land=%d pollution=%d sampled=%d enemies=%d",
                now / MILLI2CYCLES(60000), m.population, m.credits, m.crime, m.landValue, m.pollution, int(m.sampled), m.enemiesLeft);
    }

    const bool all = allMet(targets, m);
    holdingSinceCycle = holdingSince(all, holdingSinceCycle, now);
    const Uint32 holdCycles = MILLI2CYCLES(static_cast<Uint32>(targets.holdSeconds) * 1000);
    if(held(holdingSinceCycle, now, holdCycles)) {
        SDL_Log("MISSION_OBJECTIVES met at cycle %u: population=%d credits=%d crime=%d land=%d pollution=%d enemies=%d",
                now, m.population, m.credits, m.crime, m.landValue, m.pollution, m.enemiesLeft);
        house->win();
    }
}

std::vector<std::pair<std::string, bool>> MissionObjectives::describe(const Game& game) const {
    std::vector<std::pair<std::string, bool>> lines;
    // Other maps keep their old screen unless they set city goals.
    if(!isActive() && !game.getGameInitSettings().isDuneCityCampaign()) return lines;
    const House* house = game.getOutcomeHouse();
    if(house == nullptr) return lines;
    const Metrics m = measure(game, *house);
    const auto add = [&](Goal g, const std::string& text) { lines.emplace_back(text, met(targets, m, g)); };
    if(targets.used[Population])
        add(Population, fmt::sprintf(_("Population %s / %s"), groupedNumber(m.population), groupedNumber(targets.value[Population])));
    if(targets.used[Credits])
        add(Credits, fmt::sprintf(_("Treasury %s / %s"), groupedNumber(m.credits), groupedNumber(targets.value[Credits])));
    if(targets.used[Crime])
        add(Crime, fmt::sprintf(_("Crime %s (%d) - at most %d"), m.sampled ? DuneCity::crimeCategory(m.crime) : "-",
                                m.crime, targets.value[Crime]));
    if(targets.used[LandValue])
        add(LandValue, fmt::sprintf(_("Land value %s (%d) - at least %d"), m.sampled ? DuneCity::landValueCategory(m.landValue) : "-",
                                    m.landValue, targets.value[LandValue]));
    if(targets.used[Pollution])
        add(Pollution, fmt::sprintf(_("Pollution %d - at most %d"), m.pollution, targets.value[Pollution]));
    if(targets.used[DefeatEnemies])
        add(DefeatEnemies, m.enemiesLeft == 0 ? std::string(_("Enemies defeated"))
                                              : fmt::sprintf(_("Defeat the enemy: %d houses left"), m.enemiesLeft));
    if(conquestWins && !targets.used[DefeatEnemies]) {
        // The usual Dune victory still stands beside any city goals.
        const std::string text = isActive() || deadlineCycle > 0 ? _("Or destroy every enemy house: %d left")
                                                           : _("Destroy every enemy house: %d left");
        lines.emplace_back(fmt::sprintf(text, m.enemiesLeft), m.enemiesLeft == 0);
    }
    const Uint32 now = game.getGameCycleCount();
    const Uint32 holdCycles = MILLI2CYCLES(static_cast<Uint32>(targets.holdSeconds) * 1000);
    if(holdingSinceCycle != INVALID_GAMECYCLE && holdCycles > 0) {
        const Uint32 elapsed = now >= holdingSinceCycle ? now - holdingSinceCycle : 0;
        lines.emplace_back(fmt::sprintf(_("Hold for %s more"), clock(holdCycles > elapsed ? holdCycles - elapsed : 0)), true);
    }
    if(deadlineCycle > 0) {
        const Uint32 left = deadlineCycle > now ? deadlineCycle - now : 0;
        lines.emplace_back(fmt::sprintf(deadlineWins ? _("Hold out: %s left") : _("Time left: %s"), clock(left)), deadlineWins);
    }
    return lines;
}

std::string MissionObjectives::summary() const {
    std::vector<std::string> parts;
    for(int g = 0; g < GoalCount; ++g)
        if(targets.used[g]) parts.push_back(goalName(static_cast<Goal>(g), targets.value[g]));
    std::string text = _("Objective: ");
    for(size_t i = 0; i < parts.size(); ++i) {
        if(i > 0) text += (i + 1 == parts.size()) ? _(" and ") : std::string(", ");
        text += parts[i];
    }
    return text;
}
