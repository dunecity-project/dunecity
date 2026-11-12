# All against Atreides — mandatory AI regression matches

Stefan, 2026-10-16: "add a mandatory test after all ai changes to make sure it can still win all
against atreides on vanilla with this setup ... atreides quantbot brutal should win". His 1.0.842
game on the MBA took 88 game minutes to win (the `quantsucks.dls` save at 47 minutes, continued
headless).

## Setup (exactly his game)

- Bundled `data/maps/singleplayer/5P - 128x128 - All against Atreides.ini`, mod vanilla, tech 8.
  Atreides starts with 100,000 credits and a small base. Ordos, Harkonnen, Sardaukar and Fremen
  start with 500 credits each and large prebuilt bases.
- Atreides: QuantBot Brutal. Others: **AI Player (hard)** (`AIPlayerHard`). The save's roster shows
  this, and so does his telemetry: it has no QuantBot events for the other four houses.
- Local spectator, so no human player. Lobby order and teams are his: Harkonnen, Atreides,
  Ordos, Fremen, Sardaukar. Atreides is alone on team 2.
- Game options: `tests/ai/all-against-atreides-options.ini`, the `[Game Options]` section of his
  `Dune City.ini` (unit override 0, harvester override 0, concrete required, explored map, worms
  respawn and drop spice, rocket turrets without power).

## Pass condition

On every supplied seed, Atreides must eliminate all four houses (`House::isAlive`, the
engine's own defeat rule) within **50 game minutes**. Stefan explicitly set this recovery
limit on 2026-11-07, replacing the former 40-minute gate; a slow win still fails.
The runner and `quantbot_all_against_atreides` CTest use 50 by default and retain all six
fixed default seeds. The approved recovery plan's focused gate uses exactly
**1756374161 and 906213928**, selected with the existing `--seeds` option:

```sh
dunecity-queue submit --cwd /Users/stefan/Documents/projects/dunecity-feature-batch --rev COMMIT --tag aaa-recovery -- \
  python3 tests/ai/scenario-matrix.py --tier full --cells '^aaa/qb-v-4ah/vanilla$' \
  --aaa-seeds 2 --check --build-dir build-int --results RESULTS
```

The queued form reuses the scenario-matrix cell with the same map, roster, options
and 50-minute deadline; its first two fixed seeds are exactly the two above.
The legacy `run-all-against-atreides.py --seeds 1756374161 906213928` selector is
also supported, but that older report-rich driver is not itself a distributed
batch harness (`--output-dir`, not `--results`). Do not submit it directly to
`dunecity-queue submit`; use the category above, or the pinned CTest gate for all six.

Commit the tooling first; the queue uses that immutable revision, not dirty source.
The default game cap is 51 minutes; victory after minute 50 fails. Do not describe a
two-seed recovery run as a completed six-seed CTest gate, drop seeds, or change the
deadline to get a candidate through. Historical measurements below retain their original limits.

The runner (`tests/ai/run-all-against-atreides.py`) builds one probe and plays three matches at
once. For each seed it prints the winner, the game time, Atreides' credits, army value and units,
harvesters and structures every 5 minutes, the enemies' army value, the attack waves
(`ground_hunt`: count and size) and the defence events (`defence_response`, `base_scramble`,
`army_recall`). It uses the headless batch defaults (`docs/headless-performance.md`): one path
thread per match (`--path-workers 1`) and no unit-phase timing. The AI decision log stays on,
because the attack and defence lines are read from it (`--no-ai-telemetry` drops them for
~3% CPU). The whole test takes about 3.5 minutes (205 s on 2026-10-16, 75-94 s per match,
load ~8-13). Useful options: `--harvester-limit -1` (the other common lobby setting),
`--seeds`, `--human-seat` (releases before 1.0.820 have no spectator mode), `--report-only`
with a large `--win-minutes` (version comparisons).

Building blocks in `run-campaign-balance.py`:

- `--elimination-match` (`tests/ai/elimination-match.inc`): play until one team is left, with
  per-minute house samples.
- `--game-options FILE`.
- `--load-save FILE --save-profile DIR`: continue a player's save. `DIR/workshop` supplies the
  mod and map revisions the save pins.

## Cause

The regression arrived with army recovery (`d774f387`, 1.0.812). Three parts of it hurt this
match: (1) the front-battery goal grew with base size, not with the threat, so Atreides' 100,000
credit base ordered 91 rocket turrets; (2) the wall rule wanted 2 walls per turret (95 walls),
together about 30,000 credits diverted during minutes 10-20 while the AI Player houses grow
from 13k to 117k; (3) the posture trade counted only mobile kills, so assaulting turret-defended
bases read as a bad trade, and the house-level withdrawals kept pulling home a Brutal army that
was ahead (one seed lost after a `severe_local_defeat` recall of the first wave at 10.2 min).

## Measured (game minutes to eliminate all four; L = Atreides lost)

| seed | 1.0.796 (human seat) | 1.0.820 | 1.0.833 | 1.0.842 | 1.0.842 recovery off (ablation) | branch, withdrawals on | branch |
|---|---|---|---|---|---|---|---|
| 1756374161 | 35.1 | 31.4 | 42.4 | 42.4 | 28.7 | 36.5 | 34.0 |
| 906213928 | 36.9 | 66.7 | 32.4 | 32.4 | 31.1 | 35.3 | 31.9 |
| 41562270 | 36.7 | 35.8 | 43.0 | 43.0 | 32.3 | 34.4 | 32.2 |
| 1841712204 | 32.8 | 69.5 | – | 32.6 | 31.5 | 31.7 | 31.7 |
| 1430976924 | 32.1 | 37.7 | – | L 29.9 | 30.2 | L 39.0 | 32.8 |
| 1804669015 | 35.6 | 37.0 | – | 41.6 | 33.9 | 32.8 | 32.8 |

1.0.833 and 1.0.842 play identical games (no 833→842 regression). With an idle human seat,
1.0.842 loses one seed and needs 71.2 minutes on another. Turning recovery off in 1.0.842
restores 6/6 wins in 29-34 minutes. Changes on the branch: the front battery is limited to its
8-12 band, walls follow only the fixed-count turrets, destroyed buildings count as kills in the
posture trade, and the house-level withdrawals are off by default (below). "branch, withdrawals
on" is the first three changes alone.

Robustness on the branch: harvester override -1 (unchecked) gives the same six times as 0
(34.0 31.9 32.2 31.7 32.8 32.8; both mean no ceiling). Three further seeds win in 31.7
(271828182), 32.4 (3141592653) and 31.1 (1618033988) minutes. The `perf/headless-sim` merge
(bit-identical engine speed-ups, one path thread) reproduces all six times exactly.

Starting from the first three changes ("branch, withdrawals on"), the house-level withdrawals were
switched off one trigger at a time, each on all six seeds (game minutes per seed, in the table's
order; L = lost):

| withdrawal trigger disabled | per seed |
|---|---|
| severe local defeat | 36.5 35.3 34.4 31.7 L35.1 32.8 |
| local squad pull-back | 36.5 35.3 34.4 31.7 L39.0 32.8 |
| sustained attrition | 34.0 31.9 32.2 31.7 L45.4 32.8 |
| core under attack | 36.5 35.3 34.4 31.7 L36.0 32.8 |
| outnumbered dispatch gate | identical to the branch (never fired) |
| all four withdrawal triggers | 34.0 31.9 32.2 31.7 32.8 32.8 |

No single trigger is the cause. Together they cost a lost game and 2-5 minutes per win. The
original design (`d774f387`) argued for them from fixtures only ("fixture success does not
establish improved win rate"). Withdrawals are therefore off by default
(`[Army Recovery] WithdrawalsEnabled`, default false). Rally, batteries, the recall machinery and
the whole-army base defence are unchanged, and `quantbot_army_recovery` still verifies the
withdrawal machine with the setting on.

### Withdrawals and `quantbot_no_spice_income_low-cash`

That test (zero-spice `4P - 64x64 - 3 vs 1`, four Brutal QuantBots, seed 148680876, 12 minutes)
failed once withdrawals were switched off: Atreides (house 1) built 3 R/I lots instead of 4. It
is not a withdrawal effect. With and without withdrawals the first two minutes are identical:
Atreides' starting army walks into its Harkonnen neighbour's starting units and loses all seven
units for two, and Harkonnen then takes its yard (2.9 minutes without withdrawals, 3.7 with);
Atreides is dead at the end of both games. The extra 0.8 minutes let it place a third
residential lot, exactly the 4th lot the assertion counted. The 1.0.817 design
(`docs/no-spice-economy-817.md`) already recorded "one defeated house builds only three lots
before losing its base" for this scenario and required four lots of surviving cities only; the
probe asserted it of every house. `run-no-spice-city-progress.py` now requires the four-lot
R/I opening of every house alive at the end, still requires more than 100 banked tax credits
of every house with a starting yard, reports defeated houses (`defeated` in
`income-receipt.json`), and fails if no surviving city was checked. Withdrawals stay off.

## Saved Dune2R setup (2026-11-04)

`worseaiallagainstatreides.dls` is a different content/rules combination from the
original vanilla gate: **Dune2R, city mode disabled**, Atreides QuantBot Brutal
against four AI Player Hard houses, observed locally. Its map bytes match the
bundled map. The save is at cycle 111,308 (29.68 minutes), with seed 1702260251.
Its rules have fog and powered rocket turrets enabled, unlike the original
fixture. `tests/ai/all-against-atreides-dune2r-options.ini` records those saved
rules explicitly, including rules that are inert outside city mode.

The scenario matrix's `aaa/qb-v-4ah-saved/Dune2R` cell uses the original six
fixed seeds plus this saved seed. `--economy-samples` retains the existing
read-only economy diagnostics, `--attack-events` retains attack/defence events,
and every scenario now keeps per-minute credits, army, harvesters and structure
samples before deleting its disposable profile. `games.csv` records the source
commit and simulation-source fingerprint. `--check` applies the matrix's
per-game expectations to selected full-tier cells as well as smoke runs.

Single-key `brutal-aaa-*-off` play styles isolate new switch defaults in that
saved setup. Existing audit/capital off styles cover StorageCapacity,
CityPowerSizing, CityOpeningCapital, CityConstructionCapital and
CityProductionPrerequisites. The default-off Civ Wars and MoreRocketTurrets
experiments are not enabled in the control.

### Powered non-city executor

The real saved position has 99 Harvesters but only 35,110 credits of live combat
units against 120,710 enemy combat value. Six of seven fully upgraded Heavy
Factories are idle; the seventh is building a Harvester. A normal 60-second
continuation earns 8,392 spice credits but builds no heavy combat units while
losing three yards, two heavy factories and ten refineries.

Previously, powered non-city Dune2R did not select the funded spice-economy
policy, despite having no city taxes. Its executor still required more than
2,000 **remaining planning credits** after shared planning reserved queues and
other purchases. The planner selected an affordable active-defence Launcher in
15 of 37 passes, but that separate floor prevented every heavy-allocation
decision. Earlier classic fallback branches bought Harvesters below the AI cap
even though the planned worker target was already exceeded.

The first, Heavy-Factory-only experiment fixed the observed executor mismatch
but did not restore strength: both arms won 0/7 saved-rules games within 60
minutes. Fixing only the cash floor and surplus-worker fallback was insufficient.

The hashed, default-on `FundedClassicProduction` selects the existing funded
spice policy once for the whole build pass: worker/refinery investment, factory
and MCV capacity, and production use the same plan. Its scope is Custom Brutal,
non-campaign, non-city games. Engine power requirements, queue commitments and
the existing unit-mix learning remain unchanged. The switch does not change
city, campaign or already vanilla-funded policies.

### Matched acceptance and the second CTest

Both arms below use frozen source `6aa02856` and the exact saved-rule fixture.
`T60` is a time-out, not a win; `L` means Atreides was eliminated.

| Seed | Current policy OFF | Funded policy ON |
|---|---:|---:|
| 1756374161 | T60 | 29.79 |
| 906213928 | L33.08 | 35.29 |
| 41562270 | T60 | 31.16 |
| 1841712204 | L42.51 | 31.43 |
| 1430976924 | L30.96 | 33.99 |
| 1804669015 | T60 | 34.04 |
| 1702260251 | L42.35 | 33.15 |

The native v1.0.864 baseline (`4fb0469e`) also fails this setup: 0 wins,
6 losses and 1 time-out in seven games. The existing vanilla gate did not cover
this rules/content combination. No matched historical last-good commit was
established; older-source bisection was stopped after the same-build restoration.

At the minute-20 checkpoint, all seven games have samples: mean Atreides combat
value rises from 43,691 to 122,016, while mean unspent credits fall from 21,574 to
5,100. Mean opposing combat value falls from 103,250 to 34,121. Nonempty
`ground_hunt` dispatches rise from 2.58 to 3.41 per ten alive game minutes; mean
first dispatch stays 4.656 minutes. This restores production and tempo, not an
earlier first-wave timer.

The exact saved position was also advanced by 3,750 relative cycles (60 seconds)
in each arm. ON completes one Launcher versus none, places no new Harvester
factory order versus one, and retains six yards versus three. Final combat
value is 27,750 versus 26,000. This is not an eventual saved-game victory claim,
nor a claim that more total combat units were completed during that minute.
Power requirements remain active: sampled deficit observations fall from 6/37
to 1/37; these are observations, not measured blackout durations.

The same-binary city preservation audit has 16 games per arm and 48 paired
Hard/Brutal houses: all state digests, scores, armies, dispatches, detector
results and storage losses are identical. Map, options, content, object data,
binary and source fingerprints match in every pair.

`quantbot_all_against_atreides_dune2r` now requires **all seven wins within
40 game minutes**, about 4.71 minutes (13%) above the measured worst. It uses
three games concurrently and a 600-second wall timeout. It exercises defaults,
not a forced ON style, and retains army/economy/attack diagnostics. The original
six-seed vanilla test and its 40-minute limit are unchanged.

Evidence is under `../outputs/worse-all-v-atreides-20261104/`:
`funded-policy-{off,on}` (games, samples, dispatches),
`funded-policy-save-comparison.json`, `funded-policy-save-provenance.json`,
and `full-policy-city-comparison.txt`. Frozen binary SHA256:
`9cf0a5d3dfe5648b844f4a19e79a639ac25892c993901675f2d6133f4b6b820c`.
The Dune2R shared-spending probe checks actual low-cash queues, surplus workers,
unaffordable purchases and unchanged Easy/Medium/Hard execution.

The fresh default-policy CTest on combined source `35c3a7ca` passed all seven
seeds in **128.8 seconds wall**, with the same worst win of 35.29 game minutes.
The canonical eight AI/performance regressions also passed on that source.
Spectator matches are reported by the winning team: their raw engine `outcome`
can be `lost` for the detached local seat even when team 2 wins. Acceptance uses
the actual `winner_team` and an explicit 40-minute expectation, not that local
seat's result.

## CI

Not in the CI Linux job. The harness relinks the macOS app bundle
(`bin/dunecity.app/Contents/MacOS/dunecity`) through Ninja, and CI runs a sanitized Debug tree,
where the 40-minute match sets would take far longer than the job allows. Run them locally
through ctest.
