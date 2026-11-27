#!/usr/bin/env python3
"""Acceptance test of Zone Garrisons in real games (docs/zone-garrisons.md, HANDOVER "Zone Garrisons").

Stefan, 2026-11-27, the requests this test holds to (one ACCEPT line each):
  "i want infantry and troopers to be able to occupy friendly and enemy R/I/C - using the capture
   command ... it now is owned by the house that occupies it. AI can also garasion its own buildings
   defensively"                                                          -> A1, A2
  "make infantry cost 30 credits"                                         -> A3
  "evacuate buildings ... when it gets below X% health"                   -> A4
  "if the building explodes they are ejectedb ut are damaged"             -> A5
  "if units evacuate the building returns to its original owners control" -> A6
  "the ai tries capuring undefended buildings"                            -> A7
  "mass spamming infantry to try capture buildings"                       -> A8
  "sto the ai pulling soldiers away on their way in"                      -> A9
  "allow troopers to reinforce garrisons - they keep their weapns so troopers can hit air,
   attack with their missiles"                                            -> A10, A11
  "we want to be fighting house to house basically"                       -> A12

Plays QuantBot Brutal free-for-alls on Stefan's 4P DuneCity map (rs160-dunecity) with every Zone
Garrisons switch on (play style house-to-house) through tests/ai/vanilla-match.py and asserts each
line from the ZONE_GARRISON lines of the match log (garrison.csv). Each line prints
ACCEPT <id> PASS|FAIL with its measured value; any FAIL exits non-zero. The player's own path (the
Capture and Garrison buttons, the I key, map clicks, Evacuate) is asserted through the real UI in
tests/units/run-zone-garrison-probe.py.
"""
import argparse
import csv
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
parser.add_argument('--minutes', type=int, default=45)
parser.add_argument('--jobs', type=int, default=3)
parser.add_argument('--reuse', action='store_true', help='Only check the tables already in --output-dir')
args = parser.parse_args()
out = args.output_dir.resolve()
TAG = 'house-to-house'
THRESHOLD = 60      # DuneCity::ZoneGarrison::kEvacuateHealthPercent
TROOPER = '44'      # Unit_Trooper
SOLDIER = '32'      # Unit_Soldier

if not args.reuse:
    command = [sys.executable, str(root / 'tests/ai/vanilla-match.py'), '--build-dir', args.build_dir,
               '--results', str(out), '--tag', TAG, '--difficulty', 'brutal', '--minutes', str(args.minutes),
               '--maps', args.map, '--ffa', 'default@house-to-house',
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
rows = table('garrison.csv')
per_game = defaultdict(Counter)
for r in rows:
    per_game[r['seed']][(r['event'], r.get('why', ''))] += 1
    per_game[r['seed']][(r['event'], '*')] += 1


def count(event, why='*'):
    return [per_game[s][(event, why)] for s in seeds]


entered = [r for r in rows if r['event'] == 'entered']
taken = [r for r in entered if r['taken'] == '1']
own_entries = [r for r in entered if r['taken'] == '0']
accept('A1 soldiers garrison their own zones (AI defends with them), every game',
       min(count('order', 'defend')) >= 1 and min(Counter(r['seed'] for r in own_entries)[s] for s in seeds) >= 5,
       f'defend orders per game {count("order", "defend")}, own-zone entries per game '
       f'{[Counter(r["seed"] for r in own_entries)[s] for s in seeds]}')
accept('A2 an enemy zone walked into becomes the house\'s, every game',
       min(Counter(r['seed'] for r in taken)[s] for s in seeds) >= 3,
       f'zones taken per game {[Counter(r["seed"] for r in taken)[s] for s in seeds]}')

levy = [r for r in rows if r['event'] == 'levy_train']
accept('A3 a Soldier costs 30 credits (every levy Soldier bought at 30)',
       len(levy) >= 1 and all(r['price'] == '30' for r in levy),
       f'{len(levy)} levy Soldiers trained, prices {sorted(set(r["price"] for r in levy))}')

exits = [r for r in rows if r['event'] == 'exit']
low = [r for r in exits if r['low_health'] == '1']
low_ok = [r for r in low if int(r['health']) * 100 < THRESHOLD * int(r['max_health'])]
accept(f'A4 a garrison leaves its zone below {THRESHOLD}% health on its own',
       len(low) >= 3 and len(low_ok) == len(low) and sum(int(r['deployed']) for r in low) >= 3,
       f'{len(low)} low-health exits ({len(low_ok)} below {THRESHOLD}%), '
       f'{sum(int(r["deployed"]) for r in low)} soldiers out')

ruins = [r for r in exits if r['destroyed'] == '1' and int(r['deployed'] or 0) > 0]
accept('A5 soldiers thrown out of an exploding zone come out alive and hurt',
       len(ruins) >= 1 and sum(int(r['hurt']) for r in ruins) > 0,
       f'{len(ruins)} zones destroyed with soldiers inside, {sum(int(r["deployed"]) for r in ruins)} soldiers out, '
       f'{sum(int(r["hurt"]) for r in ruins)} hit points lost')

returned = count('returned')
accept('A6 a captured zone goes back to its original owner when its garrison is gone, every game',
       min(returned) >= 1, f'zones handed back per game {returned}')

raid = count('order', 'raid')
raid_taken = sum(1 for r in taken)
accept('A7 the AI sends soldiers to take undefended enemy zones, every game',
       min(raid) >= 1 and raid_taken >= 1, f'raid orders per game {raid}, zones taken {raid_taken}')

accept('A8 the AI trains a levy of cheap Soldiers and walks them into enemy zones, every game',
       min(Counter(r['seed'] for r in levy)[s] for s in seeds) >= 10 and min(count('order', 'levy')) >= 10,
       f'levy Soldiers per game {[Counter(r["seed"] for r in levy)[s] for s in seeds]}, '
       f'levy orders per game {count("order", "levy")}')

# Every order paired with what became of it: the soldier went in, died on the way, was refused at the
# door (zone full or below the threshold on arrival), the controller ended it (zone gone, held by
# another house, no progress), or it was still walking when the game ended. An order with none of
# these before the soldier's next order is one something else took the soldier away from.
outcomes = Counter()
by_why = defaultdict(Counter)
events = defaultdict(list)   # (seed, unit) -> [(cycle, event)]
for r in rows:
    if r['event'] in ('order', 'entered', 'died_on_way', 'refused_at_zone', 'refused_at_order', 'order_ended') and r.get('unit'):
        events[(r['seed'], r['unit'])].append((int(r['cycle'] or 0), r['event'], r.get('why', '')))
for key, seq in events.items():
    seq.sort()
    for i, (cycle, event, why) in enumerate(seq):
        if event != 'order':
            continue
        nxt = next((e for c, e, w in seq[i + 1:] if c >= cycle), None)
        result = {'entered': 'entered', 'died_on_way': 'died on the way', 'refused_at_zone': 'refused at the door',
                  'refused_at_order': 'refused when given',
                  'order_ended': 'ended by the controller', 'order': 'taken away', None: 'walking at the end'}[nxt]
        outcomes[result] += 1
        by_why[why][result] += 1
orders = sum(outcomes.values())
refused = sum(count('redirect_refused'))
taken_away = outcomes['taken away']
# Own-zone orders (defend, front, reinforce) whose soldier lived and whose zone still stood: these went
# in unless something stopped them, so their orders per entry is the "close to 1" Stefan's request means.
# Soldiers killed under fire on the way, and zones lost meanwhile, are counted and shown, not hidden.
OWN = ('defend', 'front', 'reinforce')
own_live = sum(n for w in OWN for o, n in by_why[w].items() if o not in ('died on the way', 'ended by the controller'))
own_in = sum(by_why[w]['entered'] for w in OWN)
own_died = sum(by_why[w]['died on the way'] for w in OWN)
per_entry = orders / max(1, len(entered))
accept('A9 no other controller pulls a soldier away on its way in',
       refused >= 1 and taken_away <= 0.02 * orders and own_in >= 0.85 * own_live,
       f'{orders} orders: {dict(outcomes)}; {taken_away} taken away ({100 * taken_away / max(1, orders):.1f}%); '
       f'own-zone orders of living soldiers to standing zones {own_in}/{own_live} went in '
       f'({own_live / max(1, own_in):.2f} orders per entry; {own_died} more died under fire on the way); '
       f'{refused} other controllers\' orders refused; {per_entry:.2f} orders per entry '
       f'(by behaviour: { {w: dict(c) for w, c in by_why.items()} })')

reinforce = count('order', 'reinforce')
joined = [r for r in entered if r['item'] == TROOPER and int(r['occupants'] or 0) >= 2]
mixed = defaultdict(set)
for r in entered:
    mixed[(r['seed'], r['zone'])].add(r['item'])
accept('A10 Troopers reinforce garrisons (mixed garrisons), every game',
       min(reinforce) >= 1 and len(joined) >= 3 and sum(1 for items in mixed.values() if {TROOPER, SOLDIER} <= items) >= 1,
       f'reinforce orders per game {reinforce}, Troopers joining a held zone {len(joined)}, '
       f'zones with Soldiers and Troopers {sum(1 for items in mixed.values() if {TROOPER, SOLDIER} <= items)}')

finished = [r for r in rows if r['event'] in ('exit', 'killed_out')]
shots = sum(int(r['shots'] or 0) for r in finished)
air = sum(int(r['air_shots'] or 0) for r in finished)
fired_air = sum(count('fired_air'))
accept('A11 garrisons fire out with their own weapons, Troopers at aircraft too',
       shots >= 100 and air >= 1 and fired_air >= 1,
       f'{shots} shots from garrisons, {air} at aircraft ({fired_air} Trooper shots at aircraft logged)')

front = count('order', 'front')
chain = count('order', 'chain')
accept('A12 house to house: front zones garrisoned first and captures chained block by block, every game',
       min(front) >= 1 and min(chain) >= 1 and sum(chain) >= 5,
       f'front orders per game {front}, chain orders per game {chain}')

sys.exit(0 if all(results) else 1)
