# 1.0.807 promotion check

An AI switch that passed its competitive paired/control games also needs the
historical **decisiveness/timeout-regression check** before promotion: on the
1.0.807 deck, candidate games ending with any winner must be at least the
same-source OFF count. This does **not** measure victories against the old AI.
All houses run that arm's policy; the historical count is informational.
Independent candidate-seat and held-out games establish competitive improvement.
The earlier historical statistical gate was retired; no policy is changed here.

## The deck

`tests/ai/ai-baseline-league.py` replays the 192 frozen research-OFF games the
1.0.807 reference played, with the exact maps, rosters, seeds and requested
options in `tests/ai/baseline-fixtures/`: DuneCity 96 (no and medium spice),
Vanilla 48 and Dune2R 48 (medium spice); 1v1 and six-house FFA; Medium, Hard and
Brutal; seeds 9101, 148680876, 55001, 77001, 1841712204, 8101, 8202, 8303;
128x128, CY start, 3,000 credits, technology 9, 40 game minutes. Every house
runs the build's own AI (a style applies to every house), so a game counts as a
win when it ends with a winner (`outcome=decided`: every remaining house except
the hidden Natives shares one team) before the cap. A time-limit game is no win.

The deck is research-OFF because 1.0.807 has no Research Lab. A switch that
only acts with research ON plays identical games here and passes with equal
counts; its effect is measured by its paired games, not by this check.

## Run it

Play the deck twice on one immutable pin, then compare:

```sh
dunecity-queue submit --cwd "$STAGING" --rev "$COMMIT" --tag 807-deck-off -- \
  python3 tests/ai/ai-baseline-league.py --style NAME-off --results /abs/results/off
dunecity-queue submit --cwd "$STAGING" --rev "$COMMIT" --tag 807-deck-on -- \
  python3 tests/ai/ai-baseline-league.py --style NAME-on --results /abs/results/on
python3 tests/ai/ai-baseline-gate.py --candidate /abs/results/on --off /abs/results/off \
  --reference ~/Documents/projects/outputs/baseline-league-20261106/releases/1.0.807-episodes
```

Submit through `agent-workspace run --project dunecity --owner "$OWNER" --tests
--wait -- ...` and wait with `dunecity-queue wait JOB` outside that lock. The
check prints `PASS, decided games: candidate X / OFF Y / 807 Z (of 192;
not head-to-head strength)` (exit 0) or `FAIL, ...` (exit 1). Arms that did not play
the exact reference deck (game ids, maps, options, seeds), failed games, candidate and OFF from two
source commits, or a reference that is not native 1.0.807 are invalid input
(`INVALID: ...`, exit 2), never a pass.

The canonical normalized 1.0.807 export is an alternative primary reference when
the original raw results are unavailable; it is not the retired statistical gate:

```sh
python3 tests/ai/ai-baseline-gate.py --candidate /abs/results/on --off /abs/results/off \
  --reference-observations \
  ~/Documents/projects/outputs/baseline-league-20261106/1.0.807-observations.csv.gz
```

`--reference` and `--reference-observations` are mutually exclusive. The latter
accepts plain or gzip canonical scorecard CSV. It requires the exact 192
research-OFF contexts on the eight seeds above (not the manifest's extra Vanilla
seed block), their map/options/context hashes, native source `abdf71e7`, test bridge
`863d1ac6`, and every expected house/team's measured binary native-win summary.
Malformed schema, duplicate/conflicting records, partial rosters, multiple winning
teams, or mismatched provenance are invalid input. The canonical extractor validates
complete, error-free native games before exporting; its `win` metric is exactly
`winner_team != 0 && house.team == winner_team`. Count each winning game once.
No raw `outcome` or `error` columns are invented. Candidate/OFF raw validation,
the candidate-at-least-OFF rule, and the informational historical win count are
unchanged; normalized metrics do not restore the old historical statistical gate.


## CPU cost of unit and combat candidates

A candidate that changes how units fight or move (targeting, focus fire,
counters, combat genes, raids) also needs a CPU check before it is turned on
(Stefan, 2026-11-09): **on the stress game, the candidate must not raise the
p95 QuantBot or simulation CPU per tick by more than 10% over native, and no
tick, frame or house update may exceed a perf_regression budget that native
keeps.** The stress game is `tests/ai/simulation-throughput.py --cases stress256`
(docs/game-performance.md "Stress scenario"): Vanilla six-house FFA on
"6P - 256x256 - Alkozeltser 4", every house QuantBot Brutal playing the style,
no unit cap, 50 game minutes, seeds 1101-6606. Play both arms on one pin and
compare:

```sh
dunecity-queue submit --cwd "$STAGING" --rev "$COMMIT" --tag stress-native --machines aws-mel2 -- \
  python3 tests/ai/simulation-throughput.py --cases stress256 --results /abs/results/native
dunecity-queue submit --cwd "$STAGING" --rev "$COMMIT" --tag stress-NAME --machines aws-mel2 -- \
  python3 tests/ai/simulation-throughput.py --cases stress256 --style NAME-on --results /abs/results/candidate
python3 tests/ai/simulation-throughput.py --perf-gate /abs/results/native /abs/results/candidate
```

Both arms run on one machine (aws-mel2 has 96 slots; a game takes about 7
minutes there), so thread CPU in ms compares directly: a seed's ratio is
candidate over native and the check takes the geometric mean of the six. The
budgets are multiples of the median calibration loop of all twelve games (one
game's start-up calibration read twice its usual time on a loaded host), on the
seeds' geometric mean; where native already exceeds a budget (frame p99 is at
5.4-5.5 on this game) the candidate may not exceed native by more than 10%. It
prints one line, `PASS, QuantBot CPU p95/tick x1.02, sim CPU p95/tick x1.01
(...)` (exit 0) or `FAIL, ...` (exit 1); arms from two source commits or
hosts, other seeds or settings, or failed games are `INVALID: ...` (exit 2),
never a pass. A switch that is native in six-house games (a sides scope) is
checked with its unscoped level, which runs the same code in more fights.

| Date | Candidate (style) | Source | Verdict |
|---|---|---|---|
| 2026-11-09 | SweepLane genome w004-3ae12812 (`genome-w004-3ae12812`) | `4bdaf108` | PASS, QuantBot x0.942, sim x0.935 |
| 2026-11-09 | SweepLane genome w007-981f7c19 (`genome-w007-981f7c19`) | `4bdaf108` | PASS, QuantBot x1.018, sim x0.965 |
| 2026-11-09 | Focus fire level 2 (`focus-fire-2`) | `4bdaf108` | PASS, QuantBot x1.047, sim x1.010 |
| 2026-11-09 | Vanilla army counters (`vanilla-army-counters-100`) | `4bdaf108` | PASS, QuantBot x1.042, sim x1.035 |
| 2026-11-09 | Null arm, native in FFA6 (`focus-fire-2-sides-4`) | `4bdaf108` | PASS, x0.996 / x0.993; 6/6 games digest-identical to native (noise floor) |
| 2026-11-09 | counters50-S5 (`counters50-sides-5`, round 119 default) | `8617247d` | PASS, QuantBot x0.999, sim x0.994; FFA6 no-leak check, 6/6 signatures native |

## Reference and results

1.0.807 is native source `abdf71e7` built through the test-only bridge
`863d1ac6`, queue job `20261106T210623-baseline-league-807-episodes-d00a75`:
all 192 games, 22 ended with any winner. Results:
`~/Documents/projects/outputs/baseline-league-20261106/releases/1.0.807-episodes/games.csv`.
The bridge builds the old engine and plays 1.0.807 against itself on these
maps; two builds cannot share one game, so the comparison is per build on the
same games.

| Date | Candidate | Source | Verdict |
|---|---|---|---|
| 2026-11-09 | ResearchOrder L2 (`CityResearchOrder=2`) | B2 `f9fd39b0` | PASS, candidate 53 / OFF 53 / 807 22 |
| 2026-11-09 | DuelOpening City raid (`DuelOpeningPlan=2`) | B2 `f9fd39b0` | PASS, candidate 56 / OFF 53 / 807 22 |
| 2026-11-09 | CampaignLeftovers margin (`CampaignHomeFields=2`) | B2 `f9fd39b0` | PASS, candidate 53 / OFF 53 / 807 22 |
| 2026-11-09 | City capital pacing L1 (`CityCapitalPacing=1`) | B3m `7e188318` | PASS, candidate 56 / OFF 56 / 807 22 |
| 2026-11-09 | counters50-S5 (`VanillaArmyCounters=1`, blend 5000, `VanillaArmyCounterMapScope=5`) | release-line `8617247d` | PASS, candidate 56 / OFF 56 / 807 22; 192/192 outcomes equal |
| 2026-11-09 | City capital pacing L2 (`CityCapitalPacing=2`, round 120 default) | release-line `a1d85ffa` | PASS, candidate 56 / OFF (default L1) 56 / 807 22; research-off deck, inert |
