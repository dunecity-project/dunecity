#!/usr/bin/env python3
"""Bounded OFF/ON GarrisonHold acceptance through real Both controllers and native effects.

Uses the configured production objects/assets, replacing main.cpp only. Pins the unchanged
shelter/permission/movement authority to 168d643ee406a78124c25011cd082392e932d9c3.
"""
import argparse
from collections import Counter, defaultdict
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
from probe_profile import (NATIVE_TARGET, discard_profile_assets, native_build_commands,
                           native_probe_binary, native_probe_includes, require_free_space, scratch_dir)
from probe_process import processes, run

CORE_AUTHORITY = '168d643ee406a78124c25011cd082392e932d9c3'
CORE_HASHES = {
    'include/dunecity/ZoneGarrisonPolicy.h': '6376253b87e4c24fb3e78ea07e8faf4164d6f6d9388e8182f6bf5db91084574d',
    'include/structures/ZoneStructure.h': '0e8775a2f3124b5426a6711f99165759707bc75143909ee1cdee4ac3db5238f1',
    'src/structures/ZoneStructureGarrison.cpp': '7896c05f620b2a1ff136ead98d40c18481896aa2c68d6725f43e85abfe6cdaf8',
    'src/units/InfantryBase.cpp': '670e3d135e849c9635432cdcafa654b5408d0429e7d313d5f2e87c57e83edf1b',
    'src/units/UnitBase.cpp': '1856c8f8361aab36a33ee2b4803f7a8168977b862ad3da3187d216af06d5ffbf',
}
CASES = {'config_surface', 'quiet_default_off', 'quiet_on', 'replacement_off', 'replacement_on',
         'relocation_on', 'destruction_on', 'permission_off', 'human_override'}


def fields(line):
    return {key: int(value) if re.fullmatch(r'-?\d+', value) else value
            for key, value in re.findall(r'(\w+)=([^\s]+)', line)}


def require(ok, why):
    if not ok:
        raise RuntimeError(why)


def inspect_log(text, returncode, out):
    cases, results, units, configs, summaries, begins = [], [], [], [], [], []
    native = defaultdict(list)
    active = None
    for line in text.splitlines():
        if 'GARRISON_HOLD_' in line:
            line = line[line.index('GARRISON_HOLD_'):]
            print(line, flush=True)
            row = fields(line)
            if line.startswith('GARRISON_HOLD_BEGIN:'):
                active = row['name']
                begins.append(row)
            elif line.startswith('GARRISON_HOLD_CASE:'):
                cases.append(row)
                active = None
            elif line.startswith('GARRISON_HOLD_RESULT:'):
                results.append(row)
                active = None
            elif line.startswith('GARRISON_HOLD_UNIT:'):
                units.append(row)
            elif line.startswith('GARRISON_HOLD_CONFIG:'):
                configs.append(row)
                active = None
            elif line.startswith('GARRISON_HOLD_PASS:'):
                summaries.append(row)
            elif line.startswith('GARRISON_HOLD_FAIL:'):
                active = None
        elif active and 'ZONE_GARRISON:' in line:
            native[active].append(fields(line[line.index('ZONE_GARRISON:'):]))
    (out / 'observations.json').write_text(json.dumps({
        'returncode': returncode, 'core_authority': CORE_AUTHORITY, 'begins': begins,
        'cases': cases, 'results': results, 'units': units, 'config': configs,
        'native_events': native, 'summaries': summaries,
    }, indent=2) + '\n', encoding='utf-8')
    require(returncode == 0 and 'GARRISON_HOLD_FAIL:' not in text
            and len(summaries) == 1 and summaries[0] == {'cases': len(CASES), 'failures': 0},
            'Native Hold assertions failed: ' + str(out / 'run.log'))
    require(Counter(row['name'] for row in begins) == Counter(CASES)
            and Counter(row['name'] for row in cases) == Counter(CASES)
            and all(row['pass'] == 1 for row in cases), 'Missing, duplicate or unsuccessful case markers')
    require(Counter(row['name'] for row in results) == Counter(CASES - {'config_surface'}),
            'Missing or duplicate native effect observations')
    require(len(configs) == 1 and configs[0] == {
        'code_default': 0, 'shipped_default': 0, 'saved_on': 1, 'loaded_on': 1,
        'overlay_independent': 1, 'off_hash_unchanged': 1, 'on_hash_changed': 1,
    }, 'Default/load/save/per-house/hash exposure did not activate')
    result = {row['name']: row for row in results}
    original_ids = {}
    for name, row in result.items():
        require(row['both'] == 1 and row['permission'] == int(name != 'permission_off'),
                name + ': controller or core permission differs')
        require(row['hold'] == int(name not in {'quiet_default_off', 'replacement_off'}),
                name + ': treatment was silently supplied by bundled tactics')
        if name == 'permission_off':
            continue
        phases = {phase: [unit for unit in units if unit['name'] == name and unit['phase'] == phase]
                  for phase in ('onset', 'end')}
        onset = {unit['id'] for unit in phases['onset']}
        ending = {unit['id'] for unit in phases['end']}
        require(len(phases['onset']) == len(phases['end']) == len(onset) == row['originals']
                and onset == ending and len(onset) > 0, name + ': original live IDs were lost/duplicated')
        require(all(unit['live'] == unit['hidden'] == unit['inside'] == 1 and unit['active'] == 0
                    for unit in phases['onset']), name + ': setup did not physically hide every original')
        require(sum(unit['inside'] for unit in phases['end']) == row['retained'],
                name + ': end occupancy does not account the original IDs')
        original_ids[name] = onset
        if name != 'destruction_on':
            require(all(unit['live'] == 1 for unit in phases['end']), name + ': original unit died/disappeared')
        if name in {'quiet_on', 'replacement_on', 'relocation_on'}:
            require(all(unit['inside'] == unit['hidden'] == 1 and unit['active'] == 0
                        for unit in phases['end']), name + ': original ID was not retained in real shelter')
        if name in {'quiet_default_off', 'human_override'}:
            require(all(unit['active'] == 1 and unit['inside'] == unit['hidden'] == 0
                        for unit in phases['end']), name + ': released originals are not actually on map')

    def events(name, event):
        return [row for row in native[name] if row.get('event') == event]

    for name in ('quiet_on', 'replacement_on', 'relocation_on', 'destruction_on', 'permission_off'):
        require(not events(name, 'release') and not [row for row in events(name, 'exit') if not row['destroyed']],
                name + ': unauthorized ordinary evacuation while Hold was ON')
    off, on = result['quiet_default_off'], result['quiet_on']
    require(off['departures'] == off['originals'] and off['retained'] == 0
            and on['departures'] == 0 and on['retained'] == on['originals']
            and off['ai_passes'] >= 8 and on['ai_passes'] >= 8 and off['cycles'] > 0 and on['cycles'] > 0,
            'Quiet OFF/ON arms were no-ops or did not run many real controller passes')
    releases = [row for row in events('quiet_default_off', 'release') if row['zone'] == off['zone']]
    require(sum(row['deployed'] for row in releases) == off['originals'], 'OFF quiet release never actually activated')
    for name, why in (('quiet_on', 'quiet_release'), ('replacement_on', 'soldier_replacement')):
        blocked = [row for row in events(name, 'hold_blocked') if row['why'] == why
                   and row['zone'] == result[name]['zone'] and row['blocked_total'] > 0]
        require(blocked, name + ': no actual production guard activation (zero-exposure experiment)')

    off, on = result['replacement_off'], result['replacement_on']
    require(off['departures'] == off['candidate_inside'] == 1 and off['retained'] == off['originals'] - 1
            and on['departures'] == on['candidate_inside'] == 0 and on['retained'] == on['originals']
            and on['ai_passes'] >= 8 and off['ai_passes'] >= 1
            and all(row['free_troopers'] > 0 and row['threat_distance_raw'] > 0 and row['threat_firing'] == 0
                    for row in (off, on)), 'Full-Soldier replacement lacked an effective paired opportunity')
    exits = events('replacement_off', 'exit')
    require(len(exits) == 1 and exits[0]['zone'] == off['zone'] and exits[0]['destroyed'] == 0
            and exits[0]['deployed'] == 1 and exits[0]['remaining'] == off['originals'] - 1,
            'OFF full-Soldier replacement did not really eject exactly one original')

    row = result['relocation_on']
    require(row['exact30_held'] == 1 and row['departures'] == row['walked'] == row['arrivals'] == row['originals']
            and row['retained'] == row['originals'] and row['cycles'] > 0, 'Native below-30% walking relocation did not finish')
    attempts = events('relocation_on', 'relocation_attempt')
    departures = events('relocation_on', 'relocation_departure')
    arrivals = events('relocation_on', 'relocation_arrival')
    require(len(attempts) == 1 and attempts[0]['source'] == row['zone']
            and attempts[0]['moving'] == attempts[0]['occupants'] == row['originals'] and attempts[0]['stayed'] == 0
            and attempts[0]['health_raw'] == row['health_raw'] and attempts[0]['max_health'] == row['max_health'],
            'Relocation was not the once-per-drop building-owned native event')
    require(Counter(event['unit'] for event in departures) == Counter(original_ids['relocation_on'])
            and Counter(event['unit'] for event in arrivals) == Counter(original_ids['relocation_on'])
            and all(event['source'] == row['zone'] and event['destination'] == row['destination'] for event in departures)
            and all(event['destination'] == row['destination'] for event in arrivals),
            'Native relocation departure/arrival accounting lost original IDs')

    row = result['destruction_on']
    exits = events('destruction_on', 'exit')
    require(len(exits) == 1 and exits[0]['zone'] == row['zone'] and exits[0]['destroyed'] == 1
            and exits[0]['occupants'] == row['originals'] == row['deployed'] + row['killed']
            and exits[0]['deployed'] == row['deployed'] == 2 and exits[0]['killed'] == row['killed'] == 1
            and exits[0]['remaining'] == row['retained'] == 0 and exits[0]['hurt'] > 0 and row['explosions'] > 0,
            'Native destruction did not completely account living, injured and killed originals')
    dead = {unit['id'] for unit in units if unit['name'] == 'destruction_on' and unit['phase'] == 'end' and unit['live'] == 0}
    casualties = {event['unit'] for event in events('destruction_on', 'casualty') if event.get('cause') == 'collapse'}
    require(dead == casualties and len(dead) == row['killed'], 'Destruction casualty IDs lack native collapse evidence')
    row = result['permission_off']
    require(row['configured_tactics'] == 6 and row['capture_commands'] == 2 and row['entries'] == 0
            and row['damage_raw'] > 0 and row['ai_passes'] > 0 and row['cycles'] > 0
            and not events('permission_off', 'entered'),
            'Global permission OFF was bypassed or normal native effects never activated')
    require(result['human_override']['human_commands'] == 1
            and result['human_override']['departures'] == result['human_override']['originals'],
            'Hold suppressed explicit human authority')
    print('GARRISON_HOLD_NUMBERS: quiet_off_exits=%d quiet_on_exits=0 quiet_on_passes=%d '
          'replacement_off_exits=1 replacement_on_exits=0 relocation_departures=%d relocation_arrivals=%d '
          'destruction_living=2 destruction_killed=1 permission_off_entries=0' % (
              result['quiet_default_off']['departures'], result['quiet_on']['ai_passes'],
              result['relocation_on']['departures'], result['relocation_on']['arrivals']), flush=True)


processes.install_signal_handlers()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--wall-timeout', type=int, default=180)
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
require_free_space()
core_hashes = {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in CORE_HASHES}
require(core_hashes == CORE_HASHES, 'Shelter/permission/movement source differs from immutable core authority ' + CORE_AUTHORITY)
inputs = list(CORE_HASHES) + [
    'include/players/QuantBotConfig.h', 'src/players/QuantBotConfig.cpp',
    'src/players/QuantBot.cpp', 'src/players/QuantBotGarrison.cpp',
    'config/QuantBot Config.ini.default', 'tests/ai/genome-registry.json',
    'tests/ai/garrison-hold-probe.inc', 'tests/ai/run-garrison-hold-probe.py',
]
provenance = {'core_authority': CORE_AUTHORITY, 'core_hashes': core_hashes,
              'inputs': {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in inputs},
              'build_dir': str(build), 'wall_timeout': args.wall_timeout}
(out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
print(f'GARRISON_HOLD_AUTHORITY: files={len(CORE_HASHES)} matched={len(core_hashes)}', flush=True)
run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
commands = native_build_commands(build)
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
require(main.count(needle) == 1, 'Main menu assertion injection point changed')
main = main.replace(needle, 'int menuResult = runGarrisonHoldProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))',
                    'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
includes = ''.join('#include "' + path.as_posix() + '"\n' for path in native_probe_includes(root))
includes += '#include "' + str(root / 'tests/ai/garrison-hold-probe.inc') + '"\n'
source, obj = out / 'garrison-hold-main.cpp', out / 'garrison-hold-main.o'
source.write_text(main[:position] + includes + main[position:], encoding='utf-8')
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
cc.append('-fno-access-control')
binary = native_probe_binary(build, out, 'garrison-hold-probe')
link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
main_objects = [index for index, argument in enumerate(link) if argument.endswith('/main.cpp.o')]
require(len(main_objects) == 1, 'Production link must contain exactly one main object')
link[main_objects[0]] = str(obj)
provenance.update(compile_command=cc, link_command=link)
with (out / 'build.log').open('w') as log:
    run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
provenance['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
(out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
for enabled in (False, True):
    (out / ('garrison-hold-on.ini' if enabled else 'garrison-hold-off.ini')).write_text(
        '[Force Concentration]\nGarrisonHold=' + str(int(enabled)) + '\n', encoding='utf-8')
with scratch_dir('garrison-hold-probe') as scratch:
    profile = scratch / 'profile'
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(
        '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
        'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\n'
        'Player Name = Hold observer\n', encoding='utf-8')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
               SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
               GARRISON_HOLD_OUT=str(out))
    env.pop('DUNECITY_TEST_GARRISON_EVACUATE_PERCENT', None)
    try:
        with (out / 'run.log').open('w') as log:
            done = run([str(binary), '--window', '--showlog', '--non-interactive'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, timeout=args.wall_timeout)
        inspect_log((out / 'run.log').read_text(errors='replace'), done.returncode, out)
    finally:
        discard_profile_assets(profile)
print('Garrison Hold activation passed. Evidence: ' + str(out), flush=True)
