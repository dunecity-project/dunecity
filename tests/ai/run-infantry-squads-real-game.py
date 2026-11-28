#!/usr/bin/env python3
"""Acceptance test of infantry squads in real games (docs/infantry-squads.md, HANDOVER "Infantry squads").

Stefan, 2026-11-28, the requests this test holds to (one ACCEPT line each):
  "in the original dune you could build three troopers for a discount - i think maybe 3 were 200
   credits?" ... "yes"                                                         -> S1
  "and the ai should use it if it wants to build these men"                    -> S2
  "also additional infantry and wor should be built if the ai needs more capacity similar to
   factories"                                                                  -> S3
  "also include infantry in the calculation for what to build (same performance algorithm)"
                                                                               -> S4
  "also both wor and infantry barracks count as a high density residential and is taxable"
                                                                               -> S5

Plays QuantBot Brutal free-for-alls on Stefan's 4P DuneCity map (rs160-dunecity) with every infantry
switch on (play style infantry-squads) through tests/ai/vanilla-match.py and asserts each line from the
INFANTRY_* and CITY_INFANTRY_HOUSING lines of the match log (infantry.csv). Each line prints
ACCEPT <id> PASS|FAIL with its measured value; any FAIL exits non-zero. The player's own path (the build
lists, one click buying three men, a Barracks placed from the yard list raising the tax base) is asserted
through the real UI in tests/units/run-infantry-squad-probe.py.
"""
import argparse
import csv
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--map', default='rs160-dunecity')
parser.add_argument('--seeds', nargs='+', type=int, default=[9101, 9102, 9103])
parser.add_argument('--minutes', type=int, default=40)
parser.add_argument('--jobs', type=int, default=3)
parser.add_argument('--reuse', action='store_true', help='Only check the tables already in --output-dir')
args = parser.parse_args()
out = args.output_dir.resolve()
TAG = 'infantry-squads'
SQUADS = {'46': ('Infantry', 50), '47': ('Troopers', 200)}   # Unit_Infantry, Unit_Troopers: price
SINGLE = {'32': 30, '44': 100}                                # Unit_Soldier, Unit_Trooper
TOP_RESIDENTIAL_TAX_EIGHTHS = 80                              # a level-3 Residential zone: 40 people x 2
LANE_ORDER_SECONDS = 60
CYCLES_PER_SECOND = 62.5

if not args.reuse:
    # A fresh folder: vanilla-match.py skips games its results already hold.
    shutil.rmtree(out, ignore_errors=True)
    command = [sys.executable, str(root / 'tests/ai/vanilla-match.py'), '--build-dir', args.build_dir,
               '--results', str(out), '--tag', TAG, '--difficulty', 'brutal', '--minutes', str(args.minutes),
               '--maps', args.map, '--ffa', 'default@infantry-squads',
               '--seeds', *[str(s) for s in args.seeds], '--jobs', str(args.jobs)]
    print('+ ' + ' '.join(command), flush=True)
    subprocess.run(command, check=True, cwd=root)


def table(name):
    path = out / name
    return [r for r in csv.DictReader(path.open(newline='')) if r.get('tag') == TAG] if path.exists() else []


results = []


def accept(line, ok, value):
    results.append(ok)
    print(f'ACCEPT {line} {"PASS" if ok else "FAIL"}: {value}', flush=True)


games = [g for g in table('games.csv') if not g.get('error')]
if len(games) != len(args.seeds):
    print(f'ACCEPT setup FAIL: {len(games)} of {len(args.seeds)} games finished')
    sys.exit(1)
seeds = [g['seed'] for g in games]
rows = table('infantry.csv')
kind = defaultdict(list)
for r in rows:
    kind[r['kind']].append(r)


def per_game(items):
    counts = Counter(r['seed'] for r in items)
    return [counts[s] for s in seeds]


# S1: three men for every squad, at 200 / 50.
delivered = [r for r in kind['INFANTRY_SQUAD'] if r['event'] == 'delivered']
short = [r for r in delivered if r['men'] != '3' or r['expected'] != '3']
squad_orders = [r for r in kind['INFANTRY_ORDER'] if r['item'] in SQUADS]
wrong_price = [r for r in squad_orders if int(r['price']) != SQUADS[r['item']][1]]
accept('S1 a squad order costs 200 (Troopers) / 50 (Infantry) and puts exactly three men on the map, every game',
       min(per_game(delivered)) >= 1 and not short and not wrong_price,
       f'squads delivered per game {per_game(delivered)}, {len(short)} short of three; '
       f'{len(squad_orders)} squad orders, prices {sorted(set((r["item"], r["price"]) for r in squad_orders))}')

# S2: of the men QuantBot buys from a builder that offers the squad, >= 90% come as squads, in every
# house-game that buys any there.
ai_offered = [r for r in kind['INFANTRY_ORDER'] if r['ai'] == '1' and r['squad_offered'] == '1']
men = defaultdict(lambda: [0, 0])   # (seed, house) -> [men as squads, all men]
for r in ai_offered:
    entry = men[(r['seed'], r['house'])]
    entry[1] += int(r['men'])
    if r['item'] in SQUADS:
        entry[0] += int(r['men'])
low = {k: v for k, v in men.items() if v[0] < 0.9 * v[1]}
accept('S2 the AI buys its men as squads when a squad is cheaper per man (>= 90% of the men, every house-game)',
       len(men) >= len(seeds) and not low,
       f'{len(men)} house-games bought men where a squad was offered; squad share of men '
       f'{sorted(round(v[0] / v[1], 2) for v in men.values() if v[1])}; below 90%: {low}')

# S3: each infantry backlog leads to another Barracks/WOR ordered within a minute (>= 90% of backlogs that
# are not already answered), and >= 2 of the producer stand in at least half of the house-games.
lanes = kind['INFANTRY_LANE']
backlogs = [r for r in lanes if r['event'] == 'backlog']
ordered = defaultdict(list)
for r in lanes:
    if r['event'] == 'ordered':
        ordered[(r['seed'], r['house'], r['producer'])].append(int(r['cycle']))
answered = sum(1 for r in backlogs if any(0 <= c - int(r['cycle']) <= LANE_ORDER_SECONDS * CYCLES_PER_SECOND
                                         for c in ordered[(r['seed'], r['house'], r['producer'])]))
standing = {(r['seed'], r['house']) for r in lanes if r['event'] == 'standing' and int(r['count']) >= 2}
house_games = {(r['seed'], r['house']) for r in kind['INFANTRY_MIX']}
accept('S3 the AI adds Barracks/WOR when infantry production is the limit (factory lane rule)',
       len(backlogs) >= 1 and answered >= 0.9 * len(backlogs) and len(standing) >= 0.5 * max(1, len(house_games)),
       f'{len(backlogs)} backlogs, {answered} answered by an order within {LANE_ORDER_SECONDS} s; '
       f'{sum(len(v) for v in ordered.values())} lanes ordered; {len(standing)} of {len(house_games)} house-games '
       f'with a second Barracks or WOR standing from the rule')

# S4: infantry is scored like the vehicle slots: its share moves off its opening as its score comes in,
# and the city Barracks/WOR buy to that share (not only against aircraft) in every house-game.
mix = defaultdict(list)
for r in kind['INFANTRY_MIX']:
    mix[(r['seed'], r['house'])].append(r)
moved = {k for k, v in mix.items() if any(abs(int(r['share_bps']) - int(r['opening_bps'])) >= 100 for r in v)}
learning = {k for k, v in mix.items() if any(r['learning'] == '1' for r in v)}
buys = Counter((r['seed'], r['house']) for r in kind['INFANTRY_BUY'] if r['reason'] == 'city_infantry_share')
# by minute 20, unless the measured share is under 2% for the whole game
MINUTE_20 = 20 * 60 * CYCLES_PER_SECOND
shared = {k for k, v in mix.items() if any(int(r['share_bps']) >= 200 for r in v)}
on_share = {k for k, v in mix.items() if any(r['on_share'] == '1' and int(r['cycle']) <= MINUTE_20 for r in v)}
accept('S4 infantry is in the unit-mix calculation: its measured share moves and the city buys to it, every house-game',
       len(mix) >= len(seeds) and moved >= learning and all(buys[k] >= 3 for k in mix) and on_share >= shared,
       f'{len(mix)} house-games; share moved >= 1 point in {len(moved)} ({len(learning)} learning); '
       f'city share purchases per house-game {sorted(buys[k] for k in mix)}; '
       f'{len(on_share & shared)} of {len(shared)} house-games with a share of 2%+ reached half of it by minute 20')

# S5: Barracks and WOR pay a top-density Residential zone's tax each.
housing = kind['CITY_INFANTRY_HOUSING']
bad = [r for r in housing if int(r['tax_eighths']) != TOP_RESIDENTIAL_TAX_EIGHTHS * int(r['housed'])
       or int(r['population']) != 40 * int(r['housed'])]
accept('S5 every Barracks/WOR adds a level-3 Residential zone\'s tax base (80 eighths) and population (40)',
       sum(int(r['housed']) for r in housing) >= 1 and not bad,
       f'{len(housing)} census rows, {sum(int(r["housed"]) for r in housing)} housing-building-years, '
       f'{len(bad)} rows off the rule')

sys.exit(0 if all(results) else 1)
