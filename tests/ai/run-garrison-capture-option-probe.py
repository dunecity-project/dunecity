#!/usr/bin/env python3
"""Assert the real city permission menu, INI persistence and human/AI ON/OFF activation."""
import argparse
from collections import Counter
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

processes.install_signal_handlers()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
require_free_space()
run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
commands = native_build_commands(build)
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point')
main = main.replace(needle, 'int menuResult = runGarrisonCaptureOptionProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))',
                    'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
includes = ''.join('#include "' + path.as_posix() + '"\n' for path in native_probe_includes(root))
includes += '#include "' + str(root / 'tests/ai/garrison-capture-option-probe.inc') + '"\n'
source, obj = out / 'garrison-capture-main.cpp', out / 'garrison-capture-main.o'
source.write_text(main[:position] + includes + main[position:])
cc = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        i = cc.index(option)
        del cc[i:i + 2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
cc.extend(['-fno-access-control', '-DGARRISON_CAPTURE_MAP_PATH="'
           + str(root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini') + '"'])
binary = native_probe_binary(build, out, 'garrison-capture-option-probe')
link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

profile_ini = (
    '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
    'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\n'
    'Player Name = Garrison observer\n')

with scratch_dir('garrison-capture-option-probe') as scratch:
    profile = scratch / 'profile'
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(profile_ini, encoding='utf-8')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
               SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1')
    logfile = out / 'run.log'
    with logfile.open('w') as log:
        done = run([str(binary), '--window', '--showlog', '--non-interactive'], cwd=out, env=env,
                   stdout=log, stderr=subprocess.STDOUT, timeout=180)
    text = logfile.read_text(errors='replace')
    for line in text.splitlines():
        if 'GARRISON_CAPTURE_' in line:
            print(line[line.index('GARRISON_CAPTURE_'):], flush=True)
    if done.returncode != 0 or text.count('GARRISON_CAPTURE_PASS:') != 1:
        raise RuntimeError('Garrisoning and Capturing option probe failed: ' + str(logfile))
    events, arm = {0: Counter(), 1: Counter()}, None
    human_arms = set()
    for line in text.splitlines():
        begin = re.search(r'GARRISON_CAPTURE_ARM: option=([01]) begin=1 configured=([01])', line)
        if begin:
            arm = int(begin[1])
            if int(begin[2]) != 1 - arm:
                raise RuntimeError('The AI permission arms did not oppose the independent config switches')
        human = re.search(r'GARRISON_CAPTURE_HUMAN: option=([01]) own=(\d+) enemy=(\d+) captured=([01]).*pass=1', line)
        if human:
            expected = (2, 2, 1) if human[1] == '1' else (0, 0, 0)
            if tuple(map(int, human.group(2, 3, 4))) != expected:
                raise RuntimeError('The real human Capture arm ignored the selected permission')
            human_arms.add(int(human[1]))
        if arm is not None:
            event = re.search(r'ZONE_GARRISON: event=(order|entered)\b', line)
            if event:
                events[arm][event[1]] += 1
        if 'GARRISON_CAPTURE_ARM:' in line and 'pass=1' in line:
            arm = None
    if events[0]['order'] or events[0]['entered']:
        raise RuntimeError('The OFF permission admitted configured AI garrison/capture tactics: ' + str(events[0]))
    if not events[1]['order'] or not events[1]['entered']:
        raise RuntimeError('The ON option did not issue AND complete real AI garrison orders: ' + str(events[1]))
    if human_arms != {0, 1}:
        raise RuntimeError('The real human ON/OFF Capture assertions did not both execute')
    print('GARRISON_CAPTURE_ACTIVATION: off=' + str(dict(events[0]))
          + ' on=' + str(dict(events[1])), flush=True)
    discard_profile_assets(profile)
print('Garrisoning and Capturing: real menu, INI, settings and human/AI permission passed. Logs: ' + str(out))

migrations = (
    ('legacy-off', 'Zone Garrisons = false\nAI Infantry Tactics = true\n',
     'Zone Garrisons = false\nAI Infantry Tactics = true\n', '00'),
    ('legacy-on-ai-off', 'Zone Garrisons = true\nAI Infantry Tactics = false\n',
     'Zone Garrisons = true\nAI Infantry Tactics = false\n', '11'),
    ('new-key-wins', 'Garrisoning and Capturing = false\nZone Garrisons = true\nAI Infantry Tactics = true\n',
     'Garrisoning and Capturing = true\nZone Garrisons = false\nAI Infantry Tactics = false\n', '01'),
    ('ai-only-off', 'AI Infantry Tactics = false\n', 'AI Infantry Tactics = false\n', '11'),
)
for name, global_options, city_options, expected in migrations:
    with scratch_dir('garrison-capture-migration-' + name) as scratch:
        profile = scratch / 'profile'
        profile.mkdir()
        (profile / 'Dune City.ini').write_text(
            profile_ini + '[Game Options]\n' + global_options + '[Game Options dunecity]\n' + city_options
            + '[gAmE oPtIoNs archived]\nZone Garrisons = false\nAI Infantry Tactics = true\n'
            + '[Unrelated]\nZone Garrisons = untouched\nAI Infantry Tactics = untouched\n', encoding='utf-8')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
                   GARRISON_CAPTURE_MIGRATION=expected)
        logfile = out / ('migration-' + name + '.log')
        with logfile.open('w') as log:
            done = run([str(binary), '--window', '--showlog', '--non-interactive'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, timeout=60)
        text = logfile.read_text(errors='replace')
        if done.returncode != 0 or text.count('GARRISON_CAPTURE_MIGRATION:') != 1 or 'pass=1' not in text:
            raise RuntimeError('Real-startup migration failed (' + name + '): ' + str(logfile))
        print('GARRISON_CAPTURE_MIGRATION_CASE: name=' + name + ' expected=' + expected + ' pass=1', flush=True)
        discard_profile_assets(profile)
