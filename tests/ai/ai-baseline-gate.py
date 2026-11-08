#!/usr/bin/env python3
"""Immutable-evidence, independent-objective AI baseline gate (Python 3.9 stdlib).

Inputs are the normalized observations emitted by ai_baseline_scorecard.py. No
engine is launched, no historical release is reinterpreted as a research-ON arm,
and no scalar fitness or point-estimate-only acceptance rule is used. See the
parent baseline-league documentation for the frozen reference acquisition command.
"""
import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import csv
import gzip
import hashlib
import io
import json
import math
from pathlib import Path
import random
import re
import statistics
import sys

from ai_baseline_scorecard import COLUMNS, CORE_FAMILIES, METRIC_DEFINITIONS, SCHEMA_VERSION


SAFETY_MIN_SEEDS = 8
CONFIDENCE = 0.95
SUMMARY_MINUTE = -1.0
FINAL_ARMY_SHARE_METRICS = frozenset((
    'devastator_live_value_share', 'deviator_live_value_share', 'sonic_tank_live_value_share'))
STATUSES = ('measured', 'unavailable', 'censored', 'not_applicable')
GROUP_FIELDS = ('scope', 'mod', 'difficulty', 'research', 'context', 'house', 'metric', 'minute')
RECEIPT_FIELDS = ('source_commit', 'baseline_release_source', 'map_sha256', 'options_sha256',
                  'context_sha256', 'content_hash', 'object_data_hash')
SAME_SOURCE_FIELDS = ('source_commit', 'baseline_release_source', 'content_hash', 'object_data_hash')
METRIC_FIELDS = ('role', 'release', 'arm', 'scope', 'mod', 'difficulty', 'research', 'context',
                 'spice', 'format', 'house', 'metric', 'minute', 'direction', 'core',
                 'source_commits', 'baseline_release_sources', 'rows', 'measured', 'unavailable',
                 'censored', 'not_applicable', 'missing', 'total_seeds', 'independent_seeds',
                 'estimate', 'ci_low', 'ci_high', 'ci_status', 'status_reasons')
COMPARISON_FIELDS = ('comparison_id', 'kind', 'candidate_release', 'candidate_arm', 'reference_release',
                     'reference_arm', 'scope', 'mod', 'difficulty', 'research', 'context', 'spice',
                     'format', 'house', 'metric', 'minute', 'direction', 'core', 'gating', 'applicable',
                     'candidate_seeds', 'reference_seeds', 'paired_seed_count', 'candidate_estimate',
                     'candidate_ci_low', 'candidate_ci_high', 'reference_estimate', 'reference_ci_low',
                     'reference_ci_high', 'paired_delta', 'delta_ci_low', 'delta_ci_high',
                     'absolute_margin', 'relative_margin', 'margin', 'signed_benefit',
                     'noninferiority_slack', 'slack_ci_low', 'slack_ci_high', 'ci_status', 'status',
                     'coverage_errors', 'candidate_counts', 'reference_counts')
BEST_FIELDS = ('scope', 'mod', 'difficulty', 'research', 'context', 'spice', 'format', 'house', 'metric',
               'minute', 'direction', 'historical_releases', 'measured_references', 'reference_release',
               'reference_arm', 'reference_estimate', 'reference_ci_low', 'reference_ci_high',
               'candidate_estimate', 'candidate_ci_low', 'candidate_ci_high', 'paired_seed_count',
               'paired_delta', 'delta_ci_low', 'delta_ci_high', 'margin', 'slack_ci_low', 'slack_ci_high',
               'status', 'coverage_errors')


class GateError(ValueError):
    """A machine-readable input or contract failure, never a passing comparison."""

    def __init__(self, code, message, **details):
        super().__init__(message)
        self.code = code
        self.details = details

    def as_dict(self):
        return dict(code=self.code, message=str(self), **self.details)


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)


def _finite_mean(values):
    try:
        value = statistics.fmean(values)
    except (OverflowError, ValueError) as error:
        raise GateError('NONFINITE_DERIVED_METRIC', str(error)) from error
    if not math.isfinite(value):
        raise GateError('NONFINITE_DERIVED_METRIC', 'Nonfinite seed-block mean')
    return value


def scenario_id(identifier, seed):
    """Retain requested scenario; pool only the seed and known /swap orientation."""
    parts = identifier.split('/')
    if parts and parts[-1] == 'swap':
        parts.pop()
    # A seed can equal a map size or another numeric option. Remove only the
    # final matching component, never those earlier requested context fields.
    matches = [index for index, part in enumerate(parts) if part == seed]
    if matches:
        del parts[matches[-1]]
    return '/'.join(parts)


def scenario_dimensions(identifier):
    parts = identifier.split('/')
    if 'matrix' in parts:
        start = parts.index('matrix')
        if len(parts) > start + 4:
            return parts[start + 3], parts[start + 4]
    return '', ''


def _display_dimensions(rows):
    spice = {row['_spice'] for row in rows}
    formats = {row['_format'] for row in rows}
    return (next(iter(spice)) if len(spice) == 1 else 'all',
            next(iter(formats)) if len(formats) == 1 else 'all')


def _is_core(metric, minute):
    definition = METRIC_DEFINITIONS[metric]
    return definition['core'] and minute in definition.get('core_minutes', (SUMMARY_MINUTE,))


def _valid_not_applicable(row):
    """Only explicit native feature receipts can exempt a conditional objective."""
    if row['status'] != 'not_applicable':
        return False
    reason, metadata, payload = row['reason'].partition(';unsupported_requested_options=')
    if metadata:
        try:
            if not isinstance(json.loads(payload), dict):
                return False
        except (ValueError, TypeError):
            return False
    applicability = METRIC_DEFINITIONS[row['metric']]['applicability']
    return ((applicability == 'city' and reason == 'city_simulation_disabled')
            or (applicability == 'research_on' and row['research'] == 'off'
                and reason == 'research_off_or_unsupported_not_emulated')
            or (row['metric'] in FINAL_ARMY_SHARE_METRICS and row.get('minute') == SUMMARY_MINUTE
                and reason == 'no_live_army_denominator'))


def validate_rows(rows, role):
    """Validate every observation and deduplicate only byte-equivalent logical rows."""
    result, seen, duplicates = [], {}, 0
    for number, original in enumerate(rows, 2):
        row = {key: str(original.get(key, '') if original.get(key) is not None else '').strip()
               for key in COLUMNS}
        try:
            if int(row['schema_version']) != SCHEMA_VERSION:
                raise ValueError('unsupported schema_version')
            seed = int(row['seed'])
            if seed < 0:
                raise ValueError('negative seed')
            row['seed'] = str(seed)
            row['minute'] = float(row['minute'])
            if not math.isfinite(row['minute']):
                raise ValueError('nonfinite minute')
        except (ValueError, TypeError) as error:
            raise GateError('INVALID_OBSERVATION', str(error), role=role, row=number) from error
        for field in ('release', 'arm', 'id', 'mod', 'difficulty', 'research', 'house', 'team') + RECEIPT_FIELDS:
            if not row[field]:
                raise GateError('MISSING_IDENTITY', 'Missing ' + field, role=role, row=number, field=field)
        if row['research'] not in ('off', 'on'):
            raise GateError('INVALID_RESEARCH', 'Research must be explicitly off or on', role=role, row=number)
        if row['status'] not in STATUSES:
            raise GateError('INVALID_STATUS', 'Unknown observation status', role=role, row=number)
        if row['metric'] not in METRIC_DEFINITIONS:
            raise GateError('UNKNOWN_METRIC', 'Metric has no scorecard definition', metric=row['metric'])
        definition = METRIC_DEFINITIONS[row['metric']]
        if definition['direction'] not in ('higher', 'lower'):
            raise GateError('INVALID_METADATA', 'Metric direction must be higher or lower', metric=row['metric'])
        if row['value']:
            try:
                row['value'] = float(row['value'])
            except ValueError as error:
                raise GateError('INVALID_METRIC', 'Non-numeric metric', role=role, row=number) from error
            if not math.isfinite(row['value']):
                raise GateError('NONFINITE_METRIC', 'NaN and infinity are never evidence', role=role, row=number)
        else:
            row['value'] = None
        if row['status'] == 'measured' and row['value'] is None:
            raise GateError('MISSING_MEASURED_VALUE', 'Measured metric has no value', role=role, row=number)
        if row['research'] == 'on' and row['metric'] == 'research_level':
            if (row['status'] == 'not_applicable' or 'unsupported' in row['reason'].lower()
                    or 'research_off' in row['reason'].lower()):
                raise GateError('UNSUPPORTED_RESEARCH_LABEL', 'Research-ON cohort lacks native support',
                                role=role, release=row['release'], id=row['id'])
        key = tuple(row[field] for field in ('release', 'arm', 'id', 'seed', 'mod', 'difficulty',
                                            'research', 'house', 'metric', 'minute'))
        payload = tuple(row[field] for field in COLUMNS)
        if key in seen:
            if seen[key] != payload:
                raise GateError('CONFLICTING_DUPLICATE', 'Conflicting observations for one logical unit',
                                role=role, id=row['id'], metric=row['metric'], house=row['house'])
            duplicates += 1
            continue
        seen[key] = payload
        row['_role'] = role
        row['_missing'] = False
        row['_scenario'] = scenario_id(row['id'], row['seed'])
        row['_spice'], row['_format'] = scenario_dimensions(row['id'])
        result.append(row)
    if not result:
        raise GateError('EMPTY_OBSERVATIONS', 'No observations supplied', role=role)
    return result, duplicates


class _DigestReader(io.RawIOBase):
    def __init__(self, handle):
        self.handle = handle
        self.digest = hashlib.sha256()
        self.bytes = 0

    def readable(self):
        return True

    def readinto(self, target):
        data = self.handle.read(len(target))
        target[:len(data)] = data
        self.digest.update(data)
        self.bytes += len(data)
        return len(data)


def read_observations(paths, role):
    """Hash the exact compressed/plain bytes as they are consumed, not a later reread."""
    rows, receipts = [], []
    for supplied in paths:
        path = Path(supplied)
        try:
            with path.open('rb') as raw:
                digest_reader = _DigestReader(raw)
                with io.BufferedReader(digest_reader) as buffered:
                    compressed = buffered.peek(2)[:2] == b'\x1f\x8b'
                    stream = gzip.GzipFile(fileobj=buffered, mode='rb') if compressed else buffered
                    with io.TextIOWrapper(stream, encoding='utf-8-sig', newline='') as text:
                        reader = csv.DictReader(text)
                        if reader.fieldnames != list(COLUMNS):
                            raise GateError('CSV_SCHEMA_MISMATCH', 'CSV columns must exactly match interface schema',
                                            path=str(path), expected=list(COLUMNS), actual=reader.fieldnames)
                        start = len(rows)
                        for row in reader:
                            if None in row or any(value is None for value in row.values()):
                                raise GateError('CSV_ROW_WIDTH', 'Truncated or overwide CSV row', path=str(path))
                            rows.append(row)
                receipts.append(dict(role=role, path=str(path.resolve()), sha256=digest_reader.digest.hexdigest(),
                                     bytes=digest_reader.bytes, rows=len(rows) - start, gzip=compressed))
        except GateError:
            raise
        except (OSError, EOFError, UnicodeError, csv.Error) as error:
            raise GateError('UNREADABLE_OBSERVATIONS', str(error), role=role, path=str(path)) from error
    return rows, receipts


def _cohort_key(row):
    return row['_role'], row['release'], row['arm']


def _game_key(row):
    return row['mod'], row['research'], row['id'], row['seed']


def _prepare_cohorts(rows):
    cohorts = defaultdict(list)
    for row in rows:
        cohorts[_cohort_key(row)].append(row)
    for key, observations in cohorts.items():
        games = defaultdict(list)
        for row in observations:
            games[_game_key(row)].append(row)
        additions = []
        for game_rows in games.values():
            identities = {tuple(row[field] for field in RECEIPT_FIELDS if field != 'context_sha256')
                          for row in game_rows}
            if len(identities) != 1:
                raise GateError('INCONSISTENT_GAME_IDENTITY', 'One game has conflicting native/source/map/options receipts',
                                release=key[1], id=game_rows[0]['id'])
            houses = defaultdict(list)
            for row in game_rows:
                houses[row['house']].append(row)
            roster = []
            for house, house_rows in houses.items():
                membership = {(row['team'], row['difficulty'], row['context_sha256']) for row in house_rows}
                if len(membership) != 1:
                    raise GateError('INCONSISTENT_HOUSE_IDENTITY', 'House changes roster or context within one game',
                                    release=key[1], id=house_rows[0]['id'], house=house)
                exemplar = house_rows[0]
                roster.append((house, exemplar['team'], exemplar['difficulty']))
                if exemplar['research'] == 'on' and not any(row['metric'] == 'research_level' for row in house_rows):
                    raise GateError('RESEARCH_SUPPORT_UNDECLARED', 'Research-ON requires an explicit research_level observation',
                                    release=key[1], id=exemplar['id'], house=house)
                available = {(row['metric'], row['minute']) for row in house_rows}
                for metric in CORE_FAMILIES:
                    for minute in METRIC_DEFINITIONS[metric].get('core_minutes', (SUMMARY_MINUTE,)):
                        if (metric, minute) not in available:
                            missing = dict(exemplar, metric=metric, minute=minute, value=None,
                                           status='unavailable', reason='missing_core_metric', _missing=True)
                            additions.append(missing)
                            game_rows.append(missing)
            roster = tuple(sorted(roster))
            for row in game_rows:
                row['_roster'] = roster
        observations.extend(additions)
    return dict(cohorts)


def _group_keys(row):
    base = (row['mod'], row['difficulty'], row['research'])
    tail = (row['metric'], row['minute'])
    yield ('context_house',) + base + (row['_scenario'], row['house']) + tail
    if row['minute'] == SUMMARY_MINUTE:
        yield ('context_pooled',) + base + (row['_scenario'], 'all') + tail
        yield ('pooled_sensitivity',) + base + ('all_contexts', 'all') + tail


def _group_rows(rows):
    groups = defaultdict(list)
    for row in rows:
        for key in _group_keys(row):
            groups[key].append(row)
    return dict(groups)


def _counts(rows):
    counts = Counter(row['status'] for row in rows)
    return {**{status: counts[status] for status in STATUSES},
            'missing': sum(row['_missing'] for row in rows), 'rows': len(rows)}


def seed_blocks(rows):
    """One independent seed; first average houses within game, then games/orientations.

    A partially observed seed is not filled with zero or allowed to drop its missing
    houses. Its statuses remain in the report, but it cannot supply paired evidence.
    """
    by_seed = defaultdict(list)
    for row in rows:
        by_seed[row['seed']].append(row)
    values = {}
    for seed, seed_rows in by_seed.items():
        if any(row['status'] != 'measured' for row in seed_rows):
            continue
        games = defaultdict(list)
        for row in seed_rows:
            games[_game_key(row)].append(row['value'])
        values[seed] = _finite_mean(_finite_mean(game) for game in games.values())
    return values


@dataclass
class IntervalRequest:
    values: dict
    target: dict
    low_field: str
    high_field: str


def simultaneous_intervals(requests, samples=4999, seed=20261106):
    """Selection-adjusted seed-block bootstrap bands, including every reference.

    Resampling indexes are shared by every objective with an identical seed set.
    The cutoff is the bootstrap maximum absolute standardized mean deviation.
    Distinct seed-set strata receive Bonferroni alpha allocations; this remains
    conservative even when strata overlap. Zero-variance bootstrap distributions
    have exact conditional bands. Sparse/empty vectors cost no bootstrap work.
    Finite-tail resolution is required, never replaced by a point-estimate pass.
    """
    groups = defaultdict(list)
    for request in requests:
        keys = tuple(sorted(request.values))
        request.target[request.low_field] = None
        request.target[request.high_field] = None
        if len(keys) < 2:
            request.target['ci_status'] = 'insufficient_independent_seeds'
            continue
        values = tuple(request.values[key] for key in keys)
        mean = _finite_mean(values)
        try:
            scale = statistics.stdev(values) / math.sqrt(len(values))
        except (OverflowError, ValueError) as error:
            raise GateError('NONFINITE_DERIVED_METRIC', str(error)) from error
        if not math.isfinite(scale):
            raise GateError('NONFINITE_DERIVED_METRIC', 'Nonfinite seed-block standard error')
        if scale == 0:
            request.target[request.low_field] = mean
            request.target[request.high_field] = mean
            request.target['ci_status'] = 'simultaneous_seed_bootstrap'
        else:
            groups[keys].append((request, values, mean, scale))
    strata = len(groups)
    unresolved = 0
    if not strata:
        return dict(method='seed_set_stratified_simultaneous_max_standardized_bootstrap',
                    confidence=CONFIDENCE, seed_set_strata=0, unresolved_strata=0, samples=samples)
    alpha = (1.0 - CONFIDENCE) / strata
    for keys, items in sorted(groups.items()):
        if samples * alpha < 10:
            unresolved += 1
            for request, _, _, _ in items:
                request.target['ci_status'] = 'insufficient_bootstrap_tail_resolution'
            continue
        digest = hashlib.sha256(encoded([seed, keys]).encode('utf-8')).digest()
        rng = random.Random(int.from_bytes(digest[:16], 'big'))
        count = len(keys)
        # Identical standardized vectors need only one evaluation per draw.
        standardized = set(tuple((value - mean) / (scale * count) for value in values)
                           for _, values, mean, scale in items)
        maxima = []
        for _ in range(samples):
            indexes = [rng.randrange(count) for _ in range(count)]
            maxima.append(max(abs(math.fsum(vector[index] for index in indexes)) for vector in standardized))
        maxima.sort()
        rank = min(samples - 1, math.ceil((1.0 - alpha) * (samples + 1)) - 1)
        cutoff = maxima[rank]
        for request, _, mean, scale in items:
            request.target[request.low_field] = mean - cutoff * scale
            request.target[request.high_field] = mean + cutoff * scale
            request.target['ci_status'] = 'simultaneous_seed_bootstrap'
    return dict(method='seed_set_stratified_simultaneous_max_standardized_bootstrap',
                confidence=CONFIDENCE, seed_set_strata=strata, unresolved_strata=unresolved,
                alpha_per_stratum=alpha, samples=samples)


def _summaries(cohorts, groups, requests):
    summaries, index = [], {}
    for cohort in sorted(cohorts):
        role, release, arm = cohort
        for group, rows in sorted(groups[cohort].items()):
            record = dict(zip(GROUP_FIELDS, group))
            definition = METRIC_DEFINITIONS[record['metric']]
            values = seed_blocks(rows)
            spice, game_format = _display_dimensions(rows)
            record.update(role=role, release=release, arm=arm, direction=definition['direction'],
                          core=_is_core(record['metric'], record['minute']), spice=spice, format=game_format,
                          source_commits=sorted({row['source_commit'] for row in rows}),
                          baseline_release_sources=sorted({row['baseline_release_source'] for row in rows}),
                          **_counts(rows), total_seeds=len({row['seed'] for row in rows}),
                          independent_seeds=len(values), estimate=_finite_mean(values.values()) if values else None,
                          ci_low=None, ci_high=None, ci_status='no_measured_seed_blocks',
                          status_reasons=dict(sorted(Counter(row['status'] + ':' + row['reason']
                                                             for row in rows if row['status'] != 'measured').items())))
            # Sampled time rows are descriptive only, never snapshot pseudo-n.
            if record['minute'] == SUMMARY_MINUTE and values:
                requests.append(IntervalRequest(values, record, 'ci_low', 'ci_high'))
            elif values:
                record['ci_status'] = 'sampled_time_descriptive_only'
            summaries.append(record)
            index[cohort, group] = record
    return summaries, index


def _unit_key(row):
    return tuple(row[field] for field in ('id', 'seed', 'mod', 'difficulty', 'research', 'house', 'metric', 'minute'))


def _coverage(candidate, reference, same_source):
    errors = []
    left = {_unit_key(row): row for row in candidate}
    right = {_unit_key(row): row for row in reference}
    candidate_seeds = {row['seed'] for row in candidate}
    reference_seeds = {row['seed'] for row in reference}
    if candidate_seeds != reference_seeds:
        errors.append('UNEQUAL_SEED_SETS')
    if set(left) != set(right):
        errors.append('INCOMPLETE_MATCHED_UNITS')
    fields = ('team', '_roster', 'context_sha256', 'map_sha256', 'options_sha256')
    if same_source:
        fields += SAME_SOURCE_FIELDS
    for field in fields:
        if any(left[key][field] != right[key][field] for key in set(left) & set(right)):
            errors.append('MISMATCH_' + field.lstrip('_').upper())
    if any(_valid_not_applicable(left[key]) != _valid_not_applicable(right[key])
           and not (left[key]['metric'] in FINAL_ARMY_SHARE_METRICS
                    and left[key]['minute'] == SUMMARY_MINUTE)
           for key in set(left) & set(right)):
        errors.append('MISMATCH_APPLICABILITY')
    if any(row['status'] != 'measured' and not _valid_not_applicable(row) for row in candidate + reference):
        errors.append('UNMEASURED_CORE_OR_OBJECTIVE')
    if any(row['_missing'] for row in candidate + reference):
        errors.append('MISSING_CORE_METRIC')
    return sorted(set(errors))


def _comparison(kind, candidate_cohort, reference_cohort, group, candidate, reference, requests):
    record = dict(zip(GROUP_FIELDS, group))
    definition = METRIC_DEFINITIONS[record['metric']]
    core = _is_core(record['metric'], record['minute'])
    identifier = hashlib.sha256(encoded([kind, candidate_cohort, reference_cohort, group]).encode('utf-8')).hexdigest()[:24]
    spice, game_format = _display_dimensions(candidate + reference)
    record.update(comparison_id=identifier, kind=kind, candidate_release=candidate_cohort[1],
                  candidate_arm=candidate_cohort[2], reference_release=reference_cohort[1],
                  reference_arm=reference_cohort[2], direction=definition['direction'], core=core,
                  gating=kind in ('current_off', 'historical') and core and record['scope'] == 'context_house',
                  applicable=True, spice=spice, format=game_format,
                  candidate_seeds=sorted({row['seed'] for row in candidate}),
                  reference_seeds=sorted({row['seed'] for row in reference}),
                  paired_seed_count=0, candidate_estimate=None, reference_estimate=None,
                  candidate_ci_low=None, candidate_ci_high=None, reference_ci_low=None, reference_ci_high=None,
                  paired_delta=None, delta_ci_low=None, delta_ci_high=None,
                  absolute_margin=float(definition['practical_absolute_margin']),
                  relative_margin=float(definition['practical_relative_margin']), margin=None,
                  signed_benefit=None, noninferiority_slack=None, slack_ci_low=None, slack_ci_high=None,
                  ci_status='no_complete_paired_seed_blocks', status='INCOMPLETE_COVERAGE',
                  coverage_errors=[], candidate_counts=_counts(candidate), reference_counts=_counts(reference))
    left, right = seed_blocks(candidate), seed_blocks(reference)
    record['candidate_estimate'] = _finite_mean(left.values()) if left else None
    record['reference_estimate'] = _finite_mean(right.values()) if right else None
    record['coverage_errors'] = _coverage(candidate, reference, kind == 'current_off')
    if record['coverage_errors']:
        return record
    if candidate and reference and all(_valid_not_applicable(row) for row in candidate + reference):
        record.update(applicable=False, status='NOT_APPLICABLE',
                      ci_status='explicit_native_applicability_exemption')
        return record
    seeds = sorted(set(left) & set(right))
    if not seeds:
        return record
    direction = 1.0 if definition['direction'] == 'higher' else -1.0
    deltas = {seed: left[seed] - right[seed] for seed in seeds}
    # One fixed margin per comparison, on the declared basis (mean of absolute
    # reference seed blocks). A per-seed margin would add the reference's own
    # seed variance to the slack interval, so even an identical arm could not pass.
    margin = record['absolute_margin'] + record['relative_margin'] * _finite_mean(abs(right[seed]) for seed in seeds)
    slacks = {seed: direction * deltas[seed] + margin for seed in seeds}
    record.update(paired_seed_count=len(seeds), paired_delta=_finite_mean(deltas.values()),
                  signed_benefit=direction * _finite_mean(deltas.values()), margin=margin,
                  noninferiority_slack=_finite_mean(slacks.values()))
    requests.append(IntervalRequest(deltas, record, 'delta_ci_low', 'delta_ci_high'))
    requests.append(IntervalRequest(slacks, record, 'slack_ci_low', 'slack_ci_high'))
    return record


def _release_order(cohort):
    version = re.search(r'(\d+)\.(\d+)\.(\d+)', cohort[1])
    return tuple(int(part) for part in version.groups()) if version else (0, 0, 0)


def _comparisons(cohorts, groups, requests):
    history = sorted((key for key in cohorts if key[0] == 'history'), key=lambda key: (_release_order(key), key))
    controls = [key for key in cohorts if key[0] == 'control']
    candidates = [key for key in cohorts if key[0] == 'candidate']
    if len(controls) != 1 or len(candidates) != 1:
        raise GateError('AMBIGUOUS_CURRENT_COHORT', 'Control and candidate must each name one release and one arm')
    if len({key[1] for key in history}) != len(history):
        raise GateError('AMBIGUOUS_HISTORICAL_COHORT', 'Each historical release must have exactly one immutable arm')
    control, candidate = controls[0], candidates[0]
    if candidate[1] != control[1]:
        raise GateError('CURRENT_RELEASE_MISMATCH', 'Same-source OFF/ON must name the same current release')
    pairs = [('current_off', candidate, control)]
    pairs.extend(('historical', candidate, reference) for reference in history)
    chronological = history + [control]
    pairs.extend(('adjacent_release', newer, older) for older, newer in zip(chronological, chronological[1:]))
    comparisons = []
    for kind, left, right in pairs:
        left_groups, right_groups = groups[left], groups[right]
        supported_research = {row['research'] for row in cohorts[right]}
        for group in sorted(set(left_groups) | set(right_groups)):
            if group[-1] != SUMMARY_MINUTE:
                continue
            candidate_rows, reference_rows = left_groups.get(group, []), right_groups.get(group, [])
            comparison = _comparison(kind, left, right, group, candidate_rows, reference_rows, requests)
            if (kind != 'current_off' and group[3] == 'on' and 'on' not in supported_research
                    and not reference_rows):
                comparison.update(applicable=False, status='NO_HISTORICAL_RESEARCH_COVERAGE',
                                  coverage_errors=['NO_HISTORICAL_RESEARCH_COVERAGE'])
            comparisons.append(comparison)
    return comparisons, history, control, candidate


def _finish_comparisons(comparisons, summaries, minimum):
    control_releases = {cohort[1] for cohort, _ in summaries if cohort[0] == 'control'}
    for row in comparisons:
        left_role = ('control' if row['kind'] == 'adjacent_release'
                     and row['candidate_release'] in control_releases else 'history')
        if row['kind'] != 'adjacent_release':
            left_role = 'candidate'
        right_role = 'control' if row['kind'] == 'current_off' else 'history'
        group = tuple(row[field] for field in GROUP_FIELDS)
        for prefix, role in (('candidate', left_role), ('reference', right_role)):
            key = ((role, row[prefix + '_release'], row[prefix + '_arm']), group)
            summary = summaries.get(key)
            if summary:
                row[prefix + '_ci_low'], row[prefix + '_ci_high'] = summary['ci_low'], summary['ci_high']
        if not row['applicable'] or row['coverage_errors']:
            continue
        if row['paired_seed_count'] < minimum:
            row['status'] = 'INSUFFICIENT_SEEDS'
        elif row['slack_ci_low'] is None or row['slack_ci_high'] is None:
            row['status'] = 'INSUFFICIENT_PRECISION'
        elif row['slack_ci_high'] < 0:
            row['status'] = 'REGRESSION'
        elif row['slack_ci_low'] >= 0:
            row['status'] = 'NONINFERIOR'
        else:
            row['status'] = 'INCONCLUSIVE_INTERVAL'


def _best_historical(comparisons):
    groups = defaultdict(list)
    for row in comparisons:
        if row['kind'] == 'historical':
            groups[tuple(row[field] for field in GROUP_FIELDS)].append(row)
    best = []
    for group, references in sorted(groups.items()):
        measured = [row for row in references if row['reference_estimate'] is not None and row['applicable']]
        sign = 1.0 if references[0]['direction'] == 'higher' else -1.0
        selected = max(measured, key=lambda row: (sign * row['reference_estimate'], row['reference_release'])) if measured else references[0]
        record = {field: selected.get(field) for field in BEST_FIELDS}
        record.update(historical_releases=sorted({row['reference_release'] for row in references}),
                      measured_references=len(measured))
        if not measured:
            states = {row['status'] for row in references}
            status = next(iter(states)) if states in ({'NOT_APPLICABLE'}, {'NO_HISTORICAL_RESEARCH_COVERAGE'}) else 'NO_MEASURED_HISTORICAL_REFERENCE'
            record.update(reference_release=None, reference_arm=None, reference_estimate=None, status=status)
        best.append(record)
    return best


def evaluate(history_rows, control_rows, candidate_rows, min_seeds=8, bootstrap_samples=4999,
             bootstrap_seed=20261106):
    """Return compact decision plus the three complete machine-readable tables."""
    if min_seeds < 1 or bootstrap_samples < 1:
        raise GateError('INVALID_STATISTICAL_OPTIONS', 'Seed minimum and bootstrap samples must be positive')
    normalized, duplicates = [], {}
    for role, rows in (('history', history_rows), ('control', control_rows), ('candidate', candidate_rows)):
        observations, duplicates[role] = validate_rows(rows, role)
        normalized.extend(observations)
    cohorts = _prepare_cohorts(normalized)
    groups = {key: _group_rows(rows) for key, rows in cohorts.items()}
    requests = []
    metrics, metric_index = _summaries(cohorts, groups, requests)
    comparisons, history, control, candidate = _comparisons(cohorts, groups, requests)
    # Freeze the authorization family to the declared hard objectives, including
    # every historical release, its reference CI and adjacent-release contrasts.
    # Censored diagnostic timing can create many sparse seed subsets; it must
    # neither authorize a core objective nor exhaust that family's tail resolution.
    interval_policy = simultaneous_intervals([request for request in requests if request.target['core']],
                                             bootstrap_samples, bootstrap_seed)
    diagnostic_policy = simultaneous_intervals([request for request in requests if not request.target['core']],
                                               bootstrap_samples, bootstrap_seed)
    minimum = max(SAFETY_MIN_SEEDS, min_seeds)
    _finish_comparisons(comparisons, metric_index, minimum)
    best = _best_historical(comparisons)
    required = [row for row in comparisons if row['gating'] and row['applicable']]
    failures = []
    for row in required:
        if row['status'] != 'NONINFERIOR':
            failures.append({field: row[field] for field in ('comparison_id', 'kind', 'reference_release',
                            'mod', 'difficulty', 'research', 'context', 'house', 'metric', 'status',
                            'paired_seed_count', 'coverage_errors')})
    historical_groups = defaultdict(list)
    for row in comparisons:
        if row['kind'] == 'historical' and row['gating']:
            historical_groups[tuple(row[field] for field in GROUP_FIELDS)].append(row)
    candidate_groups = {key for key, rows in groups[candidate].items() if key[0] == 'context_house'
                        and _is_core(key[-2], key[-1]) and not all(_valid_not_applicable(row) for row in rows)}
    for group in sorted(candidate_groups):
        applicable = [row for row in historical_groups.get(group, []) if row['applicable']]
        if not applicable:
            failures.append(dict(zip(GROUP_FIELDS, group), code='NO_HISTORICAL_COVERAGE',
                                 status='NO_HISTORICAL_RESEARCH_COVERAGE' if group[3] == 'on' else 'NO_HISTORICAL_COVERAGE'))
    if any(row['status'] == 'REGRESSION' for row in required):
        verdict = 'BLOCKED_REGRESSION'
    elif required and not failures:
        verdict = 'DEFAULT_ON_ALLOWED'
    else:
        verdict = 'INCONCLUSIVE'
    margins = {metric: {field: METRIC_DEFINITIONS[metric][field] for field in
                       ('direction', 'practical_absolute_margin', 'practical_relative_margin',
                        'applicability', 'core_minutes')}
               for metric in CORE_FAMILIES}
    report = dict(schema_version=SCHEMA_VERSION, verdict=verdict, errors=[], failures=failures,
                  current=dict(release=candidate[1], candidate_arm=candidate[2], control_arm=control[2]),
                  historical_releases=[dict(release=key[1], arm=key[2],
                                            source_commits=sorted({row['source_commit'] for row in cohorts[key]}),
                                            baseline_release_sources=sorted({row['baseline_release_source'] for row in cohorts[key]}))
                                       for key in history],
                  policy=dict(independent_unit='seed; houses within game then games/orientations within seed',
                              requested_min_seeds=min_seeds, effective_min_seeds=minimum,
                              hard_safety_floor=SAFETY_MIN_SEEDS, bootstrap_seed=bootstrap_seed,
                              intervals=interval_policy, diagnostic_intervals=diagnostic_policy, margins=margins,
                              confidence_families='all core/reference intervals jointly; noncore diagnostics separately',
                              relative_margin_basis='mean of absolute reference seed-block values',
                              primary_scope='context_house; pooled rows are sensitivity, not substitutes',
                              historical_selection='every supplied immutable release retained; best per metric/context',
                              research='exact off/on cohorts only; unsupported historical ON never synthesized'),
                  coverage={role: _counts([row for observations in cohorts.values() for row in observations
                                           if row['_role'] == role]) for role in ('history', 'control', 'candidate')},
                  exact_duplicates_removed=duplicates,
                  comparisons=dict(total=len(comparisons), required=len(required),
                                   statuses=dict(sorted(Counter(row['status'] for row in comparisons).items()))),
                  likely_regression_intervals=[{field: row[field] for field in
                                               ('reference_release', 'candidate_release', 'mod', 'difficulty',
                                                'research', 'context', 'house', 'metric', 'paired_delta',
                                                'delta_ci_low', 'delta_ci_high', 'margin', 'paired_seed_count')}
                                               for row in comparisons if row['kind'] == 'adjacent_release'
                                               and row['scope'] == 'context_house' and row['core']
                                               and row['status'] == 'REGRESSION'])
    return report, metrics, best, comparisons


def _csv_value(value):
    return encoded(value) if isinstance(value, (dict, list, tuple)) else value


def write_csv_gzip(path, fields, rows):
    with Path(path).open('wb') as raw:
        with gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=0) as zipped:
            with io.TextIOWrapper(zipped, encoding='utf-8', newline='') as text:
                writer = csv.DictWriter(text, fieldnames=fields, extrasaction='ignore', lineterminator='\n')
                writer.writeheader()
                for row in rows:
                    writer.writerow({key: _csv_value(row.get(key)) for key in fields})


def write_report(output, report, metrics, best, comparisons):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    tables = (('release-metrics.csv.gz', METRIC_FIELDS, metrics),
              ('best-historical.csv.gz', BEST_FIELDS, best),
              ('paired-comparisons.csv.gz', COMPARISON_FIELDS, comparisons))
    for filename, fields, rows in tables:
        write_csv_gzip(output / filename, fields, rows)
    report['artifacts'] = {filename: dict(rows=len(rows)) for filename, _, rows in tables}
    (output / 'report.json').write_text(encoded(report) + '\n', encoding='utf-8')
    lines = ['AI BASELINE GATE: ' + report['verdict'],
             'Independent hard objectives; no scalar fitness. Missing/censored/unavailable is not zero or a pass.']
    if 'policy' in report:
        policy = report['policy']
        lines.extend(['Independent seed minimum: requested %s; effective %s (hard floor %s).' %
                      (policy['requested_min_seeds'], policy['effective_min_seeds'], policy['hard_safety_floor']),
                      'Intervals: simultaneous seed-block max-standardized bootstrap; Bonferroni across seed sets.',
                      'All core/reference intervals form one 95% family; noncore diagnostics have a separate 95% family.',
                      'All historical releases are retained; best reference is selected independently per objective/context.',
                      'Pooled sensitivity cannot rescue a failing house/context; research ON is never compared to historical OFF.',
                      'Core margins (signed benefit + margin >= 0 establishes noninferiority):'])
        for metric, margin in sorted(policy['margins'].items()):
            lines.append('  %s: %s better; absolute=%g, reference-relative=%g' %
                         (metric, margin['direction'], margin['practical_absolute_margin'], margin['practical_relative_margin']))
        lines.append('Required comparisons: ' + str(report['comparisons']['required']))
    lines.append('Errors: %d; incomplete/regressing objectives: %d.' %
                 (len(report.get('errors', [])), len(report.get('failures', []))))
    for error in report.get('errors', []):
        lines.append('ERROR ' + encoded(error))
    for failure in report.get('failures', [])[:30]:
        lines.append('OBJECTIVE ' + encoded(failure))
    if len(report.get('failures', [])) > 30:
        lines.append('Full objective failures retained in report.json and paired-comparisons.csv.gz.')
    lines.append('Exact input SHA256 receipts are in report.json; retain those immutable inputs with this report.')
    (output / 'report.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--history', nargs='+', required=True, help='Frozen historical observations, gzip or plain CSV')
    parser.add_argument('--control', required=True, help='Same-source current feature-OFF observations')
    parser.add_argument('--candidate', required=True, help='Same-source current feature-ON observations')
    parser.add_argument('--output', required=True, help='Permanent decision/report directory')
    parser.add_argument('--min-seeds', type=int, default=8, help='Requested independent seeds; cannot lower safety floor8')
    parser.add_argument('--bootstrap-samples', type=int, default=4999)
    parser.add_argument('--bootstrap-seed', type=int, default=20261106)
    args = parser.parse_args(argv)
    inputs = []
    try:
        history, receipts = read_observations(args.history, 'history')
        inputs.extend(receipts)
        control, receipts = read_observations([args.control], 'control')
        inputs.extend(receipts)
        candidate, receipts = read_observations([args.candidate], 'candidate')
        inputs.extend(receipts)
        report, metrics, best, comparisons = evaluate(history, control, candidate, args.min_seeds,
                                                      args.bootstrap_samples, args.bootstrap_seed)
    except GateError as error:
        report = dict(schema_version=SCHEMA_VERSION, verdict='INCONCLUSIVE',
                      errors=[error.as_dict()], failures=[], input_valid=False)
        metrics, best, comparisons = [], [], []
    report['inputs'] = inputs
    report.setdefault('input_valid', True)
    write_report(args.output, report, metrics, best, comparisons)
    print('AI_BASELINE_GATE_RESULT: ' + encoded(dict(verdict=report['verdict'], input_valid=report['input_valid'],
                                                 report=str(Path(args.output) / 'report.json'))))
    if not report['input_valid']:
        return 4
    return {'DEFAULT_ON_ALLOWED': 0, 'BLOCKED_REGRESSION': 2, 'INCONCLUSIVE': 3}[report['verdict']]


if __name__ == '__main__':
    sys.exit(main())
