# 1.0.807 promotion check

An AI switch that has passed its paired and control games needs one more check
before it is turned on by default (Stefan, 2026-11-09): **on 1.0.807's own test
deck, the candidate build must win at least as many games as the switch-OFF
build of the same source.** 1.0.807's own win count is printed alongside for
information; it does not block. This replaces the earlier historical
statistical gate (independent objectives, 50,000-draw bootstrap), which was
removed because today's AI with every switch OFF already failed it.

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
check prints one line, `PASS, candidate X / OFF Y / 807 Z (wins of 192 games)`
(exit 0) or `FAIL, ...` (exit 1). Arms that did not play exactly the reference
deck (game ids, maps, options, seeds), failed games, candidate and OFF from two
source commits, or a reference that is not native 1.0.807 are invalid input
(`INVALID: ...`, exit 2), never a pass.

## Reference and results

1.0.807 is native source `abdf71e7` built through the test-only bridge
`863d1ac6`, queue job `20261106T210623-baseline-league-807-episodes-d00a75`:
all 192 games, 22 won. Results:
`~/Documents/projects/outputs/baseline-league-20261106/releases/1.0.807-episodes/games.csv`.
The bridge builds the old engine and plays 1.0.807 against itself on these
maps; two builds cannot share one game, so the comparison is per build on the
same games.

| Date | Candidate | Source | Verdict |
|---|---|---|---|
| 2026-11-09 | ResearchOrder L2 (`CityResearchOrder=2`) | B2 `f9fd39b0` | PASS, candidate 53 / OFF 53 / 807 22 |
