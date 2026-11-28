#!/usr/bin/env python3
"""Report what Troopers did against Ornithopters in AI matches, Trooper Ornithopter Reach off against on.

Reads the tables dunecity-audit.py writes with --attack-events (games.csv, houses.csv,
combat.csv, plain or .gz) for two arms played on the same maps, seeds and sides, one with
tests/ai/game-options/trooper-ornithopter-reach-off.ini and one with ...-on.ini (or the
-unexplored- pair):

  dunecity-queue submit ... -- python3 tests/ai/dunecity-audit.py --mode matrix --attack-events \\
      --game-options tests/ai/game-options/trooper-ornithopter-reach-off.ini --variant reach-off --results OFF ...
  dunecity-queue submit ... -- python3 tests/ai/dunecity-audit.py --mode matrix --attack-events \\
      --game-options tests/ai/game-options/trooper-ornithopter-reach-on.ini --variant reach-on --results ON ...
  python3 tests/ai/trooper-reach-report.py --off OFF --on ON

Per arm and per game: Ornithopters killed by Troopers (the lethal hit's natural source was a
Trooper), hit points Troopers removed from Ornithopters, Troopers killed by Ornithopters,
Ornithopters and Troopers built, and the time-out rate. The paired difference on - off is per
game (same map, seed and sides) with a 95% bootstrap interval (tests/ai/match_score.py). A
match diverges from its pair after the first changed shot, so pairing removes the map and the
start, not the rest of the game. Every house in a match plays the same arm, so there is no
candidate house and no placement score to compare; pairs with the same final digest are
counted, because there the rule changed nothing at all.
"""
import argparse
import csv
import gzip
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_score  # noqa: E402

PAIR = ('format', 'seed', 'size', 'spice_level', 'difficulty', 'base', 'army', 'city', 'limit', 'initial_tech')
TROOPER = '44'
ORNITHOPTERS = ('35', '80')
MEASURES = (('trooper_kills', 'Ornithopters killed by Troopers'),
            ('trooper_damage', 'Hit points Troopers removed from Ornithopters'),
            ('trooper_hits', 'Trooper rockets that hit an Ornithopter'),
            ('troopers_lost', 'Troopers killed by Ornithopters'),
            ('ornithopters_lost', 'Ornithopters lost to anything'),
            ('ornithopters_built', 'Ornithopters built'),
            ('troopers_built', 'Troopers built'))

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--off', type=Path, nargs='+', required=True, help='Result directories of the reach-off arm')
parser.add_argument('--on', type=Path, nargs='+', required=True, help='Result directories of the reach-on arm')
parser.add_argument('--title', default='Trooper Ornithopter Reach in AI matches')
parser.add_argument('--output', type=Path, help='Also write the report here')
args = parser.parse_args()


def read(folders, name):
    rows = []
    for folder in folders:
        for path, opener in ((folder / name, open), (folder / (name + '.gz'), gzip.open)):
            if path.exists():
                with opener(path, 'rt', newline='') as handle:
                    rows.extend(csv.DictReader(handle))
                break
    return rows


def arm(folders):
    """{pair key: measures of one finished game}"""
    games = {}
    for row in read(folders, 'games.csv'):
        if row.get('error') or row.get('skipped') not in (None, '', '0'):
            continue
        games[row['id']] = dict(key=tuple(row[field] for field in PAIR) + (row.get('sides', ''),),
                                timeout=row['outcome'] == 'time_limit', digest=row['digest'],
                                minutes=float(row['minutes'] or 0), telemetry=row.get('attack_telemetry') == '1',
                                **{name: 0 for name, _ in MEASURES})
    for row in read(folders, 'houses.csv'):
        game = games.get(row['id'])
        if game is None:
            continue
        game['ornithopters_built'] += int(row['ornithopter_built'] or 0) + int(row['elite_ornithopter_built'] or 0)
        game['ornithopters_lost'] += int(row['ornithopter_lost'] or 0) + int(row['elite_ornithopter_lost'] or 0)
        game['troopers_built'] += int(row['trooper_built'] or 0)
    for row in read(folders, 'combat.csv'):
        game = games.get(row['id'])
        if game is None or row['target_unit'] != '1':
            continue
        if row['attacker_item'] == TROOPER and row['target_item'] in ORNITHOPTERS:
            game['trooper_kills'] += int(row['kills'])
            game['trooper_hits'] += int(row['hits'])
            game['trooper_damage'] += int(row['hp_removed_milli']) / 1000
        elif row['attacker_item'] in ORNITHOPTERS and row['target_item'] == TROOPER:
            game['troopers_lost'] += int(row['kills'])
    result = {}
    for game in games.values():
        if not game['telemetry']:
            raise SystemExit('A game has no attack telemetry; play the arms with --attack-events')
        if game['key'] in result:
            raise SystemExit('Two games of one arm share a setup: ' + repr(game['key']))
        result[game['key']] = game
    return result


off, on = arm(args.off), arm(args.on)
keys = sorted(set(off) & set(on))
if not keys:
    raise SystemExit('No setup was played in both arms')
lines = ['# ' + args.title, '',
         'Paired games (same map, seed and sides): %d. Only in the off arm: %d; only in the on arm: %d.'
         % (len(keys), len(set(off) - set(on)), len(set(on) - set(off))),
         'Formats: ' + ', '.join('%s x%d' % (name, sum(1 for key in keys if key[0] == name))
                                 for name in sorted({key[0] for key in keys})) + '.', '',
         '| Measure, per game | Reach off | Reach on | Paired difference (on - off) | 95% interval |',
         '|---|---:|---:|---:|---|']
for name, label in MEASURES:
    a = [off[key][name] for key in keys]
    b = [on[key][name] for key in keys]
    differences = [y - x for x, y in zip(a, b)]
    low, high = match_score.bootstrap([float(value) for value in differences], 20261122)
    lines.append('| %s | %.2f | %.2f | %+.2f | %+.2f to %+.2f |' % (
        label, statistics.mean(a), statistics.mean(b), statistics.mean(differences), low, high))
lines.append('')


def share(numerator, denominator, games):
    top = sum(game[numerator] for game in games)
    bottom = sum(game[denominator] for game in games)
    return '%d of %d (%.1f%%)' % (top, bottom, 100 * top / bottom) if bottom else '%d of 0' % top


for label, games in (('off', [off[key] for key in keys]), ('on', [on[key] for key in keys])):
    lines.append('Reach %s: Troopers killed %s Ornithopters lost; Ornithopters killed %s Troopers built; '
                 'time-outs %d of %d games (%.0f%%); mean length %.1f game minutes.' % (
                     label, share('trooper_kills', 'ornithopters_lost', games),
                     share('troopers_lost', 'troopers_built', games),
                     sum(game['timeout'] for game in games), len(games),
                     100 * sum(game['timeout'] for game in games) / len(games),
                     statistics.mean(game['minutes'] for game in games)))
same = sum(1 for key in keys if off[key]['digest'] == on[key]['digest'])
exposed = [key for key in keys if off[key]['ornithopters_built'] and off[key]['troopers_built']]
lines += ['', 'Pairs that ended in the same state (same final digest: the rule changed nothing): %d of %d.'
          % (same, len(keys)),
          'Pairs in which the off arm built both Ornithopters and Troopers: %d of %d.' % (len(exposed), len(keys)),
          'No placement score: every house in a match plays the same arm.', '']
text = '\n'.join(lines)
print(text)
if args.output:
    args.output.write_text(text + '\n', encoding='utf-8')
