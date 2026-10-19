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
  win/lose conditions end it (`Game::finished`: conquest or harvest quota; the DuneCity economic victory
  is removed on `fix/remove-economic-victory`) or the time limit.
- Game rules come from the mod (`ModManager::loadEffectiveGameOptions`) as a new Custom Game takes them;
  `--game-options` (All against Atreides: Stefan's own options) applies on top.
- Once a game minute every house is sampled, and its **sortie** measured: combat ground units more than
  20 tiles (Chebyshev) from every structure of their house; a minute with 3 or more away is a sortie
  minute. Scenario triggers (reinforcements, time-outs) are counted as they fire.
- The end logs `SCENARIO_HOUSE` per house (team, controllers, alive, elimination time, kill/loss value,
  structures, army, peak units away, sortie minutes) and `SCENARIO_RESULT` (outcome, winning team, game
  minutes, triggers and how many fired, win/lose flags, quota, city population),
  then the final state digest (`PROBE_DIGEST ... final=1`).

The driver writes `games.csv` (one row per game; the win type of a campaign win: conquest or
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
| `campaign` | Atreides, Harkonnen and Ordos; levels 1-9: the level's first mission and, for levels 2-8, the second (missions 1-22 of `CampaignControls`); vanilla and DuneCity; partner Hard (2 seeds), Medium and Easy (1 seed); vanilla first missions also against `CampaignAIPlayer` | the mission loads and runs; Hard must win; how (conquest/quota); triggers fired |
| `ladder` | Every adjacent pair (QB v QH, QH v QM, QM v QE) plus QB v QM and QH v QE, on a vanilla and a DuneCity Random Skirmish map (96x96, small bases, small armies), Habbanya-Penny and a vanilla Construction Yard only 96x96 start; 18 seeds per pair and map, each played twice with the houses swapped (`#swap`; each side keeps its start): 144 games per pair (`--ladder-seeds 9`: 72) | the stronger QuantBot's placement score interval above 0 (over all maps; the interval resamples seeds) |
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

**Full** (`--tier full`, 1,283 games, 720 of them ladder): the whole matrix.

## Running

```sh
# smoke, as ctest runs it
python3 tests/ai/scenario-matrix.py --tier smoke --fresh --jobs 3 --build-dir build --results build/scenario-matrix-smoke
# full tier, one machine
python3 tests/ai/scenario-matrix.py --tier full --jobs 8 --results ~/Documents/projects/dunecity-results/scenario-matrix/<tag>
# a slice: --categories ladder campaign, --seeds-per-cell 2, --cells 'QB-v-QM|3v3' (regex on
# category/cell/mod), --ladder-seeds 9 (72 ladder games per pair), --list (print the plan)
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
5. **DuneCity campaigns were won economically on 1.0.847**: 74 of 90 DuneCity Hard wins were the economic
   victory (since removed: `fix/remove-economic-victory`), e.g. 505/500 population at 8.6 min on Ordos
   level 4, with little or no fighting; mean win 16 min against 20 min vanilla.
6. DuneCity games end later: Brutal v Medium on the DuneCity Random Skirmish map 2/8 time-outs, 4v4
   fortress 3/4 time-outs (the Hard team wins the fourth), the 4P DuneCity map QH mirror 3/4 time-outs.

## Difficulty ladder, team games and late campaign missions (branch `fix/ai-difficulty-ladder`)

Stefan: "Brutal > Medium, Hard > Easy, Medium > Easy". Before = `fix/ai-army-stuck-in-base` 8a8e4833 (+ the
ladder's CY map), after = 5ad8968d (game code identical to the branch head). Results:
`~/Documents/projects/dunecity-results/difficulty-ladder/` (`before`, `c9`, `c5-campaign`; reports
`before-report.md`, `after-report.md`, `after-campaign-report.md`; diagnoses `diag-*`).

### What Medium and Easy differed in, and why Habbanya-Penny timed out

In custom games Easy and Medium shared one army behaviour: a standing raid share (Easy 25%, Medium 40% of
the ground army per wave, the rest at home, `SimpleArmyPolicy::customAttack`), both at the global 40%
attack threshold of their limits (Easy 15,000 / Medium 25,000 on 96-128 maps), no ornithopter raids. They
differed only in limits, harvesters, defenders, infantry share and repair yards. On Habbanya-Penny both
reached their caps (Medium 24,000 of 25,000 with 117,000-158,000 credits unspent, Easy 15,000 behind its
turrets) and traded evenly for 60 minutes: 8/8 time-outs on 1.0.847.

Brutal lost to Medium on small maps for the opposite reason: its first wave waited for 25% of its 80,000
limit (20,000) while Medium's left at 10,000; in 23 of 72 games Brutal had 3,500-10,500 at home, never
launched (sortie 0-2 minutes) and died to Medium's first wave at 17-27 minutes. On CY starts Medium's
ornithopters also razed Brutal's undefended base at minute 9-10.

### Changes

- **Custom Medium joins the whole-army difficulties** (`QuantBot::autonomousCustomArmy`, formerly
  `autonomousCustomHardBrutal`): one concentrated wave, recovery posture, force concentration, engine Hunt,
  whole-army base defence. Easy keeps the raid share (Easy at its own level is unchanged). Air raids stay
  Hard/Brutal.
- **Held opening capped at Medium's limit:** a whole-army house that started without an army able to strike
  ends its held opening at its own threshold share of at most Medium's limit (Brutal 6,250, Hard 7,500
  (global 40% = 10,000), Medium 10,000 on 96-128 maps). Starting armies keep their thresholds
  (`quantbot_ffa_army_deploys`). Applying it to every wave (c10) was not better and was reverted.
- **Reachable readiness:** a custom house whose credits plus the spice left cannot buy its threshold attacks
  with what it has (`SimpleArmyPolicy::reachableReadiness`).
- **Medium spends banked credits:** up to half its cap above it from credits over 10,000
  (`QuantBotBuildPolicy::surplusMilitaryBudget`; Medium x 1.5 stays below Hard's cap). +100% let the reverse
  All against Atreides Medium hold off four Hard houses for 60 minutes.
- **Team play:** "outnumbered" and the perimeter turret goal compare our team's ground army with the
  strongest enemy team (`teamGroundArmyValue`), and the held opening ends once the team's army is worth the
  house threshold. In 1.0.847 each Hard ally compared its own army with the whole Medium team, was
  "outnumbered" and garrisoned, and none of the three launched a wave in any of 12 seeds (1/12 won).
- **Campaign partner:** the economy push no longer plans 43-56 harvesters as if alone on the map (no army
  for 12 minutes in missions 20-22); a Palace missile on a harvester is not a contact to scramble the army to
  (vanilla/DuneCity m22); after 15 minutes the partner's wave readiness is measured against the strongest
  enemy house's army (1.25x, at least 2,000, at most 75% of its limit) instead of a fixed 8,000.
- **DuneCity campaign stalls:** structures keep off the reserved factory plots (five refineries on the heavy
  factory's plot, Ordos m17: no heavy factory in 80 minutes); silos are built where the tech level has no
  heavy factory; the spice quota counts the store's tax credits too in DuneCity (taxes fill the shared store
  first, so Harkonnen m3 / Ordos m1 quotas could never be met - this also applies to human players).
- **Medium starport opening** walks to the heavy factory when it may not add a repair yard (the 7P FFA
  idle Medium had 17 harvesters and no factory).
- **Random Skirmish generator:** an asymmetric map whose start has no spice within three times its own
  field's seeding distance by ground is regenerated (next seed; the first connected one if none passes).
  The 7P FFA idle house's nearest spice was 91 tiles round a massif. Symmetric maps are fair by construction
  and unchanged; every pinned generator digest is unchanged.

### Results (cluster, full tier)

| Ladder pair (72 games) | Before W-L-T | Before score [95% CI] | After W-L-T | After score [95% CI] |
|---|---|---|---|---|
| QB v QM | 43-24-5 | +0.25 [+0.02, +0.47] | 46-24-2 | +0.30 [+0.08, +0.50] |
| QH v QE | 62-9-1 | +0.73 [+0.57, +0.87] | 64-8-0 | +0.78 [+0.64, +0.92] |
| QM v QE | 32-10-30 | +0.20 [+0.02, +0.38] | 58-11-3 | +0.64 [+0.46, +0.80] |

QM v QE time-outs 30/72 -> 3/72; on Habbanya-Penny 0-0-18 -> 17-0-1. QB v QM by map after: Habbanya-Penny +0.82, rs-vanilla
+0.22, rs-dunecity +0.15, CY 0.00 (before -0.29).

Campaign, Hard partner (QuantBot Easy enemies): vanilla 84/96 -> 92/96, DuneCity 90/96 -> 96/96 (no
mission left unfinished; Ordos m17, Harkonnen m3, Ordos m1 all won); vanilla missions 20-22 Atreides and
Harkonnen 4/12 -> 9/12. Still lost: Atreides m20 (both seeds; the Harkonnen enemy holds 11,000-16,000 and 8
turrets, waves trade 1:1), Harkonnen m22 seed 95897 (a captured Devastator detonated in the base, a
sandworm), Atreides m17 seed 80762 (new). Against `CampaignAIPlayer` 23/27 as before.

Team games: 3v3 Hard v Medium 0/4 -> 1/4 (the Hard houses now sortie 5-21 minutes; a Medium team has the
same army tactics now, so Hard's edge is its economy and limit only - not fixed); 4v4 fortress 1/3 -> 2/4;
reverse All against Atreides 6/6 lost by Medium as required; All against Atreides vanilla 6/6 within 40
min, DuneCity 6/6 (before 5/6). 7P FFA: no idle house (before QM idle 1/4). Regressions: 6P DuneCity FFA
0/4 decided (before 3/4), the skirmish controller order is QH +0.26 > QB +0.18 > QM +0.12 > QE (Brutal
plays 20 houses, interval -0.13..+0.48).
