#!/usr/bin/env python3
"""Report how well Airports are defended against city airplanes: AirportTurrets off against on.

Stefan, 2026-11-26: "also airports need decent rocket turret defenses". Reads the tables
vanilla-match.py writes (games.csv, houses.csv, planes.csv) for:

  self-play arms: every house with the switch off (--off) and on (--on), same maps, seeds and
  sides, played with --env DUNECITY_TEST_AIRPORT_TURRETS=0 / =1 in one build;
  a mixed arm (--mixed, optional): --ffa default,default@airport-turrets, half the houses with
  the switch on, every balanced assignment per seed. Only this arm can show whether the rule
  helps a house win (the default rule, CLAUDE.md "AI test games: scoring and the default rule").

Measures, per arm, from the engine's match log lines:
  CITY_AIRPORT_COVER (once a game minute per Airport): own-team Rocket Turrets in its ring (within
    5 tiles of the footprint) and in reach of a plane over it (16 tiles);
  CITY_PLANE_NEAR_AIRPORT: an enemy plane's first approach (within 16 tiles) and first pass over
    (within 1 tile) of each Airport; with CITY_PLANE_DOWN (where it fell, the nearest enemy
    Airport) each plane-Airport approach ends as shot down before reaching it, reached it and
    shot down within 16 tiles of it, or reached it and got away;
  CITY_PLANE_KILL / CITY_PLANE_CRASH_KILL: buildings lost to planes, Airports among them;
  CITY_AIRPORT_GUARD: the turrets (and Wind Traps) the rule ordered and their price.

  python3 tests/ai/airport-turrets-report.py --results DIR [DIR ...] --off airport-off --on airport-on \\
      [--mixed airport-mixed] [--output report.md]
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

PAIR = ('difficulty', 'map', 'seed', 'sides')
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
CYCLES_PER_MINUTE = 3750
APPROACH_TILES = 16
AIRPORT_ITEM = '49'
ROCKET_TURRET_ITEM = '12'
ON_LABEL = 'default@airport-turrets'
SEED = 20261126

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', type=Path, nargs='+', required=True)
parser.add_argument('--off', default='airport-off')
parser.add_argument('--on', default='airport-on')
parser.add_argument('--mixed', help='Tag of the mixed arm (default v %s)' % ON_LABEL)
parser.add_argument('--minutes', default='1,5,10,20,30,45', help='Ring table minutes')
parser.add_argument('--output', type=Path)
args = parser.parse_args()


def read(name):
    rows = []
    for folder in args.results:
        path = folder / name
        if path.exists():
            with path.open(newline='') as handle:
                rows.extend(csv.DictReader(handle))
        elif (folder / (name + '.gz')).exists():
            with gzip.open(folder / (name + '.gz'), 'rt', newline='') as handle:
                rows.extend(csv.DictReader(handle))
    return rows


def pair_of(row):
    return tuple(row[k] for k in PAIR)


def num(text, default=0.0):
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


tags = [t for t in (args.off, args.on, args.mixed) if t]
games = {(row['tag'], pair_of(row)): row for row in read('games.csv') if row['tag'] in tags}
houses = defaultdict(list)
for row in read('houses.csv'):
    if row['tag'] in tags:
        houses[(row['tag'], pair_of(row))].append(row)
events = defaultdict(list)
for row in read('planes.csv'):
    if row['tag'] in tags:
        events[(row['tag'], pair_of(row))].append(row)


def ok(tag, pair):
    game = games.get((tag, pair))
    return game is not None and not game.get('error')


def placings(tag, pair):
    game = games[(tag, pair)]
    rows = houses[(tag, pair)]
    timeout = game['winner'] in ('', '-1')
    eliminated = {HOUSE_NUMBERS[r['house']]: float(r['eliminated_s']) for r in rows if r['eliminated_s'] != ''}
    if not timeout:
        for r in rows:
            if HOUSE_NUMBERS[r['house']] != int(game['winner']):
                eliminated.setdefault(HOUSE_NUMBERS[r['house']], float(game['end_s'] or 0))
    placing = match_score.score_game([HOUSE_NUMBERS[r['house']] for r in rows], eliminated)
    return {r['house']: (r['label'], placing[HOUSE_NUMBERS[r['house']]]) for r in rows}, timeout


def passes(rows):
    """Plane-Airport approaches and how each ended."""
    near = defaultdict(dict)        # (plane, airport) -> {stage: cycle}
    down = {}
    for row in rows:
        if row['event'] == 'near_airport':
            near[(row['object'], row['airport'])].setdefault(row['stage'], num(row['cycle']))
        elif row['event'] == 'down':
            down[row['object']] = row
    out = {'approach': 0, 'down_before': 0, 'reached': 0, 'down_after': 0, 'got_away': 0}
    for (plane, airport), stages in near.items():
        out['approach'] += 1
        fell = down.get(plane)
        near_this = (fell is not None and fell.get('airport') == airport
                     and 0 <= num(fell.get('airport_dist'), -1) <= APPROACH_TILES)
        if 'over' not in stages:
            if near_this:
                out['down_before'] += 1
            continue
        out['reached'] += 1
        if near_this and num(fell['cycle']) >= stages['over']:
            out['down_after'] += 1
        else:
            out['got_away'] += 1
    return out


def measures(tag, pair):
    rows = events[(tag, pair)]
    kills = [r for r in rows if r['event'] == 'kill']
    guard = [r for r in rows if r['event'] == 'airport_guard']
    flown = passes(rows)
    downs = [r for r in rows if r['event'] == 'down']
    launched = sum(1 for r in rows if r['event'] == 'launched')
    game = games[(tag, pair)]
    return {
        'launched': launched,
        'down_share': 100.0 * len(downs) / launched if launched else 0.0,
        'buildings_lost': len(kills) + sum(1 for r in rows if r['event'] == 'crash_kill'),
        'airports_lost': sum(1 for r in kills if r['target_item'] == AIRPORT_ITEM),
        'airport_bombs': sum(1 for r in rows if r['event'] == 'bomb' and r['target_item'] == AIRPORT_ITEM),
        **flown,
        'down_before_share': 100.0 * flown['down_before'] / flown['approach'] if flown['approach'] else 0.0,
        'reached_share': 100.0 * flown['reached'] / flown['approach'] if flown['approach'] else 0.0,
        'guard_turrets': sum(1 for r in guard if r['item'] == ROCKET_TURRET_ITEM),
        'guard_credits': sum(num(r['price']) for r in guard),
        'timeout': 1.0 if game['winner'] in ('', '-1') else 0.0,
        'minutes': num(game['end_s']) / 60,
    }


def cover_table(tag, pairs, minutes):
    """Airport samples at each minute: (n, median ring, % ring>=4, median reach, % powered)."""
    by_minute = defaultdict(list)
    for pair in pairs:
        for row in events[(tag, pair)]:
            if row['event'] != 'airport_cover':
                continue
            minute = round(num(row['cycle']) / CYCLES_PER_MINUTE)
            by_minute[minute].append((int(num(row['ring'])), int(num(row['reach'])), int(num(row['powered'], 1))))
    out = {}
    for minute in minutes:
        samples = by_minute.get(minute, [])
        if samples:
            rings = [s[0] for s in samples]
            reach = [s[1] for s in samples]
            out[minute] = (len(samples), statistics.median(rings), 100.0 * sum(r >= 4 for r in rings) / len(rings),
                           statistics.median(reach), 100.0 * sum(s[2] for s in samples) / len(samples))
    return out


report = ['# Airport Rocket Turret defence: AirportTurrets off against on', '']
pairs = sorted(p for (t, p) in games if t == args.off and ok(args.off, p) and ok(args.on, p))
failed = [k for k, g in games.items() if g.get('error')]
report += ['%d self-play games in both arms (%s, %s)%s.' % (len(pairs), args.off, args.on,
                                                             ', %d failed' % len(failed) if failed else ', none failed'), '']
if pairs:
    m = {tag: {p: measures(tag, p) for p in pairs} for tag in (args.off, args.on)}
    labels = (('launched', 'Planes launched a game'), ('down_share', 'Flights shot down, %'),
              ('approach', 'Enemy plane approaches to an Airport (16 tiles) a game'),
              ('down_before', '... shot down before reaching it'),
              ('down_before_share', '... shot down before reaching it, % (per game)'),
              ('reached', '... reached it (over it)'), ('reached_share', '... reached it, % (per game)'),
              ('down_after', '... reached it, then shot down within 16 tiles'),
              ('got_away', '... reached it and got away'),
              ('airport_bombs', 'Bombs on Airports a game'), ('airports_lost', 'Airports destroyed by bombs a game'),
              ('buildings_lost', 'Buildings lost to planes (bombs and crashes) a game'),
              ('guard_turrets', 'Airport ring turrets ordered a game'),
              ('guard_credits', 'Credits on the ring a game (turrets, foundations, Wind Traps)'),
              ('timeout', 'Time-outs (share of games)'), ('minutes', 'Game length, minutes'))
    report += ['| Measure | Off | On | Paired on - off | 95% interval |', '|---|---:|---:|---:|---|']
    for key, label in labels:
        off = [m[args.off][p][key] for p in pairs]
        on = [m[args.on][p][key] for p in pairs]
        low, high = match_score.bootstrap([b - a for a, b in zip(off, on)], SEED)
        report.append('| %s | %.2f | %.2f | %+.2f | %+.2f to %+.2f |' % (
            label, statistics.mean(off), statistics.mean(on), statistics.mean(on) - statistics.mean(off), low, high))
    for tag, title in ((args.off, 'Off'), (args.on, 'On')):
        approaches = sum(m[tag][p]['approach'] for p in pairs)
        before = sum(m[tag][p]['down_before'] for p in pairs)
        reached = sum(m[tag][p]['reached'] for p in pairs)
        after = sum(m[tag][p]['down_after'] for p in pairs)
        report.append('')
        report.append('%s, pooled: %d approaches; %d (%.0f%%) shot down before reaching the Airport, %d (%.0f%%) '
                      'reached it, of which %d (%.0f%%) were shot down near it afterwards.' % (
                          title, approaches, before, 100.0 * before / max(1, approaches), reached,
                          100.0 * reached / max(1, approaches), after, 100.0 * after / max(1, reached)))
    minutes = [int(x) for x in args.minutes.split(',')]
    report += ['', '## Rocket Turrets at each Airport (one sample a game minute per Airport)', '',
               '| Minute | Arm | Airports sampled | Ring turrets (median, within 5 tiles) | Airports with 4+ in the ring, % '
               '| Turrets reaching a plane over it (median, 16 tiles) | Powered, % |', '|---:|---|---:|---:|---:|---:|---:|']
    tables = {tag: cover_table(tag, pairs, minutes) for tag in (args.off, args.on)}
    for minute in minutes:
        for tag, title in ((args.off, 'Off'), (args.on, 'On')):
            row = tables[tag].get(minute)
            if row:
                report.append('| %d | %s | %d | %g | %.0f | %g | %.0f |' % ((minute, title) + row))
    report += ['', '## Placement in the self-play arms (seat check only: every house plays the same rule)', '',
               '| House | Off | On | Paired | 95% interval |', '|---|---:|---:|---:|---|']
    seat = defaultdict(lambda: ([], []))
    for p in pairs:
        for i, tag in enumerate((args.off, args.on)):
            for house, (label, placing) in placings(tag, p)[0].items():
                seat[house][i].append(placing.score)
    for house, (off, on) in sorted(seat.items()):
        if len(off) == len(on) and off:
            low, high = match_score.bootstrap([b - a for a, b in zip(off, on)], SEED)
            report.append('| %s | %+.2f | %+.2f | %+.2f | %+.2f to %+.2f |' % (
                house, statistics.mean(off), statistics.mean(on), statistics.mean(on) - statistics.mean(off), low, high))

if args.mixed:
    mixed = sorted(p for (t, p) in games if t == args.mixed and ok(args.mixed, p))
    report += ['', '## Mixed FFA: %s against default, half the houses each (%s, %d games)'
               % (ON_LABEL, args.mixed, len(mixed)), '',
               match_score.describe('placement', match_score.DRAW_SCORE, 0), '']
    comparisons = []
    timeouts = 0
    lost = defaultdict(float)
    credits = 0.0
    for p in mixed:
        placing, timeout = placings(args.mixed, p)
        timeouts += timeout
        label_of = {HOUSE_NUMBERS[h]: lab for h, (lab, _) in placing.items()}
        on_scores = [pl.score for lab, pl in placing.values() if lab == ON_LABEL]
        off_scores = [pl.score for lab, pl in placing.values() if lab != ON_LABEL]
        if on_scores and off_scores:
            comparisons.append((statistics.mean(on_scores), statistics.mean(off_scores),
                                any(pl.drawn for lab, pl in placing.values() if lab == ON_LABEL),
                                any(pl.drawn for lab, pl in placing.values() if lab != ON_LABEL)))
        for row in events[(args.mixed, p)]:
            target = label_of.get(int(num(row.get('target_house'), -1)), '')
            if row['event'] in ('kill', 'crash_kill'):
                lost[('buildings', target == ON_LABEL)] += 1
                if row['event'] == 'kill' and row['target_item'] == AIRPORT_ITEM:
                    lost[('airports', target == ON_LABEL)] += 1
            if row['event'] == 'airport_guard':
                credits += num(row['price'])
    c = match_score.compare(comparisons, SEED)
    if c:
        n = max(1, len(mixed))
        report += ['| Arm | Mean placement score | Alive at a time-out | Buildings lost to planes a game (2 houses) '
                   '| Airports lost to bombs a game |', '|---|---:|---:|---:|---:|',
                   '| %s | %+.3f | %.0f%% | %.1f | %.2f |' % (ON_LABEL, c.candidate, 100 * c.candidate_draws,
                                                            lost[('buildings', True)] / n, lost[('airports', True)] / n),
                   '| default | %+.3f | %.0f%% | %.1f | %.2f |' % (c.control, 100 * c.control_draws,
                                                                  lost[('buildings', False)] / n, lost[('airports', False)] / n),
                   '',
                   'Paired difference %+.3f (95%% interval %+.3f to %+.3f): the candidate %s the control. '
                   'Time-outs: %.0f%% of games. Ring credits: %.0f a game.'
                   % (c.difference, c.low, c.high, c.verdict, 100.0 * timeouts / n, credits / n)]

text = '\n'.join(report) + '\n'
print(text)
if args.output:
    args.output.write_text(text)
