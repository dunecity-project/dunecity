#!/usr/bin/env python3
"""Measure whether Troopers fight Ornithopters better with the reach rule.

Controlled engagements in the existing Ninja Release build (no other CMake tree): Trooper
groups of 1, 3 (one Troopers order) and 6 (two orders) on Guard are raided by 1, 2 or 3
Ornithopters that fly in from one side, on a diagonal or from different sides, starting 8
or 14 tiles away. Every encounter is played twice in the same binary, with [Game Options]
"Trooper Ornithopter Reach" off (the rule up to 1.0.876) and on; Trooper Anti-Air is on in
both, so the reach is the only difference. The seed moves the start tiles, the same way in
both arms.

Reported per arm: Ornithopters shot down, Troopers lost, Ornithopter hit points removed,
and the time of the first kill; and the paired difference (on minus off, paired by
encounter) with its 95% bootstrap interval (tests/ai/match_score.py). Three kinds of
ground: explored without fog (the Dune City mod's own options: the team sees all ground, so
the gain is the immediate, unfaced answer), unexplored (Troopers in the field, where the old
search also needed seen ground), and explored under Fog of War. On explored and on
unexplored ground the test requires more Ornithopters shot down, the whole interval above 0,
and Trooper losses not higher (interval not wholly above 0); under fog Ornithopters shot down
must not be lower.

Outputs: encounters.csv, pairs.csv, summary.md, run.log.
"""
import argparse
import csv
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
from probe_profile import NATIVE_TARGET, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402
from probe_profile import default_build_dir, discard_profile_assets, require_free_space, scratch_dir  # noqa: E402
from match_score import bootstrap  # noqa: E402

NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
CYCLE_SECONDS = 0.016
MEASURES = ('ornithopters_down', 'troopers_lost', 'ornithopter_damage', 'shots')
KEY = ('terrain', 'troopers', 'ornithopters', 'approach', 'distance', 'seed')


def seed_list(text):
    seeds = []
    for part in text.split(','):
        if '-' in part:
            first, last = part.split('-')
            seeds.extend(range(int(first), int(last) + 1))
        else:
            seeds.append(int(part))
    if not seeds or len(set(seeds)) != len(seeds) or min(seeds) < 0:
        raise argparse.ArgumentTypeError('seeds must be distinct nonnegative integers, e.g. 1-6 or 3,5,8')
    return seeds


def pair_up(rows):
    arms = {}
    for row in rows:
        arms.setdefault(tuple(row[field] for field in KEY), {})[row['reach']] = row
    pairs = []
    for key, arm in sorted(arms.items()):
        if set(arm) != {'0', '1'}:
            raise RuntimeError('Encounter without both arms: ' + repr(key))
        pairs.append((key, arm['0'], arm['1']))
    return pairs


def interval(values, seed=20261122):
    return bootstrap([float(value) for value in values], seed)


def describe(pairs, label):
    """One block of the report: arm means, paired difference and interval per measure."""
    lines = ['### ' + label + ' (' + str(len(pairs)) + ' paired encounters)', '',
             '| Measure | Reach off | Reach on | Paired difference (on - off) | 95% interval |',
             '|---|---:|---:|---:|---|']
    result = {}
    for measure in MEASURES:
        off = [int(a[measure]) for _, a, _ in pairs]
        on = [int(b[measure]) for _, _, b in pairs]
        differences = [y - x for x, y in zip(off, on)]
        low, high = interval(differences)
        result[measure] = (statistics.mean(off), statistics.mean(on), statistics.mean(differences), low, high)
        lines.append('| %s | %.3f | %.3f | %+.3f | %+.3f to %+.3f |' % (
            measure.replace('_', ' '), *result[measure]))
    kills_off = [int(a['first_kill_tick']) for _, a, _ in pairs if int(a['first_kill_tick']) >= 0]
    kills_on = [int(b['first_kill_tick']) for _, _, b in pairs if int(b['first_kill_tick']) >= 0]
    both = [(int(a['first_kill_tick']), int(b['first_kill_tick'])) for _, a, b in pairs
            if int(a['first_kill_tick']) >= 0 and int(b['first_kill_tick']) >= 0]
    lines += ['', 'Encounters with at least one Ornithopter shot down: off %d/%d, on %d/%d.' % (
        len(kills_off), len(pairs), len(kills_on), len(pairs))]
    if kills_off:
        lines.append('Mean time to the first kill where there was one: off %.1f s.' % (
            statistics.mean(kills_off) * CYCLE_SECONDS))
    if kills_on:
        lines.append('Mean time to the first kill where there was one: on %.1f s.' % (
            statistics.mean(kills_on) * CYCLE_SECONDS))
    if both:
        differences = [(on - off) * CYCLE_SECONDS for off, on in both]
        low, high = interval(differences)
        lines.append('Where both arms had a kill (%d encounters): first kill %+.1f s with the rule on '
                     '(95%% interval %+.1f to %+.1f s).' % (len(both), statistics.mean(differences), low, high))
        result['first_kill_both'] = (len(both), statistics.mean(differences), low, high)
    for arm, index in (('off', 1), ('on', 2)):
        distances = [int(pair[index]['first_shot_distance']) / 64 for pair in pairs
                     if int(pair[index]['first_shot_distance']) >= 0]
        if distances:
            lines.append('First Trooper shot, reach %s: in %d/%d encounters, at a mean %.1f tiles from the '
                         'nearest Ornithopter.' % (arm, len(distances), len(pairs), statistics.mean(distances)))
    lines.append('')
    return lines, result


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--seeds', type=seed_list, default=seed_list('1-4'),
                    help='Seeds for the unexplored-ground encounters (54 encounter kinds per seed), e.g. 1-4')
parser.add_argument('--fog-seeds', type=seed_list, default=seed_list('1'),
                    help='Seeds for explored ground under Fog of War')
parser.add_argument('--explored-seeds', type=seed_list, default=seed_list('1-4'),
                    help='Seeds for explored ground without fog, the Dune City mod\'s own options')
parser.add_argument('--report', action='store_true',
                    help='Only recompute the report from an existing encounters.csv in --output-dir')
parser.add_argument('--wall-timeout', type=int, default=3000)
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)

if not args.report:
    require_free_space()
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    commands = native_build_commands(build)
    main = (root / 'src/main.cpp').read_text(encoding='utf-8')
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed; update the Trooper Ornithopter effect runner.')
    main = main.replace(needle, 'int menuResult = runTrooperOrnithopterEffect();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = (main[:position] + NATIVE_INCLUDES + '#include "'
            + (root / 'tests/units/trooper-ornithopter-effect.inc').as_posix() + '"\n' + main[position:])
    source, obj = out / 'trooper-ornithopter-effect-main.cpp', out / 'trooper-ornithopter-effect-main.o'
    source.write_text(main, encoding='utf-8')
    compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
    # A private diagnostic object must not overwrite Ninja's production depfile/PCH records.
    for option in ('-include', '-MT', '-MF'):
        if option in compile_command:
            position = compile_command.index(option)
            del compile_command[position:position + 2]
    for option in ('-MD', '-MMD'):
        if option in compile_command:
            compile_command.remove(option)
    compile_command[compile_command.index('-o') + 1] = str(obj)
    compile_command[compile_command.index('-c') + 1] = str(source)
    compile_command.append('-fno-access-control')
    binary = native_probe_binary(build, out, 'trooper-ornithopter-effect')
    link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

    with scratch_dir('trooper-ornithopter-effect') as scratch:
        profile = scratch / 'profile'
        profile.mkdir()
        (profile / 'Dune City.ini').write_text(
            '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\nPlayer Name = AA tester\n',
            encoding='utf-8')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
                   TROOPER_ORNITHOPTER_EFFECT_OUT=str(out),
                   TROOPER_ORNITHOPTER_EFFECT_SEEDS=','.join(str(seed) for seed in args.seeds),
                   TROOPER_ORNITHOPTER_EFFECT_FOG_SEEDS=','.join(str(seed) for seed in args.fog_seeds),
                   TROOPER_ORNITHOPTER_EFFECT_EXPLORED_SEEDS=','.join(str(seed) for seed in args.explored_seeds))
        logfile = out / 'run.log'
        with logfile.open('w') as log:
            subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.wall_timeout)
        text = logfile.read_text(errors='replace')
        if text.count('TROOPER_ORNITHOPTER_EFFECT_PASS:') != 1 or 'TROOPER_ORNITHOPTER_EFFECT_FAIL:' in text:
            raise RuntimeError('Missing or failed Trooper Ornithopter effect run; see ' + str(logfile))
        discard_profile_assets(profile)
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

with (out / 'encounters.csv').open(newline='') as handle:
    rows = list(csv.DictReader(handle))
pairs = pair_up(rows)
field = pairs[0][1].keys()
with (out / 'pairs.csv').open('w', newline='') as handle:
    writer = csv.writer(handle)
    writer.writerow(list(KEY) + [m + '_off' for m in MEASURES] + [m + '_on' for m in MEASURES]
                    + ['first_kill_tick_off', 'first_kill_tick_on'])
    for key, off, on in pairs:
        writer.writerow(list(key) + [off[m] for m in MEASURES] + [on[m] for m in MEASURES]
                        + [off['first_kill_tick'], on['first_kill_tick']])

report = ['# Trooper Ornithopter Reach: controlled engagements', '',
          'Reach off is the rule up to 1.0.876; on is the fix. Trooper Anti-Air is on in both arms.',
          'One cycle is 16 ms; an encounter ends when one side is gone or after 90 game seconds.', '']
failures = []
for terrain, title, must_gain in (('explored', 'Explored ground, no fog (the Dune City mod\'s options)', True),
                                  ('unexplored', 'Unexplored ground', True),
                                  ('fogged', 'Explored ground under Fog of War', False)):
    chosen = [pair for pair in pairs if pair[0][0] == terrain]
    if not chosen:
        continue
    seeds = sorted({int(pair[0][5]) for pair in chosen})
    lines, overall = describe(chosen, title + ', seeds ' + ','.join(str(seed) for seed in seeds))
    report += lines
    if must_gain:
        for troopers in ('1', '3', '6'):
            lines, _ = describe([pair for pair in chosen if pair[0][1] == troopers],
                                '%s, %s Trooper%s' % (title, troopers, '' if troopers == '1' else 's'))
            report += lines
        for ornithopters in ('1', '2', '3'):
            lines, _ = describe([pair for pair in chosen if pair[0][2] == ornithopters],
                                '%s, %s Ornithopter%s' % (title, ornithopters, '' if ornithopters == '1' else 's'))
            report += lines
        if not overall['ornithopters_down'][3] > 0:
            failures.append('%s: Ornithopters shot down did not improve, interval %+.3f to %+.3f'
                            % ((terrain,) + overall['ornithopters_down'][3:5]))
        if overall['troopers_lost'][3] > 0:
            failures.append('%s: Trooper losses rose, interval %+.3f to %+.3f'
                            % ((terrain,) + overall['troopers_lost'][3:5]))
    elif overall['ornithopters_down'][4] < 0:
        failures.append('%s: fewer Ornithopters were shot down, interval %+.3f to %+.3f'
                        % ((terrain,) + overall['ornithopters_down'][3:5]))
(out / 'summary.md').write_text('\n'.join(report) + '\n', encoding='utf-8')
print('\n'.join(report))

if failures:
    sys.exit('TROOPER ORNITHOPTER EFFECT FAILED: ' + '; '.join(failures))
print('Trooper Ornithopter effect: ' + str(out / 'summary.md'))
