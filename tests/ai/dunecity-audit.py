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
from bisect import bisect_right
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
             'bankrupt_queue_empty', 'early_no_fight', 'zero_harvesters', 'storage_full')
META = ('id', 'seed', 'size', 'spice_level', 'format', 'difficulty', 'variant', 'base', 'army', 'city', 'limit', 'style')
TROOPER_FIELDS = ('wor', 'wor_built', 'trooper_live', 'trooper_queued', 'trooper_single_queued',
                  'trooper_squads_queued', 'trooper_squads5_queued', 'trooper_built', 'ornithopter_live',
                  'elite_ornithopter_live', 'ornithopter_built', 'elite_ornithopter_built', 'ornithopter_lost',
                  'elite_ornithopter_lost', 'ornithopter_killed', 'elite_ornithopter_killed')
# Stable engine item IDs (include/data.h); lethal_damage names the victim type.
ENEMY_AIR_FIELDS = {35: 'enemy_ornithopter_kills', 80: 'enemy_elite_ornithopter_kills'}
GAME_FIELDS = META + ('sides', 'map_seed', 'terrain_attempts', 'skipped', 'outcome', 'winner_team', 'minutes',
                      'loop_ms', 'wall_s', 'digest', 'attack_telemetry', 'error', 'config_hash', 'options_hash',
                      'map_hash', 'content_hash', 'object_data_hash', 'ai_config_hashes', 'probe_sha256',
                      'source_commit', 'source_fingerprint', 'source_fingerprint_error',
                      'air_kill_telemetry', 'air_kill_telemetry_reasons', 'capture_mib')
HOUSE_FIELDS = META + ('house', 'team', 'alive', 'eliminated_s', 'kill_value', 'loss_value', 'structures',
                       'army_value', 'army_units', 'sortie_minutes', 'attack_telemetry', 'attack_count',
                       'first_attack_minute', 'storage_full_cycles', 'storage_overflow_cycles', 'storage_income_lost',
                       'score', 'drawn', 'won', 'flags') + tuple(
    field for detector in DETECTORS for field in (detector, 'eligible_' + detector, 'first_' + detector)
) + TROOPER_FIELDS + ('air_kill_telemetry',) + tuple(ENEMY_AIR_FIELDS.values())
CYCLES_PER_MINUTE = 3750  # The engine's 16 ms fixed simulation tick.
SAMPLE_VALUES = ('minute', 'cycle', 'house', 'team', 'alive', 'credits', 'army_value', 'army_units', 'harvesters',
                 'structures', 'factories', 'kill_value', 'loss_value', 'power', 'need', 'population', 'tax', 'demand',
                 'built', 'structures_lost', 'base_hp', 'yards', 'yard_level', 'queue', 'upgrading', 'waiting', 'fronts',
                 'gun', 'rocket', 'radar', 'radar_built', 'starport', 'starport_built', 'heavy', 'heavy_built', 'ix',
                 'ix_built', 'fighters', 'moving', 'targeting', 'enemies', 'enemy_value', 'covered_enemies', 'spice',
                 'ledger_enabled', 'spice_gross', 'tax_gross', 'starting_cash', 'spice_cash', 'city_cash', 'capacity',
                 'yard_storage', 'police_funding', 'army_target', 'unit_limit', 'spice_multiplier', 'sale_credits',
                 'spent_total', 'power_spent', 'police_charged', 'refunded', 'storage_lost', 'city_net_applied',
                 'storage_full_cycles', 'storage_overflow_cycles', 'storage_income_lost'
) + TROOPER_FIELDS + ('air_kills_complete',) + tuple(ENEMY_AIR_FIELDS.values())
SAMPLE_FIELDS = META + SAMPLE_VALUES + ('flags',)
ATTACK_FIELDS = META + ('house', 'cycle', 'members', 'value')
AIR_KILL_FIELDS = META + ('session', 'seq', 'cycle', 'house', 'attacker', 'victim_house', 'object', 'item',
                          'enemy', 'capture_complete')
lock = threading.Lock()


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def require_schema(path, fields):
    if path.exists():
        with path.open(newline='') as handle:
            if next(csv.reader(handle), []) != list(fields):
                raise ValueError(f'CSV schema changed; use a new results directory: {path}')


def append(path, fields, rows):
    with lock:
        new = not path.exists()
        require_schema(path, fields)
        with path.open('a', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
            if new:
                writer.writeheader()
            writer.writerows(rows)


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def air_kill_capture(out, houses, final_cycle, enabled):
    """Retain actual lethal callbacks, never assign an opponent's losses as kills.

    House::getNumKilledItems is victim-typed but includes friendly splash. The
    existing ObjectBase::handleDamage callback supplies the lethal damagerOwner
    even after its firing object has disappeared. Filter victim/attacker teams
    here; only a complete capture establishes an exact count, including zero.
    """
    if not enabled:
        return dict(status='disabled', reasons='', events=[])
    paths = sorted((out / 'profile').rglob('events.jsonl'))
    teams = {int(house['house']): int(house['team']) for house in houses}
    reasons, events = [], []
    limited = False
    if len(paths) != 1:
        reasons.append('missing_capture' if not paths else 'multiple_captures')
    for path in paths:
        started = ended = summaries = last_seq = 0
        end_cycle, session = None, None
        summary_houses, victims = set(), set()
        with path.open(encoding='utf-8', errors='replace') as handle:
            for line in handle:
                try:
                    event = json.loads(line)
                    if not isinstance(event, dict) or not isinstance(event.get('data'), dict):
                        raise ValueError('not a telemetry record')
                    seq, cycle = int(event['seq']), int(event['cycle'])
                    if seq != last_seq + 1:
                        reasons.append('sequence_gaps')
                    last_seq = seq
                    if session is None:
                        session = event['session']
                    if not session or event['session'] != session or event.get('schema_version') != 1:
                        reasons.append('invalid_session')
                    name, data = event['event'], event['data']
                    if name == 'session_start':
                        started += 1
                    elif name == 'session_end':
                        ended += 1
                        end_cycle = cycle
                    elif name == 'game_summary':
                        summaries += 1
                        summary_houses.update(int(house) for house in data['houses'])
                    elif name == 'capture_limit':
                        limited = True
                    elif name == 'simulation_exception':
                        reasons.append('simulation_exception')
                    elif name == 'lethal_damage' and int(data['item']) in ENEMY_AIR_FIELDS:
                        victim, attacker_house = int(event['house']), int(data['attacker_house'])
                        object_id, attacker = int(data['object']), int(data['attacker'])
                        if object_id in victims:
                            reasons.append('duplicate_air_death')
                            continue
                        victims.add(object_id)
                        enemy = ''
                        if victim not in teams or (attacker_house >= 0 and attacker_house not in teams):
                            reasons.append('unknown_combat_house')
                        elif attacker_house >= 0:
                            enemy = int(teams[victim] != teams[attacker_house])
                        events.append(dict(session=session, seq=seq, cycle=cycle, house=attacker_house,
                                           attacker=attacker, victim_house=victim, object=object_id,
                                           item=int(data['item']), enemy=enemy))
                except (ValueError, TypeError, KeyError):
                    reasons.append('malformed_records')
        if started != 1 or ended != 1 or summaries != 1:
            reasons.append('missing_session_boundary_or_summary')
        if end_cycle is None or end_cycle < final_cycle:
            reasons.append('short_capture')
        if set(teams) - summary_houses:
            reasons.append('incomplete_final_roster')
    if limited:
        reasons.append('capture_limit')
    complete = not reasons
    for event in events:
        event['capture_complete'] = int(complete)
    return dict(status='complete' if complete else 'capture_limit' if limited else
                'missing' if not paths else 'incomplete',
                reasons=','.join(dict.fromkeys(reasons)), events=events)


def add_air_kill_samples(samples, capture):
    cycles = defaultdict(list)
    for event in capture['events']:
        if event['enemy'] == 1:
            cycles[(event['house'], ENEMY_AIR_FIELDS[event['item']])].append(event['cycle'])
    for values in cycles.values():
        values.sort()
    complete = capture['status'] == 'complete'
    for sample in samples:
        sample['air_kills_complete'] = int(complete)
        for field in ENEMY_AIR_FIELDS.values():
            sample[field] = bisect_right(cycles[(int(sample['house']), field)], int(sample['cycle'])) if complete else ''


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
                                              f'city={args.city},spice={SPICE[spice]},sand=65,rock=30,mountain=5,'
                                              f'tech=9,credits=3000,seed={seed}'))
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
            'storage_full': (s.get('storage_overflow_cycles', 0) > old.get('storage_overflow_cycles', 0),
                             0, alive and s.get('ledger_enabled') == 1 and 'storage_overflow_cycles' in s),
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
           '--seed', str(game['seed']), '--minutes', str(game['minutes']),
           '--capture-mib', str(getattr(args, 'capture_mib', 256)),
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
    probe_sha256 = hashlib.sha256(Path(binary).read_bytes()).hexdigest()
    started = time.monotonic()
    process = subprocess.run(command(args, binary, game, out), cwd=root, capture_output=True, text=True,
                             env=dict(os.environ, BALANCE_DUNECITY_AUDIT='1'))
    log = out / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    result = next((parse(line, 'SCENARIO_RESULT:') for line in lines if 'SCENARIO_RESULT:' in line), {})
    final = [parse(line, 'SCENARIO_HOUSE:') for line in lines if 'SCENARIO_HOUSE:' in line]
    generated = next((parse(line, 'SCENARIO_MAP:') for line in lines if 'SCENARIO_MAP:' in line), {})
    timing = next((parse(line, 'PROBE_TIMING:') for line in lines if 'PROBE_TIMING:' in line), {})
    setup = next((parse(line, 'SCENARIO_SETUP:') for line in lines if 'SCENARIO_SETUP:' in line), {})
    configs = [parse(line, 'SCENARIO_AI_CONFIG:') for line in lines if 'SCENARIO_AI_CONFIG:' in line]
    summary_path = out / 'summary.json'
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {}
    digest = next((parse(line, 'PROBE_DIGEST:').get('digest', '') for line in lines
                   if 'PROBE_DIGEST:' in line and 'final=1' in line), '')
    merged = defaultdict(dict)
    for line in lines:
        for marker in ('ELIMINATION_SAMPLE:', 'DUNECITY_AUDIT:', 'DUNECITY_ECONOMY:', 'DUNECITY_TROOPERS:'):
            if marker in line:
                data = parse(line, marker)
                data.update(json.loads(data.pop('economy', '{}')))
                merged[(int(data['house']), int(data['cycle']))].update(data)
    samples = [dict(metadata(game), **data) for _, data in sorted(merged.items())]
    capture = air_kill_capture(out, final, max((int(sample['cycle']) for sample in samples), default=0),
                               args.attack_events)
    add_air_kill_samples(samples, capture)
    missing = any('power' not in sample for sample in samples)
    error = '' if process.returncode == 0 and result and samples and not missing else f'rc={process.returncode};incomplete={int(missing)}'
    row = dict(metadata(game), sides=','.join(f'{h}:{t}:{d}' for h, t, d in game['sides']),
               map_seed=generated.get('map_seed', ''), terrain_attempts=generated.get('attempts', ''),
               skipped=generated.get('skipped', ''), outcome=result.get('outcome', ''), winner_team=result.get('winner_team', ''),
               minutes=result.get('minutes', ''), loop_ms=timing.get('loop_ms', ''),
               wall_s=round(time.monotonic() - started, 3), digest=digest, attack_telemetry=int(args.attack_events), error=error)
    row.update({field: setup.get(field, '') for field in (
        'config_hash', 'options_hash', 'map_hash', 'content_hash', 'object_data_hash')})
    row.update(ai_config_hashes=','.join(f'{item["house"]}:{item["config_hash"]}' for item in configs),
               probe_sha256=probe_sha256, source_commit=summary.get('sourceCommit', ''),
               source_fingerprint=summary.get('sourceFingerprint', ''),
               source_fingerprint_error=summary.get('sourceFingerprintError', ''))
    row.update(air_kill_telemetry=capture['status'], air_kill_telemetry_reasons=capture['reasons'],
               capture_mib=getattr(args, 'capture_mib', 256))
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
            for event in summary['attacks']:
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
            last = max(groups[number], key=lambda row: int(row['cycle']))
            house_rows.append(dict(metadata(game), **{key: house[key] for key in (
                'house', 'team', 'alive', 'eliminated_s', 'kill_value', 'loss_value', 'structures',
                'army_value', 'army_units', 'sortie_minutes')}, **detectors,
                score=round(scores[number].score, 4), drawn=int(scores[number].drawn),
                won=int(house['team'] == result.get('winner_team')), difficulty=difficulty_of[number],
                attack_telemetry=int(args.attack_events),
                attack_count=len(attacks[number]) if args.attack_events else '',
                first_attack_minute=min(attacks[number]) if attacks[number] else '',
                air_kill_telemetry=capture['status'],
                **{key: last.get(key, '') for key in TROOPER_FIELDS + tuple(ENEMY_AIR_FIELDS.values())},
                **{key: last.get(key, '') if args.attack_events or key == 'storage_full_cycles' else ''
                   for key in ('storage_full_cycles', 'storage_overflow_cycles', 'storage_income_lost')},
                flags='/'.join(d for d in DETECTORS if detectors[d])))
        append(results / 'samples.csv', SAMPLE_FIELDS, samples)
        append(results / 'houses.csv', HOUSE_FIELDS, house_rows)
        if args.attack_events:
            append(results / 'waves.csv', ATTACK_FIELDS, attack_rows)
            append(results / 'air-kills.csv', AIR_KILL_FIELDS,
                   [dict(metadata(game), **event) for event in capture['events']])
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
                    '--minutes', '1', '--capture-mib', str(getattr(args, 'capture_mib', 256)), '--no-ai-telemetry'],
                   cwd=root, check=True, capture_output=True, text=True)
    shutil.rmtree(out / 'profile', ignore_errors=True)
    return probe_binary(out)


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    for name, fields in (('games.csv', GAME_FIELDS), ('houses.csv', HOUSE_FIELDS),
                         ('samples.csv', SAMPLE_FIELDS), ('waves.csv', ATTACK_FIELDS),
                         ('air-kills.csv', AIR_KILL_FIELDS)):
        require_schema(results / name, fields)
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
            hits = sum(int(row.get(name, 0)) for row in rows)
            exposed = sum(int(row.get('eligible_' + name, 0)) for row in rows)
            cells.append(f'{hits}/{exposed}')
        print(f'| {difficulty} | {spice} | {len(rows)} | ' + ' | '.join(cells) + ' |')
    for name in DETECTORS:
        examples = [f'{row["id"]}:h{row["house"]}@{row["first_" + name]}'
                    for row in houses if row.get(name) == '1']
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
    storage_report(directories)
    trooper_report(directories)


def checkpoint_samples(directories):
    samples = defaultdict(dict)
    for sample in load(directories, 'samples.csv', ('id', 'house', 'cycle')):
        cycle = int(sample['cycle'])
        if cycle in (10 * CYCLES_PER_MINUTE, 20 * CYCLES_PER_MINUTE):
            samples[(sample['id'], sample['house'])][cycle] = sample
    return samples


def checkpoint_value(house, samples, minute, field):
    point = samples[(house['id'], house['house'])].get(minute * CYCLES_PER_MINUTE)
    if point:
        return int(point[field])
    if house['alive'] == '0' and float(house['eliminated_s']) <= minute * 60:
        return 0
    return None  # A surviving early winner was not observed at this checkpoint.


def combat_report(directories, allowed=None, label='Combat'):
    """Exact ground dispatches when enabled; checkpoints include defeated houses as zero."""
    games = {r['id']: r for r in load(directories, 'games.csv', ('id',)) if not r['error']}
    houses = [r for r in load(directories, 'houses.csv', ('id', 'house'))
              if r['id'] in games and (allowed is None or (r['id'], r['house']) in allowed)]
    samples = checkpoint_samples(directories)
    groups = defaultdict(list)
    for house in houses:
        groups[(house['difficulty'], house['spice_level'])].append(house)
    groups[('all', 'all')] = houses
    print(f'{label}: first attack is a nonempty ground_hunt dispatch, not a readiness check or sortie duration.')
    print('| Difficulty | Spice | Attacking/measured | First attack mean min (attackers) | Attacks/10 alive-min | Army10 mean (n) | Army20 mean (n) | Rocket10 mean (n) | Rocket20 mean (n) | Kill-value mean |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    for (difficulty, spice), rows in sorted(groups.items()):
        measured = [r for r in rows if r.get('attack_telemetry') == '1']
        first = [float(r['first_attack_minute']) for r in measured if r.get('first_attack_minute', '') != '']
        minutes = sum(min(float(games[r['id']]['minutes']), float(r['eliminated_s']) / 60)
                      if r['alive'] == '0' else float(games[r['id']]['minutes']) for r in measured)
        frequency = sum(int(r['attack_count']) for r in measured) * 10 / minutes if minutes else None
        checkpoints = []
        for field in ('army_value', 'rocket'):
            for minute in (10, 20):
                values = []
                for house in rows:
                    value = checkpoint_value(house, samples, minute, field)
                    if value is not None:
                        values.append(value)
                checkpoints.append(f'{statistics.mean(values):.1f} ({len(values)})' if values else 'unmeasured')
        first_mean = f'{statistics.mean(first):.2f}' if first else 'unmeasured'
        rate = f'{frequency:.3f}' if frequency is not None else 'unmeasured'
        kills = f'{statistics.mean(int(r["kill_value"]) for r in rows):.1f}' if rows else 'unmeasured'
        print(f'| {difficulty} | {spice} | {len(first)}/{len(measured)} | {first_mean} | {rate} | '
              + ' | '.join(checkpoints) + f' | {kills} |')

def storage_report(directories, allowed=None, label='Storage'):
    rows = [row for row in load(directories, 'houses.csv', ('id', 'house'))
            if (allowed is None or (row['id'], row['house']) in allowed)
            and row.get('storage_overflow_cycles', '') != '']
    if not rows:
        print(f'{label}: full/overflow cycle accounting unmeasured')
        return
    print(f'{label}: {len(rows)} measured houses; '
          f'full-storage {sum(int(row["storage_full_cycles"]) for row in rows) / CYCLES_PER_MINUTE:.3f} house-min; '
          f'income-overflow {sum(int(row["storage_overflow_cycles"]) for row in rows) / CYCLES_PER_MINUTE:.3f} house-min; '
          f'{sum(int(row["storage_income_lost"]) for row in rows)} credits lost '
          '(capacity-loss ticks excluded)')


def trooper_report(directories):
    completed = {row['id'] for row in load(directories, 'games.csv', ('id',)) if not row['error']}
    groups = defaultdict(list)
    for row in load(directories, 'houses.csv', ('id', 'house')):
        if row['id'] in completed:
            groups[(row['difficulty'], row['spice_level'])].append(row)
    print('Troopers/air: final live counts and cumulative engine ledgers; means show measured house counts. '
          'Trooper built is engine-completed/delivered singles after squad expansion, not strictly WOR-trained; '
          'queued counts single-equivalents.')
    tables = (
        (('wor', 'WOR live'), ('wor_built', 'WOR built'), ('trooper_live', 'Trooper live'),
         ('trooper_queued', 'Trooper queued'), ('trooper_built', 'Trooper built')),
        (('ornithopter_lost', 'Own Ornithopter losses'), ('elite_ornithopter_lost', 'Own elite losses'),
         ('enemy_ornithopter_kills', 'Enemy Ornithopter kills'), ('enemy_elite_ornithopter_kills', 'Enemy elite kills')),
    )
    for table in tables:
        print()
        print('| Difficulty | Spice | ' + ' | '.join(label for _, label in table) + ' |')
        print('|---|---|' + '---|' * len(table))
        for (difficulty, spice), rows in sorted(groups.items()):
            cells = []
            for field, _ in table:
                values = [int(row[field]) for row in rows if row.get(field, '') != ''
                          and (field not in ENEMY_AIR_FIELDS.values() or row.get('air_kill_telemetry') == 'complete')]
                cells.append(f'{statistics.mean(values):.2f} ({len(values)}/{len(rows)})' if values else 'unmeasured')
            print(f'| {difficulty} | {spice} | ' + ' | '.join(cells) + ' |')
    print('Enemy kills require complete lethal_damage callbacks and opposing teams; raw *_killed house ledgers '
          'in CSVs include friendly splash and are not the enemy-kill metric. Missing history stays unmeasured.')


def trooper_pair_keys(previous, current, old, new):
    """The additional acceptance tables require actual same-build OFF/ON pairs."""
    matched, rejected = [], defaultdict(int)
    fingerprints = ('probe_sha256', 'source_commit', 'source_fingerprint', 'map_hash',
                    'options_hash', 'content_hash', 'object_data_hash')
    settings = tuple(field for field in META if field not in ('id', 'variant', 'style')) + ('sides',)
    for key in sorted(old.keys() & new.keys()):
        before, after = previous.get(key[0]), current.get(key[0])
        reason = ''
        if not before or not after or before.get('error') or after.get('error'):
            reason = 'incomplete_game'
        elif before.get('style') != 'trooper-support-off' or after.get('style') != 'trooper-support-on':
            reason = 'not_trooper_support_OFF_ON'
        elif any(before.get(field, '') == '' or after.get(field, '') == '' for field in fingerprints):
            reason = 'missing_provenance'
        elif any(before[field] != after[field] for field in fingerprints):
            reason = 'different_build_map_or_options'
        elif before.get('source_fingerprint_error') or after.get('source_fingerprint_error'):
            reason = 'source_fingerprint_error'
        elif any(before.get(field, '') != after.get(field, '') for field in settings):
            reason = 'different_game_settings'
        elif any(old[key].get(field, '') != new[key].get(field, '') for field in ('team', 'difficulty')):
            reason = 'different_house_settings'
        if reason:
            rejected[reason] += 1
        else:
            matched.append(key)
    return matched, dict(rejected)


def paired_metric(keys, old, new, field, direction=None, telemetry=False):
    values = []
    for key in keys:
        rows = (old[key], new[key])
        if any(row.get(field, '') == '' for row in rows):
            continue
        if (telemetry or field in ENEMY_AIR_FIELDS.values()) and any(
                row.get('air_kill_telemetry') != 'complete' for row in rows):
            continue
        if telemetry and any(row.get('attack_telemetry') != '1' for row in rows):
            continue
        values.append(tuple(float(row[field]) for row in rows))
    if not values:
        return 'unmeasured', 'unmeasured'
    before, after = (statistics.mean(pair[side] for pair in values) for side in (0, 1))
    state = 'unmeasured'
    if direction is not None and len(values) == len(keys):
        state = 'not worse' if direction * (after - before) >= 0 else 'worse'
    cell = f'{before:.2f} -> {after:.2f} ({after-before:+.2f}; {len(values)}/{len(keys)} pairs)'
    return cell + (f'; {state}' if direction is not None else ''), state


def paired_detector(keys, old, new, detector):
    fields = (detector, 'eligible_' + detector)
    if not keys or any(row.get(field, '') == '' for key in keys
                       for row in (old[key], new[key]) for field in fields):
        return 'unmeasured', 'unmeasured'
    counts = [tuple(sum(int(source[key][field]) for key in keys) for field in fields) for source in (old, new)]
    state = 'unmeasured'
    if all(exposed for _, exposed in counts):
        state = 'not worse' if counts[1][0] * counts[0][1] <= counts[0][0] * counts[1][1] else 'worse'
    return f'{counts[0][0]}/{counts[0][1]} -> {counts[1][0]}/{counts[1][1]}; {state}', state


def trooper_comparison(control, candidate):
    games = [{row['id']: row for row in load([directory], 'games.csv', ('id',))}
             for directory in (control, candidate)]
    houses = [{(row['id'], row['house']): row for row in load([directory], 'houses.csv', ('id', 'house'))}
              for directory in (control, candidate)]
    keys, rejected = trooper_pair_keys(*games, *houses)
    print(f'Trooper support same-build OFF -> ON: {len(keys)} verified house pairs; '
          f'excluded {sum(rejected.values())} ({json.dumps(rejected, sort_keys=True)}). '
          'Probe/source/map/options/content/object-data fingerprints must match; AI config hashes may differ.')
    groups = defaultdict(list, {(level, 'all'): [] for level in ('hard', 'brutal')})
    for key in keys:
        house = houses[0][key]
        if house['difficulty'] in ('hard', 'brutal'):
            groups[(house['difficulty'], house['spice_level'])].append(key)
            groups[(house['difficulty'], 'all')].append(key)
    print('Guardrails: detector hit/exposed rates must not increase; dispatch and kill-value means must not decrease. '
          'Dispatches remain nonempty ground_hunt events and require a complete callback capture here. '
          'Unmeasured is not a passing gate; paired outcome scores above are a separate acceptance input.')
    print()
    print('| Difficulty | Spice | Pairs | economy_stall | bankrupt_queue_empty | missing_starport | Dispatches mean | Kill-value mean | Guardrails |')
    print('|---|---|---|---|---|---|---|---|---|')
    for (difficulty, spice), paired in sorted(groups.items()):
        cells = [paired_detector(paired, *houses, detector) for detector in (
            'economy_stall', 'bankrupt_queue_empty', 'missing_starport')]
        cells += [paired_metric(paired, *houses, 'attack_count', direction=1, telemetry=True),
                  paired_metric(paired, *houses, 'kill_value', direction=1)]
        states = [state for _, state in cells]
        verdict = 'worse' if 'worse' in states else 'unmeasured' if 'unmeasured' in states else 'not worse'
        print(f'| {difficulty} | {spice} | {len(paired)} | ' + ' | '.join(cell for cell, _ in cells) + f' | {verdict} |')
    tables = (
        (('wor', 'WOR live'), ('wor_built', 'WOR built'), ('trooper_live', 'Trooper live'),
         ('trooper_queued', 'Trooper queued'), ('trooper_built', 'Trooper built')),
        (('ornithopter_lost', 'Own Ornithopter losses'), ('elite_ornithopter_lost', 'Own elite losses'),
         ('enemy_ornithopter_kills', 'Enemy Ornithopter kills'), ('enemy_elite_ornithopter_kills', 'Enemy elite kills')),
    )
    print('Support and AA: paired final-house means OFF -> ON (delta; measured/verified pairs). '
          'Own losses use House original-owner loss counters; enemy kills use opposing-team lethal callbacks, '
          'never enemy total losses. Live counts are observed at each game end, not equal-time checkpoints.')
    print('Trooper built is the engine completed/delivered single counter, not a WOR-only training counter.')
    for table in tables:
        print()
        print('| Difficulty | Spice | ' + ' | '.join(label for _, label in table) + ' |')
        print('|---|---|' + '---|' * len(table))
        for (difficulty, spice), paired in sorted(groups.items()):
            cells = [paired_metric(paired, *houses, field)[0] for field, _ in table]
            print(f'| {difficulty} | {spice} | ' + ' | '.join(cells) + ' |')




def rocket_comparison(control, candidate, allowed=None):
    valid = [{r['id'] for r in load([directory], 'games.csv', ('id',)) if not r['error']}
             for directory in (control, candidate)]
    houses = [{(r['id'], r['house']): r for r in load([directory], 'houses.csv', ('id', 'house'))
               if r['id'] in games} for directory, games in zip((control, candidate), valid)]
    samples = [checkpoint_samples([directory]) for directory in (control, candidate)]
    shared = sorted(houses[0].keys() & houses[1].keys())
    if allowed is not None:
        shared = [key for key in shared if key in allowed]
    groups = defaultdict(list)
    for key in shared:
        house = houses[0][key]
        groups[(house['difficulty'], house['spice_level'])].append(key)
    groups[('all', 'all')] = shared
    print('Rocket checkpoints: eliminated houses count as zero; surviving early winners are censored. '
          'Joint-alive pairs require actual alive samples in both arms.')
    print('| Difficulty | Spice | Minute | Observable control -> candidate (delta; pairs) | Joint-alive control -> candidate (delta; pairs) | Censored control/candidate |')
    print('|---|---|---|---|---|---|')

    def means(pairs):
        if not pairs:
            return 'unmeasured'
        old = statistics.mean(pair[0] for pair in pairs)
        new = statistics.mean(pair[1] for pair in pairs)
        return f'{old:.2f} -> {new:.2f} ({new-old:+.2f}; {len(pairs)})'

    for (difficulty, spice), keys in sorted(groups.items()):
        for minute in (10, 20):
            observed, alive = [], []
            censored = [0, 0]
            for key in keys:
                values = [checkpoint_value(houses[side][key], samples[side], minute, 'rocket')
                          for side in (0, 1)]
                for side in (0, 1):
                    censored[side] += values[side] is None
                if any(value is None for value in values):
                    continue
                observed.append(values)
                points = [samples[side][key].get(minute * CYCLES_PER_MINUTE) for side in (0, 1)]
                if all(point and point['alive'] == '1' for point in points):
                    alive.append(values)
            print(f'| {difficulty} | {spice} | {minute} | {means(observed)} | {means(alive)} | '
                  f'{censored[0]}/{censored[1]} |')


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
            rates.append(f'{sum(int(source[key].get(name, 0)) for key in common)}/'
                         f'{sum(int(source[key].get("eligible_" + name, 0)) for key in common)}')
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
    storage_report([control], set(common), 'Control storage')
    storage_report([candidate], set(common), 'Candidate storage')
    rocket_comparison(control, candidate, set(common))
    trooper_comparison(control, candidate)


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', type=Path)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--prebuilt-probe', type=Path, help='Use a frozen probe binary without building or leasing the checkout')
parser.add_argument('--scratch')
parser.add_argument('--jobs', type=int, default=2)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--capture-mib', type=int, default=256,
                    help='Ordinary diagnostic telemetry allowance (256-4096 MiB); shipped log cap is unchanged')
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
if not 256 <= args.capture_mib <= 4096:
    parser.error('Use 256-4096 MiB of capture space')
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
