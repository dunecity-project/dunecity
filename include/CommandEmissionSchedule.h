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

#ifndef COMMANDEMISSIONSCHEDULE_H
#define COMMANDEMISSIONSCHEDULE_H

#include <Definitions.h>

#include <SDL.h>

/**
    Paces relay command-history packets independently of rendering and simulation speed.
    Polling carries at most 64 frames per response. At a 1.1-second exchange interval,
    its drain rate is below the normal 62.5 simulation steps per second.

    Each emission retains the existing history window. Besides the 100 ms cadence,
    emit when that window reaches the previous emission's exclusive upper bound.
    CommandManager checks once per cycle, so successive windows remain contiguous
    even when simulation catch-up advances faster than wall time.
*/
class CommandEmissionSchedule {
public:
    /// Wall-clock cadence: ten emissions a second, whatever the render or simulation rate is.
    static constexpr Uint32 kIntervalMs = 100;

    /// How much history one emission repeats. This is the window CommandManager has always sent.
    static constexpr Uint32 kHistoryMs = 2500;

    /// The same window expressed in cycles, which is how the retention rule reasons about it.
    static constexpr Uint32 kHistoryCycles = MILLI2CYCLES(kHistoryMs);

    /**
        First cycle an emission at this cycle would carry, saturating at 0.
        \param  currentCycle    the cycle the emission is made at
        \return the inclusive lower bound of the emitted window
    */
    static Uint32 historyStartCycle(Uint32 currentCycle) {
        return (currentCycle > kHistoryCycles) ? (currentCycle - kHistoryCycles) : 0;
    }

    /**
        \param  nowMs           SDL_GetTicks() at the call, wrapping is handled
        \param  currentCycle    the cycle the emission would be made at
        \return true if the command history has to go out now
    */
    bool shouldEmit(Uint32 nowMs, Uint32 currentCycle) const {
        if(!everEmitted) {
            return true;
        }

        // Retention. The caller checks this once per cycle and the window start advances by at
        // most one cycle per cycle, so the first time it reaches the frontier it is exactly on
        // it: the next window still begins where the last one ended, and nothing is skipped.
        if(historyStartCycle(currentCycle) >= sentThroughCycle) {
            return true;
        }

        // Cadence. Unsigned subtraction, so the 49.7 day wrap of SDL_GetTicks() is a normal
        // interval and not a 49 day pause.
        return (nowMs - lastEmissionMs) >= kIntervalMs;
    }

    /**
        Records an emission that has just been handed to the network manager.
        \param  nowMs           the same clock reading shouldEmit() was asked with
        \param  windowEndCycle  the exclusive upper bound of the window that was sent
    */
    void noteEmission(Uint32 nowMs, Uint32 windowEndCycle) {
        everEmitted     = true;
        lastEmissionMs  = nowMs;
        sentThroughCycle = windowEndCycle;
    }

    /**
        Forgets everything about the previous session. A frontier left over from a finished match
        would suppress the retention rule in the next one, and its emission time would be read
        against a clock that has since moved on.
    */
    void reset() { *this = CommandEmissionSchedule(); }

private:
    bool   everEmitted      = false;    ///< false until the first emission of this session
    Uint32 lastEmissionMs   = 0;        ///< SDL_GetTicks() of the last emission
    Uint32 sentThroughCycle = 0;        ///< exclusive upper bound of the last emitted window
};

/**
    Suppresses only byte-identical repeats of a direct peer's rolling command window.

    A direct peer emits as part of the cycle loop, but that loop also spins while the
    simulation is held: Game's inner loop calls CommandManager::update() every iteration
    and only advances the cycle when it is neither waiting for a peer's commands nor
    paused. While it is waiting, the same window is therefore offered again and again as
    fast as the loop turns, which is how an ordered data channel reaches its buffered and
    retained-job bounds and starts refusing sends.

    What makes a repeat safe to drop is that the emitted payload is a pure function of the
    window bounds, the local player id and that player's commands inside the window. So an
    emission is only suppressed while all three of the window start, the window end and a
    local-command revision counter are unchanged. Any cycle movement in either direction,
    and any newly accepted local command, emits immediately: the lead, the window and the
    payload are exactly what they were before this class existed.

    Suppression is never indefinite. After kIdleRetryMs the unchanged window goes out
    again, so a peer that lost a packet still receives the retransmission that the rolling
    history exists to provide, and neither side can wedge waiting for the other.
*/
class DirectEmissionSchedule {
public:
    /// Liveness floor: an unchanged window is still retransmitted this often.
    static constexpr Uint32 kIdleRetryMs = 100;

    /**
        \param  nowMs           SDL_GetTicks() at the call, wrapping is handled
        \param  windowStart     inclusive lower bound of the window about to be emitted
        \param  windowEnd       exclusive upper bound of the window about to be emitted
        \param  localRevision   counts commands accepted for the local player
        \return true if this window has to go out now
    */
    bool shouldEmit(Uint32 nowMs, Uint32 windowStart, Uint32 windowEnd,
                    Uint32 localRevision) const {
        if(!everEmitted) {
            return true;
        }

        // Cycle movement, forwards or backwards. Early in a match the start saturates at 0
        // while the end still advances, so both bounds are compared rather than just one.
        if(windowStart != lastWindowStart || windowEnd != lastWindowEnd) {
            return true;
        }

        // A command of our own accepted at a cycle already inside this window changes the
        // payload without moving either bound. Comparing for inequality rather than order
        // keeps this correct across the counter's own wrap.
        if(localRevision != lastLocalRevision) {
            return true;
        }

        // Unsigned subtraction, so the 49.7 day wrap of SDL_GetTicks() is a normal interval
        // and not a 49 day silence.
        return (nowMs - lastEmissionMs) >= kIdleRetryMs;
    }

    /**
        Records a window that has just been handed to the network manager.
        \param  nowMs           the same clock reading shouldEmit() was asked with
        \param  windowStart     inclusive lower bound of the window that was sent
        \param  windowEnd       exclusive upper bound of the window that was sent
        \param  localRevision   the revision the sent window reflects
    */
    void noteEmission(Uint32 nowMs, Uint32 windowStart, Uint32 windowEnd,
                      Uint32 localRevision) {
        everEmitted       = true;
        lastEmissionMs    = nowMs;
        lastWindowStart   = windowStart;
        lastWindowEnd     = windowEnd;
        lastLocalRevision = localRevision;
    }

    /**
        Forgets the previous session or command history. A replay, a savegame, a checkpoint
        rebuild or a truncation rewrites the commands inside the window without moving its
        bounds, so inheriting this state could suppress a window whose contents changed.
    */
    void reset() { *this = DirectEmissionSchedule(); }

private:
    bool   everEmitted       = false;   ///< false until the first emission of this session
    Uint32 lastEmissionMs    = 0;       ///< SDL_GetTicks() of the last emission
    Uint32 lastWindowStart   = 0;       ///< inclusive lower bound of the last emitted window
    Uint32 lastWindowEnd     = 0;       ///< exclusive upper bound of the last emitted window
    Uint32 lastLocalRevision = 0;       ///< local-command revision the last window reflected
};

#endif // COMMANDEMISSIONSCHEDULE_H
