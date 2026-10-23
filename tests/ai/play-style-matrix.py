#!/usr/bin/env python3
"""Play-style tables (docs/ai-play-styles.md) from formation-duel.py and vanilla-match.py results.

Scoring (tests/ai/match_score.py, docs/ai-play-styles.md "Scoring and the default rule"): every 1v1
game gives each label its placement score, +1 win, -1 loss, --draw-score (-0.25) for both sides of a
time-out. A label beats another when the 95% bootstrap interval (2,000 resamples of games, fixed seed)
of the per-game difference of their scores is wholly above 0. --scoring legacy restores the old rule
(1 win, 0 loss, a time-out to the side 1,000+ credits ahead on killed unit value (--adjudicate), else
0.5; interval of the score against 0.5).

  play-style-matrix.py --duel D1 D2 --vanilla V1 V2 --screen default
      every label against the base label: Fortress and CY score, difference, W-L / time-outs
  play-style-matrix.py --vanilla V1 --matrix default rush boom ...
      the pairwise matrix (row's score against the column) and each row's mean
  play-style-matrix.py --vanilla V1 --breakdown default --by map|house|difficulty
      each label against the base per map, house or difficulty
Labels may be given without the default@ prefix (rush = default@rush). --tags and --difficulty
filter rows; --same A=B counts label A as B (bit-identical rules recorded under two names, e.g.
default#Learned-Swarm=default@swarm).
"""
import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_score  # noqa: E402

HOUSE_NUMBERS = {'harkonnen': '0', 'atreides': '1', 'ordos': '2', 'fremen': '3', 'sardaukar': '4', 'mercenary': '5'}
SEED = 20261018


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


def scores(won, lost, kill_me, kill_them, args):
    """(score, strict, opponent's score, drawn) of one side of a 1v1."""
    score, strict = match_score.legacy_score(won, lost, kill_me, kill_them, args.adjudicate)
    if args.scoring == 'legacy':
        return score, strict, 1.0 - score, strict == 0.5
    mine, theirs = match_score.duel(won, lost, args.draw_score)
    return mine.score, strict, theirs.score, mine.drawn


def label(name, args):
    return dict(args.same).get(name, name)


def duel_records(directory, args):
    """(kind, label, opponent, score, strict, context) per side of every duel game; context also holds
    the opponent's score ('rival') and whether the game timed out ('drawn')."""
    out = []
    for game in read(Path(directory) / 'games.csv'):
        if game.get('error') or not keep(game, args):
            continue
        winner = game['winner']
        for me, them in (('a', 'b'), ('b', 'a')):
            house = HOUSE_NUMBERS[game[f'house_{me}']]
            other = HOUSE_NUMBERS[game[f'house_{them}']]
            score, strict, rival, drawn = scores(winner == house, winner == other, number(game[f'kill_value_{me}']),
                                                 number(game[f'kill_value_{them}']), args)
            out.append(('fortress', label(game[f'shape_{me}'], args), label(game[f'shape_{them}'], args), score, strict,
                        {'map': game['size'], 'house': game[f'house_{me}'], 'difficulty': game['difficulty'],
                         'end_s': number(game['end_s']), 'rival': rival, 'drawn': drawn,
                         'game': tuple(game[k] for k in ('tag', 'difficulty', 'size', 'seed',
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
        for (house, name), (other_house, other) in (tuple(sides), tuple(reversed(sides))):
            mine, theirs = rows.get((name, house)), rows.get((other, other_house))
            if not mine or not theirs:
                continue
            if args.by_strategy:
                # The strategy the house actually played (a house that cannot field a mix plays Learned).
                name = f'default#{mine["strategy"]}' if name.startswith('default#') and mine.get('strategy') else name
                other = f'default#{theirs["strategy"]}' if other.startswith('default#') and theirs.get('strategy') else other
            score, strict, rival, drawn = scores(game['winner'] == HOUSE_NUMBERS[house],
                                                 game['winner'] == HOUSE_NUMBERS[other_house],
                                                 number(mine['kill_value']), number(theirs['kill_value']), args)
            out.append(('cy', label(name, args), label(other, args), score, strict,
                        {'map': game['map'], 'house': house, 'difficulty': game['difficulty'],
                         'end_s': number(game['end_s']), 'rival': rival, 'drawn': drawn, 'game': key}))
    return out


def keep(game, args):
    return (not args.tags or game['tag'] in args.tags) and (not args.difficulty or game['difficulty'] == args.difficulty)


def full(name):
    return name if name in ('default', 'none', 'ai') or any(c in name for c in '@+#') else f'default@{name}'


def short(name):
    return name[len('default@'):] if name.startswith('default@') else ('BASE' if name == 'default' else name)


def compare(records):
    """The default rule for the records' label against their opponent (one record per game)."""
    return match_score.compare([(r[3], r[5]['rival'], r[5]['drawn'], r[5]['drawn']) for r in records], SEED, args.scoring)


def interval(c):
    """Legacy: the score's interval; placement: the paired difference and its interval."""
    if args.scoring == 'legacy':
        return f'{c.low:.2f}-{c.high:.2f}'
    return f'Δ {c.difference:+.2f}, {c.low:+.2f}..{c.high:+.2f}'


def cell(records):
    if not records:
        return '-'
    c = compare(records)
    wins = sum(r[4] == 1.0 for r in records)
    losses = sum(r[4] == 0.0 for r in records)
    return f'{c.candidate:.3f} ({interval(c)}) n={c.n} {wins}-{losses}/{c.n - wins - losses}'


def mark(c):
    return '**' if match_score.significant(c) else ''


def screen(records, base):
    labels = sorted({r[1] for r in records if r[2] == base and r[1] != base})
    print(f'{match_score.describe(args.scoring, args.draw_score, args.adjudicate)}.\n')
    print(f'| Style v {short(base)} | Fortress score ({"95% CI" if args.scoring == "legacy" else "difference v base, 95% CI"}) n W-L/T '
          f'| CY score (same) n W-L/T | CY verdict | Pooled | CY time-outs | CY win / loss min (median) |')
    print('|---|---|---|---|---|---|---|')
    rows = []
    for name in labels:
        mine = [r for r in records if r[1] == name and r[2] == base]
        fort = [r for r in mine if r[0] == 'fortress']
        cy = [r for r in mine if r[0] == 'cy']
        wins = [r[5]['end_s'] / 60 for r in cy if r[4] == 1.0]
        losses = [r[5]['end_s'] / 60 for r in cy if r[4] == 0.0]
        pooled = [r[3] for r in mine]
        rows.append((statistics.mean(r[3] for r in cy) if cy else 0, name, fort, cy, pooled, wins, losses))
    for _, name, fort, cy, pooled, wins, losses in sorted(rows, reverse=True):
        c = compare(cy)
        print(f'| {short(name)} | {cell(fort)} | {cell(cy)} | {c.verdict if c else ""} | '
              f'{statistics.mean(pooled):.3f} | {100 * c.candidate_draws if c else 0:.0f}% | '
              f'{statistics.median(wins) if wins else 0:.1f} / {statistics.median(losses) if losses else 0:.1f} |')


def matrix(records, labels):
    by = defaultdict(list)
    for r in records:
        by[r[1], r[2]].append(r)
    print(f'{match_score.describe(args.scoring, args.draw_score, args.adjudicate)}.\n')
    print('| Row v column | ' + ' | '.join(short(l) for l in labels) + ' | Row mean | Row time-outs |')
    print('|---|' + '---|' * (len(labels) + 2))
    for row in labels:
        cells, means, draws = [], [], []
        for column in labels:
            mine = by.get((row, column), [])
            if row == column or not mine:
                cells.append('-')
                continue
            c = compare(mine)
            cells.append(f'{mark(c)}{c.candidate:.2f}{mark(c)} ({interval(c)})')
            means.append(c.candidate)
            draws.append(c.candidate_draws)
        print(f'| {short(row)} | ' + ' | '.join(cells) + f' | {statistics.mean(means) if means else 0:.3f} | '
              f'{100 * statistics.mean(draws) if draws else 0:.0f}% |')
    line = '0.5' if args.scoring == 'legacy' else '0 (the row\'s score minus the column\'s, per game)'
    print(f'\n(Bold: the 95% interval excludes {line}. Games per cell: '
          + ', '.join(sorted({str(len(v)) for (a, b), v in by.items() if a in labels and b in labels})) + '.)')


def breakdown(records, base, field):
    labels = sorted({r[1] for r in records if r[2] == base and r[1] != base})
    groups = sorted({r[5][field] for r in records if r[2] == base})
    print(f'{match_score.describe(args.scoring, args.draw_score, args.adjudicate)}.\n')
    print(f'| Style v {short(base)} | ' + ' | '.join(groups) + ' |')
    print('|---|' + '---|' * len(groups))
    for name in labels:
        cells = []
        for group in groups:
            mine = [r for r in records if r[1] == name and r[2] == base and r[5][field] == group]
            if not mine:
                cells.append('-')
                continue
            c = compare(mine)
            cells.append(f'{mark(c)}{c.candidate:.2f}{mark(c)} n={c.n}')
        print(f'| {short(name)} | ' + ' | '.join(cells) + ' |')


def pair(text):
    if '=' not in text:
        raise ValueError(text)
    return tuple(text.split('=', 1))


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--duel', nargs='*', default=[], help='formation-duel.py results directories')
parser.add_argument('--vanilla', nargs='*', default=[], help='vanilla-match.py results directories')
parser.add_argument('--tags', nargs='*')
parser.add_argument('--difficulty', choices=('hard', 'brutal'))
parser.add_argument('--adjudicate', type=float, default=1000, help='--scoring legacy only: the time-out trade margin')
parser.add_argument('--same', nargs='*', type=pair, default=[], metavar='A=B', help='Count label A as label B')
parser.add_argument('--by-strategy', action='store_true',
                    help='CY: count a default#<strategy> house under the strategy it played (houses.csv strategy column)')
parser.add_argument('--screen', metavar='BASE')
parser.add_argument('--matrix', nargs='+', metavar='LABEL')
parser.add_argument('--breakdown', metavar='BASE')
parser.add_argument('--by', choices=('map', 'house', 'difficulty'), default='map')
match_score.add_arguments(parser)
args = parser.parse_args()
records = [r for d in args.duel for r in duel_records(d, args)] + [r for d in args.vanilla for r in vanilla_records(d, args)]
if args.scores:
    match_score.write_scores(args.scores, [
        {'game': '/'.join(r[5]['game']), 'side': r[5]['house'], 'house': r[5]['house'], 'label': r[1],
         'eliminated': int(r[4] == 0.0), 'place': {1.0: 1, 0.0: 2, 0.5: 1.5}[r[4]], 'score': f'{r[3]:.4f}',
         'drawn': int(r[5]['drawn']), 'end_s': r[5]['end_s'], 'decisive': int(r[4] != 0.5)} for r in records])
if args.screen:
    screen(records, label(full(args.screen), args))
if args.matrix:
    matrix(records, [label(full(l), args) for l in args.matrix])
if args.breakdown:
    breakdown(records, label(full(args.breakdown), args), args.by)
