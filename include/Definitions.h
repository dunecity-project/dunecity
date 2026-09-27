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
#define SAVEGAMEVERSION     9849

// v1.0.0–v1.0.7 shipped SAVEGAMEVERSION 9810 with Num_ItemID=48.
// v1.0.8–v1.0.10 also used 9810 but with Num_ItemID=52 (4 items added
// without bumping the version — the bug this fixes).
#define LEGACY_NUM_ITEM_ID_DUNELEGACY  41   // Original Dune Legacy 0.99.x: Structure_LastID=19, Unit_Troopers=40
#define LEGACY_NUM_ITEM_ID_9810  48

#define MAX_PLAYERNAMELENGHT    24

#define DIAGONALSPEEDCONST (FixPt_SQRT2 >> 1)           // = sqrt(2)/2 = 0.707106781


#define GAMESPEED_MAX 32
#define GAMESPEED_MIN 4   // Fastest: 4ms per cycle (twice the previous 8ms maximum speed)
#define GAMESPEED_DEFAULT 16  // 16ms per cycle = default game speed (matches 0.97.5)
#define MILLI2CYCLES(MILLISECONDS) ((MILLISECONDS)/GAMESPEED_DEFAULT)   // this is calculated in game milliseconds (dune 2 has about the same in game speed "fastest")
#define VOLUME_MAX 100
#define VOLUME_MIN 0

#define NUM_ZOOMLEVEL   3

#define WINLOSEFLAGS_AI_NO_BUILDINGS        0x01
#define WINLOSEFLAGS_HUMAN_HAS_BUILDINGS    0x02
#define WINLOSEFLAGS_QUOTA                  0x04
#define WINLOSEFLAGS_TIMEOUT                0x08
#define WINLOSEFLAGS_ECONOMIC               0x10

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
#define NUM_TEAM_SLOTS (NUM_TEAMS + 1)

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
