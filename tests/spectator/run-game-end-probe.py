#!/usr/bin/env python3
"""How a game ends for a view without a side, against the real engine, in isolated profiles.

Links a diagnostic copy of the game whose main menu is replaced by tests/spectator/game-end-probe.inc
(production objects are unchanged) and runs one process per case:

  vanilla-ffa, dunecity-ffa      spectated Random Skirmish, four houses each on its own team: the
                                 watched house and all but one enemy go out; the game ends at once
                                 with that enemy named the winner (in its colour) in the game and
                                 in the statistics, then leaves for the menu
  vanilla-teams, dunecity-teams  spectated 2v2: ends when the second house of a team goes out, the
                                 other team named the winner
  played                         control: a played skirmish still ends as a win on the last enemy and
                                 as a loss on the player's own house, with the original lines
  eliminated-watcher             Equilibrium (WinFlags=1): a player whose house is out keeps watching,
                                 and the game ends when one AI is left
  quota                          Silicon Valley quota: spectated, any house's quota wins; played, only
                                 the player's own (original rule)
  timeout                        a one-minute time limit on a spectated free-for-all: no winner
  multiplayer                    two networked players and a spectator on one simulation: the host's
                                 loss leaves the spectator watching; the guest's win ends the
                                 spectator's game at the same cycle with the same state digest
  natural-1v1                    (not in the default set: minutes of play) a spectated vanilla 1v1
                                 the AIs play out themselves, Brutal against Easy

--baseline links the cases against a tree without the fix and only reports what happens.
"""
import argparse
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
parser.add_argument('--cases', default='vanilla-ffa,dunecity-ffa,vanilla-teams,dunecity-teams,played,'
                                       'eliminated-watcher,quota,timeout,multiplayer')
parser.add_argument('--baseline', action='store_true', help='Report only; for a build without the fix')
parser.add_argument('--keep', action='store_true', help='Keep the profiles and the probe executable')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed.')
main = main.replace(needle, 'int menuResult = runGameEndProbe();')
clamp = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
if main.count(clamp) != 1:
    raise RuntimeError('Window-size fixture injection point changed.')
main = main.replace(clamp, '(void)displayIndex;')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + str(root / 'tests/spectator/game-end-probe.inc') + '"\n' + main[position:]
work = Path(tempfile.mkdtemp(prefix='game-end-probe-build-', dir=out))
source, obj = work / 'game-end-main.cpp', work / 'game-end-main.o'
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
    compile_command.append('-DGAME_END_BASELINE')
app = work / 'game-end-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True)
(app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/game-end-probe'
link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (work / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

summary, failed = [], []
for case in args.cases.split(','):
    profile = work / ('profile-' + case)
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(
        '[Video]\nPhysical Width = 1024\nPhysical Height = 768\nWidth = 1024\nHeight = 768\n'
        'Interface Height = 768\nFullscreen = false\n[General]\nPlay Intro = false\nPlayer Name = Game end tester\n')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               GAME_END_PROBE_OUT=str(out), GAME_END_PROBE_CASE=case, GAME_END_SOURCE_DIR=str(root))
    logpath = out / f'run-{case}.log'
    with logpath.open('w') as log:
        result = subprocess.run([str(binary), '--window', '--showlog'], cwd=work, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=1800 if case == 'natural-1v1' else 600)
    text = logpath.read_text(errors='replace')
    lines = [line[line.index('GAME_END_'):] for line in text.splitlines() if 'GAME_END_' in line]
    summary += [f'[{case}]'] + lines
    if result.returncode != 0 or text.count('GAME_END_PROBE_PASS:') != 1:
        failed.append(f'{case} (exit {result.returncode}; see {logpath})')

(out / 'summary.txt').write_text('\n'.join(summary) + '\n')
print('\n'.join(summary))
if not args.keep:
    shutil.rmtree(work, ignore_errors=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
if failed:
    raise SystemExit('Failed: ' + ', '.join(failed))
