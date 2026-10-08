#!/usr/bin/env python3
"""Exercise local spectator mode against the real engine in an isolated profile.

Links the production objects with a diagnostic main, exactly as tests/menu/run-menu-probe.py
and tests/ai/run-campaign-balance.py do, so no test hook ships in the app. No connection is
opened and no shared user settings are touched.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description="Watch an AI-vs-AI match and verify read-only observation.")
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--map', type=Path, required=True)
parser.add_argument('--mod', default='dunecity')
parser.add_argument('--seed', type=int, default=20260817)
parser.add_argument('--ordinary-round-trip', action='store_true',
                    help='Run the control case: an ordinary human single-player save/reload')
parser.add_argument('--setup', action='store_true',
                    help='Exercise the Offline Custom Game Spectate checkbox and roster')
parser.add_argument('--wall-timeout', type=int, default=600)
args = parser.parse_args()

build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed.')
main = main.replace(needle, 'int menuResult = runSpectatorProbeMain();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + str(root / 'tests/spectator/spectator-probe.inc') + '"\n' + main[position:]

source, obj = out / 'spectator-main.cpp', out / 'spectator-main.o'
source.write_text(main)
compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
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

app = out / 'spectator-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
if not (app / 'Resources').exists():
    (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/spectator-probe'
link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

profile = out / 'profile'
profile.mkdir(exist_ok=True)
(profile / 'Dune City.ini').write_text(
    '[Video]\nPhysical Width = 1280\nPhysical Height = 720\nWidth = 1280\nHeight = 720\n'
    'Interface Height = 720\nFullscreen = false\n'
    '[General]\nPlay Intro = false\nPlayer Name = Spectator probe\n')

env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
           SPECTATOR_MAP=str(args.map.resolve()), SPECTATOR_MOD=args.mod, SPECTATOR_SEED=str(args.seed),
           SPECTATOR_PROBE_OUT=str(out))
if args.ordinary_round_trip:
    env['SPECTATOR_ORDINARY_ROUND_TRIP'] = '1'
if args.setup:
    env['SPECTATOR_SETUP_PROBE'] = '1'

logpath = out / 'run.log'
with logpath.open('w') as log:
    subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.wall_timeout)

text = logpath.read_text(errors='replace')
results = [line for line in text.splitlines() if 'SPECTATOR_PROBE_PASS:' in line]
if not results:
    raise RuntimeError('Missing spectator probe result; see ' + str(logpath))
for line in [l for l in text.splitlines() if 'SPECTATOR_PROBE_DETAIL:' in l] + results:
    print(line.split('SPECTATOR_PROBE_', 1)[1])
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
