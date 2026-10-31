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
// 68: QuantBot MCVs reach through diagonal gaps, settle new rock once a formation
//     holds two yards without waiting for rocket cover, travel with an escort,
//     and production MCVs stop at that share. MCV, escort and purchase orders
//     change lockstep decisions. Save and runtime layouts remain unchanged.
// 69: QuantBot answers an attack on its building with the nearest free troops
//     instead of forcing the whole army across the map, sends them with a
//     hunt-toward order (UnitBase::doHuntToward) that attacks enemies met on the
//     way, and never re-scrambles a scrambled unit. New engine targeting and
//     save 9854.
// 70: The special-unit and unit-mix score blend no longer overflows int64 with large
//     lifetime combat evidence (it scales both weights down instead of wrapping), so
//     QuantBot production choices differ from 69 once that evidence is large.
//     Save and runtime layouts remain unchanged.
// 71: Modern RTS controls (docs/rts-controls.md): new commands CMD_UNIT_ATTACKMOVE,
//     CMD_UNIT_PATROL, CMD_UNIT_QUEUEORDER and CMD_BUILDER_SETREPEAT, attack mode HOLD,
//     and classic unit orders now record the unit's order queue. Save 9855.
// 71: Construction Yard pre-placement queue. CMD_BUILDER_RESERVE_PLACEMENT and
//     CMD_BUILDER_CANCEL_RESERVATION are new commands, and a reserved order is placed by
//     the simulation when it finishes, so a 70 peer cannot execute or replay them.
//     Save 9856.
// 71: Formation movement. New CMD_UNIT_FORMATION_MOVE (unit, slot, group token); every
//     unit order ends formation membership; formation members move at the group's
//     speed cap; QuantBot, campaign AI and AIPlayer group moves use formations. Save 9857.
// 72: Plan building sites is a match rule (GameOptionsClass::planBuildingSites, settings
//     marker MOD8). It is in the game-options hash, and with it off the executor refuses
//     CMD_BUILDER_RESERVE_PLACEMENT, so a 71 peer would accept reservations this peer
//     drops. Save 9858.
// 73: Formation shapes and hunting in formation. New CMD_UNIT_FORMATION_HUNT (unit, slot,
//     facing, order kind and group token) gives a group attack-move or patrol formation pace
//     and breaks it into a fight on contact; CMD_UNIT_FORMATION_MOVE carries the facing in
//     its top bits; members wait rather than step into a sonic unit's lane; AI group moves
//     use the Combined-arms shape. Save 9859.
// 74: Formation roles and one-speed march. Harvesters never join a formation (the formation
//     executors give them the plain order), siege tanks stand in the front row, MCVs take
//     the centre slot, a group forms up before it marches, every member keeps the slowest
//     member's pace (infantry included), members ahead of one that fell behind wait for it,
//     and only a sonic unit of the same type may stand in a sonic unit's lane; QuantBot
//     colonists march in formation with their escort. Save 9860.
// 75: Form up where they stand. CMD_UNIT_FORMATION_MOVE accepts order kind FormUp (3): the
//     slot is also the member's form-up tile, so the group walks straight into its shape
//     without a march. A 74 peer refuses the kind and would leave the units where they are.
//     Save 9861.
// 76: QuantBot hunt parties (Blob waves). WaveFormation accepts Blob: a Custom Hard/Brutal
//     QuantBot wave gathers, marches as one blob, releases into Hunt on contact, reforms and
//     falls back a short way to reform; its party state is saved. A 75 peer would order the
//     same AI units differently and desynchronise. Save 9862.
// 77: QuantBot AI decisions change, no saved state or command change: the Custom Hard/Brutal
//     front battery stays in the 8-12 band, walls follow only the fixed-count defence
//     turrets, and a harvester override of 0 is "no ceiling" for every AI (QuantBot initial
//     limit, Mentat). A 76 peer would order different buildings and desync. Save 9862.
// 78: QuantBot force concentration defaults, no saved state or command change: Custom
//     Hard/Brutal hunters focus fire, wave members not yet fighting converge on a fight of
//     their army nearby, and waves launch at 60% of the former readiness ([Force
//     Concentration] in QuantBot Config, hashed). A 77 peer would order the same AI units
//     differently and desync. Save 9862.
// 79: QuantBot force concentration round 4: the Custom Hard/Brutal idle reserve bundles at a
//     forward rally and ready waves launch from the bundle ([Force Concentration] Bundle,
//     LaunchFromBundle, hashed; the bundle and the gate's wait are saved, 9863), and focus
//     fire and converging leave a hunting launcher's close-range escape alone. A 78 peer
//     would order the same AI units differently and desync. Save 9863.
// 80: QuantBot force concentration round 5, defence while outnumbered: a remembered enemy
//     army; while it outnumbers ours no Custom Hard/Brutal wave leaves, the reserve holds the
//     enemy-facing base perimeter and rocket turrets are added there; defences are sized at
//     1.5x the threat with the bundle first, distant defenders muster, and a worker raid the
//     troops in reach cannot match gets nobody ([Force Concentration] PerimeterHold,
//     PerimeterTurrets, SizedDefence, hashed; the remembered army and the musters are saved,
//     9864). Also the footprint rule: own units are traffic to the AI's site search, idle ones
//     are asked off planned sites, a finished building waits for them, and the hold applies in
//     the city simulation. A 79 peer would order the same AI units differently and desync.
//     Save 9864.
// 81: Formation march without jams, and QuantBot opening readiness; no saved state or command
//     change. A marching member that fell behind routes round a member keeping the pace rather
//     than queueing behind it, and a member waiting for a laggard gives way to a formation-mate
//     stepping onto its tile (it may be planned through). A Custom Hard/Brutal QuantBot whose
//     starting army cannot launch a wave (a Construction Yard start) holds its first ground
//     wave for full readiness ([Force Concentration] OpeningReadiness, hashed). An 80 peer
//     would move the same units differently and desync. Save 9864 unchanged.
// 82: QuantBot strategies (docs/ai-play-styles.md, "Strategies"): every Custom Hard/Brutal
//     QuantBot plays a unit-mix strategy and a tempo picked once from the game seed, its house and
//     [Strategy] Mode / MixWeights / TempoWeights (hashed); the mix is the prior its learned unit
//     mix starts from, the tempo its wave readiness. An 81 peer would build and attack
//     differently and desync. Save 9865 (the strategy).
// 83: QuantBot starting-mix bounds (docs/ai-play-styles.md, section 8): every starting unit mix
//     (the configured opening, the presets, Counter) gives each producible mix slot 2%-50% before
//     the learning, the presets are re-cut within those bounds, and the Tuned ([Unit Ratios Tuned],
//     hashed) and Bounded (random within the bounds from seed and house) mixes are new. An 82 peer
//     would build differently and desync. Save 9866 (the resolved Tuned/Bounded ratios). Unreleased,
//     so the same protocol also covers one mix slot per special (Devastator, Sonic Tank, Deviator;
//     the observer allocation snapshot has ten slots), the tuned ratios per tech band and save 9868.
//     (Branch feat/ai-ratio-tuning only; never released.)
// 84: QuantBot in a free-for-all: "outnumbered" (perimeter turrets and the test-only round 5
//     rules) measures the remembered army of the strongest single enemy team, not every enemy
//     summed, and the forward bundle and the perimeter point move out of the house's own
//     buildings and roads (a city base) towards the enemy. The perimeter hold (V, PerimeterHold)
//     is removed: an outnumbered house no longer holds its waves and reserve at home. No saved
//     state or command change; an 82 or 83 peer would order the same AI units differently and
//     desync. (Branches fix/ai-army-stuck-in-base, fix/ai-late-game-2 and fix/ai-difficulty-ladder
//     only; never released.)
// 85: No population/economic victory. WinFlags bit 0x10 (set by original Dune II scenarios such
//     as WinFlags=19/23) is ignored as upstream; a DuneCity campaign mission is no longer won when
//     total city population reaches 500. An 82 peer would end the game differently. No saved
//     state change (the retired threshold slot is still written as 0 and discarded on load).
//     (Branch fix/remove-economic-victory only; never released.)
// 86: 83 + 84 + 85 together on the 1.0.848 trial integration (integrate/848-trial only; never
//     released).
// 87: 1.0.848 = 83 + 84 + 85 + 86 and the difficulty ladder (fix/ai-difficulty-ladder): Custom
//     Medium QuantBots play the whole-army path (autonomousCustomArmy) and spend banked credits up
//     to +50% past their military cap (QuantBotBuildPolicy::surplusMilitaryBudget); a whole-army
//     house's held opening ends at its own threshold share of Medium's limit, and readiness that
//     credits plus map spice cannot reach attacks with what it has; "outnumbered", perimeter
//     turrets and the opening readiness judge the house's team against the strongest enemy team;
//     the campaign partner's harvester target counts competing houses, a Palace missile on a
//     harvester is not a contact, and after 15 minutes its readiness is measured against the
//     strongest enemy army; in DuneCity structures keep off reserved production plots, silos come
//     before heavy-factory tech and Medium's opening walks to the heavy factory; and a spice quota
//     (WinFlags 0x04) is met by the house's whole earned store, refined spice and city tax credits
//     alike, in every game mode; and a Medium/Hard/Brutal house whose team army leads the strongest
//     enemy team's sends its waves at the weakest known house (FinishWhenLeading, hashed). Any
//     82-86 peer would build, attack or end the game differently and
//     desync. Save 9868 (83's ten-slot mix state); 9867 and older saves load.
// 88: Difficulty separation (fix/difficulty-separation): only Custom Hard and Brutal QuantBots play
//     a production strategy (QuantBot::playsStrategy; 87 gave Custom Medium the default
//     Fixed:Tuned,Swarm mix and tempo too); a custom QuantBot's harvester fleet is capped by its
//     difficulty's configured HarvesterLimit*Map (QuantBot::configuredCustomHarvesterLimit; 87
//     overwrote it with the spice budget), and a custom Brutal house keeps its opening worker floor
//     and equal army capital per worker in vanilla games too (DuneCity only in 87). An 87 peer would
//     build and attack differently and desync. No saved state change; save 9868.
// 89: Spectated games end when decided (fix/spectator-game-end): runtime-only, nothing simulated
//     changes, so no bump was taken; the number stays unused.
// 90: City airplane (fix/city-airplane, unreleased on its own): the plane flies straight legs on a
//     continuous heading (CityAircraft::airplaneSteer replaces turnTo(getDir()), which zig-zagged)
//     and moves at the sixteen-heading speeds; a plane shot down crashes with a Death Hand impact
//     credited to its shooter (AmbientAirplane::crash -> Bullet::detonateDeathHand); RocketTurret,
//     Launcher, EliteLauncher, Trooper and Scoutpost hold fire while that blast would land on their
//     own team (AmbientAirplane::crashEndangers). No saved state change.
// 93: The Natives house (feat/natives-house, unreleased on its own): a new house slot,
//     HOUSE_NATIVES = 12 (NUM_HOUSES 13, team 0, no players), owns every sandworm and every city
//     crime trooper; worms no longer count for or are controlled by the house a map names, crime
//     troopers no longer join an enemy player's army, and the AI ignores Natives as a house.
//     Save 9871; older saves load and their worms convert.
// 94: Release 1.0.851 combines 90 and 93 (and the render-only unit-jitter fix, which changes no
//     simulation): a crashing plane's Death Hand also hits Natives worms and troopers, and anti-air
//     never treats Natives as its own team. An 88, 90 or 93 peer would desync. Save 9871.
// 100: Online spectating to the end (fix/spectator-online): JOIN_SYNC 18 sends the remaining
//     cycles and operation 19 marks/acknowledges the host's final cycle. Save 9871 unchanged.
// 101: Natives follow-up: team NATIVES_TEAM (10) is hostile to anonymous team-0 houses,
//     and reinforcement worms spare their summoner's team. Save 9873.
// 102: Combined follow-up batch includes the observer end-stream protocol and Natives team /
//     reinforcement-worm rules. Older peers cannot interpret the same match state. Save 9873.
// 103: Adds the continuous-ground-steps match rule to the combined follow-up batch:
//     GameOptionsClass::continuousGroundSteps is hashed and streamed in MOD9 (save 9874).
//     On: chained Dynasty ground steps clear justStoppedMoving, and a unit that missed
//     route ticks while awaiting a carryall resumes at its cadence instead of in the past.
//     Off (default): retains the old movement and render-only UnitBase::DrawnStepScope.
//     A 102 peer cannot read the MOD9 settings stream.
// 104: AI roles (docs/ai-roles.md): five qBotFighter* player classes appended to the
//      PlayerFactory list the lobby indexes, and QuantBot's role byte (save 9875). A 103 peer
//      cannot create a Fighter and would play it as another AI.
// 105: MOD10 planned construction: retry obstructions, clear allies, cancel enemies, and the
//      shared yard queue. An older peer cannot read the two added settings bytes (save 9876).
//      The Mentat AI is retired: its playerDataList entries are gone, so later AI indices moved.
// 106: MOD11 slow city year: the city year (budget, zone growth) lasts 120 s. An older peer
//      cannot read the added settings byte (save 9877).
// 107: city matches give the Construction Yard 1000 credits of storage at match start;
//      a 106 peer would start the same mod with none and desync.
#define NETWORK_PROTOCOL_VERSION            107

// Mod transfer limits
#define MAX_MOD_TRANSFER_SIZE   (10 * 1024 * 1024)  // 10MB max mod size
#define MOD_CHUNK_SIZE          (64 * 1024)          // 64KB per chunk

#endif // NETWORKPACKETTYPES_H
