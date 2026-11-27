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
  front palaces      FRONT_PALACE planned/placed per house-game, median distance of the planned sites
                     to the nearest enemy building, and Palaces still standing 5 minutes after placement
  fielded Launchers  Launchers' share of the combat vehicle value the house fields from minute 50 on
                     (mix.csv army; Stefan fielded 83%), and Launchers lost per house-game
  Palace weapon      the house's Palace damage value (units.csv; only Harkonnen's Death Hand hits)
Acceptance against Stefan's save (analysis/acceptance-list.md), per arm:
  by m72             lesson Palaces placed by game minute 72 per house-game (median; his 19, target 15) and
                     house-games reaching 15
  low-lot share m72  the house's low-value lots over its R/C lots at the last CITY_LOTS line by minute 72
                     (his 9%, target 20% or less; the AIs in his game 30-51%), and the mean land value
  expiry causes      over the expired plans, the build passes it was pending behind another Palace, held
                     saving, pre-empted, ordered, and held off its site; and which placement checks
                     the held ones failed (own units, road connections, reserved, ground access,
                     recent loss, danger, reactor)
  front no site      FRONT_PALACE no_site lines by reason (turrets: fewer than 3; band: no tile 10-31 from
                     the enemy with turrets ahead; site: no buildable site there)
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
CYCLES_PER_MINUTE = 3750
# CityLessonsPolicy::BlockCheck: the placement checks a held lesson Palace failed.
BLOCK_CHECKS = ((1, 'own_units'), (2, 'road_connections'), (4, 'reserved'), (8, 'ground_access'),
                (16, 'recent_loss'), (32, 'danger'), (64, 'reactor'))


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
    games, houses, lessons, mixes, units = [], [], [], [], []
    for directory in directories:
        games += rows(directory, 'games.csv')
        houses += rows(directory, 'houses.csv')
        lessons += rows(directory, 'lessons.csv')
        mixes += rows(directory, 'mix.csv')
        units += rows(directory, 'units.csv')
    key = lambda row: tuple(row[k] for k in KEY)  # noqa: E731
    played = {key(g) for g in games if not g.get('error')}
    per = {}
    for h in houses:
        if key(h) in played:
            per[(key(h), h['house'])] = dict(label=h['label'], tag=h['tag'], map=h['map'],
                                             eliminated=h['eliminated_s'] or None, launcher_passes=0,
                                             planned=0, placed=0, expired=0, effects=[], front_planned=0,
                                             front_placed=0, front_survived=0, front_effects=0, front_enemy=[],
                                             fielded=[], by72=0, front_by72=0, lots72=None,
                                             expiry=collections.Counter(), no_site=collections.Counter(),
                                             blocked=collections.Counter())
    names = {v: k for k, v in HOUSE_NUMBERS.items()}
    for row in lessons:
        house = row['house'] if row['house'] in HOUSE_NUMBERS else names.get(int(number(row['house'], -1)), row['house'])
        slot = (key(row), house)
        if slot not in per:
            continue
        m = per[slot]
        early = number(row.get('cycle'), 0) <= 72 * CYCLES_PER_MINUTE
        if row['kind'] == 'city_lots':
            if early:
                m['lots72'] = (number(row['lots']), number(row['low_lots']), number(row['lv_now']))
            continue
        if row['event'] == 'placed' and early:
            m['by72' if row['kind'] == 'low_value_palace' else 'front_by72'] += 1
        if row['event'] == 'expired':
            for cause in ('pending', 'saving', 'preempted', 'ordered', 'held'):
                m['expiry'][cause] += number(row.get(cause + '_passes'), 0) > 0
            m['expiry']['plans'] += 1
            for bit, name in BLOCK_CHECKS:
                m['blocked'][name] += int(number(row.get('blocked'), 0)) & bit != 0
        if row['event'] == 'no_site':
            m['no_site'][row.get('reason') or '?'] += 1
            continue
        if row['kind'] == 'launcher_army':
            m['launcher_passes'] += 1
        elif row['kind'] == 'front_palace':
            if row['event'] == 'planned':
                m['front_planned'] += 1
                m['front_enemy'].append(number(row['enemy_tiles']))
            elif row['event'] == 'placed':
                m['front_placed'] += 1
            elif row['event'] == 'effect':
                m['front_effects'] += 1
                m['front_survived'] += row['met'] == '1'
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
    for row in mixes:
        slot = (key(row), row['house'])
        if slot in per and number(row['minute']) >= 50:
            army = [number(v) for v in row['army'].split('/')][:VEHICLE_SLOTS]
            if sum(army) > 0:
                per[slot]['fielded'].append(army[LAUNCHER_SLOT] / sum(army))
    for u in units:
        slot = (key(u), names.get(int(number(u['house'], -1)), u['house']))
        if slot not in per or u['phase'] not in ('match_end', 'end'):
            continue
        if u['name'] == 'Launcher':
            per[slot]['launchers_lost'] = number(u['lost'])
        elif u['name'] == 'Palace':
            per[slot]['palace_damage'] = number(u['damage_value'])
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
               'mean rise | rose >=10 | front planned/placed per game | front enemy tiles | front standing 5 min | '
               'fielded Launcher share m50+ | Launchers lost | Palace weapon damage | activation |',
               '|---|---:|---|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---|---:|---:|---:|---:|---:|---|']
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
            uses_palace = 'low-value' in label or 'lessons' in label
            uses_front = 'front-palace' in label
            front_fired = sum(1 for m in members if m['front_placed'] > 0)
            front_enemy = sorted(d for m in members for d in m['front_enemy'])
            fielded = [statistics.mean(m['fielded']) for m in members if m['fielded']]
            fielded_ok = sum(1 for m in members if m['fielded'] and statistics.mean(m['fielded']) >= 0.6)
            verdicts = []
            if uses_launcher:
                verdicts.append('launcher ' + ('tested' if launcher_fired * 2 >= n else 'NOT TESTED'))
            if uses_palace:
                verdicts.append('palace ' + ('tested' if palace_fired * 2 >= n else 'NOT TESTED'))
            if uses_front:
                verdicts.append('front palace ' + ('tested' if front_fired * 2 >= n else 'NOT TESTED'))
            out.append(
                f'| {label} | {n} | {diff} | {comparison.verdict if comparison else "-"} | '
                f'{(sum(timed) / len(timed)) if timed else float("nan"):.0%} | {launcher_fired}/{n} | '
                f'{sum(m["launcher_passes"] for m in members) / max(1, n):.0f} | '
                f'{(statistics.mean(shares) if shares else float("nan")):.0%} | {launcher_ok}/{n} | {palace_fired}/{n} | '
                f'{sum(m["planned"] for m in members) / max(1, n):.1f}/{sum(m["placed"] for m in members) / max(1, n):.1f}/'
                f'{sum(m["expired"] for m in members) / max(1, n):.1f} | '
                f'{(statistics.mean(r for r, _ in effects) if effects else float("nan")):+.1f} | '
                f'{rose}/{len(effects)} | '
                f'{sum(m["front_planned"] for m in members) / max(1, n):.1f}/{sum(m["front_placed"] for m in members) / max(1, n):.1f} | '
                f'{front_enemy[len(front_enemy) // 2] if front_enemy else float("nan"):.0f} | '
                f'{sum(m["front_survived"] for m in members)}/{sum(m["front_effects"] for m in members)} | '
                f'{(statistics.mean(fielded) if fielded else float("nan")):.0%} ({fielded_ok}/{n} >= 60%) | '
                f'{statistics.mean([m.get("launchers_lost", 0) for m in members]):.0f} | '
                f'{statistics.mean([m.get("palace_damage", 0) for m in members]):.0f} | {", ".join(verdicts) or "-"} |')
        return out

    def acceptance(select, title):
        out = [f'\n### Acceptance, {title}\n',
               '| arm | house-games | low-value Palaces by m72 (median, range) | >= 15 by m72 | front Palaces by m72 | '
               'low-lot share m72 (median) | <= 20% | mean land value m72 | expired plans: pending/saving/preempted/ordered/held | '
               'front no site: turrets/band/site | held plans blocked by |',
               '|---|---:|---|---:|---:|---:|---:|---:|---|---|---|']
        chosen = [m for m in per.values() if select(m)]
        for label in sorted({m['label'] for m in chosen}, key=lambda l: (l != args.control, l)):
            members = [m for m in chosen if m['label'] == label]
            n = len(members)
            by72 = sorted(m['by72'] for m in members)
            shares = sorted(low / lots for lots, low, _ in (m['lots72'] for m in members if m['lots72']) if lots)
            lv = [v for _, _, v in (m['lots72'] for m in members if m['lots72'])]
            ex = sum((m['expiry'] for m in members), collections.Counter())
            ns = sum((m['no_site'] for m in members), collections.Counter())
            bl = sum((m['blocked'] for m in members), collections.Counter())
            blocked = ', '.join(f'{name} {bl[name]}' for _, name in BLOCK_CHECKS if bl[name]) or '-'
            out.append(
                f'| {label} | {n} | {statistics.median(by72):.1f} ({by72[0]}-{by72[-1]}) | {sum(b >= 15 for b in by72)}/{n} | '
                f'{statistics.median(m["front_by72"] for m in members):.1f} | '
                f'{(statistics.median(shares) if shares else float("nan")):.0%} | {sum(x <= 0.2 for x in shares)}/{len(shares)} | '
                f'{(statistics.mean(lv) if lv else float("nan")):.0f} | '
                f'{ex["plans"]}: {ex["pending"]}/{ex["saving"]}/{ex["preempted"]}/{ex["ordered"]}/{ex["held"]} | '
                f'{ns["turrets"]}/{ns["band"]}/{ns["site"]} | {blocked} |')
        return out

    out = ['# Lessons from Stefan\'s Metropolis save: score and activation', '',
           match_score.describe(args.scoring, args.draw_score, 0)]
    out += table(lambda m: True, 'All games')
    for map_name in sorted({m['map'] for m in per.values()}):
        out += table(lambda m, mp=map_name: m['map'] == mp, map_name)
    for map_name in sorted({m['map'] for m in per.values()}):
        out += acceptance(lambda m, mp=map_name: m['map'] == mp, map_name)
    text = '\n'.join(out) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text)


if __name__ == '__main__':
    main()
