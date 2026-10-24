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

#ifndef DEFINITIONS_H
#define DEFINITIONS_H

#define SCREEN_BPP                  32
#define SCREEN_FORMAT               SDL_PIXELFORMAT_ABGR8888
#define SCREEN_MIN_WIDTH            640
#define SCREEN_MIN_HEIGHT           480
#define SCREEN_DEFAULT_DISPLAYINDEX 0

#define AUDIO_FREQUENCY     44100

#define DEFAULT_PORT        28747
#define DEFAULT_METASERVER  "https://dunelegacy.com/metaserver/metaserver.php"

/**
    Base URL of the room relay that carries crossplay games (see docs/room-relay-protocol.md).

    Empty by default on purpose: nothing in this repository deploys a relay, and the game must
    not invent an endpoint. An operator sets "Relay Endpoint" in the Network section of the
    configuration file, or passes --RelayEndpoint= on the command line.
*/
#define DEFAULT_RELAY_ENDPOINT  "https://dunelegacy.com/relay"

/// The loopback relay used for development and testing. Only ever plain http/ws, only loopback.
#define DEVELOPMENT_RELAY_ENDPOINT "http://127.0.0.1:8787"

/**
    Base URL of the direct-play signaling service (see docs/direct-play.md).

    This service introduces players to each other and does nothing else: it never carries a
    gameplay byte, and a match keeps running if it goes away. It is deliberately a different path
    from the relay endpoint above, so that an admission answer cannot move a direct match back
    onto somebody's server - the client refuses a "/relay" address on this setting outright.
*/
#define DEFAULT_DIRECT_ENDPOINT "https://dunelegacy.com/p2p"

/// The loopback signaling service used for development. Plain http, loopback only, opt-in only.
#define DEVELOPMENT_DIRECT_ENDPOINT "http://127.0.0.1:8788"

#define SAVEMAGIC           8675309
// 9822: Worfinery persists its progressive harvester extraction state.
// 9820: CitySimulation persists every house's R/C/I and budget state.
// 9817 added House::cityCredits; 9818 introduced the all-house city layout.
// 9824: House auto-repair setting and PoliceStation reinforcement cooldown.
// 9825: City district unrest progress.
// 9826: House combat reward and learning loss/damage counters.
// 9827: QuantBot harvesting anchor selection cycle.
// 9828: QuantBot crime-service construction allocation progress.
// 9829: QuantBot deterministic power-demand forecast samples.
// 9830: QuantBot coordinated waves, player orders, escorts and placement loss memory.
// 9833: City district gang buildup persists dangerous-building exposure.
// 9834: Former crime exposure slot stores mature-outbreak gathering time.
// 9835: Residential zones persist individual houses and apartment population.
// 9836: Airports persist reinforcement cooldown and partially deployed pairs.
// 9837: Network campaign/mission game types and shared-house co-op saves.
// 9838: QuantBot campaign assault membership, opening and recovery timers.
// 9839: Preserve scripted campaign attackers separately from automatic waves.
// 9840: Exact Workshop map and mod revisions.
// 9841: Cumulative gross city tax receipts per house (debriefing statistic).
// 9842: QuantBot original campaign economy, building permissions and depletion state.
// 9843: MOD5 setup extension stores the construction-yard limit.
// 9844: Ground route cadence and interpolated tile-step continuation.
// 9845: Dynasty projectile clock, steering and arming state.
// 9846: Refundable production payments preserve starting-credit provenance.
// 9847: Structures persist power-damage and foundation-decay state separately
//       (Dynasty-aligned degradation); older saves restore without foundation
//       decay because their single timer cannot identify the foundation.
// 9848: QuantBot opening dispersal sites and the cycle at which ordinary
//       regrouping resumes. They decide whether a starting unit is left alone
//       or marched back onto the home rock, so a save or network checkpoint
//       taken during the opening has to carry them.
// 9849: Measured Deviator contribution. Units carry their deviation control interval,
//       projectiles and Devastator fuses carry the beneficiary decided when they were
//       fired or armed, houses carry the measured outgoing/absorbed/terminal counters,
//       and the game carries the episode-id counter. Saves written before this version
//       have the old flat conversion estimate cleared on load
//       (DeviationReward::migrateLegacyLedger) so the two accountings cannot mix.
// 9850: MOD6 game rules persist the original unit damage checkbox.
// 9851: per-house spice income multiplier. Three places carry it, because no single one of
//       them can: MOD7 in GameInitSettings carries the *chosen* lobby rows (which may still
//       say Random), the SMUL block after the setup colours carries the *resolved* rows of a
//       saved match, and House carries the live factor the simulation actually pays out with.
// 9852: QuantBot army posture. A Custom Hard/Brutal house carries its posture
//       (offensive, withdrawing, recovering) and the cycle it entered it, the
//       cumulative mobile-combat attrition ledger with its baseline ring, the
//       tracked Custom wave and its shared objective, the protected assembly
//       point and the fair recall cursor. None of it can be recomputed after a
//       load: the ledger is cumulative, and the posture decides whether a unit
//       is marching home or marching out, so a save or network checkpoint taken
//       mid-withdrawal has to carry it.
// 9853: Micropolis city aircraft. The city helicopter and airplane replaced
//       their placeholder orbit/flyover state with the original sprite state:
//       the helicopter carries its home pad, patrol count, report cooldown,
//       scan cooldown, report tally and return-home flag; the airplane carries
//       its take-off frame and remaining flight budget. None of it can be
//       recomputed after a load — a report cooldown and a patrol count decide
//       whether the next congested tile produces a message or silence, and a
//       take-off frame decides which artwork is on screen. A 9852 save still
//       loads: the old fields are read and the aircraft resumes with a fresh
//       patrol instead of an orbit it no longer flies.
// 9854: UnitBase hunt-toward goal and retry count. A unit sent to an attacked
//       building with doHuntToward() walks there in Hunt and attacks only enemies
//       it meets on the way; a save taken mid-walk has to keep that rule. A 9853
//       save loads with no unit hunting toward anything.
// 9855: Modern RTS controls (docs/rts-controls.md). Units carry their player order queue
//       (Shift-queued orders, attack-move and patrol: up to 16 orders, whether the front
//       one was started, the walk retry count and the attack-move break-off point), and
//       builders carry the items set to repeat production. A 9854 save loads with empty
//       queues and no repeating items. Attack mode HOLD (10) is new in the same version.
// 9856: Construction Yard pre-placement queue. Every production queue entry
//       carries its reservation (id, plot, fallback flag) and every builder its
//       next reservation id and the cycles the front reservation has waited for
//       units to clear the plot. The plot decides where a finished building
//       appears, so it cannot be recomputed after a load. A 9854 save loads with
//       no reservations: every queued order keeps today's place-it-yourself flow.
// 9857: UnitBase formation membership: group id, formation slot and the group's speed
//       cap (docs/formations.md). A save taken mid-march keeps the group together at
//       its pace. A 9854 save loads with no unit in a formation.
// 9858: GameInitSettings MOD8: the plan-building-sites match rule, appended after the
//       MOD7 spice income block (docs/preplace-queue.md). A 9857 build does not know
//       MOD8 and would load such a save as vanilla, so it must refuse it. A 9857 save
//       loads with the rule on, the only behaviour 9857 had by default.
// 9859: Formation shapes and hunting in formation (docs/formations.md): UnitBase records
//       whether its formation is a hunting one, which the first contact of any member
//       breaks into a fight, and the formation's facing, along which the march keeps sonic
//       lanes clear. A 9858 or older save loads every formation as a plain march without a facing.
// 9860: One-speed formation march (docs/formations.md): UnitBase records its form-up tile
//       (where the member gathers before its group marches; invalid once marching), how its
//       order towards the slot is given again when the group has formed up, and the pace
//       unit's type (the slowest member, whose step time every capped member keeps). A 9859
//       or older save loads every formation as already marching at its speed cap.
// 9861: Form up where they stand (docs/formations.md): a save or replay may carry
//       CMD_UNIT_FORMATION_MOVE commands of order kind FormUp, which a 9860 build refuses,
//       so it must not load them. The layout is unchanged: a 9860 save loads as it is.
// 9862: QuantBot hunt parties (Blob waves, docs/formations.md): each party's phase, objective,
//       rally/fall-back point, contact and gather clocks, fall-back count, members and the
//       members waiting for the rear, appended to the QuantBot block. A 9861 save loads with no
//       party: its waves carry on as plain engine hunters.
// 9863: QuantBot force concentration (docs/formations.md, "Round 4"): the reserve bundle point,
//       the army-centre gathering point, the bundle's 30 s re-choice clock and the launch
//       gate's first-hold cycle, appended to the QuantBot block. A 9862 save loads with no
//       bundle: it is chosen afresh at the first unit pass.
// 9864: QuantBot defence while outnumbered (docs/formations.md, "Round 5"): the remembered
//       enemy army (each sighted fighter's id, value and last-seen cycle) and the defence
//       musters (building, point, clock, members), appended to the QuantBot block after the
//       bundle. A 9863 save (1.0.844) loads with no memory and no muster.
// 9865: QuantBot strategies (docs/ai-play-styles.md, "Strategies"): whether the strategy was
//       picked, its unit-mix strategy and its tempo, appended to the QuantBot block after the
//       musters. A 9864 save loads unpicked; the first update picks by the same pure function
//       of the game seed, the house and the config.
// 9866: QuantBot starting-mix bounds and tuned/bounded mixes (docs/ai-play-styles.md, section 8):
//       the starting ratios a Tuned or Bounded strategy resolved to (eight 16-bit basis points,
//       all zero for every other mix), appended to the QuantBot block after the strategy. A 9865
//       save loads as it is: none of its mixes needs them.
// 9868: QuantBot mix slots per special (docs/ai-play-styles.md, section 8): the resolved
//       Tuned/Bounded starting ratios hold ten slots (tank, siege tank, launcher, Devastator,
//       Sonic Tank, Deviator, ornithopter, trike, raider trike, quad) instead of the eight with one
//       special group. A 9866/9867 save loads its eight; the special share is split between the
//       specials the house can build. (The learning window keeps its eight-slot serialized form:
//       every production pass replaces it with the house's match totals before it is read.)
// 9871: The Natives house (HOUSE_NATIVES = 12, NUM_HOUSES 13): the object data, the house table
//       and every tile's explored/last-seen state carry a thirteenth house. 9869/9870 are left to
//       parallel branches. A 9868 or older save loads its twelve, gets no Natives house until one
//       is needed, and its sandworms are re-tagged to Natives on load (Game::loadSaveGame);
//       troopers an older save's crime raised cannot be told from a house's own and keep their
//       owner (see 9873).
// 9873: Natives follow-up (fix/natives-followup): the Natives house plays on its own team
//       (NATIVES_TEAM, the eleventh visibility slot, saved as the third bit of the byte that
//       holds teams 8 and 9, so object records keep their size), and a sandworm remembers the
//       house whose reinforcement brought it
//       (Sandworm::summonerHouseID; that house's team is spared). A 9871 or older save loads
//       with the Natives house moved off team 0, the new visibility slot copied from team 0's
//       (the team Natives saw with) and no summoner. Crime troopers of a pre-9871 save still
//       keep their owner: nothing that save stores tells them from the house's own (HANDOVER,
//       2026-10-24 Natives follow-up).
#define SAVEGAMEVERSION     9873
/// First save with the Natives house slot (NUM_HOUSES 13); older ones stored NUM_PLAYER_HOUSES.
#define SAVEGAMEVERSION_NATIVES 9871
/// First save with NATIVES_TEAM's visibility slot and the sandworm's summoner.
#define SAVEGAMEVERSION_NATIVES_TEAM 9873

// v1.0.0–v1.0.7 shipped SAVEGAMEVERSION 9810 with Num_ItemID=48.
// v1.0.8–v1.0.10 also used 9810 but with Num_ItemID=52 (4 items added
// without bumping the version — the bug this fixes).
#define LEGACY_NUM_ITEM_ID_DUNELEGACY  41   // Original Dune Legacy 0.99.x: Structure_LastID=19, Unit_Troopers=40
#define LEGACY_NUM_ITEM_ID_9810  48

#define MAX_PLAYERNAMELENGHT    24

#define DIAGONALSPEEDCONST (FixPt_SQRT2 >> 1)           // = sqrt(2)/2 = 0.707106781


#define GAMESPEED_MAX 32
// Fastest: 2ms per cycle (500 cycles/s), twice the previous 4ms setting.
// Default tick-based gameplay conversions remain at 16ms.
#define GAMESPEED_MIN 2
#define GAMESPEED_DEFAULT 16  // 16ms per cycle = default game speed (matches 0.97.5)
#define MILLI2CYCLES(MILLISECONDS) ((MILLISECONDS)/GAMESPEED_DEFAULT)   // this is calculated in game milliseconds (dune 2 has about the same in game speed "fastest")
#define VOLUME_MAX 100
#define VOLUME_MIN 0

#define NUM_ZOOMLEVEL   3

#define WINLOSEFLAGS_AI_NO_BUILDINGS        0x01
#define WINLOSEFLAGS_HUMAN_HAS_BUILDINGS    0x02
#define WINLOSEFLAGS_QUOTA                  0x04
#define WINLOSEFLAGS_TIMEOUT                0x08
// Bit 0x10 (set in some original Dune II scenarios, WinFlags=19/23) has no meaning and is ignored,
// as in Dune II and upstream Dune Legacy. DuneCity has no population/economic victory.

#define MAX_XSIZE 512
#define MAX_YSIZE 512

#define BUILDRANGE 2
#define MIN_CARRYALL_LIFT_DISTANCE 6
#define STRUCTURE_ANIMATIONTIMER 31

#define RANDOMSPICEMIN (111 - 37)        //how much spice on each spice tile
#define RANDOMSPICEMAX (111 + 37)
#define RANDOMTHICKSPICEMIN (222 - 74)
#define RANDOMTHICKSPICEMAX (222 + 74)

#define TILESIZE    64              // size of tile pieces 16x16 in zoom level 0

#define D2_TILESIZE 16              // the size of a tile in D2

#define SIDEBARWIDTH 144
#define SIDEBAR_COLUMN_WIDTH 12

#define NONE_ID (static_cast<Uint32>(-1))          // unsigned -1
#define INVALID_POS (-1)
#define INVALID (-1)
#define INVALID_GAMECYCLE (static_cast<Uint32>(-1))

#define NUM_TEAMS 9
/// The Natives house's team: no lobby row can choose it (lobby teams are 1-9, team 0 is the
/// anonymous houses a custom map creates), so worms and crime troopers are hostile to every
/// other house, anonymous ones included. It is no participant in any win/lose check.
#define NATIVES_TEAM (NUM_TEAMS + 1)
#define NUM_TEAM_SLOTS (NUM_TEAMS + 2)

#define DEVIATIONTIME MILLI2CYCLES(120*1000)
#define TRACKSTIME MILLI2CYCLES((1 << 16))
#define HARVESTERMAXSPICE 700
#define HARVESTSPEED (0.1344_fix)
#define BADLYDAMAGEDRATIO (0.5_fix)                //if health/getMaxHealth() < this, damage will become bad - smoke and shit
#define HEAVILYDAMAGEDRATIO (025_fix)             //if health/getMaxHealth() < this, damage will become heavy damage - red color
#define HEAVILYDAMAGEDSPEEDMULTIPLIER (0.75_fix)
#define ROADSPEEDMULTIPLIER          (4)          //how much faster ground units move on road tiles
#define NUMSELECTEDLISTS 9
#define NUM_INFANTRY_PER_TILE 5                 //how many infantry can fit on a tile

#define UNIT_REPAIRCOST (0.1_fix)
#define DEFAULT_GUARDRANGE 10                   //0 - 10, how far unit will search for enemy when guarding
#define DEFAULT_STARTINGCREDITS 3000

#define HUMANPLAYERCLASS        "HumanPlayer"
#define DEFAULTAIPLAYERCLASS    "qBotEasy"


#ifndef RESTRICT
#if defined(_MSC_VER)
#define RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#else
#define RESTRICT
#endif
#endif // RESTRICT

#endif //DEFINITIONS_H
