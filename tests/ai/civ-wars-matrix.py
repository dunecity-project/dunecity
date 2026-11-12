#!/usr/bin/env python3
"""Matched real-engine Civilization Wars games, submitted one game per cluster unit.

The default matrix uses five fixed seeds, 1v1/2v2/3v3, five difficulty pairs and
strategy off/on/left/right. --both-sides exchanges controllers and their strategy
settings between the named teams; logical --arms left/right follow controllers,
not a fixed map edge. Their actual runner --civ-strategy is recorded separately.

Raw CSVs retain probe observations and telemetry before leased scratch is cleaned.
An empty exact telemetry count means unavailable, never zero. Partial captures
retain explicitly named observed lower bounds. Time limits are never adjudicated
on material. Reports use match_score team placements and bootstrap paired seed
means, not individual houses or the two orientations as independent trials.
Worm item ledgers separate all four harvester families from combat and support.
Tech timing -1 means observed unreached, not a timing; absent data stays unknown.
Source/map/options-matched comparisons require a measured content fingerprint or
a known clean commit; legacy unidentified dirty snapshots remain descriptive.
"""
import argparse
import csv
import hashlib
import json
import platform
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time
import uuid
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
from probe_profile import finish_game, probe_binary, require_free_space, scratch_dir  # noqa: E402
import match_score  # noqa: E402

SEEDS = (7101, 7202, 7303, 7404, 7505)
PAIRS = ('medium:medium', 'hard:hard', 'brutal:brutal', 'hard:medium', 'brutal:hard')
ARMS = ('off', 'on', 'left', 'right')
# ItemID_enum in include/data.h; HarvesterHelpers.h includes all four refinery families.
HARVESTER_ITEMS = frozenset((31, 59, 85, 86))
COMBAT_ITEMS = frozenset((28, 29, 32, 33, 35, 36, 37, 39, 40, 41, 42, 43, 44, 46, 47,
                          52, 53, 54, 55, 63, 64, 74, 75, 77, 78, 79, 80, 81, 82, 83))
LOSS_KINDS = ('harvester', 'combat', 'other')
DIFFICULTIES = ('medium', 'hard', 'brutal')
COMMON = ('id', 'attempt', 'pair_id', 'seed', 'format', 'team_size', 'pair', 'left', 'right',
          'swap', 'arm', 'strategy', 'size', 'worms', 'spice', 'minutes', 'options_input_sha256')
COUNTERS = ('cycle', 'minute', 'house', 'team', 'alive', 'credits', 'tech', 'research_target', 'research_remaining',
            'harvesters', 'refineries', 'islands', 'city_pop', 'army_value', 'army_units', 'produced', 'lost',
            'waves', 'reached', 'waves_source', 'waves_complete', 'reached_source')
TECH_FIELDS = tuple(f'tech{level}_s' for level in range(2, 10))
GAME_FIELDS = COMMON + (
    'outcome', 'winner_team', 'end_s', 'focal_team', 'left_score', 'right_score', 'left_drawn', 'right_drawn',
    'config_hash', 'options_hash', 'map_hash', 'global_strategy', 'strategy_override', 'observing',
    'digest', 'digest_cycle', 'source_commit', 'working_tree_modified', 'host', 'platform',
    'source_fingerprint', 'source_fingerprint_error',
    'telemetry_status', 'telemetry_reasons', 'telemetry_files', 'telemetry_events', 'capture_limit',
    'telemetry_parse_errors', 'telemetry_seq_gaps', 'worm_ids_complete', 'worm_count',
    'worm_losses_complete', 'worm_losses', 'observed_worm_losses', 'attacks', 'observed_attacks', 'performance_complete',
    'ai_wall_ms', 'placement_wall_ms', 'loop_thread_cpu_ms', 'loop_wall_ms', 'cycles', 'cycles_per_s',
    'ai_update_thread_cpu_ms', 'ai_update_wall_ms', 'ai_update_calls', 'wall_s', 'returncode', 'error',
    'setup_json', 'result_json', 'digest_json', 'metadata_json', 'roster_json', 'override_json',
    'campaign_result', 'command_json')
HOUSE_FIELDS = COMMON + COUNTERS + ('eliminated_s',) + TECH_FIELDS + (
    'place', 'score', 'drawn', 'worm_losses', 'observed_worm_losses', 'attacks', 'observed_attacks',
    'telemetry_status', 'player_class', 'bot_strategy', 'initial_built', 'initial_lost', 'roster_json', 'data_json')
SAMPLE_FIELDS = COMMON + COUNTERS + ('data_json',)
RESEARCH_FIELDS = COMMON + ('cycle', 'house', 'team', 'event', 'tech', 'target', 'remaining', 'lab', 'data_json')
ITEM_FIELDS = COMMON + ('source', 'event', 'cycle', 'house', 'team', 'item', 'actual', 'built', 'produced',
                        'lost', 'object', 'attacker', 'observed_count', 'complete', 'data_json')
WAVE_FIELDS = COMMON + ('source', 'event', 'session', 'seq', 'cycle', 'house', 'team', 'launch_cycle',
                        'members', 'value', 'waves', 'complete', 'data_json')
PERF_FIELDS = COMMON + ('source', 'session', 'seq', 'cycle', 'house', 'team', 'item', 'scope', 'unit',
                        'count', 'sum', 'max', 'max_cycle', 'over_33ms', 'over_100ms', 'over_250ms',
                        'start_cycle', 'end_cycle', 'start_wall_us', 'end_wall_us', 'elapsed_us',
                        'dropped_samples', 'complete', 'loop_thread_cpu_ms', 'loop_wall_ms', 'cycles',
                        'cycles_per_s', 'ai_wall_ms', 'ai_update_thread_cpu_ms', 'ai_update_wall_ms',
                        'ai_update_calls', 'update_thread_cpu_ms', 'update_wall_ms', 'update_calls', 'data_json')
WORM_FIELDS = COMMON + ('cycle', 'id_object', 'object_id', 'item', 'house', 'data_json')
DECISION_FIELDS = COMMON + ('session', 'seq', 'cycle', 'house', 'team', 'event', 'complete', 'data_json')
CSV_FILES = (('houses.csv', HOUSE_FIELDS), ('samples.csv', SAMPLE_FIELDS), ('research.csv', RESEARCH_FIELDS),
             ('items.csv', ITEM_FIELDS), ('waves.csv', WAVE_FIELDS), ('perf.csv', PERF_FIELDS),
             ('worms.csv', WORM_FIELDS), ('decisions.csv', DECISION_FIELDS), ('games.csv', GAME_FIELDS))
RECORD = re.compile(r'\b(CIV_WARS_[A-Z_]+|PROBE_DIGEST|CAMPAIGN_BALANCE_RESULT):?\s+(.*)')
lock = threading.Lock()


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True)


def number(value):
    try:
        result = float(value)
        return result if float('-inf') < result < float('inf') else None
    except (TypeError, ValueError):
        return None


def integer(value):
    value = number(value)
    return int(value) if value is not None and value.is_integer() else None


def positive(value):
    value = int(value)
    if value < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return value


def seed_value(value):
    value = int(value)
    if not 0 <= value <= 0xffffffff:
        raise argparse.ArgumentTypeError('must be a 32-bit unsigned seed')
    return value


def difficulty_pair(value):
    parts = value.lower().split(':')
    if len(parts) != 2 or any(part not in DIFFICULTIES for part in parts):
        raise argparse.ArgumentTypeError('use DIFFICULTY:DIFFICULTY (medium, hard or brutal)')
    return ':'.join(parts)


def options_digest(args):
    path = getattr(args, 'game_options', None)
    return hashlib.sha256(Path(path).read_bytes()).hexdigest() if path else 'none'


def schedule(args):
    """JSON-serializable games; IDs depend on input bytes, never a transported INI's path."""
    games = []
    options = options_digest(args)
    for team_size in dict.fromkeys(args.team_sizes):
        for pair in dict.fromkeys(args.pairs):
            first, second = pair.split(':')
            for seed in dict.fromkeys(args.seeds):
                for swap in range(2 if args.both_sides else 1):
                    left, right = (second, first) if swap else (first, second)
                    pair_id = (f'civ-wars/{team_size}v{team_size}/{pair}/size{args.size}'
                               f'/worms{args.worms}/spice{args.spice}/min{args.minutes}'
                               f'/options-{options}/seed{seed}/swap{swap}')
                    for arm in dict.fromkeys(args.arms):
                        strategy = {'left': 'right', 'right': 'left'}.get(arm, arm) if swap else arm
                        focal = (2 if swap else 1) if arm != 'right' else (1 if swap else 2)
                        games.append(dict(id=f'{pair_id}/arm-{arm}/strategy-{strategy}', pair_id=pair_id,
                                          seed=seed, format=f'{team_size}v{team_size}', team_size=team_size,
                                          pair=pair, left=left, right=right, swap=swap, arm=arm,
                                          strategy=strategy, focal_team=focal, size=args.size, worms=args.worms,
                                          spice=args.spice, minutes=args.minutes, options_input_sha256=options))
    return games


def read(path):
    path = Path(path)
    if not path.is_file():
        return []
    with path.open(newline='', encoding='utf-8') as handle:
        return list(csv.DictReader(handle))


def append(path, fields, rows):
    """Keep old and new columns aligned, preserving unknown legacy columns on migration."""
    path = Path(path)
    new = not path.exists()
    if not new:
        with path.open(newline='', encoding='utf-8') as handle:
            header = next(csv.reader(handle), [])
        fields = tuple(dict.fromkeys((*fields, *header)))
        if tuple(header) != tuple(fields):
            old = read(path)
            temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
            with temporary.open('w', newline='', encoding='utf-8') as handle:
                writer = csv.DictWriter(handle, fields, extrasaction='ignore')
                writer.writeheader()
                writer.writerows(old)
            temporary.replace(path)
    with path.open('a', newline='', encoding='utf-8') as handle:
        writer = csv.DictWriter(handle, fields, extrasaction='ignore')
        if new:
            writer.writeheader()
        writer.writerows(rows)


def records(lines):
    found = defaultdict(list)
    for line in lines:
        match = RECORD.search(line)
        if match:
            data = dict(part.split('=', 1) for part in match[2].split() if '=' in part)
            found[match[1]].append(data)
    return found


def raw_row(base, data, **extra):
    # Probe fields cannot replace the scheduled game's identity (WORM itself has an id).
    return {**{key: value for key, value in data.items() if key not in COMMON},
            **base, **extra, 'data_json': encoded(data)}


def telemetry(out, final_cycle, houses, worm_ids):
    """Stream the capture once before finish_game discards it; retain exactness separately."""
    paths = sorted((out / 'profile').rglob('events.jsonl'))
    counts = dict(events=0, parse_errors=0, seq_gaps=0, capture_limit=0, dropped_samples=0)
    attacks, losses = Counter(), Counter()
    waves, items, perf, metadata = [], [], [], []
    decisions = []
    reasons = []
    summary_houses = set()
    teams = {str(h['house']): str(h['team']) for h in houses if h.get('house') and h.get('team')}
    if len(paths) != 1:
        reasons.append('missing_capture' if not paths else 'multiple_captures')
    for path in paths:
        started, ended, summaries, last_seq, end_cycle = 0, 0, 0, 0, None
        seen_losses = set()
        with path.open(encoding='utf-8', errors='replace') as handle:
            for line in handle:
                try:
                    event = json.loads(line)
                    if not isinstance(event, dict) or not isinstance(event.get('data'), dict):
                        raise ValueError('not a telemetry record')
                except (ValueError, TypeError):
                    counts['parse_errors'] += 1
                    continue
                counts['events'] += 1
                seq = integer(event.get('seq'))
                if seq != last_seq + 1:
                    counts['seq_gaps'] += 1
                if seq is not None:
                    last_seq = seq
                name, data = event.get('event'), event['data']
                house = str(event.get('house', ''))
                common = dict(cycle=event.get('cycle', ''), house=house, team=teams.get(house, ''),
                              session=event.get('session', ''), seq=event.get('seq', ''))
                if name == 'session_start':
                    started += 1
                    metadata.append(data)
                elif name == 'session_end':
                    ended += 1
                    end_cycle = integer(event.get('cycle'))
                elif name == 'capture_limit':
                    counts['capture_limit'] += 1
                elif name == 'simulation_exception':
                    reasons.append('simulation_exception')
                elif name == 'ground_hunt' and (number(data.get('members')) or 0) > 0:
                    attacks[house] += 1
                    waves.append({**common, **data, 'source': 'telemetry', 'event': name,
                                  'data_json': encoded(data)})
                elif name in ('attack_deferred', 'state_snapshot', 'research_started',
                              'rock_expansion_site', 'mcv_deployment', 'colonist_escort'):
                    decisions.append({**common, 'event': name, 'data_json': encoded(data)})
                elif name == 'lethal_damage' and str(data.get('attacker')) in worm_ids:
                    victim = (house, str(data.get('object')))
                    if victim in seen_losses:
                        continue
                    seen_losses.add(victim)
                    losses[house] += 1
                    items.append({**common, **data, 'source': 'telemetry', 'event': 'worm_loss',
                                  'observed_count': 1, 'data_json': encoded(data)})
                elif name == 'performance_window':
                    dropped = integer(data.get('dropped_samples'))
                    counts['dropped_samples'] += dropped if dropped is not None else 1
                    metrics = data.get('metrics', {})
                    if not isinstance(metrics, dict) or any(not isinstance(m, dict) for m in metrics.values()):
                        counts['parse_errors'] += 1
                        continue
                    window = {key: data.get(key, '') for key in (
                        'start_cycle', 'end_cycle', 'start_wall_us', 'end_wall_us', 'elapsed_us', 'dropped_samples')}
                    for metric in metrics.values():
                        mh = str(metric.get('house', ''))
                        perf.append({**common, **window, **metric, 'team': teams.get(mh, ''),
                                     'source': 'telemetry', 'data_json': encoded(metric)})
                elif name == 'game_summary':
                    summaries += 1
                    roster = data.get('houses', {})
                    if not isinstance(roster, dict):
                        counts['parse_errors'] += 1
                        continue
                    for h, details in roster.items():
                        if not isinstance(details, dict):
                            counts['parse_errors'] += 1
                            continue
                        summary_houses.add(str(h))
                        ledgers = {key: details.get(key, {}) for key in ('actual', 'built', 'lost')}
                        if any(not isinstance(ledger, dict) for ledger in ledgers.values()):
                            counts['parse_errors'] += 1
                            continue
                        for item in sorted(set().union(*(set(ledger) for ledger in ledgers.values()))):
                            items.append(dict(source='game_summary', event='final_counts', cycle=common['cycle'],
                                              house=h, team=details.get('team', teams.get(str(h), '')), item=item,
                                              actual=ledgers['actual'].get(item, 0), built=ledgers['built'].get(item, 0),
                                              lost=ledgers['lost'].get(item, 0), complete=1,
                                              data_json=encoded({key: ledger.get(item, 0)
                                                                 for key, ledger in ledgers.items()})))
        if started != 1 or ended != 1 or summaries != 1:
            reasons.append('missing_session_boundary_or_summary')
        if final_cycle is None or end_cycle is None or end_cycle < final_cycle:
            reasons.append('short_capture')
    if counts['capture_limit']:
        reasons.append('capture_limit')
    if counts['parse_errors']:
        reasons.append('malformed_records')
    if counts['seq_gaps']:
        reasons.append('sequence_gaps')
    if paths and set(teams) - summary_houses:
        reasons.append('incomplete_final_roster')
    complete = not reasons
    for wave in waves:
        wave['complete'] = int(complete)
    for item in items:
        if item['event'] == 'worm_loss':
            item['complete'] = int(complete)
    performance_complete = complete and counts['dropped_samples'] == 0
    for row in perf:
        row['complete'] = int(performance_complete)
    for row in decisions:
        row['complete'] = int(complete)
    return dict(status='complete' if complete else 'capture_limit' if counts['capture_limit'] else
                'missing' if not paths else 'incomplete', reasons=','.join(dict.fromkeys(reasons)),
                complete=complete, performance_complete=performance_complete, paths=len(paths),
                counts=counts, attacks=attacks, losses=losses, waves=waves, items=items, perf=perf,
                metadata=metadata, decisions=decisions)


def command(args, binary, game, out):
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--mod', 'CivWars', '--local-spectator',
           '--civ-wars', str(game['team_size']), '--civ-size', str(game['size']), '--civ-worms', str(game['worms']),
           '--civ-spice', str(game['spice']), '--civ-strategy', game['strategy'],
           '--partner-difficulty', game['left'], '--enemy-difficulty', game['right'], '--seed', str(game['seed']),
           '--minutes', str(game['minutes']), '--capture-mib', '1024', '--wall-timeout', str(args.wall_timeout),
           '--path-workers', '1']
    if getattr(args, 'game_options', None):
        cmd += ['--game-options', str(Path(args.game_options).resolve())]
    return cmd


def score_houses(houses):
    ids = [str(h['house']) for h in houses]
    eliminated = {str(h['house']): float(h['eliminated_s']) for h in houses if str(h['alive']) == '0'}
    teams = {str(h['house']): str(h['team']) for h in houses}
    return match_score.score_game(ids, eliminated, teams=teams)


def play(args, scratch, binary, game):
    scratch = Path(scratch)
    short = hashlib.sha256(game['id'].encode()).hexdigest()[:20]
    # UUID ties raw rows to their completion marker, including retries after a partial CSV write.
    base = {key: game.get(key, '') for key in COMMON}
    base['attempt'] = uuid.uuid4().hex
    out = scratch / ('g' + short[:12] + base['attempt'][:8])
    failed = True
    started = time.monotonic()
    try:
        cmd = command(args, binary, game, out)
        process = subprocess.run(cmd, cwd=root, capture_output=True, text=True, errors='replace')
        wall = time.monotonic() - started
        log = out / 'run.log'
        lines = log.read_text(errors='replace').splitlines() if log.is_file() else process.stdout.splitlines()
        found = records(lines)
        setup = next(iter(found['CIV_WARS_SETUP']), {})
        result = next(iter(found['CIV_WARS_RESULT']), {})
        houses = found['CIV_WARS_HOUSE']
        roster = {data['house']: data for data in found['CIV_WARS_ROSTER'] if 'house' in data}
        digest = next((d for d in reversed(found['PROBE_DIGEST']) if d.get('final') == '1'), {})
        perf = next(iter(found['CIV_WARS_PERF']), {})
        worms = found['CIV_WARS_WORM']
        worm_ids = {w.get('object_id', w.get('id')) for w in worms} - {None, ''}
        errors = []
        if process.returncode:
            errors.append(f'rc={process.returncode}')
        for marker in ('CIV_WARS_SETUP', 'CIV_WARS_RESULT', 'CIV_WARS_PERF', 'CAMPAIGN_BALANCE_RESULT'):
            if len(found[marker]) != 1:
                errors.append(f'{marker}_count={len(found[marker])}')
        if not digest:
            errors.append('missing_final_digest')
        valid_houses = (len(houses) == game['team_size'] * 2
                        and len({h.get('house') for h in houses}) == len(houses)
                        and Counter(h.get('team') for h in houses) == {'1': game['team_size'], '2': game['team_size']}
                        and all(h.get('house') and h.get('alive') in ('0', '1') and
                                (h['alive'] == '1' or (number(h.get('eliminated_s')) is not None
                                                     and float(h['eliminated_s']) >= 0)) for h in houses))
        if not valid_houses:
            errors.append('incomplete_house_results')
        if result.get('outcome') not in ('won', 'time_limit', 'extinct') or number(result.get('end_s')) is None:
            errors.append('missing_match_outcome')
        capture = telemetry(out, integer(digest.get('cycle')), houses, worm_ids)
        worm_complete = (capture['complete'] and result.get('worm_ids_complete') == '1'
                         and integer(result.get('worm_count')) == len(worm_ids))
        places = score_houses(houses) if valid_houses else {}
        row = {**base, 'outcome': result.get('outcome', ''), 'winner_team': result.get('winner_team', ''),
               'end_s': result.get('end_s', ''), 'focal_team': game['focal_team'],
               **{key: setup.get(key, '') for key in ('config_hash', 'options_hash', 'map_hash', 'global_strategy',
                                                     'strategy_override', 'observing')},
               'digest': digest.get('digest', ''), 'digest_cycle': digest.get('cycle', ''),
               'host': platform.node(), 'platform': sys.platform,
               'telemetry_status': capture['status'], 'telemetry_reasons': capture['reasons'],
               'telemetry_files': capture['paths'], 'telemetry_events': capture['counts']['events'],
               'capture_limit': capture['counts']['capture_limit'],
               'telemetry_parse_errors': capture['counts']['parse_errors'],
               'telemetry_seq_gaps': capture['counts']['seq_gaps'],
               'worm_ids_complete': result.get('worm_ids_complete', ''), 'worm_count': result.get('worm_count', ''),
               'worm_losses_complete': int(worm_complete),
               'worm_losses': sum(capture['losses'].values()) if worm_complete else '',
               'observed_worm_losses': sum(capture['losses'].values()),
               'attacks': sum(capture['attacks'].values()) if capture['complete'] else '',
               'observed_attacks': sum(capture['attacks'].values()),
               'performance_complete': int(capture['performance_complete']),
               **{key: perf.get(key, '') for key in ('loop_thread_cpu_ms', 'loop_wall_ms', 'cycles', 'cycles_per_s',
                                                    'ai_update_thread_cpu_ms', 'ai_update_wall_ms', 'ai_update_calls')},
               'wall_s': round(wall, 3), 'returncode': process.returncode, 'error': ';'.join(errors),
               'setup_json': encoded(setup), 'result_json': encoded(result), 'digest_json': encoded(digest),
               'metadata_json': encoded(capture['metadata']),
               'roster_json': encoded(found['CIV_WARS_ROSTER']), 'override_json': encoded(found['CIV_WARS_OVERRIDE']),
               'campaign_result': encoded(found['CAMPAIGN_BALANCE_RESULT']), 'command_json': encoded(cmd)}
        summary_path = out / 'summary.json'
        if summary_path.is_file():
            try:
                summary = json.loads(summary_path.read_text(encoding='utf-8'))
                row.update(source_commit=summary.get('sourceCommit', ''),
                           working_tree_modified=summary.get('workingTreeModified', ''),
                           source_fingerprint=summary.get('sourceFingerprint') or '',
                           source_fingerprint_error=summary.get('sourceFingerprintError') or '')
            except (ValueError, TypeError):
                row['error'] += ';malformed_runner_summary'
        for team, side in (('1', 'left'), ('2', 'right')):
            p = next((places[h['house']] for h in houses if h.get('team') == team and h.get('house') in places), None)
            row[side + '_score'] = p.score if p else ''
            row[side + '_drawn'] = int(p.drawn) if p else ''
        for field, scope in (('ai_wall_ms', 'frame.ai'), ('placement_wall_ms', 'ai.findPlaceLocation')):
            values = [number(p.get('sum')) for p in capture['perf'] if p.get('scope') == scope and p.get('unit') == 'us']
            row[field] = sum(v for v in values if v is not None) / 1000 if values and all(
                v is not None for v in values) and capture['performance_complete'] else ''
        # Probe's whole House AI phase is a direct measurement, distinct from QuantBot::update.
        if number(perf.get('ai_wall_ms')) is not None:
            row['ai_wall_ms'] = perf['ai_wall_ms']
        house_rows = []
        for house in houses:
            h = house.get('house', '')
            p = places.get(h)
            controller = roster.get(h, {})
            house_rows.append(raw_row(base, house, place=p.place if p else '', score=p.score if p else '',
                                      drawn=int(p.drawn) if p else '',
                                      worm_losses=capture['losses'][h] if worm_complete else '',
                                      observed_worm_losses=capture['losses'][h],
                                      attacks=capture['attacks'][h] if capture['complete'] else '',
                                      observed_attacks=capture['attacks'][h], telemetry_status=capture['status'],
                                      player_class=controller.get('player_class', ''),
                                      bot_strategy=controller.get('strategy', ''),
                                      initial_built=controller.get('initial_built', ''),
                                      initial_lost=controller.get('initial_lost', ''), roster_json=encoded(controller)))
        item_rows = [raw_row(base, data, source='probe') for data in found['CIV_WARS_ITEM']]
        item_rows += [{**item, **base, 'complete': int(worm_complete) if item['event'] == 'worm_loss'
                       else item['complete']} for item in capture['items']]
        wave_rows = [raw_row(base, data, source='custom_wave_launch_changes', event='launch', complete=0)
                     for data in found['CIV_WARS_WAVE']]
        wave_rows += [{**wave, **base} for wave in capture['waves']]
        perf_rows = [raw_row(base, data, source='probe_loop', complete=1) for data in found['CIV_WARS_PERF']]
        perf_rows += [raw_row(base, data, source='probe_ai_update', complete=1) for data in found['CIV_WARS_AI_PERF']]
        perf_rows += [{**p, **base} for p in capture['perf']]
        rows = {'houses.csv': house_rows,
                'samples.csv': [raw_row(base, data) for data in found['CIV_WARS_SAMPLE']],
                'research.csv': [raw_row(base, data) for data in found['CIV_WARS_RESEARCH']],
                'items.csv': item_rows, 'waves.csv': wave_rows, 'perf.csv': perf_rows,
                'decisions.csv': [{**decision, **base} for decision in capture['decisions']],
                'worms.csv': [raw_row(base, data, id_object=data.get('id', data.get('object_id', '')))
                              for data in worms], 'games.csv': [row]}
        results = Path(args.results)
        results.mkdir(parents=True, exist_ok=True)
        with lock:
            if row['error']:
                (results / f'failed-{short}-{base["attempt"]}.txt').write_text(
                    game['id'] + '\n' + process.stdout[-8000:] + '\n' + process.stderr[-8000:]
                    + '\n' + '\n'.join(lines[-100:]), encoding='utf-8')
            for name, fields in CSV_FILES:  # games.csv MUST remain last for cluster collection.
                append(results / name, fields, rows[name])
        failed = bool(row['error'])
        return row
    finally:
        finish_game(scratch, out, failed)


def build_probe(args, scratch):
    """Cluster preparation supplies only --build-dir/--results; do not require Civ inputs."""
    out = Path(scratch) / 'probe'
    try:
        subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'),
                        '--build-dir', str(args.build_dir), '--output-dir', str(out), '--scenario',
                        '--skirmish', 'size=64,base=cy,army=0',
                        '--scenario-sides', 'harkonnen:1:qBotEasy,atreides:2:qBotEasy', '--house', 'harkonnen',
                        '--local-spectator', '--minutes', '1', '--capture-mib', '256', '--no-ai-telemetry'],
                       cwd=root, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', file=sys.stderr)
        print(error.stderr or '', file=sys.stderr)
        raise
    shutil.rmtree(out / 'profile', ignore_errors=True)
    return probe_binary(out)


def completed(results):
    games = {}
    for row in read(Path(results) / 'games.csv'):
        if row['id'] not in games or not row.get('error') or games[row['id']].get('error'):
            games[row['id']] = row
    return games


def run(args):
    results = Path(args.results)
    if args.fresh:
        shutil.rmtree(results, ignore_errors=True)
    results.mkdir(parents=True, exist_ok=True)
    done = {key for key, row in completed(results).items() if not row.get('error')}
    plan = schedule(args)
    scheduled = {game['id'] for game in plan}
    todo = [game for game in plan if game['id'] not in done]
    print(f'{len(todo)} games to play ({len(done)} recorded successful games)', flush=True)
    if todo:
        require_free_space()
        with scratch_dir('civ-wars-matrix', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = [pool.submit(play, args, scratch, binary, game) for game in todo]
                for index, future in enumerate(as_completed(futures), 1):
                    row = future.result()
                    print(f'[{index}/{len(todo)}] {row["id"]} -> {row["outcome"] or "?"} '
                          f'team={row["winner_team"] or "?"} {row["end_s"]}s '
                          f'wall={row["wall_s"]}s telemetry={row["telemetry_status"]} '
                          f'{row["error"]}', flush=True)
    return any(row.get('error') for row in completed(results).values() if row['id'] in scheduled)


def mean_text(values, digits=1):
    values = [value for value in values if value is not None]
    return f'{statistics.mean(values):.{digits}f}' if values else '-'


def side_score(game, team):
    return number(game.get('left_score' if str(team) == '1' else 'right_score'))


def source_fingerprint_value(game):
    value = game.get('source_fingerprint') or ''
    return value if re.fullmatch(r'[0-9a-f]{64}', value) and not game.get('source_fingerprint_error') else None


def source_identified(game):
    if not game.get('source_commit') or game.get('source_fingerprint_error'):
        return False
    if game.get('source_fingerprint'):
        return source_fingerprint_value(game) is not None
    return str(game.get('working_tree_modified', '')).lower() in ('false', '0')


def cohort(game):
    settings = tuple(game.get(key, '') for key in ('format', 'pair', 'size', 'worms', 'spice', 'minutes',
                                                  'options_input_sha256', 'source_commit', 'options_hash'))
    modified = 'content-identified' if source_fingerprint_value(game) else game.get('working_tree_modified', '')
    return settings + (modified, game.get('source_fingerprint') or '', game.get('source_fingerprint_error') or '')


def focal_houses(game, houses):
    key = game.get('_report_key', game['id'])
    return [house for house in houses.get(key, []) if house.get('team') == str(game['focal_team'])]


def load_report(directories):
    """Join only the chosen completed attempt, without collapsing IDs across source cohorts."""
    games, tables = {}, {name: {} for name, _ in CSV_FILES if name != 'games.csv'}
    for directory in directories:
        selected = {}
        for game in read(Path(directory) / 'games.csv'):
            key = (game['id'], cohort(game), game.get('map_hash', ''))
            if key not in selected or not game.get('error') or selected[key].get('error'):
                selected[key] = game
        selected_rows = {name: defaultdict(list) for name in tables}
        for name in tables:
            for row in read(Path(directory) / name):
                selected_rows[name][(row.get('id'), row.get('attempt', ''))].append(row)
        for game in selected.values():
            gid = game['id']
            # Unknown provenance must not merge unrelated legacy directories.
            provenance = '' if source_identified(game) else str(Path(directory).resolve())
            key = (gid, cohort(game), game.get('map_hash', ''), provenance)
            if key not in games or not game.get('error') or games[key].get('error'):
                game['_report_key'] = key
                game['_research_available'] = (Path(directory) / 'research.csv').is_file()
                games[key] = game
                for name in tables:
                    joined = selected_rows[name][(gid, game.get('attempt', ''))]
                    tables[name][key] = list({row['house']: row for row in joined}.values()
                                            ) if name == 'houses.csv' else joined
    return list(games.values()), tables


def loss_kind(item):
    item = integer(item)
    return 'harvester' if item in HARVESTER_ITEMS else 'combat' if item in COMBAT_ITEMS else 'other'


def worm_loss_counts(game, items):
    """None exact counts are unavailable; observed counts remain explicitly lower bounds."""
    losses = [row for row in items if row.get('event') == 'worm_loss']
    observed = Counter({kind: 0 for kind in LOSS_KINDS})
    seen = set()
    for row in losses:
        identity = (row.get('house'), row.get('object'))
        if row.get('object') and identity in seen:
            continue
        seen.add(identity)
        if row.get('team') == str(game['focal_team']):
            observed[loss_kind(row.get('item'))] += integer(row.get('observed_count')) or 1
    total = sum(integer(row.get('observed_count')) or 1 for row in losses)
    complete = (str(game.get('worm_losses_complete')) == '1'
                and number(game.get('observed_worm_losses')) == total
                and all(row.get('team') in ('1', '2') and integer(row.get('item')) is not None
                        and str(row.get('complete')) == '1' for row in losses))
    return observed, dict(observed) if complete else None


def tech_reach(games, houses, level):
    """Reached median excludes -1; reach denominator excludes unavailable team observations."""
    timings, observed = [], 0
    for game in games:
        hs = focal_houses(game, houses)
        values = [number(house.get(f'tech{level}_s')) for house in hs]
        reached = [value / 60 for value in values if value is not None and value >= 0]
        if reached:
            observed += 1
            timings.append(min(reached))
        elif len(hs) == integer(game.get('team_size')) and all(value == -1 for value in values):
            observed += 1
    return timings, observed


def paired_scores(rows, arm):
    """One trial per seed; only identical identified source, map, options and orientation."""
    indexed = defaultdict(lambda: defaultdict(list))
    for game in rows:
        if game.get('arm') in ('off', arm):
            key = (cohort(game), game.get('pair_id'), game.get('seed'), game.get('swap'))
            indexed[key][game['arm']].append(game)
    paired, matched = defaultdict(list), 0
    for by_arm in indexed.values():
        if len(by_arm['off']) != 1 or len(by_arm[arm]) != 1:
            continue  # Ambiguous map/source variants cannot be pooled into a single seed.
        control, game = by_arm['off'][0], by_arm[arm][0]
        if not source_identified(game) or not source_identified(control):
            continue
        if any(not game.get(key) or game[key] != control.get(key) for key in ('map_hash', 'options_hash')):
            continue
        candidate = side_score(game, game['focal_team'])
        baseline = side_score(control, game['focal_team'])
        if candidate is not None and baseline is not None:
            paired[str(game['seed'])].append((candidate, baseline))
            matched += 1
    return matched, {seed: (statistics.mean(pair[0] for pair in values),
                            statistics.mean(pair[1] for pair in values)) for seed, values in sorted(paired.items())}


def report(args):
    dirs = args.report or [args.results]
    games, tables = load_report(dirs)
    houses = tables['houses.csv']
    good = [game for game in games if not game.get('error')]
    print(f'# Civilization Wars: {len(good)} complete games, {len(games) - len(good)} failed games\n')
    print(match_score.describe('placement', match_score.DRAW_SCORE, 0))
    print('\nFocus = first difficulty in the pair for off/on/left; second for right. Side swaps move '
          'controllers AND their strategy setting. left/right are TEST-ONLY UNHASHED per-bot overrides. '
          'Win rates use all completed games, not just decisive matches. Time-outs are not material wins.\n')
    print('Waves/reached in probe rows are explicitly limited observations: custom-wave changes are not '
          'all launches; reached counts distinct living combat units on far-quarter rock, not wave arrivals. '
          'Telemetry attacks count nonempty ground_hunt events. Worm losses match lethal_damage.attacker '
          'against observed worm IDs. Empty exact counts mean unavailable; observed counts are lower bounds. '
          'Islands is a proxy: live yards on central rock, summed over the team, NOT unique island ownership.\n')
    grouped = defaultdict(list)
    for game in good:
        grouped[cohort(game)].append(game)
    for settings, rows in sorted(grouped.items()):
        fmt, pair, size, worms, spice, minutes, options, commit, actual_options, modified, fingerprint, fingerprint_error = settings
        print(f'## {fmt} {pair}; size={size} worms={worms} spice={spice} cap={minutes}m '
              f'options={options} actual_options={actual_options or "unavailable"} '
              f'source={commit or "unavailable"} modified={modified or "unknown"} '
              f'fingerprint={fingerprint or "unavailable"} fingerprint_error={fingerprint_error or "none"}\n')
        print('| Arm | Games (seeds) | Focus W-L-T-E | Win rate | Time-outs | Mean / max min | '
              'Islands proxy mean | Team tech mean | Attacks mean (available games) | '
              'Worm losses mean (available games) | Complete telemetry |')
        print('|---|---|---|---|---|---|---|---|---|---|---|')
        for arm in ARMS:
            arm_rows = [game for game in rows if game['arm'] == arm]
            if not arm_rows:
                continue
            wins = sum(game['outcome'] == 'won' and game['winner_team'] == str(game['focal_team']) for game in arm_rows)
            losses = sum(game['outcome'] == 'won' and game['winner_team'] != str(game['focal_team']) for game in arm_rows)
            timeouts = sum(game['outcome'] == 'time_limit' for game in arm_rows)
            extinct = sum(game['outcome'] == 'extinct' for game in arm_rows)
            islands, tech, attacks, worm_losses = [], [], [], []
            lengths = [number(game['end_s']) / 60 for game in arm_rows if number(game['end_s']) is not None]
            for game in arm_rows:
                hs = focal_houses(game, houses)
                for key, values, reducer in (('islands', islands, sum), ('tech', tech, statistics.mean),
                                             ('attacks', attacks, sum), ('worm_losses', worm_losses, sum)):
                    observed = [number(house.get(key)) for house in hs]
                    if len(hs) == int(game['team_size']) and all(value is not None for value in observed):
                        values.append(reducer(observed))
            n = len(arm_rows)
            max_length = f'{max(lengths):.1f}' if lengths else '-'
            print(f'| {arm} | {n} ({len({game["seed"] for game in arm_rows})}) | '
                  f'{wins}-{losses}-{timeouts}-{extinct} | {wins / n:.1%} | {timeouts / n:.1%} | '
                  f'{mean_text(lengths)} / {max_length} | {mean_text(islands)} | {mean_text(tech)} | '
                  f'{mean_text(attacks)} ({len(attacks)}) | {mean_text(worm_losses)} ({len(worm_losses)}) | '
                  f'{sum(game["telemetry_status"] == "complete" for game in arm_rows)}/{n} |')
        print('\nFirst house on the focus team to reach each REAL per-house tech level: median minutes '
              'among reaching games only. -1 = observed unreached, never a negative timing. '
              'Reach fractions use games with a reached house or all focus-house timings observed; '
              'missing timings are excluded, known unreached games stay in the denominator.\n')
        print('| Arm | ' + ' | '.join(f'Tech {level}: min (games)' for level in range(2, 10)) + ' |')
        print('|---|' + '---|' * 8)
        for arm in ARMS:
            arm_rows = [game for game in rows if game['arm'] == arm]
            if not arm_rows:
                continue
            cells = []
            for level in range(2, 10):
                timings, observed = tech_reach(arm_rows, houses, level)
                cells.append(f'{statistics.median(timings):.1f} ({len(timings)}/{observed})' if timings
                             else f'- (0/{observed})')
            print(f'| {arm} | ' + ' | '.join(cells) + ' |')
        print('\nFocus-team worm deaths by actual item IDs: harvesters=31/59/85/86; combat excludes '
              'all harvester families, MCVs, transports, ambient units and worms. Other IDs stay separate. '
              'Exact means require complete worm identity/capture and a fully joined item ledger. '
              'Observed means are lower bounds even when some games have exact captures.\n')
        print('| Arm | Category | Exact mean (games) | Observed lower-bound mean (joined games) |')
        print('|---|---|---|---|')
        for arm in ARMS:
            loss_rows = [game for game in rows if game['arm'] == arm]
            if not loss_rows:
                continue
            for kind in LOSS_KINDS:
                exact_values, observed_values = [], []
                for game in loss_rows:
                    items = tables['items.csv'].get(game['_report_key'], [])
                    observed, exact = worm_loss_counts(game, items)
                    if items or number(game.get('observed_worm_losses')) == 0:
                        observed_values.append(observed[kind])
                    if exact is not None:
                        exact_values.append(exact[kind])
                print(f'| {arm} | {kind} | {mean_text(exact_values)} ({len(exact_values)}) | '
                      f'{mean_text(observed_values)} ({len(observed_values)}) |')
        print('\nActual research starts from research.csv event=start (not inferred tech changes), '
              'median first focus-house start per target in minutes; games starting / available complete rosters.\n')
        print('| Arm | ' + ' | '.join(f'Target {level}: min (games)' for level in range(2, 10)) + ' |')
        print('|---|' + '---|' * 8)
        for arm in ARMS:
            arm_rows = [game for game in rows if game['arm'] == arm]
            if not arm_rows:
                continue
            cells = []
            for level in range(2, 10):
                starts, available = [], 0
                for game in arm_rows:
                    if not game['_research_available'] or len(focal_houses(game, houses)) != integer(game.get('team_size')):
                        continue
                    available += 1
                    cycles = [number(row.get('cycle')) for row in tables['research.csv'][game['_report_key']]
                              if row.get('event') == 'start' and integer(row.get('target')) == level
                              and row.get('team') == str(game['focal_team'])]
                    cycles = [cycle for cycle in cycles if cycle is not None and cycle >= 0]
                    if cycles:
                        starts.append(min(cycles) * 0.016 / 60)  # GAMESPEED_DEFAULT = 16ms.
                cells.append(f'{statistics.median(starts):.1f} ({len(starts)}/{available})' if starts
                             else f'- (0/{available})')
            print(f'| {arm} | ' + ' | '.join(cells) + ' |')
        print('\nPaired placement scores against off, matching source commit, seed, orientation and map/options '
              'hashes. Source identity requires matching measured fingerprints or known clean commits. '
              'Legacy dirty/unknown snapshots are ineligible. Intervals resample seed means '
              '(both orientations of one seed are ONE trial); fewer than five fixed seeds are insufficient.\n')
        print('| Arm vs off | Paired games / seeds | Candidate / off score | Delta [95% seed bootstrap] | Verdict |')
        print('|---|---|---|---|---|')
        required_seeds = {str(seed) for seed in SEEDS}
        for arm in ARMS[1:]:
            matched, paired_seeds = paired_scores(rows, arm)
            seed_pairs = list(paired_seeds.values())
            if not seed_pairs:
                print(f'| {arm} | 0 / 0 | - | unavailable | - |')
                continue
            deltas = [candidate - control for candidate, control in seed_pairs]
            low, high = match_score.bootstrap(deltas, int(hashlib.sha256(encoded((settings, arm)).encode()).hexdigest()[:8], 16))
            matched_seeds = set(paired_seeds)
            sufficient = len(seed_pairs) >= 5 and required_seeds <= matched_seeds
            verdict = ('beats off' if low > 0 else 'loses to off' if high < 0 else 'not distinguished'
                       ) if sufficient else 'insufficient fixed-seed coverage'
            print(f'| {arm} | {matched} / {len(seed_pairs)} | '
                  f'{statistics.mean(pair[0] for pair in seed_pairs):+.3f} / '
                  f'{statistics.mean(pair[1] for pair in seed_pairs):+.3f} | '
                  f'{statistics.mean(deltas):+.3f} [{low:+.3f}, {high:+.3f}] | {verdict} |')
        print()
    for game in sorted(games, key=lambda game: game['id']):
        if game.get('error'):
            print(f'FAILED {game["id"]}: {game["error"]}')


# Keep every definition, parser default and argument ABOVE this literal assignment:
# dunecity-queue's AST loader executes only that prefix.
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--results', help='Directory for raw CSVs; games.csv is written last')
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--seeds', nargs='+', type=seed_value, default=list(SEEDS))
parser.add_argument('--team-sizes', nargs='+', type=int, choices=(1, 2, 3), default=[1, 2, 3])
parser.add_argument('--pairs', nargs='+', type=difficulty_pair, default=list(PAIRS))
parser.add_argument('--arms', nargs='+', choices=ARMS, default=list(ARMS))
parser.add_argument('--minutes', type=int, choices=range(1, 121), default=30)
parser.add_argument('--size', type=int, choices=range(4), default=0)
parser.add_argument('--worms', type=int, choices=range(11), default=1)
parser.add_argument('--spice', type=int, choices=range(5), default=2)
parser.add_argument('--both-sides', action='store_true', help='Also exchange controllers AND their strategy settings')
parser.add_argument('--jobs', type=positive, default=1, help='Standalone parallelism; cluster schedules one game per unit')
parser.add_argument('--wall-timeout', type=positive, default=1800, help='Wall seconds per game')
parser.add_argument('--scratch', help='Existing managed scratch root; default is a fresh leased scratch run')
parser.add_argument('--game-options', type=Path, help='Transported INI overlay; exact content hash is part of every game ID')
parser.add_argument('--report', nargs='*', metavar='RESULTS', help='Report these directories, or --results if none given')
parser.add_argument('--fresh', action='store_true', help='Remove the results directory before scheduling')
args = parser.parse_args()
if args.report is not None:
    if not args.report and not args.results:
        parser.error('--report needs directories or --results')
    report(args)
elif not args.results:
    parser.error('--results is required to play games')
else:
    sys.exit(1 if run(args) else 0)
