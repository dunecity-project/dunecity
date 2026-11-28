#!/usr/bin/env python3
"""Check on real units that a Saboteur passes over zones nobody garrisons (Saboteurs Skip Empty Zones).

Stefan, 2026-11-28: "also make sabateurs not target r/i/c (not valuable enough) unless it is
garissoned". In a Dune City game a Palace-released Saboteur, human or QuantBot owned, goes past empty
Residential, Commercial and Industrial zones to the building beyond; with the rule off it blows up the
nearest zone. A garrisoned zone is still a target and is blown up (the front Soldier dies, the other
is thrown out with half its hit points); a zone whose garrison leaves while the Saboteur walks to it is
given up; with only empty zones about it takes no target; the player's own right-click on an empty
zone is obeyed (tests/units/saboteur-zone-probe.inc).

The match log must also show the rule acting: SABOTEUR_ZONE_SKIP lines (the zone the rule up to
protocol 149 would have taken), SABOTEUR_ZONE_DROP lines (the evacuated zone given up) and no
SABOTEUR_ZONE_RULE violation line.

Requires the existing Ninja Release build and bundled game assets. Uses an isolated profile and
dummy SDL drivers. Logs and the test executable are retained in --output-dir.
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
parser.add_argument('--overlay-source', action='append', default=[],
                    help='Production source (e.g. src/units/Saboteur.cpp) to compile from the working tree '
                         'into the probe instead of linking the build tree\'s object')
parser.add_argument('--expect-failure', action='store_true',
                    help='Require the probe to fail: for showing the behaviour of an older build')
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-saboteur-zone-probe-'))
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
main = main.replace(needle, 'int menuResult = runSaboteurZoneProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/saboteur-zone-probe.inc'
main = main[:pos] + NATIVE_INCLUDES + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'saboteur-zone-probe-main.cpp'
source.write_text(main)
# The probe reads the Palace's private weapon timer; only this diagnostic binary may.
units = [(source, 'src/main.cpp', ['-fno-access-control'])]
units += [(root / production, production, []) for production in args.overlay_source]
binary = native_probe_binary(build, out, 'saboteur-zone-probe')
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

env = dict(os.environ, DUNECITY_USERDIR=str(out / 'profile'), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
logfile = out / 'run.log'
with logfile.open('w') as log:
    done = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                          stdout=log, stderr=subprocess.STDOUT, timeout=240)
text = logfile.read_text(errors='replace')
for line in text.splitlines():
    for marker in ('SABOTEUR_ZONE_CASE:', 'SABOTEUR_ZONE_PROBE_', 'SABOTEUR_ZONE_SKIP:', 'SABOTEUR_ZONE_DROP:',
                   'SABOTEUR_ZONE_RULE:'):
        if marker in line:
            print(line[line.index(marker):])
problems = []
if done.returncode != 0 or 'SABOTEUR_ZONE_PROBE_PASS:' not in text or 'SABOTEUR_ZONE_PROBE_FAIL:' in text:
    problems.append('a case failed')
# The rule's own log: it passed zones over (the two owners' skipped and garrison-leaves cases), gave
# up the evacuated zone (once per owner) and never chose an empty zone itself.
if text.count('SABOTEUR_ZONE_SKIP:') < 2:
    problems.append('fewer than 2 SABOTEUR_ZONE_SKIP lines')
if text.count('SABOTEUR_ZONE_DROP:') < 2:
    problems.append('fewer than 2 SABOTEUR_ZONE_DROP lines')
if 'SABOTEUR_ZONE_RULE: event=violation' in text:
    problems.append('a SABOTEUR_ZONE_RULE violation line')
failed = bool(problems)
if failed != args.expect_failure:
    raise RuntimeError(('Saboteur zone probe passed although a failure was expected: ' if not failed
                        else 'Saboteur zone probe failed (' + '; '.join(problems) + '): ') + str(logfile))
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Saboteur zone targeting ' + ('failed as expected' if args.expect_failure else 'passed') + '. Logs: ' + str(out))
