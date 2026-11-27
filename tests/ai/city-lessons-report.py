#!/usr/bin/env python3
"""Lessons from Stefan's Metropolis save (include/players/CityLessonsPolicy.h): score and activation of
each arm from vanilla-match.py results.

    city-lessons-report.py --results DIR [DIR ...] [--control default] [--output report.md]

Every label in the results is an arm; --control (default "default") is what each other label is
compared with: per mixed game, the arm houses' mean placement score minus the control houses' mean
(tests/ai/match_score.py), with the 95% bootstrap interval.

Activation, per house-game of an arm (CLAUDE.md "AI behaviours: prove the switch actually does its job"):
  launcher fired     house-games with at least one LAUNCHER_ARMY line (the mix held), and passes per game
  launcher effect    of the vehicle value the house built, the Launcher share (mix.csv, final row), and
                     house-games where it reached 50% with no Ornithopter bought
  palace planned/placed/expired/effect   LOW_VALUE_PALACE events per house-game
  palace fired       house-games with at least one Palace placed on its planned site
  palace effect      mean land-value rise of the planned lots two minutes after placement, and the
                     share of effects that rose by 10 or more with the Palace still standing
An arm whose behaviour fired in fewer than half its house-games is NOT TESTED, whatever its score.
"""
import argparse
import collections
import csv
import gzip
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_score  # noqa: E402

KEY = ('tag', 'difficulty', 'map', 'seed', 'sides')
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5,
                 'neutral': 6}
VEHICLE_SLOTS = 10
LAUNCHER_SLOT, ORNITHOPTER_SLOT = 2, 6
MIN_RISE = 10


def rows(directory, name):
    for path in (directory / name, directory / (name + '.gz')):
        if path.exists():
            opener = gzip.open if path.suffix == '.gz' else open
            with opener(path, 'rt', newline='') as handle:
                return list(csv.DictReader(handle))
    return []


def number(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def collect(directories):
    games, houses, lessons, mixes = [], [], [], []
    for directory in directories:
        games += rows(directory, 'games.csv')
        houses += rows(directory, 'houses.csv')
        lessons += rows(directory, 'lessons.csv')
        mixes += rows(directory, 'mix.csv')
    key = lambda row: tuple(row[k] for k in KEY)  # noqa: E731
    played = {key(g) for g in games if not g.get('error')}
    per = {}
    for h in houses:
        if key(h) in played:
            per[(key(h), h['house'])] = dict(label=h['label'], tag=h['tag'], map=h['map'],
                                             eliminated=h['eliminated_s'] or None, launcher_passes=0,
                                             planned=0, placed=0, expired=0, effects=[])
    names = {v: k for k, v in HOUSE_NUMBERS.items()}
    for row in lessons:
        house = row['house'] if row['house'] in HOUSE_NUMBERS else names.get(int(number(row['house'], -1)), row['house'])
        slot = (key(row), house)
        if slot not in per:
            continue
        m = per[slot]
        if row['kind'] == 'launcher_army':
            m['launcher_passes'] += 1
        elif row['kind'] == 'low_value_palace':
            if row['event'] in ('planned', 'placed', 'expired'):
                m[row['event']] += 1
            elif row['event'] == 'effect':
                m['effects'].append((number(row['rise']), row['palace_stands'] == '1'))
    final = {}
    for row in mixes:
        slot = (key(row), row['house'])
        if slot in per and (slot not in final or number(row['minute']) >= number(final[slot]['minute'])):
            final[slot] = row
    for slot, row in final.items():
        built = [number(v) for v in row['built'].split('/')][:VEHICLE_SLOTS]
        total = sum(built)
        per[slot].update(vehicle_built=total, launcher_share=(built[LAUNCHER_SLOT] / total) if total else 0.0,
                         ornithopter_built=built[ORNITHOPTER_SLOT])
    return per


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--results', nargs='+', required=True, type=Path)
    parser.add_argument('--control', default='default')
    parser.add_argument('--output', type=Path)
    match_score.add_arguments(parser)
    args = parser.parse_args()
    per = collect(args.results)

    by_game = collections.defaultdict(list)
    for (game, house), m in per.items():
        by_game[game].append((house, m))
    pairs = collections.defaultdict(list)
    for game, members in by_game.items():
        if len(members) < 2:
            continue
        eliminated = {house: number(m['eliminated']) for house, m in members if m['eliminated']}
        placings = match_score.score_game([house for house, _ in members], eliminated, args.draw_score)
        timed_out = sum(1 for house, _ in members if house not in eliminated) > 1
        for house, m in members:
            m['score'], m['drawn'], m['timed_out'] = placings[house].score, placings[house].drawn, timed_out
        labels = {m['label'] for _, m in members}
        if args.control in labels and len(labels) > 1:
            control = [m for _, m in members if m['label'] == args.control]
            for label in labels - {args.control}:
                cand = [m for _, m in members if m['label'] == label]
                pairs[label].append((members[0][1], (statistics.mean(m['score'] for m in cand),
                                     statistics.mean(m['score'] for m in control),
                                     any(m['drawn'] for m in cand), any(m['drawn'] for m in control))))

    def table(select, title):
        out = [f'\n## {title}\n',
               '| arm | house-games | score diff vs control [95%] | verdict | time-outs | launcher fired | passes/game | '
               'Launcher share built | >=50% & no Orni | palace fired | planned/placed/expired per game | '
               'mean rise | rose >=10 | activation |',
               '|---|---:|---|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---|']
        chosen = {slot: m for slot, m in per.items() if select(m)}
        labels = sorted({m['label'] for m in chosen.values()}, key=lambda l: (l != args.control, l))
        for label in labels:
            members = [m for m in chosen.values() if m['label'] == label]
            n = len(members)
            arm_pairs = [p for first, p in pairs.get(label, []) if select(first)]
            comparison = match_score.compare(arm_pairs, 2026) if arm_pairs else None
            diff = (f'{comparison.difference:+.3f} [{comparison.low:+.3f}, {comparison.high:+.3f}]' if comparison else '-')
            timed = [m['timed_out'] for m in members if 'timed_out' in m]
            launcher_fired = sum(1 for m in members if m['launcher_passes'] > 0)
            launcher_ok = sum(1 for m in members if m['launcher_passes'] > 0 and m.get('launcher_share', 0) >= 0.5
                              and m.get('ornithopter_built', 0) == 0)
            palace_fired = sum(1 for m in members if m['placed'] > 0)
            effects = [e for m in members for e in m['effects']]
            rose = sum(1 for rise, stands in effects if rise >= MIN_RISE and stands)
            shares = [m['launcher_share'] for m in members if 'launcher_share' in m]
            uses_launcher = 'launcher' in label or 'lessons' in label
            uses_palace = 'palace' in label or 'lessons' in label
            verdicts = []
            if uses_launcher:
                verdicts.append('launcher ' + ('tested' if launcher_fired * 2 >= n else 'NOT TESTED'))
            if uses_palace:
                verdicts.append('palace ' + ('tested' if palace_fired * 2 >= n else 'NOT TESTED'))
            out.append(
                f'| {label} | {n} | {diff} | {comparison.verdict if comparison else "-"} | '
                f'{(sum(timed) / len(timed)) if timed else float("nan"):.0%} | {launcher_fired}/{n} | '
                f'{sum(m["launcher_passes"] for m in members) / max(1, n):.0f} | '
                f'{(statistics.mean(shares) if shares else float("nan")):.0%} | {launcher_ok}/{n} | {palace_fired}/{n} | '
                f'{sum(m["planned"] for m in members) / max(1, n):.1f}/{sum(m["placed"] for m in members) / max(1, n):.1f}/'
                f'{sum(m["expired"] for m in members) / max(1, n):.1f} | '
                f'{(statistics.mean(r for r, _ in effects) if effects else float("nan")):+.1f} | '
                f'{rose}/{len(effects)} | {", ".join(verdicts) or "-"} |')
        return out

    out = ['# Lessons from Stefan\'s Metropolis save: score and activation', '',
           match_score.describe(args.scoring, args.draw_score, 0)]
    out += table(lambda m: True, 'All games')
    for map_name in sorted({m['map'] for m in per.values()}):
        out += table(lambda m, mp=map_name: m['map'] == mp, map_name)
    text = '\n'.join(out) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text)


if __name__ == '__main__':
    main()
