#!/usr/bin/env python3
"""Exercise transport containment against the real engine.

Collects a badly damaged booked unit with a free repair yard and a second free carrier present,
and checks what the engine actually enrols as cargo, which aircraft keeps the passenger, and that
the repair trip still happens. Then checks the recovery of units that are already hidden inside
nothing - the shape old saves carry - without disturbing valid cargo, refinery or repair-bay
occupants, including an occupied tile, no legal tile at all, carrier and structure destruction,
the scheduled cadence, determinism against the shared random generator, and elimination once a
recovered unit is destroyed normally.

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable are retained in --output-dir.
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
parser.add_argument('--mods', default='vanilla,dunecity,Dune2R',
                    help='Comma-separated mods to run the fixture under')
parser.add_argument('--worker-counts', default='1,4',
                    help='DUNECITY_PATH_WORKERS values the first mod is repeated under; the '
                         'boundary continuation must be identical across them')
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-containment-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runTransportContainmentProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/transport-containment-probe.inc'
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'containment-probe-main.cpp'
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
obj = out / 'containment-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # Inspect real unit state only in this diagnostic binary.
app = out / 'containment-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/containment-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
mods = [name for name in args.mods.split(',') if name]
workers = [value for value in args.worker_counts.split(',') if value]


def run(mod, path_workers, tag, profile=None):
    # The profile is a parameter because the worker-count comparison has to differ in exactly one
    # thing: a fresh profile changes the mod checksum strings the save carries, which says nothing
    # about the simulation.
    env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + (profile or tag))),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               CONTAINMENT_PROBE_MOD=mod, CONTAINMENT_PROBE_OUT=str(out / tag),
               DUNECITY_PATH_WORKERS=path_workers)
    (out / tag).mkdir(parents=True, exist_ok=True)
    logfile = out / ('run-' + tag + '.log')
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
    text = logfile.read_text()
    if 'TRANSPORT_CONTAINMENT_PROBE_PASS:' not in text:
        raise RuntimeError('Missing transport-containment result: ' + str(logfile))
    if 'TRANSPORT_CONTAINMENT_PROBE_FAIL:' in text:
        raise RuntimeError('Transport-containment check failed: ' + str(logfile))
    # The boundary continuation, with the saved-byte checksum: this is what must not depend on
    # how many path workers happened to be running.
    return [line.split('TRANSPORT_CONTAINMENT_PROBE_')[1] for line in text.splitlines()
            if 'TRANSPORT_CONTAINMENT_PROBE_TAILDIGEST' in line
            or 'TRANSPORT_CONTAINMENT_PROBE_CONTINUATION' in line]


continuations = {}
for mod in mods:
    continuations[mod] = run(mod, workers[0], mod + '-workers' + workers[0])
    print('transport containment passed: mod=' + mod + ' workers=' + workers[0])
for extra in workers[1:]:
    tag = mods[0] + '-workers' + extra
    repeated = run(mods[0], extra, tag, profile=mods[0] + '-workers' + workers[0])
    if repeated != continuations[mods[0]]:
        raise RuntimeError('Worker count ' + extra + ' changed the boundary continuation: '
                           + str(out / ('run-' + tag + '.log')))
    print('boundary continuation identical: mod=' + mods[0] + ' workers='
          + workers[0] + ' vs ' + extra)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Transport containment verified for: ' + args.mods + '. Logs: ' + str(out))
