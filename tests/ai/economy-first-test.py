#!/usr/bin/env python3
"""EconomyFirst acts, reaches its proximate effect, and stays quiet where it must (CTest economy_first).

This is the ACTIVATION test (CLAUDE.md "AI behaviours" rules 1-2), with its own small fixed numbers: the switch
invests in yards and zones before units when on, does nothing when off or on a spice-rich map. It is NOT the
acceptance test. Stefan's footprint from his game is the acceptance table in tests/ai/economy-first-report.py
(written before the measured games), where a line the AI does not reach is reported "not met".

Stefan, 2026-11-28, after his last Air game ("i did the building for the ai and we obliterated the enemy
opponents"): "yes the ai needs to escape the poverty trap on low / no spice maps". CLAUDE.md, "AI behaviours"
rules 1-2 and "Asked-for behaviour" rule 2: a headless real game on his own map (tests/ai/maps/4P - 192x192 -
Air 1v3.ini, no spice, 20,000 credits each, his game options), house Neutral (his seat) with the switch on
against three default Brutal QuantBots, the same game with it off, and a spice-rich map with it on.

Asserts, with the switch ON on his map (house 6, the first MINUTES game minutes):
  fired        ECONOMY_FIRST pass lines (the switch acted)
  mcv          the Heavy Factory ordered at least MIN_MCVS MCVs by minute 5 (economy_first_mcv orders)
  yards        at least MIN_YARDS Construction Yards standing at some minute up to 5 (his: 4 by minute 2.2)
  hold         units bought in the first 2 minutes at most half of the OFF game's (his house: few; the AIs ~18,000)
  zones        at least MIN_ZONE_ORDERS economy_first_zone orders by minute 10
  effect       zones added by minute 10 at least the OFF game's + MIN_ZONE_LEAD (the city grows, not only orders)
with it OFF: no ECONOMY_FIRST line and no economy_first_* order; on the spice-rich map (rs192-dunecity, ON):
one spice_rich line and no pass line (Stefan scoped it to low / no spice maps). Exits 1 on any FAIL.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
MAP = root / 'tests/ai/maps/4P - 192x192 - Air 1v3.ini'
OPTIONS = root / 'tests/ai/maps/air-1v3-game-options.ini'
SKIRMISH_OPTIONS = root / 'tests/performance/data/stefan-game-options.ini'
SEED = 9461
MINUTES = 11
CYCLES_PER_MINUTE = 3750
HOUSE = 6
MIN_MCVS, MIN_YARDS, MIN_ZONE_ORDERS, MIN_ZONE_LEAD = 2, 3, 10, 10
UNIT_ITEMS = set(range(27, 48)) | set(range(50, 56))
MCV = 34


def fields(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def play(args, name, sides, minutes, extra):
    out = Path(args.results) / name
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
               '--output-dir', str(out), '--house', sides[0][0],
               '--match-sides', ','.join(f'{h}:{l}' for h, l in sides), '--local-spectator',
               '--enemy-difficulty', 'brutal', '--partner-difficulty', 'brutal', '--minutes', str(minutes),
               '--seed', str(SEED), '--capture-mib', '256', '--wall-timeout', '1800', '--path-workers', '1',
               '--no-ai-telemetry'] + extra
    env = dict(os.environ, DUNECITY_TEST_ECONOMY_LOG='1')
    result = subprocess.run(command, cwd=root, capture_output=True, text=True, env=env)
    log = out / 'run.log'
    if not log.exists():
        sys.exit(f'{name}: no run.log (exit {result.returncode})\n{result.stderr[-2000:]}')
    return log.read_text(errors='replace').splitlines()


def house_lines(lines, marker):
    for line in lines:
        if marker in line:
            f = fields(line, marker)
            if f.get('house') == str(HOUSE):
                yield f


def summary(lines):
    orders = list(house_lines(lines, 'BUILD_ORDER:'))
    states = list(house_lines(lines, 'ECONOMY_STATE:'))
    first = list(house_lines(lines, 'ECONOMY_FIRST:'))
    cyc = lambda f: int(f.get('cycle', 0))  # noqa: E731
    zones = lambda f: int(f['zones_r']) + int(f['zones_c']) + int(f['zones_i'])  # noqa: E731
    start = zones(states[0]) if states else 0
    at10 = [s for s in states if cyc(s) <= 10 * CYCLES_PER_MINUTE]
    return dict(
        passes=sum(f.get('event') == 'pass' for f in first),
        spice_rich=sum(f.get('event') == 'spice_rich' for f in first),
        mcvs=sum(1 for o in orders if int(o['item']) == MCV and o['rule'] == 'economy_first_mcv'
                 and cyc(o) <= 5 * CYCLES_PER_MINUTE),
        yards=max([int(s['yards']) for s in states if cyc(s) <= 5 * CYCLES_PER_MINUTE] or [0]),
        units2=sum(int(o['price']) for o in orders if int(o['item']) in UNIT_ITEMS and int(o['item']) != MCV
                   and cyc(o) <= 2 * CYCLES_PER_MINUTE),
        zone_orders=sum(1 for o in orders if o['rule'] == 'economy_first_zone' and cyc(o) <= 10 * CYCLES_PER_MINUTE),
        lesson_orders=sum(1 for o in orders if o['rule'].startswith('economy_first')),
        zones10=(zones(at10[-1]) - start) if at10 else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--results', required=True)
    args = parser.parse_args()
    Path(args.results).mkdir(parents=True, exist_ok=True)
    others = [('atreides', 'default'), ('ordos', 'default'), ('mercenary', 'default')]
    air = ['--custom-map', str(MAP), '--mod', 'dunecity', '--game-options', str(OPTIONS)]
    on = summary(play(args, 'on', [('neutral', 'default@economy-first')] + others, MINUTES, air))
    off = summary(play(args, 'off', [('neutral', 'default@economy-first-off')] + others, MINUTES, air))
    rich = summary(play(args, 'rich', [('neutral', 'default@economy-first'), ('atreides', 'default'),
                                       ('ordos', 'default'), ('mercenary', 'default')], 2,
                        ['--skirmish-ffa-size', '192', '--skirmish-map-seed', str(SEED), '--mod', 'dunecity',
                         '--game-options', str(SKIRMISH_OPTIONS)]))
    checks = [
        ('ON fired (ECONOMY_FIRST pass lines)', on['passes'], on['passes'] > 0),
        (f'ON MCVs ordered by minute 5 >= {MIN_MCVS}', on['mcvs'], on['mcvs'] >= MIN_MCVS),
        (f'ON yards by minute 5 >= {MIN_YARDS} (his 4 by 2.2)', on['yards'], on['yards'] >= MIN_YARDS),
        ('ON units bought in minutes 0-2 <= half of OFF', f"{on['units2']} vs {off['units2']}",
         on['units2'] * 2 <= max(1, off['units2'])),
        (f'ON economy_first_zone orders by minute 10 >= {MIN_ZONE_ORDERS}', on['zone_orders'],
         on['zone_orders'] >= MIN_ZONE_ORDERS),
        (f'ON zones added by minute 10 >= OFF + {MIN_ZONE_LEAD}', f"{on['zones10']} vs {off['zones10']}",
         on['zones10'] >= off['zones10'] + MIN_ZONE_LEAD),
        ('OFF: no ECONOMY_FIRST line, no economy_first order', f"{off['passes']} / {off['lesson_orders']}",
         off['passes'] == 0 and off['spice_rich'] == 0 and off['lesson_orders'] == 0),
        ('spice-rich map, ON: spice_rich logged, no pass line', f"{rich['spice_rich']} / {rich['passes']}",
         rich['spice_rich'] == 1 and rich['passes'] == 0),
    ]
    failed = 0
    print('| check | measured | result |\n|---|---|---|')
    for name, value, ok in checks:
        failed += not ok
        print(f"| {name} | {value} | {'PASS' if ok else 'FAIL'} |")
    print(f'ECONOMY_FIRST_TEST: {failed} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
