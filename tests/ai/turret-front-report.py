#!/usr/bin/env python3
"""Rocket Turrets at the front (include/players/TurretFrontPolicy.h): one table per arm from
vanilla-match.py results.

    turret-front-report.py --results DIR [DIR ...] [--control default] [--output report.md]

Every label in the results is an arm; --control (default "default") is what each other label is
compared with. Mixed games (labels sharing a game) give the paired placement score of
tests/ai/match_score.py: per game, the candidate houses' mean score minus the control houses' mean,
with the 95% bootstrap interval. Self-play games (one label for every house) give no score, only the
economy columns; name them with --pair TAG_ON:TAG_OFF.

Per house-game means:
  turrets built/lost   UNIT_ECONOMY Rocket Turret rows (built excludes turrets the map started with)
  upkeep               police upkeep the house's Rocket Turrets drew, from TURRET_LEDGER every two
                       minutes (turret_upkeep_milli_per_min x minutes)
  return               Rocket Turret damage value / (price x built) - the end-of-game figure - and
                       / (price x built + upkeep): what a credit spent on turrets bought
  destroyed            houses.csv kill_value (enemy value destroyed)
  credits, population  the last TURRET_LEDGER line
  front, retired       TURRET_FRONT orders and TURRET_RETIRE turrets per house-game

Activation (CLAUDE.md, "AI behaviours: prove the switch actually does its job"), a table per map:
for each arm and rule (front_reinforce, turret_creep, turret_advance, turret_drill, retire), the share of the arm's
house-games in which it fired, its orders per house-game, and how many of the ordered turrets stood
at their site (TURRET_FRONT_PLACED) and fired on a unit (TURRET_FRONT_ENGAGED). An arm whose rules
fired in fewer than half of its house-games, or whose ordered turrets mostly never stood, is
"not tested", whatever its score.
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
TURRET_ITEM = '12'
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5,
                 'neutral': 6}
LEDGER_MINUTES = 2.0
FRONT_RULES = ('front_reinforce', 'turret_creep', 'turret_advance', 'turret_drill')
# The rules each play style switches on (its arm label default@<style>).
ARM_RULES = {'front-turrets': ('front_reinforce',), 'advancing-turrets': ('turret_advance',),
             'turret-creep': ('turret_creep',), 'rear-turret-retire': ('retire',),
             'moving-line': ('turret_advance', 'retire'),
             'turret-front-all': ('front_reinforce', 'turret_creep', 'turret_advance', 'retire'),
             'turret-drill': ('turret_drill',)}


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
    games, houses, units, turrets = [], [], [], []
    for directory in directories:
        games += rows(directory, 'games.csv')
        houses += rows(directory, 'houses.csv')
        units += rows(directory, 'units.csv')
        turrets += rows(directory, 'turrets.csv')
    key = lambda row: tuple(row[k] for k in KEY)  # noqa: E731
    played = {key(g) for g in games if not g.get('error')}
    per = collections.defaultdict(dict)
    for h in houses:
        if key(h) in played:
            per[(key(h), h['house'])].update(label=h['label'], tag=h['tag'], map=h['map'],
                                             kill=number(h['kill_value']), loss=number(h['loss_value']),
                                             eliminated=h['eliminated_s'] or None)
    names = {v: k for k, v in HOUSE_NUMBERS.items()}
    economy = {}
    for u in units:
        if u['item'] != TURRET_ITEM:
            continue
        slot = (key(u), names.get(int(number(u['house'], -1)), u['house']))
        if slot not in economy or u['phase'] == 'end':   # the game's own end line wins
            economy[slot] = u
    for slot, u in economy.items():
        if slot in per:
            per[slot].update(built=number(u['built']), lost=number(u['lost']), damage=number(u['damage_value']))
    ledgers = collections.defaultdict(list)
    for t in turrets:
        slot = (key(t), t['house'])
        if slot not in per:
            continue
        if t['event'] == 'ledger':
            ledgers[slot].append(t)
        elif t['event'] == 'front':
            per[slot][t['rule']] = per[slot].get(t['rule'], 0) + 1
        elif t['event'] in ('placed', 'engaged', 'missed'):
            per[slot][t['event'] + ':' + t['rule']] = per[slot].get(t['event'] + ':' + t['rule'], 0) + 1
        elif t['event'] == 'retire':
            per[slot]['retired'] = per[slot].get('retired', 0) + 1
    for slot, lines in ledgers.items():
        lines.sort(key=lambda t: number(t['cycle']))
        per[slot].update(upkeep=sum(number(t['turret_upkeep_milli_per_min']) * LEDGER_MINUTES / 1000.0 for t in lines),
                         credits=number(lines[-1]['credits']), population=number(lines[-1]['population']),
                         standing=number(lines[-1]['rocket_turrets']))
    return per


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--results', nargs='+', required=True, type=Path)
    parser.add_argument('--control', default='default')
    parser.add_argument('--pair', action='append', default=[], help='Self-play: TAG_ON:TAG_OFF')
    parser.add_argument('--output', type=Path)
    match_score.add_arguments(parser)
    args = parser.parse_args()
    per = collect(args.results)

    by_game = collections.defaultdict(list)
    for (game, house), m in per.items():
        by_game[game].append((house, m))
    timeouts = collections.defaultdict(list)
    pairs = collections.defaultdict(list)
    for game, members in by_game.items():
        eliminated = {house: number(m['eliminated']) for house, m in members if m['eliminated']}
        if len(members) < 2:
            continue
        placings = match_score.score_game([house for house, _ in members], eliminated, args.draw_score)
        timed_out = sum(1 for house, _ in members if house not in eliminated) > 1
        labels = {m['label'] for _, m in members}
        for house, m in members:
            m['score'], m['drawn'], m['timed_out'] = placings[house].score, placings[house].drawn, timed_out
        for label in labels:
            timeouts[label].append(timed_out)
        if args.control in labels and len(labels) > 1:
            control = [m for _, m in members if m['label'] == args.control]
            for label in labels - {args.control}:
                cand = [m for _, m in members if m['label'] == label]
                pairs[label].append((members[0][1], (statistics.mean(m['score'] for m in cand),
                                     statistics.mean(m['score'] for m in control),
                                     any(m['drawn'] for m in cand), any(m['drawn'] for m in control))))

    def table(select, title):
        out = [f'\n## {title}\n',
               '| arm | games | score diff vs control [95%] | verdict | time-outs | turrets built | lost | standing | '
               'upkeep paid | return (listed) | return with upkeep | destroyed | credits | population | '
               'front orders | retired |',
               '|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
        chosen = {slot: m for slot, m in per.items() if select(m)}
        labels = sorted({m['label'] for m in chosen.values()}, key=lambda l: (l != args.control, l))
        for label in labels:
            members = [m for m in chosen.values() if m['label'] == label]
            n_games = len({g for (g, _), m in chosen.items() if m['label'] == label})
            arm_pairs = [p for first, p in pairs.get(label, []) if select(first)]
            comparison = match_score.compare(arm_pairs, 2026) if arm_pairs else None
            diff = (f'{comparison.difference:+.3f} [{comparison.low:+.3f}, {comparison.high:+.3f}]'
                    if comparison else '-')
            avg = lambda f: statistics.mean([m[f] for m in members if f in m]) if any(f in m for m in members) else float('nan')  # noqa: E731
            built, damage, upkeep = (sum(m.get(f, 0.0) for m in members) for f in ('built', 'damage', 'upkeep'))
            listed = damage / (250.0 * built) if built else float('nan')
            with_upkeep = damage / (250.0 * built + upkeep) if (built or upkeep) else float('nan')
            timed = [m['timed_out'] for m in members if 'timed_out' in m]
            n = max(1, len(members))
            front = sum(m.get(r, 0) for m in members for r in FRONT_RULES)
            out.append(f'| {label} | {n_games} | {diff} | {comparison.verdict if comparison else "-"} | '
                       f'{(sum(timed) / len(timed)) if timed else float("nan"):.0%} | {avg("built"):.0f} | {avg("lost"):.0f} | '
                       f'{avg("standing"):.0f} | {avg("upkeep"):.0f} | {listed:.2f}x | {with_upkeep:.2f}x | '
                       f'{avg("kill"):.0f} | {avg("credits"):.0f} | {avg("population"):.0f} | {front / n:.1f} | '
                       f'{sum(m.get("retired", 0) for m in members) / n:.1f} |')
        return out

    def activation(select, title):
        out = [f'\n## Activation: {title}\n',
               '| arm | rule | house-games | fired in | orders / house-game | placed | engaged | missed | tested |',
               '|---|---|---:|---:|---:|---:|---:|---:|---|']
        chosen = [m for m in per.values() if select(m)]
        for label in sorted({m['label'] for m in chosen}):
            style = label.split('@', 1)[1] if '@' in label else ''
            if style not in ARM_RULES:
                continue
            members = [m for m in chosen if m['label'] == label]
            verdicts = []
            for rule in ARM_RULES[style]:
                key = 'retired' if rule == 'retire' else rule
                fired = sum(1 for m in members if m.get(key, 0) > 0)
                orders = sum(m.get(key, 0) for m in members)
                placed = sum(m.get('placed:' + rule, 0) for m in members)
                engaged = sum(m.get('engaged:' + rule, 0) for m in members)
                missed = sum(m.get('missed:' + rule, 0) for m in members)
                share = fired / len(members) if members else 0.0
                ok = share >= 0.5 and (rule == 'retire' or (orders > 0 and placed >= 0.5 * orders))
                verdicts.append(ok)
                effect = '-' if rule == 'retire' else f'{placed} ({placed / orders:.0%})' if orders else '0'
                hits = '-' if rule == 'retire' else f'{engaged} ({engaged / placed:.0%})' if placed else '0'
                out.append(f'| {label} | {rule} | {len(members)} | {share:.0%} | {orders / max(1, len(members)):.1f} | '
                           f'{effect} | {hits} | {"-" if rule == "retire" else missed} | {"yes" if ok else "**not tested**"} |')
            if verdicts and not all(verdicts):
                out.append(f'| {label} | (arm) | | | | | | | **not tested** |')
        return out

    ALL = lambda m: True  # noqa: E731
    out = ['# Rocket Turrets at the front', '', match_score.describe(args.scoring, args.draw_score, 0)]
    out += table(ALL, 'All games')
    for map_name in sorted({m['map'] for m in per.values()}):
        out += table(lambda m, mp=map_name: m['map'] == mp, f'{map_name}')
        out += activation(lambda m, mp=map_name: m['map'] == mp, f'{map_name}')
    for spec in args.pair:
        on, off = spec.split(':', 1)
        out += table(lambda m, tags=(on, off): m['tag'] in tags, f'Self-play {on} against {off}')
    text = '\n'.join(out) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text)


if __name__ == '__main__':
    main()
