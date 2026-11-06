"""Finite, provenance, clustering and independent-objective gate contract fixtures.

These tests exercise only stdlib report logic; they do not build or launch games.
"""
import contextlib
import copy
import csv
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
SPEC = importlib.util.spec_from_file_location('ai_baseline_gate', HERE / 'ai-baseline-gate.py')
gate = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = gate
SPEC.loader.exec_module(gate)


def digest(value):
    return hashlib.sha256(value.encode('utf-8')).hexdigest()


def fixtures(release='current', arm='on', seeds=range(8), houses=('0', '1'), research='off',
             values=None, swap=False, historical=False, spice='none'):
    rows = []
    source = digest('source-' + (release if historical else 'current'))
    for seed in seeds:
        for orientation in ('', '/swap') if swap else ('',):
            identifier = 'matrix/cy-a0-c0/128/%s/1v1/hard/40m/%s%s' % (spice, seed, orientation)
            for house in houses:
                context = gate.encoded(dict(id=identifier, mod='dunecity', difficulty='hard', research=research, seed=str(seed)))
                common = dict(schema_version=1, release=release, source_commit=source,
                              baseline_release_source=source, arm=arm, id=identifier, seed=str(seed),
                              mod='dunecity', difficulty='hard', research=research, house=house,
                              team=str(int(house) + 1), map_sha256=digest('map-' + str(seed)),
                              options_sha256=digest('options-' + research), context_sha256=digest(context),
                              content_hash=digest('content-' + (release if historical else 'current')),
                              object_data_hash=digest('objects-' + (release if historical else 'current')),
                              minute=-1, status='measured', reason='')
                for metric in gate.CORE_FAMILIES:
                    value = (0.5 if metric == 'win_score'
                             else 0.1 if metric in ('economy_stall_fraction', 'base_loss_fraction')
                             else 1.0 if metric.startswith('adopt_') or metric == 'research_level'
                             else 0.2 if metric.endswith('_live_value_share')
                             else 10.0 if metric.startswith('zone_')
                             else 1000.0 if metric == 'population' else 100.0)
                    if values and metric in values:
                        override = values[metric]
                        value = override(seed, house, orientation) if callable(override) else override
                    observation = dict(common, metric=metric, value=value)
                    if metric == 'research_level' and research == 'off':
                        observation.update(value='', status='not_applicable',
                                           reason='research_off_or_unsupported_not_emulated')
                    rows.append(observation)
    return rows


def historical(release='1.0.792', **kwargs):
    return fixtures(release=release, arm='historical', historical=True, **kwargs)


def analyze(history=None, control=None, candidate=None, **options):
    return gate.evaluate(historical() if history is None else history,
                         fixtures(arm='off') if control is None else control,
                         fixtures() if candidate is None else candidate,
                         bootstrap_samples=499, **options)


def primary(comparisons, metric, kind='historical', house='0'):
    return next(row for row in comparisons if row['kind'] == kind and row['scope'] == 'context_house'
                and row['metric'] == metric and row['house'] == house)


class InputContractTests(unittest.TestCase):
    def assert_code(self, code, callback):
        with self.assertRaises(gate.GateError) as caught:
            callback()
        self.assertEqual(code, caught.exception.code)

    def test_rejects_nonfinite_even_when_not_measured(self):
        for value in (float('nan'), float('inf'), -float('inf')):
            for status in ('measured', 'censored', 'unavailable'):
                rows = fixtures()
                rows[0].update(value=value, status=status)
                with self.subTest(value=value, status=status):
                    self.assert_code('NONFINITE_METRIC', lambda: analyze(candidate=rows))

    def test_rejects_nonfinite_minute(self):
        rows = fixtures()
        rows[0]['minute'] = float('inf')
        self.assert_code('INVALID_OBSERVATION', lambda: analyze(candidate=rows))

    def test_source_values_cannot_be_missing(self):
        for field in ('source_commit', 'baseline_release_source', 'content_hash', 'object_data_hash',
                      'map_sha256', 'options_sha256', 'context_sha256'):
            rows = fixtures()
            rows[0][field] = ''
            with self.subTest(field=field):
                self.assert_code('MISSING_IDENTITY', lambda: analyze(candidate=rows))

    def test_conflicting_duplicate_is_refused_not_averaged(self):
        rows = fixtures()
        duplicate = dict(rows[0], value=rows[0]['value'] + 1)
        rows.append(duplicate)
        self.assert_code('CONFLICTING_DUPLICATE', lambda: analyze(candidate=rows))

    def test_duplicate_with_changed_receipt_is_also_conflicting(self):
        rows = fixtures()
        rows.append(dict(rows[0], map_sha256=digest('different')))
        self.assert_code('CONFLICTING_DUPLICATE', lambda: analyze(candidate=rows))

    def test_identical_duplicates_do_not_inflate_seeds(self):
        rows = fixtures()
        rows += copy.deepcopy(rows)
        report, _, _, comparisons = analyze(candidate=rows)
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        self.assertEqual(8, primary(comparisons, 'win_score')['paired_seed_count'])
        self.assertEqual(len(rows) // 2, report['exact_duplicates_removed']['candidate'])

    def test_core_cannot_be_marked_not_applicable(self):
        rows = fixtures()
        rows[0].update(status='not_applicable', value='', reason='unsupported')
        report, _, _, comparisons = analyze(candidate=rows)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        self.assertIn('UNMEASURED_CORE_OR_OBJECTIVE', primary(comparisons, 'win_score')['coverage_errors'])

    def test_unsupported_research_cannot_be_mislabeled_on(self):
        rows = historical(research='on')
        for row in rows:
            if row['metric'] == 'research_level':
                row.update(status='not_applicable', value='', reason='unsupported_in_release')
        self.assert_code('UNSUPPORTED_RESEARCH_LABEL', lambda: analyze(history=rows))

    def test_on_requires_explicit_native_research_observation(self):
        rows = [row for row in fixtures(research='on') if row['metric'] != 'research_level']
        self.assert_code('RESEARCH_SUPPORT_UNDECLARED', lambda: analyze(candidate=rows))


class MatchingTests(unittest.TestCase):
    def test_off_on_requires_same_native_source_content_and_options(self):
        for field in ('source_commit', 'baseline_release_source', 'content_hash', 'object_data_hash',
                      'map_sha256', 'options_sha256', 'context_sha256'):
            rows = fixtures()
            for row in rows:
                row[field] = digest('different-' + field + '-' + row['seed'])
            report, _, _, comparisons = analyze(candidate=rows)
            comparison = primary(comparisons, 'kill_value', kind='current_off')
            with self.subTest(field=field):
                self.assertEqual('INCONCLUSIVE', report['verdict'])
                self.assertIn('MISMATCH_' + field.upper(), comparison['coverage_errors'])
                self.assertEqual(0, comparison['paired_seed_count'])

    def test_whole_release_can_change_source_and_content_not_map_or_requested_options(self):
        report, _, _, comparisons = analyze()
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        self.assertFalse(primary(comparisons, 'kill_value')['coverage_errors'])
        for field in ('map_sha256', 'options_sha256'):
            rows = historical()
            for row in rows:
                row[field] = digest('different-' + row['seed'])
            changed, _, _, comparisons = analyze(history=rows)
            with self.subTest(field=field):
                self.assertEqual('INCONCLUSIVE', changed['verdict'])
                self.assertIn('MISMATCH_' + field.upper(), primary(comparisons, 'kill_value')['coverage_errors'])

    def test_equal_house_observations_do_not_excuse_unequal_game_roster(self):
        rows = fixtures(houses=('0',))
        report, _, _, comparisons = analyze(candidate=rows)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        self.assertIn('MISMATCH_ROSTER', primary(comparisons, 'kill_value')['coverage_errors'])

    def test_team_changes_are_not_matched(self):
        rows = fixtures()
        for row in rows:
            if row['house'] == '1':
                row['team'] = '1'
        report, _, _, comparisons = analyze(candidate=rows)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        self.assertIn('MISMATCH_ROSTER', primary(comparisons, 'win_score')['coverage_errors'])

    def test_seed_intersection_is_not_accepted_as_complete_coverage(self):
        report, _, _, comparisons = analyze(candidate=fixtures(seeds=range(1, 9)))
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        self.assertIn('UNEQUAL_SEED_SETS', primary(comparisons, 'kill_value')['coverage_errors'])
        self.assertEqual(0, primary(comparisons, 'kill_value')['paired_seed_count'])

    def test_candidate_cannot_drop_frozen_historical_context(self):
        history = historical() + historical(spice='medium')
        report, _, _, comparisons = analyze(history=history)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        missing = [row for row in comparisons if row['kind'] == 'historical' and row['spice'] == 'medium']
        self.assertTrue(missing)
        self.assertTrue(all('INCOMPLETE_MATCHED_UNITS' in row['coverage_errors'] for row in missing))

    def test_missing_core_row_is_explicit_unavailable_not_silently_dropped(self):
        rows = fixtures()
        del rows[next(index for index, row in enumerate(rows) if row['metric'] == 'kill_value')]
        report, metrics, _, comparisons = analyze(candidate=rows)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        self.assertEqual(1, report['coverage']['candidate']['missing'])
        self.assertIn('MISSING_CORE_METRIC', primary(comparisons, 'kill_value')['coverage_errors'])
        self.assertTrue(any(row['role'] == 'candidate' and row['metric'] == 'kill_value'
                            and row['missing'] == 1 for row in metrics))

    def test_unavailable_historical_objective_never_becomes_zero_or_pass(self):
        rows = historical()
        for row in rows:
            if row['metric'] == 'special_built':
                row.update(status='unavailable', value='', reason='native_lifetime_ledger_absent')
        report, metrics, best, comparisons = analyze(history=rows)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        comparison = primary(comparisons, 'special_built')
        self.assertIsNone(comparison['reference_estimate'])
        self.assertIsNone(comparison['paired_delta'])
        self.assertEqual(0, comparison['paired_seed_count'])
        self.assertTrue(any(row['metric'] == 'special_built' and row['reference_release'] is None for row in best))
        self.assertTrue(any(row['role'] == 'history' and row['metric'] == 'special_built'
                            and row['unavailable'] > 0 and row['estimate'] is None for row in metrics))

    def test_censored_counts_retained_and_core_censoring_cannot_pass(self):
        rows = fixtures()
        for row in rows:
            if row['metric'] == 'attack_count' and row['seed'] == '0':
                row.update(status='censored', value=20, reason='capture_limit')
        report, metrics, _, comparisons = analyze(candidate=rows)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        self.assertEqual(2, report['coverage']['candidate']['censored'])
        self.assertIn('UNMEASURED_CORE_OR_OBJECTIVE', primary(comparisons, 'attack_count')['coverage_errors'])
        self.assertTrue(any(row['censored'] and row['independent_seeds'] == 7 for row in metrics))


class InferenceTests(unittest.TestCase):
    def test_complete_eight_seed_independent_objectives_establish_noninferiority(self):
        report, _, _, comparisons = analyze()
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        self.assertFalse(report['failures'])
        self.assertTrue(all(row['status'] == 'NONINFERIOR' for row in comparisons if row['gating'] and row['applicable']))

    def test_meaningful_supported_regression_blocks_even_if_other_objectives_improve(self):
        rows = fixtures(values={'kill_value': 80, 'attack_count': 1000})
        report, _, _, comparisons = analyze(candidate=rows)
        self.assertEqual('BLOCKED_REGRESSION', report['verdict'])
        comparison = primary(comparisons, 'kill_value')
        self.assertEqual('REGRESSION', comparison['status'])
        self.assertEqual(-20, comparison['paired_delta'])
        self.assertEqual(10, comparison['margin'])
        self.assertLess(comparison['slack_ci_high'], 0)

    def test_lower_is_better_objectives_have_correct_margin_direction(self):
        report, _, _, comparisons = analyze(candidate=fixtures(values={'economy_stall_fraction': 0.2}))
        self.assertEqual('BLOCKED_REGRESSION', report['verdict'])
        comparison = primary(comparisons, 'economy_stall_fraction')
        self.assertEqual('lower', comparison['direction'])
        self.assertAlmostEqual(0.05, comparison['margin'])
        self.assertLess(comparison['slack_ci_high'], 0)

    def test_point_estimate_inside_margin_does_not_establish_noninferiority(self):
        noisy = lambda seed, house, orientation: 25 if seed % 2 else 175
        report, _, _, comparisons = analyze(candidate=fixtures(values={'kill_value': noisy}))
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        comparison = primary(comparisons, 'kill_value')
        self.assertEqual(0, comparison['paired_delta'])
        self.assertLess(comparison['slack_ci_low'], 0)
        self.assertGreater(comparison['slack_ci_high'], 0)
        self.assertEqual('INCONCLUSIVE_INTERVAL', comparison['status'])

    def test_four_seeds_many_houses_games_and_orientations_are_still_four_seeds(self):
        options = dict(seeds=range(4), houses=tuple(str(index) for index in range(6)), swap=True)
        report, metrics, _, comparisons = analyze(history=historical(**options),
                                                  control=fixtures(arm='off', **options),
                                                  candidate=fixtures(**options), min_seeds=4)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        self.assertEqual(8, report['policy']['effective_min_seeds'])
        self.assertEqual(4, primary(comparisons, 'kill_value')['paired_seed_count'])
        self.assertEqual('INSUFFICIENT_SEEDS', primary(comparisons, 'kill_value')['status'])
        self.assertTrue(all(row['independent_seeds'] == 4 for row in metrics if row['metric'] == 'kill_value'))

    def test_seed_blocks_average_games_before_inference(self):
        rows, _ = gate.validate_rows(fixtures(seeds=(1, 2), swap=True,
                                            values={'kill_value': lambda seed, house, orientation:
                                                    seed * 100 + int(house) * 20 + (40 if orientation else 0)}), 'candidate')
        cohorts = gate._prepare_cohorts(rows)
        observations = next(iter(cohorts.values()))
        values = gate.seed_blocks([row for row in observations if row['metric'] == 'kill_value'])
        self.assertEqual({'1': 130, '2': 230}, values)

    def test_per_metric_best_is_actual_measured_release_not_assumed_807(self):
        references = historical('1.0.792', values={'win_score': 0.8, 'kill_value': 100})
        references += historical('1.0.807', values={'win_score': 0.6, 'kill_value': 200})
        references += historical('1.0.843', values={'win_score': 0.7, 'kill_value': 150})
        report, _, best, comparisons = analyze(history=references,
                                              control=fixtures(arm='off', values={'win_score': 0.8, 'kill_value': 200}),
                                              candidate=fixtures(values={'win_score': 0.8, 'kill_value': 200}))
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        selected = {row['metric']: row for row in best if row['scope'] == 'context_house' and row['house'] == '0'}
        self.assertEqual('1.0.792', selected['win_score']['reference_release'])
        self.assertEqual('1.0.807', selected['kill_value']['reference_release'])
        self.assertEqual(3, selected['kill_value']['measured_references'])
        self.assertIsNotNone(selected['kill_value']['reference_ci_low'])
        self.assertEqual(0, selected['kill_value']['paired_delta'])
        self.assertEqual({'1.0.792', '1.0.807', '1.0.843'},
                         {row['reference_release'] for row in comparisons if row['kind'] == 'historical'})

    def test_simultaneous_intervals_protect_multiple_reference_selection(self):
        values_a = {str(index): value for index, value in enumerate((-1, -1, -1, -1, 1, 1, 1, 1))}
        values_b = {str(index): value for index, value in enumerate((-1, 1, -1, 1, -1, 1, -1, 1))}
        only, together_a, together_b = {}, {}, {}
        gate.simultaneous_intervals([gate.IntervalRequest(values_a, only, 'low', 'high')], samples=999)
        policy = gate.simultaneous_intervals([gate.IntervalRequest(values_a, together_a, 'low', 'high'),
                                             gate.IntervalRequest(values_b, together_b, 'low', 'high')], samples=999)
        self.assertLessEqual(together_a['low'], only['low'])
        self.assertGreaterEqual(together_a['high'], only['high'])
        self.assertEqual(1, policy['seed_set_strata'])
        repeated_a, repeated_b = {}, {}
        gate.simultaneous_intervals([gate.IntervalRequest(values_b, repeated_b, 'low', 'high'),
                                     gate.IntervalRequest(values_a, repeated_a, 'low', 'high')], samples=999)
        self.assertEqual(together_a, repeated_a)
        self.assertEqual(together_b, repeated_b)

    def test_inadequate_bootstrap_tail_resolution_is_not_point_estimate_pass(self):
        record = {}
        policy = gate.simultaneous_intervals([gate.IntervalRequest({'0': 0, '1': 1}, record, 'low', 'high')], samples=9)
        self.assertIsNone(record['low'])
        self.assertEqual(1, policy['unresolved_strata'])
        self.assertEqual('insufficient_bootstrap_tail_resolution', record['ci_status'])

    def test_adjacent_releases_show_likely_regression_interval(self):
        history = historical('1.0.792', values={'kill_value': 200})
        history += historical('1.0.807', values={'kill_value': 100})
        report, _, _, _ = analyze(history=history)
        self.assertTrue(any(row['reference_release'] == '1.0.792' and row['candidate_release'] == '1.0.807'
                            and row['metric'] == 'kill_value' for row in report['likely_regression_intervals']))

    def test_missing_historical_research_is_explicit_never_807_off_relabel(self):
        report, _, best, comparisons = analyze(history=historical('1.0.807'),
                                              control=fixtures(arm='off', research='on'),
                                              candidate=fixtures(research='on'))
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        on_history = [row for row in comparisons if row['kind'] == 'historical' and row['research'] == 'on']
        self.assertTrue(on_history)
        self.assertTrue(all(row['status'] == 'NO_HISTORICAL_RESEARCH_COVERAGE' for row in on_history))
        self.assertTrue(all(row['paired_seed_count'] == 0 for row in on_history))
        self.assertTrue(any(row.get('code') == 'NO_HISTORICAL_COVERAGE' for row in report['failures']))
        self.assertTrue(all(row['reference_release'] is None for row in best if row['research'] == 'on'))

    def test_supported_on_history_stays_separate_from_off(self):
        history = historical(research='off') + historical(research='on')
        control = fixtures(arm='off', research='off') + fixtures(arm='off', research='on')
        candidate = fixtures(research='off') + fixtures(research='on')
        report, _, _, comparisons = analyze(history=history, control=control, candidate=candidate)
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        self.assertEqual({'off', 'on'}, {row['research'] for row in comparisons if row['gating']})

    def test_disjoint_historical_bridge_pins_keep_one_native_release(self):
        rows = historical()
        for row in rows:
            if int(row['seed']) >= 4:
                row['source_commit'] = digest('second-test-only-observer-bridge')
        report, metrics, _, comparisons = analyze(history=rows)
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        self.assertEqual(2, len(report['historical_releases'][0]['source_commits']))
        self.assertEqual(1, len(report['historical_releases'][0]['baseline_release_sources']))
        self.assertEqual(8, primary(comparisons, 'kill_value')['paired_seed_count'])
        self.assertTrue(any(row['role'] == 'history' and len(row['source_commits']) == 2
                            for row in metrics))

    def test_seed_equal_to_requested_map_size_does_not_remove_size(self):
        identifier = 'matrix/cy-a0-c0/128/none/1v1/hard/40m/128/swap'
        self.assertEqual('matrix/cy-a0-c0/128/none/1v1/hard/40m',
                         gate.scenario_id(identifier, '128'))

    def test_distinct_overlapping_seed_sets_receive_bonferroni_allocation(self):
        values = {str(index): float(index % 2) for index in range(8)}
        overlapping = {str(index): float(index % 2) for index in range(4, 12)}
        first, second = {}, {}
        policy = gate.simultaneous_intervals([gate.IntervalRequest(values, first, 'low', 'high'),
                                             gate.IntervalRequest(overlapping, second, 'low', 'high')], samples=999)
        self.assertEqual(2, policy['seed_set_strata'])
        self.assertAlmostEqual(0.025, policy['alpha_per_stratum'])
        self.assertIsNotNone(first['low'])
        self.assertIsNotNone(second['low'])

    def test_pooled_dimensions_say_all_instead_of_first_context_spice(self):
        report, metrics, _, comparisons = analyze(history=historical() + historical(spice='medium'),
                                                  control=fixtures(arm='off') + fixtures(arm='off', spice='medium'),
                                                  candidate=fixtures() + fixtures(spice='medium'))
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        self.assertTrue(all(row['spice'] == 'all' for row in metrics if row['scope'] == 'pooled_sensitivity'))
        self.assertTrue(all(row['spice'] == 'all' for row in comparisons if row['scope'] == 'pooled_sensitivity'))

    def test_city_zoning_and_population_are_independent_hard_objectives(self):
        for metric in ('zone_residential', 'zone_industrial', 'zone_commercial', 'population'):
            base = 1000 if metric == 'population' else 10
            report, _, _, comparisons = analyze(candidate=fixtures(values={metric: base / 2}))
            with self.subTest(metric=metric):
                self.assertEqual('BLOCKED_REGRESSION', report['verdict'])
                comparison = primary(comparisons, metric)
                self.assertTrue(comparison['gating'])
                self.assertEqual('REGRESSION', comparison['status'])
                self.assertAlmostEqual(base * .10, comparison['margin'])

    def test_supported_research_level_loss_is_a_hard_regression(self):
        report, _, _, comparisons = analyze(history=historical(research='on', values={'research_level': 2}),
                                            control=fixtures(arm='off', research='on', values={'research_level': 2}),
                                            candidate=fixtures(research='on', values={'research_level': 1}))
        self.assertEqual('BLOCKED_REGRESSION', report['verdict'])
        comparison = primary(comparisons, 'research_level')
        self.assertTrue(comparison['gating'])
        self.assertEqual(.25, comparison['margin'])
        self.assertEqual('REGRESSION', comparison['status'])

    def test_native_per_type_built_and_shares_cannot_hide_behind_total_special_built(self):
        for metric in ('devastator_built', 'deviator_built', 'sonic_tank_built',
                       'devastator_live_value_share', 'deviator_live_value_share', 'sonic_tank_live_value_share'):
            value = .10 if metric.endswith('_share') else 50
            report, _, _, comparisons = analyze(candidate=fixtures(values={metric: value}))
            with self.subTest(metric=metric):
                self.assertEqual('BLOCKED_REGRESSION', report['verdict'])
                comparison = primary(comparisons, metric)
                self.assertEqual('REGRESSION', comparison['status'])
                self.assertAlmostEqual(.05 if metric.endswith('_share') else 10, comparison['margin'])
                self.assertEqual('NONINFERIOR', primary(comparisons, 'special_built')['status'])

    def test_explicit_matching_city_disabled_and_research_off_are_exempt_not_zero(self):
        def city_disabled(rows):
            for row in rows:
                if gate.METRIC_DEFINITIONS[row['metric']]['applicability'] == 'city':
                    row.update(value='', status='not_applicable', reason='city_simulation_disabled')
            return rows
        report, _, best, comparisons = analyze(history=city_disabled(historical()),
                                               control=city_disabled(fixtures(arm='off')),
                                               candidate=city_disabled(fixtures()))
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        for metric in ('zone_residential', 'population', 'research_level'):
            comparison = primary(comparisons, metric)
            self.assertFalse(comparison['applicable'])
            self.assertEqual('NOT_APPLICABLE', comparison['status'])
            self.assertIsNone(comparison['paired_delta'])
        self.assertTrue(any(row['metric'] == 'population' and row['status'] == 'NOT_APPLICABLE' for row in best))

    def test_candidate_cannot_exempt_city_objective_when_reference_measured_it(self):
        rows = fixtures()
        for row in rows:
            if row['metric'] == 'population':
                row.update(value='', status='not_applicable', reason='city_simulation_disabled')
        report, _, _, comparisons = analyze(candidate=rows)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        self.assertIn('MISMATCH_APPLICABILITY', primary(comparisons, 'population')['coverage_errors'])

    def test_undefined_zero_army_share_remains_inconclusive_not_conditional_survivor_pass(self):
        rows = fixtures()
        for row in rows:
            if row['metric'] == 'sonic_tank_live_value_share' and row['seed'] == '0':
                row.update(value='', status='not_applicable', reason='no_live_army_denominator')
        report, metrics, _, comparisons = analyze(candidate=rows)
        self.assertEqual('INCONCLUSIVE', report['verdict'])
        comparison = primary(comparisons, 'sonic_tank_live_value_share')
        self.assertTrue(comparison['applicable'])
        self.assertIn('UNMEASURED_CORE_OR_OBJECTIVE', comparison['coverage_errors'])
        self.assertEqual(0, comparison['paired_seed_count'])
        self.assertTrue(any(row['role'] == 'candidate' and row['metric'] == 'sonic_tank_live_value_share'
                            and row['not_applicable'] and row['independent_seeds'] == 7 for row in metrics))

    def test_city_checkpoint_is_diagnostic_not_an_extra_independent_core_sample(self):
        rows = fixtures()
        checkpoint = next(dict(row, minute=5, value=0) for row in rows if row['metric'] == 'zone_residential')
        rows.append(checkpoint)
        report, metrics, _, comparisons = analyze(candidate=rows)
        self.assertEqual('DEFAULT_ON_ALLOWED', report['verdict'])
        sampled = [row for row in metrics if row['role'] == 'candidate' and row['metric'] == 'zone_residential'
                   and row['minute'] == 5]
        self.assertTrue(sampled)
        self.assertTrue(all(not row['core'] for row in sampled))
        self.assertTrue(all(row['minute'] == -1 for row in comparisons))


class CsvAndCliTests(unittest.TestCase):
    def write_plain(self, path, rows):
        with path.open('w', encoding='utf-8', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=gate.COLUMNS)
            writer.writeheader()
            writer.writerows(rows)

    def test_plain_and_gzip_inputs_and_reproducible_permanent_reports(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            history_path, control_path, candidate_path = [directory / name for name in
                                                          ('history.csv.gz', 'off.csv', 'on.csv.gz')]
            gate.write_csv_gzip(history_path, gate.COLUMNS, historical())
            self.write_plain(control_path, fixtures(arm='off'))
            gate.write_csv_gzip(candidate_path, gate.COLUMNS, fixtures())
            outputs = [directory / 'first', directory / 'second']
            for output in outputs:
                with contextlib.redirect_stdout(io.StringIO()):
                    result = gate.main(['--history', str(history_path), '--control', str(control_path),
                                        '--candidate', str(candidate_path), '--output', str(output),
                                        '--bootstrap-samples', '499'])
                self.assertEqual(0, result)
            filenames = ('report.json', 'report.txt', 'release-metrics.csv.gz',
                         'best-historical.csv.gz', 'paired-comparisons.csv.gz')
            for name in filenames:
                self.assertEqual((outputs[0] / name).read_bytes(), (outputs[1] / name).read_bytes())
            report = json.loads((outputs[0] / 'report.json').read_text())
            self.assertEqual(hashlib.sha256(history_path.read_bytes()).hexdigest(), report['inputs'][0]['sha256'])
            self.assertEqual([True, False, True], [receipt['gzip'] for receipt in report['inputs']])
            with gzip.open(outputs[0] / 'paired-comparisons.csv.gz', 'rt', newline='') as handle:
                rows = list(csv.DictReader(handle))
            self.assertTrue(rows)
            self.assertIn('slack_ci_low', rows[0])

    def test_invalid_input_writes_machine_readable_failure_and_all_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            malformed = directory / 'malformed.csv'
            malformed.write_text('metric,value\nkill_value,inf\n', encoding='utf-8')
            output = directory / 'report'
            with contextlib.redirect_stdout(io.StringIO()):
                result = gate.main(['--history', str(malformed), '--control', str(malformed),
                                    '--candidate', str(malformed), '--output', str(output)])
            self.assertEqual(4, result)
            report = json.loads((output / 'report.json').read_text())
            self.assertEqual('INCONCLUSIVE', report['verdict'])
            self.assertFalse(report['input_valid'])
            self.assertEqual('CSV_SCHEMA_MISMATCH', report['errors'][0]['code'])
            self.assertTrue((output / 'best-historical.csv.gz').is_file())

    def test_cli_min_four_never_calls_four_seeds_safe(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            paths = [directory / name for name in ('history.csv', 'off.csv', 'on.csv')]
            for path, rows in zip(paths, (historical(seeds=range(4)), fixtures(arm='off', seeds=range(4)),
                                         fixtures(seeds=range(4)))):
                self.write_plain(path, rows)
            with contextlib.redirect_stdout(io.StringIO()):
                result = gate.main(['--history', str(paths[0]), '--control', str(paths[1]),
                                    '--candidate', str(paths[2]), '--min-seeds', '4', '--output', str(directory / 'report')])
            self.assertEqual(3, result)
            report = json.loads((directory / 'report/report.json').read_text())
            self.assertEqual('INCONCLUSIVE', report['verdict'])
            self.assertEqual(8, report['policy']['effective_min_seeds'])


if __name__ == '__main__':
    unittest.main()
