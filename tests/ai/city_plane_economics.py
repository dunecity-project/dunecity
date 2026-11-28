#!/usr/bin/env python3
"""Do city airplanes pay for themselves? Value destroyed per flight against the plane's price.

Stefan, 2026-11-26: "for airplanes we really want to see how many units or buildings they destroy (and
total value) vs their cost (1500) to see if they make economic sense".

Reads planes.csv from vanilla-match.py results (one or more folders) and follows each flight by the
plane's object ID, from CITY_PLANE_LAUNCHED (free or bought, house, cycle) to CITY_PLANE_DOWN (shot
down) or CITY_PLANE_LEFT (flew off the map). A flight still in the air when the game ends is counted
with what it destroyed so far and marked "in the air at the end".

What a flight destroyed, split by source and kind:
  - bomb, buildings  CITY_PLANE_KILL       (Bullet_Bomb; object = the plane that dropped it)
  - bomb, units      CITY_PLANE_UNIT_KILL source=bomb
  - crash, buildings CITY_PLANE_CRASH_KILL (the plane's small crash when shot down)
  - crash, units     CITY_PLANE_UNIT_KILL source=crash
Only objects of another team count towards the value; what the plane's own side lost (units under its
bomb or crash) is reported apart. Value is the build price of what was destroyed, from the game's own
unit data for its original house (ObjectData.ini: zones 100 each, Wind Trap 300, Rocket Turret 250,
Airport 5000, Harvester 300, Siege Tank 600, ...). A zone counts as its 100-credit build price, not as
what has grown on it; partial damage to an object that survives is not counted.

A free plane costs nothing; a bought one CityAircraft::kPlanePrice (1500). "Repays" is a flight that
destroyed at least 1500 of enemy value, free or bought, i.e. a flight that would have paid for itself
had it been bought.

  python3 tests/ai/city_plane_economics.py --results DIR [DIR ...] [--tag TAG] [--output report.md]

city-plane-report.py prints the same tables for each arm it compares.
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

PLANE_PRICE = 1500          # CityAircraft::kPlanePrice
CYCLES_PER_MINUTE = 3750    # 16 ms a game cycle (GAMESPEED_DEFAULT)
PAIR = ('difficulty', 'map', 'seed', 'sides')
HOUSE_NAMES = {0: 'Harkonnen', 1: 'Atreides', 2: 'Ordos', 3: 'Fremen', 4: 'Sardaukar', 5: 'Mercenary'}
MINUTE_BANDS = ((0, 10), (10, 20), (20, 30), (30, 45), (45, 10 ** 6))
SOURCES = (('bomb', 'building'), ('bomb', 'unit'), ('crash', 'building'), ('crash', 'unit'))


def read_rows(folders, name):
    rows = []
    for folder in folders:
        path = Path(folder) / name
        if path.exists():
            with path.open(newline='') as handle:
                rows.extend(csv.DictReader(handle))
        elif (Path(folder) / (name + '.gz')).exists():
            with gzip.open(Path(folder) / (name + '.gz'), 'rt', newline='') as handle:
                rows.extend(csv.DictReader(handle))
    return rows


def number(text, default=0.0):
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


class Flight:
    __slots__ = ('game', 'house', 'object', 'bought', 'launch_cycle', 'end', 'end_cycle', 'killer_item',
                 'count', 'value', 'own_count', 'own_value', 'targets')

    def __init__(self, game, house, obj, bought, cycle):
        self.game, self.house, self.object, self.bought, self.launch_cycle = game, house, obj, bought, cycle
        self.end, self.end_cycle, self.killer_item = 'air', None, ''
        self.count = defaultdict(int)       # (source, kind) -> enemy objects destroyed
        self.value = defaultdict(float)     # (source, kind) -> enemy credits destroyed
        self.own_count, self.own_value = 0, 0.0
        self.targets = defaultdict(lambda: [0, 0.0])

    @property
    def total(self):
        return sum(self.value.values())

    @property
    def minute(self):
        return self.launch_cycle / CYCLES_PER_MINUTE

    def kills(self, kind=None, source=None):
        return sum(n for (s, k), n in self.count.items() if (kind is None or k == kind) and (source is None or s == source))

    def worth(self, kind=None, source=None):
        return sum(v for (s, k), v in self.value.items() if (kind is None or k == kind) and (source is None or s == source))


def flights_of(rows, tag=None):
    """Flights by (game, object). Rows of games that have no object on their kill lines (logs from
    before 2026-11-26) give flights with no kills; check `has_objects`."""
    global PLANE_PRICE
    flights, games = {}, set()
    has_objects = False
    # The price the games charged (CITY_PLANE_BOUGHT; DUNECITY_TEST_PLANE_PRICE arms differ).
    prices = {number(r.get('price')) for r in rows if r['event'] == 'bought' and (tag is None or r.get('tag') == tag)}
    PLANE_PRICE = int(max(prices)) if prices else 1500
    for row in rows:
        if tag is not None and row.get('tag') != tag:
            continue
        game = (row.get('tag', ''),) + tuple(row.get(k, '') for k in PAIR)
        games.add(game)
        event, obj = row['event'], row.get('object', '')
        if event == 'launched':
            flights[(game, obj)] = Flight(game, int(number(row['house'], -1)), obj, row.get('bought') == '1',
                                          number(row['cycle']))
    for row in rows:
        if tag is not None and row.get('tag') != tag:
            continue
        game = (row.get('tag', ''),) + tuple(row.get(k, '') for k in PAIR)
        event, obj = row['event'], row.get('object', '')
        flight = flights.get((game, obj))
        if event in ('kill', 'crash_kill', 'unit_kill') and obj:
            has_objects = True
        if flight is None:
            continue
        if event in ('down', 'left'):
            flight.end, flight.end_cycle = event, number(row['cycle'])
            flight.killer_item = row.get('killer_item', '')
        elif event in ('kill', 'crash_kill', 'unit_kill'):
            source = 'crash' if event == 'crash_kill' else (row.get('source') or 'bomb')
            kind = 'unit' if event == 'unit_kill' else 'building'
            price = number(row.get('price'))
            if row.get('own') == '1':
                flight.own_count += 1
                flight.own_value += price
                continue
            flight.count[(source, kind)] += 1
            flight.value[(source, kind)] += price
            target = flight.targets[row.get('target_name') or row.get('target_item') or '?']
            target[0] += 1
            target[1] += price
    return list(flights.values()), games, has_objects


def ci(values, seed=20261126):
    low, high = match_score.bootstrap(values, seed) if len(values) > 1 else (0.0, 0.0)
    return '%s to %s' % (fmt(low), fmt(high))


def fmt(value):
    return '{:,.0f}'.format(value)


def pct(part, whole):
    return '%.0f%%' % (100.0 * part / whole) if whole else '-'


def flight_row(label, flights):
    if not flights:
        return '| %s | 0 | | | | | | | |' % label
    totals = [f.total for f in flights]
    down = sum(1 for f in flights if f.end == 'down')
    return '| %s | %d | %s | %s | %s | %s | %.2f | %.2f | %s |' % (
        label, len(flights), fmt(statistics.median(totals)), fmt(statistics.mean(totals)), ci(totals),
        pct(sum(1 for t in totals if t >= PLANE_PRICE), len(flights)),
        statistics.mean(f.kills('building') for f in flights), statistics.mean(f.kills('unit') for f in flights),
        pct(down, len(flights)))


def report(flights, games, title, rows=()):
    lines = ['## %s' % title, '']
    if not flights:
        return lines + ['No city airplane flew in these games.', '']
    n_games = len(games)
    bought = [f for f in flights if f.bought]
    free = [f for f in flights if not f.bought]
    lines += ['%d games, %d flights (%d free, %d bought). A plane costs %s; a free plane costs nothing. '
              'Value = build price of enemy objects destroyed.' % (n_games, len(flights), len(free), len(bought),
                                                                   fmt(PLANE_PRICE)), '',
              '### Per flight', '',
              '| Flights | n | Median value | Mean value | 95% interval of mean | Repay 1500 | Buildings | Units | Shot down |',
              '|---|---:|---:|---:|---|---:|---:|---:|---:|',
              flight_row('All', flights), flight_row('Free', free), flight_row('Bought', bought)]
    ended = [f for f in flights if f.end != 'air']
    if ended:
        lines.append(flight_row('Ended in the game (not in the air at the end)', ended))

    lines += ['', '### Where the value comes from (mean per flight)', '',
              '| Source | Buildings | Building value | Units | Unit value | Share of value |', '|---|---:|---:|---:|---:|---:|']
    grand = sum(f.total for f in flights) or 1.0
    for source in ('bomb', 'crash'):
        b = statistics.mean(f.kills('building', source) for f in flights)
        bv = statistics.mean(f.worth('building', source) for f in flights)
        u = statistics.mean(f.kills('unit', source) for f in flights)
        uv = statistics.mean(f.worth('unit', source) for f in flights)
        share = sum(f.worth(source=source) for f in flights) / grand
        lines.append('| %s | %.2f | %s | %.2f | %s | %.0f%% |' % (source.capitalize(), b, fmt(bv), u, fmt(uv), 100 * share))
    own_n = sum(f.own_count for f in flights)
    lines.append('| Own side lost to its own planes (not in the value) | | | %.2f | %s | |'
                 % (own_n / len(flights), fmt(sum(f.own_value for f in flights) / len(flights))))

    # Per game.
    per_game = defaultdict(list)
    for f in flights:
        per_game[f.game].append(f)
    game_values = [sum(f.total for f in per_game.get(g, [])) for g in games]
    game_bought = [sum(1 for f in per_game.get(g, []) if f.bought) for g in games]
    game_flights = [len(per_game.get(g, [])) for g in games]
    game_bought_value = [sum(f.total for f in per_game.get(g, []) if f.bought) for g in games]
    spent = sum(game_bought) * PLANE_PRICE
    lines += ['', '### Per game', '',
              '| Measure | Mean a game | Median | 95% interval |', '|---|---:|---:|---|',
              '| Flights | %.1f | %.0f | %s |' % (statistics.mean(game_flights), statistics.median(game_flights), ci(game_flights)),
              '| Planes bought | %.1f | %.0f | %s |' % (statistics.mean(game_bought), statistics.median(game_bought), ci(game_bought)),
              '| Credits spent on planes | %s | %s | %s |' % (fmt(statistics.mean(game_bought) * PLANE_PRICE),
                                                             fmt(statistics.median(game_bought) * PLANE_PRICE),
                                                             ci([b * PLANE_PRICE for b in game_bought])),
              '| Enemy value destroyed, all flights | %s | %s | %s |' % (fmt(statistics.mean(game_values)),
                                                                        fmt(statistics.median(game_values)), ci(game_values)),
              '| ... by bought flights | %s | %s | %s |' % (fmt(statistics.mean(game_bought_value)),
                                                          fmt(statistics.median(game_bought_value)), ci(game_bought_value)),
              '| Buildings destroyed | %.1f | | |' % (sum(f.kills('building') for f in flights) / n_games),
              '| Units destroyed | %.1f | | |' % (sum(f.kills('unit') for f in flights) / n_games),
              '',
              'Value destroyed per plane bought: %s by the bought planes themselves (%s spent, %.2f credits back per '
              'credit); %s counting the free planes too.' % (
                  fmt(sum(game_bought_value) / max(1, sum(game_bought))), fmt(spent),
                  sum(game_bought_value) / spent if spent else 0.0, fmt(sum(game_values) / max(1, sum(game_bought))))]

    extra = [r for r in rows if r.get('event') == 'airport_extra'
             and (r.get('tag', ''),) + tuple(r.get(k, '') for k in PAIR) in games]
    if extra:
        lines += ['', 'Extra Airports ordered (QuantBot ExtraAirports): %.2f a game; value per ended flight when '
                  'ordered, median %s.' % (len(extra) / n_games,
                                           fmt(statistics.median(number(r['value_per_flight']) for r in extra)))]

    # By house.
    lines += ['', '### By house', '',
              '| House | Flights | Bought | Median value | Mean value | Repay 1500 | Shot down | Value a game | Spent a game |',
              '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    by_house = defaultdict(list)
    for f in flights:
        by_house[f.house].append(f)
    for house, fl in sorted(by_house.items()):
        totals = [f.total for f in fl]
        lines.append('| %s | %d | %d | %s | %s | %s | %s | %s | %s |' % (
            HOUSE_NAMES.get(house, house), len(fl), sum(f.bought for f in fl), fmt(statistics.median(totals)),
            fmt(statistics.mean(totals)), pct(sum(t >= PLANE_PRICE for t in totals), len(fl)),
            pct(sum(f.end == 'down' for f in fl), len(fl)), fmt(sum(totals) / n_games),
            fmt(sum(f.bought for f in fl) * PLANE_PRICE / n_games)))

    # By launch minute.
    lines += ['', '### By game minute of launch', '',
              '| Minute | Flights | Bought | Median value | Mean value | Repay 1500 | Buildings | Units | Shot down |',
              '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for low, high in MINUTE_BANDS:
        fl = [f for f in flights if low <= f.minute < high]
        if not fl:
            continue
        totals = [f.total for f in fl]
        label = '%d-%d' % (low, high) if high < 10 ** 6 else '%d+' % low
        lines.append('| %s | %d | %d | %s | %s | %s | %.2f | %.2f | %s |' % (
            label, len(fl), sum(f.bought for f in fl), fmt(statistics.median(totals)), fmt(statistics.mean(totals)),
            pct(sum(t >= PLANE_PRICE for t in totals), len(fl)), statistics.mean(f.kills('building') for f in fl),
            statistics.mean(f.kills('unit') for f in fl), pct(sum(f.end == 'down' for f in fl), len(fl))))

    # Distribution and how flights end.
    totals = sorted(f.total for f in flights)
    quart = statistics.quantiles(totals, n=4) if len(totals) > 3 else [0, 0, 0]
    durations = [(f.end_cycle - f.launch_cycle) / CYCLES_PER_MINUTE * 60 for f in flights if f.end_cycle is not None]
    lines += ['', 'Value per flight: quartiles %s / %s / %s, %s of flights destroyed nothing, top 10%% of flights '
              'destroyed %s of all value.' % (
                  fmt(quart[0]), fmt(quart[1]), fmt(quart[2]), pct(sum(t == 0 for t in totals), len(totals)),
                  pct(sum(totals[-max(1, len(totals) // 10):]), sum(totals) or 1)),
              'Flights ended: %d shot down, %d flew off the map, %d in the air at the end; median flight %s s.' % (
                  sum(f.end == 'down' for f in flights), sum(f.end == 'left' for f in flights),
                  sum(f.end == 'air' for f in flights), fmt(statistics.median(durations)) if durations else '-')]
    killers = defaultdict(int)
    for f in flights:
        if f.end == 'down':
            killers[f.killer_item or '?'] += 1
    if killers:
        lines.append('Shot down by item ID: ' + ', '.join('%s %d' % kv for kv in sorted(killers.items(), key=lambda kv: -kv[1])) + '.')
    targets = defaultdict(lambda: [0, 0.0])
    for f in flights:
        for name, (n, v) in f.targets.items():
            targets[name][0] += n
            targets[name][1] += v
    if targets:
        lines.append('Destroyed, by value: ' + ', '.join(
            '%s %d (%s)' % (name.replace('_', ' '), n, fmt(v))
            for name, (n, v) in sorted(targets.items(), key=lambda kv: -kv[1][1])[:16]) + '.')
    return lines + ['']


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--results', type=Path, nargs='+', required=True)
    parser.add_argument('--tag', action='append', help='Only these tags (default: each tag found, one section each)')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    rows = read_rows(args.results, 'planes.csv')
    game_rows = [r for r in read_rows(args.results, 'games.csv') if not r.get('error')]
    tags = args.tag or sorted({r['tag'] for r in game_rows})
    out = ['# City airplanes: value destroyed against their price', '']
    for tag in tags:
        games = {(tag,) + tuple(r.get(k, '') for k in PAIR) for r in game_rows if r['tag'] == tag}
        flights, _, has_objects = flights_of(rows, tag)
        flights = [f for f in flights if f.game in games]
        if not has_objects:
            out += ['## %s' % tag, '', 'These logs predate per-flight kill lines (object= on CITY_PLANE_KILL); '
                    'no per-flight value can be computed.', '']
            continue
        out += report(flights, games, tag, rows)
    text = '\n'.join(out) + '\n'
    print(text)
    if args.output:
        args.output.write_text(text, encoding='utf-8')


if __name__ == '__main__':
    main()
