# QuantBot map checks: 807 gameplay, 808 menu update

Measured on 2026-10-06 with the real engine on the Mac mini. Atreides Brutal QuantBot won all 10 tested **All against Atreides** seeds against all four Hard original-AI houses. Six Brutal-vs-Brutal **Sihaya-Ferryman** games all reached a normal victory/defeat: Harkonnen won four, Atreides two. These are the observed samples under the configuration below, not a guarantee for untested seeds or rules.

## Maps, controllers and rules

- All against Atreides: `data/maps/singleplayer/5P - 128x128 - All against Atreides.ini`, 128x128, tech 8. Atreides `qBotBrutal` alone versus Harkonnen, Ordos, Fremen and Sardaukar `AIPlayerHard` allied together. The uncontrolled Mercenary sandworm house remains team 0.
- Sihaya-Ferryman: `data/maps/multiplayer/2P - 128x128 - 1v1 - Sihaya-Ferryman.ini`, 128x128, tech 8. Harkonnen and Atreides each use `qBotBrutal`, on opposing teams. The uncontrolled Fremen sandworms remain team 0. Three seeds, both roster orders; the resolved Player1/Player2 sections are recorded below.
- DuneCity city simulation; 1x spice; unlimited unit and harvester overrides (0); concrete required; explored map; no fog; worm respawn and killed-worm spice enabled; turret power requirement disabled; manual carryall drops explicitly disabled to match the preceding controlled test. One path worker. These explicit carryall rules are separate from the mod default.
- All-map starts are the map's own asymmetric setup: Atreides has 100,000 credits and a Construction Yard; the enemy houses each have 500 credits and extensive prebuilt bases. This evaluates the requested map challenge. Sihaya starts each player with 2,000 credits, an MCV and the map's eight-unit force.

Each observed house has a passive HumanPlayer followed by its Brutal QuantBot. For Custom games, `House::addPlayer` sets the house AI flag when the bot is added; campaign human-helper branches are gated out. The observer issues no orders. One Sihaya observer-side control retained the same winner and initial Hunt launch cycles; finish differed by 172 cycles (2.752 seconds), so no claim of byte-identical observer swaps is made.

## All against Atreides

| Seed | Result | Finish cycle | Game time | End Atreides combat troops | Rocket turrets built | Largest Hunt dispatch |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Won | 175505 | 46:48 | 341 | 121 | 114 |
| 7 | Won | 151695 | 40:27 | 483 | 96 | 153 |
| 43 | Won | 165392 | 44:06 | 489 | 106 | 279 |
| 101 | Won | 173486 | 46:15 | 305 | 136 | 165 |
| 1729 | Won | 160473 | 42:47 | 480 | 130 | 328 |
| 65537 | Won | 195264 | 52:04 | 318 | 127 | 230 |
| 314159 | Won | 144489 | 38:31 | 575 | 101 | 324 |
| 27182818 | Won | 189508 | 50:32 | 327 | 130 | 214 |
| 486409243 | Won | 153948 | 41:03 | 352 | 106 | 158 |
| 1547732733 | Won | 143964 | 38:23 | 368 | 104 | 123 |

All four enemy houses are `alive=0` and Atreides is `alive=1` in every engine `game_summary`. Times range from 38:23 to 52:04; median 43:26. Named house sections fix the starting layout; these seeds vary combat/economy randomness, not the map's starting positions.

## Sihaya-Ferryman

| Seed | Player1 house | Winner | Game time | Final Harkonnen combat troops | Final Atreides combat troops | Harkonnen / Atreides turrets built | Harkonnen / Atreides largest Hunt |
|---:|---|---|---:|---:|---:|---:|---:|
| 1 | Atreides | Harkonnen | 49:58 | 248 | 0 | 20 / 18 | 142 / 84 |
| 1 | Harkonnen | Harkonnen | 45:36 | 420 | 0 | 96 / 21 | 152 / 76 |
| 43 | Harkonnen | Harkonnen | 63:01 | 358 | 1 | 74 / 68 | 120 / 93 |
| 43 | Atreides | Harkonnen | 68:18 | 445 | 0 | 66 / 102 | 117 / 134 |
| 486409243 | Harkonnen | Atreides | 89:01 | 0 | 343 | 80 / 78 | 166 / 143 |
| 486409243 | Atreides | Atreides | 54:29 | 0 | 510 | 14 / 83 | 79 / 275 |

Three matches were still active at the initial 60-minute cap. They were rerun once with the same seed/rules and a 120-minute cap, and all finished. The normalized decision/event records before cycle 225,000 match the original 60-minute run exactly for all three extensions. These capped runs are not additional independent samples.

All logged `ground_hunt` events in both matrices have `whole_army=1`, `campaign_limited=0`; actual dispatch sizes, repeated base scrambles, posture changes, production and combat are retained in `verified-summary.json` for each run. This is evidence of army activity; repair, transport and other authority holds still matter, and a Hunt order alone is not proof that every unit can navigate.

## Economy and unit results

Sihaya final per-house ledgers (credits and unit counts):

| Seed / Player1 | House | Spice refined | Net city income | Storage credits lost | Total spent | Combat units built / lost / alive | Turrets built / alive |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 / Atreides | Harkonnen | 265,009 | 55,926 | 109,150 | 194,944 | 402 / 226 / 248 | 20 / 20 |
| 1 / Atreides | Atreides | 398,275 | 20,596 | 275,528 | 145,722 | 251 / 440 / 0 | 18 / 0 |
| 1 / Harkonnen | Harkonnen | 377,040 | 47,799 | 26,572 | 370,810 | 673 / 375 / 420 | 96 / 82 |
| 1 / Harkonnen | Atreides | 241,053 | 25,912 | 120,487 | 149,417 | 277 / 455 / 0 | 21 / 0 |
| 43 / Harkonnen | Harkonnen | 350,912 | 159,408 | 13,538 | 490,701 | 1051 / 894 / 358 | 74 / 74 |
| 43 / Harkonnen | Atreides | 567,135 | 86,878 | 258,311 | 403,482 | 736 / 1157 / 1 | 68 / 0 |
| 43 / Atreides | Harkonnen | 647,780 | 142,359 | 203,683 | 596,841 | 1224 / 1003 / 445 | 66 / 66 |
| 43 / Atreides | Atreides | 538,667 | 115,640 | 206,406 | 455,585 | 1007 / 1492 / 0 | 102 / 0 |
| 486409243 / Harkonnen | Harkonnen | 529,120 | 194,863 | 26,701 | 699,349 | 1610 / 2153 / 0 | 80 / 0 |
| 486409243 / Harkonnen | Atreides | 726,987 | 143,295 | 75,270 | 780,074 | 1545 / 1721 / 343 | 78 / 72 |
| 486409243 / Atreides | Harkonnen | 292,519 | 46,935 | 159,950 | 182,167 | 330 / 544 / 0 | 14 / 0 |
| 486409243 / Atreides | Atreides | 369,782 | 138,746 | 11,119 | 491,133 | 839 / 526 / 510 | 83 / 83 |

The defeated economies often refined a lot of spice but lost substantial credits to storage and spent less on production. For example, Atreides in seed 1 with Player1=Atreides refined 398,275 but recorded 275,528 storage loss and 145,722 total spending; Harkonnen spent 194,944. This identifies a useful storage/production investigation, not proof that storage alone caused defeat. Both sides use the same Brutal policy. The four-versus-two outcome is too small a sample to certify faction balance.

Aggregate Sihaya combat-unit ledgers, six completed games (each game counted once):

| House | Unit | Built | Lost | Alive at match end, summed | Killing blows | Combat reward proxy | Lost value |
|---|---|---:|---:|---:|---:|---:|---:|
| Harkonnen | Devastator | 165 | 97 | 74 | 204 | 69,367 | 77,600 |
| Harkonnen | Soldier | 0 | 1,076 | 14 | 4 | 1,481 | 64,560 |
| Harkonnen | Launcher | 1,260 | 794 | 472 | 1,502 | 771,665 | 357,300 |
| Harkonnen | 'Thopter | 507 | 402 | 105 | 539 | 322,786 | 241,200 |
| Harkonnen | Quad | 1,226 | 989 | 237 | 574 | 198,414 | 197,800 |
| Harkonnen | Siege Tank | 507 | 327 | 192 | 906 | 314,160 | 196,200 |
| Harkonnen | Tank | 324 | 217 | 119 | 185 | 65,320 | 65,100 |
| Harkonnen | Trike | 1,301 | 1,113 | 255 | 521 | 152,634 | 166,950 |
| Harkonnen | Trooper | 0 | 180 | 3 | 7 | 3,035 | 18,000 |
| Atreides | Soldier | 0 | 1,370 | 5 | 5 | 1,637 | 82,200 |
| Atreides | Launcher | 627 | 281 | 352 | 485 | 296,967 | 126,450 |
| Atreides | 'Thopter | 365 | 309 | 56 | 237 | 127,764 | 185,400 |
| Atreides | Quad | 1,201 | 1,138 | 63 | 354 | 139,115 | 227,600 |
| Atreides | Siege Tank | 186 | 151 | 47 | 234 | 84,838 | 90,600 |
| Atreides | Sonic Tank | 930 | 697 | 239 | 2,705 | 998,664 | 418,200 |
| Atreides | Tank | 233 | 217 | 28 | 93 | 41,570 | 65,100 |
| Atreides | Trike | 1,113 | 1,108 | 56 | 215 | 83,492 | 166,200 |
| Atreides | Trooper | 0 | 520 | 8 | 25 | 12,694 | 52,000 |

Combat reward is the engine's credit-equivalent damage and kill/conversion proxy, not cash income. Starting forces, palace-granted troops and captured units mean built minus lost does not equal survivors. Survivors summed across different finish times are descriptive counts, not a controlled unit-efficiency comparison. Individual per-game ledgers and attacker/victim lethal-blow matrices are in the verified JSON artifacts; original AI decision reasoning is not logged, but its engine production/loss/combat ledgers are.

## Performance limits and remaining work

The large Atreides-winning seed 486409243 game (Player1=Atreides) recorded 148,337 path-wait observations out of 545,033 movement-pause observations (27.2%). Its worst recorded 30-game-second window had 20,133 path waits out of 22,107 pauses (91.1%). These counts describe paused navigation attempts, not a fraction of game time, stationary units, or rendered frames. They warrant a further path-queue investigation under a rendered workload.

The headless driver advances `updateGameState()` without rendering. It retains the 15,000-node base budget and carry-over; its frame-count-dependent adaptive FPS branch is not exercised. Wall durations include logging and up to two concurrent simulations on the Mac mini. No MBA FPS or frame-time percentile claim is made from this batch, and no extra threading/cache/squad change was introduced on this evidence.

## Provenance, UI and installation

- Gameplay source: `abdf71e70697a01cae2ba27ed86aa9bfc2103e97` (807). Frozen diagnostic engine SHA-256: `c34df1bb6dc20df0b65d200c8ccb7d2c70a5c4b2b333d37e2734c865d1625b34`.
- All-map SHA-256: `40179c370eb94cf794d028719e28139a7151737a1af3e18acc93690fa1cf747d`. The earlier cached map and bundled map match byte-for-byte.
- Sihaya SHA-256: `a001c8fb5450e9448a46fbe43983bbc202fe5a7b2399e441342e257df15e53b6`.
- Runtime gates assert mod, mode, seed, map size, tech, options, resolved controller-house bindings, team partition and neutral worm houses. Outcomes are checked against the engine final house-alive ledger, not just process success. No capture was counted when the launcher failed before loading a game due to a missing `BALANCE_LEVEL`; those logs remain under `launcher-failure-missing-level/`.
- 808 source: `08f5a0d3ae1e24d80ee88757ce18379a0bf326ff`. Menu-only measured Spice label width, vertical centring and explanatory line above the roster; AI policy/protocol unchanged. Native CTest and existing menu navigation probe pass; rendered layouts reviewed at 640x480, 854x480 and 1280x720. 807's preceding full suite passed all 56 targets.
- 808 installed and separately verified on `Stefans-MacBook-Air.local` at `/Applications/dunecity.app`, executable SHA-256 `22e649c8cfdfecb03102a210caccffb47a6d91bde18261680b7d2a894066820f`. All 36 bundled ARM64 Mach-O files have portable load paths; deep/strict signature and bundled SDL initialization/hidden rendering pass. Native/portable code and string sections match. All 2,066 save/INI files and the Desktop shortcut were verified; no running game was stopped or launched.
- 807 rollback: `/Applications/.dunecity-backup-before-808-20261006/dunecity.app`. Protocol 55, save format 9852, observer runtime 7 and policy `whole-army-base-scramble-v90` remain.
- Raw artifacts: `/Users/stefan/Documents/projects/outputs/quantbot-map-evaluation-807-20261006`. Summary: `verified-matrix-summary.json`; individual `verified-summary.json`; `extension-prefix-verification.json`; installation receipts and independent verification. No push or public publication.
