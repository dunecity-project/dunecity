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

#ifndef NETWORKPACKETTYPES_H
#define NETWORKPACKETTYPES_H

/**
    Wire constants of the ENet protocol. Kept separate from NetworkManager.h so the packet
    admission policy can be included by code (and tests) that must not pull in the manager.
    These values are part of the legacy wire format - do not renumber them.
*/

#define NETWORKDISCONNECT_QUIT              1
#define NETWORKDISCONNECT_TIMEOUT           2
#define NETWORKDISCONNECT_PLAYER_EXISTS     3
#define NETWORKDISCONNECT_GAME_FULL         4
#define NETWORKDISCONNECT_PROTOCOL_MISMATCH 5

#define NETWORKPACKET_UNKNOWN               0
#define NETWORKPACKET_CONNECT               1
#define NETWORKPACKET_DISCONNECT            2
#define NETWORKPACKET_PEER_CONNECTED        3
#define NETWORKPACKET_SENDGAMEINFO          4
#define NETWORKPACKET_SENDNAME              5
#define NETWORKPACKET_CHATMESSAGE           6
#define NETWORKPACKET_CHANGEEVENTLIST       7
#define NETWORKPACKET_STARTGAME             8
#define NETWORKPACKET_COMMANDLIST           9
#define NETWORKPACKET_SELECTIONLIST         10
#define NETWORKPACKET_CONFIG_HASH           11
#define NETWORKPACKET_SETPATHBUDGET         12  // Phase 1.4: Budget negotiation
#define NETWORKPACKET_CLIENTSTATS           13  // Multiplayer: Client performance stats
#define NETWORKPACKET_MOD_INFO              14  // Host -> Client: mod name + checksums
#define NETWORKPACKET_MOD_REQUEST           15  // Client -> Host: request mod files
#define NETWORKPACKET_MOD_CHUNK             16  // Host -> Client: mod file chunk
#define NETWORKPACKET_MOD_COMPLETE          17  // Host -> Client: transfer complete
#define NETWORKPACKET_MOD_ACK               18  // Client -> Host: acknowledge mod sync complete
#define NETWORKPACKET_KEEPALIVE             19  // Periodic ping to keep NAT mappings alive

#define NETWORKPACKET_COOP_MISSION          20
#define NETWORKPACKET_JOIN_SYNC             21
#define NETWORKPACKET_JOIN_ACK              22

#define NETWORKPACKET_MATCH_CONTROL         23  // Host -> Clients: authoritative speed/pause state
#define NETWORKPACKET_MATCH_RESUME_REQUEST  24  // Client -> Host: ask to leave a pause

// Network protocol version - increment when packet formats change
// Version 2: Added simMsAvg to NETWORKPACKET_CLIENTSTATS (5 fields instead of 4)
// Version 3: Added mod transfer packets (MOD_INFO, MOD_REQUEST, MOD_CHUNK, MOD_COMPLETE)
// Version 4: Fixed nine-house deterministic state and versioned visibility storage
// Version 11: Added MATCH_CONTROL and MATCH_RESUME_REQUEST. A paused simulation sends no
//             command lists, so the resume request cannot travel in the command stream and
//             needs its own packet; the authoritative reply carries the shared match settings
//             (wall-clock tick pacing and the pause/resume revision) a peer on version 10
//             would silently ignore.
// Version 12: MOD5 game setup carries construction-yard limits. Older peers
//             cannot enforce the rule and must not join the same simulation.
// Version 13: QuantBot population admission and growth ceilings change lockstep decisions.
// Version 14: Carryall approach, docking and turning change lockstep movement.
// Version 17: Dynasty projectile motion, detonation and splash rules.
// Version 18: Combined shared-helper population admission and projectile build.
// Version 19: All-difficulty emergency Starport MCV recovery changes AI orders.
// Version 20: Proactive crime/rocket coverage, opening economy and air defence AI.
// Version 21: Keep opening city growth alongside routine defence and supplier investment.
// Version 22: Sustainable policing budgets and core-first spice expansion.
// Version 23: Earned credits share refinery/silo capacity and are capped immediately.
// Version 24: Three-turret expansion sequencing, funding smoothing and source-aware refunds.
// Version 25: Map deduplication and mature AI storage prioritization.
// Version 26: AI progression, aircraft rescue and terrain-reachable Hunt targets.
// Version 27: Combined AI changes and Dynasty turret/launcher cadence.
// Version 28: Starport prerequisites change AI tech progression decisions.
// Version 29: Dynasty-aligned foundation and power degradation change simulation health.
// Version 30: Health-scaled windtraps, city land condition and AI concrete policy.
// Version 32: QuantBot anticipates queued demand and maintains a Vanilla power buffer.
// Version 33: QuantBot local MCV deployment decisions change.
// Version 34: City growth and air interception decisions, stadium requirement.
// Version 35: Space-driven QuantBot colonisation decisions.
// Version 36: Cramped city placement and Starport colonisation decisions.
// Version 37: Opening space, prompt MCV colonies and launcher air defence change AI orders.
// Version 38: Gas deviation targets aircraft at one shared chance for every house
//             and mode, so target selection and synchronized deviation draws differ.
// Version 39: Deviated units cannot request or enter repairs, changing AI orders.
// Version 40: Measured Deviator contribution. Captured Devastators are armed at the first
//             eligible AI scan and the deviation ledger is deterministic simulation state,
//             so peers on version 39 would diverge in both orders and saved state.
// 41: Shared ordinary damage/splash policy and the MOD6 original-damage rule.
// Version 42: Original Dune II ordinary ground damage option is active.
// Version 43: Pathfinding is sliced against a strict per-cycle node budget. The
//             nodes a cycle spends, the order routes are delivered in and the
//             suspended-search scheduler are all simulation state, so a peer on
//             version 42 would hand paths back on different cycles and diverge.
//             The observer runtime stream is version 6 for the same reason.
// Version 44: Fractional repairs finish at maximum HP. Explicit unit overrides
//             also change Brutal military production and count admission. Older
//             peers would compute different repair releases and AI orders.
// Version 45: Police patrol admission uses the effective game unit-category limits
//             instead of a separate 250-unit ceiling. Older peers can spawn different
//             patrols from the same command or AI tick.
// Version 47: A ground unit that has been on the same tile for thirty seconds of simulation
//             time while still wanting to move asks for a carryall, retrying on a throttle
//             rather than once per completed path search, and Stop now releases an outstanding
//             pickup booking. Which units are lifted, and on which cycle, is simulation state:
//             a peer on version 46 would keep driving a unit this build flies, and would hold a
//             booking this build cancels. The observer runtime stream is version 7 for the same
//             reason.
// 48: per-house spice income multipliers. The lobby gained a change-event type and
//     GameInitSettings gained the MOD7 block, so a 47 peer cannot decode either; the factor
//     also feeds the state digest, so an older peer would compute a different house hash.
// 49: carryall pickup keeps the actual passenger and releases replacement bookings;
//     deterministic containment recovery restores orphaned passengers from older matches.
//     Older peers would hide different units and disagree about house survival.
// 50: QuantBot army posture. A Custom Hard/Brutal house withdraws, assembles at
//     a protected rally and gates its offensive dispatch on an outnumbered front
//     and a share of its configured military value. Which units move, when a
//     wave leaves and where emplacements are built are all simulation state, so
//     a peer on 49 would issue different AI orders from the same cycle and the
//     two would diverge. The save layout is 9852 for the same reason.
// 51: Custom Hard/Brutal city batteries may replace eligible enemy-facing R/I/C
//     lots. Peers on 50 would preserve those lots and issue different orders.
// 54: Hunt launchers retain close-range spacing; rebuilt armies leave recovery
//     by strength. Older peers would issue different movement and attack orders.
// 55: Custom Hard/Brutal base attacks mobilise the whole ground army on Hunt,
//     retaining live invasion orders across repeated contacts. Older peers
//     send different troops and use different movement and attack orders.
// 56: Allow 2/3ms shared game speeds in GameInitSettings and MATCH_CONTROL.
//     Older peers reject these values; save and observer layouts are unchanged.
// 57: Wrap ground combat rotation after movement; older peers can generate an invalid
//     track direction and diverge or corrupt tile memory. Save/runtime layouts are unchanged.
// 58: QuantBot establishes city tax income before optional spice technology on
//     depleted maps. Older peers issue different construction and import orders
//     from the same state. Save and runtime layouts remain unchanged.
// 59: Needed civic buildings and nuclear power enter gradual-payment production
//     before their full price is saved. Older peers choose different construction
//     and spending orders. Save and runtime layouts remain unchanged.
// 60: Required city civics and their missing prerequisites outrank optional
//     spending, including between positive demand-clipping phases. Older peers
//     choose different construction orders. Save and runtime layouts are unchanged.
// 61: Micropolis city aircraft. The Airport launches them with the original
//     doAirport() odds from the shared simulation RNG, and both aircraft steer,
//     report traffic and retire on the original sprite clock. A peer on 60 draws
//     a different number of RNG values on the same cycle, flies the placeholder
//     orbit/flyover instead, and keeps up to three aircraft per house where this
//     build keeps one of each — so the two diverge immediately. The save layout
//     is 9853 for the same reason.
// 62: City airplanes retain off-map waypoints and fly out when their cruise
//     budget expires, instead of disappearing over the city. Changed routes,
//     retirement times and subsequent Airport RNG draws require matching peers.
//     The aircraft save layout remains 9853.
// 66: Victim-type performance changes kill completion rewards and subsequent AI
//     allocations. Reactive city Trooper/WOR production and rocket infantry Hunt
//     targeting also change lockstep decisions. Save and runtime layouts remain unchanged.
#define NETWORK_PROTOCOL_VERSION            67

// Mod transfer limits
#define MAX_MOD_TRANSFER_SIZE   (10 * 1024 * 1024)  // 10MB max mod size
#define MOD_CHUNK_SIZE          (64 * 1024)          // 64KB per chunk

#endif // NETWORKPACKETTYPES_H
