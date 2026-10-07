#!/usr/bin/env python3
"""Exercise Tile::setTrack bounds against the real engine.

Builds a headless fixture from the production objects, but recompiles src/Tile.cpp (and the
one translation unit that calls setTrack) into the probe, so the checks can be pointed at
either the current working tree or the pre-fix source from git HEAD~ without touching the
shared build tree. On a populated real Tile - damage, dead units, a deployed occupant - an
out-of-range direction must change nothing, must not disturb the tiles that follow it in
Map's tiles vector or separately allocated tiles, and must leave save/load and destruction
intact, while the eight legal directions still record and serialize their game cycles.

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers; it never builds the shared tree. Logs and the test
executables are retained in --output-dir.
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
parser.add_argument('--mod', default='vanilla')
parser.add_argument('--map', default='data/maps/singleplayer/4P - 128x128 - 4 corners.ini',
                    help='Map the fixture initialises; defaults to the replay scenario')
parser.add_argument('--seed', default='315473198', help='Seed of the replay the crash came from')
parser.add_argument('--cases', default='valid,oob8,oob9,oob255,negative')
parser.add_argument('--phases', default='prepatch,current',
                    help='"prepatch" builds Tile from the pre-fix revision and requires the '
                         'out-of-range cases to fail; "current" requires every case to pass')
parser.add_argument('--prepatch-revision', default='HEAD',
                    help='Revision the prepatch phase takes src/Tile.cpp and include/Tile.h from')
parser.add_argument('--no-bounds-trap', dest='bounds_trap', action='store_false',
                    help='Build the probe-local Tile.cpp without the trapping bounds check, so '
                         'the pre-fix damage/neighbour corruption is reported by the state '
                         'comparisons instead of trapping inside setTrack')
parser.add_argument('--timeout', type=int, default=600)
args = parser.parse_args()
variant = "" if args.bounds_trap else "-no-bounds"
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-track-probe-'))
out.mkdir(parents=True, exist_ok=True)
map_path = (root / args.map).resolve()
if not map_path.is_file():
    raise RuntimeError('Fixture map not found: ' + str(map_path))

# Require valid shared objects before linking the private diagnostic objects.
deps = subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)],
                      cwd=root, capture_output=True, text=True)
print('check-build-deps.py exit=' + str(deps.returncode))
if deps.returncode != 0:
    raise RuntimeError(deps.stdout + deps.stderr)

target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target],
                                text=True).splitlines()


def compile_command(relative_source):
    """The production compile of one source, with Ninja's dependency bookkeeping removed."""
    for line in lines:
        if ' -c ' not in line:
            continue
        argv = shlex.split(line)
        if '-c' not in argv:
            continue
        if argv[argv.index('-c') + 1].endswith(relative_source):
            break
    else:
        raise RuntimeError('No compile command for ' + relative_source)
    for option in ('-include', '-MT', '-MF'):
        if option in argv:
            i = argv.index(option)
            del argv[i:i + 2]
    for option in ('-MD', '-MMD'):
        if option in argv:
            argv.remove(option)
    return argv


link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]

main_source = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main_source.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main_source = main_source.replace(needle, 'int menuResult = runTileTrackBoundsProbe();')
main_source = main_source.replace('if(shouldPlayIntro && (bFirstInit==true))',
                                  'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main_source.index('int main(')
include = root / 'tests/units/tile-track-bounds-probe.inc'
main_source = main_source[:pos] + '#include "' + str(include) + '"\n' + main_source[pos:]


def build_probe(phase):
    """One probe binary: the fixture plus a probe-local Tile.cpp for this phase."""
    work = out / (phase + variant)
    work.mkdir(parents=True, exist_ok=True)
    overlay = []
    if phase == 'prepatch':
        # The fix changes setTrack's signature, so the pre-fix source needs the pre-fix header.
        header_dir = work / 'overlay'
        header_dir.mkdir(parents=True, exist_ok=True)
        for relative, destination in (('include/Tile.h', header_dir / 'Tile.h'),
                                      ('src/Tile.cpp', work / 'Tile.cpp')):
            destination.write_bytes(subprocess.check_output(
                ['git', 'show', args.prepatch_revision + ':' + relative], cwd=root))
        tile_source = work / 'Tile.cpp'
        overlay = ['-I', str(header_dir)]
    else:
        tile_source = root / 'src/Tile.cpp'
    source = work / 'tile-track-probe-main.cpp'
    source.write_text(main_source)
    objects = {}
    units = ((source, 'src/main.cpp', ['-DPROBE_MAP_PATH="' + str(map_path) + '"',
                                       # Read real private tile state only in this diagnostic binary.
                                       '-fno-access-control']),
             # The probe-local Tile.cpp also carries a trapping bounds check, the way the
             # browser run that found this did. On this host tracksCreationTime[8] falls in
             # the padding before the damage vector, so index 8 only shows up as a bounds
             # violation; indices 9 and up overwrite the vector's own pointers. Trapping
             # rather than diagnosing keeps the production link line free of a UBSan runtime.
             (tile_source, 'src/Tile.cpp', ['-fno-access-control'] + (
                 ['-fsanitize=bounds', '-fsanitize-trap=bounds'] if args.bounds_trap else [])),
             (root / 'src/units/GroundUnit.cpp', 'src/units/GroundUnit.cpp', []))
    with (work / 'build.log').open('w') as log:
        for actual, production, extra in units:
            argv = compile_command(production)
            obj = work / (Path(production).name + '.o')
            argv[argv.index('-o') + 1] = str(obj)
            argv[argv.index('-c') + 1] = str(actual)
            argv[1:1] = overlay
            argv.extend(extra)
            objects[production] = obj
            log.write('# compile ' + production + '\n' + shlex.join(argv) + '\n')
            log.flush()
            subprocess.run(argv, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        app = work / 'tile-track-probe.app/Contents'
        (app / 'MacOS').mkdir(parents=True, exist_ok=True)
        resources = app / 'Resources'
        if not resources.exists():
            resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
        binary = app / 'MacOS/tile-track-probe'
        argv = list(link)
        argv[argv.index('-o') + 1] = str(binary)
        for production, obj in objects.items():
            # CMake names the objects after the path below src/: src/units/GroundUnit.cpp
            # becomes src/CMakeFiles/dunecity.dir/units/GroundUnit.cpp.o.
            suffix = '/' + production[len('src/'):] + '.o'
            hits = [i for i, arg in enumerate(argv) if arg.endswith(suffix)]
            if len(hits) != 1:
                raise RuntimeError('Link line carries ' + str(len(hits)) + ' objects for '
                                   + production + '; expected exactly one')
            argv[hits[0]] = str(obj)
        log.write('# link\n' + shlex.join(argv) + '\n')
        log.flush()
        subprocess.run(argv, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def run(binary, phase, case):
    tag = phase + variant + '-' + case
    env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + tag)),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               TILE_TRACK_PROBE_MOD=args.mod, TILE_TRACK_PROBE_CASE=case,
               TILE_TRACK_PROBE_LABEL=phase, TILE_TRACK_PROBE_SEED=args.seed)
    logfile = out / ('run-' + tag + '.log')
    with logfile.open('w') as log:
        done = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                              stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
    text = logfile.read_text()
    passed = 'TILE_TRACK_PROBE_PASS:' in text and 'TILE_TRACK_PROBE_FAIL:' not in text \
        and done.returncode == 0
    detail = next((line for line in text.splitlines() if 'TILE_TRACK_PROBE_FAIL:' in line),
                  'exit=' + str(done.returncode))
    for line in text.splitlines():
        if 'TILE_TRACK_PROBE_LAYOUT' in line or 'TILE_TRACK_PROBE_TIMESTAMPS' in line:
            print('  ' + line.split('TILE_TRACK_PROBE_')[1])
    return passed, detail, logfile


cases = [name for name in args.cases.split(',') if name]
failures = []
for phase in [name for name in args.phases.split(',') if name]:
    binary = build_probe(phase)
    for case in cases:
        passed, detail, logfile = run(binary, phase, case)
        print(phase + ' ' + case + ': ' + ('passed' if passed else 'failed') + ' (' + detail + ')')
        if phase == 'current' and not passed:
            failures.append('current ' + case + ' must pass: ' + detail + ' ' + str(logfile))
        if phase == 'prepatch' and passed != (case == 'valid'):
            failures.append('prepatch ' + case + ' expected '
                            + ('pass' if case == 'valid' else 'failure')
                            + ' but ' + ('passed' if passed else 'failed') + ': ' + str(logfile))
if failures:
    raise RuntimeError('Tile track bounds probe: ' + '; '.join(failures))
print('Tile track bounds verified for cases: ' + args.cases + '. Logs: ' + str(out))
