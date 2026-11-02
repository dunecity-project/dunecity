#!/usr/bin/env python3
"""Real-engine Civ Wars early-air defence and ordinary DuneCity strategy isolation gates.

Queue example (the queue prepares one locked/cached probe per native tree):
  dunecity-queue submit --cwd . --tag civ-wars-gates -- python3 tests/ai/civ-wars-gates.py \
      --results /path/to/evidence --seeds 7101 7202 --repeats 2 --minutes 3

Each queue unit contains BOTH hashed off/on arms and ALL repeats, so a unit cannot
pass before its own comparisons execute. Defaults cover lab/yard on both generated
sides, plus ordinary Random Skirmish with Research Lab disabled and enabled. Air
units use canonical starts, a single full-health enemy aircraft at 30 seconds and
unmodified QuantBot updates. The single-aircraft strength requires at least two
original starter turrets covering the target and approach. No defended orders,
boosts, damage injection, invulnerability, forced production or replenishment.

Air pass requires a real defender projectile targeting that aircraft, complete
engine lethal-damage attribution to its defending team, and survival of BOTH
original core objects through the configured horizon. Off/on air results are
reported without claiming an improvement. Ordinary off/on final AND periodic
state digests must match, while config hashes must differ. Every arm's repeats
must match. Missing/truncated logs, telemetry, CSV rows or configured cases fail.

Full run/launcher logs, setup/summary, compressed complete telemetry, raw case and
observation CSVs are retained as flat files (including on queue workers). games.csv
is the LAST completion marker. --report validates the entire configured schedule
and artifact checksums without compiling or playing anything. Reuse a results
directory only for this exact source/schedule; sourceFingerprint must be complete
and match across arms/repeats. sourceCommit/workingTreeModified remain descriptive;
the fingerprint identifies source inputs, not toolchains or rendered assets.
"""
import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import threading
import time
import uuid

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, probe_binary, require_free_space, scratch_dir  # noqa: E402

VERSION = 'civ-gates-1'
ARMS = ('off', 'on')
RECORD = re.compile(r'\b(PROBE_CONFIG|PROBE_DIGEST|PROBE_TIMING|CAMPAIGN_BALANCE_RESULT|CIV_[A-Z_]+|SCENARIO_[A-Z_]+):\s*(.*)')
IDENTITY = ('gate_id', 'attempt', 'kind', 'seed', 'team_size', 'defender_team', 'target', 'difficulty', 'research', 'minutes', 'repeats')
CASE_FIELDS = (*IDENTITY, 'case_id', 'strategy', 'repeat', 'host', 'platform', 'returncode', 'wall_s', 'error',
               'config_hash', 'options_hash', 'map_hash', 'digest', 'cycle', 'source_commit', 'working_tree_modified',
               'source_fingerprint', 'source_fingerprint_error', 'observations_count', 'observations_sha256',
               'defeated_cycle', 'yard_min_raw', 'lab_min_raw', 'defence_projectile_target_cycles',
               'strike_projectile_cycles', 'defender_kills', 'telemetry_events', 'signature',
               'setup_json', 'air_result_json', 'digest_sequence_json', 'kills_json', 'artifacts_json')
OBS_FIELDS = (*IDENTITY, 'case_id', 'strategy', 'repeat', 'marker', 'ordinal', 'data_json')
GAME_FIELDS = ('id', *IDENTITY, 'host', 'platform', 'cases_expected', 'cases_executed', 'wall_s', 'error', 'comparison_json')
lock = threading.Lock()


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True)


def integer(value):
    try:
        number = int(value)
        return number if str(number) == str(value) else None
    except (ValueError, TypeError):
        return None


def positive(value):
    value = int(value)
    if value < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return value


def repeats_value(value):
    value = positive(value)
    if value < 2:
        raise argparse.ArgumentTypeError('at least two repeats are required')
    return value


def seed_value(value):
    value = int(value)
    if not 0 <= value <= 0xffffffff:
        raise argparse.ArgumentTypeError('must be a 32-bit unsigned seed')
    return value


def schedule(args):
    """One JSON-serializable unit per seed/scenario; comparisons stay on one machine."""
    games = []
    for seed in dict.fromkeys(args.seeds):
        for team_size in dict.fromkeys(args.team_sizes):
            for difficulty in dict.fromkeys(args.difficulties):
                for team in (1, 2):
                    for target in ('lab', 'yard'):
                        game = dict(kind='air', seed=seed, team_size=team_size, defender_team=team,
                                    target=target, difficulty=difficulty, research=1,
                                    minutes=args.minutes, repeats=args.repeats)
                        game['id'] = VERSION + '/' + encoded(game)
                        games.append(game)
        for research in (0, 1):
            game = dict(kind='ordinary', seed=seed, team_size=1, defender_team='', target='',
                        difficulty='hard', research=research, minutes=args.minutes, repeats=args.repeats)
            game['id'] = VERSION + '/' + encoded(game)
            games.append(game)
    return games


def read(path):
    path = Path(path)
    if not path.is_file():
        return []
    with path.open(newline='', encoding='utf-8') as handle:
        rows = list(csv.DictReader(handle, strict=True))
    if any(None in row or any(value is None for value in row.values()) for row in rows):
        raise ValueError('Truncated or malformed CSV: ' + str(path))
    return rows


def append(path, fields, rows):
    if not rows:
        return
    if path.is_file() and path.stat().st_size:
        with path.open(newline='', encoding='utf-8') as handle:
            if next(csv.reader(handle), []) != list(fields):
                raise ValueError('Incompatible CSV schema: ' + str(path))
        new = False
    else:
        new = True
    with path.open('a', newline='', encoding='utf-8') as handle:
        writer = csv.DictWriter(handle, fields, extrasaction='raise')
        if new:
            writer.writeheader()
        writer.writerows({key: row.get(key, '') for key in fields} for row in rows)


def records(text):
    found = defaultdict(list)
    for line in text.splitlines():
        match = RECORD.search(line)
        if match:
            found[match[1]].append(dict(part.split('=', 1) for part in match[2].split() if '=' in part))
    return found


def exactly(found, marker, errors):
    rows = found[marker]
    if len(rows) != 1:
        errors.append(f'{marker}_count={len(rows)}')
        return {}
    return rows[0]


def telemetry(path, final_cycle, houses, strike, air_setup):
    """Use the existing engine's complete stream, never absence-as-zero kill counts."""
    errors, kills = [], []
    starts = ends = summaries = events = previous = 0
    end_cycle = summary_cycle = None
    summary_houses = set()
    sessions = set()
    if not path.is_file():
        return ['missing_telemetry'], [], 0
    with path.open(encoding='utf-8') as handle:
        for line in handle:
            try:
                event = json.loads(line)
                data = event['data']
                if not isinstance(data, dict) or integer(event.get('seq')) != previous + 1:
                    raise ValueError('bad record or sequence')
                previous += 1
                events += 1
                sessions.add(event['session'])
                name = event['event']
                if name == 'session_start':
                    starts += 1
                elif name == 'session_end':
                    ends += 1
                    end_cycle = integer(event.get('cycle'))
                elif name == 'game_summary':
                    summaries += 1
                    summary_cycle = integer(event.get('cycle'))
                    summary_houses.update(data['houses'])
                elif name in ('capture_limit', 'simulation_exception'):
                    errors.append(name)
                elif name == 'lethal_damage' and str(data.get('object')) == strike.get('object'):
                    kills.append({'cycle': event.get('cycle'), 'house': event.get('house'), **data})
            except (ValueError, TypeError, KeyError):
                errors.append('malformed_or_discontinuous_telemetry')
    if (starts, ends, summaries) != (1, 1, 1) or len(sessions) != 1:
        errors.append('incomplete_telemetry_session')
    if final_cycle is None or summary_cycle != final_cycle or end_cycle is None or end_cycle < final_cycle:
        errors.append('short_telemetry')
    if not houses or not houses <= summary_houses:
        errors.append('incomplete_telemetry_roster')
    if strike:
        if len(kills) != 1:
            errors.append('strike_missing_unique_real_kill')
        elif (str(kills[0].get('house')) != air_setup.get('attacker')
              or str(kills[0].get('attacker_house')) not in houses
              or str(kills[0].get('attacker_house')) == air_setup.get('attacker')):
            errors.append('strike_kill_not_from_defending_team')
    return sorted(set(errors)), kills, events


def command(args, binary, game, strategy, out, options):
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--local-spectator',
           '--civ-strategy', strategy, '--seed', str(game['seed']), '--minutes', str(game['minutes']),
           '--path-workers', '1', '--wall-timeout', str(args.wall_timeout), '--capture-mib', '1024']
    if game['kind'] == 'air':
        left, right = (game['difficulty'], 'hard') if game['defender_team'] == 1 else ('hard', game['difficulty'])
        cmd += ['--mod', 'CivWars', '--civ-wars', str(game['team_size']), '--civ-size', '0',
                '--civ-worms', '1', '--civ-spice', '2', '--civ-air-probe', game['target'],
                '--civ-air-team', str(game['defender_team']), '--partner-difficulty', left, '--enemy-difficulty', right]
    else:
        cmd += ['--mod', 'dunecity', '--scenario', '--skirmish',
                f'size=96,base=small,army=0,city=1,seed={game["seed"]},tech=1,credits=5000,symmetric=1',
                '--scenario-sides', 'harkonnen:1:qBotHard,atreides:2:qBotHard', '--house', 'harkonnen',
                '--partner-difficulty', 'hard', '--enemy-difficulty', 'hard', '--game-options', str(options),
                '--civ-ordinary-gate', 'research' if game['research'] else 'no-research']
    return cmd


def retain_file(source, destination):
    shutil.copyfile(source, destination)
    data = destination.read_bytes()
    return dict(name=destination.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def run_case(args, scratch, binary, game, base, strategy, repeat):
    short = hashlib.sha256(game['id'].encode()).hexdigest()[:16]
    tag = f'{short}-{base["attempt"]}-{strategy}-{repeat}'
    out = scratch / ('g' + uuid.uuid4().hex[:10])
    results = Path(args.results)
    errors, artifacts = [], []
    row = {**base, 'case_id': tag, 'strategy': strategy, 'repeat': repeat,
           'host': platform.node(), 'platform': sys.platform}
    options = scratch / f'options-{tag}.ini'
    if game['kind'] == 'ordinary':
        options.write_text('[Game Options]\nResearch Lab = ' + ('true' if game['research'] else 'false') + '\n',
                           encoding='utf-8')
    started = time.monotonic()
    failed = True
    try:
        cmd = command(args, binary, game, strategy, out, options)
        # An inherited probe selector must not silently replace the configured engine run.
        env = {key: value for key, value in os.environ.items()
               if not key.startswith('BALANCE_') and key != 'DUNECITY_USERDIR'}
        process = subprocess.run(cmd, cwd=root, env=env, capture_output=True, text=True, errors='replace')
        row.update(returncode=process.returncode, wall_s=round(time.monotonic() - started, 3))
        if process.returncode:
            errors.append(f'runner_rc={process.returncode}')
        launcher = results / (tag + '.launcher.log')
        launcher.write_text(encoded(cmd) + '\nSTDOUT\n' + process.stdout + '\nSTDERR\n' + process.stderr, encoding='utf-8')
        data = launcher.read_bytes()
        artifacts.append(dict(name=launcher.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        log = out / 'run.log'
        if not log.is_file():
            errors.append('missing_run_log')
        text = log.read_text(encoding='utf-8', errors='replace') if log.is_file() else ''
        if text and not text.endswith('\n'):
            errors.append('truncated_run_log')
        found = records(text)
        config = exactly(found, 'PROBE_CONFIG', errors)
        campaign = exactly(found, 'CAMPAIGN_BALANCE_RESULT', errors)
        timing = exactly(found, 'PROBE_TIMING', errors)
        finals = [data for data in found['PROBE_DIGEST'] if data.get('final') == '1']
        if len(finals) != 1:
            errors.append(f'final_digest_count={len(finals)}')
        final = finals[0] if len(finals) == 1 else {}
        cycle = integer(final.get('cycle'))
        if (not re.fullmatch(r'[0-9a-f]{16}', final.get('digest', '')) or cycle is None
                or campaign.get('cycle') != final.get('cycle') or timing.get('cycles') != final.get('cycle')
                or campaign.get('seed') != str(game['seed'])):
            errors.append('incomplete_final_state')
        setup_marker = 'CIV_WARS_SETUP' if game['kind'] == 'air' else 'CIV_ORDINARY_SETUP'
        setup = exactly(found, setup_marker, errors)
        if (setup.get('seed') != str(game['seed']) or setup.get('observing') != '1'
                or setup.get('baseline_cycle') != '0' or not setup.get('options_hash')
                or not re.fullmatch(r'[0-9a-f]{16}', setup.get('map_hash', ''))
                or not config.get('config_hash') or config.get('config_hash') != setup.get('config_hash')
                or config.get('civ_strategy') != strategy or config.get('global_civ_strategy') != str(int(strategy == 'on'))
                or config.get('civ_wars') != str(int(game['kind'] == 'air'))):
            errors.append('incorrect_engine_setup')
        digest_sequence = found['PROBE_DIGEST']
        # MILLI2CYCLES uses the engine's recorded 30-second conversion, checked below.
        roster_marker = 'CIV_WARS_ROSTER' if game['kind'] == 'air' else 'SCENARIO_ROSTER'
        roster = found[roster_marker]
        houses = {data.get('house', '') for data in roster}
        if (len(roster) != 2 * game['team_size'] or len(houses) != len(roster)
                or Counter(data.get('team') for data in roster) != {'1': game['team_size'], '2': game['team_size']}):
            errors.append('incorrect_engine_roster')
        air_setup, air_result, strike = {}, {}, {}
        if game['kind'] == 'air':
            exactly(found, 'CIV_WARS_RESULT', errors)
            exactly(found, 'CIV_WARS_PERF', errors)
            ai = found['CIV_WARS_AI_PERF']
            if (len(ai) != len(roster) or {data.get('house') for data in ai} != houses
                    or any((integer(data.get('update_calls')) or 0) < 1 for data in ai)):
                errors.append('missing_actual_quantbot_updates')
            air_setup = exactly(found, 'CIV_AIR_SETUP', errors)
            air_result = exactly(found, 'CIV_AIR_RESULT', errors)
            strike = exactly(found, 'CIV_AIR_STRIKE', errors)
            defeated = exactly(found, 'CIV_AIR_DEFEATED', errors)
            if (air_setup.get('target_kind') != game['target']
                    or air_setup.get('defender_team') != str(game['defender_team'])
                    or (integer(air_setup.get('starter_cover')) or 0) < 2
                    or air_setup.get('strike_count') != '1' or strike.get('count') != '1'
                    or strike.get('accepted') != '1' or strike.get('target') != air_setup.get('target')
                    or strike.get('cycle') != air_setup.get('strike_cycle')
                    or defeated.get('object') != strike.get('object')
                    or defeated.get('cycle') != air_result.get('defeated_cycle')):
                errors.append('incorrect_or_missing_strike')
            if (any(air_result.get(field) != '1' for field in
                    ('pass', 'injected', 'strike_count', 'defeated', 'core_survives', 'yard_survives', 'lab_survives'))
                    or air_result.get('end_cycle') != final.get('cycle')
                    or (integer(air_result.get('defence_projectile_target_cycles')) or 0) < 1
                    or (integer(air_result.get('yard_min_raw')) or 0) < 1
                    or (integer(air_result.get('lab_min_raw')) or 0) < 1):
                errors.append('strike_not_defeated_with_original_core_surviving')
        else:
            exactly(found, 'SCENARIO_MAP', errors)
            exactly(found, 'SCENARIO_RESULT', errors)
            if (setup.get('civ_wars') != '0' or setup.get('city_sim') != '1'
                    or setup.get('research_lab') != str(game['research']) or setup.get('quantbots') != '2'
                    or (not game['research'] and setup.get('initial_labs') != '0')):
                errors.append('ordinary_map_or_research_setting_not_executed')
        final_houses = found['CIV_WARS_HOUSE'] if game['kind'] == 'air' else found['SCENARIO_HOUSE']
        if len(final_houses) != len(roster) or {data.get('house') for data in final_houses} != houses:
            errors.append('incomplete_final_house_results')
        paths = list((out / 'profile').rglob('events.jsonl'))
        if len(paths) != 1:
            errors.append(f'telemetry_file_count={len(paths)}')
        path = paths[0] if len(paths) == 1 else out / 'missing-events.jsonl'
        telemetry_errors, kills, events = telemetry(path, cycle, houses, strike, air_setup)
        errors += telemetry_errors
        # The first telemetry record supplies the real engine cycle conversion; no hardcoded Hz.
        metadata = {}
        if path.is_file():
            try:
                with path.open(encoding='utf-8') as handle:
                    metadata = json.loads(next(handle))['data']
            except (ValueError, KeyError, StopIteration):
                errors.append('missing_engine_clock_metadata')
        half_minute = integer(metadata.get('cycles_per_30_seconds'))
        if (half_minute is None or half_minute <= 0 or cycle != game['minutes'] * 2 * half_minute
                or metadata.get('mod') != ('CivWars' if game['kind'] == 'air' else 'dunecity')
                or integer(metadata.get('city_sim')) != 1):
            errors.append('configured_horizon_or_city_mode_not_completed')
        if cycle is not None and cycle > 0:
            expected_periodic = list(range(0, cycle, 750))  # ProbeDigest::interval's unchanged default.
            periodic = [data for data in digest_sequence if data.get('final') != '1']
            if ([integer(data.get('cycle')) for data in periodic] != expected_periodic
                    or any(not re.fullmatch(r'[0-9a-f]{16}', data.get('digest', '')) for data in periodic)):
                errors.append('missing_or_truncated_periodic_digests')
        if game['kind'] == 'air':
            defenders = {data.get('house') for data in roster if data.get('team') == str(game['defender_team'])}
            if len(kills) != 1 or str(kills[0].get('attacker_house')) not in defenders:
                errors.append('no_unique_defending_team_lethal_hit')
            if integer(air_setup.get('strike_cycle')) != half_minute:
                errors.append('strike_not_at_thirty_seconds')
        # Save every byte before finish_game deletes regenerated scratch/profile assets.
        for name in ('run.log', 'setup.json', 'summary.json'):
            source = out / name
            if source.is_file():
                artifacts.append(retain_file(source, results / (tag + '.' + name)))
            else:
                errors.append('missing_' + name)
        summary = {}
        if (out / 'summary.json').is_file():
            try:
                summary = json.loads((out / 'summary.json').read_text(encoding='utf-8'))
            except (ValueError, TypeError):
                errors.append('malformed_runner_summary')
        if (not re.fullmatch(r'[0-9a-f]{64}', summary.get('sourceFingerprint') or '')
                or summary.get('sourceFingerprintError') != ''):
            errors.append('missing_or_incomplete_source_fingerprint')
        if path.is_file():
            destination = results / (tag + '.events.jsonl.gz')
            with path.open('rb') as source, destination.open('wb') as raw:
                with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as compressed:
                    shutil.copyfileobj(source, compressed)
            data = destination.read_bytes()
            artifacts.append(dict(name=destination.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        if options.is_file():
            artifacts.append(retain_file(options, results / (tag + '.options.ini')))
        deterministic = {'digests': digest_sequence, 'setup': setup, 'air_setup': air_setup, 'strike': strike,
                         'air_result': air_result, 'kills': kills,
                         'houses': final_houses, 'source_fingerprint': summary.get('sourceFingerprint')}
        row.update(config_hash=config.get('config_hash', ''), options_hash=setup.get('options_hash', ''),
                   map_hash=setup.get('map_hash', ''), digest=final.get('digest', ''), cycle=final.get('cycle', ''),
                   source_commit=summary.get('sourceCommit', ''), working_tree_modified=summary.get('workingTreeModified', ''),
                   source_fingerprint=summary.get('sourceFingerprint', ''),
                   source_fingerprint_error=summary.get('sourceFingerprintError', ''),
                   defender_kills=len(kills) if not telemetry_errors else '', telemetry_events=events,
                   signature=hashlib.sha256(encoded(deterministic).encode()).hexdigest(),
                   setup_json=encoded(setup), air_result_json=encoded(air_result),
                   digest_sequence_json=encoded(digest_sequence), kills_json=encoded(kills), artifacts_json=encoded(artifacts))
        row.update({key: air_result.get(key, '') for key in ('defeated_cycle', 'yard_min_raw', 'lab_min_raw',
                                                           'defence_projectile_target_cycles', 'strike_projectile_cycles')})
        observations = [{**base, 'case_id': tag, 'strategy': strategy, 'repeat': repeat, 'marker': marker,
                         'ordinal': index, 'data_json': encoded(data)}
                        for marker, values in found.items() for index, data in enumerate(values)]
        observation_data = sorted((data['marker'], str(data['ordinal']), data['data_json']) for data in observations)
        row.update(observations_count=len(observations),
                   observations_sha256=hashlib.sha256(encoded(observation_data).encode()).hexdigest())
        row['error'] = ';'.join(sorted(set(errors)))
        with lock:
            append(results / 'observations.csv', OBS_FIELDS, observations)
            append(results / 'cases.csv', CASE_FIELDS, [row])
        failed = bool(row['error'])
        return row
    finally:
        options.unlink(missing_ok=True)
        finish_game(scratch, out, failed)


def compare_cases(game, rows):
    errors, comparison = [], {}
    expected = {(arm, repeat) for arm in ARMS for repeat in range(game['repeats'])}
    counts = Counter((row.get('strategy'), integer(row.get('repeat'))) for row in rows)
    if set(counts) != expected or any(value != 1 for value in counts.values()):
        errors.append('missing_or_duplicate_configured_cases')
    if any(row.get('error') for row in rows):
        errors.append('case_failed')
    by_arm = {arm: [row for row in rows if row.get('strategy') == arm] for arm in ARMS}
    for arm, arm_rows in by_arm.items():
        if len(arm_rows) != game['repeats'] or len({row.get('signature') for row in arm_rows}) != 1:
            errors.append(arm + '_repeat_mismatch')
    if all(by_arm.values()):
        off, on = by_arm['off'][0], by_arm['on'][0]
        if (not off.get('config_hash') or not on.get('config_hash') or off['config_hash'] == on['config_hash']):
            errors.append('config_hashes_not_distinct')
        for key in ('map_hash', 'options_hash', 'cycle', 'source_commit', 'source_fingerprint'):
            if not str(off.get(key, '')) or str(off.get(key)) != str(on.get(key)):
                errors.append('unmatched_' + key)
        if game['kind'] == 'ordinary' and any(off.get(key) != on.get(key) for key in ('digest', 'digest_sequence_json')):
            errors.append('ordinary_off_on_digest_mismatch')
        comparison = {arm: {key: by_arm[arm][0].get(key, '') for key in
                           ('digest', 'cycle', 'config_hash', 'defeated_cycle', 'yard_min_raw', 'lab_min_raw',
                            'defence_projectile_target_cycles', 'strike_projectile_cycles', 'defender_kills')}
                      for arm in ARMS}
    return errors, comparison


def play(args, scratch, binary, game):
    scratch = Path(scratch)
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    base = {key: game.get(key, '') for key in IDENTITY}
    base.update(gate_id=game['id'], attempt=uuid.uuid4().hex[:12])
    started = time.monotonic()
    rows = []
    # Do not stop after an unsuccessful arm: all configured controls still execute.
    for strategy in ARMS:
        for repeat in range(game['repeats']):
            rows.append(run_case(args, scratch, binary, game, base, strategy, repeat))
    errors, comparison = compare_cases(game, rows)
    row = {**base, 'id': game['id'], 'host': platform.node(), 'platform': sys.platform,
           'cases_expected': len(ARMS) * game['repeats'], 'cases_executed': len(rows),
           'wall_s': round(time.monotonic() - started, 3), 'error': ';'.join(errors), 'comparison_json': encoded(comparison)}
    with lock:
        append(results / 'games.csv', GAME_FIELDS, [row])  # The queue's LAST completion marker.
    return row


def build_probe(args, scratch):
    """Only preparation can compile; the existing native-tree cache lock serializes it."""
    out = Path(scratch) / 'probe'
    process = subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'),
                              '--build-dir', str(args.build_dir), '--output-dir', str(out), '--scenario',
                              '--skirmish', 'size=64,base=cy,army=0', '--scenario-sides',
                              'harkonnen:1:qBotEasy,atreides:2:qBotEasy', '--house', 'harkonnen',
                              '--local-spectator', '--minutes', '1', '--capture-mib', '256', '--no-ai-telemetry'],
                             cwd=root, capture_output=True, text=True, errors='replace')
    if process.returncode:
        print(process.stdout, file=sys.stderr)
        print(process.stderr, file=sys.stderr)
        raise RuntimeError('Preparing shared balance probe failed; see ' + str(out))
    shutil.rmtree(out / 'profile', ignore_errors=True)
    return probe_binary(out)


def completed(results):
    found = {}
    for row in read(Path(results) / 'games.csv'):
        if row['id'] not in found or not row.get('error') or found[row['id']].get('error'):
            found[row['id']] = row
    return found


def report(args):
    """Fail closed against the actual schedule, all case rows and retained artifact bytes."""
    results = Path(args.results)
    games = completed(results)
    case_rows = defaultdict(list)
    for row in read(results / 'cases.csv'):
        case_rows[(row['gate_id'], row['attempt'])].append(row)
    observation_cases = defaultdict(list)
    for row in read(results / 'observations.csv'):
        observation_cases[row['case_id']].append(row)
    failures, expected_cases = [], 0
    for game in schedule(args):
        expected_cases += len(ARMS) * game['repeats']
        row = games.get(game['id'])
        errors = []
        if row is None:
            failures.append((game['id'], ['missing_gate']))
            continue
        if row.get('error'):
            errors.append(row['error'])
        cases = case_rows[(game['id'], row.get('attempt'))]
        comparison_errors, comparison = compare_cases(game, cases)
        errors += comparison_errors
        if (integer(row.get('cases_expected')) != len(ARMS) * game['repeats']
                or integer(row.get('cases_executed')) != len(cases)):
            errors.append('incomplete_gate_completion_marker')
        for case in cases:
            observations = observation_cases[case['case_id']]
            observation_data = sorted((data['marker'], str(data['ordinal']), data['data_json']) for data in observations)
            if (not observations or integer(case.get('observations_count')) != len(observations)
                    or hashlib.sha256(encoded(observation_data).encode()).hexdigest() != case.get('observations_sha256')):
                errors.append('missing_or_truncated_raw_observations')
            try:
                artifacts = json.loads(case['artifacts_json'])
                if len(artifacts) < 5:
                    raise ValueError('missing artifacts')
                for artifact in artifacts:
                    path = results / artifact['name']
                    if path.name != artifact['name'] or not path.is_file():
                        raise ValueError('missing artifact')
                    data = path.read_bytes()
                    if len(data) != artifact['bytes'] or hashlib.sha256(data).hexdigest() != artifact['sha256']:
                        raise ValueError('artifact checksum mismatch')
            except (ValueError, TypeError, KeyError):
                errors.append('missing_truncated_or_changed_artifacts')
        if errors:
            failures.append((game['id'], sorted(set(errors))))
        else:
            print('PASS ' + game['id'] + '\n  ' + encoded(comparison))
    for gate_id, errors in failures:
        print('FAIL ' + gate_id + ': ' + ';'.join(errors), file=sys.stderr)
    print(f'CIV_GATES_RESULT: pass={int(not failures)} gates={len(schedule(args))} '
          f'cases_expected={expected_cases} failed_gates={len(failures)} results={results}')
    return bool(failures)


def run(args):
    results = Path(args.results)
    if args.fresh and results.exists():
        shutil.rmtree(results)
    results.mkdir(parents=True, exist_ok=True)
    done = completed(results)
    todo = [game for game in schedule(args) if game['id'] not in done or done[game['id']].get('error')]
    if todo:
        require_free_space()
        with scratch_dir('civ-wars-gates', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                for future in as_completed([pool.submit(play, args, scratch, binary, game) for game in todo]):
                    row = future.result()
                    print(('FAIL ' if row['error'] else 'PASS ') + row['id'], flush=True)
    return report(args)


# All definitions/parser setup MUST remain above this literal assignment for the queue AST loader.
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', required=True, help='Permanent flat logs, compressed telemetry and CSV evidence')
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--seeds', nargs='+', type=seed_value, default=[7101, 7202])
parser.add_argument('--team-sizes', nargs='+', type=int, choices=(1, 2, 3), default=[1])
parser.add_argument('--difficulties', nargs='+', choices=('easy', 'medium', 'hard', 'brutal'), default=['hard'],
                    help='Defending difficulty; the injected aircraft owner remains a real Hard QuantBot')
parser.add_argument('--repeats', type=repeats_value, default=2)
parser.add_argument('--minutes', type=int, choices=range(2, 11), default=3, help='Completed horizon, including the 30-second strike')
parser.add_argument('--jobs', type=positive, default=1)
parser.add_argument('--wall-timeout', type=positive, default=1800, help='Per actual engine game, seconds')
parser.add_argument('--scratch', help='Existing managed scratch root; otherwise create one leased scratch run')
parser.add_argument('--fresh', action='store_true', help='Discard this results directory before standalone execution')
parser.add_argument('--report', action='store_true', help='Validate complete evidence only; no compilation or games')
args = parser.parse_args()
sys.exit(1 if (report(args) if args.report else run(args)) else 0)
