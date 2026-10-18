# AI scenario matrix

Stefan, 2026-10-18: "you need to increase the diversity of game you test - construction yard, prebuilt
base, dunecity vs vanilla, all against atreides VANILLA, some campaign games to make sure campaigns
don't break. e.g. a brutal should defeat a medium quantbot on the enemy. a hard or maybe even a medium
should beat an easy. test different levels from 1-9 ... a few of the MP popular maps for 1v1, 2v2, 3v3".

Until 1.0.847 almost every AI measurement was a 1v1 or All against Atreides. The matrix plays many
kinds of game with one driver, `tests/ai/scenario-matrix.py`, and prints one summary table.

## How a game is played

Every game is a real match played headless to its end by the balance probe:
`run-campaign-balance.py --scenario` (probe code `tests/ai/scenario-match.inc`, dispatched from
`campaign-balance-probe.inc`). Nothing in the shipped game changes.

- **Matches** (`--scenario-sides house:team:class,...` with `--custom-map FILE` or
  `--skirmish key=value,...`): every house gets its own team number and controller class (QuantBot
  `qBotEasy..qBotBrutal`, AI Player `AIPlayerEasy..Hard`); a local spectator watches. The game runs until
  one team is left (`House::isAlive`, the engine's defeat rule) or the time limit. `--skirmish` generates
  a Random Skirmish *multiplayer* map (one start per side, every start alike: `size`, `base`
  none = MCV only / `cy` / outpost / small / medium / large / fortress, `army` = `kArmyBudgets` index
  0 none, 2 = 2,500 small, 5 = 20,000 large, `city`, `seed`, `tech`, `rock`, `spice`, `credits`,
  `symmetric`); its mode (vanilla or DuneCity city simulation) follows `--mod` as in the menu.
  `base=cy` is a Construction Yard only start: each start's MCV is deployed where the map put it before
  the first cycle (`SCENARIO_CY_START`), no army, the menu's default 3,000 credits.
- **Campaign missions** (no sides; `--house`, `--level`, optional `--mission`): the human seat is shared
  by an idle human player and a QuantBot partner of `--partner-difficulty`; the enemies are the campaign's
  default enemy AI (QuantBot Easy, `HouseChoiceMenu` `kEnemyAIClasses[0]`), or with
  `--enemy-ai campaign` the "scenario settings" `CampaignAIPlayer`. The game runs until the mission's own
  win/lose conditions end it (`Game::finished`: conquest, harvest quota, or the DuneCity economic victory
  - city population over the mission's threshold) or the time limit.
- Game rules come from the mod (`ModManager::loadEffectiveGameOptions`) as a new Custom Game takes them;
  `--game-options` (All against Atreides: Stefan's own options) applies on top.
- Once a game minute every house is sampled, and its **sortie** measured: combat ground units more than
  20 tiles (Chebyshev) from every structure of their house; a minute with 3 or more away is a sortie
  minute. Scenario triggers (reinforcements, time-outs) are counted as they fire.
- The end logs `SCENARIO_HOUSE` per house (team, controllers, alive, elimination time, kill/loss value,
  structures, army, peak units away, sortie minutes) and `SCENARIO_RESULT` (outcome, winning team, game
  minutes, triggers and how many fired, win/lose flags, quota, city population and economic threshold),
  then the final state digest (`PROBE_DIGEST ... final=1`).

The driver writes `games.csv` (one row per game; the win type of a campaign win: conquest, economic or
quota) and `houses.csv` (one row per house, with its `match_score` placement score: teams are entrants,
a time-out is a negative draw). Games already in `games.csv` are skipped; failed games are retried on the
next run; a crashed game's last log lines go to `failed-*.txt`.

## The matrix

Controller codes: QE/QM/QH/QB = QuantBot Easy/Medium/Hard/Brutal, AE/AM/AH = AI Player Easy/Medium/Hard.
Houses are Harkonnen, Atreides, Ordos, Sardaukar, Fremen, Mercenary, Rebels, Neutral (first N), rotated
by the seed's index so every controller plays every house; map-named houses (All against Atreides, the
6P maps) stay fixed.

| Category | Cells | What is checked |
|---|---|---|
| `skirmish` | 13 Random Skirmish cells: 1v1, 3-8 player FFA, 2v2, 3v3, 4v4; MCV only, small, medium and fortress bases; no, small and large armies; 8 vanilla, 5 DuneCity; QuantBot difficulties mixed with AI Player; 4 seeds each | decided, eliminations, idle armies; team cells with a stronger team must be won by it; placement score per controller (QB > QH > QM > QE) |
| `cy` | 7 Construction Yard only cells: 1v1 QH v AH, QH v QM, QB v QE (40 min), QH mirror; 4P FFA vanilla and DuneCity; 2v2 mirror | as above |
| `aaa` | All against Atreides, 6 quantbot_all_against_atreides seeds each: vanilla Brutal v 4 AI Player Hard (win within 40 min), the same in DuneCity (win within 40 min), the reverse Atreides Medium v 4 QuantBot Hard (must lose) | expected winner |
| `campaign` | Atreides, Harkonnen and Ordos; levels 1-9: the level's first mission and, for levels 2-8, the second (missions 1-22 of `CampaignControls`); vanilla and DuneCity; partner Hard (2 seeds), Medium and Easy (1 seed); vanilla first missions also against `CampaignAIPlayer` | the mission loads and runs; Hard must win; how (conquest/economic/quota); triggers fired |
| `ladder` | QB v QM, QH v QE, QM v QE on a vanilla and a DuneCity Random Skirmish map (96x96, small bases, small armies) and Habbanya-Penny; 8 seeds per pair and map, sides alternating | the stronger QuantBot's placement score interval above 0 (over all maps) |
| `mp` | The most-played bundled maps (dunelegacy.com usage statistics, all time, 2026-10-18): 1v1 Habbanya-Penny (364 games), Tuono-Orac (104), Sihaya-Chvrches (82); 2v2 Sihaya-Toto (113), Deserted (105), 4P DuneCity (68, DuneCity mod); 3v3 Sihaya-Lenka (536), Full Wormage, Alkozeltser 4 (265, 256x256: 2 seeds, 80 min). QH teams v QH teams (4 seeds), and QH teams v AH teams on the 2v2/3v3 maps (must win) | decided, idle armies, expected winner |

Time limits: 60 game minutes (7-8 player and 256x256 maps 80; All against Atreides vanilla 41, as the
mandatory test; campaign 80).

### Flags

The report lists, per cell, the winners by team and controllers, the share decided, the expectation
check (k/n with a Wilson 95% interval) and these flags, and collects every problem at the end:

- **crash**: the probe failed or left no result;
- **time-out**: two or more teams alive at the limit; **nobody eliminated** in a game of 3+ houses;
- **idle**: a house that lasted 20+ game minutes without a single sortie minute or kill;
- **campaign not finished / lost** (the Hard partner must win every mission);
- **inverted**: an expectation's interval wholly below 50%, or the ladder's strong side scoring below 0.

## Tiers

**Smoke** (`ctest -R quantbot_scenario_matrix`, mandatory with `quantbot_all_against_atreides`):

| Game | Pass when |
|---|---|
| skirmish 4P FFA, 128x128, medium bases, small armies, vanilla, QB/QH/QM/QE | someone eliminated, nobody idle, decided |
| cy 1v1 QB v QE, 64x64, DuneCity, played twice | QB wins within 40 min; the replay's final digest equals the first game's |
| aaa reverse: Atreides QM v 4 QH (first quantbot_all_against_atreides seed) | the enemies win |
| ladder QH v QE, vanilla Random Skirmish 96x96, both sides | QH wins both |
| mp 2v2 Sihaya-Toto, QH+QH v QH+QH | decided, two or more houses sortie |
| campaign Atreides L1 (vanilla), Ordos L4 (vanilla), Harkonnen L9 (DuneCity), Hard partner | won |

Every game must also finish without a crash. 10 games, three at a time.

**Full** (`--tier full`, 617 games): the whole matrix.

## Running

```sh
# smoke, as ctest runs it
python3 tests/ai/scenario-matrix.py --tier smoke --fresh --jobs 3 --build-dir build --results build/scenario-matrix-smoke
# full tier, one machine
python3 tests/ai/scenario-matrix.py --tier full --jobs 8 --results ~/Documents/projects/dunecity-results/scenario-matrix/<tag>
# a slice: --categories ladder campaign, --seeds-per-cell 2, --list (print the plan)
# report over any number of results directories (e.g. the four shards)
python3 tests/ai/scenario-matrix.py --report DIR1 DIR2 ...
```

Split across the four machines with `--shard K/N` (every N-th game from the K-th), each into its own
results directory, then `--report` all of them:

```sh
dunecity-pro sync $PWD && dunecity-pro run --jobs 8 sm-<tag>-s1 scenario-matrix --tier full --shard 1/4
dunecity-air sync $PWD && dunecity-air run --jobs 7 sm-<tag>-s2 scenario-matrix --tier full --shard 2/4
# PC: bundle the branch to C:\dunecity\deps, fetch + checkout in C:\dunecity\src, detach build.ps1, then
#   ps-batch.ps1 -Tag sm-<tag>-s3 -Harness scenario-matrix -Jobs 8 -ArgsFile <file: --tier full --shard 3/4>
dunecity-queue submit --slots 3 --cwd $PWD --tag sm-<tag>-s4 --owner <you> -- \
    python3 tests/ai/scenario-matrix.py --tier full --shard 4/4 --results <dir> --jobs 3
dunecity-pro pull sm-<tag>-s1 <dir1>; dunecity-air pull sm-<tag>-s2 <dir2>   # PC: scp C:\dunecity\results\sm-<tag>-s3\*.csv
```

The runners' `run-batch.sh` accepts `scenario-matrix` as a harness (added 2026-10-18). Parity: the
smoke tier on the Pro, the Air and the PC gives the same final digest for every game as on the mini.

## Machines and time

Full tier, 635 games, 2026-10-18, split `--shard K/4`:

| Machine | Jobs | Games | Wall | Notes |
|---|---|---|---|---|
| MacBook Pro (`dunecity-pro`) | 8 | 159 | 21 min | |
| MacBook Air (`dunecity-air`) | 7 | 159 + 158 | 21.5 + 25 min | ran shards 2 and 4 (the mini queue was full) |
| Windows PC (`dunecity-win`) | 8 | 157 | 32 min | one 4v4 DuneCity fortress game hit the 1,800 s wall limit (625 s on the Pro) and was replayed on the Air |
| Mac mini (queue) | 3 | smoke only | 1.7-2.2 min smoke | queue was full all afternoon |

So the full tier is about 25 minutes on three runners (about 3 CPU-hours: campaign missions take
5-40 s wall, most matches 20-120 s, 7-8 player and 4v4 DuneCity games up to 10 minutes). The smoke
tier takes 70 s (8 jobs) to 130 s (3 jobs, loaded mini). Parity: the smoke tier's ten final digests are
identical on the mini, the Pro, the Air and the PC (the PC prints some `end_s` 0.1 s lower: printf
rounding of the same cycle count).

## Results on 1.0.847 (staging `integrate/feature-batch` b683286f)

Full report: `docs/ai-scenario-matrix-results/847/report.md`; raw CSVs
`~/Documents/projects/dunecity-results/scenario-matrix/full-847`. Smoke on 1.0.847: 9/10 pass, the
4P FFA times out with nobody eliminated (ctest `quantbot_scenario_matrix` fails on this baseline; the
other 36 tests of `ctest -j3 -R 'quantbot_|dunelegacy_tests|skirmish_generator'` pass).

No crash in 635 games (the only failures were harness ones: Full Wormage's binary map header, now
read; one PC wall-time limit), no idle army anywhere (every house that lasted 20 minutes made sorties or
kills). What holds:

- **All against Atreides vanilla** 6/6 within 40 min (mean 31 min); **reverse** (Atreides Medium v 4
  QuantBot Hard) lost 6/6 as it must; **DuneCity** Brutal wins 5/6 (mean 41 min; one seed lost).
- **Hard v Easy** 23-1 (score +0.92 [+0.75, +1.00]); **Brutal v Medium** 17-5-2 (+0.48 [+0.12, +0.78]),
  but only 4-4 on the small vanilla Random Skirmish map; QuantBot QH teams beat AI Player Hard teams
  in every 2v2/3v3 (24/24) and QH beats AH 1v1 CY 4/4.
- Placement score by controller over Random Skirmish: QB +0.30, QH +0.18, QM -0.13, AH -0.28,
  QE -0.35, AM -0.57 - the QuantBot order holds overall.
- **Campaign**: 411 missions run, every one loads and runs, triggers fire (1,287 fired); the Hard
  partner wins 174/192 (vanilla 84/96, DuneCity 90/96).

Problems found:

1. **Free-for-alls do not end.** 4-8 house FFAs time out in 17 of 24 skirmish FFA games and 5/8 CY
   FFAs (6P DuneCity 4/4), often with nobody eliminated although every house fights (kill values 20-90k).
   Team games mostly end. The smoke FFA is one of them (ArmyStuck's bug).
2. **Medium does not beat Easy.** QM v QE 7-3-14: 8/8 time-outs on Habbanya-Penny, 6/8 on the DuneCity
   map, 5-3 on the vanilla map; score +0.02 [-0.23, +0.31] - the ladder order is not shown.
3. **3v3 Hard team loses to the Medium team** on the Random Skirmish 160x160 medium-base map (0/4).
4. **Hard partner loses late campaign missions**: vanilla level 8-9 (missions 20-22) Atreides/Harkonnen
   lose 8 of 12 Hard games against QuantBot Easy enemies (Ordos m21 once); Atreides m11 lost once;
   Harkonnen m14 and Ordos m18 once not finished; DuneCity: Atreides/Harkonnen m22 lost once each, Ordos
   m17 never finishes (80 min, 2/2), Harkonnen m3 and Ordos m1 once not finished; against
   `CampaignAIPlayer` 4/27 lost (Atreides m2, m5, m20; Ordos m5).
5. **DuneCity campaigns are won economically**: 74 of 90 DuneCity Hard wins are the economic victory
   (city population past the mission threshold, e.g. 505/500 at 8.6 min on Ordos level 4) with little or
   no fighting; mean win 16 min against 20 min vanilla.
6. DuneCity games end later: Brutal v Medium on the DuneCity Random Skirmish map 2/8 time-outs, 4v4
   fortress 2/3 time-outs, the 4P DuneCity map QH mirror 3/4 time-outs.
