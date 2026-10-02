/*
 *  QuantBotSchedulePolicy.h — pure AI update scheduling predicates.
 *
 *  QuantBot's heavy per-house pass runs every AIUPDATEINTERVAL cycles. Legacy
 *  modes keep the original single gate, `(cycle + houseID) % 50 == 0`, which
 *  bunches houses one cycle apart and runs unit management and base building in
 *  the same cycle.
 *
 *  Custom single-player instead splits that pass into two stateless phases at
 *  the SAME 50-cycle cadence, derived purely from (cycle, houseID): unit
 *  management on one cycle, base building half an interval later. Houses are
 *  spread evenly across the interval rather than packed together.
 *
 *  Everything here is a pure function of the cycle, the house id and the game
 *  type. No clock, no frame rate, no thread completion, no stored cursor and no
 *  serialised state: a peer or a reloaded save recomputes the same phase from
 *  the cycle counter alone. These are the predicates the engine runs, and the
 *  tests pin them directly.
 *
 *  This is an intentional decision-timing change for custom single-player: a
 *  house now evaluates the world at a different cycle than before, so its
 *  decisions and therefore the match trajectory can legitimately differ from
 *  1.0.793. Cadence, ordering within a phase and every gameplay choice are
 *  preserved; nothing is suppressed or capped. Multiplayer, coop, campaign and
 *  skirmish timing is bit-for-bit unchanged.
 */
#ifndef QUANTBOTSCHEDULEPOLICY_H
#define QUANTBOTSCHEDULEPOLICY_H

#include <DataTypes.h>

namespace QuantBotSchedulePolicy {

/// Only custom single-player is phased. A loaded single-player save restores its
/// real GameType. A multiplayer load may retain CustomGame in the saved world,
/// so the launch type must independently exclude network sessions.
constexpr bool phasedSchedule(GameType type, GameType launchType = GameType::Invalid) {
    return type == GameType::CustomGame && !isNetworkGameType(launchType);
}

/// Cycle offset within the interval on which this house manages its units.
/// Houses are spread evenly, so two neighbouring ids no longer land one cycle
/// apart: with 12 house slots and a 50-cycle interval the gap is 4 cycles, and
/// houses 0 and 4 — the two that dominate the measured profile — sit 16 apart.
constexpr int unitPhase(int houseID, int interval, int houseSlots) {
    if (interval <= 0 || houseSlots <= 0) return 0;
    const int slot = ((houseID % houseSlots) + houseSlots) % houseSlots;
    return (slot * interval) / houseSlots;
}

/// Base building runs half an interval after unit management, so the two never
/// fall on the same cycle for the same house (interval >= 2).
constexpr int buildPhase(int houseID, int interval, int houseSlots) {
    if (interval <= 0) return 0;
    return (unitPhase(houseID, interval, houseSlots) + interval / 2) % interval;
}

/// The legacy predicate, unchanged: every house's whole pass on one cycle.
constexpr bool legacyDue(uint32_t cycle, int houseID, int interval) {
    if (interval <= 0) return false;
    return ((cycle + static_cast<uint32_t>(houseID)) % static_cast<uint32_t>(interval)) == 0;
}

constexpr bool unitPhaseDue(uint32_t cycle, int houseID, int interval, int houseSlots) {
    if (interval <= 0) return false;
    return (cycle % static_cast<uint32_t>(interval))
        == static_cast<uint32_t>(unitPhase(houseID, interval, houseSlots));
}

constexpr bool buildPhaseDue(uint32_t cycle, int houseID, int interval, int houseSlots) {
    if (interval <= 0) return false;
    return (cycle % static_cast<uint32_t>(interval))
        == static_cast<uint32_t>(buildPhase(houseID, interval, houseSlots));
}

} // namespace QuantBotSchedulePolicy

#endif // QUANTBOTSCHEDULEPOLICY_H
