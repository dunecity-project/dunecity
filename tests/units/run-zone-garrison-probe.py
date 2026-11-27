#!/usr/bin/env python3
"""Check Zone Garrisons on real units in the Dune City mod (tests/units/zone-garrison-probe.inc).

Soldiers and Troopers sent with the capture order occupy own Residential, Commercial and Industrial
zones (four at most) and take empty enemy ones (the zone, its tiles, density and power draw become
theirs); inside they fire out; three Troopers shoot a held enemy zone empty and take it; the owner's
Evacuate command brings them out beside the zone and an enemy's is refused; they come out of a zone
destroyed around them; an ally's zone is refused; transport recovery leaves them inside; a captured,
garrisoned zone survives a save and load; with the rule off nothing is garrisoned. garrison-zoom*.png
show the sandbags for a person to look at.

Requires the existing Ninja Release build and bundled game assets. Uses an isolated profile and dummy
SDL drivers. Logs, pictures and the test executable are kept in --output-dir.

--overlay-source recompiles the named production sources from the working tree into the probe, so a
change to them can be checked before the shared tree is rebuilt.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import NATIVE_TARGET, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402
from probe_profile import discard_profile_assets, require_free_space, scratch_dir  # noqa: E402
NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--overlay-source', action='append', default=[],
                    help='Production source (e.g. src/structures/ZoneStructureGarrison.cpp) to compile from the '
                         'working tree into the probe instead of linking the build tree\'s object')
parser.add_argument('--wall-timeout', type=int, default=400)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-zone-garrison-probe-'))
out.mkdir(parents=True, exist_ok=True)
require_free_space()
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
lines = native_build_commands(build)


def compile_command(production):
    """The production compile of one source, with Ninja's dependency bookkeeping removed."""
    for line in lines:
        if ' -c ' not in line:
            continue
        argv = shlex.split(line)
        if '-c' in argv and argv[argv.index('-c') + 1].endswith('/' + production):
            break
    else:
        raise RuntimeError('No compile command for ' + production)
    for option in ('-include', '-MT', '-MF'):
        if option in argv:
            i = argv.index(option)
            del argv[i:i + 2]
    for option in ('-MD', '-MMD'):
        if option in argv:
            argv.remove(option)
    return argv


main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runZoneGarrisonProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/zone-garrison-probe.inc'
main = main[:pos] + NATIVE_INCLUDES + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'zone-garrison-probe-main.cpp'
source.write_text(main)
units = [(source, 'src/main.cpp', ['-fno-access-control'])]
units += [(root / production, production, []) for production in args.overlay_source]
binary = native_probe_binary(build, out, 'zone-garrison-probe')
link = shlex.split(next(line for line in lines if ' -o ' + NATIVE_TARGET + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
with (out / 'build.log').open('w') as log:
    for actual, production, extra in units:
        cc = compile_command(production)
        obj = out / (Path(production).name + '.o')
        cc[cc.index('-o') + 1] = str(obj)
        cc[cc.index('-c') + 1] = str(actual)
        cc.extend(extra)
        subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        suffix = '/' + production[len('src/'):] + '.o'
        hits = [i for i, argument in enumerate(link) if argument.endswith(suffix)]
        if len(hits) != 1:
            raise RuntimeError('Link line carries ' + str(len(hits)) + ' objects for ' + production)
        link[hits[0]] = str(obj)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

with scratch_dir('zone-garrison-probe') as scratch:
    profile = scratch / 'profile'
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(
        '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
        'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\n'
        'Player Name = Garrison tester\n', encoding='utf-8')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1', ZONE_GARRISON_OUT=str(out))
    logfile = out / 'run.log'
    with logfile.open('w') as log:
        done = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                              stdout=log, stderr=subprocess.STDOUT, timeout=args.wall_timeout)
    text = logfile.read_text(errors='replace')
    for line in text.splitlines():
        if 'ZONE_GARRISON_' in line:
            print(line[line.index('ZONE_GARRISON_'):])
    if done.returncode != 0 or text.count('ZONE_GARRISON_PROBE_PASS:') != 1 or 'ZONE_GARRISON_PROBE_FAIL:' in text:
        raise RuntimeError('Zone garrison probe failed: ' + str(logfile))
    discard_profile_assets(profile)
(out / 'zone-garrison.dls').unlink(missing_ok=True)
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Zone garrisons passed. Logs and pictures: ' + str(out))
