#!/usr/bin/env python3
"""The Natives house (worms and city-crime troopers) against the real engine, in isolated profiles.

Links a diagnostic copy of the game whose main menu is replaced by tests/natives/natives-probe.inc
(production objects are unchanged) and runs one process per case:

  maps       every sandworm is the Natives': vanilla Equilibrium (worms written as a played house's),
             DuneCity Habbanya-Penny (worms naming an absent house: no anonymous house is created),
             a Random Skirmish with a worm in its INI, worms spawned later by a player house, and a
             campaign mission; Natives is in no house choice, observer selector or statistics row;
             the inspect panel names 'Natives' and the radar draws its own colour
  crime      DuneCity: city crime raises Natives troopers that hunt the unrest's building, are no
             army in QuantBot's threat estimate, come back as the Natives' after a save and reload,
             and are fought by the house's defence
  winlose    a played game is won on the last enemy with Natives units alive; the Natives losing
             every unit ends nothing; a spectated free-for-all ends naming the last real team
  save9868   tests/natives/data/worms-9868.dls.gz (Gamma Sector, worms owned by a played house)
             loads with every worm the Natives'; re-saved as the current version it stays so

--baseline links against a tree without the Natives house (e.g. a worktree at afb2315c) and runs
the case that writes the 9868 fixture: make-save.
"""
import argparse
import gzip
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--cases', default='maps,crime,winlose,save9868')
parser.add_argument('--baseline', action='store_true', help='Link against a tree without the Natives house; make-save only')
parser.add_argument('--keep', action='store_true', help='Keep the profiles and the probe executable')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
if args.baseline and args.cases == parser.get_default('cases'):
    args.cases = 'make-save'
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

# The tree the build was configured from (the baseline's main.cpp, not this one's).
source_root = Path(next(line.split('=', 1)[1] for line in (build / 'CMakeCache.txt').read_text().splitlines()
                        if line.startswith('CMAKE_HOME_DIRECTORY:')))
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (source_root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed.')
main = main.replace(needle, 'int menuResult = runNativesProbe();')
clamp = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
if main.count(clamp) != 1:
    raise RuntimeError('Window-size fixture injection point changed.')
main = main.replace(clamp, '(void)displayIndex;')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + str(root / 'tests/natives/natives-probe.inc') + '"\n' + main[position:]
work = Path(tempfile.mkdtemp(prefix='natives-probe-build-', dir=out))
source, obj = work / 'natives-main.cpp', work / 'natives-main.o'
source.write_text(main)
compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
for option in ('-include', '-MT', '-MF'):
    if option in compile_command:
        index = compile_command.index(option)
        del compile_command[index:index + 2]
for option in ('-MD', '-MMD'):
    if option in compile_command:
        compile_command.remove(option)
compile_command[compile_command.index('-o') + 1] = str(obj)
compile_command[compile_command.index('-c') + 1] = str(source)
compile_command.append('-fno-access-control')
if args.baseline:
    compile_command.append('-DNATIVES_BASELINE')
app = work / 'natives-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True)
(app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/natives-probe'
link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (work / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

fixture = work / 'worms-9868.dls'
packed = root / 'tests/natives/data/worms-9868.dls.gz'
if packed.exists():
    fixture.write_bytes(gzip.decompress(packed.read_bytes()))

summary, failed = [], []
for case in args.cases.split(','):
    profile = work / ('profile-' + case)
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(
        '[Video]\nPhysical Width = 1024\nPhysical Height = 768\nWidth = 1024\nHeight = 768\n'
        'Interface Height = 768\nFullscreen = false\n[General]\nPlay Intro = false\nPlayer Name = Natives tester\n')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NATIVES_PROBE_OUT=str(out), NATIVES_PROBE_CASE=case, NATIVES_SOURCE_DIR=str(root),
               NATIVES_SAVE_9868=str(fixture))
    logpath = out / f'run-{case}.log'
    with logpath.open('w') as log:
        result = subprocess.run([str(binary), '--window', '--showlog'], cwd=work, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=900)
    text = logpath.read_text(errors='replace')
    lines = [line[line.index('NATIVES_PROBE'):] for line in text.splitlines() if 'NATIVES_PROBE' in line]
    summary += [f'[{case}]'] + lines
    if result.returncode != 0 or text.count('NATIVES_PROBE_PASS:') != 1:
        failed.append(f'{case} (exit {result.returncode}; see {logpath})')

(out / 'summary.txt').write_text('\n'.join(summary) + '\n')
print('\n'.join(summary))
if not args.keep:
    shutil.rmtree(work, ignore_errors=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
if failed:
    raise SystemExit('Failed: ' + ', '.join(failed))
