#!/usr/bin/env python3
"""Report the Fremen Palace worm in AI matches: the old Fremen warriors (control) against the worm.

Reads the tables vanilla-match.py writes (games.csv, houses.csv, worms.csv) for a control tag played
with tests/ai/game-options/fremen-worm-off.ini and one or more candidate tags played on the same maps,
seeds and sides with fremen-worm-on.ini (each perhaps with its own PalaceWorm play style):

  python3 tests/ai/vanilla-match.py --tag worm-off --game-options tests/ai/game-options/fremen-worm-off.ini \\
      --maps rs160-dunecity --ffa default --houses fremen harkonnen atreides ordos ...
  python3 tests/ai/vanilla-match.py --tag worm-on  --game-options tests/ai/game-options/fremen-worm-on.ini ...
  python3 tests/ai/palace-worm-report.py --results DIR --control worm-off --candidates worm-on worm-on-0 worm-on-2

Per arm: worms summoned, meals (and the credit value eaten), blooms, how the worms ended (bloom,
expired, sated, beaten), the time-out rate and the Fremen house's placement score. Per arm and map,
the activation: in how many Fremen house-games the worm was summoned, hunted, ate and bloomed, how
many of its hunts led to a meal and its bloom orders to a burst, and how many orders other
controllers gave it; an arm summoning in fewer than half its house-games, or whose orders mostly
failed, is reported NOT TESTED
(tests/ai/match_score.py). The paired difference candidate - control is per game (same map, seed
and sides) with a 95% bootstrap interval: the candidate is a better Fremen special only when the
whole interval of the Fremen placement difference is above 0 (the default rule). A match diverges
from its pair at the first Palace charge, so pairing removes the start position and map only.
"""
import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_score  # noqa: E402

# Arms may differ only in a play style the label carries (default@palace-worm-0), so the sides are
# not part of the pairing key; every arm plays the same houses in the same slots.
PAIR = ('difficulty', 'map', 'seed')
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
FREMEN = HOUSE_NUMBERS['fremen']
ENDINGS = ('bloom', 'expired', 'sated', 'beaten')

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', type=Path, nargs='+', required=True)
parser.add_argument('--control', default='worm-off', help='Tag of the games played with the rule off (warriors)')
parser.add_argument('--candidates', nargs='+', default=['worm-on'], help='Tags played with the rule on')
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


tags = [args.control] + args.candidates
games = {(row['tag'], pair_of(row)): row for row in read('games.csv') if row['tag'] in tags}
houses = defaultdict(list)
for row in read('houses.csv'):
    houses[(row['tag'], pair_of(row))].append(row)
events = defaultdict(list)
for row in read('worms.csv'):
    events[(row['tag'], pair_of(row))].append(row)


def ok(tag, pair):
    return (tag, pair) in games and not games[(tag, pair)].get('error')


def game_stats(tag, pair):
    game = games[(tag, pair)]
    rows = [row for row in events[(tag, pair)] if row['house'] == str(FREMEN)]
    timeout = game['winner'] in ('', '-1')
    rows_h = houses[(tag, pair)]
    eliminated = {HOUSE_NUMBERS[r['house']]: float(r['eliminated_s']) for r in rows_h if r['eliminated_s'] != ''}
    if not timeout:
        for r in rows_h:
            if HOUSE_NUMBERS[r['house']] != int(game['winner']):
                eliminated.setdefault(HOUSE_NUMBERS[r['house']], float(game['end_s'] or 0))
    placing = match_score.score_game([HOUSE_NUMBERS[r['house']] for r in rows_h], eliminated) if len(rows_h) > 1 else {}
    meals = [row for row in rows if row['event'] == 'ate']
    return dict(summoned=sum(row['event'] == 'summoned' for row in rows), meals=len(meals),
                eaten=sum(float(row['price'] or 0) for row in meals),
                **{end: sum(row['event'] == end for row in rows) for end in ENDINGS},
                timeout=timeout, minutes=float(game['end_s'] or 0) / 60,
                fremen=placing[FREMEN].score if FREMEN in placing else None,
                fremen_won=game['winner'] == str(FREMEN))


def activation(tag, pairs):
    """How often the worm acted per Fremen house-game, and how often its actions reached their effect
    (AGENTS.md "AI behaviours: prove the switch actually does its job"). Read from the PALACE_WORM lines."""
    out = []
    for map_name in sorted({p[1] for p in pairs}):
        mine = [p for p in pairs if p[1] == map_name]
        per = []
        hunted = hunted_ate = ordered = burst = redirected = cancelled = 0
        for pair in mine:
            rows = [r for r in events[(tag, pair)] if r['house'] == str(FREMEN)]
            count = defaultdict(int)
            for r in rows:
                count[r['event']] += 1
            per.append(count)
            redirected += count['redirected']
            cancelled += count['bloom_cancelled']
            by_worm = defaultdict(list)
            for r in rows:
                by_worm[r['object']].append(r['event'])
            for seq in by_worm.values():
                if 'hunt' in seq:
                    hunted += 1
                    hunted_ate += any(e == 'ate' for e in seq[seq.index('hunt'):])
                if 'bloom_order' in seq:
                    ordered += 1
                    burst += 'bloom' in seq
        n = len(per)
        share = lambda e: sum(c[e] > 0 for c in per)
        mean = lambda e: sum(c[e] for c in per) / n
        fired = share('summoned')
        hunt_rate = hunted_ate / hunted if hunted else None
        bloom_rate = burst / ordered if ordered else None
        failed = (hunt_rate is not None and hunt_rate < 0.5) and (bloom_rate is None or bloom_rate < 0.5)
        verdict = 'tested' if n and fired * 2 >= n and not failed else 'NOT TESTED'
        out.append(f'  {tag} {map_name}: {n} Fremen house-games; summoned in {fired}, hunted in {share("hunt")}, '
                   f'ate in {share("ate")}, bloomed in {share("bloom")}; per house-game {mean("summoned"):.1f} worms, '
                   f'{mean("hunt"):.1f} hunt orders, {mean("ate"):.1f} meals, {mean("bloom_order"):.1f} bloom orders, '
                   f'{mean("bloom"):.1f} blooms')
        out.append(f'    effect: {hunted_ate}/{hunted} hunting worms ate'
                   + (f' ({100 * hunt_rate:.0f}%)' if hunt_rate is not None else '')
                   + f', {burst}/{ordered} bloom orders burst' + (f' ({100 * bloom_rate:.0f}%)' if bloom_rate is not None else '')
                   + f', {cancelled} bloom trips cancelled; {redirected} orders from other controllers; activation: {verdict}')
    return out


lines = []
for candidate in args.candidates:
    pairs = sorted(pair for tag, pair in games if tag == candidate and ok(candidate, pair) and ok(args.control, pair))
    if not pairs:
        lines += [f'{candidate}: no game played in both arms.', '']
        continue
    stats = {tag: {pair: game_stats(tag, pair) for pair in pairs} for tag in (args.control, candidate)}
    lines += [f'Fremen Palace worm: {candidate} against {args.control} (warriors), {len(pairs)} paired games',
              f'maps: {", ".join(sorted({p[1] for p in pairs}))}; difficulty: {", ".join(sorted({p[0] for p in pairs}))}',
              'arm           worms  meals  value eaten  bloom  expired  sated  beaten  Fremen wins  time-outs  '
              'mean minutes  Fremen score']
    for tag in (args.control, candidate):
        arm = list(stats[tag].values())
        scores = [g['fremen'] for g in arm if g['fremen'] is not None]
        lines.append(f'{tag:<12}  {sum(g["summoned"] for g in arm):>5}  {sum(g["meals"] for g in arm):>5}  '
                     f'{sum(g["eaten"] for g in arm):>11.0f}  '
                     + '  '.join(f'{sum(g[e] for g in arm):>{len(e) if len(e) > 5 else 5}}' for e in ENDINGS)
                     + f'  {sum(g["fremen_won"] for g in arm):>11}  {100 * statistics.mean(g["timeout"] for g in arm):>8.1f}%'
                     f'  {statistics.mean(g["minutes"] for g in arm):>12.1f}  '
                     f'{statistics.mean(scores) if scores else float("nan"):>+12.3f}')

    def difference(text, value, digits=3):
        values = [value(stats[candidate][p]) - value(stats[args.control][p]) for p in pairs
                  if value(stats[candidate][p]) is not None and value(stats[args.control][p]) is not None]
        if len(values) < 2:
            lines.append(f'  {text}: not enough games ({len(values)})')
            return
        low, high = match_score.bootstrap(values, 20261127)
        verdict = 'higher' if low > 0 else 'lower' if high < 0 else 'no difference shown'
        lines.append(f'  {text}: {statistics.mean(values):+.{digits}f} ({low:+.{digits}f} to {high:+.{digits}f}), '
                     f'{len(values)} games: {verdict}')

    lines.append('Activation per arm and map (the warriors arm must show no worm):')
    lines += activation(candidate, pairs)
    control_worms = sum(1 for pair in pairs for r in events[(args.control, pair)] if r['event'] == 'summoned')
    lines.append(f'  {args.control}: {control_worms} worms summoned' + ('' if control_worms == 0 else ' -- THE RULE WAS NOT OFF'))
    lines.append(f'Paired difference {candidate} - {args.control} per game (95% bootstrap interval):')
    difference('Fremen placement score', lambda g: g['fremen'])
    difference('Fremen won (1/0)', lambda g: 1.0 if g['fremen_won'] else 0.0)
    difference('time-out (1/0)', lambda g: 1.0 if g['timeout'] else 0.0)
    difference('game minutes', lambda g: g['minutes'], 1)
    lines.append('')

text = '\n'.join(lines)
print(text)
if args.output:
    args.output.write_text(text + '\n')
