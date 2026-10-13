#!/usr/bin/env python3
"""Random Skirmish against the real engine, in isolated profiles.

Links a diagnostic copy of the game whose main menu is replaced by tests/skirmish/skirmish-probe.inc
(production objects are unchanged) and runs one process per case:

  menu         the Random Skirmish screen: house cap, persistence, Save map, screenshots
  legacy-map   a bundled city map without zone levels still starts every lot empty
  vanilla-1v1  small vanilla 1v1
  allied-2v3   2v3 against allied enemies, Large bases, 10k armies, plus a one-minute smoke run
  city-192     DuneCity 192x192, 15k cities at tech 8, measured population, smoke run, save/load
  stefan-128   Stefan's played configuration (128, 2 allies + 2 enemies, tech 9, Medium bases,
               5k armies, 40k cities, seed 488631365): measured population, smoke run, save/load
  partner-support  the player's own Medium base, 5k army and 5k city with a qBotSupportMedium
               partner in the house: two players, base on positive power, in-game screenshots,
               smoke run
  partner-quantbot a full qBotMedium partner in the player's house loads
  render       whole-map pictures (4 px per tile) and terrain measurements of map-editor random
               maps and of generated maps; --render-save adds the map a saved match started from

Every match case asserts houses/teams, players per house, structure and unit counts, army value
and power against the generator's own plan, and draws the loaded (and played) map. A smoke run
fails on any error, invalid entry or occupied position the engine logs while it plays.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--cases', default='menu,legacy-map,vanilla-1v1,allied-2v3,city-192,stefan-128,'
                                       'partner-support,partner-quantbot,render')
parser.add_argument('--keep', action='store_true', help='Keep profiles and saves (screenshots are always kept)')
parser.add_argument('--render-save', type=Path, help='render: also draw the map this saved match (.dls) started from')
parser.add_argument('--render-tag', default='current', help='render: file-name prefix of the generated maps')
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
main = main.replace(needle, 'int menuResult = runSkirmishProbe();')
clamp = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
if main.count(clamp) != 1:
    raise RuntimeError('Window-size fixture injection point changed.')
main = main.replace(clamp, '(void)displayIndex;')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + str(root / 'tests/skirmish/skirmish-probe.inc') + '"\n' + main[position:]
work = Path(tempfile.mkdtemp(prefix='skirmish-probe-build-', dir=out))
source, obj = work / 'skirmish-main.cpp', work / 'skirmish-main.o'
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
app = work / 'skirmish-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True)
(app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/skirmish-probe'
link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (work / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

# Engine lines that mean a generated map or match is broken.
problem = re.compile(r'invalid|occupied|error|exception|failed|cannot|unknown', re.IGNORECASE)
# Pre-existing startup lines unrelated to the match (mod/asset discovery in an empty profile).
startup_noise = re.compile(r'Error loading|Warning: could not|libpng', re.IGNORECASE)

summary = []
for case in args.cases.split(','):
    sizes = ((640, 480), (1280, 720)) if case == 'menu' else ((1024, 768),)
    for width, height in sizes:
        label = case if len(sizes) == 1 else f'{case}-{width}'
        profile = work / ('profile-' + label)
        profile.mkdir()
        (profile / 'Dune City.ini').write_text(
            f'[Video]\nPhysical Width = {width}\nPhysical Height = {height}\nWidth = {width}\nHeight = {height}\n'
            f'Interface Height = {height}\nFullscreen = false\n[General]\nPlay Intro = false\nPlayer Name = Skirmish tester\n')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   SKIRMISH_PROBE_OUT=str(out), SKIRMISH_PROBE_CASE=case, SKIRMISH_SOURCE_DIR=str(root))
        if case in ('allied-2v3', 'city-192', 'stefan-128', 'partner-support'):
            env['SKIRMISH_PROBE_SMOKE'] = '1'
        if case == 'render':
            env['SKIRMISH_RENDER_TAG'] = args.render_tag
            if args.render_save:
                env['SKIRMISH_RENDER_SAVE'] = str(args.render_save.resolve())
        logpath = out / f'run-{label}.log'
        with logpath.open('w') as log:
            subprocess.run([str(binary), '--window', '--showlog'], cwd=work, env=env, stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=600)
        text = logpath.read_text(errors='replace')
        if text.count('SKIRMISH_PROBE_PASS:') != 1:
            raise RuntimeError(f'{label}: no pass line; see {logpath}')
        if 'SKIRMISH_SMOKE_BEGIN' in text:
            played = text.split('SKIRMISH_SMOKE_BEGIN', 1)[1].split('SKIRMISH_SMOKE_END', 1)[0]
            bad = [line for line in played.splitlines()[1:] if problem.search(line) and not startup_noise.search(line)]
            if bad:
                raise RuntimeError(f'{label}: {len(bad)} problem line(s) in the played minute, first: {bad[0]}')
        loaded = text.split('SKIRMISH_PROBE:', 1)[0] if 'SKIRMISH_PROBE:' in text else text
        map_warnings = [line for line in loaded.splitlines()
                        if re.search(r'- Random \d+v\d+.*?\d+:|Invalid or occupied|Invalid (unit|building|house|position)|refused zone', line)]
        if map_warnings:
            raise RuntimeError(f'{label}: map load warnings, first: {map_warnings[0]}')
        summary += [line[line.index('SKIRMISH_PROBE'):] for line in text.splitlines() if 'SKIRMISH_PROBE' in line]
        summary += [line[line.index('SKIRMISH_SMOKE'):] for line in text.splitlines() if 'SKIRMISH_SMOKE' in line]
        summary += [line[line.index('SKIRMISH_RENDER'):] for line in text.splitlines() if 'SKIRMISH_RENDER' in line]

(out / 'summary.txt').write_text('\n'.join(summary) + '\n')
print('\n'.join(summary))
if not args.keep:
    for path in work.iterdir():
        if path.name.startswith('profile-') or path.suffix in ('.o', '.cpp'):
            shutil.rmtree(path) if path.is_dir() else path.unlink()
    shutil.rmtree(work / 'skirmish-probe.app', ignore_errors=True)
    for save in out.glob('*.dls'):
        save.unlink()
