#!/usr/bin/env python3
"""Exercise exact technology installments, legacy start gates, saves and real 640x480 UI.

Uses the single existing macOS Ninja build, an isolated leased source-default profile
and dummy SDL. CSV, summary, saves, UI PNGs, telemetry and logs stay in --output-dir.
Run through dunecity-queue after the combined committed build.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import struct
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import copy_revisions, default_build_dir, discard_profile_assets, require_free_space, scratch_dir

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--load-save', type=Path, help='Also load an exact player save, preserve captured/paid timers, then resave/reload the current format')
parser.add_argument('--save-profile', type=Path, help='Profile holding the exact Workshop revisions pinned by --load-save')
args = parser.parse_args()
if args.load_save and not args.load_save.is_file():
    parser.error('--load-save needs an existing saved game')
if args.save_profile and not args.load_save:
    parser.error('--save-profile needs --load-save')
build, out = args.build_dir.resolve(), args.output_dir.resolve()
require_free_space()
out.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text(encoding='utf-8')
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed.')
main = main.replace(needle, 'int menuResult = runResearchLabProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + (root / 'tests/units/research-lab-probe.inc').as_posix() + '"\n' + main[position:]
source, obj = out / 'research-lab-main.cpp', out / 'research-lab-main.o'
source.write_text(main, encoding='utf-8')
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
app = out / 'research-lab-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
if not (app / 'Resources').exists():
    (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/research-lab-probe'
link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
with scratch_dir('research-lab-probe') as scratch:
    profile = scratch / 'profile'
    profile.mkdir()
    if args.save_profile:
        copy_revisions(args.save_profile, profile, [args.load_save])
    (profile / 'Dune City.ini').write_text(
        '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
        'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\nPlayer Name = Research tester\n',
        encoding='utf-8')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
               SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
               DUNECITY_AI_TELEMETRY='1', RESEARCH_LAB_PROBE_OUT=str(out))
    if args.load_save:
        env['RESEARCH_LAB_LOAD_SAVE'] = str(args.load_save.resolve())
    logfile = out / 'run.log'
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
    text = logfile.read_text(errors='replace')
    markers = [line for line in text.splitlines() if 'RESEARCH_LAB_PROBE_PASS:' in line]
    if len(markers) != 1 or 'RESEARCH_LAB_PROBE_FAIL:' in text:
        raise RuntimeError('Missing or failed Research Lab result; see ' + str(logfile))
    lines = text.splitlines()
    totals = [line for line in lines if 'RESEARCH_INSTALLMENT_TOTAL:' in line]
    if len(totals) != 32:
        raise RuntimeError('Expected every research pace x levels 2-9; see ' + str(logfile))
    legacy_versions = [int(match.group(1)) for match in re.finditer(
        r'RESEARCH_LEGACY_LAYOUT_PASS: version=(\d+)', text)]
    if sorted(legacy_versions) != [9882, 9883]:
        raise RuntimeError('Missing explicitly synthetic 9882/9883 full-save migration cases')
    upgrade_totals = [line for line in lines if 'UPGRADE_INSTALLMENT_TOTAL:' in line]
    upgrade_saves = [line for line in lines if 'UPGRADE_SAVE_CONTINUATION_PASS:' in line]
    upgrade_ai_partial = [line for line in lines if 'UPGRADE_AI_PARTIAL_PASS:' in line]
    upgrade_legacy = [int(match.group(1)) for match in re.finditer(
        r'UPGRADE_LEGACY_LAYOUT_PASS: version=(\d+)', text)]
    if len(upgrade_totals) != 4 or len(upgrade_saves) != 2 or len(upgrade_ai_partial) != 2:
        raise RuntimeError('Missing yard/factory upgrade cases: human+AI exact legacy-tick, partial AI, or full-save continuation')
    if sorted(upgrade_legacy) != [9882, 9882, 9883, 9883]:
        raise RuntimeError('Missing explicitly synthetic old-save yard/factory upgrade gate cases')
    if sum('RESEARCH_QUANTBOT_POLICY: installed_reference=3710464d no_experimental_overlay=1' in line for line in lines) != 1:
        raise RuntimeError('Natural QuantBot case did not use the installed-reference policy')
    states = ('installment_available', 'installment_paused', 'installment_partial',
              'installment_researching', 'installment_complete', 'upfront_researching')
    captures = [out / ('research-ui-' + state + '-640x480.png') for state in states]
    captures.append(out / 'research-ui-options-640x480.png')
    upgrade_states = tuple(kind + '_' + phase for kind in ('cy', 'factory')
                           for phase in ('ready_partial', 'active', 'paused', 'partial', 'completed'))
    captures.extend(out / ('upgrade-ui-' + state + '-640x480.png') for state in upgrade_states)
    for capture in captures:
        data = capture.read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR' or struct.unpack('>II', data[16:24]) != (640, 480):
            raise RuntimeError('Missing or incorrectly sized real UI capture: ' + str(capture))
    if sum('RESEARCH_UI_PROBE:' in line for line in lines) != len(states):
        raise RuntimeError('Missing real-state research panel geometry assertions')
    if sum('RESEARCH_UI_OPTIONS:' in line for line in lines) != 1 or sum('RESEARCH_UI_COMMAND:' in line for line in lines) != 1:
        raise RuntimeError('Missing rendered options or ordinary human Research-button command assertions')
    if sum('UPGRADE_UI_PROBE:' in line for line in lines) != len(upgrade_states):
        raise RuntimeError('Missing real-state yard/factory upgrade panel geometry assertions')
    if sum('UPGRADE_UI_COMMAND:' in line for line in lines) != 2:
        raise RuntimeError('Missing ordinary human yard/factory Upgrade-button command assertions')
    with (out / 'research.csv').open(newline='') as handle:
        rows = list(csv.DictReader(handle))
    actual = [(int(row['pace']), int(row['tech_level'])) for row in rows if row['case'] == 'installment_exact_total']
    if sorted(actual) != [(pace, target) for pace in range(4) for target in range(2, 10)]:
        raise RuntimeError('Research CSV omitted or duplicated a pace/target exact-payment result')
    required = {'installment_partial_before_save', 'installment_exact_resume_complete',
                'installment_cancel_no_refund', 'upfront_rule_change_complete', 'synthetic_legacy_paid_complete',
                'active_before_save', 'completed_after_reload', 'off_legacy_digest',
                'map_independent_quote', 'pace_slow_complete', 'quantbot_built_lab', 'quantbot_final'}
    missing = required - {row['case'] for row in rows}
    if missing:
        raise RuntimeError('Research fixture omitted required cases: ' + ', '.join(sorted(missing)))
    with (out / 'upgrades.csv').open(newline='') as handle:
        upgrades = list(csv.DictReader(handle))
    exact = [(row['controller'], int(row['item'])) for row in upgrades if row['case'] == 'exact_legacy_ticks']
    items = {item for _, item in exact}
    if len(items) != 2 or sorted(exact) != sorted((controller, item) for controller in ('human', 'quantbot') for item in items):
        raise RuntimeError('Upgrade CSV omitted or duplicated human/AI yard/factory exact-payment cases')
    for case, controller in (('human_partial_saved', 'human'), ('human_resume_complete', 'human'),
                             ('ai_partial_resume_complete', 'quantbot')):
        actual = [(row['controller'], int(row['item'])) for row in upgrades if row['case'] == case]
        if sorted(actual) != sorted((controller, item) for item in items):
            raise RuntimeError('Upgrade CSV omitted or duplicated required cases: ' + case)
    legacy = [(int(row['legacy_version']), int(row['item'])) for row in upgrades if row['case'] == 'legacy_full_price_gate']
    if sorted(legacy) != sorted((version, item) for version in (9882, 9883) for item in items):
        raise RuntimeError('Upgrade CSV omitted or duplicated synthetic old-save full-price gates')
    summary = {'source_default_technology_installments_off': True, 'ai_policy_reference': '3710464d', 'exact_pace_level_cases': len(totals),
               'synthetic_legacy_save_versions': legacy_versions, 'external_player_save': bool(args.load_save),
               'ui_resolution': [640, 480], 'ui_states': list(states), 'upgrade_ui_states': list(upgrade_states),
               'upgrade_fully_funded_cases': len(upgrade_totals), 'upgrade_save_continuations': len(upgrade_saves),
               'upgrade_ai_partial_cases': len(upgrade_ai_partial), 'upgrade_synthetic_legacy_versions': upgrade_legacy,
               'cases': sorted({row['case'] for row in rows}), 'upgrade_cases': sorted({row['case'] for row in upgrades}),
               'result': 'pass'}
    (out / 'research-summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    discard_profile_assets(profile)
    shutil.copytree(profile, out / 'profile', dirs_exist_ok=True)
    shutil.rmtree(profile)
    print(markers[0])
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Research Lab real-engine telemetry: ' + str(out / 'research.csv'))
print('Technology upgrade real-engine telemetry: ' + str(out / 'upgrades.csv'))
print('Research Lab case matrix and rendered evidence: ' + str(out / 'research-summary.json'))
