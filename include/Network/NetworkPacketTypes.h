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
#define NETWORK_PROTOCOL_VERSION            38

// Mod transfer limits
#define MAX_MOD_TRANSFER_SIZE   (10 * 1024 * 1024)  // 10MB max mod size
#define MOD_CHUNK_SIZE          (64 * 1024)          // 64KB per chunk

#endif // NETWORKPACKETTYPES_H
