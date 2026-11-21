#!/usr/bin/env python3
"""Measure carryall rotation, approach, pickup and delivery using real game objects.

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable are retained in --output-dir.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import NATIVE_TARGET, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402
NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--record-baseline', action='store_true')
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-carryall-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = NATIVE_TARGET
lines = native_build_commands(build)
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runCarryallFlightProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/carryall-flight-probe.inc'
main = main[:pos] + NATIVE_INCLUDES + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'carryall-probe-main.cpp'
source.write_text(main)
cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
# Test compilation must not overwrite Ninja's production dependency records.
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        i = cc.index(option)
        del cc[i:i+2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
obj = out / 'carryall-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # Inspect real sidebar/modal state only in this diagnostic binary.
binary = native_probe_binary(build, out, 'carryall-probe')
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
for mod in ('vanilla', 'dunecity', 'Dune2R'):
    env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + mod)),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               CARRYALL_PROBE_MOD=mod, CARRYALL_PROBE_OUT=str(out),
               CARRYALL_PROBE_BASELINE='1' if args.record_baseline else '0')
    logfile = out / ('run-' + mod + '.log')
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
    if 'CARRYALL_FLIGHT_PROBE_PASS:' not in logfile.read_text():
        raise RuntimeError('Missing carryall result: ' + str(logfile))
if len({(out / (mod + '.csv')).read_bytes() for mod in ('vanilla', 'dunecity', 'Dune2R')}) != 1:
    raise RuntimeError('The three modes produced different carryall trajectories')
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Carryall flight scenarios passed for all three modes. Logs: ' + str(out))
