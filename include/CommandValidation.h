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

#ifndef COMMANDVALIDATION_H
#define COMMANDVALIDATION_H

#include <Command.h>

#include <cstddef>
#include <limits>

/**
    Validation of commands that arrived over the network, plus the cycle-window arithmetic used
    when they are scheduled. These are pure functions so the production path (CommandManager)
    and the regression tests exercise exactly the same rules.

    Everything here is a *network* policy. Replays and savegames keep their existing parsing
    behaviour; they are loaded through CommandManager::load(), which does not use these checks.
*/
namespace CommandValidation {

/// Largest parameter count any command uses. Command::executeCommand() never reads past 4.
constexpr std::size_t kMaxCommandParameters = 4;

/**
    Expected parameter count for a command, mirroring the checks in Command::executeCommand().
    Commands with a variable count report their maximum through maxParameterCount() instead.
    \param  commandID   the command to look up
    \return the exact number of parameters, or -1 when the command is not fixed-arity
*/
inline int exactParameterCount(CMDTYPE commandID) {
    switch(commandID) {
        case CMD_PLACE_STRUCTURE:               return 3;
        case CMD_UNIT_MOVE2POS:                 return 4;
        case CMD_UNIT_MOVE2OBJECT:              return 2;
        case CMD_UNIT_ATTACKPOS:                return 4;
        case CMD_UNIT_ATTACKOBJECT:             return 2;
        case CMD_UNIT_HEAL:                     return 2;
        case CMD_INFANTRY_CAPTURE:              return 2;
        case CMD_UNIT_REQUESTCARRYALLDROP:      return 3;
        case CMD_UNIT_SENDTOREPAIR:             return 1;
        case CMD_UNIT_SETMODE:                  return 2;
        case CMD_DEVASTATOR_STARTDEVASTATE:     return 1;
        case CMD_MCV_DEPLOY:                    return 1;
        case CMD_HARVESTER_RETURN:              return 1;
        case CMD_STRUCTURE_SETDEPLOYPOSITION:   return 3;
        case CMD_STRUCTURE_REPAIR:              return 1;
        case CMD_BUILDER_UPGRADE:               return 1;
        case CMD_BUILDER_PRODUCEITEM:           return 3;
        case CMD_BUILDER_CANCELITEM:            return 3;
        case CMD_BUILDER_SETONHOLD:             return 2;
        case CMD_PALACE_SPECIALWEAPON:          return 1;
        case CMD_PALACE_DEATHHAND:              return 3;
        case CMD_STARPORT_PLACEORDER:           return 1;
        case CMD_STARPORT_CANCELORDER:          return 1;
        case CMD_TURRET_ATTACKOBJECT:           return 2;
        case CMD_PLAYER_PAUSE:                  return 0;
        case CMD_PLAYER_RESUME:                 return 0;
        case CMD_TEST_SYNC:                     return 1;
        case CMD_TECHCENTER_SPAWN:              return 1;
        case CMD_SCOUTPOST_UPGRADE:             return 1;
        case CMD_SCOUTPOST_CHEMIPOST_UPGRADE:   return 1;
        case CMD_HOUSE_AUTO_REPAIR:             return 1;
        case CMD_POLICE_REINFORCEMENTS:         return 1;
        case CMD_ZONE_DEMOLISH:                 return 1;
        case CMD_STRUCTURE_DEMOLISH:            return 1;
        case CMD_CAMPAIGN_SKIP:                 return 0;
        case CMD_MATCH_PAUSE:                   return 1;
        case CMD_BUILDER_RESERVE_PLACEMENT:     return 4;
        case CMD_BUILDER_CANCEL_RESERVATION:    return 2;

        // The city commands read up to three optional parameters
        // (CitySimulation::executeCityCommand substitutes 0 for missing ones).
        case CMD_CITY_PLACE_ZONE:
        case CMD_CITY_SET_TAX_RATE:
        case CMD_CITY_SET_BUDGET:
        case CMD_CITY_TOOL:                     return -1;

        // CMD_NONE has no handler at all: Command::executeCommand() throws on it.
        case CMD_NONE:
        default:                                return -2;
    }
}

/**
    \param  commandID   the command id as it came off the wire
    \return true if commandID names a command that may be executed
*/
inline bool isKnownCommandID(Uint32 commandID) {
    return commandID > static_cast<Uint32>(CMD_NONE)
        && commandID < static_cast<Uint32>(CMD_MAX);
}

/**
    Checks a deserialized command against the command table: known id and exactly the
    parameter count Command::executeCommand() requires. An unknown id or a wrong count makes
    executeCommand() throw, and that throw happens inside the simulation loop where it is
    rethrown - so the command has to be dropped before it is ever queued.
    \param  commandID       the command id as it came off the wire
    \param  parameterCount  the number of parameters that were deserialized
    \return true if the command is safe to schedule
*/
inline bool isWellFormedCommand(Uint32 commandID, std::size_t parameterCount) {
    if(!isKnownCommandID(commandID)) {
        return false;
    }
    if(parameterCount > kMaxCommandParameters) {
        return false;
    }

    const int expected = exactParameterCount(static_cast<CMDTYPE>(commandID));
    if(expected == -2) {
        return false;
    }
    if(expected == -1) {
        return parameterCount <= 3;
    }
    return parameterCount == static_cast<std::size_t>(expected);
}

/// Hard caps for a received COMMANDLIST packet. One legitimate packet covers
/// [gameCycle - MILLI2CYCLES(2500), gameCycle + networkCycleBuffer): at the default game speed
/// that is ~156 history cycles plus the buffer, and each entry holds the commands one player
/// issued in a single cycle. A player pressing a control group or box-selecting hundreds of
/// units still issues one command per unit in a cycle, so the per-cycle bound is generous.
constexpr Uint32 kMaxCommandListEntries = 512;
constexpr Uint32 kMaxCommandsPerEntry = 512;
/// Aggregate bound for one packet. The product of the two bounds above would allow a quarter
/// of a million commands in a nominally valid packet; a real one carries a handful.
constexpr Uint32 kMaxCommandsPerPacket = 4096;

/**
    \param  entryCount  number of cycle entries the packet claims to contain
    \return true if a COMMANDLIST with this many entries may be parsed
*/
inline bool isAcceptableCommandListEntryCount(Uint32 entryCount) {
    return entryCount <= kMaxCommandListEntries;
}

/**
    \param  commandCount    number of commands one cycle entry claims to contain
    \return true if a cycle entry with this many commands may be parsed
*/
inline bool isAcceptableCommandCountPerEntry(Uint32 commandCount) {
    return commandCount <= kMaxCommandsPerEntry;
}

/**
    \param  totalCommands   commands decoded from one packet so far
    \return true if the packet may hold this many commands in total
*/
inline bool isAcceptableCommandTotal(std::size_t totalCommands) {
    return totalCommands <= static_cast<std::size_t>(kMaxCommandsPerPacket);
}

/// Bounds for replay and savegame command streams. These are local files rather than network
/// input, so they are deliberately generous: at the default game speed 4 million cycles is
/// about eighteen hours of play. What they stop is a crafted file making CommandManager resize
/// its per-cycle vector to four billion entries or accumulate commands without end.
constexpr Uint32 kMaxReplayCycle = 4u * 1000u * 1000u;
constexpr std::size_t kMaxReplayCommands = 4u * 1000u * 1000u;

/// Extra cycles of slack accepted beyond the receiver's own command buffer, covering the
/// sender running ahead by up to one buffer plus lockstep jitter.
constexpr Uint32 kCycleWindowSlack = 64;

/**
    Highest cycle number a peer may schedule a command for, saturating at UINT32_MAX so the
    window arithmetic itself can never wrap.
    \param  currentCycle        the receiver's current game cycle
    \param  networkCycleBuffer  the local command buffer in cycles
    \return the last acceptable cycle
*/
inline Uint32 maxAcceptableCommandCycle(Uint32 currentCycle, Uint32 networkCycleBuffer) {
    const Uint32 headroom = (networkCycleBuffer > (std::numeric_limits<Uint32>::max() / 2))
        ? (std::numeric_limits<Uint32>::max() / 2)
        : (networkCycleBuffer * 2);

    if(currentCycle > std::numeric_limits<Uint32>::max() - headroom) {
        return std::numeric_limits<Uint32>::max();
    }
    Uint32 limit = currentCycle + headroom;
    if(limit > std::numeric_limits<Uint32>::max() - kCycleWindowSlack) {
        return std::numeric_limits<Uint32>::max();
    }
    return limit + kCycleWindowSlack;
}

/**
    A received command list entry may reference cycles that already passed (retransmissions of
    the rolling 2.5 s history) but not cycles far in the future - CommandManager::addCommand()
    resizes its timeslot vector to the cycle number, so an unbounded cycle is an unbounded
    allocation.
    \param  cycle               the cycle the sender asks for
    \param  currentCycle        the receiver's current game cycle
    \param  networkCycleBuffer  the local command buffer in cycles
    \return true if the cycle is inside the acceptable window
*/
inline bool isAcceptableCommandCycle(Uint32 cycle, Uint32 currentCycle, Uint32 networkCycleBuffer) {
    return cycle <= maxAcceptableCommandCycle(currentCycle, networkCycleBuffer);
}

/**
    Saturating successor used for HumanPlayer::nextExpectedCommandsCycle. Wrapping at
    UINT32_MAX would reset the "already processed" watermark to 0 and let an attacker replay
    the whole history.
    \param  cycle   the cycle that was just processed
    \return cycle + 1, saturated
*/
inline Uint32 nextCycleAfter(Uint32 cycle) {
    return (cycle == std::numeric_limits<Uint32>::max()) ? cycle : (cycle + 1);
}

} // namespace CommandValidation

#endif // COMMANDVALIDATION_H
