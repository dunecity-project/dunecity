#!/usr/bin/env python3
"""Acceptance test of the Fremen Palace worm in real games (match rule Fremen Palace Worm).

Stefan, 2026-11-27: "i want freeman house to get a worm as its palace special - it has full control
over the worm and can eat enemy units with it via hunt, or can explode to create spice where the ai
wants near its base", and the AI behaviour tweaked to use it.

Plays QuantBot Brutal free-for-alls through tests/ai/vanilla-match.py, with the rule on and, for one
seed, off, and asserts each acceptance line from the PALACE_WORM lines of the match log (worms.csv).
The map is the game's own Random Skirmish, 4 players, 160x160, DuneCity (rs160-dunecity, Fremen,
Harkonnen, Atreides, Ordos), the set-up of the OFF/ON measurement and of quantbot_ffa_army_deploys:
there the Fremen house starts with a Palace and its first charge is due at 5 minutes. On the bundled
4P Deserted map the Fremen QuantBot never builds a Palace before it is wiped out (18 minutes, seed
7301), so no worm is ever called there; the player's path on Deserted is asserted by the probe.

  A1 the Fremen Palace calls a worm the Fremen house owns, in every game with the rule on;
  A2 the rule off calls none (the Palace calls the Fremen warriors);
  A3 the worm hunts: QuantBot orders hunts and the hunting worms eat enemy units;
  A4 the worm bursts into spice where the AI chose near its base: bursts made a spice field,
     near a Fremen refinery;
  A5 the AI's orders reach their effect: most hunts end in a meal, most bloom orders in a burst;
  A6 no other controller orders the worm about.

Each line prints ACCEPT <id> PASS|FAIL with its measured value; any FAIL exits non-zero. The human
player's control (Palace button, select, Hunt, Attack, Spice Bloom) is asserted through the real
UI in tests/units/run-palace-worm-probe.py.
"""
import argparse
import csv
import shutil
import statistics
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--map', default='rs160-dunecity')
parser.add_argument('--seeds', nargs='+', type=int, default=[9101, 9102, 9103])
parser.add_argument('--minutes', type=int, default=45)
parser.add_argument('--jobs', type=int, default=3)
parser.add_argument('--reuse', action='store_true', help='Only check the tables already in --output-dir')
args = parser.parse_args()
out = args.output_dir.resolve()
FREMEN = '3'
HOUSES = ['fremen', 'harkonnen', 'atreides', 'ordos']


def play(tag, options, seeds):
    command = [sys.executable, str(root / 'tests/ai/vanilla-match.py'), '--build-dir', args.build_dir,
               '--results', str(out / tag), '--tag', tag, '--difficulty', 'brutal', '--minutes', str(args.minutes),
               '--maps', args.map, '--ffa', 'default', '--houses', *HOUSES,
               '--game-options', str(root / 'tests/ai/game-options' / options),
               '--seeds', *[str(s) for s in seeds], '--jobs', str(args.jobs)]
    print('+ ' + ' '.join(command), flush=True)
    subprocess.run(command, check=True, cwd=root)


def table(tag, name):
    path = out / tag / name
    return list(csv.DictReader(path.open(newline=''))) if path.exists() else []


if not args.reuse:
    # vanilla-match.py resumes from the games.csv it finds; a rerun must not count another build's games.
    for tag in ('worm-on', 'worm-off'):
        shutil.rmtree(out / tag, ignore_errors=True)
    play('worm-on', 'fremen-worm-on.ini', args.seeds)
    play('worm-off', 'fremen-worm-off.ini', args.seeds[:1])

results = []


def accept(line, ok, value):
    results.append(ok)
    print(f'ACCEPT {line} {"PASS" if ok else "FAIL"}: {value}', flush=True)


games_on = [g for g in table('worm-on', 'games.csv') if not g.get('error')]
games_off = [g for g in table('worm-off', 'games.csv') if not g.get('error')]
if len(games_on) != len(args.seeds) or len(games_off) != 1:
    print(f'ACCEPT setup FAIL: {len(games_on)} rule-on and {len(games_off)} rule-off games finished '
          f'(wanted {len(args.seeds)} and 1)')
    sys.exit(1)

per_game = defaultdict(lambda: defaultdict(int))
worms = defaultdict(list)
spice = []
for row in table('worm-on', 'worms.csv'):
    game = row['seed']
    per_game[game][(row['house'], row['event'])] += 1
    if row['house'] == FREMEN:
        worms[(game, row['object'])].append(row['event'])
        if row['event'] == 'spice':
            spice.append((int(row['spice_tiles'] or 0), int(row['refinery_dist'] or -1)))
seeds = [g['seed'] for g in games_on]

summons = [per_game[s][(FREMEN, 'summoned')] for s in seeds]
others = sum(n for s in seeds for (house, event), n in per_game[s].items() if event == 'summoned' and house != FREMEN)
accept('A1 Fremen Palace calls a Fremen worm in every game', min(summons) >= 1 and others == 0,
       f'worms per game {summons}, other houses {others}')

off = sum(1 for row in table('worm-off', 'worms.csv') if row['event'] == 'summoned')
accept('A2 rule off calls no worm', off == 0, f'{off} worms with the rule off')

hunts = sum(per_game[s][(FREMEN, 'hunt')] for s in seeds)
meals = sum(per_game[s][(FREMEN, 'ate')] for s in seeds)
eaten_games = sum(per_game[s][(FREMEN, 'ate')] > 0 for s in seeds)
accept('A3 the worm hunts and eats enemy units', hunts >= 1 and meals >= 3 and eaten_games * 2 >= len(seeds),
       f'{hunts} hunt orders, {meals} enemy units eaten, meals in {eaten_games}/{len(seeds)} games')

near = [d for tiles, d in spice if tiles >= 30 and 0 <= d <= 18]
accept('A4 bursts make spice fields near a Fremen refinery', len(spice) >= 1 and len(near) * 2 >= len(spice),
       f'{len(spice)} bursts, {len(near)} with >= 30 spice tiles within 18 tiles of a refinery'
       + (f', median distance {statistics.median(d for _, d in spice)}' if spice else ''))

hunted = [seq for seq in worms.values() if 'hunt' in seq]
hunted_ate = sum(any(e == 'ate' for e in seq[seq.index('hunt'):]) for seq in hunted)
ordered = [seq for seq in worms.values() if 'bloom_order' in seq]
burst = sum('bloom' in seq for seq in ordered)
accept('A5 the AI orders reach their effect',
       hunted and hunted_ate * 2 >= len(hunted) and ordered and burst * 2 >= len(ordered),
       f'{hunted_ate}/{len(hunted)} hunting worms ate, {burst}/{len(ordered)} bloom orders burst')

redirected = sum(per_game[s][(FREMEN, 'redirected')] for s in seeds)
cancelled = sum(per_game[s][(FREMEN, 'bloom_cancelled')] for s in seeds)
accept('A6 no other controller orders the worm', redirected == 0, f'{redirected} redirections, {cancelled} bloom trips cancelled')

sys.exit(0 if all(results) else 1)
