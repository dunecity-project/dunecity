"""Read-only loss, quality and clustered experiment reports for dunecity-audit.

Manifest/config values are run inputs, not build identities. These reports never
submit games, build source, change defaults or infer repair use from refinery use.
Repair quality uses exact raw Q32 final ledgers, not sparse stock means. Complete
capture, monotonic totals and bay-cycle partitions are required for quality review;
zero damaged exposure or no actual delivered service is not useful-service proof.
Return outcomes are bounded by actual attempts. Deliveries additionally require
successful releases, but a unit can die during ground deployment before any
release succeeds. Such closed aborts remain measured failures, never deliveries.
Targeted diagnostic manifests use one original-world OFF/ON observation per case,
descriptive effects only, and can never become confirmation or promotion evidence.
"""
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import random
import re
import statistics
from collections import Counter, defaultdict
from datetime import datetime
from statistics import NormalDist

SCHEMA = 'dunecity-experiment/1'
STATUSES = ('opportunity-missing', 'implementation-failed', 'mechanism-unsupported',
            'inconclusive', 'harmful', 'confirmed', 'promoted', 'retired')
MAX_ROWS = 250000
MAX_SUMMARIES = 256
DETECTORS = ('economy_stall', 'power_deficit', 'no_ground_defence', 'missing_starport',
             'missing_radar', 'missing_heavy_factory', 'missing_ix', 'idle_under_attack',
             'unused_credits', 'bankrupt_queue_empty', 'early_no_fight', 'zero_harvesters', 'storage_full')
REPAIR_EVENT_FIELDS = tuple('repair_' + field for field in (
    'treatment', 'actual', 'committed', 'busy', 'completed_waiting', 'serviceable_waiting',
    'transport_blocked', 'transport_inflight', 'access_blocked', 'usable_local', 'busy_local', 'waiting_local',
    'pressure_cycles', 'target', 'reason'))
REPAIR_QUALITY_KEYS = (
    'version', 'start_cycle', 'end_cycle', 'left_censored', 'capture_complete',
    'right_censored_jobs', 'right_censored_returns', 'baseline_bays', 'baseline_pending_paid_raw', 'live_bays',
    'capital_paid_raw_total', 'capital_refund_requested_raw_total', 'capital_refund_actual_raw_total',
    'health_restored_raw_total', 'service_paid_raw_total', 'completed_total', 'return_attempted_total', 'released_ground_total',
    'handoff_carrier_total', 'delivered_total', 'useful_delivered_total', 'aborted_total', 'return_aborted_total',
    'live_cycles_total', 'free_cycles_total', 'damaged_cycles_total', 'full_return_cycles_total', 'unfunded_cycles_total')
REPAIR_QUALITY_FIELDS = tuple('repair_quality_' + key for key in REPAIR_QUALITY_KEYS)
REPAIR_QUALITY_TOTAL_KEYS = tuple(key for key in REPAIR_QUALITY_KEYS if key.endswith('_total'))
REPAIR_QUALITY_UNITS = {
    'repair_quality_' + key: ('Q32 HP raw (1 HP = 4294967296)' if key == 'health_restored_raw_total'
        else 'Q32 credits raw (1 credit = 4294967296)' if '_raw' in key
        else 'bay-cycles (16 ms)' if key.endswith('_cycles_total')
        else 'cycles (16 ms)' if key in ('start_cycle', 'end_cycle')
        else 'schema version' if key == 'version' else '0/1 flag' if key in ('left_censored', 'capture_complete') else 'count')
    for key in REPAIR_QUALITY_KEYS}
REPAIR_METRICS = ('repair_yards', 'repair_yards_built', 'repair_yards_queued',
                  'repair_actual', 'repair_committed', 'repair_busy', 'repair_completed_waiting',
                  'repair_serviceable_waiting', 'repair_transport_blocked', 'repair_transport_inflight', 'repair_access_blocked',
                  'repair_usable_local', 'repair_busy_local', 'repair_waiting_local',
                  'repair_pressure_cycles', 'repair_target') + REPAIR_QUALITY_FIELDS
QUALITY_METRICS = REPAIR_METRICS + (
    'refineries', 'refineries_built', 'refinery_bays', 'occupied_refinery_bays',
    'fleet_bay_available_cycles', 'fleet_bay_occupied_cycles', 'carryalls', 'carryalls_built',
    'factories', 'harvesters', 'harvesters_lost', 'population', 'research_level',
    'required_power_deficit_cycles', 'spent_total', 'fleet_capital_current_value',
    'fleet_capital_built_value')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value, field='number'):
    if value is None or value == '':
        return None
    require(not isinstance(value, bool), 'Boolean ' + field)
    try:
        value = float(value)
    except (TypeError, ValueError):
        raise ValueError('Invalid ' + field) from None
    require(math.isfinite(value), 'Nonfinite ' + field)
    return value


def compact_json(path, maximum=2 * 1024 * 1024):
    with Path(path).open('rb') as handle:
        data = handle.read(maximum + 1)
    require(len(data) <= maximum, 'Oversize JSON: ' + str(path))
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=unique)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def text(value, label):
    require(isinstance(value, str) and value.strip() and len(value) <= 16384, 'Missing ' + label)
    return value


def resolve(base, value):
    path = Path(value).expanduser()
    return path if path.is_absolute() else Path(base) / path


def preregistered_cluster_sd(design):
    """Optional frozen planning variance; current outcomes never supply it."""
    if 'paired_cluster_sd' not in design:
        return None
    sd = number(design['paired_cluster_sd'], 'design.paired_cluster_sd')
    require(sd is not None and sd > 0, 'design.paired_cluster_sd must be finite and positive')
    return sd


def validate_manifest(manifest, base):
    require(manifest.get('schema') == SCHEMA, 'Unsupported experiment manifest')
    diagnostic = manifest.get('purpose') == 'diagnostic'
    require('purpose' not in manifest or diagnostic, 'Unsupported experiment purpose')
    if diagnostic:
        require(type(manifest.get('diagnostic_house')) is int and manifest['diagnostic_house'] == 3,
                'Diagnostic requires physical diagnostic_house 3')
        if 'diagnostic_case_index' in manifest:
            require(type(manifest['diagnostic_case_index']) is int and manifest['diagnostic_case_index'] > 0,
                    'diagnostic_case_index must be a positive original full-deck index')
    else:
        require('diagnostic_house' not in manifest, 'diagnostic_house requires purpose diagnostic')
        require('diagnostic_case_index' not in manifest, 'diagnostic_case_index requires purpose diagnostic')
    require(re.fullmatch(r'[a-z0-9][a-z0-9-]{2,95}', manifest.get('id', '')), 'Invalid experiment id')
    for field in ('mechanism', 'falsifier', 'scope', 'off_contract', 'stopping_rule'):
        text(manifest.get(field), field)
    revision = manifest.get('snapshot', {}).get('revision', '')
    require(re.fullmatch(r'[0-9a-f]{40}', revision), 'Snapshot must be one committed full SHA')
    text(manifest['snapshot'].get('cwd'), 'snapshot.cwd')
    baseline = manifest.get('baseline', {})
    for field in ('release', 'source_commit', 'config_identity', 'population'):
        text(baseline.get(field), 'baseline.' + field)
    require(re.fullmatch(r'[0-9a-f]{40}', baseline['source_commit']), 'Invalid baseline commit')
    require(type(baseline.get('independent')) is bool if diagnostic else baseline.get('independent') is True,
            'Declare baseline independence honestly' if diagnostic else 'Loss baseline must be independent of policy tuning')
    for field in ('observed_failure', 'successful_counterexamples', 'negative_learning'):
        require(isinstance(manifest.get(field), list), 'Missing evidence list: ' + field)
    require(manifest['observed_failure'] and manifest['successful_counterexamples'],
            'Selection needs observed losses and successful counterexamples')
    for arm in ('off', 'on'):
        text(manifest.get('configs', {}).get(arm), 'configs.' + arm)
    require(manifest['configs']['off'] != manifest['configs']['on'], 'OFF and ON configurations must differ')
    if 'effective_config_hashes' in manifest:
        hashes = manifest['effective_config_hashes']
        require(isinstance(hashes, dict) and set(hashes) == {'native', 'off', 'on'},
                'effective_config_hashes requires native/off/on')
        require(all(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{16}', value) for value in hashes.values()),
                'effective_config_hashes values must be 16 lowercase hex digits')
    if 'config_calibration_references' in manifest:
        refs = manifest['config_calibration_references']
        require(isinstance(refs, list) and all(isinstance(ref, str) and ref.strip() for ref in refs),
                'Invalid config_calibration_references')
    for field in ('opportunity', 'mechanism_metric', 'primary'):
        metric = manifest.get(field, {})
        text(metric.get('metric'), field + '.metric')
        require(metric.get('direction') in ('higher', 'lower'), 'Invalid metric direction')
        effect = number(metric.get('minimum_effect'), field + '.minimum_effect')
        require(effect is not None and effect > 0, 'Declare a positive minimum worthwhile effect')
        aggregation = metric.get('aggregation', 'final')
        require(aggregation in ('final', 'decision_max', 'decision_mean', 'decision_last'), 'Invalid metric aggregation')
        if metric['metric'] in REPAIR_EVENT_FIELDS:
            require(aggregation != 'final', 'Repair capacity is a sparse decision stock; declare its aggregation')
        if metric['metric'] in REPAIR_QUALITY_FIELDS:
            require(aggregation == 'final', 'Repair quality requires final cumulative totals, never decision-stock aggregation')
    guards = manifest.get('guards')
    require(isinstance(guards, list), 'Missing harm guards')
    for guard in guards:
        text(guard.get('metric'), 'guard.metric')
        require(guard.get('direction') in ('higher', 'lower'), 'Invalid guard direction')
        margin = number(guard.get('margin'), 'guard.margin')
        require(margin is not None and margin >= 0, 'Invalid harm margin')
        aggregation = guard.get('aggregation', 'final')
        require(aggregation in ('final', 'decision_max', 'decision_mean', 'decision_last'), 'Invalid metric aggregation')
        if guard['metric'] in REPAIR_QUALITY_FIELDS:
            require(aggregation == 'final', 'Repair quality requires final cumulative totals, never decision-stock aggregation')
    design = manifest.get('design', {})
    clusters = design.get('cluster_fields')
    require(isinstance(clusters, list) and 'seed' in clusters and all(
        field in ('seed', 'map_seed', 'mod', 'sample_case', 'map_hash') for field in clusters),
        'Cluster related seats/maps/seeds; house or seat is not an independent cluster')
    if not diagnostic:
        alpha, power = number(design.get('alpha')), number(design.get('power'))
        require(alpha is not None and 0 < alpha < .5 and power is not None and .5 < power < 1,
                'Invalid declared error/power budget')
        require(type(design.get('finalists')) is int and design['finalists'] >= 1, 'Declare finalists actually confirmed')
        preregistered_cluster_sd(design)
    require(design.get('stopping') == 'fixed-budget', 'Declare fixed budget; no unplanned repeated peeking')
    budget = manifest.get('budget', {})
    require(type(budget.get('max_games')) is int and budget['max_games'] > 0, 'Missing game budget')
    require(number(budget.get('max_wall_s')) is not None and budget['max_wall_s'] > 0, 'Missing wall-time budget')
    if diagnostic:
        require('promotion_controls' not in manifest or isinstance(manifest['promotion_controls'], list),
                'Invalid diagnostic control references')
    else:
        require(isinstance(manifest.get('promotion_controls'), list) and manifest['promotion_controls'],
                'Retain applicable approved promotion controls')
    stages = manifest.get('stages', {})
    require(set(stages) == {'pilot'} if diagnostic else 'pilot' in stages and 'confirmation' in stages,
            'Diagnostic permits pilot only; confirmation/promotion forbidden' if diagnostic
            else 'Pilot and independent confirmation decks required')
    tuning, decks, total_games = set(), {}, 0
    for stage, specification in stages.items():
        require(stage in ('pilot', 'screen', 'evaluation', 'confirmation'), 'Unknown experiment stage')
        path = resolve(base, text(specification.get('case_manifest'), stage + '.case_manifest'))
        deck = compact_json(path)
        cases = deck.get('cases', [])
        require(deck.get('schema_version') == 1 and cases, 'Missing explicit audit cases')
        seeds = set()
        for case in cases:
            seed = case.get('seed')
            require(type(seed) is int and 0 <= seed <= 0xffffffff, 'Invalid deck seed')
            seeds.add(seed)
        if stage == 'confirmation':
            try:
                locked = datetime.fromisoformat(specification.get('locked_at', '').replace('Z', '+00:00'))
                require(locked.utcoffset() is not None, 'Holdout lock must have timezone')
            except (TypeError, ValueError):
                raise ValueError('Missing independent holdout lock') from None
            require(specification.get('untouched') is True, 'Tuned holdout needs a new independent deck')
        else:
            tuning.update(seeds)
        argv = specification.get('args')
        require(isinstance(argv, list) and argv and all(isinstance(arg, str) for arg in argv), 'Missing audit run arguments')
        allowed = {'--mode', '--mod', '--minutes', '--base', '--army', '--city', '--tech',
                   '--variant', '--game-options', '--capture-mib'}
        if diagnostic:
            allowed.update(('--diagnostic-house', '--diagnostic-case-index'))
        require(len(argv) % 2 == 0 and all(argv[index] in allowed and not argv[index + 1].startswith('--')
                for index in range(0, len(argv), 2)), 'Only explicit audit run arguments are allowed')
        require(len(set(argv[::2])) == len(argv[::2]), 'Repeated run argument')
        require('--mode' in argv and argv[argv.index('--mode') + 1] == 'sample'
                and '--mod' in argv and argv[argv.index('--mod') + 1] in ('vanilla', 'dunecity'),
                'Experiment uses one explicit content population and the existing sample audit path')
        indices = list(range(1, len(cases) + 1))
        if diagnostic:
            if '--diagnostic-house' in argv:
                require(argv[argv.index('--diagnostic-house') + 1] == str(manifest['diagnostic_house']),
                        'Diagnostic target argument disagrees with manifest')
            selected = manifest.get('diagnostic_case_index')
            if '--diagnostic-case-index' in argv:
                argument = argv[argv.index('--diagnostic-case-index') + 1]
                require(re.fullmatch(r'[1-9][0-9]*', argument), 'Invalid diagnostic case index argument')
                require(selected is None or selected == int(argument), 'Diagnostic case index argument disagrees with manifest')
                selected = int(argument)
            if selected is not None:
                require(selected <= len(cases), 'Diagnostic case index is absent from the original deck')
                indices = [selected]
            require(all(cases[index - 1].get('format') == 'ffa6' for index in indices),
                    'Diagnostic house 3 is absent from an original case roster')
            seeds = {cases[index - 1]['seed'] for index in indices}
        decks[stage] = dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                            seeds=sorted(seeds), case_count=len(indices))
        if diagnostic:
            decks[stage].update(case_indices=indices, source_case_count=len(cases))
        total_games += len(indices) * (2 if diagnostic else 4)
    require(total_games <= budget['max_games'], 'Preregistered decks exceed total game budget')
    require(budget['max_wall_s'] >= total_games, 'Wall budget must allow at least one second per game')
    require(diagnostic or not (set(decks['confirmation']['seeds']) & tuning), 'Confirmation reuses tuning seed/map clusters')
    return {'schema': 'dunecity-experiment-provenance/1', 'manifest_sha256': digest(manifest),
            'source_revision': revision, 'configs': manifest['configs'], 'decks': decks,
            'build_identity_inputs': ['source', 'harness', 'assets', 'toolchain', 'platform', 'build_options'],
            'config_values_are_run_inputs': True,
            'effective_config_hashes': manifest.get('effective_config_hashes', {}),
            'config_calibration_references': manifest.get('config_calibration_references', []),
            **({'purpose': 'diagnostic', 'diagnostic_house': manifest['diagnostic_house'],
                'observations_per_case_per_arm': 1, 'games_per_case': 2} if diagnostic else {})}


def canonical_csv_fields(fields):
    """One column per name, preserving the first occurrence's ordering."""
    return tuple(dict.fromkeys(fields))


def normalized_csv_rows(handle, path, maximum=None):
    """Collapse historical repeated names only when every corresponding value agrees."""
    reader = csv.reader(handle)
    fields = next(reader, [])
    require(fields, 'Invalid CSV header: ' + str(path))
    for index, values in enumerate(reader):
        require(maximum is None or index < maximum, 'Audit row bound reached; select a smaller result directory')
        if not values:
            continue
        require(len(values) == len(fields), 'Malformed CSV: ' + str(path))
        row = {}
        for field, value in zip(fields, values):
            require(field not in row or row[field] == value,
                    'Conflicting duplicate CSV column ' + repr(field) + ' at record ' + str(index + 2) + ': ' + str(path))
            row[field] = value
        yield row


def read_table(directory, name, keys):
    path = Path(directory) / name
    if not path.is_file():
        path = Path(str(path) + '.gz')
    if not path.is_file():
        return []
    opener = gzip.open if path.suffix == '.gz' else open
    rows = {}
    with opener(path, 'rt', encoding='utf-8-sig', newline='') as handle:
        for row in normalized_csv_rows(handle, path, MAX_ROWS):
            require(all(key in row and row[key] != '' for key in keys), 'Missing row identity: ' + str(path))
            identity = tuple(row[key] for key in keys)
            require(identity not in rows or rows[identity] == row, 'Conflicting duplicate result: ' + str(identity))
            rows[identity] = row
    return list(rows.values())


def dataset(directories):
    tables = (('games', 'games.csv', ('id',)), ('houses', 'houses.csv', ('id', 'house')),
              ('samples', 'samples.csv', ('id', 'house', 'cycle')),
              ('repairs', 'repair-capacity.csv', ('id', 'house', 'seq')))
    result = {name: {} for name, _, _ in tables}
    for directory in directories:
        for name, filename, keys in tables:
            for row in read_table(directory, filename, keys):
                key = tuple(row[field] for field in keys)
                require(key not in result[name] or result[name][key] == row, 'Mixed runs have conflicting identities: ' + str(key))
                result[name][key] = row
                require(len(result[name]) <= MAX_ROWS, 'Combined audit exceeds bounded row count')
    return {name: list(rows.values()) for name, rows in result.items()}


def repair_observations(capture):
    """Retain decision stocks and final ledgers together without inventing service."""
    rows = []
    events = capture.get('decisions', []) + capture.get('repair_finals', [])
    for event in events:
        name = event.get('event')
        if name not in ('repair_capacity', 'game_summary'):
            continue
        row = {field: event.get(field, '') for field in ('session', 'seq', 'cycle', 'house', 'player')}
        row.update(event=name, capture_status=capture['status'], capture_reasons=capture.get('reasons', ''),
                   capture_start_cycle=capture.get('start_cycle'), capture_end_cycle=capture.get('end_cycle'))
        data = event.get('data', {})
        row['data'] = json.dumps(data, sort_keys=True, separators=(',', ':'))
        for field in REPAIR_EVENT_FIELDS:
            value = data.get(field.removeprefix('repair_'), '') if name == 'repair_capacity' else ''
            row[field] = int(value) if isinstance(value, bool) else value
        quality = data.get('quality' if name == 'repair_capacity' else 'repair_quality')
        for field, key in zip(REPAIR_QUALITY_FIELDS, REPAIR_QUALITY_KEYS):
            row[field] = quality.get(key, '') if isinstance(quality, dict) else ''
        rows.append(row)
    return rows


def repair_integer(value, field):
    """Parse ledger integers without a float round-trip or implicit Boolean counts."""
    if value is None or value == '':
        return None
    require(type(value) is int or isinstance(value, str) and re.fullmatch(r'-?(0|[1-9][0-9]*)', value),
            'Invalid integer ' + field)
    value = int(value)
    require(value >= 0 and ('_raw' not in field or value < 2 ** 63), 'Out-of-range ' + field)
    return value


def repair_field_coverage(rows, field):
    """Across-house summaries of final totals; raw Q32 means remain exact rationals."""
    values = [row.get(field) for row in rows if row.get(field) is not None]
    total = sum(values) if values else None
    return {'observed': len(values), 'eligible': len(rows), 'missing': len(rows) - len(values),
            'mean': statistics.mean(values) if values and '_raw' not in field else None,
            'mean_exact': {'numerator': total, 'denominator': len(values)} if values else None,
            'total': total, 'min': min(values) if values else None, 'max': max(values) if values else None,
            'unit': REPAIR_QUALITY_UNITS[field], 'aggregation': 'final per-house ledger, never decision mean'}


def repair_measurements(data):
    """Validate one ledger per house-game; retain censored observations as such.

    Sparse gaps between decisions do not lose cumulative exposure. A missing final,
    changed baseline/session, clipped stream or decreasing counter cannot supply a
    complete whole-game total. No per-job event count substitutes for paid service.
    """
    groups = defaultdict(list)
    for row in data.get('repairs', []):
        groups[(row['id'], row['house'])].append(row)
    result = {}
    for game, house in selected_houses(data):
        identity = (house['id'], house['house'])
        rows = groups.get(identity, [])
        reasons, snapshots, finals = [], [], []
        invalid = missing = left = clipped = False
        previous, baseline, session = None, None, None
        def record_reason(reason):
            if reason not in reasons:
                reasons.append(reason)
        def order(row):
            try:
                return (repair_integer(row.get('seq'), 'seq') or 0, repair_integer(row.get('cycle'), 'cycle') or 0)
            except ValueError:
                return (0, 0)
        for row in sorted(rows, key=order):
            present = any(row.get(field) not in (None, '') for field in REPAIR_QUALITY_FIELDS)
            try:
                raw = json.loads(row.get('data') or '{}')
                if not isinstance(raw, dict):
                    invalid = True
                    record_reason('malformed_data')
                nested = raw.get('quality' if row.get('event', 'repair_capacity') == 'repair_capacity' else 'repair_quality') if isinstance(raw, dict) else None
                present |= isinstance(raw, dict) and ('quality' in raw or 'repair_quality' in raw)
                if isinstance(raw, dict) and ('quality' in raw or 'repair_quality' in raw) and not isinstance(nested, dict):
                    invalid = True
                    record_reason('malformed_quality')
            except (TypeError, ValueError):
                invalid = True
                record_reason('malformed_data')
            if not present:
                missing = True
                record_reason('missing_quality_snapshot')
                continue
            if finals:
                invalid = True
                record_reason('quality_after_final_ledger')
            values = {}
            for field, key in zip(REPAIR_QUALITY_FIELDS, REPAIR_QUALITY_KEYS):
                try:
                    value = repair_integer(row.get(field), field)
                except ValueError:
                    value, invalid = None, True
                    record_reason('invalid:' + key)
                values[field] = value
                if value is None:
                    missing = True
                    record_reason('missing:' + key)
            ledger = {key: values[field] for field, key in zip(REPAIR_QUALITY_FIELDS, REPAIR_QUALITY_KEYS)}
            if ledger['version'] not in (None, 1) or any(ledger[key] not in (None, 0, 1) for key in ('left_censored', 'capture_complete')):
                invalid = True
                record_reason('unsupported_version_or_censor_flag')
            left |= ledger['left_censored'] == 1
            if row.get('capture_status') != 'complete' or row.get('capture_reasons'):
                clipped = True
                record_reason('incomplete_capture')
            try:
                cycle = repair_integer(row.get('cycle'), 'cycle')
                seq = repair_integer(row.get('seq'), 'seq')
                start = repair_integer(row.get('capture_start_cycle'), 'capture_start_cycle')
                end = repair_integer(row.get('capture_end_cycle'), 'capture_end_cycle')
                require(cycle is not None and seq is not None and seq > 0, 'Missing record position')
                if start is None:
                    missing = True
                    record_reason('missing_capture_start')
                if end is None:
                    clipped = True
                    record_reason('missing_capture_end')
                if start is not None and end is not None and (start > end or cycle < start or cycle > end):
                    invalid = True
                    record_reason('invalid_capture_bounds')
                if ledger['end_cycle'] is not None and ledger['end_cycle'] != cycle:
                    invalid = True
                    record_reason('ledger_record_cycle_mismatch')
                if ledger['start_cycle'] is not None and ledger['end_cycle'] is not None and ledger['start_cycle'] > ledger['end_cycle']:
                    invalid = True
                    record_reason('invalid_ledger_window')
                if start is not None and ledger['start_cycle'] is not None and ledger['start_cycle'] != start:
                    left = True
                    record_reason('partial_start_window')
                if row.get('event') == 'game_summary':
                    finals.append(row)
                    if ledger['capture_complete'] != 1:
                        clipped = True
                        record_reason('ledger_capture_not_closed')
                    if any(ledger[key] is not None and ledger[key] > 0 for key in ('right_censored_jobs', 'right_censored_returns')):
                        clipped = True
                        record_reason('unfinished_jobs_or_returns')
                    if end is not None and cycle != end:
                        clipped = True
                        record_reason('short_final_window')
            except ValueError:
                invalid = True
                record_reason('invalid_record_or_capture_bounds')
            current_baseline = tuple(ledger[key] for key in ('start_cycle', 'left_censored', 'baseline_bays', 'baseline_pending_paid_raw'))
            if not row.get('session') or session is not None and session != row['session']:
                invalid = True
                record_reason('invalid_or_changed_session')
            session = row.get('session')
            if baseline is not None and baseline != current_baseline:
                invalid = True
                record_reason('changed_baseline')
            baseline = current_baseline
            partition = [ledger[key] for key in ('live_cycles_total', 'free_cycles_total', 'damaged_cycles_total', 'full_return_cycles_total')]
            if all(value is not None for value in partition) and partition[0] != sum(partition[1:]):
                invalid = True
                record_reason('invalid_bay_cycle_partition')
            if ledger['unfunded_cycles_total'] is not None and ledger['damaged_cycles_total'] is not None and ledger['unfunded_cycles_total'] > ledger['damaged_cycles_total']:
                invalid = True
                record_reason('unfunded_exceeds_damaged_cycles')
            if ledger['capital_refund_actual_raw_total'] is not None and ledger['capital_refund_requested_raw_total'] is not None and ledger['capital_refund_actual_raw_total'] > ledger['capital_refund_requested_raw_total']:
                invalid = True
                record_reason('actual_refund_exceeds_request')
            attempts = ledger['return_attempted_total']
            outcomes = [ledger[key] for key in ('delivered_total', 'return_aborted_total')]
            releases = [ledger[key] for key in ('released_ground_total', 'handoff_carrier_total')]
            if outcomes[0] is not None and all(value is not None for value in releases) and outcomes[0] > sum(releases):
                invalid = True
                record_reason('deliveries_exceed_successful_releases')
            if attempts is not None:
                if all(value is not None for value in outcomes) and sum(outcomes) > attempts:
                    invalid = True
                    record_reason('return_outcomes_exceed_attempts')
                if all(value is not None for value in releases) and sum(releases) > attempts:
                    invalid = True
                    record_reason('return_releases_exceed_attempts')
            if ledger['useful_delivered_total'] is not None and ledger['delivered_total'] is not None and ledger['useful_delivered_total'] > ledger['delivered_total']:
                invalid = True
                record_reason('useful_deliveries_exceed_deliveries')
            if previous is not None:
                for key in REPAIR_QUALITY_TOTAL_KEYS + ('end_cycle',):
                    if ledger[key] is not None and previous[key] is not None and ledger[key] < previous[key]:
                        invalid = True
                        record_reason('decreasing:' + key)
            previous = ledger
            snapshots.append(values)
        if game.get('error'):
            invalid = True
            record_reason('game_error')
        if len(finals) > 1:
            invalid = True
            record_reason('multiple_final_ledgers')
        if snapshots and not finals:
            record_reason('missing_final_ledger')
        if left:
            record_reason('left_censored')
        status = ('invalid' if invalid else 'unknown' if not snapshots else 'unmeasured' if missing
                  else 'left-censored' if left else 'right-censored' if clipped or not finals else 'measured')
        values = snapshots[-1] if snapshots else {field: None for field in REPAIR_QUALITY_FIELDS}
        exposed = values['repair_quality_damaged_cycles_total']
        useful = bool(status == 'measured' and all(values[field] is not None and values[field] > 0 for field in (
            'repair_quality_damaged_cycles_total', 'repair_quality_health_restored_raw_total',
            'repair_quality_service_paid_raw_total', 'repair_quality_completed_total', 'repair_quality_useful_delivered_total')))
        result[identity] = {'id': identity[0], 'house': identity[1], 'status': status, 'reasons': reasons,
                            'snapshots': len(snapshots), 'final_ledgers': len(finals), 'values': values,
                            'damaged_exposure_observed': exposed is not None and exposed > 0,
                            'useful_service_proven': useful}
    return result


def outcome(game, house):
    if game.get('error'):
        return 'invalid', None
    raw = number(game.get('winner_team'), 'winner_team')
    scored = number(game.get('scored_winner_team'), 'scored_winner_team')
    winner = scored if scored is not None else raw
    team = number(house.get('team'), 'team')
    if winner is None or team is None:
        return 'unmeasured', None
    require(winner >= 0 and winner.is_integer() and team > 0 and team.is_integer(), 'Invalid team result')
    return ('draw' if winner == 0 else 'win' if team == winner else 'loss'), dict(
        raw_winner_team=raw, scored_winner_team=scored, win_condition=game.get('win_condition') or 'unreported',
        engine_outcome=game.get('outcome') or 'unreported')


def survival(game, house):
    end = number(game.get('minutes'), 'minutes')
    eliminated = number(house.get('eliminated_s'), 'eliminated_s')
    if house.get('alive') == '1':
        return {'minutes': end, 'status': 'right-censored' if end is not None else 'unmeasured'}
    if house.get('alive') != '0' or eliminated is None or eliminated < 0 or end is None or eliminated > end * 60 + 1:
        return {'minutes': None, 'status': 'unmeasured', 'reason': 'missing/invalid elimination observation'}
    return {'minutes': eliminated / 60, 'status': 'observed-inactivity',
            'limit': 'First inactivity is not necessarily permanent defeat; deliveries can revive a house.'}


def stratum(game, house):
    return tuple(str(game.get(field) or 'unmeasured') for field in (
        'source_commit', 'source_fingerprint', 'options_hash', 'ai_config_hashes', 'mod', 'format',
        'difficulty', 'base', 'army', 'city', 'initial_tech', 'spice_level')) + (house.get('house', 'unmeasured'),)


def selected_houses(data):
    games = {row['id']: row for row in data['games']}
    for house in data['houses']:
        game = games.get(house['id'])
        if game is not None and (game.get('candidate_house', '') == '' or game['candidate_house'] == house['house']):
            yield game, house


def coverage(rows, metric):
    values = [number(row.get(metric), metric) for row in rows]
    values = [value for value in values if value is not None and value >= 0]
    return {'observed': len(values), 'eligible': len(rows), 'missing': len(rows) - len(values),
            'mean': statistics.mean(values) if values else None,
            'min': min(values) if values else None, 'max': max(values) if values else None}


def trace_costs(directories):
    summaries, total_bytes = [], 0
    for directory in directories:
        for path in sorted((Path(directory) / 'cluster-evidence').glob('*/trace-summary.json')):
            require(len(summaries) < MAX_SUMMARIES, 'Trace-summary bound reached; select a smaller diagnostic deck')
            summaries.append(compact_json(path, 50 * 1024))
            total_bytes += path.stat().st_size
    return {'trace_summaries': len(summaries), 'summary_bytes': total_bytes,
            'raw_trace_bytes': coverage(summaries, 'raw_trace_bytes'),
            'capture_missing_or_truncated': sum(bool(row.get('summary_truncated') or row.get('parse_errors')
                                                   or row.get('raw_trace_available') is not True) for row in summaries)}


def outcome_totals(data):
    selected = list(selected_houses(data))
    return {'engine_decided_games': sum(game.get('outcome') == 'decided' and not game.get('error') for game in data['games']),
            'engine_games': len(data['games']),
            'candidate_house_results': dict(Counter(outcome(game, house)[0] for game, house in selected)),
            'limit': 'Engine decided counts any winner; candidate-house scored wins are separate and are not wins against an old AI.'}


def phase_sample_coverage(data):
    losses = {(house['id'], house['house']) for game, house in selected_houses(data) if outcome(game, house)[0] == 'loss'}
    phases, observed = {}, set()
    for sample in data['samples']:
        identity = (sample['id'], sample['house'])
        if identity not in losses:
            continue
        minute = number(sample.get('minute'))
        if minute is None or minute < 0:
            continue
        phase = 'opening-0-10m' if minute < 10 else 'midgame-10-30m' if minute < 30 else 'late-30m+'
        entry = phases.setdefault(phase, {'samples': 0, 'house_games': set()})
        entry['samples'] += 1
        entry['house_games'].add(identity)
        observed.add(identity)
    return {'loss_house_games': len(losses), 'unsampled_loss_house_games': len(losses - observed),
            'phases': {phase: {'samples': entry['samples'], 'observed_loss_house_games': len(entry['house_games'])}
                       for phase, entry in sorted(phases.items())},
            'limit': 'Samples are repeated observations, not independent games; absent phase coverage is not absence of failure.'}


def complete_matching(matching):
    return bool(matching['matched'] and not matching['missing_metric'] and not matching['rejected']
                and not matching.get('partial_telemetry') and not matching['unmatched_control'] and not matching['unmatched_candidate'])


def actual_costs(data, directories, measured=None):
    measured = measured or {}
    wall = coverage(data['games'], 'wall_s')
    game_wall = sum(value for value in (number(row.get('wall_s')) for row in data['games']) if value is not None and value >= 0)
    preparation = number(measured.get('preparation_wall_s'))
    require(all(number(measured.get(field)) is None or number(measured[field]) >= 0
                for field in ('preparation_wall_s', 'game_cpu_s', 'build_cpu_s', 'queue_wait_s')), 'Negative measured cost')
    return {'game_wall_s': game_wall if wall['observed'] else None, 'game_wall_coverage': wall,
            **{field: number(measured.get(field)) for field in ('preparation_wall_s', 'game_cpu_s', 'build_cpu_s', 'queue_wait_s')},
            'useful_game_time_fraction': game_wall / (game_wall + preparation)
            if preparation is not None and wall['observed'] == wall['eligible'] and game_wall + preparation > 0 else None,
            'missing_cost_fields': [key for key in ('preparation_wall_s', 'game_cpu_s', 'build_cpu_s', 'queue_wait_s')
                                   if number(measured.get(key)) is None], **trace_costs(directories)}


def quality_report(data, directories, measured=None):
    selected = list(selected_houses(data))
    houses = [house for game, house in selected if not game.get('error')]
    ids = {(house['id'], house['house']) for house in houses}
    samples = [row for row in data['samples'] if (row['id'], row['house']) in ids]
    repairs = [row for row in data.get('repairs', []) if (row['id'], row['house']) in ids]
    stocks = [row for row in repairs if row.get('event', 'repair_capacity') == 'repair_capacity']
    measurements = repair_measurements(data)
    def final_values(house):
        entry = measurements[(house['id'], house['house'])]
        return entry['values'] if entry['status'] == 'measured' else {}
    finals = [final_values(house) for house in houses]
    fields = {field: repair_field_coverage(finals, field) if field in REPAIR_QUALITY_FIELDS else coverage(houses, field)
              for field in QUALITY_METRICS}
    sampled = {field: fields[field] if field in REPAIR_QUALITY_FIELDS
               else coverage(stocks if field in REPAIR_EVENT_FIELDS else samples, field) for field in QUALITY_METRICS}
    by_outcome = defaultdict(list)
    for game, house in selected:
        by_outcome[outcome(game, house)[0]].append(house)
    entries = list(measurements.values())
    complete = bool(entries) and all(entry['status'] == 'measured' for entry in entries)
    useful = any(entry['useful_service_proven'] for entry in entries)
    return {'schema': 'dunecity-quality-report/1', 'houses': len(houses),
            'outcomes': outcome_totals(data),
            'final_fields': fields, 'sample_fields': sampled,
            'by_outcome': {kind: {field: repair_field_coverage([final_values(house) for house in rows], field)
                                  if field in REPAIR_QUALITY_FIELDS else coverage(rows, field) for field in QUALITY_METRICS}
                           for kind, rows in sorted(by_outcome.items())},
            'sampled_house_games': len({(row['id'], row['house']) for row in samples}),
            'repair_observed_house_games': len({(row['id'], row['house']) for row in repairs}),
            'repair_capture_status': dict(Counter(row.get('capture_status', 'unreported') for row in repairs)),
            'repair_quality': {'eligible_house_games': len(entries),
                               'status': dict(Counter(entry['status'] for entry in entries)),
                               'house_games': entries, 'units': REPAIR_QUALITY_UNITS,
                               'measurement_ready': complete, 'useful_service_proven': useful,
                               'ready_for_quality_review': complete and useful,
                               'unexposed_house_games': sum(entry['status'] == 'measured' and not entry['damaged_exposure_observed'] for entry in entries)},
            'missing_repair_usage': [field for field in REPAIR_METRICS if not sampled[field]['observed']],
            'interpretation': ['Refinery fleet_bay_* and occupied_refinery_bays measure harvester unloading, never repair service.',
                               'Repair capacity stocks are sparse decisions; cumulative repair_quality counters use validated final ledgers only.',
                               'completed_waiting is healed stock awaiting release, not cumulative completed service.',
                               'Q32 money and HP are exact raw integers; raw means are exact numerator/denominator pairs in the stated unit.',
                               'Left/right-censored or invalid totals remain visible per house but cannot supply whole-game quality coverage.',
                               'Zero damaged exposure is not useful-service proof; positive paid healing, completion and useful physical delivery are required.',
                               'fleet_capital_* is replacement asset value, not actual paid capital; accepted orders are commitments.',
                               'Final/survivor means alone cannot establish opportunity cost. Missing demand/usage requires bounded diagnostics.',
                               'No global repair-yard cap follows from a missing or idle-capacity observation.'],
            'cost': actual_costs(data, directories, measured)}


def loss_audit(directories, baseline_label, expected_source=None):
    text(baseline_label, 'baseline label')
    data = dataset(directories)
    if expected_source:
        require(re.fullmatch(r'[0-9a-f]{40}', expected_source), 'Expected baseline source must be a full commit SHA')
    groups, records = defaultdict(list), []
    for game, house in selected_houses(data):
        result, semantics = outcome(game, house)
        source = game.get('source_commit', '')
        require(not expected_source or not source or source == expected_source, 'Result is not the requested baseline source')
        record = {'id': game['id'], 'house': house['house'], 'seed': game.get('seed'),
                  'result': result, 'semantics': semantics, 'survival': survival(game, house),
                  'source_verified': bool(re.fullmatch(r'[0-9a-f]{40}', source)
                                          and game.get('options_hash') and game.get('ai_config_hashes')
                                          and (not expected_source or source == expected_source)),
                  'trace_diagnostics': game.get('diagnostics') or None}
        records.append(record)
        groups[stratum(game, house)].append((game, house, record))
    causes = []
    for key, rows in groups.items():
        losses = [(g, h, r) for g, h, r in rows if r['result'] == 'loss']
        wins = [(g, h, r) for g, h, r in rows if r['result'] == 'win']
        for detector in DETECTORS:
            measured = [(g, h, r) for g, h, r in losses if h.get('eligible_' + detector) == '1'
                        and number(h.get(detector)) is not None]
            exposed = [(g, h, r) for g, h, r in measured if number(h[detector]) > 0]
            if not exposed:
                continue
            independent = {g.get('seed') or g['id'] for g, _, _ in exposed}
            counterexamples = [r for _, h, r in wins if h.get('eligible_' + detector) == '1'
                               and number(h.get(detector)) is not None]
            severity = [number(h.get('loss_value')) for _, h, _ in exposed if number(h.get('loss_value')) is not None]
            phase_counts = Counter()
            for _, house, _ in exposed:
                first = number(house.get('first_' + detector))
                phase_counts['unmeasured' if first is None or first < 0 else
                             'opening-0-10m' if first < 10 else 'midgame-10-30m' if first < 30 else 'late-30m+'] += 1
            causes.append({'mechanism': detector, 'population': key, 'eligible_losses': len(losses),
                           'measured_eligible_losses': len(measured), 'exposed_losses': len(exposed),
                           'independent_loss_clusters': len(independent),
                           'prevalence': len(exposed) / len(measured) if measured else None,
                           'severity_loss_value': statistics.median(severity) if severity else None,
                           'representative_losses': [r for _, _, r in exposed[:2]],
                           'successful_counterexamples': counterexamples[:2],
                           'first_failure_phase_counts': dict(phase_counts),
                           'phase_loss_denominator': len(losses),
                           'confidence': 'observational; action/cause must be checked in bounded node excerpts',
                           'actionable_for_review': len(independent) >= 2 and bool(counterexamples)
                           and all(r['source_verified'] for _, _, r in exposed)})
    causes.sort(key=lambda row: (row['actionable_for_review'], row['exposed_losses'], row['severity_loss_value'] or 0), reverse=True)
    shortlisted = [row for row in causes if row['actionable_for_review']][:3]
    return {'schema': 'dunecity-loss-audit/1', 'baseline': baseline_label,
            'source_requested': expected_source, 'outcomes': dict(Counter(row['result'] for row in records)),
            'eligible_house_games': len(records), 'identity_missing': sum(not row['source_verified'] for row in records),
            'populations': [{'identity': key, 'eligible': len(rows),
                             'outcomes': dict(Counter(row['result'] for _, _, row in rows))} for key, rows in sorted(groups.items())[:128]],
            'omitted_population_count': max(0, len(groups) - 128),
            'outcome_semantics': outcome_totals(data),
            'phase_sample_coverage': phase_sample_coverage(data),
            'ranked_causes': shortlisted, 'unselected': [dict(mechanism=row['mechanism'],
                reason='lower practical priority' if row['actionable_for_review'] else
                'missing repeated independent losses, verified baseline identity or successful counterexamples')
                for row in causes if row not in shortlisted][:12],
            'missing_diagnostic': None if shortlisted else
                'Inspect bounded matched loss/success summaries and exact node events; no quota of new ideas.',
            'quality': quality_report(data, directories),
            'limits': ['No random 25% FFA baseline is assumed; asymmetric seats and opponents remain stratified.',
                       'Detector association is a review lead, not a proven cause or permission to implement.',
                       'Raw winner, scored population winner and inactivity/censoring remain separate.']}


def clustered_effect(values, design, minimum_effect, planned_clusters=None, diagnostic=False):
    """Independent clusters; observed variance plans future work, never current power."""
    if diagnostic:
        clusters = defaultdict(list)
        for key, value in values:
            clusters[key].append(value)
        means = [statistics.mean(rows) for _, rows in sorted(clusters.items())]
        return {'effect': statistics.mean(means) if means else None, 'clusters': len(means),
                'paired_observations': len(values), 'interval': None, 'power_sufficient': False,
                'descriptive': True,
                'limits': 'Targeted diagnostic: one original-world OFF/ON observation per case. Descriptive only; no confidence, power, general-strength or promotion claim.'}
    sd = preregistered_cluster_sd(design)
    require(planned_clusters is None or type(planned_clusters) is int and planned_clusters >= 0,
            'Invalid frozen cluster budget')
    clusters = defaultdict(list)
    for key, value in values:
        clusters[key].append(value)
    means = [statistics.mean(rows) for _, rows in sorted(clusters.items())]
    alpha = design['alpha'] / design['finalists']
    interval = None
    if len(means) >= 2:
        rng = random.Random(0)
        draws = sorted(statistics.mean(rng.choices(means, k=len(means))) for _ in range(4000))
        interval = [draws[int(4000 * alpha / 2)], draws[min(3999, math.ceil(4000 * (1 - alpha / 2)) - 1)]]
    observed_sd = statistics.stdev(means) if len(means) >= 2 else None
    z = NormalDist().inv_cdf(1 - alpha / 2) + NormalDist().inv_cdf(design['power'])
    required = max(2, math.ceil((z * sd / minimum_effect) ** 2)) if sd is not None else None
    exploratory = max(2, math.ceil((z * observed_sd / minimum_effect) ** 2)) \
        if observed_sd is not None and observed_sd > 0 else None
    return {'effect': statistics.mean(means) if means else None, 'interval': interval,
            'confidence': 1 - alpha, 'clusters': len(means), 'paired_observations': len(values),
            'observed_cluster_sd': observed_sd,
            'exploratory_future_clusters_normal_approximation': exploratory,
            'preregistered_cluster_sd': sd, 'frozen_cluster_budget': planned_clusters,
            'planned_clusters_normal_approximation': required,
            'power_sufficient': bool(required is not None and planned_clusters is not None
                                     and planned_clusters >= required and len(means) == planned_clusters),
            'limits': 'Normal-approximation planning only. Decision power requires preregistered variance and the completed frozen cluster budget; observed SD/future N are exploratory, never retrospective power. A flat small pilot does not prove no effect.'}


def measured_metric(data, metric, aggregation='final'):
    """Reduce decision rows once per house/game; never count build passes as trials."""
    selected = list(selected_houses(data))
    if metric in REPAIR_QUALITY_FIELDS:
        require(aggregation == 'final', 'Repair quality requires final cumulative totals, never decision-stock aggregation')
        return {key: (entry['values'][metric] if entry['status'] not in ('unknown', 'invalid') else None,
                      entry['status'] != 'measured') for key, entry in repair_measurements(data).items()}
    if aggregation == 'final':
        return {(house['id'], house['house']): (number(house.get(metric)), False) for _, house in selected}
    groups = defaultdict(list)
    for row in data.get('repairs', []):
        if row.get('event', 'repair_capacity') == 'repair_capacity':
            groups[(row['id'], row['house'])].append(row)
    result = {}
    for _, house in selected:
        key = (house['id'], house['house'])
        rows = groups.get(key, [])
        observed = [(row, number(row.get(metric))) for row in rows]
        values = [value for _, value in observed if value is not None and value >= 0]
        partial = not rows or any(row.get('capture_status') != 'complete' or value is None or value < 0
                                  for row, value in observed)
        if not values:
            result[key] = (None, True)
            continue
        last = max(rows, key=lambda row: (number(row['cycle']), number(row['seq'])))
        value = max(values) if aggregation == 'decision_max' else statistics.mean(values) if aggregation == 'decision_mean' else number(last.get(metric))
        result[key] = (value, partial)
    return result


def arm_identity(manifest, data, game, arm, cases):
    """Verify actual transport receipts, not requested labels or global deltas.

    SCENARIO_AI_CONFIG has no controller index. Duplicate house rows therefore
    cannot verify multiple controllers; never collapse them into a last-wins map.
    This verifies observed transport only, not the intended mechanism's force value.
    """
    selected = game.get('candidate_house')
    seat = game.get('candidate_seat')
    try:
        index = int(game.get('sample_case', '')) - 1
        if index < 0 or index >= len(cases):
            return 'planned-case'
        case = cases[index]
        if any(str(game.get(field, '')) != str(case[field]) for field in
               ('seed', 'format', 'size', 'spice_level', 'initial_tech')):
            return 'planned-case'
        if game.get('difficulty') != case.get('difficulty', 'brutal'):
            return 'planned-case'
        count = {'1v1': 2, '2v2': 4, 'ffa4': 4, 'ffa6': 6}[case['format']]
        houses = ('0', '1', '2', '4', '3', '5')[:count]
        shift = index % count
        houses = houses[shift:] + houses[:shift]
        if manifest.get('purpose') == 'diagnostic':
            if selected != str(manifest['diagnostic_house']) or selected not in houses or seat != 'target-house-' + selected:
                return 'candidate-seat'
            target_tier = 'brutal' if selected == houses[0] else case.get('difficulty', 'brutal')
            phase_difficulty = str(('easy', 'medium', 'hard', 'brutal').index(target_tier))
        else:
            position = 0 if seat == 'original' else (2 if case['format'] == '2v2' else count - 1) if seat == 'swapped' else None
            if position is None or selected != houses[position]:
                return 'candidate-seat'
            phase_difficulty = '3'
        native = game.get('config_hash')
        if not native:
            return 'config-unverified'
        if (game.get('config_hash') != native or game.get('setup_style') != 'default'
                or game.get('style') != 'default'):
            return 'global-config'
        configs = json.loads(game.get('ai_config_receipts') or 'null')
        phases = json.loads(game.get('genome_phase_receipts') or 'null')
        if not isinstance(configs, list) or not isinstance(phases, list):
            return 'controller-unverified'
        observed = {row['house']: row['config_hash'] for row in configs}
        if len(configs) != count or set(observed) != set(houses) or not all(observed.values()):
            return 'controller-unverified'
        effective = observed[selected]
        expected = {house: effective if house == selected else native for house in houses}
        if arm == 'off' and effective != native:
            return 'effective-config'
        if observed != expected:
            return 'effective-config'
        legacy = [part.split(':') for part in game.get('ai_config_hashes', '').split(',')]
        if len(legacy) != count or dict(legacy) != observed:
            return 'effective-config'
        style = manifest['configs'][arm]
        if style == 'control':
            if phases or effective != native:
                return 'phase-identity'
        elif (len(phases) != 1 or any(phases[0].get(field) != value for field, value in
                (('house', selected), ('genome', 'candidate-' + style), ('phase', 'opening'),
                 ('cycle', '0'), ('controller', '0'), ('difficulty', phase_difficulty), ('config_hash', effective)))):
            return 'phase-identity'
        roster = [row for row in data['houses'] if row['id'] == game['id']]
        if len(roster) != count or {row['house'] for row in roster} != set(houses):
            return 'controller-unverified'
        if any(row.get('seat_style') != (style if row['house'] == selected else 'control') for row in roster):
            return 'candidate-seat'
    except (ValueError, TypeError, KeyError, IndexError, AttributeError):
        return 'controller-unverified'
    return None


def actuation_identity(manifest, data, game, arm, base):
    """Preregister one independently calibrated hash per role, never infer from names.

    Optional manifest effective_config_hashes = {native, off, on} binds expected
    effective hashes to the frozen snapshot/configs. config_calibration_references
    retains the independent calibration receipts. Missing identities still allow
    observation/pilot admission and measured reports, but cannot verify actuation.
    Multiple-controller/phase heterogeneity is unverified by this simple contract.
    Repair treatment is proven by actual decisions, not a hash difference.
    """
    hashes = manifest.get('effective_config_hashes', {})
    references = manifest.get('config_calibration_references', [])
    if not hashes or not references or not all(resolve(base, ref).is_file() for ref in references):
        return False
    try:
        native, effective = hashes['native'], hashes[arm]
        if game.get('config_hash') != native or hashes['off'] != native or hashes['on'] == hashes['off']:
            return False
        selected = game['candidate_house']
        configs = json.loads(game['ai_config_receipts'])
        if any(row['config_hash'] != (effective if row['house'] == selected else native) for row in configs):
            return False
        phases = json.loads(game['genome_phase_receipts'])
        if manifest['configs'][arm] != 'control':
            if len(phases) != 1 or phases[0].get('config_hash') != effective:
                return False
        elif phases or effective != native:
            return False
        repair = manifest.get('purpose') == 'diagnostic' or any(specification['metric'] in REPAIR_METRICS + REPAIR_EVENT_FIELDS for specification in
                     [manifest[name] for name in ('opportunity', 'mechanism_metric', 'primary')] + manifest['guards'])
        if repair:
            for house in {row['house'] for row in configs}:
                rows = [row for row in data.get('repairs', []) if row['id'] == game['id'] and row['house'] == house
                        and row.get('event', 'repair_capacity') == 'repair_capacity']
                if (not rows or any(row.get('capture_status') != 'complete' for row in rows)
                        or any(not row.get('session') or not row.get('player') for row in rows)
                        or len({row['player'] for row in rows}) != 1):
                    return False
                wanted = int(arm == 'on' and house == selected)
                if any(number(row.get('repair_treatment')) != wanted for row in rows):
                    return False
        return True
    except (ValueError, TypeError, KeyError, IndexError, AttributeError):
        return False


def paired_metrics(manifest, control, candidate, metric, aggregation='final', cases=()):
    left = {(h['id'], h['house']): (g, h) for g, h in selected_houses(control)}
    right = {(h['id'], h['house']): (g, h) for g, h in selected_houses(candidate)}
    before_values = measured_metric(control, metric, aggregation)
    after_values = measured_metric(candidate, metric, aggregation)
    partial = 0
    values, rejected, missing = [], Counter(), 0
    for key in sorted(left.keys() & right.keys()):
        old, before = left[key]
        new, after = right[key]
        identities = ('source_commit', 'source_fingerprint', 'map_hash', 'content_hash', 'object_data_hash', 'options_hash', 'observer_mode')
        reason = next((field for field in identities if not old.get(field) or old.get(field) != new.get(field)), None)
        if reason or old.get('source_commit') != manifest['snapshot']['revision'] or old.get('error') or new.get('error'):
            rejected[reason or 'source/error'] += 1
            continue
        if old.get('candidate_style') != manifest['configs']['off'] or new.get('candidate_style') != manifest['configs']['on']:
            rejected['config-identity'] += 1
            continue
        reason = arm_identity(manifest, control, old, 'off', cases) or arm_identity(manifest, candidate, new, 'on', cases)
        if reason:
            rejected[reason] += 1
            continue
        if metric == 'won' and any(number(house.get('won')) is not None and
                                 number(house['won']) != int(outcome(game, house)[0] == 'win')
                                 for game, house in ((old, before), (new, after))):
            rejected['outcome-semantics'] += 1
            continue
        fields = manifest['design']['cluster_fields']
        if any(not old.get(field) or old.get(field) != new.get(field) for field in fields):
            rejected['cluster-identity'] += 1
            continue
        a, a_partial = before_values[key]
        b, b_partial = after_values[key]
        partial += int(a_partial or b_partial)
        if manifest.get('purpose') == 'diagnostic' and (a_partial or b_partial):
            missing += 1
            continue
        if a is None or b is None or a < 0 or b < 0:
            missing += 1
            continue
        values.append((tuple(old[field] for field in fields), b - a))
    return values, {'matched': len(left.keys() & right.keys()), 'unmatched_control': len(left.keys() - right.keys()),
                    'unmatched_candidate': len(right.keys() - left.keys()), 'missing_metric': missing,
                    'partial_telemetry': partial, 'rejected': dict(rejected)}


def decide(stage, facts):
    """Evidence dispositions are not queue status and never authorize default flips."""
    if facts.get('retired'):
        return 'retired'
    if facts.get('implementation_failed'):
        return 'implementation-failed'
    if facts.get('harm_demonstrated'):
        return 'harmful'
    if facts.get('opportunity_complete') and facts.get('opportunities') == 0:
        return 'opportunity-missing'
    if facts.get('mechanism_adequately_unsupported'):
        return 'mechanism-unsupported'
    if facts.get('diagnostic'):
        return 'inconclusive'
    if stage == 'confirmation' and all(facts.get(field) is True for field in (
            'independent_holdout', 'off_contract_verified', 'mechanism_verified', 'primary_confirmed',
            'power_sufficient', 'guards_complete')):
        return 'confirmed'
    return 'inconclusive'


def experiment_report(path, stage, control_directory, candidate_directory, evidence=None):
    path = Path(path)
    manifest = compact_json(path)
    provenance = validate_manifest(manifest, path.parent)
    require(stage in manifest['stages'], 'Stage not in preregistered manifest')
    diagnostic = manifest.get('purpose') == 'diagnostic'
    evidence = evidence or {}
    control, candidate = dataset([control_directory]), dataset([candidate_directory])
    planned_clusters = len(provenance['decks'][stage]['seeds'])
    expected_deck = compact_json(provenance['decks'][stage]['path'])
    reports = {}
    for name in ('opportunity', 'mechanism_metric', 'primary'):
        specification = manifest[name]
        values, matching = paired_metrics(manifest, control, candidate, specification['metric'], specification.get('aggregation', 'final'), expected_deck['cases'])
        if specification['direction'] == 'lower':
            values = [(key, -value) for key, value in values]
        reports[name] = dict(clustered_effect(values, manifest['design'], specification['minimum_effect'], planned_clusters, diagnostic), coverage=matching)
        if specification['metric'] in REPAIR_QUALITY_UNITS:
            reports[name]['unit'] = REPAIR_QUALITY_UNITS[specification['metric']]
    guards, harmful, guards_complete = {}, False, True
    for guard in manifest['guards']:
        values, matching = paired_metrics(manifest, control, candidate, guard['metric'], guard.get('aggregation', 'final'), expected_deck['cases'])
        if guard['direction'] == 'lower':
            values = [(key, -value) for key, value in values]
        result = clustered_effect(values, manifest['design'], max(guard['margin'], manifest['primary']['minimum_effect']), planned_clusters, diagnostic)
        result['coverage'] = matching
        guards[guard['metric']] = result
        if guard['metric'] in REPAIR_QUALITY_UNITS:
            result['unit'] = REPAIR_QUALITY_UNITS[guard['metric']]
        if diagnostic:
            result['observed_margin_breach'] = any(value < -guard['margin'] for _, value in values) if values else None
        guards_complete &= bool(result['interval'] and result['interval'][0] >= -guard['margin']
                                and complete_matching(matching))
        harmful |= bool(complete_matching(matching) and result['power_sufficient']
                        and result['interval'] and result['interval'][1] < -guard['margin'])
    opportunity_metric = manifest['opportunity']['metric']
    opportunity_values = measured_metric(control, opportunity_metric, manifest['opportunity'].get('aggregation', 'final'))
    opportunities = (repair_field_coverage([{opportunity_metric: value} for value, _ in opportunity_values.values()], opportunity_metric)
                     if opportunity_metric in REPAIR_QUALITY_FIELDS
                     else coverage([{'value': value} for value, _ in opportunity_values.values()], 'value'))
    opportunity_partial = any(partial for _, partial in opportunity_values.values())
    primary, mechanism = reports['primary'], reports['mechanism_metric']
    indices = provenance['decks'][stage].get('case_indices', list(range(1, len(expected_deck['cases']) + 1)))
    seats = ('target-house-' + str(manifest['diagnostic_house']),) if diagnostic else ('original', 'swapped')
    expected_seeds = Counter(str(expected_deck['cases'][index - 1]['seed']) for index in indices for _ in seats)
    expected_seats = Counter((str(index), seat) for index in indices for seat in seats)
    deck_complete = all(Counter(game.get('seed') for game, _ in selected_houses(data)) == expected_seeds
                        and Counter((game.get('sample_case'), game.get('candidate_seat'))
                                    for game, _ in selected_houses(data)) == expected_seats
                        and not any(game.get('error') for game in data['games']) for data in (control, candidate))
    def attested(name):
        record = evidence.get(name, {})
        if not isinstance(record, dict) or record.get('passed') is not True:
            return False
        refs = record.get('references', [])
        return bool(refs) and all(resolve(path.parent, ref).is_file() for ref in refs)
    transport_verified = bool(deck_complete and all(
        arm_identity(manifest, data, game, arm, expected_deck['cases']) is None
        for arm, data in (('off', control), ('on', candidate)) for game in data['games']))
    actuation_verified = bool(transport_verified and all(
        actuation_identity(manifest, data, game, arm, path.parent)
        for arm, data in (('off', control), ('on', candidate)) for game in data['games']))
    quality = {'off': quality_report(control, [control_directory]),
               'on': quality_report(candidate, [candidate_directory], evidence.get('cost'))}
    quality_complete = all(report['repair_quality']['measurement_ready'] for report in quality.values())
    useful_service = quality['on']['repair_quality']['useful_service_proven']
    facts = {'transport_verified': transport_verified, 'actuation_verified': actuation_verified,
             'opportunity_complete': actuation_verified and deck_complete and not opportunity_partial
             and complete_matching(reports['opportunity']['coverage'])
             and opportunities['observed'] == opportunities['eligible'] and opportunities['eligible'] > 0,
             'opportunities': opportunities['max'], 'implementation_failed': attested('implementation_failed'),
             'harm_demonstrated': bool(actuation_verified and deck_complete and (harmful or complete_matching(primary['coverage'])
                 and primary['power_sufficient'] and primary['interval'] and primary['interval'][1] < 0)),
             'mechanism_adequately_unsupported': bool(actuation_verified and deck_complete and complete_matching(mechanism['coverage'])
                 and mechanism['power_sufficient'] and mechanism['interval']
                 and mechanism['interval'][1] < manifest['mechanism_metric']['minimum_effect']),
             'independent_holdout': stage == 'confirmation' and deck_complete and attested('independent_holdout'),
             'off_contract_verified': attested('off_contract'),
             'mechanism_verified': bool(actuation_verified and attested('mechanism') and complete_matching(mechanism['coverage'])
                 and mechanism['interval'] and mechanism['interval'][0] > 0
                 and mechanism['effect'] >= manifest['mechanism_metric']['minimum_effect']),
             'primary_confirmed': bool(actuation_verified and deck_complete and complete_matching(primary['coverage'])
                 and primary['interval'] and primary['interval'][0] > 0
                 and primary['effect'] >= manifest['primary']['minimum_effect']),
             'power_sufficient': primary['power_sufficient'], 'guards_complete': bool(guards_complete),
             'quality_measurements_complete': quality_complete, 'useful_repair_service_proven': useful_service,
             'quality_reviewed': attested('quality_review') and quality_complete and useful_service,
             'promotion_controls_passed': not diagnostic and all(attested('control:' + name) for name in manifest.get('promotion_controls', []))}
    if diagnostic:
        facts['diagnostic'] = True
        facts['opportunity_complete'] &= quality_complete
    status = decide(stage, facts)
    facts['promotion_ready'] = bool(status == 'confirmed' and facts['quality_reviewed'] and facts['promotion_controls_passed'])
    return {'schema': 'dunecity-experiment-report/1', 'id': manifest['id'], 'stage': stage,
            'status': status, 'provenance': provenance, 'facts': facts,
            **({'purpose': 'diagnostic', 'diagnostic_house': manifest['diagnostic_house'],
                'observations_per_case_per_arm': 1, 'expected_games_per_arm': len(indices),
                'interpretation': 'Targeted descriptive OFF/ON diagnostic only; never independent confirmation or promotion.'} if diagnostic else {}),
            'metrics': reports, 'guards': guards, 'opportunity_coverage': opportunities,
            'quality': quality,
            'negative_learning': manifest['negative_learning'],
            'helped_examples': evidence.get('helped_examples', []), 'harmed_examples': evidence.get('harmed_examples', []),
            'recommendation': 'Review mechanism, missing coverage and independent confirmation; default changes remain an explicit integrator decision.',
            'missing_attestations': [key for key in ('independent_holdout', 'off_contract_verified', 'mechanism_verified',
                                                    'quality_measurements_complete', 'useful_repair_service_proven',
                                                    'quality_reviewed', 'promotion_controls_passed', 'actuation_verified') if not facts[key]]}


def quality_comparison(control_directory, candidate_directory):
    """Describe retained winner bundles without pooling colliding OFF/ON game IDs."""
    control, candidate = dataset([control_directory]), dataset([candidate_directory])
    measurements = [repair_measurements(data) for data in (control, candidate)]
    arms = [{(house['id'], house['house']): dict(house, **(
                measurements[index][(house['id'], house['house'])]['values']
                if measurements[index][(house['id'], house['house'])]['status'] == 'measured'
                else dict.fromkeys(REPAIR_QUALITY_FIELDS))) for _, house in selected_houses(data)}
            for index, data in enumerate((control, candidate))]
    shared = sorted(arms[0].keys() & arms[1].keys())
    changes = {}
    for metric in QUALITY_METRICS:
        pairs = [(arms[0][key].get(metric), arms[1][key].get(metric)) if metric in REPAIR_QUALITY_FIELDS
                 else (number(arms[0][key].get(metric)), number(arms[1][key].get(metric))) for key in shared]
        measured = [(before, after) for before, after in pairs
                    if before is not None and after is not None and before >= 0 and after >= 0]
        changes[metric] = {'observed_pairs': len(measured), 'eligible_pairs': len(shared),
                           'mean_delta': statistics.mean(after - before for before, after in measured)
                           if measured and not (metric in REPAIR_QUALITY_FIELDS and '_raw' in metric) else None}
        if metric in REPAIR_QUALITY_FIELDS:
            total = sum(after - before for before, after in measured) if measured else None
            changes[metric].update(unit=REPAIR_QUALITY_UNITS[metric], aggregation='validated final per-house ledger',
                                   total_delta=total, mean_delta_exact={'numerator': total, 'denominator': len(measured)} if measured else None)
    identities = {}
    for arm, data in (('control', control), ('candidate', candidate)):
        populations = sorted({stratum(game, house) for game, house in selected_houses(data)})
        identities[arm] = {'populations': populations[:128], 'omitted': max(0, len(populations) - 128)}
    return {'schema': 'dunecity-quality-comparison/1', 'matched_house_games': len(shared),
            'unmatched_control': len(arms[0].keys() - arms[1].keys()),
            'unmatched_candidate': len(arms[1].keys() - arms[0].keys()),
            'identities': identities, 'final_stock_changes': changes,
            'control': quality_report(control, [control_directory]),
            'candidate': quality_report(candidate, [candidate_directory]),
            'limits': ['Descriptive retained evidence, not a new independent confirmation or permission to promote.',
                       'Identical game IDs alone do not establish source/map/config equality.',
                       'A blank repair-usage column cannot establish zero waste; inspect time-aligned demand and service.']}
