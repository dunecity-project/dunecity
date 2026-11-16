#!/usr/bin/env python3
"""Units a map starts with on a spice or special bloom load alive, visible and targetable.

Builds a fixture from the bundled Habbanya-Autumn map with an enemy Tank (half health) and an
own Trike on spice blooms and an enemy Quad on a special bloom, then checks in the real engine
that each one loads with its [UNITS] health, is visible to every team, leaves its bloom intact,
and that no unit is ever alive but invisible during the first 120 cycles. A Quad that arrives
on the remaining bloom must still set it off and be removed.

Requires the existing native Release build and bundled game assets. Uses fresh isolated profiles
and dummy SDL drivers. Logs and the test executable are retained in --output-dir.
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
from probe_profile import (NATIVE_TARGET, managed_profile, native_build_commands,  # noqa: E402
                           native_probe_binary, native_probe_includes, remove_tree)
NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
MODS = ('vanilla', 'dunecity')
# (x, y) of the four spice blooms ('O') on the 51x31 map; positions are y * 51 + x.
SIZE_X = 51
SPICE_BLOOMS = ((4, 6), (29, 6), (21, 24), (46, 24))
SPECIAL_BLOOM = (46, 24)
START_UNITS = (  # house, unit, INI health, tile; must match startUnits in bloom-placement-probe.inc
    ('Harkonnen', 'Tank', 128, (4, 6)),
    ('Atreides', 'Trike', 256, (29, 6)),
    ('Harkonnen', 'Quad', 192, SPECIAL_BLOOM),
)


def fixture_map(source):
    lines = source.read_text().splitlines()
    rows = {int(line[:3]): i for i, line in enumerate(lines) if line[:3].isdigit() and line[3:4] == '='}
    for x, y in SPICE_BLOOMS:
        if lines[rows[y]][4 + x] != 'O':
            raise RuntimeError(f'{source.name}: no spice bloom at ({x},{y}); update the fixture')
    x, y = SPECIAL_BLOOM
    row = lines[rows[y]]
    lines[rows[y]] = row[:4 + x] + 'Q' + row[5 + x:]
    units = lines.index('[UNITS]')
    end = next((i for i in range(units + 1, len(lines)) if lines[i].startswith('[')), len(lines))
    added = [f'ID{100 + i}={house},{unit},{health},{ty * SIZE_X + tx},64,Area Guard'
             for i, (house, unit, health, (tx, ty)) in enumerate(START_UNITS)]
    lines[end:end] = added
    return '\n'.join(lines) + '\n'


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-bloom-placement-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
map_path = out / 'bloom-placement.ini'
map_path.write_text(fixture_map(root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'))
target = NATIVE_TARGET
lines = native_build_commands(build)
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runBloomPlacementProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/bloom-placement-probe.inc'
main = main[:pos] + NATIVE_INCLUDES + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'bloom-placement-probe-main.cpp'
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
obj = out / 'bloom-placement-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # Drive the private game loop only in this diagnostic binary.
binary = native_probe_binary(build, out, 'bloom-placement-probe')
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
for mod in MODS:
    profile = out / mod / 'profile'  # managed_profile only adopts a directory named 'profile'
    if profile.exists():
        remove_tree(profile)  # every run starts from a fresh profile
    with managed_profile(profile):
        env = dict(os.environ, DUNECITY_USERDIR=str(profile),
                   SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', BLOOM_PLACEMENT_PROBE_MOD=mod)
        logfile = out / ('run-' + mod + '.log')
        with logfile.open('w') as log:
            result = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=120)
        text = logfile.read_text()
        print('\n'.join(line for line in text.splitlines() if 'BLOOM_PLACEMENT_' in line))
        if result.returncode != 0 or 'BLOOM_PLACEMENT_PROBE_PASS:' not in text:
            raise RuntimeError(f'Bloom placement probe failed in {mod} (exit {result.returncode}): {logfile}')
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Units started on blooms stayed alive and visible; an arriving unit still set its bloom off. Logs: ' + str(out))
