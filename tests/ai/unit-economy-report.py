#!/usr/bin/env python3
"""What each unit type destroyed against what it cost, city airplanes among them.

Stefan, 2026-11-26, after planeswin.dls: "city airplanes did way more damage than any other unit by a wide
margin". This sets every type side by side, so a plane's damage can be read against Devastators, Launchers,
Ornithopters and the rest, per credit spent and per credit lost as well as in total.

Reads units.csv (vanilla-match.py; one UNIT_ECONOMY row per house and type at the end of each game) and,
for the price actually paid for planes, planes.csv. A save continuation's run.log (run-campaign-balance.py
--load-save) can be read directly with --log: the "loaded" rows are subtracted from the "probe_end" rows, so
only what happened after the load counts.

Damage value is the house's combat record of the type (House::getCombatReward): the price of every enemy
object it hit times the share of hit points it removed, summed; kill bonus is apart. Spent is built x price
(the unit data price of the house; Starport discounts and upgrades are not seen). City airplanes are priced
per flight, free ones included, at the price charged (CITY_PLANE_BOUGHT; 1500): "as if bought", so a plane
compares with a unit built for the same money; what was actually spent on planes is in the notes. A vanilla
match logs both match_end and probe_end; one phase is taken per game (match_end, else probe_end, else end).

  python3 tests/ai/unit-economy-report.py --results DIR [DIR ...] [--tag TAG] [--output report.md]
  python3 tests/ai/unit-economy-report.py --log run.log [--output report.md]
"""
import argparse
import csv
import gzip
import statistics
from collections import defaultdict
from pathlib import Path

PLANE = 'Ambient_Airplane'
# Rows carry kind=unit|structure from 2026-11-26; older rows fall back to the engine's unit names
# (getItemNameByID, spaces as underscores).
UNIT_NAMES = {'Carryall', 'Chemical_Carryall', 'Devastator', 'Deviator', 'Frigate', 'Harvester', 'Launcher', 'MCV',
              "'Thopter", 'Quad', 'Raider_Trike', 'Siege_Tank', 'Sonic_Tank', 'Tank', 'Trike', 'Saboteur', 'Sandworm',
              'Soldier', 'Trooper', 'Special', 'Infantry', 'Troopers', 'Infantry5', 'Troopers5', 'Elite_Ornithopter',
              'Raider_Quad', 'Ranged_Tank', 'Slow_Tank', 'Capacity_Harvester', 'Fast_Harvester', 'Tame_Sandworm',
              'Flame_Quad', 'Gas_Infantry', 'Flame_Trooper', 'Ambient_Airplane', 'Ambient_Helicopter', 'Rocket_Trike',
              'Sonic_Trike', 'Flame_Tank', 'Elite_Launcher', 'Elite_Siege_Tank', 'Chemical_Siege_Tank', 'Harvestank'}


def is_unit(row):
    return row.get('kind') == 'unit' if row.get('kind') else row['name'] in UNIT_NAMES


def read(folders, name):
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


def num(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return 0.0


def parse_log(path):
    """UNIT_ECONOMY rows of one log, 'probe_end' less 'loaded', and the planes bought after the load."""
    loaded, ended, bought, launched = {}, {}, [], 0
    for line in Path(path).read_text(errors='replace').splitlines():
        if 'UNIT_ECONOMY:' in line:
            data = dict(p.split('=', 1) for p in line.split('UNIT_ECONOMY:', 1)[1].split() if '=' in p)
            target = loaded if data.get('phase') == 'loaded' else ended if data.get('phase') in ('probe_end', 'end', 'match_end') else None
            if target is not None:
                target[(data['house'], data['item'])] = data
        elif 'CITY_PLANE_BOUGHT:' in line:
            data = dict(p.split('=', 1) for p in line.split('CITY_PLANE_BOUGHT:', 1)[1].split() if '=' in p)
            bought.append(data)
        elif 'CITY_PLANE_LAUNCHED:' in line:
            launched += 1
    rows = []
    for key, end in ended.items():
        start = loaded.get(key, {})
        row = dict(end)
        for field in ('built', 'lost', 'damage_value', 'kill_bonus', 'kills', 'hits'):
            row[field] = str(num(end.get(field)) - num(start.get(field)))
        row['tag'] = 'log'
        rows.append(row)
    return rows, bought, launched


def one_phase(rows):
    """One end-of-game phase per game: match_end, else probe_end, else end."""
    by_game = defaultdict(list)
    for row in rows:
        by_game[tuple(row.get(k, '') for k in ('difficulty', 'map', 'seed', 'sides'))].append(row)
    kept = []
    for game_rows in by_game.values():
        for phase in ('match_end', 'probe_end', 'end'):
            chosen = [r for r in game_rows if r.get('phase') == phase]
            if chosen:
                kept += chosen
                break
    return kept


def table(rows, bought_rows, flights, games):
    by_type = defaultdict(lambda: defaultdict(float))
    for row in rows:
        t = by_type[row['name']]
        t['price'] = num(row['price'])
        t['unit'] = 1.0 if is_unit(row) else 0.0
        for field in ('built', 'lost', 'damage_value', 'kill_bonus', 'kills'):
            t[field] += num(row[field])
    plane_spent = sum(num(r.get('price')) for r in bought_rows)
    plane_bought = len(bought_rows)
    lines = ['| Type | Built | Lost | Spent | Damage value | Kills | Value per credit spent | Value per credit lost | '
             'Damage value a game |', '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    ranked = []
    for name, t in by_type.items():
        if t['damage_value'] <= 0 and t['built'] <= 0:
            continue
        if name == PLANE:
            price = plane_spent / plane_bought if plane_bought else 1500
            spent = flights * price
            lost_cost = t['lost'] * price
        else:
            spent = t['built'] * t['price']
            lost_cost = t['lost'] * t['price']
        ranked.append((name, t, spent, lost_cost))
    ranked.sort(key=lambda item: -item[1]['damage_value'])
    for name, t, spent, lost_cost in ranked:
        kind = '' if t['unit'] else ' (building)'
        label = name.replace('_', ' ') + kind
        if name == PLANE:
            label = 'City airplane (%d flights, %d bought; per flight as if bought)' % (flights, plane_bought)
        lines.append('| %s | %d | %d | %s | %s | %d | %s | %s | %s |' % (
            label, t['built'], t['lost'], '{:,.0f}'.format(spent), '{:,.0f}'.format(t['damage_value']), t['kills'],
            '%.2f' % (t['damage_value'] / spent) if spent > 0 else '-',
            '%.2f' % (t['damage_value'] / lost_cost) if lost_cost > 0 else '-',
            '{:,.0f}'.format(t['damage_value'] / max(1, games))))
    plane = by_type.get(PLANE)
    notes = []
    if plane and flights:
        notes.append('City airplanes: %s damage value a flight against a price of %s; %d of %d flights bought, %s spent.'
                     % ('{:,.0f}'.format(plane['damage_value'] / flights),
                        '{:,.0f}'.format(plane_spent / plane_bought) if plane_bought else '1,500',
                        plane_bought, flights, '{:,.0f}'.format(plane_spent)))
    army = [(n, t, s) for n, t, s, _ in ranked if t['unit'] and n != PLANE and s > 0]
    if army:
        spent = sum(s for _, _, s in army)
        value = sum(t['damage_value'] for _, t, _ in army)
        notes.append('All other unit types together: %.2f damage value per credit spent.' % (value / spent))
    return lines, notes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--results', type=Path, nargs='+')
    parser.add_argument('--log', type=Path, nargs='+', help='run.log of save continuations (loaded rows subtracted)')
    parser.add_argument('--tag', action='append')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    out = ['# Value destroyed against cost, by unit type', '']
    if args.log:
        for path in args.log:
            rows, bought, launched = parse_log(path)
            lines, notes = table(rows, bought, launched, 1)
            out += ['## %s' % path, ''] + lines + [''] + notes + ['']
    if args.results:
        units = read(args.results, 'units.csv')
        planes = read(args.results, 'planes.csv')
        games = [r for r in read(args.results, 'games.csv') if not r.get('error')]
        tags = args.tag or sorted({r['tag'] for r in games})
        for tag in tags:
            n_games = sum(1 for r in games if r['tag'] == tag)
            rows = one_phase([r for r in units if r['tag'] == tag])
            bought = [r for r in planes if r['tag'] == tag and r['event'] == 'bought']
            flights = sum(1 for r in planes if r['tag'] == tag and r['event'] == 'launched')
            lines, notes = table(rows, bought, flights, n_games)
            out += ['## %s (%d games)' % (tag, n_games), ''] + lines + [''] + notes + ['']
    text = '\n'.join(out) + '\n'
    print(text)
    if args.output:
        args.output.write_text(text, encoding='utf-8')


if __name__ == '__main__':
    main()
