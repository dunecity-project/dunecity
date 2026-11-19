// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MISSIONOBJECTIVES_H
#define MISSIONOBJECTIVES_H

#include <DataTypes.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class INIFile;
class InputStream;
class OutputStream;

/**
    Authored DuneCity mission goals: the optional [OBJECTIVES] section of a scenario INI
    (docs/city-campaign.md). They are an addition to WinFlags, never a replacement:

      [OBJECTIVES]
      Population=8000    ; displayed city population of the player's house (internal x20)
      Credits=20000      ; the treasury the sidebar shows (House::getCredits)
      MaxCrime=63        ; average crime at the house's developed buildings, 0..255 (Safe < 64)
      MinLandValue=80    ; average land value, 0..250 (Middle Class from 80)
      MaxPollution=96    ; average pollution, 0..255
      DefeatEnemies=1    ; every enemy house must also be gone
      HoldSeconds=30     ; every goal must hold this long without a break (default 30)

    The mission is won when every listed goal holds at the same time for HoldSeconds. A map whose
    WinFlags keep AI_NO_BUILDINGS is still also won by destroying every enemy (the usual Dune
    rule); WinFlags=2 with DefeatEnemies=1 makes the city goals and the conquest both required.
    A deadline is the existing TimeOut with WinFlags 0x08 and no LoseFlags 0x08.

    WinFlags bit 0x10 stays meaningless (protocol 85): only this explicit section adds city goals.
    The evaluation reads deterministic simulation state only and decides nothing but the local
    game's end, the same as the quota rule.
*/
namespace MissionObjectivePolicy {

enum Goal : int { Population = 0, Credits, Crime, LandValue, Pollution, DefeatEnemies, GoalCount };

/// The targets; a goal is used when its `used` flag is set.
struct Targets {
    std::array<bool, GoalCount> used{};
    std::array<int, GoalCount> value{};
    int holdSeconds = 30;

    bool any() const {
        for(const bool u : used) if(u) return true;
        return false;
    }
};

/// What the house has now. `sampled` is false while the city has no developed building:
/// a crime, pollution or land value goal never passes on an empty city.
struct Metrics {
    int population = 0;   ///< displayed
    int credits = 0;
    int crime = 0;
    int landValue = 0;
    int pollution = 0;
    bool sampled = false;
    int enemiesLeft = 0;
};

inline bool met(const Targets& targets, const Metrics& m, Goal goal) {
    if(!targets.used[goal]) return true;
    const int target = targets.value[goal];
    switch(goal) {
        case Population:    return m.population >= target;
        case Credits:       return m.credits >= target;
        case Crime:         return m.sampled && m.crime <= target;
        case LandValue:     return m.sampled && m.landValue >= target;
        case Pollution:     return m.sampled && m.pollution <= target;
        case DefeatEnemies: return m.enemiesLeft == 0;
        default:            return true;
    }
}

inline bool allMet(const Targets& targets, const Metrics& m) {
    if(!targets.any()) return false;
    for(int g = 0; g < GoalCount; ++g) if(!met(targets, m, static_cast<Goal>(g))) return false;
    return true;
}

/// The cycle the goals started holding: `previous` while they keep holding (or `now` when they
/// have just started), INVALID_GAMECYCLE once any goal fails.
inline Uint32 holdingSince(bool allGoalsMet, Uint32 previous, Uint32 now) {
    if(!allGoalsMet) return INVALID_GAMECYCLE;
    return previous == INVALID_GAMECYCLE ? now : previous;
}

inline bool held(Uint32 since, Uint32 now, Uint32 holdCycles) {
    return since != INVALID_GAMECYCLE && now >= since && now - since >= holdCycles;
}

/// "8,000" for 8000: the targets are read by players, not engineers.
inline std::string groupedNumber(int value) {
    const std::string digits = std::to_string(value < 0 ? -static_cast<long long>(value) : static_cast<long long>(value));
    std::string out;
    for(size_t i = 0; i < digits.size(); ++i) {
        if(i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}

} // namespace MissionObjectivePolicy

class Game;
class House;

/// The goals of the running mission and how long they have held (saved from SAVEGAMEVERSION 9887).
class MissionObjectives {
public:
    MissionObjectivePolicy::Targets targets;
    /// First cycle all goals held without a break, INVALID_GAMECYCLE when they do not hold.
    Uint32 holdingSinceCycle = INVALID_GAMECYCLE;
    /// Goals already announced as reached in the news ticker (bit per Goal).
    Uint32 announcedMask = 0;
    /// The cycle of the authored TimeOut, 0 when the map has none (display only: the
    /// TimeoutTrigger decides it).
    Uint32 deadlineCycle = 0;
    /// True when the TimeOut is a survival victory (LoseFlags 0x08) rather than a deadline.
    bool deadlineWins = false;
    /// [EVENTS] FirstEnemyAttack=N: no house hostile to the objective house launches an attack
    /// wave before this cycle (0 = none). A campaign mission's opening grace period.
    Uint32 enemyFirstAttackCycle = 0;
    /// The objective house's treasury at the first evaluation (-1 before it): a treasury goal's
    /// progress is what the house has added since.
    Sint32 startCredits = -1;
    /// Destroying every enemy house also wins (WinFlags/LoseFlags bit 0x01): the panel says so.
    bool conquestWins = false;

    bool isActive() const { return targets.any(); }
    /// The opening grace period still holds at @a cycle.
    bool enemyAttacksHeld(Uint32 cycle) const { return cycle < enemyFirstAttackCycle; }

    /// Reads [OBJECTIVES]; leaves the targets empty when the section is absent.
    void loadFromINI(const INIFile& inifile);
    void save(OutputStream& stream) const;
    void load(InputStream& stream);

    /// The outcome house's current values.
    static MissionObjectivePolicy::Metrics measure(const Game& game, const House& house);

    /// Called once per game cycle after the city simulation: ends the mission when the goals have held.
    void update(Game& game);

    /// The lines of the in-game objectives panel (empty without a house, or when the map has no
    /// goals and is not a DuneCity campaign mission).
    std::vector<std::pair<std::string, bool>> describe(const Game& game) const;

    /// One sentence for the news ticker at the start of the mission.
    std::string summary() const;

    /// The two-minute sample last logged (runtime only, never saved or simulated).
    Uint32 lastSampleBucket = INVALID_GAMECYCLE;

    /// Evaluated every this many cycles (about half a second at the default speed).
    static constexpr Uint32 kEvaluateEveryCycles = 32;
};

#endif // MISSIONOBJECTIVES_H
