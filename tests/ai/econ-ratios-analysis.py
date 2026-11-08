#!/usr/bin/env python3
"""Analyze captured economic-ratio factorials; never run games or invent observations.

  python3 tests/ai/econ-ratios-analysis.py --design OUTPUT/screen-throughput/design.json \
      --output OUTPUT/analysis --metrics observer-fields.json

Input design: schema_version=1 from econ-ratios-design.py. Each command's
result_dir contains the normal StatsDesign/audit games.csv, houses.csv,
samples.csv, map-features.csv and optionally any observer CSV named below.
The StatsDesign genome_search CSV loaders/writers are reused (including .gz).
NumPy is required for rank-aware categorical least squares; no other packages.

--metrics accepts a schema-v1 JSON override, merged into the exact defaults:
  {"schema_version":1,"metrics":{
    "current_asset_credit":{"file":"fleet.csv","field":"asset_stock","aggregate":"last"},
    "actual_capital_spending":{"file":"fleet.csv","field":"capital_paid","aggregate":"last"}},
   "features":{
    "distance":{"file":"fleet.csv","field":"haul_distance","aggregate":"first",
                "require_cycle_zero":true,"unit":"tiles"},
    "available_spice":{"file":"fleet.csv","field":"shared_spice","aggregate":"first",
                       "require_cycle_zero":true,"unit":"observer spice credits"}}}
Each mapping: file (relative CSV path), field, aggregate (unique/first/last/
delta/sum/mean/time_mean), scope (house/global/house_or_global/game), scale,
per_minute, require_cycle_zero, snapshot_reduce (min/max/mean/sum at the chosen
timestamp), checkpoint (default true), optional require {field:literal}. Files join
on id and candidate house; global map rows are used only when explicitly
allowed. first/last/delta/time_mean require cycle or minute timestamps when
there is more than one row. Missing fields/completeness are unmeasured, not 0.
A derived metric is {"numerator":"NAME","denominator":"NAME"}; a zero
or missing denominator is explicitly undefined, never a reason to drop wins,
losses, eliminations, zero-income rows or other observed metrics.

Default observer mappings use samples.csv fleet_capital_current_value,
fleet_capital_value_cycles and refined_spice_raw; refinery-samples.csv supplies
first-observed distance/accessibility. Ratio-table bands use the SAME native-arm
descriptor for every treatment of each common case/seat, never each treatment's
own refinery geometry. Raw own-arm geometry is retained as mechanism-only data.
Default model interactions use only cycle-zero StatsDesign map features.
spice_gross is valid even when ledger_enabled=0. Actual paid capital
spending is unmeasured unless its exact observer field is supplied; neither
fleet_capital_built_value nor accepted orders are actual paid expenditure.
Exposure is actual games.csv minutes (or observed sample time if absent), never
the configured cap. Capital efficiency uses the integral of current asset credit,
not final asset stock multiplied by the time limit.

Outputs on command only: analysis.json; observations.csv (all captured games,
including errors/losers/eliminations); ratio-table.csv; paired-native.csv;
checkpoints.csv and checkpoint-{ratios,paired-native}.csv at 3/5/8/10/15 minutes;
coverage.csv (all 108 generated format x size x spice x tech cells per arm/mod/stage
and separate literal authored map/seed cells); model-coefficients.csv.
Null estimates/CIs carry explicit status. CIs are
pointwise 95% common-case cluster bootstraps (minimum 4 independent cases),
not simultaneous promotion gates. Stages and mods are never pooled.
"""
import argparse
from collections import Counter, defaultdict
import copy
import importlib.util
import itertools
import json
import math
from pathlib import Path
import random
import statistics
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
_spec = importlib.util.spec_from_file_location('econ_ratios_statsdesign', HERE / 'genome_search.py')
STATS = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(STATS)

FORMATS = ('1v1', '2v2', 'ffa4', 'ffa6')
SIZES, SPICES, TECHS = (64, 128, 160), ('low', 'medium', 'high'), (3, 6, 9)
COMPOSITIONS = ('scarce-rock', 'standard', 'rocky-ridges')
RATIO_LEVELS = (('harvesters_per_bay', (2, 3, 4)),
                ('carryall_quarters', (0, 1, 2)), ('repair_yards', (0, 1)))
RECEIPTS = ('map_hash', 'options_hash', 'content_hash', 'object_data_hash', 'source_commit', 'candidate_house')
DEFAULTS = dict(schema_version=1, metrics={
    'spice_income_per_minute': dict(file='samples.csv', field='spice_gross', aggregate='last',
                                   scope='house', per_minute=True,
                                   unit='spice credits / observed game minute'),
    'current_asset_credit': dict(file='samples.csv', field='fleet_capital_current_value',
                                 aggregate='last', scope='house', unit='current replacement-price credits'),
    'refined_spice_credit': dict(file='samples.csv', field='refined_spice_raw', aggregate='last',
                                scope='house', scale=2 ** -32, unit='exact refined spice credits'),
    'current_asset_credit_minutes': dict(file='samples.csv', field='fleet_capital_value_cycles',
                                         aggregate='last', scope='house', scale=1 / 3750,
                                         require={'fleet_gap_count': '0'}, unit='current asset credit x observed minutes'),
    'actual_capital_spending': dict(file='samples.csv', field='actual_capital_spending',
                                    aggregate='last', scope='house', unit='actual cumulative paid capital credits'),
    'spice_income_per_minute_per_asset_credit': dict(numerator='refined_spice_credit',
                                                    denominator='current_asset_credit_minutes',
                                                    unit='spice credits / minute / time-integrated current asset credit'),
    'bay_available_cycles': dict(file='samples.csv', field='fleet_bay_available_cycles', aggregate='last', scope='house',
                                 require={'fleet_gap_count': '0'}, unit='observed available bay-cycles'),
    'bay_occupied_cycles': dict(file='samples.csv', field='fleet_bay_occupied_cycles', aggregate='last', scope='house',
                                require={'fleet_gap_count': '0'}, unit='observed occupied bay-cycles'),
    'bay_unloading_cycles': dict(file='samples.csv', field='fleet_bay_unloading_cycles', aggregate='last', scope='house',
                                 require={'fleet_gap_count': '0'}, unit='observed unloading bay-cycles'),
    'bay_occupied_fraction': dict(numerator='bay_occupied_cycles', denominator='bay_available_cycles', unit='occupied bay fraction'),
    'bay_unloading_fraction': dict(numerator='bay_unloading_cycles', denominator='bay_available_cycles', unit='unloading bay fraction'),
    'completed_trips': dict(file='samples.csv', field='harvester_trip_count', aggregate='last', scope='house', unit='completed unload-to-unload trips'),
    'completed_trip_minutes': dict(file='samples.csv', field='harvester_trip_cycles_sum', aggregate='last', scope='house', scale=1 / 3750, unit='completed trip minutes'),
    'mean_completed_trip_minutes': dict(numerator='completed_trip_minutes', denominator='completed_trips', unit='minutes per completed trip; censored trips separate'),
    'right_censored_trips': dict(file='samples.csv', field='harvester_trip_censored', aggregate='last', scope='house', unit='right-censored trips'),
    'open_trips': dict(file='samples.csv', field='harvester_trip_open', aggregate='last', scope='house', unit='open trips at observation end'),
    'left_censored_trips': dict(file='samples.csv', field='harvester_trip_left_censored', aggregate='last', scope='house', unit='left-censored arrivals'),
    'win': dict(file='houses.csv', field='won', aggregate='unique', scope='house', checkpoint=False, unit='audit win indicator'),
    'placement': dict(file='houses.csv', field='score', aggregate='unique', scope='house', checkpoint=False, unit='audit placement score'),
    'raw_conquest_win': dict(file='houses.csv', field='raw_won', aggregate='unique', scope='house',
                             checkpoint=False, unit='raw engine conquest win indicator'),
}, features={
    'distance': dict(file='refinery-samples.csv', field='nearest_spice_distance_steps', aggregate='first',
                     snapshot_reduce='mean', scope='house', unit='first-observed mean refinery 8-neighbour ground steps; descriptive'),
    'available_spice': dict(file='refinery-samples.csv', field='accessible_spice_amount', aggregate='first',
                            snapshot_reduce='max', scope='house', unit='first-observed maximum per-refinery accessible spice, NOT sum of overlapping fields'),
    'base_distance': dict(file='map-features.csv', field='base_distance_min', aggregate='first',
                          scope='house_or_global', require_cycle_zero=True,
                          unit='cycle-zero Euclidean tiles, NOT spice haul distance'),
    'initial_spice_density': dict(file='map-features.csv', field='initial_spice_density', aggregate='first',
                                  scope='house_or_global', require_cycle_zero=True,
                                  unit='cycle-zero spice tile fraction'),
    'initial_spice_amount': dict(file='map-features.csv', field='initial_spice_amount', aggregate='first',
                                 scope='house_or_global', require_cycle_zero=True,
                                 unit='cycle-zero whole-map spice; not a per-seat reachable cap'),
})
DEFAULTS['metrics'].update({
    name: dict(file='samples.csv', field=name, aggregate='last', scope='house', unit=unit)
    for name, unit in (
        ('harvesters', 'live harvesters'), ('carryalls', 'live carryalls'),
        ('refineries', 'live refineries'), ('repair_yards', 'live repair yards'),
        ('refinery_bays', 'live refinery bays'), ('harvesters_built', 'built/delivered ledger count, not purchases'),
        ('harvesters_lost', 'lost harvesters'), ('carryalls_built', 'built/delivered ledger count, not purchases'),
        ('refineries_built', 'built/delivered ledger count, not purchases'),
        ('repair_yards_built', 'built/delivered ledger count, not purchases'),
        ('fleet_capital_built_value', 'built ledger at current replacement prices; NOT actual capital spending'),
        ('army_value', 'current army replacement value'), ('credits', 'current credits'),
        ('structures', 'live structures'), ('storage_income_lost', 'observer-measured storage overflow credits'),
        ('fleet_gap_count', 'observer continuity gaps'))})
DEFAULTS['metrics']['total_paid_spending'] = dict(
    file='samples.csv', field='spent_total', aggregate='last', scope='house',
    require={'ledger_enabled': '1'}, unit='all actual paid spending ledger; NOT fleet-only capital')


def number(value):
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (TypeError, ValueError):
        return None


def integer(value):
    result = number(value)
    return int(result) if result is not None and result.is_integer() else None


def clock(row):
    cycle = number(row.get('cycle'))
    return cycle / 3750 if cycle is not None else number(row.get('minute'))


def metric_spec(path):
    result = copy.deepcopy(DEFAULTS)
    if path:
        override = STATS.load_json(path)
        if override.get('schema_version') != 1:
            raise ValueError('Metric mappings require schema_version=1')
        for family in ('metrics', 'features'):
            for name, settings in override.get(family, {}).items():
                # A field source and a derived ratio are mutually exclusive.
                if 'numerator' in settings or 'file' in settings:
                    old = result[family].get(name, {})
                    if ('numerator' in old) != ('numerator' in settings):
                        result[family][name] = {}
                result[family].setdefault(name, {}).update(settings)
    for family in ('metrics', 'features'):
        for name, settings in result[family].items():
            if 'numerator' in settings:
                if family != 'metrics' or not settings.get('denominator'):
                    raise ValueError(f'Invalid derived metric: {name}')
                continue
            file = Path(settings.get('file', ''))
            if not settings.get('field') or not str(file) or file.is_absolute() or '..' in file.parts:
                raise ValueError(f'Metric file/field must be an exact relative source: {name}')
            if settings.get('aggregate', 'unique') not in ('unique', 'first', 'last', 'delta', 'sum', 'mean', 'time_mean'):
                raise ValueError(f'Unsupported aggregation: {name}')
            if settings.get('scope', 'house') not in ('house', 'global', 'house_or_global', 'game'):
                raise ValueError(f'Unsupported scope: {name}')
    asset, paid = (result['metrics'].get(name, {}) for name in ('current_asset_credit', 'actual_capital_spending'))
    if (asset.get('file'), asset.get('field')) == (paid.get('file'), paid.get('field')):
        raise ValueError('Current asset stock and actual capital spending must map to distinct measured fields')
    return result


class Tables:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.cache = {}
        self.at_minute = None

    def rows(self, name):
        if name not in self.cache:
            path = self.directory / name
            if not path.exists() and Path(str(path) + '.gz').exists():
                path = Path(str(path) + '.gz')
            self.cache[name] = STATS.load_rows(path) if path.exists() else []
        return self.cache[name]

    def selected(self, settings, game):
        rows = [row for row in self.rows(settings['file']) if row.get('id') == game.get('id')]
        if self.at_minute is not None:
            rows = [row for row in rows if clock(row) is None or clock(row) <= self.at_minute]
        if not rows:
            return [], 'missing_rows'
        scope = settings.get('scope', 'house')
        if scope == 'game':
            return rows, 'ok'
        own = [row for row in rows if str(row.get('house', '')) == str(game.get('candidate_house'))]
        glob = [row for row in rows if row.get('house', '') == '']
        if scope == 'house_or_global':
            # Map schema deliberately puts some global fields only on house=''.
            own = [row for row in own if row.get(settings['field'], '') != '']
            return (own or glob), 'ok'
        return (glob if scope == 'global' else own), 'ok'

    def extract(self, settings, game, exposure):
        rows, status = self.selected(settings, game)
        if status != 'ok' or not rows:
            return None, status if status != 'ok' else 'missing_scope'
        field, aggregate = settings['field'], settings.get('aggregate', 'unique')
        if aggregate in ('first', 'last', 'delta', 'time_mean'):
            if any(clock(row) is None for row in rows):
                if len(rows) > 1 or settings.get('require_cycle_zero'):
                    return None, 'missing_time'
            else:
                rows.sort(key=clock)
            if settings.get('require_cycle_zero') and clock(rows[0]) != 0:
                return None, 'no_cycle_zero'
            if settings.get('exact_minute') is not None and clock(rows[-1]) != settings['exact_minute']:
                return None, 'checkpoint_not_observed'
            if aggregate in ('first', 'last'):
                chosen = rows[0] if aggregate == 'first' else rows[-1]
                same_time = [row for row in rows if clock(row) == clock(chosen)]
                reduction = settings.get('snapshot_reduce')
                if reduction:
                    raw = [number(row.get(field)) for row in same_time]
                    if any(value is None for value in raw):
                        return None, 'missing_snapshot_field'
                    if reduction not in ('min', 'max', 'mean', 'sum'):
                        raise ValueError(f'Unsupported snapshot_reduce: {reduction}')
                    reduced = {'min': min, 'max': max, 'mean': statistics.mean, 'sum': sum}[reduction](raw)
                    rows = [dict(chosen, **{field: reduced})]
                elif len({row.get(field, '') for row in same_time}) > 1:
                    return None, 'conflicting_same_time_values'
                else:
                    rows = [chosen]
        for row in rows:
            if any(str(row.get(key, '')) != str(value) for key, value in settings.get('require', {}).items()):
                return None, 'incomplete_measurement'
        values = [number(row.get(field)) for row in rows]
        if any(value is None for value in values):
            return None, 'missing_or_nonfinite_field'
        if aggregate == 'unique':
            if len(set(values)) != 1:
                return None, 'conflicting_values'
            value = values[0]
        elif aggregate in ('first', 'last'):
            value = values[0]
        elif aggregate == 'delta':
            value = values[-1] - values[0]
        elif aggregate == 'sum':
            value = sum(values)
        elif aggregate == 'mean':
            value = statistics.mean(values)
        else:
            times = [clock(row) for row in rows]
            if (len(rows) < 2 or times[0] != 0 or exposure is None
                    or times[-1] < exposure - 1 / 3750 or len(set(times)) != len(times)):
                return None, 'incomplete_time_exposure'
            value = sum((right - left) * (a + b) / 2
                        for left, right, a, b in zip(times, times[1:], values, values[1:])) / times[-1]
        value *= settings.get('scale', 1)
        if settings.get('per_minute'):
            if exposure is None or exposure <= 0:
                return None, 'missing_observed_exposure'
            value /= exposure
        return (value, 'ok') if math.isfinite(value) else (None, 'nonfinite_result')


def derive(metrics, specs, statuses):
    visiting = set()

    def resolve(name):
        if name in metrics:
            return metrics[name]
        if name in visiting or name not in specs:
            raise ValueError(f'Unknown/cyclic derived metric: {name}')
        visiting.add(name)
        settings = specs[name]
        numerator, denominator = resolve(settings['numerator']), resolve(settings['denominator'])
        if numerator is None or denominator is None:
            value, status = None, 'missing_derived_input'
        elif denominator <= 0:
            value, status = None, 'undefined_nonpositive_denominator'
        else:
            value, status = numerator / denominator, 'ok'
        visiting.remove(name)
        metrics[name], statuses[name] = value, status
        return value

    for name in specs:
        resolve(name)


def exposure_time(tables, game):
    minutes = number(game.get('minutes'))
    if minutes is not None and minutes > 0:
        return minutes, 'games.csv:minutes'
    times = [clock(row) for row in tables.rows('samples.csv') if row.get('id') == game.get('id')]
    times = [value for value in times if value is not None and value > 0]
    return (max(times), 'samples.csv:observed_time') if times else (None, 'unmeasured')


def band(value, bounds):
    if value is None:
        return 'unmeasured'
    if value < 0:
        return 'invalid_negative'
    if value == 0:
        return 'zero'
    low, high = bounds
    return f'positive_lt_{low:g}' if value < low else f'{low:g}_to_lt_{high:g}' if value < high else f'ge_{high:g}'


def normalize_control_game(raw, plan, run):
    """Adapt observed controls receipts, never unobserved historical controls."""
    game = dict(raw)
    cell = raw.get('cell', '').split('/')[0]
    matched = [(index, case) for index, case in enumerate(plan['cases'], 1)
               if case['cell'] == cell and str(case['seed']) == str(raw.get('seed'))]
    index, case = matched[0] if len(matched) == 1 else (None, {})
    house_ids = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
    side = raw.get('sides', '').split()
    first = side[0].split(':') if side else []
    candidate = house_ids.get(first[0]) if len(first) == 3 and first[2] == 'QB' else None
    expected_receipt = 'none' if run['style'] == 'native' else f'{candidate}=candidate-{run["style"]}'
    reasons = []
    if raw.get('style_seats') != expected_receipt:
        reasons.append('control_candidate_only_receipt_mismatch')
    observed = str(raw.get('control_diagnostics', '')) == '1'
    if not observed:
        reasons.append('control_diagnostic_receipt_missing')
    game.update(sample_case=index, sample_seed=plan['sample_seed'], candidate_house=candidate,
                candidate_seat='candidate', candidate_style='' if run['style'] == 'native' else run['style'],
                observer_mode='on' if observed else 'unmeasured')
    game.update({key: case.get(key) for key in ('format', 'size', 'spice_level', 'initial_tech')})
    return game, reasons, first


def checkpoint_observations(tables, game, final, specs, args):
    result = []
    for minute in args.checkpoints:
        row = dict(final, checkpoint_minute=minute)
        exposure = final['exposure_minutes']
        observed = exposure is not None and exposure + 1 / 3750 >= minute
        row.update(exposure_minutes=min(exposure, minute) if exposure is not None else None,
                   observed_to_checkpoint=observed)
        tables.at_minute = minute
        row['final_alive'] = final.get('alive')
        row['final_drawn'] = final.get('drawn')
        row['final_raw_drawn'] = final.get('raw_drawn')
        row['drawn'], row['raw_drawn'] = None, None
        if observed:
            row['alive'], row['alive_status'] = tables.extract(dict(
                file='samples.csv', field='alive', scope='house', aggregate='last', exact_minute=minute), game, minute)
        else:
            row['alive'], row['alive_status'] = None, 'checkpoint_not_observed'
        values, statuses = {}, {}
        for name, settings in specs['metrics'].items():
            if not settings.get('checkpoint', True):
                values[name], statuses[name] = None, 'final_outcome_not_snapshot'
            elif not observed:
                values[name], statuses[name] = None, 'game_ended_before_checkpoint' if exposure is not None else 'missing_exposure'
            elif 'numerator' not in settings:
                exact = dict(settings)
                if exact.get('aggregate') in ('last', 'delta', 'time_mean'):
                    exact['exact_minute'] = minute
                values[name], statuses[name] = tables.extract(exact, game, minute)
        derive(values, specs['metrics'], statuses)
        for name in values:
            row[name], row[name + '_status'] = values[name], statuses[name]
        for name, settings in specs['features'].items():
            row[name], row[name + '_status'] = tables.extract(settings, game, minute)
        row['distance_band'] = band(row.get('distance'), args.distance_bounds)
        row['available_spice_band'] = band(row.get('available_spice'), args.spice_bounds)
        result.append(row)
    tables.at_minute = None
    return result


def anchor_native_geometry(observations, checkpoints, args):
    """Keep ratio-band membership fixed across assignments of a common case.

    The native descriptor may first become available after cycle zero. It is a
    control-trajectory descriptor, NOT pre-treatment causal map heterogeneity.
    It is never replaced by a treatment's own post-treatment refinery state.
    """
    native = defaultdict(list)
    for row in observations:
        if row['style'] == 'native' and row['eligible']:
            native[row['stage'], row['mod'], row['cluster'], row['candidate_seat']].append(row)
    for row in observations + checkpoints:
        row['own_distance_band'] = row['distance_band']
        row['own_available_spice_band'] = row['available_spice_band']
        matches = native.get((row['stage'], row['mod'], row['cluster'], row['candidate_seat']), [])
        reference = matches[0] if len(matches) == 1 else None
        row['native_geometry_id'] = reference['id'] if reference else ''
        row['band_basis'] = 'shared native-arm first-observed geometry; control-trajectory stratum, not cycle-zero heterogeneity'
        for feature in ('distance', 'available_spice'):
            measured = reference is not None and reference.get(feature + '_status') == 'ok'
            row['native_' + feature] = reference.get(feature) if measured else None
            row['native_' + feature + '_status'] = 'ok' if measured else 'native_descriptor_unmeasured'
        row['distance_band'] = band(row['native_distance'], args.distance_bounds)
        row['available_spice_band'] = band(row['native_available_spice'], args.spice_bounds)


def load_observations(plans, specs, args):
    observations, availability, checkpoints = [], [], []
    for plan in plans:
        arms = {arm['style']: arm for arm in plan['arms']}
        for run in plan['commands']:
            directory = Path(run['result_dir'])
            tables = Tables(directory)
            games = tables.rows('games.csv')
            availability.append(dict(stage=plan['stage'], mod=run['mod'], style=run['style'],
                                     result_dir=str(directory), captured_game_rows=len(games)))
            id_counts = Counter(game.get('id', '') for game in games)
            authored = plan.get('case_mode') == 'authored'
            for raw_game in games:
                game, control_reasons, first_side = normalize_control_game(raw_game, plan, run) if authored else (raw_game, [], [])
                case_index = integer(game.get('sample_case'))
                case = plan['cases'][case_index - 1] if case_index and 1 <= case_index <= len(plan['cases']) else {}
                reasons = ([game['error']] if game.get('error') else []) + control_reasons
                if id_counts[game.get('id', '')] != 1:
                    reasons.append('duplicate_game_id')
                if game.get('observer_mode') != 'on':
                    reasons.append('observer_not_on')
                if not case or str(game.get('sample_seed')) != str(plan['sample_seed']):
                    reasons.append('case_identity_missing_or_wrong')
                if any(str(game.get(key)) != str(case.get(key))
                       for key in ('seed', 'format', 'size', 'spice_level', 'initial_tech')):
                    reasons.append('case_cell_mismatch')
                expected_seats = ('candidate',) if authored else ('original', 'swapped')
                if game.get('candidate_seat') not in expected_seats or game.get('candidate_house') in ('', None):
                    reasons.append('candidate_identity_missing')
                expected_style = '' if run['style'] == 'native' else run['style']
                if game.get('candidate_style', '') != expected_style or game.get('mod') != run['mod']:
                    reasons.append('candidate_style_or_mod_mismatch')
                if plan.get('source_sha') and game.get('source_commit') != plan['source_sha']:
                    reasons.append('source_pin_mismatch')
                exposure, exposure_source = exposure_time(tables, game)
                row = dict(stage=plan['stage'], mod=run['mod'], style=run['style'], id=game.get('id', ''),
                           result_dir=str(directory), sample_seed=game.get('sample_seed', ''),
                           sample_case=case_index, cluster=f'{game.get("sample_seed", "")}:{case_index}',
                           case_mode='authored' if authored else 'generated', seats_per_case=1 if authored else 2,
                           map=case.get('map', ''), cell=case.get('cell', ''),
                           candidate_seat=game.get('candidate_seat', ''), candidate_house=game.get('candidate_house', ''),
                           exposure_minutes=exposure, exposure_source=exposure_source,
                           observer_mode=game.get('observer_mode', ''), error=';'.join(reasons),
                           eligible=not reasons, planned_minutes=plan['protocol']['minutes'])
                row.update({key: game.get(key, '') for key in RECEIPTS})
                row.update({key: case.get(key, game.get(key, ''))
                            for key in ('format', 'size', 'spice_level', 'initial_tech', 'composition', 'seed')})
                arm = arms[run['style']]
                row['factorial'] = bool(arm.get('enabled'))
                for name, _ in RATIO_LEVELS:
                    row[name] = arm.get(name) if row['factorial'] else None
                for field in ('alive', 'eliminated_s', 'drawn', 'raw_drawn'):
                    value, status = tables.extract(dict(file='houses.csv', field=field, aggregate='unique', scope='house'), game, exposure)
                    row[field] = value
                    row[field + '_status'] = status
                values, statuses = {}, {}
                for name, settings in specs['metrics'].items():
                    if 'numerator' not in settings:
                        values[name], statuses[name] = tables.extract(settings, game, exposure)
                if authored:
                    # Controls have score/place but no houses.csv won field.
                    # Their win is the recorded engine winner, not a fabricated
                    # population adjudication or score-derived pseudo-win.
                    for name in ('win', 'raw_conquest_win'):
                        settings = specs['metrics'].get(name, {})
                        if settings.get('file') == 'houses.csv' and settings.get('field') in ('won', 'raw_won'):
                            winner, team = integer(game.get('winner_team')), integer(first_side[1]) if first_side else None
                            values[name] = float(winner == team) if winner is not None and team is not None else None
                            statuses[name] = 'ok' if values[name] is not None else 'missing_recorded_winner'
                derive(values, specs['metrics'], statuses)
                for name, value in values.items():
                    row[name], row[name + '_status'] = value, statuses[name]
                for name, settings in specs['features'].items():
                    row[name], row[name + '_status'] = tables.extract(settings, game, exposure)
                row['distance_band'] = band(row.get('distance'), args.distance_bounds)
                row['available_spice_band'] = band(row.get('available_spice'), args.spice_bounds)
                observations.append(row)
                checkpoints.extend(checkpoint_observations(tables, game, row, specs, args))
    # A named case is not a license to pair different engine maps/content.
    common = defaultdict(list)
    for row in observations + checkpoints:
        if row['eligible']:
            common[row['stage'], row['mod'], row['cluster'], row['candidate_seat'], row.get('checkpoint_minute', 'final')].append(row)
    for group in common.values():
        if any(len({row[key] for row in group if row[key] != ''}) > 1 for key in RECEIPTS):
            for row in group:
                row['eligible'] = False
                row['error'] = ';'.join(filter(None, (row['error'], 'common_case_receipt_mismatch')))
    anchor_native_geometry(observations, checkpoints, args)
    return observations, availability, checkpoints


def interval(values, rounds, seed):
    """One value per common case, not one per physical game/house/snapshot."""
    values = list(values)
    center = statistics.mean(values) if values else None
    if len(values) < 4:
        return dict(estimate=center, ci_low=None, ci_high=None,
                    ci_status='unmeasured' if not values else 'too_sparse_lt_4_common_cases')
    rng = random.Random(seed)
    draws = sorted(statistics.mean(values[rng.randrange(len(values))] for _ in values) for _ in range(rounds))
    return dict(estimate=center, ci_low=draws[rounds // 40], ci_high=draws[rounds - rounds // 40 - 1], ci_status='ok_pointwise')


def usable(row, metric):
    return row['eligible'] and row.get(metric) is not None and row.get(metric + '_status') == 'ok'


def summary(rows, metric, args):
    clusters = defaultdict(list)
    for row in rows:
        if usable(row, metric):
            clusters[row['cluster']].append(row[metric])
    return dict(n_games=len(rows), n_measured_games=sum(map(len, clusters.values())), n_cases=len(clusters),
                n_eliminated=sum(row.get('alive') == 0 for row in rows),
                n_captured_cases=len({row['cluster'] for row in rows}),
                n_invalid_games=sum(not row['eligible'] for row in rows),
                n_censored_before_checkpoint=sum(row.get('observed_to_checkpoint') is False for row in rows),
                n_zero_income=sum(usable(row, 'spice_income_per_minute') and row['spice_income_per_minute'] == 0 for row in rows),
                **interval([statistics.mean(clusters[key]) for key in sorted(clusters)], args.bootstrap, args.resample_seed))


def strata(row):
    yield 'overall', 'all', 'all', 'all'
    if row.get('case_mode') == 'authored':
        yield 'map:' + row['map'], 'all', 'all', 'all'
    yield 'distance', row['distance_band'], 'all', 'all'
    yield 'available_spice', 'all', row['available_spice_band'], 'all'
    yield 'seat', 'all', 'all', row['candidate_seat']
    yield 'distance_available_spice_seat', row['distance_band'], row['available_spice_band'], row['candidate_seat']


def ratio_tables(observations, metrics, args):
    groups = defaultdict(list)
    native = {}
    for row in observations:
        for values in strata(row):
            groups[(row['stage'], row['mod'], row['style'], str(row.get('checkpoint_minute', 'final'))) + values].append(row)
        if row['style'] == 'native':
            native[row['stage'], row['mod'], row['cluster'], row['candidate_seat'], str(row.get('checkpoint_minute', 'final'))] = row
    absolute, paired = [], []
    for key, rows in sorted(groups.items()):
        stage, mod, style, checkpoint, kind, distance, spice, seat = key
        context = dict(stage=stage, mod=mod, style=style, checkpoint_minute=checkpoint, stratum=kind, distance_band=distance,
                       available_spice_band=spice, candidate_seat=seat, band_basis=rows[0]['band_basis'])
        for metric in metrics:
            absolute.append(dict(context, metric=metric, **summary(rows, metric, args)))
            if style == 'native':
                continue
            differences = defaultdict(list)
            matches = 0
            for row in rows:
                reference = native.get((stage, mod, row['cluster'], row['candidate_seat'], checkpoint))
                if reference and usable(row, metric) and usable(reference, metric):
                    differences[row['cluster']].append(row[metric] - reference[metric])
                    matches += 1
            # Generated overall contrasts require both swapped seats. Authored
            # controls have one actual candidate seat, never invented swaps.
            expected_seats = rows[0]['seats_per_case']
            complete = {cluster: values for cluster, values in differences.items()
                        if kind != 'overall' or len(values) == expected_seats}
            paired.append(dict(context, metric=metric, reference='native', n_candidate_games=len(rows),
                               n_matched_games=matches, n_complete_cases=len(complete),
                               n_incomplete_cases=len(differences) - len(complete),
                               **interval([statistics.mean(complete[key]) for key in sorted(complete)],
                                          args.bootstrap, args.resample_seed)))
    return absolute, paired


def coverage_table(plans, observations, metrics):
    grouped = defaultdict(list)
    for row in observations:
        grouped[row['stage'], row['mod'], row['style']].append(row)
    output = []
    for plan in plans:
        for run in plan['commands']:
            rows = grouped[plan['stage'], run['mod'], run['style']]
            if plan.get('case_mode') == 'authored':
                for case in plan['cases']:
                    captured = [row for row in rows if row['cell'] == case['cell'] and str(row['seed']) == str(case['seed'])]
                    measured = [row for row in captured if row['eligible']]
                    entry = dict(stage=plan['stage'], mod=run['mod'], style=run['style'], map=case['map'],
                                 cell=case['cell'], seed=case['seed'], format=case['format'], size=case['size'],
                                 spice_level='authored', initial_tech=None, planned_cases=1, planned_games=1,
                                 captured_games=len(captured), valid_games=len(measured), tested_cases=int(bool(measured)),
                                 coverage_status='untested' if not measured else 'complete' if len(measured) == 1 else 'duplicate')
                    entry.update({name + '_measured_games': sum(usable(row, name) for row in captured) for name in metrics})
                    output.append(entry)
                continue
            for fmt, size, spice, tech in itertools.product(FORMATS, SIZES, SPICES, TECHS):
                def in_cell(row):
                    return (row.get('format') == fmt and integer(row.get('size')) == size
                            and row.get('spice_level') == spice and integer(row.get('initial_tech')) == tech)
                planned = [cell for cell in plan['cases'] if in_cell(cell)]
                captured = [row for row in rows if in_cell(row)]
                measured = [row for row in captured if row['eligible']]
                entry = dict(stage=plan['stage'], mod=run['mod'], style=run['style'], format=fmt, size=size,
                             spice_level=spice, initial_tech=tech, planned_cases=len(planned),
                             planned_games=2 * len(planned), captured_games=len(captured), valid_games=len(measured),
                             tested_cases=len({row['cluster'] for row in measured}),
                             original_games=sum(row['candidate_seat'] == 'original' for row in measured),
                             swapped_games=sum(row['candidate_seat'] == 'swapped' for row in measured),
                             coverage_status='untested' if not measured else 'complete' if len(measured) == 2 * len(planned) else 'partial')
                entry.update({name + '_measured_games': sum(usable(row, name) for row in captured) for name in metrics})
                output.append(entry)
    return output


def factorial_basis(row):
    names, values = ['Intercept'], [1.0]
    dummies = [[(f'{name}[{level}]', float(row.get(name) == level)) for level in levels[1:]]
               for name, levels in RATIO_LEVELS]
    for degree in (1, 2, 3):
        for indices in itertools.combinations(range(3), degree):
            for combination in itertools.product(*(dummies[index] for index in indices)):
                names.append(':'.join(term[0] for term in combination))
                values.append(math.prod(term[1] for term in combination))
    return names, values


def model_matrix(rows, map_features):
    names = factorial_basis({})[0]
    centers = {}
    for feature in map_features:
        values = [row[feature] for row in rows]
        center = statistics.mean(values)
        scale = statistics.pstdev(values) or 1.0
        centers[feature] = dict(center=center, scale=scale)
    contexts = (('format', FORMATS), ('size', SIZES), ('spice_level', SPICES),
                ('initial_tech', TECHS), ('composition', COMPOSITIONS),
                ('candidate_seat', ('original', 'swapped')))
    columns = list(names)
    for feature in map_features:
        columns += [f'{term}:{feature}_z' for term in names]
    columns += [f'map_control:{field}[{level}]' for field, levels in contexts for level in levels[1:]]
    matrix = []
    for row in rows:
        base = factorial_basis(row)[1]
        values = list(base)
        for feature in map_features:
            standardized = (row[feature] - centers[feature]['center']) / centers[feature]['scale']
            values += [value * standardized for value in base]
        values += [float(str(row[field]) == str(level)) for field, levels in contexts for level in levels[1:]]
        matrix.append(values)
    return columns, matrix, centers


def solve(np, matrix, response, weights):
    """SVD estimates are published ONLY for estimable individual coefficients."""
    root = np.sqrt(np.asarray(weights, dtype=float))
    x, y = matrix * root[:, None], response * root
    u, singular, vt = np.linalg.svd(x, full_matrices=x.shape[0] < x.shape[1])
    tolerance = (singular[0] * max(x.shape) * np.finfo(float).eps * 100) if len(singular) else 0
    rank = int(np.sum(singular > tolerance))
    beta = vt[:rank].T @ ((u[:, :rank].T @ y) / singular[:rank]) if rank else np.zeros(x.shape[1])
    null_energy = np.sum(vt[rank:] ** 2, axis=0)
    identifiable = null_energy < 1e-8
    return beta, identifiable, rank


def fit_models(observations, specs, args, np):
    grouped = defaultdict(list)
    for row in observations:
        if row['factorial'] and row['eligible']:
            grouped[row['stage'], row['mod']].append(row)
    coefficients, diagnostics = [], []
    for (stage, mod), all_rows in sorted(grouped.items()):
        all_clusters = sorted({row['cluster'] for row in all_rows})
        rng = random.Random(args.resample_seed)
        # The SAME case resampling schedule applies to every treatment, seat,
        # outcome and model in this stage/mod. Missing cases are not zero-filled.
        draws = [Counter(rng.choices(all_clusters, k=len(all_clusters))) for _ in range(args.bootstrap)]
        causal_features = all(specs['features'][name].get('file') == 'map-features.csv'
                              and specs['features'][name].get('require_cycle_zero')
                              and specs['features'][name].get('aggregate') == 'first'
                              for name in args.model_features)
        interaction_label = 'factorial_x_cycle0_map_features' if causal_features else 'factorial_x_mechanism_features'
        for label, features in (('categorical_factorial', []), (interaction_label, args.model_features)):
            feature_rows = [row for row in all_rows if all(row.get(feature) is not None
                            and row.get(feature + '_status') == 'ok' for feature in features)]
            for metric in args.model_metrics:
                rows = [row for row in feature_rows if usable(row, metric)]
                detail = dict(stage=stage, mod=mod, model=label, metric=metric,
                              n_games=len(rows), n_cases=len({row['cluster'] for row in rows}),
                              missing_feature_games=len(all_rows) - len(feature_rows),
                              missing_metric_games=len(feature_rows) - len(rows),
                              formula='categorical(H)*categorical(C)*categorical(R)' +
                                      (' * (' + ' + '.join(features) + ')' if features else '') +
                                      ' + format + size + spice_level + initial_tech + composition + candidate_seat',
                              outcome_model='linear probability' if metric in ('win', 'raw_conquest_win') else 'linear mean',
                              reference='H=2, C=0 quarter carryalls, R=0; map controls at first declared level',
                              interaction_interpretation=('Assigned-ratio effect with cycle-zero pre-treatment map modifiers'
                                                          if not features or causal_features else
                                                          'Predictive/mechanism-only modifier; no post-treatment causal heterogeneity claim'),
                              controls_not_factorial='native and explicit OFF excluded from ratio coefficient fits')
                if not rows:
                    diagnostics.append(dict(detail, status='unmeasured', rank=None, columns=None))
                    continue
                columns, matrix, centers = model_matrix(rows, features)
                matrix = np.asarray(matrix, dtype=float)
                response = np.asarray([row[metric] for row in rows], dtype=float)
                counts = Counter(row['cluster'] for row in rows)
                weights = [1 / counts[row['cluster']] for row in rows]
                beta, identifiable, rank = solve(np, matrix, response, weights)
                bootstrap_values = [[] for _ in columns]
                if len(counts) >= 4:
                    for draw in draws:
                        sampled_weights = [draw[row['cluster']] / counts[row['cluster']] for row in rows]
                        sampled, estimable, _ = solve(np, matrix, response, sampled_weights)
                        for index in range(len(columns)):
                            if identifiable[index] and estimable[index] and math.isfinite(float(sampled[index])):
                                bootstrap_values[index].append(float(sampled[index]))
                diagnostics.append(dict(detail, status='full_rank' if rank == len(columns) else 'rank_deficient',
                                        rank=rank, columns=len(columns), standardized_features=centers,
                                        identifiable_coefficients=int(sum(identifiable))))
                for index, name in enumerate(columns):
                    values = sorted(bootstrap_values[index])
                    if not identifiable[index]:
                        estimate, low, high, status = None, None, None, 'nonidentifiable'
                    else:
                        estimate, low, high = float(beta[index]), None, None
                        if len(counts) < 4:
                            status = 'too_sparse_lt_4_common_cases'
                        elif len(values) != args.bootstrap:
                            status = 'nonidentifiable_resamples'
                        else:
                            low, high = values[args.bootstrap // 40], values[args.bootstrap - args.bootstrap // 40 - 1]
                            status = 'ok_pointwise'
                    coefficients.append(dict(stage=stage, mod=mod, model=label, metric=metric, term=name,
                                             n_games=len(rows), n_cases=len(counts), rank=rank, columns=len(columns),
                                             estimate=estimate, ci_low=low, ci_high=high, ci_status=status,
                                             estimable_resamples=len(values), requested_resamples=args.bootstrap))
    return coefficients, diagnostics


def write_table(path, rows, empty_fields):
    fields = list(dict.fromkeys(key for row in rows for key in row)) if rows else list(empty_fields)
    STATS.write_rows(path, fields, rows)


def parser():
    cli = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    cli.add_argument('--design', nargs='+', required=True, type=Path, help='One or more fixed design.json plans; stages remain separate')
    cli.add_argument('--output', required=True, type=Path, help='New analysis output directory; captured game evidence is never changed')
    cli.add_argument('--metrics', type=Path, help='Schema-v1 exact observer field/source overrides, as described above')
    cli.add_argument('--bootstrap', type=int, default=2000, help='Common-case cluster draws, minimum 200; default 2000')
    cli.add_argument('--resample-seed', type=int, default=1180000)
    cli.add_argument('--distance-bounds', nargs=2, type=float, default=(16, 48), metavar=('NEAR', 'FAR'),
                     help='Fixed distance band boundaries in mapped observer units (default ground steps: 16, 48)')
    cli.add_argument('--spice-bounds', nargs=2, type=float, default=(10000, 50000), metavar=('LOW', 'HIGH'),
                     help='Fixed available-spice boundaries in exact mapped observer units, not quantiles')
    cli.add_argument('--model-features', nargs='+', default=['base_distance', 'initial_spice_density', 'initial_spice_amount'],
                     help='Default: cycle-zero StatsDesign map modifiers. Post-treatment overrides are labeled predictive/mechanism-only')
    cli.add_argument('--model-metrics', nargs='+', default=['spice_income_per_minute', 'current_asset_credit',
                     'spice_income_per_minute_per_asset_credit', 'actual_capital_spending', 'win', 'placement'],
                     help='Mapped outcomes to fit; other captured metrics still appear in ratio/checkpoint tables')
    cli.add_argument('--checkpoints', type=int, nargs='+', default=[3, 5, 8, 10, 15],
                     help='Exact observed game-minute snapshots; early endings remain explicitly censored, not extrapolated')
    return cli


def main():
    cli = parser()
    args = cli.parse_args()
    if args.bootstrap < 200 or any(not 0 < low < high for low, high in (args.distance_bounds, args.spice_bounds)):
        cli.error('Use >=200 bootstrap draws and positive increasing band boundaries')
    try:
        import numpy as np
    except ImportError:
        cli.error('Rank-aware model fitting requires NumPy in the analysis Python environment')
    try:
        specs = metric_spec(args.metrics)
        if any(name not in specs['features'] for name in args.model_features):
            raise ValueError('--model-features must name mapped features')
        if any(name not in specs['metrics'] for name in args.model_metrics):
            raise ValueError('--model-metrics must name mapped metrics')
        if any(minute <= 0 for minute in args.checkpoints) or len(set(args.checkpoints)) != len(args.checkpoints):
            raise ValueError('--checkpoints must be distinct positive game minutes')
        plans = [STATS.load_json(path) for path in args.design]
        if any(plan.get('schema_version') != 1 or plan.get('kind') != 'economic-fleet-full-categorical-factorial' for plan in plans):
            raise ValueError('Use schema-v1 economic-fleet design.json files')
        parents = plans
        plans = []
        for parent in parents:
            plans.append(parent)
            plans.extend(dict(block, declared_base=parent['declared_base'], source_sha=parent.get('source_sha'))
                         for block in parent.get('additional_blocks', []))
        if len({plan['stage'] for plan in plans}) != len(plans):
            raise ValueError('Do not count the same stage twice')
        if args.output.exists() and any(args.output.iterdir()):
            raise ValueError('Use an empty/new output directory; previous analysis evidence is preserved')
        observations, availability, checkpoints = load_observations(plans, specs, args)
        absolute, paired = ratio_tables(observations, specs['metrics'], args)
        checkpoint_absolute, checkpoint_paired = ratio_tables(checkpoints, specs['metrics'], args)
        coverage = coverage_table(plans, observations, specs['metrics'])
        coefficients, diagnostics = fit_models(observations, specs, args, np)
        output = args.output.resolve()
        report = dict(schema_version=1, kind='economic-fleet-categorical-analysis',
                      designs=[str(path.resolve()) for path in args.design],
                      provenance=[dict(stage=plan['stage'], declared_base=plan['declared_base'], source_sha=plan.get('source_sha'),
                                       case_manifest_sha256=plan['case_manifest_sha256']) for plan in plans],
                      metric_mapping=specs, captured_runs=availability, captured_games=len(observations),
                      eligible_games=sum(row['eligible'] for row in observations),
                      error_counts=dict(Counter(reason for row in observations for reason in row['error'].split(';') if reason)),
                      metric_statuses={name: dict(Counter(row.get(name + '_status', '') for row in observations)) for name in specs['metrics']},
                      model_diagnostics=diagnostics, bootstrap=dict(draws=args.bootstrap, seed=args.resample_seed,
                        checkpoint_minutes=args.checkpoints,
                        cluster='stage/mod/sample_seed/sample_case; both seats and all treatments stay together',
                        interval='pointwise percentile 95%; minimum four common cases; no fabricated rank-deficient intervals'),
                      bands=dict(distance=list(args.distance_bounds), available_spice=list(args.spice_bounds),
                                 units='Exact mapped feature units; zero and unmeasured are separate explicit bands',
                                 basis='Same native-arm earliest available refinery descriptor for every arm of a common case/seat; not cycle-zero causal heterogeneity'),
                      estimands=dict(absolute='Equal-weight common-case means of physical candidate game values',
                                     paired='Candidate minus shared native, same common case and physical seat; generated overall requires both seats, authored controls retain one actual seat',
                                     current_asset_credit='Observed current replacement-price stock; not cumulative purchases',
                                     actual_capital_spending='Observed actual cumulative paid capital; not accepted-order quotes or current stock',
                                     asset_efficiency='Exact refined spice / integrated current replacement-asset credit-minutes; never built ledger or quoted orders',
                                     spice_income_per_minute='Observed spice-gross ledger / observed actual game exposure, not fixed cap',
                                     win_placement='All observed results, including losses, eliminations and draws; no income-based selection'),
                      caveats=['No automatic style selection or numeric optimization.',
                               'Screen-throughput is not a full-game promotion gate; stages and mods remain separate.',
                               'Missing/error observations remain in observations and coverage; they are never fabricated zero losses.',
                               'Checkpoint estimates require actual follow-up to that minute; censored counts and all final outcomes remain visible, never survival-imputed zeros.',
                               'Causal map interactions default to cycle-zero StatsDesign features. Own-arm distance/bay utilization are post-treatment mechanisms; native-anchored ratio bands are control-trajectory strata, not pre-treatment map features.',
                               'Sparse and aliased terms have null CIs; pointwise intervals are not multiple-comparison promotion evidence.',
                               'Any runtime feature-conditioned ratio rule requires a separately preregistered fresh-seed test; no rule is selected or promoted here.',
                               'Unpinned design provenance is explicitly null and is not proof of a passed parent source gate.'])
        output.mkdir(parents=True, exist_ok=True)
        write_table(output / 'observations.csv', observations, ('stage', 'mod', 'style', 'id', 'eligible', 'error'))
        write_table(output / 'checkpoints.csv', checkpoints, ('stage', 'mod', 'style', 'id', 'checkpoint_minute', 'observed_to_checkpoint'))
        write_table(output / 'checkpoint-ratios.csv', checkpoint_absolute, ('stage', 'mod', 'style', 'checkpoint_minute', 'metric', 'estimate', 'ci_status'))
        write_table(output / 'checkpoint-paired-native.csv', checkpoint_paired, ('stage', 'mod', 'style', 'checkpoint_minute', 'metric', 'estimate', 'ci_status'))
        write_table(output / 'ratio-table.csv', absolute, ('stage', 'mod', 'style', 'metric', 'estimate', 'ci_low', 'ci_high', 'ci_status'))
        write_table(output / 'paired-native.csv', paired, ('stage', 'mod', 'style', 'metric', 'estimate', 'ci_low', 'ci_high', 'ci_status'))
        write_table(output / 'coverage.csv', coverage, ('stage', 'mod', 'style', 'format', 'size', 'spice_level', 'initial_tech', 'coverage_status'))
        write_table(output / 'model-coefficients.csv', coefficients, ('stage', 'mod', 'model', 'metric', 'term', 'estimate', 'ci_low', 'ci_high', 'ci_status'))
        STATS.save_json(output / 'analysis.json', report)
        print(json.dumps(dict(output=str(output), captured_games=len(observations),
                              eligible_games=report['eligible_games'], untested_cells=sum(row['coverage_status'] == 'untested' for row in coverage)), indent=2))
    except (OSError, ValueError, KeyError) as exc:
        cli.error(str(exc))


if __name__ == '__main__':
    main()
