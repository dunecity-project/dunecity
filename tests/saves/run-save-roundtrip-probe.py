#!/usr/bin/env python3
"""Savegame compatibility against the real engine: earlier releases' saves load, run, re-save and reload.

Links a diagnostic copy of the game whose main menu is replaced by tests/saves/save-roundtrip-probe.inc
(production objects are unchanged). The build may be configured from another tree, e.g. a worktree of
an earlier release: the probe main.cpp comes from that tree, the probe include from this one.

  --make-saves LABEL   (build of an earlier release) play a vanilla and a DuneCity match for --minutes
                       game minutes and write LABEL-vanilla.dls and LABEL-dunecity.dls into the output
                       directory; the profile that wrote them is kept as profile-LABEL (its mods and
                       Workshop store are what that release left on a player's machine)
  SAVE ...             (this release) each save (.dls or .dls.gz; default: tests/saves/data, saves that
                       1.0.842, 1.0.847, 1.0.850 and the public 1.0.851 wrote with --make-saves) loads,
                       runs 600 cycles, is re-saved as the current savegame version and loads again;
                       worms are the Natives' at every stage. By default in a fresh profile (a save
                       brought from another machine or a cleared cache); --profile-template DIR starts
                       from a copy of DIR instead (the same machine after an update). A revision the
                       profile lacks is reported instead of opening the Workshop download dialog.

The fixtures pin the shipped Vanilla/Dune City revisions of their release. They load only while this
release ships the same mod files (the Game Version stamp aside); after a mod content change, regenerate
them from the earlier releases' builds or a player's store copy is needed, exactly as for players.
"""
import argparse
import os
import gzip
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('saves', nargs='*', type=Path, help='Savegames to round-trip')
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--make-saves', metavar='LABEL', help='Write LABEL-vanilla.dls and LABEL-dunecity.dls instead')
parser.add_argument('--minutes', type=int, default=3, help='Game minutes before --make-saves saves (default 3)')
parser.add_argument('--profile-template', type=Path, help='Load in a copy of this profile instead of a fresh one')
parser.add_argument('--allow-end', action='store_true', help='A game may end within the 600 cycles')
parser.add_argument('--timeout', type=int, default=900, help='Seconds per probe process')
parser.add_argument('--keep', action='store_true', help='Keep the profiles and the probe executable')
args = parser.parse_args()
if args.make_saves and args.saves:
    parser.error('Give either --make-saves LABEL or savegames to load.')
if not args.make_saves and not args.saves:
    args.saves = sorted((root / 'tests/saves/data').glob('*.dls.gz'))
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

# The tree the build was configured from (an earlier release's main.cpp, not necessarily this one's).
source_root = Path(next(line.split('=', 1)[1] for line in (build / 'CMakeCache.txt').read_text().splitlines()
                        if line.startswith('CMAKE_HOME_DIRECTORY:')))
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (source_root / 'src/main.cpp').read_text()
for needle, replacement in (('int menuResult = MainMenu().showMenu();', 'int menuResult = runSaveRoundtripProbe();'),
                            ('clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);', '(void)displayIndex;')):
    if main.count(needle) != 1:
        raise RuntimeError('Injection point changed: ' + needle)
    main = main.replace(needle, replacement)
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + str(root / 'tests/saves/save-roundtrip-probe.inc') + '"\n' + main[position:]
work = Path(tempfile.mkdtemp(prefix='save-probe-build-', dir=out))
source, obj = work / 'save-probe-main.cpp', work / 'save-probe-main.o'
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
if args.make_saves:
    compile_command.append('-DSAVE_PROBE_MAKER')
app = work / 'save-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True)
(app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/save-probe'
link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (work / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)


def run(label, profile, case, extra):
    if not (profile / 'Dune City.ini').exists():
        profile.mkdir(parents=True, exist_ok=True)
        (profile / 'Dune City.ini').write_text(
            '[Video]\nPhysical Width = 1024\nPhysical Height = 768\nWidth = 1024\nHeight = 768\n'
            'Interface Height = 768\nFullscreen = false\n[General]\nPlay Intro = false\nPlayer Name = Save tester\n')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SAVE_PROBE_OUT=str(out), SAVE_PROBE_CASE=case, SAVE_PROBE_LABEL=label,
               SAVE_PROBE_SOURCE_DIR=str(source_root), SAVE_PROBE_MINUTES=str(args.minutes),
               SAVE_PROBE_ALLOW_END='1' if args.allow_end else '0', **extra)
    logpath = out / f'run-{label}.log'
    with logpath.open('w') as log:
        try:
            code = subprocess.run([str(binary), '--window', '--showlog'], cwd=work, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    text = logpath.read_text(errors='replace')
    lines = [line[line.index('SAVE_PROBE'):] for line in text.splitlines() if 'SAVE_PROBE' in line]
    ok = code == 0 and text.count('SAVE_PROBE_PASS:') == 1
    return [f'[{label}] ' + ('PASS' if ok else f'FAIL (exit {code}; see {logpath})')] + lines, ok


summary, failed = [], []
if args.make_saves:
    lines, ok = run(args.make_saves, out / ('profile-' + args.make_saves), 'make-save', {})
    summary += lines
    failed += [] if ok else [args.make_saves]
else:
    for save in args.saves:
        label = save.name.removesuffix('.gz').removesuffix('.dls')
        if save.suffix == '.gz':
            unpacked = work / (label + '.dls')
            unpacked.write_bytes(gzip.decompress(save.read_bytes()))
            save = unpacked
        profile = work / ('profile-' + label)
        if args.profile_template:
            shutil.copytree(args.profile_template, profile, symlinks=True)
        lines, ok = run(label, profile, 'load', {'SAVE_PROBE_SAVE': str(save.resolve())})
        summary += lines
        failed += [] if ok else [label]

(out / 'summary.txt').open('a').write('\n'.join(summary) + '\n')
print('\n'.join(summary))
if not args.keep:
    shutil.rmtree(work, ignore_errors=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
if failed:
    raise SystemExit('Failed: ' + ', '.join(failed))
