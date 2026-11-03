#!/usr/bin/env python3
"""Reproducible DuneCity QuantBot Random Skirmish audit (docs/quantbot-dunecity-audit.md).

The ordinary Random Skirmish generator and real all-QuantBot game loop supply every
measurement. Detectors identify suspicious house histories, not automatic proof of a bug.
Use dunecity-queue submit for games; --report and --compare only read captured CSVs.
"""
import argparse
import csv
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
import threading
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
from probe_profile import finish_game, probe_binary, require_free_space, scratch_dir  # noqa: E402
import match_score  # noqa: E402

HOUSES = ('harkonnen', 'atreides', 'ordos', 'sardaukar', 'fremen', 'mercenary')
HOUSE_IDS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
CLASSES = {'easy': 'qBotEasy', 'medium': 'qBotMedium', 'hard': 'qBotHard', 'brutal': 'qBotBrutal'}
SPICE = {'none': 0, 'low': 1, 'medium': 2}
DETECTORS = ('economy_stall', 'power_deficit', 'no_ground_defence', 'missing_starport', 'missing_radar',
             'missing_heavy_factory', 'missing_ix', 'idle_under_attack', 'unused_credits',
             'bankrupt_queue_empty', 'early_no_fight', 'zero_harvesters')
META = ('id', 'seed', 'size', 'spice_level', 'format', 'difficulty', 'variant', 'base', 'army', 'city', 'limit', 'style')
GAME_FIELDS = META + ('sides', 'map_seed', 'terrain_attempts', 'skipped', 'outcome', 'winner_team', 'minutes',
                      'loop_ms', 'wall_s', 'digest', 'attack_telemetry', 'error')
HOUSE_FIELDS = META + ('house', 'team', 'alive', 'eliminated_s', 'kill_value', 'loss_value', 'structures',
                       'army_value', 'army_units', 'sortie_minutes', 'attack_telemetry', 'attack_count',
                       'first_attack_minute', 'score', 'drawn', 'won', 'flags') + tuple(
    field for detector in DETECTORS for field in (detector, 'eligible_' + detector, 'first_' + detector))
CYCLES_PER_MINUTE = 3750  # The engine's 16 ms fixed simulation tick.
SAMPLE_VALUES = ('minute', 'cycle', 'house', 'team', 'alive', 'credits', 'army_value', 'army_units', 'harvesters',
                 'structures', 'factories', 'kill_value', 'loss_value', 'power', 'need', 'population', 'tax', 'demand',
                 'built', 'structures_lost', 'base_hp', 'yards', 'yard_level', 'queue', 'upgrading', 'waiting', 'fronts',
                 'gun', 'rocket', 'radar', 'radar_built', 'starport', 'starport_built', 'heavy', 'heavy_built', 'ix',
                 'ix_built', 'fighters', 'moving', 'targeting', 'enemies', 'enemy_value', 'covered_enemies', 'spice',
                 'ledger_enabled', 'spice_gross', 'tax_gross', 'starting_cash', 'spice_cash', 'city_cash', 'capacity',
                 'yard_storage', 'police_funding', 'army_target', 'unit_limit', 'spice_multiplier', 'sale_credits',
                 'spent_total', 'power_spent', 'police_charged', 'refunded', 'storage_lost', 'city_net_applied')
SAMPLE_FIELDS = META + SAMPLE_VALUES + ('flags',)
ATTACK_FIELDS = META + ('house', 'cycle', 'members', 'value')
lock = threading.Lock()


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def append(path, fields, rows):
    with lock:
        new = not path.exists()
        with path.open('a', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
            if new:
                writer.writeheader()
            writer.writerows(rows)


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def metadata(game):
    return {field: game[field] for field in META}


def schedule(args):
    games = []
    for size in args.sizes:
        for spice in args.spice:
            for fmt in args.formats:
                difficulties = [(level,) for level in args.difficulties] if args.mode == 'matrix' else [
                    ('brutal', 'hard'), ('hard', 'medium'), ('medium', 'easy')]
                if args.mode == 'ladder' and fmt != '1v1':
                    continue
                for levels in difficulties:
                    for index, seed in enumerate(args.seeds):
                        for swapped in range(2 if args.mode == 'ladder' else 1):
                            count = 2 if fmt == '1v1' else 4
                            houses = list(HOUSES[:count])
                            shift = index % count
                            houses = houses[shift:] + houses[:shift]
                            if swapped:
                                houses.reverse()
                            teams = [1, 1, 2, 2] if fmt == '2v2' else list(range(1, count + 1))
                            sides = [(house, team, levels[i] if len(levels) > 1 else levels[0])
                                     for i, (house, team) in enumerate(zip(houses, teams))]
                            difficulty = '-v-'.join(levels)
                            cell = f'{args.mode}/{args.base}-a{args.army}-c{args.city}/{size}/{spice}/{fmt}/{difficulty}/{args.minutes}m/{seed}'
                            if swapped:
                                cell += '/swap'
                            games.append(dict(id=cell, seed=seed, size=size, spice_level=spice, format=fmt,
                                              difficulty=difficulty, variant=args.variant, sides=sides, base=args.base, army=args.army,
                                              city=args.city, limit=args.minutes, style=args.style or 'default',
                                              minutes=args.minutes, skirmish=f'size={size},base={args.base},army={args.army},'
                                              f'city={args.city},spice={SPICE[spice]},rock=2,tech=9,credits=3000,seed={seed}'))
    return games


def detect(samples):
    """Per-house flags, eligibility and first minute; durations use cycles, not sample counts."""
    first, eligible, since, previous = {}, set(), {}, None
    seen = {'starport': False, 'radar': False, 'heavy': False, 'ix': False}
    for sample in sorted(samples, key=lambda row: int(row['cycle'])):
        s = {key: int(value) for key, value in sample.items()
             if key not in META and key not in ('fronts', 'flags') and value != ''}
        minute, cycle = s['minute'], s['cycle']
        alive = bool(s['alive'])
        city = alive and s['yards'] > 0
        old = previous or s
        growth = s['built'] > old['built'] or s['population'] > old['population']
        under_attack = s['enemies'] >= 3 and (s['structures_lost'] > old['structures_lost']
                                            or s['base_hp'] < old['base_hp'])
        for item in seen:
            seen[item] |= s[item] > 0 or s.get(item + '_built', 0) > 0
        predicates = {
            'economy_stall': (city and s['credits'] < 1000 and not growth, 4, city and minute >= 4),
            'power_deficit': (city and s['power'] < s['need'], 4, city and minute >= 4),
            'no_ground_defence': (city and minute >= 5 and s['enemies'] >= 3 and s['covered_enemies'] == 0,
                                  2, city and minute >= 5 and s['enemies'] >= 3),
            'idle_under_attack': (alive and under_attack and s['fighters'] >= 3 and s['moving'] == 0
                                  and s['targeting'] == 0, 2, alive and under_attack and s['fighters'] >= 3),
            'unused_credits': (city and minute >= 5 and s['credits'] >= 5000 and s['queue'] == 0
                               and s['upgrading'] == 0 and not growth, 4, city and minute >= 9),
            'bankrupt_queue_empty': (city and s['credits'] < 100 and s['queue'] == 0 and s['upgrading'] == 0,
                                     2, city and minute >= 2),
            'zero_harvesters': (city and minute >= 5 and s['spice'] > 0 and s['harvesters'] == 0,
                                4, city and minute >= 5 and s['spice'] > 0),
        }
        for item in seen:
            name = {'heavy': 'missing_heavy_factory', 'ix': 'missing_ix'}.get(item, 'missing_' + item)
            predicates[name] = (city and minute >= 20 and not seen[item], 0, city and minute >= 20)
        predicates['early_no_fight'] = (not alive and minute < 10 and s['kill_value'] < 500, 0, True)
        flags = []
        for name, (condition, duration, exposed) in predicates.items():
            if exposed:
                eligible.add(name)
            if not condition:
                since.pop(name, None)
                continue
            since.setdefault(name, cycle)
            if cycle - since[name] >= duration * CYCLES_PER_MINUTE:
                first.setdefault(name, minute)
                flags.append(name)
        sample['flags'] = '/'.join(flags)
        previous = s
    return {field: value for detector in DETECTORS for field, value in (
        (detector, int(detector in first)), ('eligible_' + detector, int(detector in eligible)),
        ('first_' + detector, first.get(detector, '')))}


def command(args, binary, game, out):
    sides = ','.join(f'{house}:{team}:{CLASSES[level]}' for house, team, level in game['sides'])
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--scenario', '--mod', 'dunecity',
           '--seed', str(game['seed']), '--minutes', str(game['minutes']), '--capture-mib', '256',
           '--wall-timeout', str(args.wall_timeout), '--path-workers', '1', '--house', game['sides'][0][0],
           '--scenario-sides', sides, '--local-spectator', '--skirmish', game['skirmish']]
    if not args.attack_events:
        cmd += ['--no-ai-telemetry']
    if args.game_options:
        cmd += ['--game-options', str(args.game_options)]
    if args.style:
        cmd += ['--scenario-style', args.style]
    return cmd


def play(args, scratch, binary, game):
    out = scratch / ('g' + hashlib.sha256(game['id'].encode()).hexdigest()[:12])
    started = time.monotonic()
    process = subprocess.run(command(args, binary, game, out), cwd=root, capture_output=True, text=True,
                             env=dict(os.environ, BALANCE_DUNECITY_AUDIT='1'))
    log = out / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    result = next((parse(line, 'SCENARIO_RESULT:') for line in lines if 'SCENARIO_RESULT:' in line), {})
    final = [parse(line, 'SCENARIO_HOUSE:') for line in lines if 'SCENARIO_HOUSE:' in line]
    generated = next((parse(line, 'SCENARIO_MAP:') for line in lines if 'SCENARIO_MAP:' in line), {})
    timing = next((parse(line, 'PROBE_TIMING:') for line in lines if 'PROBE_TIMING:' in line), {})
    digest = next((parse(line, 'PROBE_DIGEST:').get('digest', '') for line in lines
                   if 'PROBE_DIGEST:' in line and 'final=1' in line), '')
    merged = defaultdict(dict)
    for line in lines:
        for marker in ('ELIMINATION_SAMPLE:', 'DUNECITY_AUDIT:', 'DUNECITY_ECONOMY:'):
            if marker in line:
                data = parse(line, marker)
                data.update(json.loads(data.pop('economy', '{}')))
                merged[(int(data['house']), int(data['cycle']))].update(data)
    samples = [dict(metadata(game), **data) for _, data in sorted(merged.items())]
    missing = any('power' not in sample for sample in samples)
    error = '' if process.returncode == 0 and result and samples and not missing else f'rc={process.returncode};incomplete={int(missing)}'
    row = dict(metadata(game), sides=','.join(f'{h}:{t}:{d}' for h, t, d in game['sides']),
               map_seed=generated.get('map_seed', ''), terrain_attempts=generated.get('attempts', ''),
               skipped=generated.get('skipped', ''), outcome=result.get('outcome', ''), winner_team=result.get('winner_team', ''),
               minutes=result.get('minutes', ''), loop_ms=timing.get('loop_ms', ''),
               wall_s=round(time.monotonic() - started, 3), digest=digest, attack_telemetry=int(args.attack_events), error=error)
    results = Path(args.results)
    if error:
        (results / f'failed-{out.name}.txt').write_text(game['id'] + '\n' + process.stdout[-4000:] + '\n'
                                                       + process.stderr[-4000:] + '\n' + '\n'.join(lines[-80:]))
    else:
        groups = defaultdict(list)
        for sample in samples:
            groups[int(sample['house'])].append(sample)
        ids = [int(h['house']) for h in final]
        eliminated = {int(h['house']): float(h['eliminated_s']) for h in final if h['alive'] == '0'}
        teams = {int(h['house']): int(h['team']) for h in final}
        scores = match_score.score_game(ids, eliminated, teams=teams)
        house_rows = []
        difficulty_of = {HOUSE_IDS[h]: d for h, _, d in game['sides']}
        attacks = defaultdict(list)
        attack_rows = []
        if args.attack_events:
            for event in json.loads((out / 'summary.json').read_text())['attacks']:
                if int(event['data']['members']) <= 0:
                    continue
                house = int(event['house'])
                cycle = int(event['cycle'])
                attacks[house].append(cycle / CYCLES_PER_MINUTE)
                attack_rows.append(dict(metadata(game), house=house, cycle=cycle,
                                        members=event['data']['members'], value=event['data']['value']))
        for house in final:
            number = int(house['house'])
            detectors = detect(groups[number])
            house_rows.append(dict(metadata(game), **{key: house[key] for key in (
                'house', 'team', 'alive', 'eliminated_s', 'kill_value', 'loss_value', 'structures',
                'army_value', 'army_units', 'sortie_minutes')}, **detectors,
                score=round(scores[number].score, 4), drawn=int(scores[number].drawn),
                won=int(house['team'] == result.get('winner_team')), difficulty=difficulty_of[number],
                attack_telemetry=int(args.attack_events),
                attack_count=len(attacks[number]) if args.attack_events else '',
                first_attack_minute=min(attacks[number]) if attacks[number] else '',
                flags='/'.join(d for d in DETECTORS if detectors[d])))
        append(results / 'samples.csv', SAMPLE_FIELDS, samples)
        append(results / 'houses.csv', HOUSE_FIELDS, house_rows)
        if args.attack_events:
            append(results / 'waves.csv', ATTACK_FIELDS, attack_rows)
    append(results / 'games.csv', GAME_FIELDS, [row])
    finish_game(scratch, out, bool(error))
    return row


def build_probe(args, scratch):
    if args.prebuilt_probe:
        return args.prebuilt_probe
    out = scratch / 'probe'
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
                    '--output-dir', str(out), '--scenario', '--mod', 'dunecity', '--skirmish',
                    'size=64,base=cy,army=0,spice=0', '--scenario-sides',
                    'harkonnen:1:qBotEasy,atreides:2:qBotEasy', '--house', 'harkonnen', '--local-spectator',
                    '--minutes', '1', '--capture-mib', '256', '--no-ai-telemetry'],
                   cwd=root, check=True, capture_output=True, text=True)
    shutil.rmtree(out / 'profile', ignore_errors=True)
    return probe_binary(out)


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    done = {r['id'] for r in read(results / 'games.csv') if not r['error']
            and (not args.attack_events or r.get('attack_telemetry') == '1')}
    todo = [game for game in schedule(args) if game['id'] not in done]
    print(f'{len(todo)} games to play ({len(done)} complete)', flush=True)
    if todo:
        require_free_space()
        with scratch_dir('dunecity-ai-audit', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = [pool.submit(play, args, scratch, binary, game) for game in todo]
                for n, future in enumerate(as_completed(futures), 1):
                    row = future.result()
                    print(f'[{n}/{len(todo)}] {row["id"]}: {row["minutes"]} min {row["error"]}', flush=True)
    report([results])


def load(directories, name, keys):
    rows = {}
    for directory in directories:
        for row in read(Path(directory) / name):
            rows[tuple(row[key] for key in keys)] = row
    return list(rows.values())


def report(directories):
    games = load(directories, 'games.csv', ('id',))
    houses = load(directories, 'houses.csv', ('id', 'house'))
    completed = [game for game in games if not game['error']]
    print(f'Games {len(completed)}/{len(games)}, houses {len(houses)}, '
          f'time limits {sum(game["winner_team"] == "0" for game in completed)}; '
          f'mean length {statistics.mean(float(g["minutes"]) for g in completed):.2f} min' if completed else 'No completed games')
    print('| Difficulty | Spice | Houses | ' + ' | '.join(DETECTORS) + ' |')
    print('|---|---|---|' + '---|' * len(DETECTORS))
    groups = defaultdict(list)
    for house in houses:
        groups[(house['difficulty'], house['spice_level'])].append(house)
    groups[('all', 'all')] = houses
    for (difficulty, spice), rows in sorted(groups.items()):
        cells = []
        for name in DETECTORS:
            hits = sum(int(row[name]) for row in rows)
            exposed = sum(int(row['eligible_' + name]) for row in rows)
            cells.append(f'{hits}/{exposed}')
        print(f'| {difficulty} | {spice} | {len(rows)} | ' + ' | '.join(cells) + ' |')
    for name in DETECTORS:
        examples = [f'{row["id"]}:h{row["house"]}@{row["first_" + name]}'
                    for row in houses if row[name] == '1']
        print(f'{name}: ' + ('; '.join(examples[:6]) if examples else 'none'))
    ladder = defaultdict(list)
    for game in completed:
        if game['id'].startswith('ladder/'):
            ladder[game['difficulty']].append(game)
    for pair, rows in sorted(ladder.items()):
        print(f'Ladder {pair}: stronger wins {sum(row["winner_team"] == "1" for row in rows)}/{len(rows)}, '
              f'weaker {sum(row["winner_team"] == "2" for row in rows)}, '
              f'time limit {sum(row["winner_team"] == "0" for row in rows)}')
    combat_report(directories)


def combat_report(directories, allowed=None, label='Combat'):
    """Exact ground dispatches when enabled; checkpoints include defeated houses as zero."""
    games = {r['id']: r for r in load(directories, 'games.csv', ('id',)) if not r['error']}
    houses = [r for r in load(directories, 'houses.csv', ('id', 'house'))
              if r['id'] in games and (allowed is None or (r['id'], r['house']) in allowed)]
    samples = defaultdict(dict)
    for sample in load(directories, 'samples.csv', ('id', 'house', 'cycle')):
        samples[(sample['id'], sample['house'])][int(sample['cycle'])] = sample
    groups = defaultdict(list)
    for house in houses:
        groups[(house['difficulty'], house['spice_level'])].append(house)
    groups[('all', 'all')] = houses
    print(f'{label}: first attack is a nonempty ground_hunt dispatch, not a readiness check or sortie duration.')
    print('| Difficulty | Spice | Attacking/measured | First attack mean min (attackers) | Attacks/10 alive-min | Army10 mean (n) | Army20 mean (n) | Kill-value mean |')
    print('|---|---|---|---|---|---|---|---|')
    for (difficulty, spice), rows in sorted(groups.items()):
        measured = [r for r in rows if r.get('attack_telemetry') == '1']
        first = [float(r['first_attack_minute']) for r in measured if r.get('first_attack_minute', '') != '']
        minutes = sum(min(float(games[r['id']]['minutes']), float(r['eliminated_s']) / 60)
                      if r['alive'] == '0' else float(games[r['id']]['minutes']) for r in measured)
        frequency = sum(int(r['attack_count']) for r in measured) * 10 / minutes if minutes else None
        checkpoints = []
        for minute in (10, 20):
            values = []
            for house in rows:
                point = samples[(house['id'], house['house'])].get(minute * CYCLES_PER_MINUTE)
                if point:
                    values.append(int(point['army_value']))
                elif house['alive'] == '0' and float(house['eliminated_s']) <= minute * 60:
                    values.append(0)
            checkpoints.append(f'{statistics.mean(values):.1f} ({len(values)})' if values else 'unmeasured')
        first_mean = f'{statistics.mean(first):.2f}' if first else 'unmeasured'
        rate = f'{frequency:.3f}' if frequency is not None else 'unmeasured'
        kills = f'{statistics.mean(int(r["kill_value"]) for r in rows):.1f}' if rows else 'unmeasured'
        print(f'| {difficulty} | {spice} | {len(first)}/{len(measured)} | {first_mean} | {rate} | '
              + ' | '.join(checkpoints) + f' | {kills} |')


def compare(control, candidate):
    old = {(row['id'], row['house']): row for row in load([control], 'houses.csv', ('id', 'house'))}
    new = {(row['id'], row['house']): row for row in load([candidate], 'houses.csv', ('id', 'house'))}
    common = sorted(old.keys() & new.keys())
    print(f'Paired houses {len(common)} (control-only {len(old.keys() - new.keys())}, '
          f'candidate-only {len(new.keys() - old.keys())}); same game ids and houses only')
    print('| Detector | Control hit/exposed | Candidate hit/exposed |')
    print('|---|---|---|')
    for name in DETECTORS:
        rates = []
        for source in (old, new):
            rates.append(f'{sum(int(source[key][name]) for key in common)}/'
                         f'{sum(int(source[key]["eligible_" + name]) for key in common)}')
        print(f'| {name} | ' + ' | '.join(rates) + ' |')
    for level in CLASSES:
        by_game = defaultdict(list)
        for key in common:
            if old[key]['difficulty'] == level:
                by_game[key[0]].append(key)
        # Houses in one match are not independent observations: bootstrap match means.
        pairs = [(statistics.mean(float(new[k]['score']) for k in keys),
                  statistics.mean(float(old[k]['score']) for k in keys),
                  all(new[k]['drawn'] == '1' for k in keys), all(old[k]['drawn'] == '1' for k in keys))
                 for keys in by_game.values()]
        if pairs:
            result = match_score.compare(pairs, 9101)
            print(f'{level}: mean score {result.control:.3f} -> {result.candidate:.3f}, '
                  f'paired delta {result.difference:+.3f} [{result.low:+.3f}, {result.high:+.3f}], '
                  f'{result.verdict}; wins {sum(old[k]["won"] == "1" for k in common if old[k]["difficulty"] == level)} -> '
                  f'{sum(new[k]["won"] == "1" for k in common if old[k]["difficulty"] == level)}')
    previous = {row['id']: row for row in load([control], 'games.csv', ('id',)) if not row['error']}
    current = {row['id']: row for row in load([candidate], 'games.csv', ('id',)) if not row['error']}
    keys = previous.keys() & current.keys()
    if keys:
        for label, source in (('Control', previous), ('Candidate', current)):
            print(f'{label}: mean game {statistics.mean(float(source[k]["minutes"]) for k in keys):.2f} min; '
                  f'mean loop {statistics.mean(float(source[k]["loop_ms"]) for k in keys):.1f} ms '
                  '(diagnostic only: mixed host/load, not a performance gate)')
    combat_report([control], set(common), 'Control combat')
    combat_report([candidate], set(common), 'Candidate combat')


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', type=Path)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--prebuilt-probe', type=Path, help='Use a frozen probe binary without building or leasing the checkout')
parser.add_argument('--scratch')
parser.add_argument('--jobs', type=int, default=2)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--sizes', nargs='+', type=int, choices=(96, 128, 160), default=[96, 128, 160])
parser.add_argument('--spice', nargs='+', choices=tuple(SPICE), default=list(SPICE))
parser.add_argument('--formats', nargs='+', choices=('1v1', '2v2', 'ffa4'), default=['1v1', '2v2', 'ffa4'])
parser.add_argument('--difficulties', nargs='+', choices=('medium', 'hard', 'brutal'), default=['medium', 'hard', 'brutal'])
parser.add_argument('--seeds', nargs='+', type=int, default=[9101, 148680876])
parser.add_argument('--minutes', type=int, default=30)
parser.add_argument('--base', choices=('cy', 'small', 'medium', 'large', 'fortress'), default='medium')
parser.add_argument('--army', type=int, choices=range(7), default=2)
parser.add_argument('--city', type=int, choices=range(6), default=0)
parser.add_argument('--mode', choices=('matrix', 'ladder'), default='matrix')
parser.add_argument('--variant', default='baseline')
parser.add_argument('--game-options', type=Path)
parser.add_argument('--style', help='QuantBot switch overlay from tests/ai/play-styles/ (same-build A/B)')
parser.add_argument('--attack-events', action='store_true',
                    help='Enable existing AI telemetry, retain exact ground dispatches in waves.csv, and report passivity metrics')
parser.add_argument('--list', action='store_true')
parser.add_argument('--report', nargs='+', type=Path, dest='report_dirs')
parser.add_argument('--compare', nargs=2, type=Path, metavar=('CONTROL', 'CANDIDATE'))
args = parser.parse_args()
if not 1 <= args.minutes <= 120 or any(not 0 <= seed <= 0xffffffff for seed in args.seeds):
    parser.error('Use 1-120 game minutes and unsigned 32-bit seeds')
if args.compare:
    compare(*args.compare)
elif args.report_dirs:
    report(args.report_dirs)
elif args.list:
    games = schedule(args)
    for game in games:
        print(game['id'], game['skirmish'])
    print(f'{len(games)} games, {sum(len(game["sides"]) for game in games)} houses, {args.minutes} game minutes')
elif not args.results:
    parser.error('--results is required to play games')
else:
    run(args)
