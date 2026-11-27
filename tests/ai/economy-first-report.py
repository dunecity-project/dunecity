#!/usr/bin/env python3
"""EconomyFirst: each arm's economy against Stefan's last Air game, and where the money went.

    economy-first-report.py --results DIR [DIR ...] [--output report.md]

Reads lessons.csv(.gz) from vanilla-match.py results (ECONOMY_STATE, ECONOMY_FIRST and BUILD_ORDER lines; the
control houses log them when the games ran with --env DUNECITY_TEST_ECONOMY_LOG=1).

Stefan's footprint (2026-11-28, Air game, 4P 192x192 Random 1v3 FFA DuneCity T9, seed 817478863, read from
the replay's orders and the game log): yards 4 by minute 3 (MCVs deployed at 1.7-2.2), 6 by 17.4, 9 by 22.7;
zones ordered beyond the map's 92: 76 by minute 10, 201 by 20, 328 by 31; Nuclear Plants ordered 1 / 5 / 10;
Palaces 6 / 8 / 15; population 3,130 / 4,918 / 6,734.

Per arm and minute: median yards, zones added since the first log line, Nuclear Plants, Palaces, population,
tax per minute, credits and unit value built. Then: activation (ECONOMY_FIRST pass lines, footprint met=1 at
minute 10, house-games), the first two minutes' spending (units vs structures), and the yard rules that took
the orders in the first ten minutes.
"""
import argparse, collections, csv, gzip, statistics
from pathlib import Path

CYCLES_PER_MINUTE = 3750
MARKS = (3, 10, 20, 31)
STEFAN = {3: dict(yards=4, zones=19, nuclear=0, palaces=3, population=2060),
          10: dict(yards=4, zones=76, nuclear=1, palaces=6, population=3130),
          20: dict(yards=6, zones=201, nuclear=5, palaces=8, population=4918),
          31: dict(yards=9, zones=328, nuclear=10, palaces=15, population=6734)}
KEY = ('tag', 'difficulty', 'map', 'seed', 'sides', 'house')


def rows(directory):
    for path in (Path(directory) / 'lessons.csv', Path(directory) / 'lessons.csv.gz'):
        if path.exists():
            with (gzip.open if path.suffix == '.gz' else open)(path, 'rt', newline='') as handle:
                yield from csv.DictReader(handle)


def num(value, default=0):
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--results', nargs='+', required=True)
    parser.add_argument('--output')
    args = parser.parse_args()
    states = collections.defaultdict(list)
    first = collections.defaultdict(lambda: collections.Counter())
    orders = collections.defaultdict(list)
    label_of = {}
    for directory in args.results:
        for r in rows(directory):
            key = tuple(r[k] for k in KEY)
            label_of[key] = r['label']
            if r['kind'] == 'economy_state':
                states[key].append(r)
            elif r['kind'] == 'economy_first':
                first[key][r['event'] + ('_met' if r['event'] == 'footprint' and r['met'] == '1' else '')] += 1
            elif r['kind'] == 'build_order':
                orders[key].append(r)
    out = ['# EconomyFirst: economy against Stefan\'s Air game', '']
    by_label = collections.defaultdict(list)
    for key in set(states) | set(orders):
        by_label[label_of[key]].append(key)
    labels = sorted(by_label, key=lambda l: (l != 'default', l))
    out += ['| arm | minute | house-games | yards | zones added | Nuclear | Palaces | population | tax/min | credits | unit value built |',
            '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for m in MARKS:
        s = STEFAN[m]
        out.append(f"| Stefan | {m} | 1 | {s['yards']} | {s['zones']} | {s['nuclear']} | {s['palaces']} | {s['population']} | - | - | - |")
    for label in labels:
        for m in MARKS:
            picked = []
            for key in by_label[label]:
                lines = sorted(states[key], key=lambda r: num(r['cycle']))
                if not lines:
                    continue
                start = lines[0]
                at = [r for r in lines if num(r['cycle']) <= m * CYCLES_PER_MINUTE]
                if not at or num(at[-1]['cycle']) < (m - 2) * CYCLES_PER_MINUTE:
                    continue
                r = at[-1]
                zones = lambda x: num(x['zones_r']) + num(x['zones_c']) + num(x['zones_i'])  # noqa: E731
                picked.append(dict(yards=num(r['yards']), zones=zones(r) - zones(start), nuclear=num(r['nuclear']),
                                   palaces=num(r['palaces']), population=num(r['population']),
                                   tax=num(r['tax_per_min']), credits=num(r['credits']), units=num(r['unit_built'])))
            if not picked:
                continue
            med = lambda f: statistics.median(p[f] for p in picked)  # noqa: E731
            out.append(f"| {label} | {m} | {len(picked)} | {med('yards'):.0f} | {med('zones'):.0f} | {med('nuclear'):.0f} | "
                       f"{med('palaces'):.0f} | {med('population'):.0f} | {med('tax'):.0f} | {med('credits'):.0f} | {med('units'):.0f} |")
    out += ['', '## Activation', '', '| arm | house-games | fired (ECONOMY_FIRST pass) | footprint met at minute 10 (>= 38 zones added, 4 yards) | stood down | spice-rich (did not act) | map spice at start (median) |',
            '|---|---:|---:|---:|---:|---:|---:|']
    for label in labels:
        keys = by_label[label]
        fired = sum(first[k]['pass'] > 0 for k in keys)
        met = sum(first[k]['footprint_met'] > 0 for k in keys)
        down = sum(first[k]['stood_down'] > 0 for k in keys)
        rich = sum(first[k]['spice_rich'] > 0 for k in keys)
        spice = [num(min(states[k], key=lambda r: num(r['cycle'])).get('spice')) for k in keys if states[k]]
        verdict = '' if label == 'default' else (' (tested)' if met * 2 >= len(keys) else ' **NOT TESTED**')
        out.append(f'| {label} | {len(keys)} | {fired} | {met}{verdict} | {down} | {rich} | '
                   f'{statistics.median(spice) if spice else "-"} |')
    out += ['', '## Where the money went: orders in the first 2 and 10 minutes (median credits per house-game)', '',
            '| arm | units 0-2 | structures 0-2 | MCVs 0-10 | units 0-10 | zones 0-10 | Nuclear 0-10 |', '|---|---:|---:|---:|---:|---:|---:|']
    for label in labels:
        vals = collections.defaultdict(list)
        for key in by_label[label]:
            o = orders[key]
            if not o:
                continue
            def spend(mins, pred):
                return sum(num(r['price']) for r in o if num(r['cycle']) < mins * CYCLES_PER_MINUTE and pred(r))
            unit = lambda r: 27 <= num(r['item']) <= 47 or 50 <= num(r['item']) <= 55  # noqa: E731
            vals['u2'].append(spend(2, unit))
            vals['s2'].append(spend(2, lambda r: not unit(r)))
            vals['mcv'].append(sum(1 for r in o if num(r['cycle']) < 10 * CYCLES_PER_MINUTE and num(r['item']) == 34))
            vals['u10'].append(spend(10, lambda r: unit(r) and num(r['item']) != 34))
            vals['z10'].append(sum(1 for r in o if num(r['cycle']) < 10 * CYCLES_PER_MINUTE and num(r['item']) in (20, 21, 22)))
            vals['n10'].append(sum(1 for r in o if num(r['cycle']) < 10 * CYCLES_PER_MINUTE and num(r['item']) == 25))
        if vals:
            med = lambda f: statistics.median(vals[f])  # noqa: E731
            out.append(f"| {label} | {med('u2'):.0f} | {med('s2'):.0f} | {med('mcv'):.0f} | {med('u10'):.0f} | {med('z10'):.0f} | {med('n10'):.0f} |")
    out += ['', '## Yard rules that took the structure orders, minutes 0-10 (all house-games of the arm)', '']
    for label in labels:
        rules = collections.Counter()
        for key in by_label[label]:
            for r in orders[key]:
                if r['producer'] == '2' and num(r['cycle']) < 10 * CYCLES_PER_MINUTE:
                    rules[f"{r['rule']}:{r['item']}"] += 1
        out.append(f'- {label}: ' + ', '.join(f'{k} {v}' for k, v in rules.most_common(12)))
    text = '\n'.join(out) + '\n'
    if args.output:
        Path(args.output).write_text(text)
    print(text)


main()
