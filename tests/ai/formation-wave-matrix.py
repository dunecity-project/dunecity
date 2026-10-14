#!/usr/bin/env python3
"""QuantBot attack-wave formation measurement (docs/formations.md, QuantBot waves).

Runs run-campaign-balance.py --formation-measure for every map x seed x wave shape, one
run at a time, appends the FORMATION_WAVE / FORMATION_HOUSE / result lines of each run to
CSV files and deletes the run directory straight away (the telemetry is large). Runs
already in the CSV are skipped, so an interrupted matrix resumes. --report prints the
markdown tables used in docs/formations.md.
"""
import argparse
import csv
import shutil
import statistics
import subprocess
import time
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[2]
MAPS = {
    'faceoff': '2P - 64x64 - Face Off.ini',
    'penny': '2P - 128x128 - 1v1 - Habbanya-Penny.ini',
    'ergun': '2P - 128x64 - 1v1 - Ergun-Sahij.ini',
}
MAP_NAMES = {'faceoff': '2P 64x64 Face Off', 'penny': '2P 128x128 Habbanya-Penny', 'ergun': '2P 128x64 Ergun-Sahij'}
SEEDS = (486409243, 1841712204, 41562270)
SHAPES = ('none', 'combined', 'box', 'line')
WAVE_FIELDS = ('house', 'launched', 'members', 'engaged', 'arrived', 'lost', 'contact_s', 'engage_spread_s',
               'arrival_spread_s', 'shots30', 'loss60', 'kill60', 'survivors', 'lost_before_contact', 'formed',
               'formup_s', 'formation_end_s', 'in_formation_at_contact')
HOUSE_FIELDS = ('house', 'minute', 'army_value', 'army_units', 'structures', 'kill_value', 'loss_value', 'alive')
KEY = ('tag', 'difficulty', 'map', 'seed', 'shape')


def parse(line, marker):
    body = line.split(marker, 1)[1].strip()
    return dict(part.split('=', 1) for part in body.split())


def append(path, fields, rows):
    new = not path.exists()
    with path.open('a', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        if new:
            writer.writeheader()
        writer.writerows(rows)


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def run_matrix(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    done = {tuple(row[k] for k in KEY) for row in read(results / 'runs.csv')}
    scratch = Path(args.scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    for seed in args.seeds:
        for map_key in args.maps:
            for shape in args.shapes:
                key = (args.tag, args.difficulty, map_key, str(seed), shape)
                if key in done:
                    continue
                out = scratch / f'{args.tag}-{args.difficulty}-{map_key}-{seed}-{shape}'
                shutil.rmtree(out, ignore_errors=True)
                command = ['python3', str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
                           '--output-dir', str(out), '--custom-map', str(root / 'data/maps/multiplayer' / MAPS[map_key]),
                           '--local-spectator', '--partner-difficulty', args.difficulty,
                           '--enemy-difficulty', args.difficulty, '--minutes', str(args.minutes),
                           '--seed', str(seed), '--formation-measure', '--wave-formation', shape,
                           '--capture-mib', '256', '--wall-timeout', '3500']
                started = time.time()
                outcome = subprocess.run(command, cwd=root, capture_output=True, text=True)
                wall = round(time.time() - started)
                log = out / 'run.log'
                lines = log.read_text(errors='replace').splitlines() if log.exists() else []
                result = next((l.split('CAMPAIGN_BALANCE_RESULT:', 1)[1].strip() for l in lines
                               if 'CAMPAIGN_BALANCE_RESULT:' in l), f'failed rc={outcome.returncode}')
                base = dict(zip(KEY, key))
                waves = [{**base, **{k: parse(l, 'FORMATION_WAVE:').get(k, '') for k in WAVE_FIELDS}}
                         for l in lines if 'FORMATION_WAVE:' in l]
                houses = [{**base, **{k: parse(l, 'FORMATION_HOUSE:').get(k, '') for k in HOUSE_FIELDS}}
                          for l in lines if 'FORMATION_HOUSE:' in l]
                append(results / 'waves.csv', KEY + WAVE_FIELDS, waves)
                append(results / 'houses.csv', KEY + HOUSE_FIELDS, houses)
                append(results / 'runs.csv', KEY + ('result', 'wall_s', 'waves'),
                       [{**base, 'result': result, 'wall_s': wall, 'waves': len(waves)}])
                if outcome.returncode != 0:
                    (results / f'failed-{"-".join(map(str, key))}.txt').write_text(
                        outcome.stdout[-4000:] + '\n' + outcome.stderr[-4000:] + '\n' + '\n'.join(lines[-80:]))
                shutil.rmtree(out, ignore_errors=True)
                print(f'{"/".join(map(str, key))}: {len(waves)} waves, {wall} s, {result}', flush=True)


def stats(values):
    values = [v for v in values if v is not None]
    if not values:
        return '-'
    mean = statistics.mean(values)
    median = statistics.median(values)
    return f'{mean:.1f} / {median:.1f}'


def spread_of(per_run):
    """Seed-to-seed spread: min-max of the per-run means."""
    means = [statistics.mean(v) for v in per_run.values() if v]
    if len(means) < 2:
        return '-'
    return f'{min(means):.1f}-{max(means):.1f}'


def report(args):
    results = Path(args.results)
    waves = [w for w in read(results / 'waves.csv') if w['tag'] == args.tag and w['difficulty'] == args.difficulty]
    houses = [h for h in read(results / 'houses.csv') if h['tag'] == args.tag and h['difficulty'] == args.difficulty]
    groups = [('All', None)] + [(MAP_NAMES[m], m) for m in args.maps] if args.by_map else [('All', None)]
    for title, map_key in groups:
        print(f'\n#### {title} ({args.tag}, {args.difficulty})\n')
        print('| Shape | Waves (in contact) | Contact after s, median | Engagement spread s, mean / median |'
              ' Spread seed range | Base-arrival spread s, mean / median | Shots 30 s, mean / median |'
              ' Lost 60 s, mean / median | Killed 60 s, mean / median | Killed / lost | Net trade seed range |'
              ' Survival | Lost before contact | Form-up s, median | In formation at contact |')
        print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
        for shape in args.shapes:
            rows = [w for w in waves if w['shape'] == shape and (map_key is None or w['map'] == map_key)]
            fought = [w for w in rows if float(w['contact_s']) >= 0]
            engaged = [w for w in fought if int(w['engaged']) >= 3]
            arrived = [w for w in rows if int(w['arrived']) >= 3]
            per_run = defaultdict(list)
            for w in engaged:
                per_run[(w['map'], w['seed'])].append(float(w['engage_spread_s']))
            net = defaultdict(int)
            for w in fought:
                net[(w['map'], w['seed'])] += int(w['kill60']) - int(w['loss60'])
            members = sum(int(w['members']) for w in rows)
            survivors = sum(max(0, int(w['survivors'])) for w in rows)
            loss = sum(int(w['loss60']) for w in fought)
            kill = sum(int(w['kill60']) for w in fought)
            formups = [float(w['formup_s']) for w in rows if float(w['formup_s']) >= 0]
            at_contact = [int(w['in_formation_at_contact']) / max(1, int(w['formed']))
                          for w in fought if int(w['formed']) > 0 and int(w['in_formation_at_contact']) >= 0]
            contact = [float(w['contact_s']) for w in fought]
            print(f'| {shape} | {len(rows)} ({len(fought)}) |'
                  f' {statistics.median(contact) if contact else float("nan"):.1f} |'
                  f' {stats([float(w["engage_spread_s"]) for w in engaged])} | {spread_of(per_run)} |'
                  f' {stats([float(w["arrival_spread_s"]) for w in arrived])} |'
                  f' {stats([int(w["shots30"]) for w in fought])} |'
                  f' {stats([int(w["loss60"]) for w in fought])} | {stats([int(w["kill60"]) for w in fought])} |'
                  f' {kill / loss if loss else float("nan"):.2f} |'
                  f' {min(net.values()) if net else 0}..{max(net.values()) if net else 0} |'
                  f' {survivors / members if members else 0:.0%} |'
                  f' {sum(int(w["lost_before_contact"]) for w in rows)} |'
                  f' {statistics.median(formups) if formups else "-"} |'
                  f' {statistics.mean(at_contact) if at_contact else float("nan"):.0%} |')
    print(f'\n#### Matches ({args.tag}, {args.difficulty})\n')
    print('| Shape | Army value at 10 / 20 / 30 min (mean of both houses) | Kill value at end (mean) |'
          ' Decided matches | Army-value lead swing at 30 min (mean abs difference) |')
    print('|---|---|---|---|---|')
    for shape in args.shapes:
        rows = [h for h in houses if h['shape'] == shape]
        by_minute = defaultdict(list)
        ends = defaultdict(list)
        for h in rows:
            by_minute[int(h['minute'])].append(int(h['army_value']))
            if int(h['minute']) == -1:
                ends[(h['map'], h['seed'])].append(h)
        decided = sum(1 for match in ends.values() if any(h['alive'] == '0' for h in match))
        diffs = [abs(int(m[0]['army_value']) - int(m[1]['army_value'])) for m in ends.values() if len(m) == 2]
        kills = [int(h['kill_value']) for match in ends.values() for h in match]
        trend = ' / '.join(f'{statistics.mean(by_minute[m]):.0f}' if by_minute[m] else '-' for m in (10, 20, 30))
        print(f'| {shape} | {trend} | {statistics.mean(kills) if kills else 0:.0f} | {decided}/{len(ends)} |'
              f' {statistics.mean(diffs) if diffs else 0:.0f} |')


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--results', required=True, help='Directory for runs.csv, waves.csv, houses.csv')
parser.add_argument('--scratch', help='Directory for the (deleted) run directories')
parser.add_argument('--tag', default='baseline', help='Label of the code variant being measured')
parser.add_argument('--difficulty', choices=('hard', 'brutal'), default='hard')
parser.add_argument('--minutes', type=int, default=30)
parser.add_argument('--maps', nargs='+', choices=tuple(MAPS), default=list(MAPS))
parser.add_argument('--seeds', nargs='+', type=int, default=list(SEEDS))
parser.add_argument('--shapes', nargs='+', choices=('none', 'combined', 'box', 'line', 'column'), default=list(SHAPES))
parser.add_argument('--report', action='store_true')
parser.add_argument('--by-map', action='store_true')
args = parser.parse_args()
if args.report:
    report(args)
else:
    if not args.scratch:
        parser.error('--scratch is required to run the matrix.')
    run_matrix(args)
