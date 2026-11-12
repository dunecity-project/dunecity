"""Read-only loss, quality and clustered experiment reports for dunecity-audit.

Manifest/config values are run inputs, not build identities. These reports never
submit games, build source, change defaults or infer repair use from refinery use.
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
REPAIR_METRICS = ('repair_yards', 'repair_yards_built', 'repair_yards_queued',
                  'repair_actual', 'repair_committed', 'repair_busy', 'repair_completed_waiting',
                  'repair_serviceable_waiting', 'repair_transport_blocked', 'repair_transport_inflight', 'repair_access_blocked',
                  'repair_usable_local', 'repair_busy_local', 'repair_waiting_local',
                  'repair_pressure_cycles', 'repair_target', 'repair_completed_service',
                  'repair_health_restored', 'repair_paid_capital', 'repair_idle_capacity_cycles')
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
    require(baseline.get('independent') is True, 'Loss baseline must be independent of policy tuning')
    for field in ('observed_failure', 'successful_counterexamples', 'negative_learning'):
        require(isinstance(manifest.get(field), list), 'Missing evidence list: ' + field)
    require(manifest['observed_failure'] and manifest['successful_counterexamples'],
            'Selection needs observed losses and successful counterexamples')
    for arm in ('off', 'on'):
        text(manifest.get('configs', {}).get(arm), 'configs.' + arm)
    require(manifest['configs']['off'] != manifest['configs']['on'], 'OFF and ON configurations must differ')
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
    guards = manifest.get('guards')
    require(isinstance(guards, list), 'Missing harm guards')
    for guard in guards:
        text(guard.get('metric'), 'guard.metric')
        require(guard.get('direction') in ('higher', 'lower'), 'Invalid guard direction')
        margin = number(guard.get('margin'), 'guard.margin')
        require(margin is not None and margin >= 0, 'Invalid harm margin')
    design = manifest.get('design', {})
    clusters = design.get('cluster_fields')
    require(isinstance(clusters, list) and 'seed' in clusters and all(
        field in ('seed', 'map_seed', 'mod', 'sample_case', 'map_hash') for field in clusters),
        'Cluster related seats/maps/seeds; house or seat is not an independent cluster')
    alpha, power = number(design.get('alpha')), number(design.get('power'))
    require(alpha is not None and 0 < alpha < .5 and power is not None and .5 < power < 1,
            'Invalid declared error/power budget')
    require(type(design.get('finalists')) is int and design['finalists'] >= 1, 'Declare finalists actually confirmed')
    require(design.get('stopping') == 'fixed-budget', 'Declare fixed budget; no unplanned repeated peeking')
    preregistered_cluster_sd(design)
    budget = manifest.get('budget', {})
    require(type(budget.get('max_games')) is int and budget['max_games'] > 0, 'Missing game budget')
    require(number(budget.get('max_wall_s')) is not None and budget['max_wall_s'] > 0, 'Missing wall-time budget')
    require(isinstance(manifest.get('promotion_controls'), list) and manifest['promotion_controls'],
            'Retain applicable approved promotion controls')
    stages = manifest.get('stages', {})
    require('pilot' in stages and 'confirmation' in stages, 'Pilot and independent confirmation decks required')
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
        require(len(argv) % 2 == 0 and all(argv[index] in allowed and not argv[index + 1].startswith('--')
                for index in range(0, len(argv), 2)), 'Only explicit audit run arguments are allowed')
        require(len(set(argv[::2])) == len(argv[::2]), 'Repeated run argument')
        require('--mode' in argv and argv[argv.index('--mode') + 1] == 'sample'
                and '--mod' in argv and argv[argv.index('--mod') + 1] in ('vanilla', 'dunecity'),
                'Experiment uses one explicit content population and the existing sample audit path')
        decks[stage] = dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                            seeds=sorted(seeds), case_count=len(cases))
        total_games += len(cases) * 4  # Two swapped seats in each OFF/ON arm.
    require(total_games <= budget['max_games'], 'Preregistered decks exceed total game budget')
    require(budget['max_wall_s'] >= total_games, 'Wall budget must allow at least one second per game')
    require(not (set(decks['confirmation']['seeds']) & tuning), 'Confirmation reuses tuning seed/map clusters')
    return {'schema': 'dunecity-experiment-provenance/1', 'manifest_sha256': digest(manifest),
            'source_revision': revision, 'configs': manifest['configs'], 'decks': decks,
            'build_identity_inputs': ['source', 'harness', 'assets', 'toolchain', 'platform', 'build_options'],
            'config_values_are_run_inputs': True}


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
    """Retain sparse decision stocks; no missing event becomes zero service."""
    rows = []
    for event in capture.get('decisions', []):
        if event.get('event') != 'repair_capacity':
            continue
        row = {field: event.get(field, '') for field in ('session', 'seq', 'cycle', 'house')}
        row['capture_status'] = capture['status']
        for field in REPAIR_EVENT_FIELDS:
            value = event.get('data', {}).get(field.removeprefix('repair_'), '')
            row[field] = int(value) if isinstance(value, bool) else value
        rows.append(row)
    return rows


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
    fields = {field: coverage(houses, field) for field in QUALITY_METRICS}
    sampled = {field: coverage(repairs if field in REPAIR_EVENT_FIELDS else samples, field)
               for field in QUALITY_METRICS}
    by_outcome = defaultdict(list)
    for game, house in selected:
        by_outcome[outcome(game, house)[0]].append(house)
    return {'schema': 'dunecity-quality-report/1', 'houses': len(houses),
            'outcomes': outcome_totals(data),
            'final_fields': fields, 'sample_fields': sampled,
            'by_outcome': {kind: {field: coverage(rows, field) for field in QUALITY_METRICS}
                           for kind, rows in sorted(by_outcome.items())},
            'sampled_house_games': len({(row['id'], row['house']) for row in samples}),
            'repair_observed_house_games': len({(row['id'], row['house']) for row in repairs}),
            'repair_capture_status': dict(Counter(row.get('capture_status', 'unreported') for row in repairs)),
            'missing_repair_usage': [field for field in REPAIR_METRICS if not sampled[field]['observed']],
            'interpretation': ['Refinery fleet_bay_* and occupied_refinery_bays measure harvester unloading, never repair service.',
                               'Repair capacity rows are sparse decision-time stocks, not continuous occupancy or service completions.',
                               'completed_waiting is healed stock awaiting release, not cumulative completed service.',
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


def clustered_effect(values, design, minimum_effect, planned_clusters=None):
    """Independent clusters; observed variance plans future work, never current power."""
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
    if aggregation == 'final':
        return {(house['id'], house['house']): (number(house.get(metric)), False) for _, house in selected}
    groups = defaultdict(list)
    for row in data.get('repairs', []):
        value = number(row.get(metric))
        if value is not None and value >= 0:
            groups[(row['id'], row['house'])].append(row)
    result = {}
    for _, house in selected:
        key = (house['id'], house['house'])
        rows = groups.get(key, [])
        values = [number(row[metric]) for row in rows]
        if not values:
            result[key] = (None, True)
            continue
        value = max(values) if aggregation == 'decision_max' else statistics.mean(values) if aggregation == 'decision_mean' else number(
            max(rows, key=lambda row: (number(row['cycle']), number(row['seq'])))[metric])
        result[key] = (value, any(row.get('capture_status') != 'complete' for row in rows))
    return result


def paired_metrics(manifest, control, candidate, metric, aggregation='final'):
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
    evidence = evidence or {}
    control, candidate = dataset([control_directory]), dataset([candidate_directory])
    planned_clusters = len(provenance['decks'][stage]['seeds'])
    reports = {}
    for name in ('opportunity', 'mechanism_metric', 'primary'):
        specification = manifest[name]
        values, matching = paired_metrics(manifest, control, candidate, specification['metric'], specification.get('aggregation', 'final'))
        if specification['direction'] == 'lower':
            values = [(key, -value) for key, value in values]
        reports[name] = dict(clustered_effect(values, manifest['design'], specification['minimum_effect'], planned_clusters), coverage=matching)
    guards, harmful, guards_complete = {}, False, True
    for guard in manifest['guards']:
        values, matching = paired_metrics(manifest, control, candidate, guard['metric'], guard.get('aggregation', 'final'))
        if guard['direction'] == 'lower':
            values = [(key, -value) for key, value in values]
        result = clustered_effect(values, manifest['design'], max(guard['margin'], manifest['primary']['minimum_effect']), planned_clusters)
        result['coverage'] = matching
        guards[guard['metric']] = result
        guards_complete &= bool(result['interval'] and result['interval'][0] >= -guard['margin']
                                and complete_matching(matching))
        harmful |= bool(complete_matching(matching) and result['power_sufficient']
                        and result['interval'] and result['interval'][1] < -guard['margin'])
    opportunity_values = measured_metric(control, manifest['opportunity']['metric'], manifest['opportunity'].get('aggregation', 'final'))
    opportunities = coverage([{'value': value} for value, _ in opportunity_values.values()], 'value')
    opportunity_partial = any(partial for _, partial in opportunity_values.values())
    primary, mechanism = reports['primary'], reports['mechanism_metric']
    expected_deck = compact_json(provenance['decks'][stage]['path'])
    expected_seeds = Counter(str(case['seed']) for case in expected_deck['cases'] for _ in range(2))
    deck_complete = all(Counter(game.get('seed') for game, _ in selected_houses(data)) == expected_seeds
                        and not any(game.get('error') for game in data['games']) for data in (control, candidate))
    def attested(name):
        record = evidence.get(name, {})
        if not isinstance(record, dict) or record.get('passed') is not True:
            return False
        refs = record.get('references', [])
        return bool(refs) and all(resolve(path.parent, ref).is_file() for ref in refs)
    facts = {'opportunity_complete': deck_complete and not opportunity_partial
             and complete_matching(reports['opportunity']['coverage'])
             and opportunities['observed'] == opportunities['eligible'] and opportunities['eligible'] > 0,
             'opportunities': opportunities['max'], 'implementation_failed': attested('implementation_failed'),
             'harm_demonstrated': bool(deck_complete and (harmful or complete_matching(primary['coverage'])
                 and primary['power_sufficient'] and primary['interval'] and primary['interval'][1] < 0)),
             'mechanism_adequately_unsupported': bool(deck_complete and complete_matching(mechanism['coverage'])
                 and mechanism['power_sufficient'] and mechanism['interval']
                 and mechanism['interval'][1] < manifest['mechanism_metric']['minimum_effect']),
             'independent_holdout': stage == 'confirmation' and deck_complete and attested('independent_holdout'),
             'off_contract_verified': attested('off_contract'),
             'mechanism_verified': bool(attested('mechanism') and complete_matching(mechanism['coverage'])
                 and mechanism['interval'] and mechanism['interval'][0] > 0
                 and mechanism['effect'] >= manifest['mechanism_metric']['minimum_effect']),
             'primary_confirmed': bool(deck_complete and complete_matching(primary['coverage'])
                 and primary['interval'] and primary['interval'][0] > 0
                 and primary['effect'] >= manifest['primary']['minimum_effect']),
             'power_sufficient': primary['power_sufficient'], 'guards_complete': bool(guards_complete),
             'quality_reviewed': attested('quality_review'),
             'promotion_controls_passed': all(attested('control:' + name) for name in manifest['promotion_controls'])}
    return {'schema': 'dunecity-experiment-report/1', 'id': manifest['id'], 'stage': stage,
            'status': decide(stage, facts), 'provenance': provenance, 'facts': facts,
            'metrics': reports, 'guards': guards, 'opportunity_coverage': opportunities,
            'quality': {'off': quality_report(control, [control_directory]),
                        'on': quality_report(candidate, [candidate_directory], evidence.get('cost'))},
            'negative_learning': manifest['negative_learning'],
            'helped_examples': evidence.get('helped_examples', []), 'harmed_examples': evidence.get('harmed_examples', []),
            'recommendation': 'Review mechanism, missing coverage and independent confirmation; default changes remain an explicit integrator decision.',
            'missing_attestations': [key for key in ('independent_holdout', 'off_contract_verified', 'mechanism_verified',
                                                    'quality_reviewed', 'promotion_controls_passed') if not facts[key]]}


def quality_comparison(control_directory, candidate_directory):
    """Describe retained winner bundles without pooling colliding OFF/ON game IDs."""
    control, candidate = dataset([control_directory]), dataset([candidate_directory])
    arms = [{(house['id'], house['house']): house for _, house in selected_houses(data)}
            for data in (control, candidate)]
    shared = sorted(arms[0].keys() & arms[1].keys())
    changes = {}
    for metric in QUALITY_METRICS:
        pairs = [(number(arms[0][key].get(metric)), number(arms[1][key].get(metric))) for key in shared]
        measured = [(before, after) for before, after in pairs
                    if before is not None and after is not None and before >= 0 and after >= 0]
        changes[metric] = {'observed_pairs': len(measured), 'eligible_pairs': len(shared),
                           'mean_delta': statistics.mean(after - before for before, after in measured) if measured else None}
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
