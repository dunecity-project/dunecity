#!/usr/bin/env python3
"""Exercise human and QuantBot repair trips with free, busy and absent carryalls.

Uses the existing macOS Ninja build, real game objects, isolated profiles and dummy SDL.
Repeats each mod's seeded cases and compares the event cycles. Retains logs in --output-dir.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-carryall-repair-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the carryall repair injection point.')
main = main.replace(needle, 'int menuResult = runCarryallRepairProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/carryall-repair-probe.inc'
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'carryall-repair-main.cpp'
source.write_text(main)
cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
# Leave Ninja's production dependency records and precompiled header untouched.
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        i = cc.index(option)
        del cc[i:i+2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
obj = out / 'carryall-repair-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
cc.append('-fno-access-control')  # Inspect real transport/repair transitions only in this test.
app = out / 'carryall-repair-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/carryall-repair-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
for mod in ('vanilla', 'dunecity'):
    expected = None
    for repeat in (1, 2):
        tag = mod + '-' + str(repeat)
        profile = out / ('profile-' + tag)
        profile.mkdir(parents=True, exist_ok=True)
        (profile / 'Dune City.ini').write_text(
            '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\n'
            'Player Name = Carryall tester\n')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', DUNECITY_PATH_WORKERS='1', CARRYALL_REPAIR_MOD=mod)
        logfile = out / ('run-' + tag + '.log')
        with logfile.open('w') as log:
            subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
        text = logfile.read_text()
        records = [line[line.index('CARRYALL_REPAIR_PROBE_'):] for line in text.splitlines()
                   if 'CARRYALL_REPAIR_PROBE_' in line]
        if any('CARRYALL_REPAIR_PROBE_FAIL:' in line for line in records) or not records or records[-1] != 'CARRYALL_REPAIR_PROBE_PASS: ' + mod:
            raise RuntimeError('Carryall repair case failed; see ' + str(logfile))
        if expected is not None and records != expected:
            raise RuntimeError('Carryall repair event cycles were not repeatable; see ' + str(out))
        expected = records
        print('\n'.join(records))
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Carryall repair scenarios passed twice in vanilla and dunecity. Logs: ' + str(out))
