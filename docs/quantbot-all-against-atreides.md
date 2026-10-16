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
`army_recall`). Each match takes 40-280 s of wall time, depending on host load. Useful options:
`--harvester-limit -1` (the other common lobby setting), `--human-seat` (releases before 1.0.820
have no spectator mode), `--report-only` with a large `--win-minutes` (version comparisons).

Building blocks in `run-campaign-balance.py`:

- `--elimination-match` (`tests/ai/elimination-match.inc`): play until one team is left, with
  per-minute house samples.
- `--game-options FILE`.
- `--load-save FILE --save-profile DIR`: continue a player's save. `DIR/workshop` supplies the
  mod and map revisions the save pins.

## Measured (game minutes to eliminate all four; L = Atreides lost)

| seed | 1.0.796 (human seat) | 1.0.820 | 1.0.833 | 1.0.842 | 1.0.842 recovery off (ablation) | branch |
|---|---|---|---|---|---|---|
| 1756374161 | 35.1 | 31.4 | 42.4 | 42.4 | 28.7 | 36.5 |
| 906213928 | 36.9 | 66.7 | 32.4 | 32.4 | 31.1 | 35.3 |
| 41562270 | 36.7 | 35.8 | 43.0 | 43.0 | 32.3 | 34.4 |
| 1841712204 | 32.8 | 69.5 | – | 32.6 | 31.5 | 31.7 |
| 1430976924 | 32.1 | 37.7 | – | L 29.9 | 30.2 | L 39.0 |
| 1804669015 | 35.6 | 37.0 | – | 41.6 | 33.9 | 32.8 |

1.0.833 and 1.0.842 play identical games (no 833→842 regression). With an idle human seat,
1.0.842 loses one seed and needs 71.2 minutes on another. The regression arrived with army
recovery (`d774f387`, 1.0.812): turning recovery off restores 6/6 wins in 29-34 minutes.
On the branch, the front battery is limited to its 8-12 band, walls follow only the fixed-count
turrets, and destroyed buildings count as kills in the posture trade. Five seeds now win in 32-37
minutes. Seed 1430976924 still loses: a `severe_local_defeat` withdrawal at 10.2 minutes pulls
the first wave home, and the four AI Player houses then snowball. The test therefore fails on the
branch until the posture machine stops withdrawing winning-position Brutal armies
(`HANDOVER.md`, 2026-10-16).

## CI

Not in the CI Linux job. The harness relinks the macOS app bundle
(`bin/dunecity.app/Contents/MacOS/dunecity`) through Ninja, and CI runs a sanitized Debug tree,
where a 40-minute six-game match would take far longer than the job allows. Run it locally
through ctest.
