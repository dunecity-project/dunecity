#!/usr/bin/env python3
"""Assert real all-units menus, factory/sidebar deliveries and QuantBot ON/OFF output."""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/menu'))
from probe_profile import (NATIVE_TARGET, native_build_commands, native_probe_binary,
                           native_probe_includes, discard_profile_assets, require_free_space, scratch_dir)
from probe_process import processes, run

PROFILE_INI = (
    '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
    'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\n'
    'Player Name = All Units observer\n')
MAP = 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
FIELDS = ('house', 'option', 'factory_products', 'starport_products', 'player_orders',
          'player_completed', 'bot_applicable', 'bot_completed', 'bot_new_to_house',
          'tech_negative_gates', 'upgrade_negative_gates', 'prerequisite_negative_gates', 'config')


def require(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_probe(build, out, identity):
    run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)],
        check=True, cwd=root, timeout=60)
    commands = native_build_commands(build)
    identity['build_commands_sha256'] = hashlib.sha256('\n'.join(commands).encode()).hexdigest()
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    require(main.count(needle) == 1, 'Main menu entry changed; update the test injection point')
    main = main.replace(needle, 'int menuResult = runAllUnitsAllHousesProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))',
                        'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    includes = ''.join('#include "' + path.as_posix() + '"\n' for path in native_probe_includes(root))
    includes += '#include "' + str(root / 'tests/ai/all-units-all-houses-probe.inc') + '"\n'
    source, obj = out / 'all-units-main.cpp', out / 'all-units-main.o'
    source.write_text(main[:position] + includes + main[position:])
    cc = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in cc:
            index = cc.index(option)
            del cc[index:index + 2]
    for option in ('-MD', '-MMD'):
        if option in cc:
            cc.remove(option)
    cc[cc.index('-o') + 1] = str(obj)
    cc[cc.index('-c') + 1] = str(source)
    cc.extend(['-fno-access-control', '-DALL_UNITS_MAP_PATH="' + str(root / MAP) + '"'])
    binary = native_probe_binary(build, out, 'all-units-all-houses-probe')
    link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
    with (out / 'build.log').open('w') as log:
        run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
        run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    identity['probe_binary_sha256'] = digest(binary)
    return binary


def execute_probe(binary, out):
    with scratch_dir('all-units-all-houses-probe') as scratch:
        profile = scratch / 'profile'
        profile.mkdir()
        (profile / 'Dune City.ini').write_text(PROFILE_INI, encoding='utf-8')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1')
        try:
            with (out / 'run.log').open('w') as log:
                done = run([str(binary), '--window', '--showlog', '--non-interactive'], cwd=out, env=env,
                           stdout=log, stderr=subprocess.STDOUT, timeout=240)
        finally:
            discard_profile_assets(profile)
    return done.returncode


def parse(text):
    records = defaultdict(list)
    for line in text.splitlines():
        match = re.search(r'ALL_UNITS_([A-Z_]+):\s*(.*)$', line)
        if match:
            fields = dict(re.findall(r'([a-z_]+)=([^\s]+)', match[2]))
            records[match[1]].append(fields)
            if match[1] != 'AVAIL' and match[1] != 'BOT_UNIT':
                print(line[line.index('ALL_UNITS_'):], flush=True)
    return records


def number(row, field):
    require(field in row, 'Native probe omitted field ' + field)
    return int(row[field])


def unique(rows, key):
    result = {}
    for row in rows:
        index = key(row)
        require(index not in result, 'Duplicate native probe record: ' + str(index))
        result[index] = row
    return result


def summarize(records):
    catalogs = {(number(row, 'house'), number(row, 'option')): row for row in records['CATALOG']
                if number(row, 'city') == 1}
    bots = {(number(row, 'house'), number(row, 'option')): row for row in records['BOT']}
    gates = {number(row, 'house'): row for row in records['GATES']}
    players = defaultdict(list)
    delivered = defaultdict(Counter)
    for row in records['PLAYER']:
        players[number(row, 'house')].append(row)
    for row in records['BOT_UNIT']:
        delivered[number(row, 'house'), number(row, 'option')][number(row, 'item')] += 1
    result = []
    for key in sorted(catalogs.keys() | bots.keys()):
        house, option = key
        catalog, bot, gate = catalogs.get(key, {}), bots.get(key, {}), gates.get(house, {}) if option else {}
        player = players[house] if option else []
        result.append({
            'house': house, 'option': option,
            'factory_products': int(catalog.get('factories', 0)),
            'starport_products': int(catalog.get('starport', 0)),
            'player_orders': len(player),
            'player_completed': sum(number(row, 'completed') for row in player),
            'bot_applicable': int(bot.get('applicable', 0)),
            'bot_completed': int(bot.get('completed', 0)),
            'bot_new_to_house': int(bot.get('new', 0)),
            'tech_negative_gates': int(gate.get('tech', 0)),
            'upgrade_negative_gates': int(gate.get('upgrades', 0)),
            'prerequisite_negative_gates': int(gate.get('prerequisites', 0)),
            'config': bot.get('config', ''),
            'completed_by_item': dict(sorted(delivered[key].items())),
        })
    return result


def validate(records, returncode):
    require(returncode == 0, 'The real engine probe exited nonzero; see run.log')
    require(not records['FAIL'] and len(records['PASS']) == 1, 'The real engine did not finish every fatal acceptance assertion')
    require(len(records['DEFAULT']) == 1, 'The default-city/generic-off assertion did not execute exactly once')
    default = records['DEFAULT'][0]
    require(number(default, 'city_on') == number(default, 'generic_off') == 1, 'Incorrect city/vanilla defaults')
    menus = unique(records['MENU'], lambda row: number(row, 'option'))
    require(set(menus) == {0, 1} and all(all(number(row, field) == 1 for field in ('click', 'ini', 'reopened', 'pass'))
                                      for row in menus.values()), 'Both real checkbox/INI/reopen arms must execute')
    catalogs = unique(records['CATALOG'], lambda row: (number(row, 'city'), number(row, 'house'), number(row, 'option')))
    roster = {house for city, house, option in catalogs if city == 1 and option == 0}
    require(set(range(6)) <= roster and len(roster) == number(default, 'roster'), 'The real playable city roster was not covered')
    require(set(catalogs) == {(1, house, option) for house in roster for option in (0, 1)} | {(0, 0, 0), (0, 0, 1)},
            'Missing/extra city or forced-vanilla catalogue arms')
    old_counts = {0: 12, 1: 12, 2: 14, 3: 16, 4: 16, 5: 16}
    availability = unique(records['AVAIL'], lambda row: (number(row, 'city'), number(row, 'house'), number(row, 'option'), number(row, 'item')))
    for key, catalog in catalogs.items():
        city, house, option = key
        expected = 17 if city and option else old_counts.get(house, 17) if city else 10
        require(number(catalog, 'factories') == expected and number(catalog, 'starport') == (17 if city and option else 9)
                and number(catalog, 'palace') == number(catalog, 'pass') == 1, 'Wrong exact factory/starport/Palace catalogue: ' + str(key))
        offered = [row for index, row in availability.items() if index[:3] == key]
        require(len(offered) == 17 and sum(number(row, 'factory') for row in offered) == expected,
                'The 17 actual product-by-product list assertions did not all execute: ' + str(key))
    gates = unique(records['GATES'], lambda row: number(row, 'house'))
    require(set(gates) == roster and all(all(number(row, field) > 0 for field in ('tech', 'upgrades', 'prerequisites', 'pass'))
                                       for row in gates.values()), 'Every house needs real negative technology/upgrade/prerequisite activations')
    players = unique(records['PLAYER'], lambda row: (number(row, 'house'), number(row, 'item')))
    denied = {(house, item) for (city, house, option, item), row in availability.items()
              if city == 1 and option == 0 and number(row, 'factory') == 0}
    require(set(players) == denied and all(number(row, 'completed') > 0 and number(row, 'option') == number(row, 'pass') == 1
                                         for row in players.values()), 'Every newly allowed product needs its real paid sidebar delivery')
    imports = records['IMPORT']
    require(len(imports) == 1 and number(imports[0], 'orders') == 8 and number(imports[0], 'deployed') == 12
            and number(imports[0], 'spent') > 0 and number(imports[0], 'pass') == 1,
            'The eight expanded Starport products lack real paid Order-button shipment/deployment assertions')
    bots = unique(records['BOT'], lambda row: (number(row, 'house'), number(row, 'option')))
    require(set(bots) == {(house, option) for house in roster for option in (0, 1)}, 'Missing real QuantBot ON/OFF arms')
    events = unique(records['BOT_UNIT'], lambda row: (number(row, 'house'), number(row, 'option'), number(row, 'object')))
    counts, new_counts = Counter(), Counter()
    for (house, option, _), event in events.items():
        require((house, option) in bots and event.get('source') == 'factory' and number(event, 'completed') == 1
                and number(event, 'new') in (0, 1), 'A bot activation was not an actual deployed factory unit')
        counts[house, option] += 1
        new_counts[house, option] += number(event, 'new')
    for (house, option), bot in bots.items():
        applicable = old_counts.get(house, 17) < 17
        require(number(bot, 'applicable') == applicable and number(bot, 'pass') == 1,
                'Already-unrestricted factions must explicitly report new-to-house activation as not applicable')
        require(number(bot, 'completed') == counts[house, option] and number(bot, 'new') == new_counts[house, option],
                'QuantBot summary disagrees with completed deployed-unit events')
        if applicable:
            require(counts[house, option] > 0 and number(bot, 'turns') > 0 and number(bot, 'cycles') > 0,
                    'The funded real QuantBot producer path did not activate')
            require(new_counts[house, option] > 0 if option else new_counts[house, option] == 0,
                    'Every restricted house must deploy new-to-house units ON, and zero denied factory products OFF')
            require(bot['config'] == bots[house, 1 - option]['config'] != 'not_applicable',
                    'QuantBot ON/OFF arms did not retain identical normal ratios/configuration')
        else:
            require(counts[house, option] == new_counts[house, option] == number(bot, 'turns') == 0,
                    'A faction with no denied baseline product claimed a fictitious new-to-house activation')
    priors = records['PRIOR']
    require(len(priors) == 14 and sum(number(row, 'city_shared') for row in priors) == 6,
            'The six shared-city and eight OFF/vanilla prior comparisons did not all execute')
    require({number(row, 'house') for row in priors if number(row, 'city_shared')} == set(range(6))
            and all(number(row, 'special_before') == number(row, 'special_after') and number(row, 'pass') == 1
                    and (number(row, 'city_shared') or number(row, 'legacy_equal') == 1) for row in priors),
            'The option changed the special budget or an OFF/vanilla prior')
    markets = unique(records['MARKET'], lambda row: (row['case'], number(row, 'option')))
    require(set(markets) == {(case, option) for case in ('absent', 'sold_out') for option in (0, 1)}, 'Missing no-CHOAM/authored-zero boundary arms')
    for (case, option), row in markets.items():
        expected = 17 if option else 0 if case == 'absent' else 9
        require(number(row, 'stocked') == expected and number(row, 'pass') == 1, 'Wrong missing-market stock overlay')
        require(number(row, 'trike') == 0 if case == 'sold_out' else number(row, 'trike') == 1 if option else number(row, 'trike') < 0,
                'The market overlay replaced an authored zero or failed to seed an absent product')
    require(number(records['PASS'][0], 'roster') == len(roster)
            and all(number(records['PASS'][0], field) == 1 for field in
                    ('default_city_on', 'ini', 'real_sidebar', 'gates', 'palace_unchanged', 'vanilla_hidden_off')),
            'The final acceptance record omitted a required behavior')


def main():
    processes.install_signal_handlers()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=root / 'build')
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    build, out = args.build_dir.resolve(), args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'run.log').write_text('')  # Never report activations left by a previous CTest invocation.
    identity = {'profile_ini_sha256': hashlib.sha256(PROFILE_INI.encode()).hexdigest(), 'source_inputs': {}}
    report = {'format': 1, 'status': 'failed', 'identity': identity, 'houses': [], 'errors': [], 'native_returncode': None}
    records = defaultdict(list)
    try:
        require_free_space()
        for relative in (MAP, 'config/ObjectData.ini.default', 'config/QuantBot Config.ini.default',
                         'src/main.cpp', 'src/Game.cpp', 'src/ObjectData.cpp', 'src/INIMap/INIMapLoader.cpp',
                         'src/structures/BuilderBase.cpp', 'src/structures/StarPort.cpp',
                         'src/players/QuantBotBuild.cpp', 'src/players/QuantBotBuildProducers.cpp',
                         'src/players/QuantBotStyle.cpp', 'src/players/QuantBotInfantry.cpp',
                         'include/dunecity/AllUnitsRules.h', 'include/DataTypes.h',
                         'tests/ai/all-units-all-houses-probe.inc', 'tests/ai/run-all-units-all-houses-probe.py'):
            identity['source_inputs'][relative] = digest(root / relative)
        binary = compile_probe(build, out, identity)
        report['native_returncode'] = execute_probe(binary, out)
        records = parse((out / 'run.log').read_text(errors='replace'))
        validate(records, report['native_returncode'])
        report['status'] = 'passed'
    except Exception as error:
        report['errors'].append(str(error))
        if not records and (out / 'run.log').exists():
            records = parse((out / 'run.log').read_text(errors='replace'))
    finally:
        try:
            report['houses'] = summarize(records)
        except Exception as error:
            report['status'] = 'failed'
            report['errors'].append('Invalid native count records: ' + str(error))
        report['checks'] = {name.lower(): len(records[name]) for name in
                            ('DEFAULT', 'MENU', 'CATALOG', 'AVAIL', 'GATES', 'PLAYER', 'MARKET', 'BOT', 'BOT_UNIT', 'PASS', 'FAIL')}
        (out / 'summary.json').write_text(json.dumps(report, sort_keys=True, separators=(',', ':')) + '\n')
        with (out / 'counts.csv').open('w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=FIELDS, extrasaction='ignore')
            writer.writeheader()
            writer.writerows(report['houses'])
    if report['status'] != 'passed':
        raise RuntimeError('All Units for All Houses failed: ' + '; '.join(report['errors']) + '. Evidence: ' + str(out))
    print('All Units for All Houses passed. Actual deployed-unit counts: ' + str(out / 'counts.csv'))


if __name__ == '__main__':
    main()
