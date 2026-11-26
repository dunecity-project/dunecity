#!/usr/bin/env python3
"""Report the Fremen Palace worm in AI matches: the old Fremen warriors (control) against the worm.

Reads the tables vanilla-match.py writes (games.csv, houses.csv, worms.csv) for a control tag played
with tests/ai/game-options/fremen-worm-off.ini and one or more candidate tags played on the same maps,
seeds and sides with fremen-worm-on.ini (each perhaps with its own PalaceWorm play style):

  python3 tests/ai/vanilla-match.py --tag worm-off --game-options tests/ai/game-options/fremen-worm-off.ini \\
      --maps rs160-dunecity --ffa default --houses fremen harkonnen atreides ordos ...
  python3 tests/ai/vanilla-match.py --tag worm-on  --game-options tests/ai/game-options/fremen-worm-on.ini ...
  python3 tests/ai/palace-worm-report.py --results DIR --control worm-off --candidates worm-on worm-on-0 worm-on-2

Per arm: worms summoned, meals (and the credit value eaten), blooms, how the worms ended (bloom,
expired, sated, beaten), the time-out rate and the Fremen house's placement score
(tests/ai/match_score.py). The paired difference candidate - control is per game (same map, seed
and sides) with a 95% bootstrap interval: the candidate is a better Fremen special only when the
whole interval of the Fremen placement difference is above 0 (the default rule). A match diverges
from its pair at the first Palace charge, so pairing removes the start position and map only.
"""
import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_score  # noqa: E402

PAIR = ('difficulty', 'map', 'seed', 'sides')
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
FREMEN = HOUSE_NUMBERS['fremen']
ENDINGS = ('bloom', 'expired', 'sated', 'beaten')

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', type=Path, nargs='+', required=True)
parser.add_argument('--control', default='worm-off', help='Tag of the games played with the rule off (warriors)')
parser.add_argument('--candidates', nargs='+', default=['worm-on'], help='Tags played with the rule on')
parser.add_argument('--output', type=Path, help='Also write the report here')
args = parser.parse_args()


def read(name):
    rows = []
    for folder in args.results:
        path = folder / name
        if path.exists():
            with path.open(newline='') as handle:
                rows.extend(csv.DictReader(handle))
    return rows


def pair_of(row):
    return tuple(row[k] for k in PAIR)


tags = [args.control] + args.candidates
games = {(row['tag'], pair_of(row)): row for row in read('games.csv') if row['tag'] in tags}
houses = defaultdict(list)
for row in read('houses.csv'):
    houses[(row['tag'], pair_of(row))].append(row)
events = defaultdict(list)
for row in read('worms.csv'):
    events[(row['tag'], pair_of(row))].append(row)


def ok(tag, pair):
    return (tag, pair) in games and not games[(tag, pair)].get('error')


def game_stats(tag, pair):
    game = games[(tag, pair)]
    rows = [row for row in events[(tag, pair)] if row['house'] == str(FREMEN)]
    timeout = game['winner'] in ('', '-1')
    rows_h = houses[(tag, pair)]
    eliminated = {HOUSE_NUMBERS[r['house']]: float(r['eliminated_s']) for r in rows_h if r['eliminated_s'] != ''}
    if not timeout:
        for r in rows_h:
            if HOUSE_NUMBERS[r['house']] != int(game['winner']):
                eliminated.setdefault(HOUSE_NUMBERS[r['house']], float(game['end_s'] or 0))
    placing = match_score.score_game([HOUSE_NUMBERS[r['house']] for r in rows_h], eliminated) if len(rows_h) > 1 else {}
    meals = [row for row in rows if row['event'] == 'ate']
    return dict(summoned=sum(row['event'] == 'summoned' for row in rows), meals=len(meals),
                eaten=sum(float(row['price'] or 0) for row in meals),
                **{end: sum(row['event'] == end for row in rows) for end in ENDINGS},
                timeout=timeout, minutes=float(game['end_s'] or 0) / 60,
                fremen=placing[FREMEN].score if FREMEN in placing else None,
                fremen_won=game['winner'] == str(FREMEN))


lines = []
for candidate in args.candidates:
    pairs = sorted(pair for tag, pair in games if tag == candidate and ok(candidate, pair) and ok(args.control, pair))
    if not pairs:
        lines += [f'{candidate}: no game played in both arms.', '']
        continue
    stats = {tag: {pair: game_stats(tag, pair) for pair in pairs} for tag in (args.control, candidate)}
    lines += [f'Fremen Palace worm: {candidate} against {args.control} (warriors), {len(pairs)} paired games',
              f'maps: {", ".join(sorted({p[1] for p in pairs}))}; difficulty: {", ".join(sorted({p[0] for p in pairs}))}',
              'arm           worms  meals  value eaten  bloom  expired  sated  beaten  Fremen wins  time-outs  '
              'mean minutes  Fremen score']
    for tag in (args.control, candidate):
        arm = list(stats[tag].values())
        scores = [g['fremen'] for g in arm if g['fremen'] is not None]
        lines.append(f'{tag:<12}  {sum(g["summoned"] for g in arm):>5}  {sum(g["meals"] for g in arm):>5}  '
                     f'{sum(g["eaten"] for g in arm):>11.0f}  '
                     + '  '.join(f'{sum(g[e] for g in arm):>{len(e) if len(e) > 5 else 5}}' for e in ENDINGS)
                     + f'  {sum(g["fremen_won"] for g in arm):>11}  {100 * statistics.mean(g["timeout"] for g in arm):>8.1f}%'
                     f'  {statistics.mean(g["minutes"] for g in arm):>12.1f}  '
                     f'{statistics.mean(scores) if scores else float("nan"):>+12.3f}')

    def difference(text, value, digits=3):
        values = [value(stats[candidate][p]) - value(stats[args.control][p]) for p in pairs
                  if value(stats[candidate][p]) is not None and value(stats[args.control][p]) is not None]
        if len(values) < 2:
            lines.append(f'  {text}: not enough games ({len(values)})')
            return
        low, high = match_score.bootstrap(values, 20261127)
        verdict = 'higher' if low > 0 else 'lower' if high < 0 else 'no difference shown'
        lines.append(f'  {text}: {statistics.mean(values):+.{digits}f} ({low:+.{digits}f} to {high:+.{digits}f}), '
                     f'{len(values)} games: {verdict}')

    lines.append(f'Paired difference {candidate} - {args.control} per game (95% bootstrap interval):')
    difference('Fremen placement score', lambda g: g['fremen'])
    difference('Fremen won (1/0)', lambda g: 1.0 if g['fremen_won'] else 0.0)
    difference('time-out (1/0)', lambda g: 1.0 if g['timeout'] else 0.0)
    difference('game minutes', lambda g: g['minutes'], 1)
    lines.append('')

text = '\n'.join(lines)
print(text)
if args.output:
    args.output.write_text(text + '\n')
