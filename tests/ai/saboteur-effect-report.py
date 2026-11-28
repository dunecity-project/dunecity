#!/usr/bin/env python3
"""Report what Saboteurs achieved in AI matches, Saboteur hunt off against on.

Reads the tables vanilla-match.py writes (games.csv, houses.csv, saboteurs.csv) for two tags
played on the same maps, seeds and sides, one with tests/ai/game-options/
saboteur-hunt-off.ini and one with saboteur-hunt-on.ini:

  python3 tests/ai/vanilla-match.py --tag sab-off --game-options tests/ai/game-options/saboteur-hunt-off.ini ...
  python3 tests/ai/vanilla-match.py --tag sab-on  --game-options tests/ai/game-options/saboteur-hunt-on.ini  ...
  python3 tests/ai/saboteur-effect-report.py --results DIR [DIR ...] --off sab-off --on sab-on

Per arm: Saboteurs released, vehicles and structures they destroyed, credit value destroyed,
value per Saboteur released, Saboteurs lost with nothing destroyed, the time-out rate and the
placement score (tests/ai/match_score.py) of the houses that own Saboteurs. The paired
difference on - off is per game (same map, seed and sides) with a 95% bootstrap interval. A
match diverges from its pair after the first changed Saboteur, so pairing removes the start
position and map, not the rest of the game.
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
SABOTEUR_HOUSES = ('ordos', 'mercenary')
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', type=Path, nargs='+', required=True)
parser.add_argument('--off', default='sab-off', help='Tag of the games played with the rule off')
parser.add_argument('--on', default='sab-on', help='Tag of the games played with the rule on')
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


games = {(row['tag'], pair_of(row)): row for row in read('games.csv') if row['tag'] in (args.off, args.on)}
failed = [row for row in games.values() if row.get('error')]
houses = defaultdict(list)
for row in read('houses.csv'):
    houses[(row['tag'], pair_of(row))].append(row)
events = defaultdict(list)
for row in read('saboteurs.csv'):
    events[(row['tag'], pair_of(row))].append(row)
pairs = sorted(pair for tag, pair in games if tag == args.on and (args.off, pair) in games
               and not games[(args.on, pair)].get('error') and not games[(args.off, pair)].get('error'))
if not pairs:
    raise SystemExit('No game was played in both arms.')


def game_stats(tag, pair):
    rows = events[(tag, pair)]
    released = sum(row['event'] == 'released' for row in rows)
    hits = [row for row in rows if row['event'] == 'hit']
    vehicles = sum(row['vehicle'] == '1' for row in hits)
    value = sum(float(row['price'] or 0) for row in hits)
    game = games[(tag, pair)]
    timeout = game['winner'] in ('', '-1')
    rows_h = houses[(tag, pair)]
    eliminated = {HOUSE_NUMBERS[r['house']]: float(r['eliminated_s']) for r in rows_h if r['eliminated_s'] != ''}
    if not timeout:
        for r in rows_h:
            if HOUSE_NUMBERS[r['house']] != int(game['winner']):
                eliminated.setdefault(HOUSE_NUMBERS[r['house']], float(game['end_s'] or 0))
    placing = match_score.score_game([HOUSE_NUMBERS[r['house']] for r in rows_h], eliminated) if len(rows_h) > 1 else {}
    owners = [placing[HOUSE_NUMBERS[r['house']]].score for r in rows_h if r['house'] in SABOTEUR_HOUSES and placing]
    return dict(released=released, vehicles=vehicles, structures=len(hits) - vehicles, value=value,
                lost=sum(row['event'] == 'lost' for row in rows), timeout=timeout,
                minutes=float(game['end_s'] or 0) / 60, owner_score=statistics.mean(owners) if owners else None)


stats = {tag: {pair: game_stats(tag, pair) for pair in pairs} for tag in (args.off, args.on)}
lines = [f'Saboteur hunt in AI matches: {len(pairs)} games played in both arms '
         f'({args.off} = rule off, {args.on} = rule on)',
         f'maps: {", ".join(sorted({pair[1] for pair in pairs}))}; sides: {", ".join(sorted({pair[3] for pair in pairs}))}',
         f'seeds: {", ".join(sorted({pair[2] for pair in pairs}, key=int))}', '']
if failed:
    lines += [f'{len(failed)} failed game rows are excluded: '
              + ', '.join(f'{row["tag"]}/{row["map"]}/{row["seed"]}' for row in failed), '']
lines.append('arm       released  vehicles  structures  lost unused  unresolved  value destroyed  value/Saboteur  '
             'vehicles/Saboteur  time-outs  mean minutes')
for tag in (args.off, args.on):
    arm = list(stats[tag].values())
    released = sum(g['released'] for g in arm)
    vehicles, structures = sum(g['vehicles'] for g in arm), sum(g['structures'] for g in arm)
    value, lost = sum(g['value'] for g in arm), sum(g['lost'] for g in arm)
    lines.append(f'{tag:<8}  {released:>8}  {vehicles:>8}  {structures:>10}  {lost:>11}  '
                 f'{released - vehicles - structures - lost:>10}  {value:>15.0f}  '
                 f'{value / released if released else 0:>14.1f}  {vehicles / released if released else 0:>17.3f}  '
                 f'{100 * statistics.mean(g["timeout"] for g in arm):>8.1f}%  {statistics.mean(g["minutes"] for g in arm):>12.1f}')
lines += ['', 'Paired difference on - off per game (95% bootstrap interval):']


def difference(text, value, digits=2):
    values = [value(stats[args.on][pair]) - value(stats[args.off][pair]) for pair in pairs
              if value(stats[args.on][pair]) is not None and value(stats[args.off][pair]) is not None]
    if len(values) < 2:
        lines.append(f'  {text}: not enough games with a value in both arms ({len(values)})')
        return
    low, high = match_score.bootstrap(values, 20261122)
    verdict = 'higher with the rule on' if low > 0 else 'lower with the rule on' if high < 0 else 'no difference shown'
    lines.append(f'  {text}: {statistics.mean(values):+.{digits}f} ({low:+.{digits}f} to {high:+.{digits}f}), '
                 f'{len(values)} games: {verdict}')


difference('Saboteurs released per game', lambda g: g['released'])
difference('vehicles destroyed by Saboteurs per game', lambda g: g['vehicles'])
difference('structures destroyed by Saboteurs per game', lambda g: g['structures'])
difference('credit value destroyed by Saboteurs per game', lambda g: g['value'], 0)
difference('credit value destroyed per Saboteur released', lambda g: g['value'] / g['released'] if g['released'] else None, 1)
difference('vehicles destroyed per Saboteur released', lambda g: g['vehicles'] / g['released'] if g['released'] else None, 3)
difference('Saboteurs lost with nothing destroyed, per Saboteur released',
           lambda g: g['lost'] / g['released'] if g['released'] else None, 3)
difference('placement score of the Saboteur-owning houses', lambda g: g['owner_score'], 3)
difference('time-out (1 = the game hit the time limit)', lambda g: 1.0 if g['timeout'] else 0.0, 3)

# ---- What became of each Saboteur: army orders that reached it, and how the lost ones were lost.
MODES = {'0': 'Guard', '1': 'Area Guard', '2': 'Ambush', '3': 'Hunt', '6': 'Stop', '8': 'waiting for a carryall',
         '9': 'Retreat', '10': 'Hold'}
for tag in (args.off, args.on):
    rows = [row for pair in pairs for row in events[(tag, pair)]]
    ended = [row for row in rows if row['event'] in ('hit', 'lost')]
    lost = [row for row in rows if row['event'] == 'lost']
    orders = [row for row in rows if row['event'] == 'order']
    if not any(row.get('orders', '') != '' for row in ended):
        lines += ['', f'{tag}: no order or loss detail in these tables (played before the engine logged it).']
        continue

    def count(chosen, test):
        return sum(1 for row in chosen if test(row))

    def share(part, whole):
        return f'{part} ({100 * part / whole:.1f}%)' if whole else '0'

    number = lambda row, field: int(row.get(field) or 0)   # noqa: E731
    lines += ['', f'{tag}: {len(ended)} Saboteurs ended (blew something up or were lost)',
              f'  got at least one AI army order: {share(count(ended, lambda r: number(r, "orders") > 0), len(ended))}; '
              f'had an order applied while hunting a target: {share(count(ended, lambda r: number(r, "overwrites") > 0), len(ended))}; '
              f'refused at least one: {share(count(ended, lambda r: number(r, "refused") > 0), len(ended))}']
    callers = defaultdict(int)
    for row in orders:
        callers[(row['order'], row['caller'], row['refused'])] += 1
    for (order, caller, refused), total in sorted(callers.items(), key=lambda item: -item[1])[:8]:
        lines.append(f'    {total:>5} logged orders: {order} from {caller} ({"refused" if refused == "1" else "applied"})')
    changes = defaultdict(int)
    for row in rows:
        if row['event'] == 'mode':
            changes[(MODES.get(row['from'], row['from']), MODES.get(row['to'], row['to']))] += 1
    if changes:
        lines.append('  attack-mode changes: ' + ', '.join(
            f'{a} -> {b} {total}' for (a, b), total in sorted(changes.items(), key=lambda item: -item[1])[:6]))
    if lost:
        near = count(lost, lambda r: 0 <= number(r, 'target_dist') <= 2 and r.get('target_dist', '') not in ('', '-1'))
        no_target = count(lost, lambda r: r.get('target_dist', '') in ('', '-1'))
        interfered = count(lost, lambda r: number(r, 'overwrites') > 0)
        lines.append(f'  lost with nothing destroyed: {len(lost)}; within two tiles of its target {share(near, len(lost))}, '
                     f'further away {share(len(lost) - near - no_target, len(lost))}, with no target {share(no_target, len(lost))}')
        lines.append(f'    of them after an AI army order overwrote its hunt: {share(interfered, len(lost))}; '
                     f'not hunting when lost: {share(count(lost, lambda r: r.get("mode") != "3"), len(lost))}')
        killers = defaultdict(int)
        for row in lost:
            killers[row.get('killer') or 'unknown'] += 1
        lines.append('    killed by: ' + ', '.join(f'{kind} {share(total, len(lost))}'
                                                   for kind, total in sorted(killers.items(), key=lambda item: -item[1])))
text = '\n'.join(lines)
print(text)
if args.output:
    args.output.write_text(text + '\n')
