#!/usr/bin/env python3
"""Report what city airplanes did in AI matches, the city plane rules off against on.

Reads the tables vanilla-match.py writes (games.csv, houses.csv, planes.csv) for two tags played
on the same maps, seeds and sides, one with tests/ai/game-options/city-plane-rules-off.ini (the
game up to 1.0.876: unarmed planes, anti-air holding its fire, Airport Ornithopter patrols) and
one with city-plane-rules-on.ini (the defaults):

  python3 tests/ai/vanilla-match.py --tag planes-off --game-options tests/ai/game-options/city-plane-rules-off.ini ...
  python3 tests/ai/vanilla-match.py --tag planes-on  --game-options tests/ai/game-options/city-plane-rules-on.ini  ...
  python3 tests/ai/city-plane-report.py --results DIR [DIR ...] --off planes-off --on planes-on

The same report compares any two tags, e.g. the bomb stick, bombing run and purchase rules
(city-plane-b52-off.ini against city-plane-b52-on.ini, --off-label/--on-label name the columns).

Per arm and game: planes launched and bought, bombs dropped, buildings and credits destroyed by bombs,
planes shot down (and by what), Ornithopters an Airport sent out, the time-out rate, the game
length and each house's placement score (tests/ai/match_score.py). The paired difference on - off
is per game (same map, seed and sides) with a 95% bootstrap interval. Every house plays the same
rules, so a house's placement score can only show whether the rules favour one seat; a match
diverges from its pair after the first changed shot, so pairing removes the map and the start.

Then, for each arm whose logs carry per-flight kills (from 2026-11-26), the economics of its flights:
value destroyed per flight and per game against the 1500 price, buildings and units apart, by house and
by game minute (tests/ai/city_plane_economics.py, which also runs on one tag alone).
"""
import argparse
import csv
import gzip
import statistics
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_score  # noqa: E402
import city_plane_economics  # noqa: E402

PAIR = ('difficulty', 'map', 'seed', 'sides')
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', type=Path, nargs='+', required=True)
parser.add_argument('--off', default='planes-off', help='Tag of the games played with the rules of 1.0.876')
parser.add_argument('--on', default='planes-on', help='Tag of the games played with the default rules')
parser.add_argument('--off-label', default='Off', help='Column title of the --off arm')
parser.add_argument('--on-label', default='On', help='Column title of the --on arm')
parser.add_argument('--output', type=Path, help='Also write the report here')
args = parser.parse_args()


def read(name):
    rows = []
    for folder in args.results:
        path = folder / name
        if path.exists():
            with path.open(newline='') as handle:
                rows.extend(csv.DictReader(handle))
        elif (folder / (name + '.gz')).exists():  # the queue compresses a large collected table
            with gzip.open(folder / (name + '.gz'), 'rt', newline='') as handle:
                rows.extend(csv.DictReader(handle))
    return rows


def pair_of(row):
    return tuple(row[k] for k in PAIR)


def number(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return 0.0


games = {(row['tag'], pair_of(row)): row for row in read('games.csv') if row['tag'] in (args.off, args.on)}
failed = [row for row in games.values() if row.get('error')]
houses = defaultdict(list)
for row in read('houses.csv'):
    houses[(row['tag'], pair_of(row))].append(row)
events = defaultdict(list)
for row in read('planes.csv'):
    events[(row['tag'], pair_of(row))].append(row)

pairs = sorted(pair for tag, pair in games if tag == args.off and (args.on, pair) in games
               and not games[(args.off, pair)].get('error') and not games[(args.on, pair)].get('error'))
if not pairs:
    sys.exit('No game was played in both arms')


def game_measures(tag, pair):
    rows = events[(tag, pair)]
    count = lambda event: sum(1 for row in rows if row['event'] == event)  # noqa: E731
    kills = [row for row in rows if row['event'] == 'kill']
    downs = [row for row in rows if row['event'] == 'down']
    game = games[(tag, pair)]
    timeout = game['winner'] in ('', '-1')
    rows_h = houses[(tag, pair)]
    eliminated = {HOUSE_NUMBERS[r['house']]: float(r['eliminated_s']) for r in rows_h if r['eliminated_s'] != ''}
    if not timeout:
        for r in rows_h:
            if HOUSE_NUMBERS[r['house']] != int(game['winner']):
                eliminated.setdefault(HOUSE_NUMBERS[r['house']], float(game['end_s'] or 0))
    placing = match_score.score_game([HOUSE_NUMBERS[r['house']] for r in rows_h], eliminated) if len(rows_h) > 1 else {}
    launched = max(1, count('launched'))
    bombers = len({row['object'] for row in rows if row['event'] == 'bomb'})
    return {
        'launched': count('launched'),
        'bought': count('bought'),
        'spent': sum(number(row['price']) for row in rows if row['event'] == 'bought'),
        'bombing_share': 100.0 * bombers / launched,
        'bombs_per_flight': count('bomb') / launched,
        'buildings_per_flight': len(kills) / launched,
        'bombs': count('bomb'),
        'buildings': len(kills),
        'credits': sum(number(row['price']) for row in kills),
        'down': len(downs),
        'down_by_turret': sum(1 for row in downs if row['killer_item'] == '12'),
        'patrol': count('patrol'),
        'timeout': 1.0 if timeout else 0.0,
        'minutes': float(game['end_s'] or 0) / 60,
        'eliminated': float(len([r for r in rows_h if r['eliminated_s'] != ''])),
        'placing': {r['house']: placing[HOUSE_NUMBERS[r['house']]].score for r in rows_h} if placing else {},
    }


measures = {tag: {pair: game_measures(tag, pair) for pair in pairs} for tag in (args.off, args.on)}
LABELS = (('launched', 'City planes launched per game'), ('bought', '... of them bought'),
          ('spent', 'Credits spent on planes per game'), ('bombing_share', 'Flights that released a bomb, %'),
          ('bombs_per_flight', 'Bombs per flight'), ('buildings_per_flight', 'Buildings destroyed per flight'),
          ('bombs', 'Bombs dropped per game'),
          ('buildings', 'Buildings destroyed by bombs per game'), ('credits', 'Credits destroyed by bombs per game'),
          ('down', 'City planes shot down per game'), ('down_by_turret', '... by a Rocket Turret'),
          ('patrol', 'Ornithopters sent out by Airports per game'), ('timeout', 'Time-outs (share of games)'),
          ('eliminated', 'Houses eliminated before the end, per game'), ('minutes', 'Game length, minutes'))
report = ['# City plane rules in AI matches: %s against %s' % (args.off, args.on), '',
          '%d games played in both arms%s.' % (len(pairs), ', %d failed' % len(failed) if failed else ', none failed'), '',
          '| Measure | %s | %s | Paired difference | 95%% interval |' % (args.off_label, args.on_label), '|---|---:|---:|---:|---|']
for name, label in LABELS:
    off = [measures[args.off][pair][name] for pair in pairs]
    on = [measures[args.on][pair][name] for pair in pairs]
    low, high = match_score.bootstrap([b - a for a, b in zip(off, on)], 20261123)
    report.append('| %s | %.2f | %.2f | %+.2f | %+.2f to %+.2f |'
                  % (label, statistics.mean(off), statistics.mean(on), statistics.mean(on) - statistics.mean(off), low, high))

# Placement by house: the same seat in the same game, off against on.
by_house = defaultdict(list)
for pair in pairs:
    off, on = measures[args.off][pair]['placing'], measures[args.on][pair]['placing']
    for house in off:
        if house in on:
            by_house[house].append((off[house], on[house]))
if by_house:
    report += ['', '| House | Placement score %s | %s | Paired difference | 95%% interval |' % (args.off_label, args.on_label),
               '|---|---:|---:|---:|---|']
    for house, values in sorted(by_house.items()):
        low, high = match_score.bootstrap([b - a for a, b in values], 20261123)
        report.append('| %s | %+.3f | %+.3f | %+.3f | %+.3f to %+.3f |'
                      % (house, statistics.mean(a for a, _ in values), statistics.mean(b for _, b in values),
                         statistics.mean(b - a for a, b in values), low, high))

killers = defaultdict(int)
for pair in pairs:
    for row in events[(args.on, pair)]:
        if row['event'] == 'down':
            killers[row['killer_item']] += 1
if killers:
    report += ['', 'Planes shot down with the rules on, by the shooter\'s item ID: '
               + ', '.join('%s: %d' % item for item in sorted(killers.items(), key=lambda item: -item[1])) + '.']
targets = defaultdict(int)
for pair in pairs:
    for row in events[(args.on, pair)]:
        if row['event'] == 'kill':
            targets[row['target_name'] or row['target_item']] += 1
if targets:
    report += ['', 'Buildings destroyed by bombs: '
               + ', '.join('%s %d' % item for item in sorted(targets.items(), key=lambda item: -item[1])) + '.']

# Economics of the flights, each arm on the games played in both.
plane_rows = read('planes.csv')
for tag, label in ((args.off, args.off_label), (args.on, args.on_label)):
    flights, _, has_objects = city_plane_economics.flights_of(plane_rows, tag)
    if not has_objects:
        continue
    keys = {(tag,) + pair for pair in pairs}
    flights = [f for f in flights if f.game in keys]
    report += [''] + city_plane_economics.report(flights, keys, 'Economics, %s (%s)' % (label, tag))

text = '\n'.join(report) + '\n'
print(text)
if args.output:
    args.output.write_text(text, encoding='utf-8')
