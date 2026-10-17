#!/usr/bin/env python3
"""Play-style tables (docs/ai-play-styles.md) from formation-duel.py and vanilla-match.py results.

Every 1v1 game gives each label a score: 1 win, 0 loss; a time-out goes to the side 1,000+ credits
ahead on killed unit value (--adjudicate), else 0.5 - the rule of both harnesses' reports. 95%
intervals are bootstrap (2,000 resamples of games, fixed seed).

  play-style-matrix.py --duel D1 D2 --vanilla V1 V2 --screen default
      every label against the base label: Fortress and CY score, interval, W-L / time-outs
  play-style-matrix.py --vanilla V1 --matrix default rush boom ...
      the pairwise matrix (row's score against the column) and each row's mean
  play-style-matrix.py --vanilla V1 --breakdown default --by map|house|difficulty
      each label against the base per map, house or difficulty
Labels may be given without the default@ prefix (rush = default@rush). --tags and --difficulty
filter rows.
"""
import argparse
import csv
import random
import statistics
from collections import defaultdict
from pathlib import Path

HOUSE_NUMBERS = {'harkonnen': '0', 'atreides': '1', 'ordos': '2', 'fremen': '3', 'sardaukar': '4', 'mercenary': '5'}


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def number(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def bootstrap(scores, rounds=2000):
    if not scores:
        return (0.0, 0.0)
    rng = random.Random(20261018)
    means = sorted(statistics.mean(rng.choices(scores, k=len(scores))) for _ in range(rounds))
    return means[int(0.025 * rounds)], means[int(0.975 * rounds) - 1]


def adjudicate(winner_is_me, winner_is_them, kill_me, kill_them, margin):
    if winner_is_me:
        return 1.0, 1.0
    if winner_is_them:
        return 0.0, 0.0
    trade = kill_me - kill_them
    return (1.0 if trade >= margin else 0.0 if trade <= -margin else 0.5), 0.5


def duel_records(directory, args):
    """(kind, label, opponent, score, strict, context) per side of every duel game."""
    out = []
    for game in read(Path(directory) / 'games.csv'):
        if game.get('error') or not keep(game, args):
            continue
        winner = game['winner']
        for me, them in (('a', 'b'), ('b', 'a')):
            house = HOUSE_NUMBERS[game[f'house_{me}']]
            other = HOUSE_NUMBERS[game[f'house_{them}']]
            score, strict = adjudicate(winner == house, winner == other, number(game[f'kill_value_{me}']),
                                       number(game[f'kill_value_{them}']), args.adjudicate)
            out.append(('fortress', game[f'shape_{me}'], game[f'shape_{them}'], score, strict,
                        {'map': game['size'], 'house': game[f'house_{me}'], 'difficulty': game['difficulty'],
                         'end_s': number(game['end_s']), 'game': tuple(game[k] for k in ('tag', 'difficulty', 'size', 'seed',
                                                                                        'house_a', 'shape_a', 'house_b', 'shape_b'))}))
    return out


def vanilla_records(directory, args):
    houses = defaultdict(dict)
    for row in read(Path(directory) / 'houses.csv'):
        houses[(row['tag'], row['difficulty'], row['map'], row['seed'], row['sides'])][row['label'], row['house']] = row
    out = []
    for game in read(Path(directory) / 'games.csv'):
        if game.get('error') or game['players'] != '2' or not keep(game, args):
            continue
        key = (game['tag'], game['difficulty'], game['map'], game['seed'], game['sides'])
        sides = [tuple(side.split(':', 1)) for side in game['sides'].split('|')]
        rows = houses.get(key, {})
        for (house, label), (other_house, other) in (tuple(sides), tuple(reversed(sides))):
            mine, theirs = rows.get((label, house)), rows.get((other, other_house))
            if not mine or not theirs:
                continue
            score, strict = adjudicate(game['winner'] == HOUSE_NUMBERS[house], game['winner'] == HOUSE_NUMBERS[other_house],
                                       number(mine['kill_value']), number(theirs['kill_value']), args.adjudicate)
            out.append(('cy', label, other, score, strict,
                        {'map': game['map'], 'house': house, 'difficulty': game['difficulty'],
                         'end_s': number(game['end_s']), 'game': key}))
    return out


def keep(game, args):
    return (not args.tags or game['tag'] in args.tags) and (not args.difficulty or game['difficulty'] == args.difficulty)


def full(label):
    return label if label in ('default', 'none', 'ai') or '@' in label or '+' in label else f'default@{label}'


def short(label):
    return label[len('default@'):] if label.startswith('default@') else ('BASE' if label == 'default' else label)


def cell(scores, strict=None):
    if not scores:
        return '-'
    low, high = bootstrap(scores)
    text = f'{statistics.mean(scores):.3f} ({low:.2f}-{high:.2f}) n={len(scores)}'
    if strict is not None:
        wins = sum(s == 1.0 for s in strict)
        losses = sum(s == 0.0 for s in strict)
        text += f' {wins}-{losses}/{len(strict) - wins - losses}'
    return text


def verdict(scores):
    if not scores:
        return ''
    low, high = bootstrap(scores)
    return 'beats' if low > 0.5 else 'loses to' if high < 0.5 else 'level with'


def screen(records, base):
    labels = sorted({r[1] for r in records if r[2] == base and r[1] != base})
    print(f'| Style v {short(base)} | Fortress score (95% CI) n W-L/T | CY score (95% CI) n W-L/T | CY verdict | '
          f'Pooled | CY win / loss min (median) |')
    print('|---|---|---|---|---|---|')
    rows = []
    for label in labels:
        mine = [r for r in records if r[1] == label and r[2] == base]
        fort = [r for r in mine if r[0] == 'fortress']
        cy = [r for r in mine if r[0] == 'cy']
        wins = [r[5]['end_s'] / 60 for r in cy if r[4] == 1.0]
        losses = [r[5]['end_s'] / 60 for r in cy if r[4] == 0.0]
        pooled = [r[3] for r in mine]
        rows.append((statistics.mean(r[3] for r in cy) if cy else 0, label, fort, cy, pooled, wins, losses))
    for _, label, fort, cy, pooled, wins, losses in sorted(rows, reverse=True):
        print(f'| {short(label)} | {cell([r[3] for r in fort], [r[4] for r in fort])} | '
              f'{cell([r[3] for r in cy], [r[4] for r in cy])} | {verdict([r[3] for r in cy])} | '
              f'{statistics.mean(pooled):.3f} | {statistics.median(wins) if wins else 0:.1f} / '
              f'{statistics.median(losses) if losses else 0:.1f} |')


def matrix(records, labels):
    by = defaultdict(list)
    for r in records:
        by[r[1], r[2]].append(r[3])
    print('| Row v column | ' + ' | '.join(short(l) for l in labels) + ' | Row mean |')
    print('|---|' + '---|' * (len(labels) + 1))
    for row in labels:
        cells, means = [], []
        for column in labels:
            scores = by.get((row, column), [])
            if row == column or not scores:
                cells.append('-')
                continue
            low, high = bootstrap(scores)
            mark = '**' if low > 0.5 or high < 0.5 else ''
            cells.append(f'{mark}{statistics.mean(scores):.2f}{mark} ({low:.2f}-{high:.2f})')
            means.append(statistics.mean(scores))
        print(f'| {short(row)} | ' + ' | '.join(cells) + f' | {statistics.mean(means) if means else 0:.3f} |')
    print('\n(Bold: the 95% interval excludes 0.5. Games per cell: '
          + ', '.join(sorted({str(len(v)) for (a, b), v in by.items() if a in labels and b in labels})) + '.)')


def breakdown(records, base, field):
    labels = sorted({r[1] for r in records if r[2] == base and r[1] != base})
    groups = sorted({r[5][field] for r in records if r[2] == base})
    print(f'| Style v {short(base)} | ' + ' | '.join(groups) + ' |')
    print('|---|' + '---|' * len(groups))
    for label in labels:
        cells = []
        for group in groups:
            scores = [r[3] for r in records if r[1] == label and r[2] == base and r[5][field] == group]
            if not scores:
                cells.append('-')
                continue
            low, high = bootstrap(scores)
            mark = '**' if low > 0.5 or high < 0.5 else ''
            cells.append(f'{mark}{statistics.mean(scores):.2f}{mark} n={len(scores)}')
        print(f'| {short(label)} | ' + ' | '.join(cells) + ' |')


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--duel', nargs='*', default=[], help='formation-duel.py results directories')
parser.add_argument('--vanilla', nargs='*', default=[], help='vanilla-match.py results directories')
parser.add_argument('--tags', nargs='*')
parser.add_argument('--difficulty', choices=('hard', 'brutal'))
parser.add_argument('--adjudicate', type=float, default=1000)
parser.add_argument('--screen', metavar='BASE')
parser.add_argument('--matrix', nargs='+', metavar='LABEL')
parser.add_argument('--breakdown', metavar='BASE')
parser.add_argument('--by', choices=('map', 'house', 'difficulty'), default='map')
args = parser.parse_args()
records = [r for d in args.duel for r in duel_records(d, args)] + [r for d in args.vanilla for r in vanilla_records(d, args)]
if args.screen:
    screen(records, full(args.screen))
if args.matrix:
    matrix(records, [full(l) for l in args.matrix])
if args.breakdown:
    breakdown(records, full(args.breakdown), args.by)
