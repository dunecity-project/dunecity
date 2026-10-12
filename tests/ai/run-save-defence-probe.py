#!/usr/bin/env python3
"""Play a real saved game forward and measure how each QuantBot defends.

Reports, every interval, how many of each AI house's free ground troops are
fighting, walking to a fight or idle beside one of its buildings or harvesters
under fire, plus building damage, buildings lost and enemies killed near its
assets. Requires the existing macOS Ninja build and its bundled game data.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import prepare_profile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--save', type=Path, required=True)
parser.add_argument('--cycles', type=int, default=7500, help='Cycles to play (62.5 per second)')
parser.add_argument('--interval', type=int, default=125, help='Cycles between samples')
parser.add_argument('--profile-from', type=Path,
                    help='Copy settings and installed mods from this profile so the exact saved '
                         'mod revision resolves offline')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=False)
subprocess.run(['python3', str(root/'scripts/check-build-deps.py'), str(build)], check=True)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target], text=True).splitlines()
main = (root/'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1: raise RuntimeError('Main-menu injection point changed')
main = main.replace(needle,'int menuResult = runSaveDefenceProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))','if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position]+'#include "'+str(root/'tests/ai/save-defence-probe.inc')+'"\n'+main[position:]
(out/'probe-main.cpp').write_text(main)

command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
for option in ('-include','-MT','-MF'):
    if option in command:
        position = command.index(option)
        del command[position:position+2]
for option in ('-MD','-MMD'):
    if option in command: command.remove(option)
obj = out/'probe-main.o'
command[command.index('-o')+1] = str(obj)
command[command.index('-c')+1] = str(out/'probe-main.cpp')
command.append('-fno-access-control')  # Inspect the production scheduler state in this diagnostic binary.

app = out/'defence-probe.app/Contents'
(app/'MacOS').mkdir(parents=True)
(app/'Resources').symlink_to(build/'bin/dunecity.app/Contents/Resources')
binary = app/'MacOS/defence-probe'
link = shlex.split(next(line for line in commands if ' -o '+target+' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out/'build.log').open('w') as log:
    subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
profile = out/'profile'
active = prepare_profile(profile, args.profile_from)
if active: print('DEFENCE_PROBE_PROFILE: active_mod='+active)
env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
           DUNECITY_AI_TELEMETRY='1', DEFENCE_PROBE_SAVE=str(args.save.resolve()),
           DEFENCE_PROBE_CYCLES=str(args.cycles), DEFENCE_PROBE_INTERVAL=str(args.interval))
with (out/'run.log').open('w') as log:
    subprocess.run([str(binary),'--window','--showlog'], cwd=out, env=env,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=3600)
results = [line for line in (out/'run.log').read_text().splitlines() if 'DEFENCE_PROBE_' in line]
if not any('DEFENCE_PROBE_PASS:' in line for line in results):
    raise RuntimeError('Missing defence probe result: '+str(out/'run.log'))
print('\n'.join(line[line.index('DEFENCE_PROBE_'):] for line in results if 'SAMPLE' not in line))
