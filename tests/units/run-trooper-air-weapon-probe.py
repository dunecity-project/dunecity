#!/usr/bin/env python3
"""Check that real Troopers answer an Ornithopter with their rocket anywhere in weapon range.

Uses the existing Ninja Release game objects; it does not configure or build another
CMake tree. The diagnostic main drives real Troopers (alone and as the three a Troopers
order deploys) on unexplored ground against an Ornithopter overhead, adjacent, two and
three tiles away, at the edge of weapon range and one tile beyond it, found by the
engine's own target search and by a player attack order, with the Trooper Anti-Air
match rule off and on. A real attack run, a Carryall and ground targets are the
controls: other aircraft and ground targets keep the sight rule, the gun stays the
weapon against an adjacent ground unit, and content that ignores the rule must play
both switch states identically. Results: shots.csv and a log per mod.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import NATIVE_TARGET, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402
NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
from probe_profile import default_build_dir, discard_profile_assets, require_free_space, scratch_dir  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--mods', default='dunecity,vanilla,Dune2R',
                    help='Comma-separated installed mod ids')
parser.add_argument('--wall-timeout', type=int, default=300, help='Maximum runtime seconds per mod')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
mods = [mod for mod in args.mods.split(',') if mod]
if not mods or len(set(mods)) != len(mods) or any('/' in mod or '\\' in mod or mod in ('.', '..') for mod in mods):
    parser.error('--mods must contain distinct installed mod ids, not paths')
require_free_space()
out.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

commands = native_build_commands(build)
main = (root / 'src/main.cpp').read_text(encoding='utf-8')
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed; update the Trooper air weapon runner.')
main = main.replace(needle, 'int menuResult = runTrooperAirWeaponProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = (main[:position] + NATIVE_INCLUDES + '#include "'
        + (root / 'tests/units/trooper-air-weapon-probe.inc').as_posix() + '"\n' + main[position:])
source, obj = out / 'trooper-air-weapon-main.cpp', out / 'trooper-air-weapon-main.o'
source.write_text(main, encoding='utf-8')
compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
# A private diagnostic object must not overwrite Ninja's production depfile/PCH records.
for option in ('-include', '-MT', '-MF'):
    if option in compile_command:
        position = compile_command.index(option)
        del compile_command[position:position + 2]
for option in ('-MD', '-MMD'):
    if option in compile_command:
        compile_command.remove(option)
compile_command[compile_command.index('-o') + 1] = str(obj)
compile_command[compile_command.index('-c') + 1] = str(source)
compile_command.append('-fno-access-control')
binary = native_probe_binary(build, out, 'trooper-air-weapon-probe')
link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

with scratch_dir('trooper-air-weapon-probe') as scratch:
    for mod in mods:
        profile, results = scratch / ('profile-' + mod), out / mod
        profile.mkdir()
        results.mkdir(parents=True, exist_ok=True)
        (profile / 'Dune City.ini').write_text(
            '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\nPlayer Name = AA tester\n',
            encoding='utf-8')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
                   TROOPER_AIR_WEAPON_PROBE_MOD=mod, TROOPER_AIR_WEAPON_PROBE_OUT=str(results))
        logfile = results / 'run.log'
        with logfile.open('w') as log:
            subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.wall_timeout)
        text = logfile.read_text(errors='replace')
        for line in text.splitlines():
            if 'TROOPER_AIR_WEAPON_CASE:' in line:
                print(line[line.index('TROOPER_AIR_WEAPON_CASE:'):])
        markers = [line for line in text.splitlines() if 'TROOPER_AIR_WEAPON_PROBE_PASS:' in line]
        if len(markers) != 1 or 'TROOPER_AIR_WEAPON_PROBE_FAIL:' in text:
            raise RuntimeError('Missing or failed Trooper air weapon result; see ' + str(logfile))
        discard_profile_assets(profile)
        print(markers[0][markers[0].index('TROOPER_AIR_WEAPON_PROBE_PASS:'):])
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Trooper air weapon telemetry: ' + str(out))
