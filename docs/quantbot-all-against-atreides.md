# quantbot_all_against_atreides — mandatory AI regression match

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

On every one of six fixed seeds (his match seed 1756374161 plus five more), Atreides must
eliminate all four houses (`House::isAlive`, the engine's own defeat rule) within **40 game
minutes**. A slow win counts as a failure, because a slow win is what he reported. A single seed
is chaotic: his own game and a headless replay of its seed match cycle for cycle for 12 minutes,
then end 46 minutes apart. That is why six seeds are needed. Do not raise the limit or drop a
seed to get a change through.

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

## CI

Not in the CI Linux job. The harness relinks the macOS app bundle
(`bin/dunecity.app/Contents/MacOS/dunecity`) through Ninja, and CI runs a sanitized Debug tree,
where a 40-minute six-game match would take far longer than the job allows. Run it locally
through ctest.
