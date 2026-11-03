#!/usr/bin/env python3
"""Exercise Research Lab commands, save continuation and QuantBot with real engine objects.

Uses the single existing macOS Ninja build, an isolated leased profile and dummy SDL.
Research CSV, save, telemetry, logs and the diagnostic executable stay in --output-dir.
Run through dunecity-queue after the combined committed build.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import copy_revisions, default_build_dir, discard_profile_assets, require_free_space, scratch_dir

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--load-save', type=Path, help='Also load an old player save as Normal and resave after one real tick')
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
    discard_profile_assets(profile)
    shutil.copytree(profile, out / 'profile', dirs_exist_ok=True)
    shutil.rmtree(profile)
    print(markers[0])
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Research Lab real-engine telemetry: ' + str(out / 'research.csv'))
