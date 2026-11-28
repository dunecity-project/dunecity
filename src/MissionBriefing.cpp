// SPDX-License-Identifier: GPL-2.0-or-later
// The parts of MissionObjectives that need no running game: reading [OBJECTIVES] and
// [DIFFICULTY], and the goals as the Mentat and the DuneCity Campaign menu tell them.
#include <MissionObjectives.h>

#include <FileClasses/INIFile.h>
#include <dunecity/CityEffects.h>
#include <misc/format.h>

#include <algorithm>

using namespace MissionObjectivePolicy;

namespace {

constexpr const char* kSection = "OBJECTIVES";

struct KeySpec { Goal goal; const char* key; int low; int high; };
constexpr KeySpec kKeys[] = {
    {Population, "Population", 1, 10000000},
    {Credits, "Credits", 1, kMaxCreditsGoal},
    {Crime, "MaxCrime", 0, 255},
    {LandValue, "MinLandValue", 1, 250},
    {Pollution, "MaxPollution", 0, 255},
};

/// Briefing lines are English, like the maps' own [BRIEFING] texts they are shown with; they are
/// put together from parts, which a translation could not reorder anyway.
std::string localized(const char* text) {
    return text;
}

/// "1 minute", "8 minutes", "45 seconds".
std::string duration(int seconds) {
    if(seconds >= 60 && seconds % 60 == 0) {
        const int minutes = seconds / 60;
        return fmt::sprintf(minutes == 1 ? localized("%d minute") : localized("%d minutes"), minutes);
    }
    return fmt::sprintf(seconds == 1 ? localized("%d second") : localized("%d seconds"), seconds);
}

std::string briefingGoal(Goal goal, int target) {
    switch(goal) {
        case Population: return fmt::sprintf(localized("%s people"), groupedNumber(target));
        case Credits:    return fmt::sprintf(localized("%s credits in the treasury"), groupedNumber(target));
        case Crime:      return fmt::sprintf(localized("crime at most %d (%s)"), target, DuneCity::crimeCategory(target));
        case LandValue:  return fmt::sprintf(localized("land value at least %d (%s)"), target, DuneCity::landValueCategory(target));
        case Pollution:  return fmt::sprintf(localized("pollution at most %d"), target);
        default:         return localized("every enemy house destroyed");
    }
}

} // namespace

int MissionObjectivePolicy::enemyStrengthPermille(const INIFile& inifile, const std::string& difficulty) {
    // The maps name Easy .. Brutal. QuantBot Easiest plays a mission as authored (1.0) unless
    // Easy's value is lower still, so it never starts stronger than Easy.
    if(difficulty == "Easiest" && !inifile.hasKey("DIFFICULTY", difficulty) && inifile.hasKey("DIFFICULTY", "Easy"))
        return std::min(1000, strengthPermille(inifile.getDoubleValue("DIFFICULTY", "Easy", 1.0)));
    if(difficulty.empty() || !inifile.hasKey("DIFFICULTY", difficulty)) return 1000;
    return strengthPermille(inifile.getDoubleValue("DIFFICULTY", difficulty, 1.0));
}

Targets MissionObjectivePolicy::targetsFromINI(const INIFile& inifile) {
    Targets out;
    if(!inifile.hasSection(kSection)) return out;
    for(const auto& spec : kKeys) {
        if(!inifile.hasKey(kSection, spec.key)) continue;
        out.used[spec.goal] = true;
        out.value[spec.goal] = std::clamp(inifile.getIntValue(kSection, spec.key, spec.low), spec.low, spec.high);
    }
    if(inifile.getBoolValue(kSection, "DefeatEnemies", false)) out.used[DefeatEnemies] = true;
    out.holdSeconds = std::clamp(inifile.getIntValue(kSection, "HoldSeconds", 30), 0, 600);
    return out;
}

Uint32 MissionObjectivePolicy::firstAttackCycles(const INIFile& inifile) {
    return MILLI2CYCLES(static_cast<Uint32>(std::clamp(inifile.getIntValue("EVENTS", "FirstEnemyAttack", 0), 0, 120)) * 60 * 1000);
}

std::vector<std::string> MissionObjectivePolicy::briefingLines(const Briefing& briefing) {
    // Each way to win is one line: the first is the goal, the others are alternatives.
    std::vector<std::string> wins;
    std::vector<std::string> goals;
    for(int g = 0; g < GoalCount; ++g)
        if(briefing.targets.used[g]) goals.push_back(briefingGoal(static_cast<Goal>(g), briefing.targets.value[g]));
    if(!goals.empty()) {
        std::string text;
        for(size_t i = 0; i < goals.size(); ++i) {
            if(i > 0) text += (i + 1 == goals.size()) ? localized(" and ") : std::string(", ");
            text += goals[i];
        }
        if(briefing.targets.holdSeconds > 0)
            text += fmt::sprintf(localized(", held for %s"), duration(briefing.targets.holdSeconds));
        wins.push_back(text);
    }
    const bool deadline = briefing.deadlineSeconds > 0;
    if(deadline && briefing.deadlineWins)
        wins.push_back(fmt::sprintf(localized("hold out for %s"), duration(briefing.deadlineSeconds)));
    if(briefing.conquestWins && !briefing.targets.used[DefeatEnemies])
        wins.push_back(localized("destroy every enemy house"));

    std::vector<std::string> lines;
    for(size_t i = 0; i < wins.size(); ++i) {
        std::string line = wins[i];
        if(i == 0) line = localized("Goal: ") + line;
        else line = localized("Or ") + line;
        lines.push_back(line + ".");
    }
    if(deadline && !briefing.deadlineWins)
        lines.push_back(fmt::sprintf(localized("Time limit: %s."), duration(briefing.deadlineSeconds)));
    if(briefing.firstAttackSeconds > 0) {
        const int minute = (briefing.firstAttackSeconds + 30) / 60;
        lines.push_back(minute < 1 ? localized("Enemy attacks can begin within the first minute.")
                                   : fmt::sprintf(localized("Enemy attacks can begin at minute %d."), minute));
    }
    return lines;
}

Briefing MissionObjectivePolicy::briefingFromINI(const INIFile& inifile, const std::string& difficulty) {
    Briefing briefing;
    briefing.targets = targetsFromINI(inifile);
    const int winFlags = inifile.getIntValue("BASIC", "WinFlags", 3);
    const int loseFlags = inifile.getIntValue("BASIC", "LoseFlags", 1);
    briefing.conquestWins = (winFlags & WINLOSEFLAGS_AI_NO_BUILDINGS) != 0 && (loseFlags & WINLOSEFLAGS_AI_NO_BUILDINGS) != 0;
    const int timeout = inifile.getIntValue("BASIC", "TimeOut", 0);
    if(timeout > 0 && (winFlags & WINLOSEFLAGS_TIMEOUT) != 0) {
        briefing.deadlineSeconds = timeout * 60;
        briefing.deadlineWins = (loseFlags & WINLOSEFLAGS_TIMEOUT) != 0;
    }
    const Uint32 grace = scaledGrace(firstAttackCycles(inifile), enemyStrengthPermille(inifile, difficulty));
    briefing.firstAttackSeconds = cyclesToSeconds(grace);
    return briefing;
}

