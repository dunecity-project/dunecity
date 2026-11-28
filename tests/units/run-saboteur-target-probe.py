#!/usr/bin/env python3
"""Check on real units that a Saboteur goes for the nearest enemy building or vehicle.

A Palace releases a Saboteur for a human house and for a QuantBot house. It must walk to
the nearest thing it may blow up, building or vehicle alike (harvesters and MCVs included,
aircraft and infantry not), and blow it up; a vehicle that turns up beside it on the way is
taken instead of the one further off; with nothing to sabotage it waits in Guard. An explicit
order (kept even past a nearer building that sees it) and Area Guard are covered too. Its
owner is shown the target for at least a second before the blast, and a Saboteur that is shot
on the way falls like a foot soldier instead of blowing up
(tests/units/saboteur-target-probe.inc).

Requires the existing Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable are retained in --output-dir.

--overlay-source recompiles the named production sources from the working tree into the
probe, so a change to them can be checked before the shared tree is rebuilt.
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
NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--mods', default='vanilla,dunecity')
parser.add_argument('--overlay-source', action='append', default=[],
                    help='Production source (e.g. src/units/Saboteur.cpp) to compile from the working tree '
                         'into the probe instead of linking the build tree\'s object')
parser.add_argument('--expect-failure', action='store_true',
                    help='Require the probe to fail: for showing the pre-fix behaviour of an older build')
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-saboteur-target-probe-'))
out.mkdir(parents=True, exist_ok=True)
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
main = main.replace(needle, 'int menuResult = runSaboteurTargetProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/saboteur-target-probe.inc'
main = main[:pos] + NATIVE_INCLUDES + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'saboteur-target-probe-main.cpp'
source.write_text(main)
# The probe reads the Palace's private weapon timer; only this diagnostic binary may.
units = [(source, 'src/main.cpp', ['-fno-access-control'])]
units += [(root / production, production, []) for production in args.overlay_source]
binary = native_probe_binary(build, out, 'saboteur-target-probe')
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
        # CMake names objects after the path below src/: src/units/Saboteur.cpp -> .../units/Saboteur.cpp.o
        suffix = '/' + production[len('src/'):] + '.o'
        hits = [i for i, argument in enumerate(link) if argument.endswith(suffix)]
        if len(hits) != 1:
            raise RuntimeError('Link line carries ' + str(len(hits)) + ' objects for ' + production)
        link[hits[0]] = str(obj)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

mods = [mod for mod in args.mods.split(',') if mod]
for mod in mods:
    env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + mod)),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SABOTEUR_TARGET_PROBE_MOD=mod)
    logfile = out / ('run-' + mod + '.log')
    with logfile.open('w') as log:
        done = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                              stdout=log, stderr=subprocess.STDOUT, timeout=120)
    text = logfile.read_text(errors='replace')
    for line in text.splitlines():
        if 'SABOTEUR_TARGET_CASE:' in line or 'SABOTEUR_TARGET_SHOWN:' in line or 'SABOTEUR_TARGET_PROBE_' in line:
            print(mod + ': ' + line[line.index('SABOTEUR_TARGET_'):])
    passed = done.returncode == 0 and 'SABOTEUR_TARGET_PROBE_PASS:' in text \
        and 'SABOTEUR_TARGET_PROBE_FAIL:' not in text
    if passed == args.expect_failure:
        raise RuntimeError(('Saboteur target probe passed although a failure was expected: ' if passed
                            else 'Saboteur target probe failed: ') + str(logfile))
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Saboteur target selection ' + ('failed as expected' if args.expect_failure else 'passed')
      + ' in ' + ', '.join(mods) + '. Logs: ' + str(out))
