#!/usr/bin/env python3
"""Check on real units the Fremen Palace worm (match rule Fremen Palace Worm).

The Fremen Palace calls a worm its house controls: it surfaces on the sand nearest the
Palace, takes move orders and never leaves the sand; in Hunt it eats the enemy on its sand
(not on rock, not its own side) and the kills are credited to the Fremen house; the Spice
Bloom order bursts it into a spice field; it leaves after its life or six meals; a QuantBot
owner hunts with it and otherwise bursts it for spice beside its refinery; it survives a
full save and load; with the rule off the Palace calls Fremen warriors. The player's own path is
driven through the real Palace and unit panels and Game::doInput mouse clicks, and QuantBot's
PalaceWorm levels 0/1/2 are checked by their effect and by the PALACE_WORM log lines each case wrote
(tests/units/palace-worm-probe.inc).

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
                    help='Production source (e.g. src/units/SandWorm.cpp) to compile from the working tree '
                         'into the probe instead of linking the build tree\'s object')
parser.add_argument('--expect-failure', action='store_true',
                    help='Require the probe to fail: for showing the pre-fix behaviour of an older build')
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-palace-worm-probe-'))
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
main = main.replace(needle, 'int menuResult = runPalaceWormProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/palace-worm-probe.inc'
main = main[:pos] + NATIVE_INCLUDES + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'palace-worm-probe-main.cpp'
source.write_text(main)
# The probe sets the Palace's private weapon timer; only this diagnostic binary may.
units = [(source, 'src/main.cpp', ['-fno-access-control'])]
units += [(root / production, production, []) for production in args.overlay_source]
binary = native_probe_binary(build, out, 'palace-worm-probe')
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



def check_case_events(text):
    """The PALACE_WORM log lines each case wrote (the lines the match tables read): QuantBot's hunt
    and bloom orders by PalaceWorm level, none from a human's worm, and no other controller's order."""
    cases, label = {}, None
    for line in text.splitlines():
        if 'PALACE_WORM_CASE_BEGIN: ' in line:
            label = line.split('PALACE_WORM_CASE_BEGIN: ', 1)[1].strip()
            cases[label] = {}
        elif label and 'PALACE_WORM: event=' in line:
            event = line.split('PALACE_WORM: event=', 1)[1].split()[0]
            cases[label][event] = cases[label].get(event, 0) + 1
    want = {  # label: {event: (min, max)}
        'qBotHard/hunt': {'hunt': (1, None), 'ate': (2, 2)},
        'qBotHard/bloom-near-refinery': {'hunt': (0, 0), 'bloom_order': (1, None), 'bloom': (1, 1)},
        'qBotHard/ai-level-1': {'hunt': (1, None), 'ate': (2, 2), 'bloom_order': (1, None), 'bloom': (1, 1)},
        'qBotHard/ai-level-0': {'hunt': (0, 0), 'ate': (0, 0), 'bloom_order': (1, None), 'bloom': (1, 1), 'spice': (1, 1)},
        'qBotHard/ai-level-2': {'hunt': (1, None), 'ate': (2, 2), 'bloom_order': (0, 0), 'bloom': (0, 0), 'expired': (1, 1)},
        'HumanPlayer/player-hunt-button': {'summoned': (1, 1), 'ate': (1, 1)},
        'HumanPlayer/player-attack-click': {'summoned': (1, 1), 'ate': (2, 2)},
        'HumanPlayer/player-spice-bloom': {'summoned': (1, 1), 'bloom_order': (1, 1), 'bloom': (1, 1)},
        'HumanPlayer/bundled-deserted': {'summoned': (1, 1), 'ate': (1, None), 'bloom_order': (1, 1), 'bloom': (1, 1), 'spice': (1, 1)},
        'HumanPlayer/bloom-under-fire': {'bloom_order': (1, 1), 'bloom_cancelled': (0, 0), 'bloom': (1, 1)},
        'qBotHard/bloom-under-fire': {'bloom_order': (1, 1), 'bloom_cancelled': (0, 0), 'bloom': (1, 1)},
    }
    problems = []
    for label, need in want.items():
        if label not in cases:
            problems.append(label + ': the case did not run')
            continue
        for event, (low, high) in need.items():
            count = cases[label].get(event, 0)
            if count < low or (high is not None and count > high):
                problems.append(f'{label}: {count} {event} lines, expected {low}..{high if high is not None else "any"}')
    for label, events in cases.items():
        if events.get('redirected'):
            problems.append(f'{label}: {events["redirected"]} orders to the worm from another controller')
        if label.startswith('HumanPlayer/') and events.get('hunt'):
            problems.append(f'{label}: a human\'s worm logged an AI hunt order')
    print('Palace worm log lines per case: ' + '; '.join(
        f'{label} ' + ','.join(f'{e}={n}' for e, n in sorted(events.items())) for label, events in cases.items()))
    return problems


mods = [mod for mod in args.mods.split(',') if mod]
for mod in mods:
    env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + mod)),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', PALACE_WORM_PROBE_MOD=mod,
               PALACE_WORM_PROBE_SAVE=str(out / ('palace-worm-' + mod + '.dls')),
               PALACE_WORM_PROBE_BUNDLED_MAP=str(root / 'data/maps/multiplayer/4P - 128x128 - Deserted.ini'))
    logfile = out / ('run-' + mod + '.log')
    with logfile.open('w') as log:
        done = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                              stdout=log, stderr=subprocess.STDOUT, timeout=600)
    text = logfile.read_text(errors='replace')
    for line in text.splitlines():
        if 'PALACE_WORM_CASE:' in line or 'PALACE_WORM_PROBE_' in line:
            print(mod + ': ' + line[line.index('PALACE_WORM_'):])
    passed = done.returncode == 0 and 'PALACE_WORM_PROBE_PASS:' in text \
        and 'PALACE_WORM_PROBE_FAIL:' not in text
    if passed:
        problems = check_case_events(text)
        for problem in problems:
            print(mod + ': PALACE_WORM_LOG_FAIL: ' + problem)
        passed = not problems
    if passed == args.expect_failure:
        raise RuntimeError(('Palace worm probe passed although a failure was expected: ' if passed
                            else 'Palace worm probe failed: ') + str(logfile))
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Palace worm ' + ('failed as expected' if args.expect_failure else 'passed')
      + ' in ' + ', '.join(mods) + '. Logs: ' + str(out))
