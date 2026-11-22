#!/usr/bin/env python3
"""Measure what the Saboteur vehicle hunt changes, rule off against rule on in one binary.

Every trial is one Saboteur released by the real Palace weapon against a seeded enemy position
(four layouts of a turreted base, three Troopers and five vehicles; tests/units/
saboteur-effect-probe.inc), for a human owner and a QuantBot owner. The same (owner, layout,
seed) is played with the match rule "Saboteur Vehicle Hunt" off (the unit up to 1.0.876) and on.

Reported per arm: the share of Saboteurs that destroy a vehicle, the share that destroy
anything, the credit value destroyed per Saboteur, and how many die or idle with nothing
destroyed. The paired difference on - off has a 95% bootstrap interval (tests/ai/match_score.py).

The test fails unless, for each owner, the rule raises the share of Saboteurs that destroy a
vehicle with the whole interval above 0, and neither the share that destroy anything nor the
credits destroyed per Saboteur is lower with the rule on.

Requires the existing Ninja Release build and bundled game assets. Uses an isolated profile and
dummy SDL drivers. trials.csv, summary.txt, logs and the executable are kept in --output-dir.
"""
import argparse
import csv
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import sys
import tempfile
from collections import defaultdict

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
import match_score  # noqa: E402
from probe_profile import NATIVE_TARGET, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402
NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
FIELDS = ('mod', 'rule', 'owner', 'layout', 'seed', 'outcome', 'item', 'price', 'seconds', 'vehicles', 'structures')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--mods', default='vanilla,dunecity')
parser.add_argument('--seeds', type=int, default=12, help='Seeds per layout (1..N), each played in both arms')
parser.add_argument('--report-only', action='store_true', help='Report an existing --output-dir/trials.csv')
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-saboteur-effect-probe-'))
out.mkdir(parents=True, exist_ok=True)
mods = [mod for mod in args.mods.split(',') if mod]


def play():
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    lines = native_build_commands(build)
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main menu entry changed; update the test injection point.')
    main = main.replace(needle, 'int menuResult = runSaboteurEffectProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    pos = main.index('int main(')
    include = root / 'tests/units/saboteur-effect-probe.inc'
    main = main[:pos] + NATIVE_INCLUDES + '#include "' + str(include) + '"\n' + main[pos:]
    source = out / 'saboteur-effect-probe-main.cpp'
    source.write_text(main)
    cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
    # Test compilation must not overwrite Ninja's production dependency records.
    for option in ('-include', '-MT', '-MF'):
        if option in cc:
            i = cc.index(option)
            del cc[i:i + 2]
    for option in ('-MD', '-MMD'):
        if option in cc:
            cc.remove(option)
    obj = out / 'saboteur-effect-probe-main.o'
    cc[cc.index('-o') + 1] = str(obj)
    cc[cc.index('-c') + 1] = str(source)
    cc.append('-fno-access-control')    # The probe reads the Palace's private weapon timer.
    binary = native_probe_binary(build, out, 'saboteur-effect-probe')
    link = shlex.split(next(line for line in lines if ' -o ' + NATIVE_TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    rows = []
    for mod in mods:
        env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + mod)), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SABOTEUR_EFFECT_PROBE_MOD=mod,
                   SABOTEUR_EFFECT_PROBE_SEEDS=str(args.seeds))
        logfile = out / ('run-' + mod + '.log')
        with logfile.open('w') as log:
            done = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                                  stdout=log, stderr=subprocess.STDOUT, timeout=900)
        text = logfile.read_text(errors='replace')
        if done.returncode != 0 or 'SABOTEUR_EFFECT_PROBE_PASS:' not in text or 'SABOTEUR_EFFECT_PROBE_FAIL:' in text:
            raise RuntimeError('Saboteur effect probe did not finish: ' + str(logfile))
        for line in text.splitlines():
            if 'SABOTEUR_EFFECT_TRIAL:' in line:
                rows.append(dict(pair.split('=', 1) for pair in line.split('SABOTEUR_EFFECT_TRIAL:')[1].split()))
        if sum(row['mod'] == mod for row in rows) != 2 * 4 * args.seeds * 2:
            raise RuntimeError('Incomplete trial matrix for ' + mod + ': ' + str(logfile))
    with (out / 'trials.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)


def report():
    with (out / 'trials.csv').open(newline='') as handle:
        rows = list(csv.DictReader(handle))
    lines, failures = [], []

    def measures(row):
        return {'vehicle': 1.0 if row['outcome'] == 'vehicle' else 0.0,
                'any': 1.0 if row['outcome'] in ('vehicle', 'structure') else 0.0,
                'value': float(row['price'])}

    def block(title, chosen, check):
        pairs = defaultdict(dict)
        for row in chosen:
            pairs[(row['mod'], row['owner'], row['layout'], row['seed'])][row['rule']] = row
        if not pairs or any(set(pair) != {'off', 'on'} for pair in pairs.values()):
            raise RuntimeError('Unpaired trials in ' + title)
        lines.append(f'{title}: {len(pairs)} paired releases')
        lines.append('  arm   vehicle destroyed  anything destroyed  value/Saboteur  died  idle  en route')
        for rule in ('off', 'on'):
            arm = [pair[rule] for pair in pairs.values()]
            count = lambda outcome: sum(row['outcome'] == outcome for row in arm)   # noqa: E731
            lines.append(f'  {rule:<4}  {100 * statistics.mean(measures(r)["vehicle"] for r in arm):>16.1f}%'
                         f'  {100 * statistics.mean(measures(r)["any"] for r in arm):>17.1f}%'
                         f'  {statistics.mean(measures(r)["value"] for r in arm):>14.0f}'
                         f'  {count("died"):>4}  {count("idle"):>4}  {count("enroute"):>8}')
        for name, text, scale in (('vehicle', 'share destroying a vehicle, points', 100.0),
                                  ('any', 'share destroying anything, points', 100.0),
                                  ('value', 'credits destroyed per Saboteur', 1.0)):
            values = [scale * (measures(pair['on'])[name] - measures(pair['off'])[name]) for pair in pairs.values()]
            low, high = match_score.bootstrap(values, 20261122)
            lines.append(f'  on - off, {text}: {statistics.mean(values):+.1f} (95% interval {low:+.1f} to {high:+.1f})')
            if check and name == 'vehicle' and low <= 0:
                failures.append(f'{title}: the rule does not raise the share destroying a vehicle ({low:+.1f} to {high:+.1f})')
            if check and name != 'vehicle' and statistics.mean(values) < 0:
                failures.append(f'{title}: with the rule on, {text} is lower ({statistics.mean(values):+.1f})')
        lines.append('')

    block('All releases', rows, False)
    for owner in sorted({row['owner'] for row in rows}):
        block('Owner ' + owner, [row for row in rows if row['owner'] == owner], True)
    for layout in sorted({row['layout'] for row in rows}):
        block('Layout ' + layout, [row for row in rows if row['layout'] == layout], False)
    for mod in sorted({row['mod'] for row in rows}):
        block('Mod ' + mod, [row for row in rows if row['mod'] == mod], False)
    text = '\n'.join(lines)
    (out / 'summary.txt').write_text(text + '\n')
    print(text)
    if failures:
        raise RuntimeError('; '.join(failures))
    print('Saboteur vehicle hunt: for every owner more Saboteurs destroy a vehicle with the rule on, and neither '
          'the share destroying anything nor the credits destroyed fall. '
          'Results: ' + str(out))


if not args.report_only:
    play()
report()
