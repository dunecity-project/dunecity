#!/usr/bin/env python3
"""Drive the Brutal unit-count-override production matrix through real game objects.

Requires the existing macOS Ninja Release build and bundled assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable stay in --output-dir.
"""
import argparse
import json
import re
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
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-unit-override-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runBrutalUnitOverrideProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/ai/brutal-unit-override-probe.inc'
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'unit-override-probe-main.cpp'
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
obj = out / 'unit-override-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # Inspect real builder/bot state only in this diagnostic binary.
app = out / 'unit-override-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/unit-override-probe'
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
               UNIT_OVERRIDE_PROBE_MOD=mod, UNIT_OVERRIDE_PROBE_OUT=str(out))
    profile = out / ('profile-' + mod)
    profile.mkdir(parents=True, exist_ok=True)
    (profile / 'Dune City.ini').write_text('[General]\nDiagnostic Logs = true\nPlay Intro = false\n')
    existing_events = set(profile.glob('ai-decisions/*/events.jsonl'))
    logfile = out / ('run-' + mod + '.log')
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    if 'BRUTAL_UNIT_OVERRIDE_PROBE_PASS:' not in logfile.read_text():
        raise RuntimeError('Missing override result: ' + str(logfile))
    expectation = re.search(r'OVERRIDE_QUEUE_EXPECTATION: nominal=(\d+) paid=(\d+)', logfile.read_text())
    if not expectation:
        raise RuntimeError('Missing queue-accounting expectation: ' + mod)
    nominal, paid = map(int, expectation.groups())
    plans = []
    for events in set(profile.glob('ai-decisions/*/events.jsonl')) - existing_events:
        for line in events.read_text().splitlines():
            record = json.loads(line)
            if record.get('event') == 'capital_plan' and record.get('cycle') == 17:
                plans.append(record['data'])
    if len(plans) != 1:
        raise RuntimeError('Missing unique queued-import planning record: ' + mod)
    plan = plans[0]
    if not (nominal > paid and plan['queued_military_value'] == nominal
            and plan['military_value'] == nominal and plan['committed_cost'] == 0
            and plan['military_budget'] > nominal):
        raise RuntimeError('Paid imports were miscounted or reserved twice: ' + mod)

subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Brutal unit-override matrix passed for all three modes. Logs: ' + str(out))
