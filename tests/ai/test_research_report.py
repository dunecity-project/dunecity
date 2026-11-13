#!/usr/bin/env python3
"""Bounded CSV/JSON fixtures: truthful missing data and independent research decisions."""
import csv
import gzip
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import research_report as subject
from test_dunecity_audit import repair_quality


class ResearchReports(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.off, self.on = self.root / 'off', self.root / 'on'
        self.off.mkdir()
        self.on.mkdir()
        for name, seed in (('pilot', 1), ('confirmation', 999)):
            (self.root / (name + '.json')).write_text(json.dumps(dict(schema_version=1, sample_seed=10,
                cases=[dict(seed=seed, format='1v1', size=96, spice_level='medium', initial_tech=9)])))
        self.manifest = dict(schema=subject.SCHEMA, id='repair-demand', mechanism='Serviceable repair demand',
            falsifier='No serviceable waiting vehicles', scope='Native Brutal', off_contract='OFF keeps native actions',
            stopping_rule='No adaptive peeking; fixed preregistered decks', snapshot=dict(revision='a' * 40, cwd=str(self.root)),
            baseline=dict(release='fixture', source_commit='b' * 40, config_identity='native', population='FFA4', independent=True),
            observed_failure=['retained baseline loss'], successful_counterexamples=['matched baseline win'], negative_learning=[],
            configs=dict(off='control', on='repair-demand-on'),
            opportunity=dict(metric='triggers', direction='higher', minimum_effect=1),
            mechanism_metric=dict(metric='repaired', direction='higher', minimum_effect=.1),
            primary=dict(metric='won', direction='higher', minimum_effect=.1), guards=[],
            design=dict(cluster_fields=['seed'], alpha=.05, power=.8, finalists=1, stopping='fixed-budget'),
            budget=dict(max_games=8, max_wall_s=800), promotion_controls=['native controls', 'historical', 'CPU', 'combined defaults'],
            stages={name: dict(case_manifest=name + '.json', args=['--mode', 'sample', '--mod', 'dunecity', '--minutes', '30'])
                    for name in ('pilot', 'confirmation')})
        self.manifest['stages']['confirmation'].update(locked_at='2026-11-12T00:00:00Z', untouched=True)
        self.path = self.root / 'experiment.json'
        self.save_manifest()
        calibration = self.root / 'independent-calibration.txt'
        calibration.write_text('Independent fixture calibration, not game or strength evidence.\n')
        self.manifest['effective_config_hashes'] = dict(native='1' * 16, off='1' * 16, on='2' * 16)
        self.manifest['config_calibration_references'] = [str(calibration)]
        self.evidence = {}
        self.save_manifest()

    def save_manifest(self):
        self.path.write_text(json.dumps(self.manifest))

    def game(self, game_id='g1', seed=1, arm='off', winner=2, **changes):
        row = dict(id=game_id, seed=str(seed), candidate_house='0', candidate_seat='0',
                   candidate_style=self.manifest['configs'][arm], source_commit='a' * 40,
                   source_fingerprint='fingerprint', map_hash='map', content_hash='content', object_data_hash='objects',
                   options_hash='options', config_hash='1' * 16, style='default', setup_style='default',
                   observer_mode='on', error='', size='96', sample_case='1',
                   mod='dunecity', format='1v1', difficulty='brutal', base='medium', army='2', city='0',
                   initial_tech='9', spice_level='medium', winner_team=str(winner), scored_winner_team=str(winner),
                   outcome='decided', minutes='30', wall_s='10')
        row.update(changes)
        row['candidate_seat'] = {'0': 'original', '1': 'swapped'}.get(row['candidate_seat'], row['candidate_seat'])
        if 'candidate_house' not in changes:
            row['candidate_house'] = '1' if row['candidate_seat'] == 'swapped' else '0'
        selected = row['candidate_house']
        configs = [dict(house=house, config_hash='2' * 16 if arm == 'on' and house == selected else '1' * 16)
                   for house in ('0', '1')]
        row.setdefault('ai_config_hashes', ','.join(item['house'] + ':' + item['config_hash'] for item in configs))
        row.setdefault('ai_config_receipts', json.dumps(configs))
        row.setdefault('genome_phase_receipts', json.dumps([] if arm == 'off' else [dict(
            house=selected, config_hash='2' * 16, genome='candidate-' + self.manifest['configs'][arm],
            controller='0', difficulty='3', phase='opening', cycle='0')]))
        return row

    def house(self, game_id='g1', won=0, **changes):
        row = dict(id=game_id, house='0', team='1', alive='1', eliminated_s='-1', won=str(won),
                   triggers='0', repaired='0', seat_style='control', loss_value='1000', economy_stall='1', eligible_economy_stall='1',
                   first_economy_stall='12', repair_yards='', repair_yards_built='', occupied_refinery_bays='2')
        row.update(changes)
        return row

    @staticmethod
    def table(directory, name, rows):
        fields = sorted({field for row in rows for field in row})
        with (directory / name).open('w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)

    def paired(self, seats=2, **house_changes):
        for arm, directory in (('off', self.off), ('on', self.on)):
            games = [self.game('g1-seat' + str(seat), arm=arm, candidate_seat=str(seat)) for seat in range(seats)]
            houses = [self.house(game['id'], house=house,
                                 seat_style=self.manifest['configs'][arm] if house == game['candidate_house'] else 'control',
                                 **house_changes) for game in games for house in ('0', '1')]
            self.table(directory, 'games.csv', games)
            self.table(directory, 'houses.csv', houses)

    def repair_data(self, final, snapshots=(), capture_status='complete'):
        capture = dict(status=capture_status, reasons='' if capture_status == 'complete' else 'capture_limit',
                       start_cycle=0, end_cycle=100, decisions=[
            dict(event='repair_capacity', session='ledger', seq=index + 2, cycle=quality['end_cycle'],
                 house=0, player=1, data=dict(treatment=1, actual=1, quality=quality))
            for index, quality in enumerate(snapshots)], repair_finals=[] if final is None else [
            dict(event='game_summary', session='ledger', seq=len(snapshots) + 2, cycle=100,
                 house=0, data=dict(repair_quality=final))])
        return dict(games=[self.game()], houses=[self.house()], samples=[],
                    repairs=[dict(row, id='g1', house='0') for row in subject.repair_observations(capture)])

    def diagnostic_manifest(self, cases=1, index=None):
        deck = dict(schema_version=1, sample_seed=10, cases=[
            dict(seed=920903895 + number, format='ffa6', size=128, spice_level='medium', initial_tech=9,
                 difficulty='brutal') for number in range(cases)])
        (self.root / 'pilot.json').write_text(json.dumps(deck))
        self.manifest.update(purpose='diagnostic', diagnostic_house=3,
            design=dict(cluster_fields=['seed'], stopping='fixed-budget'),
            budget=dict(max_games=2 if index is not None else cases * 2, max_wall_s=800),
            stages=dict(pilot=dict(case_manifest='pilot.json', args=[
                '--mode', 'sample', '--mod', 'dunecity', '--minutes', '30'])))
        self.manifest['baseline'].update(independent=False, population='targeted FFA6 fixture, not independent evidence')
        self.manifest['opportunity'].update(metric='repair_serviceable_waiting', aggregation='decision_max')
        self.manifest['mechanism_metric'].update(metric='repair_quality_useful_delivered_total')
        self.manifest['guards'] = [dict(metric='repair_quality_service_paid_raw_total', direction='higher', margin=0)]
        self.manifest.pop('promotion_controls')
        if index is not None:
            self.manifest['diagnostic_case_index'] = index
        self.save_manifest()
        return deck

    def diagnostic_tables(self, indices=(1,)):
        deck = json.loads((self.root / 'pilot.json').read_text())
        arms = {}
        for arm, directory in (('off', self.off), ('on', self.on)):
            data = dict(games=[], houses=[], samples=[], repairs=[])
            for index in indices:
                case = deck['cases'][index - 1]
                roster = ('0', '1', '2', '4', '3', '5')
                shift = (index - 1) % 6
                roster = roster[shift:] + roster[:shift]
                teams = {house: str(seat + 1) for seat, house in enumerate(roster)}
                winner = teams['3'] if arm == 'on' else teams['0']
                game = self.game('diagnostic-' + str(index), arm=arm, seed=case['seed'], winner=winner,
                    format='ffa6', size='128', candidate_house='3', candidate_seat='target-house-3',
                    sample_case=str(index), map_hash='map-' + str(index))
                configs = [dict(house=house, config_hash='2' * 16 if arm == 'on' and house == '3' else '1' * 16)
                           for house in roster]
                game.update(ai_config_hashes=','.join(row['house'] + ':' + row['config_hash'] for row in configs),
                    ai_config_receipts=json.dumps(configs), genome_phase_receipts=json.dumps([] if arm == 'off' else [
                        dict(house='3', config_hash='2' * 16, genome='candidate-' + self.manifest['configs']['on'],
                             controller='0', difficulty='3', phase='opening', cycle='0')]))
                data['games'].append(game)
                data['houses'].extend(self.house(game['id'], house=house, team=teams[house],
                    won=int(teams[house] == winner), seat_style=self.manifest['configs'][arm] if house == '3' else 'control')
                    for house in roster)
                final = repair_quality() if arm == 'on' else repair_quality(
                    health_restored_raw_total=0, service_paid_raw_total=0, completed_total=0,
                    return_attempted_total=0, released_ground_total=0, delivered_total=0, useful_delivered_total=0,
                    free_cycles_total=100, damaged_cycles_total=0, full_return_cycles_total=0, unfunded_cycles_total=0)
                snapshot = repair_quality(end_cycle=25, capture_complete=0, capital_refund_requested_raw_total=0,
                    capital_refund_actual_raw_total=0, health_restored_raw_total=0, service_paid_raw_total=0,
                    completed_total=0, return_attempted_total=0, released_ground_total=0, delivered_total=0, useful_delivered_total=0,
                    live_cycles_total=25, free_cycles_total=5 if arm == 'on' else 25,
                    damaged_cycles_total=10 if arm == 'on' else 0, full_return_cycles_total=10 if arm == 'on' else 0,
                    unfunded_cycles_total=0)
                capture = dict(status='complete', start_cycle=0, end_cycle=100, decisions=[
                    dict(event='repair_capacity', session='ledger-' + game['id'], seq=seat + 2, cycle=25,
                         house=int(house), player=seat + 1, data=dict(actual=1, serviceable_waiting=1,
                         treatment=int(arm == 'on' and house == '3'), quality=snapshot)) for seat, house in enumerate(roster)],
                    repair_finals=[dict(event='game_summary', session='ledger-' + game['id'], seq=8, cycle=100,
                                        house=3, data=dict(repair_quality=final))])
                data['repairs'].extend(dict(row, id=game['id'], house=str(row['house']))
                                       for row in subject.repair_observations(capture))
            for name in ('games', 'houses'):
                self.table(directory, name + '.csv', data[name])
            self.table(directory, 'repair-capacity.csv', data['repairs'])
            arms[arm] = data
        return arms

    def test_missing_repair_counts_are_not_zero_or_refinery_usage(self):
        self.paired()
        data = subject.dataset([self.on])
        report = subject.quality_report(data, [self.on])
        self.assertEqual(report['final_fields']['repair_yards']['observed'], 0)
        self.assertIsNone(report['final_fields']['repair_yards']['mean'])
        self.assertEqual(report['final_fields']['occupied_refinery_bays']['mean'], 2)
        self.assertIn('repair_quality_health_restored_raw_total', report['missing_repair_usage'])
        self.assertIsNone(report['cost']['game_cpu_s'])
        self.assertEqual(report['cost']['game_wall_s'], 20)
        self.assertEqual(report['outcomes']['engine_decided_games'], 2)
        self.assertEqual(report['outcomes']['candidate_house_results'], {'loss': 2})
        self.assertEqual(report['repair_quality']['status'], {'unknown': 2})
        self.assertFalse(report['repair_quality']['measurement_ready'])
        self.assertFalse(report['repair_quality']['useful_service_proven'])

    def test_population_score_and_elimination_sentinel_remain_distinct(self):
        game = self.game(winner=0, scored_winner_team='1', win_condition='population')
        house = self.house(won=1)
        result, semantics = subject.outcome(game, house)
        self.assertEqual(result, 'win')
        self.assertEqual(semantics['raw_winner_team'], 0)
        self.assertEqual(semantics['scored_winner_team'], 1)
        self.assertEqual(subject.survival(game, house), dict(minutes=30, status='right-censored'))
        house['alive'] = '0'
        self.assertIsNone(subject.survival(game, house)['minutes'])
        house['eliminated_s'] = '120'
        self.assertEqual(subject.survival(game, house)['minutes'], 2)

    def test_loss_ranking_uses_all_denominators_and_counterexamples(self):
        games = [self.game('g' + str(seed), seed=seed, winner=1 if seed == 4 else 2) for seed in range(1, 5)]
        houses = [self.house(game['id'], won=int(game['winner_team'] == '1'),
                             economy_stall='' if game['seed'] == '3' else '1') for game in games]
        self.table(self.off, 'games.csv', games)
        self.table(self.off, 'houses.csv', houses)
        self.table(self.off, 'samples.csv', [dict(id='g1', house='0', cycle='75000', minute='20')])
        report = subject.loss_audit([self.off], 'fixture-native', 'a' * 40)
        cause = report['ranked_causes'][0]
        self.assertEqual(cause['eligible_losses'], 3)
        self.assertEqual(cause['measured_eligible_losses'], 2)
        self.assertEqual(cause['exposed_losses'], 2)
        self.assertEqual(cause['independent_loss_clusters'], 2)
        self.assertEqual(cause['first_failure_phase_counts'], {'midgame-10-30m': 2})
        self.assertEqual(cause['successful_counterexamples'][0]['id'], 'g4')
        self.assertEqual(report['phase_sample_coverage']['unsampled_loss_house_games'], 2)
        self.assertEqual(report['outcome_semantics']['engine_decided_games'], 4)
        self.assertEqual(report['outcome_semantics']['candidate_house_results'], {'loss': 3, 'win': 1})

    def test_one_loss_without_counterexample_does_not_force_an_idea(self):
        self.paired(seats=1)
        report = subject.loss_audit([self.off], 'fixture-native')
        self.assertEqual(report['ranked_causes'], [])
        self.assertTrue(report['missing_diagnostic'])
        self.assertIn('counterexamples', report['unselected'][0]['reason'])

    def test_swapped_seats_are_not_independent_trials(self):
        result = subject.clustered_effect([(('seed1',), 0), (('seed1',), 0),
                                           (('seed2',), 0), (('seed2',), 0)], self.manifest['design'], .1)
        self.assertEqual(result['clusters'], 2)
        self.assertEqual(result['paired_observations'], 4)
        self.assertEqual(result['interval'], [0, 0])
        self.assertFalse(result['power_sufficient'])
        self.assertIsNone(result['planned_clusters_normal_approximation'])
        self.assertEqual(subject.decide('screen', {}), 'inconclusive')

    def test_holdout_cannot_reuse_tuning_seed_under_a_new_cell_name(self):
        path = self.root / 'confirmation.json'
        value = json.loads(path.read_text())
        value['cases'][0].update(seed=1, size=128)
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'reuses tuning'):
            subject.validate_manifest(self.manifest, self.root)

    def test_manifest_has_one_source_identity_but_distinct_run_configs(self):
        report = subject.validate_manifest(self.manifest, self.root)
        self.assertEqual(report['source_revision'], 'a' * 40)
        self.assertTrue(report['config_values_are_run_inputs'])
        self.assertNotIn('configs', report['build_identity_inputs'])
        self.manifest['budget']['max_games'] = 7
        with self.assertRaisesRegex(ValueError, 'game budget'):
            subject.validate_manifest(self.manifest, self.root)

    def test_missing_or_partial_pilot_is_not_a_never_triggered_pilot(self):
        self.paired(seats=1)
        result = subject.experiment_report(self.path, 'pilot', self.off, self.on, self.evidence)
        self.assertEqual(result['status'], 'inconclusive')
        self.paired()
        result = subject.experiment_report(self.path, 'pilot', self.off, self.on, self.evidence)
        self.assertEqual(result['status'], 'opportunity-missing')
        self.paired(triggers='')
        self.assertEqual(subject.experiment_report(self.path, 'pilot', self.off, self.on, self.evidence)['status'], 'inconclusive')

    def test_harm_and_implementation_failure_are_not_missing_opportunity(self):
        self.assertEqual(subject.decide('pilot', dict(harm_demonstrated=True, opportunity_complete=True, opportunities=0)), 'harmful')
        self.assertEqual(subject.decide('pilot', dict(implementation_failed=True)), 'implementation-failed')
        self.assertEqual(subject.decide('screen', dict(mechanism_adequately_unsupported=True)), 'mechanism-unsupported')
        self.assertEqual(subject.decide('confirmation', dict(primary_confirmed=True)), 'inconclusive')

    def test_repair_capacity_stocks_are_not_completed_service(self):
        capture = dict(status='limited', decisions=[dict(event='repair_capacity', session='s', seq=1, cycle=100,
            house=0, data=dict(treatment=True, actual=2, committed=3, busy=0, completed_waiting=1,
                               serviceable_waiting=0, transport_blocked=4, usable_local=2, target=2, reason='idle'))])
        row = subject.repair_observations(capture)[0]
        self.assertEqual(row['repair_completed_waiting'], 1)
        self.assertEqual(row['repair_quality_completed_total'], '')
        self.assertEqual(row['capture_status'], 'limited')
        self.assertEqual(row['repair_treatment'], 1)
        self.assertEqual(subject.repair_observations(dict(status='complete', decisions=[])), [])

    def test_sparse_repair_decisions_are_one_metric_per_game_not_independent_passes(self):
        self.paired()
        data = subject.dataset([self.off])
        data['repairs'] = [
            dict(id=house['id'], house=house['house'], seq=str(seq), cycle=str(seq * 100),
                 repair_serviceable_waiting=str(value), capture_status='complete')
            for house in data['houses'] for seq, value in ((1, 0), (2, 3))]
        values = subject.measured_metric(data, 'repair_serviceable_waiting', 'decision_max')
        self.assertEqual(len(values), 2)
        self.assertEqual(set(values.values()), {(3, False)})
        data['repairs'][0]['capture_status'] = 'limited'
        self.assertTrue(subject.measured_metric(data, 'repair_serviceable_waiting', 'decision_mean')[('g1-seat0','0')][1])

    def test_quality_uses_exact_final_q32_totals_not_sparse_stock_means(self):
        final = repair_quality()
        earlier = repair_quality(end_cycle=25, capture_complete=0, right_censored_jobs=1, live_bays=2,
            capital_paid_raw_total=2 ** 53 + 1, capital_refund_requested_raw_total=0, capital_refund_actual_raw_total=0,
            health_restored_raw_total=2 ** 30 + 1, service_paid_raw_total=2 ** 27 + 1,
            completed_total=0, return_attempted_total=0, released_ground_total=0, delivered_total=0, useful_delivered_total=0,
            live_cycles_total=25, free_cycles_total=5, damaged_cycles_total=10, full_return_cycles_total=10)
        data = self.repair_data(final, [earlier])
        report = subject.quality_report(data, [])
        entry = report['repair_quality']['house_games'][0]
        self.assertEqual(entry['status'], 'measured')  # Running capture_complete=0 is expected.
        self.assertTrue(report['repair_quality']['ready_for_quality_review'])
        for key in subject.REPAIR_QUALITY_KEYS:
            field = 'repair_quality_' + key
            self.assertEqual(entry['values'][field], final[key])
            self.assertEqual(report['final_fields'][field]['total'], final[key])
            self.assertEqual(report['sample_fields'][field]['total'], final[key])
            self.assertEqual(subject.measured_metric(data, field)[('g1', '0')], (final[key], False))
        raw = report['final_fields']['repair_quality_capital_paid_raw_total']
        self.assertEqual(raw['mean_exact'], dict(numerator=2 ** 53 + 3, denominator=1))
        self.assertIsNone(raw['mean'])
        self.assertEqual(raw['unit'], 'Q32 credits raw (1 credit = 4294967296)')
        self.assertEqual(report['final_fields']['repair_quality_health_restored_raw_total']['unit'],
                         'Q32 HP raw (1 HP = 4294967296)')
        self.assertEqual(report['final_fields']['repair_quality_damaged_cycles_total']['mean'], 30)
        self.assertEqual(subject.measured_metric(data, 'repair_actual', 'decision_mean')[('g1', '0')], (1, False))
        self.assertEqual(json.loads(json.dumps(report))['repair_quality']['house_games'][0]['values']
                         ['repair_quality_capital_paid_raw_total'], 2 ** 53 + 3)

    def test_cumulative_quality_never_accepts_decision_mean_even_in_guards(self):
        metric = 'repair_quality_service_paid_raw_total'
        data = self.repair_data(repair_quality())
        for aggregation in ('decision_mean', 'decision_last', 'decision_max'):
            with self.subTest(aggregation=aggregation):
                with self.assertRaisesRegex(ValueError, 'final cumulative totals'):
                    subject.measured_metric(data, metric, aggregation)
                self.manifest['mechanism_metric'].update(metric=metric, aggregation=aggregation)
                with self.assertRaisesRegex(ValueError, 'final cumulative totals'):
                    subject.validate_manifest(self.manifest, self.root)
        self.manifest['mechanism_metric'].update(aggregation='final')
        self.manifest['guards'] = [dict(metric=metric, direction='higher', margin=0, aggregation='decision_mean')]
        with self.assertRaisesRegex(ValueError, 'final cumulative totals'):
            subject.validate_manifest(self.manifest, self.root)

    def test_invalid_partial_and_censored_ledgers_cannot_supply_whole_game_quality(self):
        changes = (
            ('unmeasured', 'missing:service_paid_raw_total', dict(service_paid_raw_total=None)),
            ('invalid', 'invalid:health_restored_raw_total', dict(health_restored_raw_total='0.5')),
            ('invalid', 'invalid:service_paid_raw_total', dict(service_paid_raw_total=True)),
            ('invalid', 'invalid:capital_paid_raw_total', dict(capital_paid_raw_total=2 ** 63)),
            ('invalid', 'invalid:capital_paid_raw_total', dict(capital_paid_raw_total=-1)),
            ('invalid', 'invalid:completed_total', dict(completed_total=1.0)),
            ('invalid', 'invalid_bay_cycle_partition', dict(live_cycles_total=101)),
            ('invalid', 'unfunded_exceeds_damaged_cycles', dict(unfunded_cycles_total=31)),
            ('invalid', 'actual_refund_exceeds_request', dict(capital_refund_actual_raw_total=2 ** 32)),
            ('invalid', 'return_outcomes_exceed_attempts', dict(return_aborted_total=1)),
            ('invalid', 'useful_deliveries_exceed_deliveries', dict(useful_delivered_total=2)),
            ('invalid', 'ledger_record_cycle_mismatch', dict(end_cycle=99)),
            ('right-censored', 'ledger_capture_not_closed', dict(capture_complete=0)),
            ('right-censored', 'unfinished_jobs_or_returns', dict(right_censored_jobs=1)),
            ('right-censored', 'unfinished_jobs_or_returns', dict(right_censored_returns=1)),
            ('left-censored', 'left_censored', dict(left_censored=1)))
        for status, reason, mutation in changes:
            with self.subTest(reason=reason, mutation=mutation):
                data = self.repair_data(repair_quality(**mutation))
                report = subject.quality_report(data, [])
                entry = report['repair_quality']['house_games'][0]
                self.assertEqual(entry['status'], status)
                self.assertIn(reason, entry['reasons'])
                self.assertFalse(report['repair_quality']['measurement_ready'])
                self.assertFalse(report['repair_quality']['ready_for_quality_review'])
                self.assertEqual(report['final_fields']['repair_quality_service_paid_raw_total']['observed'], 0)
                self.assertTrue(subject.measured_metric(data, 'repair_quality_completed_total')[('g1', '0')][1])
        for final in (None, {}, [], True):
            with self.subTest(final=final):
                report = subject.quality_report(self.repair_data(final), [])
                self.assertFalse(report['repair_quality']['measurement_ready'])
                self.assertIsNone(report['final_fields']['repair_quality_completed_total']['total'])
        clipped = self.repair_data(repair_quality(), capture_status='capture_limit')
        self.assertEqual(subject.repair_measurements(clipped)[('g1', '0')]['status'], 'right-censored')

    def test_ground_deploy_death_before_release_remains_measured_failed_return(self):
        final = repair_quality(return_attempted_total=1, released_ground_total=0, handoff_carrier_total=0,
                               delivered_total=0, useful_delivered_total=0, return_aborted_total=1)
        data = self.repair_data(final)
        self.table(self.off, 'repair-capacity.csv', data['repairs'])
        data['repairs'] = subject.read_table(self.off, 'repair-capacity.csv', ('id', 'house', 'seq'))
        self.assertEqual(data['repairs'][0]['repair_quality_return_attempted_total'], '1')
        self.assertEqual(json.loads(data['repairs'][0]['data'])['repair_quality']['return_aborted_total'], 1)
        report = subject.quality_report(data, [])
        entry = report['repair_quality']['house_games'][0]
        self.assertEqual(entry['status'], 'measured')
        self.assertEqual(entry['reasons'], [])
        self.assertTrue(report['repair_quality']['measurement_ready'])
        self.assertFalse(report['repair_quality']['useful_service_proven'])
        self.assertFalse(report['repair_quality']['ready_for_quality_review'])
        for key, value in (('return_attempted_total', 1), ('return_aborted_total', 1),
                           ('released_ground_total', 0), ('handoff_carrier_total', 0),
                           ('delivered_total', 0), ('useful_delivered_total', 0)):
            field = 'repair_quality_' + key
            self.assertEqual(entry['values'][field], value)
            self.assertEqual(report['final_fields'][field]['total'], value)
            self.assertEqual(report['final_fields'][field]['unit'], 'count')
            self.assertEqual(subject.measured_metric(data, field)[('g1', '0')], (value, False))

    def test_ground_abort_missing_or_malformed_attempts_cannot_pass_quality_coverage(self):
        for label, value, status in (('absent', None, 'unmeasured'), ('null', None, 'unmeasured'),
                                     ('blank', '', 'unmeasured'), ('fraction', '1.5', 'invalid'),
                                     ('float', 1.0, 'invalid'), ('boolean', True, 'invalid'),
                                     ('negative', -1, 'invalid')):
            with self.subTest(label=label):
                final = repair_quality(return_attempted_total=1, released_ground_total=0, handoff_carrier_total=0,
                                       delivered_total=0, useful_delivered_total=0, return_aborted_total=1)
                if label == 'absent':
                    final.pop('return_attempted_total')
                else:
                    final['return_attempted_total'] = value
                data = self.repair_data(final)
                report = subject.quality_report(data, [])
                entry = report['repair_quality']['house_games'][0]
                self.assertEqual(entry['status'], status)
                self.assertIn(('missing:' if status == 'unmeasured' else 'invalid:') + 'return_attempted_total',
                              entry['reasons'])
                self.assertEqual(entry['values']['repair_quality_return_aborted_total'], 1)
                self.assertFalse(report['repair_quality']['measurement_ready'])
                self.assertFalse(report['repair_quality']['ready_for_quality_review'])
                self.assertIsNone(report['final_fields']['repair_quality_return_aborted_total']['total'])
                self.assertEqual(subject.measured_metric(data, 'repair_quality_return_attempted_total')[('g1', '0')],
                                 (None, True))

    def test_return_outcomes_and_successful_releases_are_independently_bounded_by_attempts(self):
        for changes, reason in (
            (dict(return_attempted_total=0), 'return_outcomes_exceed_attempts'),
            (dict(released_ground_total=2), 'return_releases_exceed_attempts'),
            (dict(released_ground_total=1, handoff_carrier_total=1), 'return_releases_exceed_attempts'),
            (dict(return_attempted_total=2, delivered_total=1), 'deliveries_exceed_successful_releases'),
            (dict(delivered_total=1), 'return_outcomes_exceed_attempts')):
            with self.subTest(changes=changes):
                final = repair_quality(return_attempted_total=1, released_ground_total=0, handoff_carrier_total=0,
                                       delivered_total=0, useful_delivered_total=0, return_aborted_total=1)
                final.update(changes)
                data = self.repair_data(final)
                entry = subject.repair_measurements(data)[('g1', '0')]
                self.assertEqual(entry['status'], 'invalid')
                self.assertIn(reason, entry['reasons'])
                self.assertFalse(subject.quality_report(data, [])['repair_quality']['measurement_ready'])
        for ground, carrier in ((1, 0), (0, 1)):
            with self.subTest(ground=ground, carrier=carrier):
                final = repair_quality(return_attempted_total=1, released_ground_total=ground,
                                       handoff_carrier_total=carrier, return_aborted_total=0)
                entry = subject.repair_measurements(self.repair_data(final))[('g1', '0')]
                self.assertEqual(entry['status'], 'measured')
                self.assertTrue(entry['useful_service_proven'])


    def test_counter_reset_or_changed_baseline_never_uses_max_as_complete_final(self):
        for field in subject.REPAIR_QUALITY_TOTAL_KEYS:
            with self.subTest(field=field):
                final = repair_quality()
                prior = repair_quality(end_cycle=25, capture_complete=0, **{field: final[field] + 1})
                entry = subject.repair_measurements(self.repair_data(final, [prior]))[('g1', '0')]
                self.assertEqual(entry['status'], 'invalid')
                self.assertIn('decreasing:' + field, entry['reasons'])
        prior = repair_quality(end_cycle=25, capture_complete=0)
        data = self.repair_data(repair_quality(baseline_bays=2), [prior])
        self.assertIn('changed_baseline', subject.repair_measurements(data)[('g1', '0')]['reasons'])
        data = self.repair_data(repair_quality(), [prior])
        data['repairs'][-1]['session'] = 'other'
        self.assertIn('invalid_or_changed_session', subject.repair_measurements(data)[('g1', '0')]['reasons'])

    def test_missing_capture_window_or_final_preserves_censoring_and_unknowns(self):
        cases = (('capture_start_cycle', None, 'unmeasured', 'missing_capture_start'),
                 ('capture_end_cycle', None, 'right-censored', 'missing_capture_end'),
                 ('capture_end_cycle', 101, 'right-censored', 'short_final_window'),
                 ('repair_quality_start_cycle', 1, 'left-censored', 'partial_start_window'))
        for field, value, status, reason in cases:
            with self.subTest(field=field):
                data = self.repair_data(repair_quality())
                data['repairs'][0][field] = value
                entry = subject.repair_measurements(data)[('g1', '0')]
                self.assertEqual(entry['status'], status)
                self.assertIn(reason, entry['reasons'])
        data = self.repair_data(None, [repair_quality(end_cycle=25, capture_complete=0)])
        entry = subject.repair_measurements(data)[('g1', '0')]
        self.assertEqual(entry['status'], 'right-censored')
        self.assertIn('missing_final_ledger', entry['reasons'])
        self.assertTrue(subject.measured_metric(data, 'repair_quality_completed_total')[('g1', '0')][1])
        data = self.repair_data(repair_quality())
        data['repairs'].insert(0, dict(id='g1', house='0', seq=1, cycle=10, data='{}', event='repair_capacity'))
        entry = subject.repair_measurements(data)[('g1', '0')]
        self.assertEqual(entry['status'], 'unmeasured')
        self.assertIn('missing_quality_snapshot', entry['reasons'])
        historical = self.repair_data(None)
        historical['houses'][0].update(repair_completed_service='999', repair_health_restored='999')
        entry = subject.repair_measurements(historical)[('g1', '0')]
        self.assertEqual(entry['status'], 'unknown')
        self.assertTrue(all(value is None for value in entry['values'].values()))

    def test_zero_exposure_and_nonuseful_delivery_are_not_useful_service_proof(self):
        no_exposure = repair_quality(damaged_cycles_total=0, free_cycles_total=50, unfunded_cycles_total=0,
            health_restored_raw_total=0, service_paid_raw_total=0, completed_total=0, released_ground_total=0,
            return_attempted_total=0, delivered_total=0, useful_delivered_total=0)
        for final in (no_exposure, repair_quality(useful_delivered_total=0),
                      repair_quality(completed_total=0, useful_delivered_total=0)):
            with self.subTest(final=final):
                report = subject.quality_report(self.repair_data(final), [])
                self.assertTrue(report['repair_quality']['measurement_ready'])
                self.assertFalse(report['repair_quality']['useful_service_proven'])
                self.assertFalse(report['repair_quality']['ready_for_quality_review'])
        report = subject.quality_report(self.repair_data(no_exposure), [])
        self.assertEqual(report['repair_quality']['unexposed_house_games'], 1)

    def test_quality_comparison_uses_exact_valid_final_deltas_not_house_or_snapshot_means(self):
        for directory, extra in ((self.off, 0), (self.on, 1)):
            data = self.repair_data(repair_quality(capital_paid_raw_total=2 ** 53 + 3 + extra))
            for table in ('games', 'houses'):
                self.table(directory, table + '.csv', data[table])
            self.table(directory, 'repair-capacity.csv', data['repairs'])
        report = subject.quality_comparison(self.off, self.on)
        delta = report['final_stock_changes']['repair_quality_capital_paid_raw_total']
        self.assertEqual(delta['observed_pairs'], 1)
        self.assertEqual(delta['total_delta'], 1)
        self.assertEqual(delta['mean_delta_exact'], dict(numerator=1, denominator=1))
        self.assertIsNone(delta['mean_delta'])

    def test_quality_attestation_cannot_promote_missing_or_censored_actual_service(self):
        deck = json.loads((self.root / 'confirmation.json').read_text())
        deck['cases'].append(dict(deck['cases'][0], seed=1000))
        (self.root / 'confirmation.json').write_text(json.dumps(deck))
        self.manifest['budget']['max_games'] = 12
        self.manifest['design']['paired_cluster_sd'] = .01
        self.save_manifest()
        reference = self.root / 'review.txt'
        reference.write_text('Consumer fixture evidence only; not native service or strength evidence.\n')
        evidence = {name: dict(passed=True, references=[str(reference)])
                    for name in ('independent_holdout', 'off_contract', 'mechanism', 'quality_review')}
        evidence.update({'control:' + name: dict(passed=True, references=[str(reference)])
                         for name in self.manifest['promotion_controls']})
        for arm, directory in (('off', self.off), ('on', self.on)):
            games, houses = [], []
            for index, seed in enumerate((999, 1000)):
                for seat in range(2):
                    selected = str((index + seat) % 2)
                    winner = int(selected) + 1 if arm == 'on' else 2 - int(selected)
                    game = self.game('holdout-' + str(seed) + '-' + str(seat), seed=seed, arm=arm, winner=winner,
                        map_hash='map-' + str(seed), sample_case=str(index + 1), candidate_seat=str(seat), candidate_house=selected)
                    games.append(game)
                    houses.extend(self.house(game['id'], house=house, team=str(int(house) + 1),
                        won=int(int(house) + 1 == winner), triggers='1', repaired=str(int(arm == 'on')),
                        seat_style=self.manifest['configs'][arm] if house == selected else 'control') for house in ('0', '1'))
            self.table(directory, 'games.csv', games)
            self.table(directory, 'houses.csv', houses)
        missing = subject.experiment_report(self.path, 'confirmation', self.off, self.on, evidence)
        self.assertEqual(missing['status'], 'confirmed')  # Strength/independence rules do not change.
        self.assertFalse(missing['facts']['quality_reviewed'])
        self.assertFalse(missing['facts']['quality_measurements_complete'])
        self.assertFalse(missing['facts']['promotion_ready'])
        originals = {}
        for directory in (self.off, self.on):
            rows = []
            for game in subject.dataset([directory])['games']:
                for row in self.repair_data(repair_quality())['repairs']:
                    rows.append(dict(row, id=game['id'], house=game['candidate_house'], session='ledger-' + game['id']))
            originals[directory] = rows
            self.table(directory, 'repair-capacity.csv', rows)
        ready = subject.experiment_report(self.path, 'confirmation', self.off, self.on, evidence)
        self.assertEqual(ready['status'], 'confirmed')
        self.assertTrue(ready['facts']['quality_reviewed'])
        self.assertTrue(ready['facts']['promotion_ready'])
        for field, value in (('repair_quality_service_paid_raw_total', ''),
                             ('repair_quality_capture_complete', '0'),
                             ('repair_quality_right_censored_jobs', '1'),
                             ('repair_quality_left_censored', '1'),
                             ('repair_quality_useful_delivered_total', '0'),
                             ('capture_status', 'capture_limit')):
            with self.subTest(field=field):
                rows = [dict(row) for row in originals[self.on]]
                for row in rows:
                    row[field] = value
                self.table(self.on, 'repair-capacity.csv', rows)
                held = subject.experiment_report(self.path, 'confirmation', self.off, self.on, evidence)
                self.assertEqual(held['status'], 'confirmed')
                self.assertFalse(held['facts']['quality_reviewed'])
                self.assertFalse(held['facts']['promotion_ready'])
        aborted = repair_quality(return_attempted_total=1, released_ground_total=0, handoff_carrier_total=0,
                                 delivered_total=0, useful_delivered_total=0, return_aborted_total=1)
        rows = [dict(row) for row in originals[self.on]]
        for row in rows:
            row.update({'repair_quality_' + key: value for key, value in aborted.items()})
            row['data'] = json.dumps(dict(repair_quality=aborted))
        self.table(self.on, 'repair-capacity.csv', rows)
        failed_returns = subject.experiment_report(self.path, 'confirmation', self.off, self.on, evidence)
        self.assertEqual(failed_returns['status'], 'confirmed')
        self.assertTrue(failed_returns['facts']['quality_measurements_complete'])
        self.assertFalse(failed_returns['facts']['useful_repair_service_proven'])
        self.assertFalse(failed_returns['facts']['quality_reviewed'])
        self.assertFalse(failed_returns['facts']['promotion_ready'])
        self.assertEqual(failed_returns['quality']['on']['final_fields']['repair_quality_return_aborted_total']['total'],
                         len(rows))
        self.assertEqual(failed_returns['quality']['on']['final_fields']['repair_quality_delivered_total']['total'], 0)


    def test_diagnostic_manifest_needs_no_fictitious_independence_power_or_confirmation(self):
        self.diagnostic_manifest()
        provenance = subject.validate_manifest(self.manifest, self.root)
        self.assertEqual(provenance['purpose'], 'diagnostic')
        self.assertEqual(provenance['games_per_case'], 2)
        self.assertEqual(provenance['decks']['pilot']['case_count'], 1)
        self.assertEqual(provenance['decks']['pilot']['case_indices'], [1])
        self.assertNotIn('confirmation', provenance['decks'])
        self.manifest['budget']['max_games'] = 1
        with self.assertRaisesRegex(ValueError, 'game budget'):
            subject.validate_manifest(self.manifest, self.root)

    def test_diagnostic_case_index_selects_original_full_deck_without_reseating_or_budget_inflation(self):
        deck = self.diagnostic_manifest(cases=128, index=128)
        provenance = subject.validate_manifest(self.manifest, self.root)
        self.assertEqual(provenance['decks']['pilot']['case_indices'], [128])
        self.assertEqual(provenance['decks']['pilot']['source_case_count'], 128)
        self.assertEqual(provenance['decks']['pilot']['case_count'], 1)
        self.assertEqual(provenance['decks']['pilot']['seeds'], [deck['cases'][127]['seed']])
        self.diagnostic_tables(indices=(128,))
        report = subject.experiment_report(self.path, 'pilot', self.off, self.on)
        self.assertTrue(report['facts']['transport_verified'])
        self.assertTrue(report['facts']['actuation_verified'])
        self.assertEqual(report['expected_games_per_arm'], 1)
        self.assertEqual(report['metrics']['mechanism_metric']['paired_observations'], 1)
        self.assertEqual(report['status'], 'inconclusive')
        self.manifest['stages']['pilot']['args'] += ['--diagnostic-house', '3', '--diagnostic-case-index', '1']
        with self.assertRaisesRegex(ValueError, 'index argument disagrees'):
            subject.validate_manifest(self.manifest, self.root)

    def test_diagnostic_purpose_target_index_and_pilot_boundaries_are_fail_closed(self):
        self.manifest['purpose'] = 'analysis'
        with self.assertRaisesRegex(ValueError, 'Unsupported.*purpose'):
            subject.validate_manifest(self.manifest, self.root)
        self.manifest.pop('purpose')
        self.manifest['diagnostic_house'] = 3
        with self.assertRaisesRegex(ValueError, 'requires purpose'):
            subject.validate_manifest(self.manifest, self.root)
        self.manifest.pop('diagnostic_house')
        self.manifest['diagnostic_case_index'] = 1
        with self.assertRaisesRegex(ValueError, 'requires purpose'):
            subject.validate_manifest(self.manifest, self.root)
        self.manifest.pop('diagnostic_case_index')
        self.manifest['baseline']['independent'] = False
        with self.assertRaisesRegex(ValueError, 'must be independent'):
            subject.validate_manifest(self.manifest, self.root)
        self.diagnostic_manifest()
        for target in (None, '3', True, 0, 4):
            with self.subTest(target=target):
                self.manifest['diagnostic_house'] = target
                with self.assertRaisesRegex(ValueError, 'physical diagnostic_house 3'):
                    subject.validate_manifest(self.manifest, self.root)
        self.manifest['diagnostic_house'] = 3
        for index in (0, True, '1', 2):
            with self.subTest(index=index):
                self.manifest['diagnostic_case_index'] = index
                with self.assertRaisesRegex(ValueError, 'index|original deck'):
                    subject.validate_manifest(self.manifest, self.root)
        self.manifest.pop('diagnostic_case_index')
        self.manifest['stages']['confirmation'] = dict(self.manifest['stages']['pilot'])
        with self.assertRaisesRegex(ValueError, 'pilot only'):
            subject.validate_manifest(self.manifest, self.root)
        self.manifest['stages'].pop('confirmation')
        self.manifest['stages']['pilot']['args'] += ['--diagnostic-house', '4']
        with self.assertRaisesRegex(ValueError, 'target argument disagrees'):
            subject.validate_manifest(self.manifest, self.root)

    def test_targeted_diagnostic_report_is_descriptive_even_with_apparent_wins_and_attestations(self):
        self.diagnostic_manifest()
        self.diagnostic_tables()
        reference = self.root / 'diagnostic-review.txt'
        reference.write_text('Consumer fixture only, not native games or independent strength evidence.\n')
        evidence = {name: dict(passed=True, references=[str(reference)]) for name in (
            'independent_holdout', 'off_contract', 'mechanism', 'quality_review')}
        report = subject.experiment_report(self.path, 'pilot', self.off, self.on, evidence)
        self.assertEqual(report['purpose'], 'diagnostic')
        self.assertEqual(report['diagnostic_house'], 3)
        self.assertEqual(report['status'], 'inconclusive')
        self.assertEqual(report['observations_per_case_per_arm'], 1)
        self.assertTrue(report['facts']['transport_verified'])
        self.assertTrue(report['facts']['actuation_verified'])
        self.assertTrue(report['facts']['quality_measurements_complete'])
        self.assertFalse(report['facts']['primary_confirmed'])
        self.assertFalse(report['facts']['power_sufficient'])
        self.assertFalse(report['facts']['promotion_controls_passed'])
        self.assertFalse(report['facts']['promotion_ready'])
        for metric in report['metrics'].values():
            self.assertEqual(metric['paired_observations'], 1)
            self.assertIsNone(metric['interval'])
            self.assertFalse(metric['power_sufficient'])
            self.assertNotIn('confidence', metric)
            self.assertTrue(metric['descriptive'])
        self.assertEqual(report['metrics']['mechanism_metric']['effect'], 1)
        self.assertEqual(report['metrics']['primary']['effect'], 1)
        self.assertEqual(report['guards']['repair_quality_service_paid_raw_total']['unit'],
                         'Q32 credits raw (1 credit = 4294967296)')
        with self.assertRaisesRegex(ValueError, 'Stage not in'):
            subject.experiment_report(self.path, 'confirmation', self.off, self.on, evidence)
        facts = {name: True for name in ('independent_holdout', 'off_contract_verified', 'mechanism_verified',
            'primary_confirmed', 'power_sufficient', 'guards_complete', 'quality_reviewed', 'promotion_controls_passed')}
        facts['diagnostic'] = True
        self.assertEqual(subject.decide('confirmation', facts), 'inconclusive')

    def test_diagnostic_missing_activation_capture_or_quality_cannot_become_zero_or_confirmation(self):
        self.diagnostic_manifest()
        originals = self.diagnostic_tables()
        for mode in ('target', 'phase', 'peer', 'treatment', 'capture', 'quality'):
            with self.subTest(mode=mode):
                data = json.loads(json.dumps(originals['on']))
                if mode == 'target':
                    data['games'][0]['candidate_house'] = '0'
                elif mode == 'phase':
                    data['games'][0]['genome_phase_receipts'] = '[]'
                elif mode == 'peer':
                    configs = json.loads(data['games'][0]['ai_config_receipts'])
                    configs[0]['config_hash'] = '2' * 16
                    data['games'][0]['ai_config_receipts'] = json.dumps(configs)
                elif mode == 'treatment':
                    for row in data['repairs']:
                        if row['house'] == '3' and row['event'] == 'repair_capacity':
                            row['repair_treatment'] = 0
                elif mode == 'capture':
                    for row in data['repairs']:
                        row['capture_status'] = 'capture_limit'
                else:
                    data['repairs'][-1]['repair_quality_service_paid_raw_total'] = ''
                for name in ('games', 'houses'):
                    self.table(self.on, name + '.csv', data[name])
                self.table(self.on, 'repair-capacity.csv', data['repairs'])
                report = subject.experiment_report(self.path, 'pilot', self.off, self.on)
                self.assertEqual(report['status'], 'inconclusive')
                self.assertFalse(report['facts']['promotion_ready'])
                if mode in ('target', 'phase', 'peer', 'treatment', 'capture'):
                    self.assertFalse(report['facts']['actuation_verified'])
                if mode in ('capture', 'quality'):
                    self.assertFalse(report['facts']['quality_measurements_complete'])
                    self.assertIsNone(report['metrics']['mechanism_metric']['effect'])
                    self.assertGreater(report['metrics']['mechanism_metric']['coverage']['partial_telemetry'], 0)

    def test_compressed_tables_preserve_missing_and_conflicting_identity(self):
        self.table(self.off, 'houses.csv', [self.house()])
        path = self.off / 'houses.csv'
        with gzip.open(str(path) + '.gz', 'wb') as handle:
            handle.write(path.read_bytes())
        path.unlink()
        self.assertEqual(subject.read_table(self.off, 'houses.csv', ('id','house'))[0]['repair_yards'], '')
        self.table(self.on, 'houses.csv', [self.house(repair_yards='2')])
        with self.assertRaisesRegex(ValueError, 'conflicting identities'):
            subject.dataset([self.off, self.on])


    def test_confirmed_strength_is_separate_from_permission_to_promote(self):
        facts = {field: True for field in ('independent_holdout', 'off_contract_verified', 'mechanism_verified',
                                          'primary_confirmed', 'power_sufficient', 'guards_complete')}
        facts.update(quality_reviewed=False, promotion_controls_passed=False)
        self.assertEqual(subject.decide('confirmation', facts), 'confirmed')
        self.assertEqual(subject.decide('pilot', facts), 'inconclusive')


    def test_historical_quality_comparison_keeps_any_winner_and_candidate_wins_separate(self):
        self.paired()
        self.table(self.on, 'games.csv', [self.game('g1-seat' + str(seat), arm='on', winner=1,
                                                   candidate_seat=str(seat)) for seat in range(2)])
        self.table(self.on, 'houses.csv', [self.house('g1-seat' + str(seat), house=str(seat), won=1,
            seat_style=self.manifest['configs']['on']) for seat in range(2)])
        result = subject.quality_comparison(self.off, self.on)
        self.assertEqual(result['matched_house_games'], 2)
        self.assertEqual(result['control']['outcomes']['engine_decided_games'], 2)
        self.assertEqual(result['candidate']['outcomes']['engine_decided_games'], 2)
        self.assertEqual(result['control']['outcomes']['candidate_house_results'], {'loss': 2})
        self.assertEqual(result['candidate']['outcomes']['candidate_house_results'], {'win': 2})
        self.assertIsNone(result['final_stock_changes']['repair_yards']['mean_delta'])
        self.assertEqual(result['final_stock_changes']['repair_yards']['observed_pairs'], 0)


    def test_observed_variance_only_plans_future_work_not_current_power(self):
        values = [(('seed' + str(index),), value) for index, value in enumerate((.25, .26, .27, .28))]
        result = subject.clustered_effect(values, self.manifest['design'], .1, planned_clusters=4)
        self.assertIsNotNone(result['observed_cluster_sd'])
        self.assertEqual(result['exploratory_future_clusters_normal_approximation'], 2)
        self.assertIsNone(result['planned_clusters_normal_approximation'])
        self.assertFalse(result['power_sufficient'])
        design = dict(self.manifest['design'], paired_cluster_sd=.02)
        planned = subject.clustered_effect(values, design, .1, planned_clusters=4)
        self.assertTrue(planned['power_sufficient'])
        self.assertEqual(planned['interval'], result['interval'])
        self.assertEqual(planned['effect'], result['effect'])
        self.assertFalse(subject.clustered_effect(values, design, .1)['power_sufficient'])
        self.assertFalse(subject.clustered_effect(values, design, .1, planned_clusters=1)['power_sufficient'])
        self.assertFalse(subject.clustered_effect(values, design, .1, planned_clusters=5)['power_sufficient'])

    def test_explicit_planning_variance_must_be_finite_and_positive(self):
        for value in (None, '', 0, -1, float('nan'), float('inf'), True):
            with self.subTest(value=value):
                self.manifest['design']['paired_cluster_sd'] = value
                with self.assertRaisesRegex(ValueError, 'paired_cluster_sd'):
                    subject.validate_manifest(self.manifest, self.root)
                with self.assertRaisesRegex(ValueError, 'paired_cluster_sd'):
                    subject.clustered_effect([], self.manifest['design'], .1, planned_clusters=4)

    def test_same_outcomes_need_independent_planning_for_confirmation_or_harm(self):
        seeds = (999, 1000, 1001, 1002)
        path = self.root / 'confirmation.json'
        deck = json.loads(path.read_text())
        deck['cases'] = [dict(deck['cases'][0], seed=seed) for seed in seeds]
        path.write_text(json.dumps(deck))
        self.manifest['budget']['max_games'] = 20
        self.manifest['primary']['minimum_effect'] = .5
        evidence_path = self.root / 'attestations.txt'
        evidence_path.write_text('Retained isolated fixture: OFF identity, intended mechanism and locked untouched holdout.\n')
        evidence = {name: dict(passed=True, references=[str(evidence_path)])
                    for name in ('independent_holdout', 'off_contract', 'mechanism')}
        evidence.update(self.evidence)
        for direction, disposition in ((1, 'confirmed'), (-1, 'harmful')):
            with self.subTest(direction=direction):
                for arm, directory in (('off', self.off), ('on', self.on)):
                    games, houses = [], []
                    for seed in seeds:
                        for seat in range(2):
                            game_id = 'confirmation-' + str(seed) + '-' + str(seat)
                            won = int(arm == 'on' and (seed != seeds[-1] or seat == 0))
                            if direction < 0:
                                won = 1 - won
                            games.append(self.game(game_id, seed=seed, arm=arm, winner=1 if won else 2,
                                                   map_hash='map-' + str(seed), candidate_seat=str(seat),
                                                   sample_case=str(seeds.index(seed) + 1),
                                                   candidate_house=str((seeds.index(seed) + seat) % 2)))
                            houses.extend(self.house(game_id, house=house, won=won, triggers='1',
                                seat_style=self.manifest['configs'][arm] if house == games[-1]['candidate_house'] else 'control',
                                repaired='1' if arm == 'on' else '0') for house in ('0', '1'))
                    self.table(directory, 'games.csv', games)
                    self.table(directory, 'houses.csv', houses)
                self.manifest['design'].pop('paired_cluster_sd', None)
                self.save_manifest()
                exploratory = subject.experiment_report(self.path, 'confirmation', self.off, self.on, evidence)
                self.assertEqual(exploratory['status'], 'inconclusive')
                self.assertFalse(exploratory['facts']['power_sufficient'])
                self.assertFalse(exploratory['facts']['harm_demonstrated'])
                self.assertEqual(exploratory['metrics']['primary']['exploratory_future_clusters_normal_approximation'], 2)
                self.manifest['design']['paired_cluster_sd'] = .25
                self.save_manifest()
                planned = subject.experiment_report(self.path, 'confirmation', self.off, self.on, evidence)
                self.assertEqual(planned['status'], disposition)
                self.assertTrue(planned['facts']['power_sufficient'])
                self.assertEqual(planned['metrics']['primary']['frozen_cluster_budget'], 4)
                self.assertEqual(planned['metrics']['primary']['interval'], exploratory['metrics']['primary']['interval'])
                frozen_hashes = self.manifest.pop('effective_config_hashes')
                self.save_manifest()
                unverified = subject.experiment_report(self.path, 'confirmation', self.off, self.on, evidence)
                self.assertEqual(unverified['status'], 'inconclusive')
                self.assertFalse(unverified['facts']['actuation_verified'])
                self.assertEqual(unverified['metrics']['primary']['interval'], planned['metrics']['primary']['interval'])
                self.manifest['effective_config_hashes'] = frozen_hashes


    def test_requested_names_cannot_replace_frozen_hashes_or_actual_controller_receipts(self):
        mutations = (
            ('config-unverified', lambda game: game.pop('config_hash')),
            ('controller-unverified', lambda game: game.pop('ai_config_receipts')),
            ('controller-unverified', lambda game: game.pop('genome_phase_receipts')),
            ('effective-config', lambda game: game.update(ai_config_hashes='0:' + '1' * 16 + ',1:' + '1' * 16)),
            ('effective-config', lambda game: game.update(ai_config_receipts=json.dumps([
                dict(house='0', config_hash='2' * 16), dict(house='1', config_hash='2' * 16)]))),
            ('controller-unverified', lambda game: game.update(ai_config_receipts=json.dumps([
                dict(house='0', config_hash='2' * 16), dict(house='0', config_hash='1' * 16),
                dict(house='1', config_hash='1' * 16)]))),
            ('phase-identity', lambda game: game.update(genome_phase_receipts='[]')),
            ('phase-identity', lambda game: game.update(genome_phase_receipts=json.dumps([dict(
                house='0', config_hash='2' * 16, genome='candidate-repair-demand-on',
                controller='1', difficulty='3', phase='opening', cycle='0')]))),
            ('candidate-seat', lambda game: game.update(candidate_seat='swapped')),
            ('planned-case', lambda game: game.update(sample_case='2')),
        )
        for reason, mutate in mutations:
            with self.subTest(reason=reason):
                self.paired()
                games = subject.dataset([self.on])['games']
                mutate(games[0])
                self.table(self.on, 'games.csv', games)
                self.save_manifest()
                result = subject.experiment_report(self.path, 'pilot', self.off, self.on, self.evidence)
                self.assertEqual(result['status'], 'inconclusive')
                self.assertGreater(result['metrics']['primary']['coverage']['rejected'].get(reason, 0), 0)

    def test_missing_or_mismatched_calibration_keeps_measurements_but_cannot_conclude(self):
        self.paired()
        frozen = json.loads(json.dumps(self.manifest))
        for change in ('absent', 'hash', 'off', 'references', 'missing-reference'):
            with self.subTest(change=change):
                self.manifest = json.loads(json.dumps(frozen))
                if change == 'absent':
                    self.manifest.pop('effective_config_hashes')
                elif change == 'hash':
                    self.manifest['effective_config_hashes']['on'] = '3' * 16
                elif change == 'off':
                    self.manifest['effective_config_hashes']['off'] = '2' * 16
                elif change == 'references':
                    self.manifest.pop('config_calibration_references')
                else:
                    self.manifest['config_calibration_references'] = [str(self.root / 'missing')]
                self.save_manifest()
                result = subject.experiment_report(self.path, 'pilot', self.off, self.on)
                self.assertEqual(result['status'], 'inconclusive')
                self.assertTrue(result['facts']['transport_verified'])
                self.assertFalse(result['facts']['actuation_verified'])
                self.assertEqual(result['opportunity_coverage']['max'], 0)
                self.assertEqual(result['metrics']['opportunity']['paired_observations'], 2)
                self.assertEqual(result['metrics']['opportunity']['coverage']['rejected'], {})

    def test_identical_static_arm_hashes_preserve_measurements_without_verifying_actuation(self):
        self.paired()
        native = self.manifest['effective_config_hashes']['native']
        self.manifest['effective_config_hashes']['on'] = native
        self.save_manifest()
        candidate = subject.dataset([self.on])
        for game in candidate['games']:
            configs = json.loads(game['ai_config_receipts'])
            phases = json.loads(game['genome_phase_receipts'])
            for receipt in configs + phases:
                receipt['config_hash'] = native
            game['ai_config_receipts'] = json.dumps(configs)
            game['genome_phase_receipts'] = json.dumps(phases)
            game['ai_config_hashes'] = ','.join(row['house'] + ':' + native for row in configs)
        self.table(self.on, 'games.csv', candidate['games'])
        result = subject.experiment_report(self.path, 'pilot', self.off, self.on)
        self.assertEqual(result['status'], 'inconclusive')
        self.assertTrue(result['facts']['transport_verified'])
        self.assertFalse(result['facts']['actuation_verified'])
        self.assertFalse(result['facts']['primary_confirmed'])
        self.assertFalse(result['facts']['mechanism_adequately_unsupported'])
        self.assertEqual(result['opportunity_coverage']['max'], 0)
        self.assertEqual(result['metrics']['opportunity']['paired_observations'], 2)
        self.assertEqual(result['metrics']['opportunity']['coverage']['rejected'], {})

    def test_malformed_expected_hashes_are_rejected_without_guessing_names(self):
        for hashes in (None, {}, {'native': '1' * 16, 'off': '1' * 16, 'on': 'repair-demand-on'},
                       {'native': '1' * 16, 'off': '1' * 16, 'on': 'A' * 16},
                       {'native': '1' * 16, 'off': '1' * 16, 'on': '2' * 16, 'house': '0'}):
            with self.subTest(hashes=hashes):
                self.manifest['effective_config_hashes'] = hashes
                with self.assertRaisesRegex(ValueError, 'effective_config_hashes'):
                    subject.validate_manifest(self.manifest, self.root)

    def test_raw_repair_stream_reaches_csv_and_clustered_report_without_inventing_missing_zero(self):
        from test_dunecity_audit import audit, write_capture
        self.manifest['opportunity'].update(metric='repair_serviceable_waiting', aggregation='decision_max')
        self.manifest['mechanism_metric'].update(metric='repair_actual', aggregation='decision_mean')
        self.save_manifest()
        for mode, expected in (('complete', 'opportunity-missing'), ('truncated', 'inconclusive'),
                               ('absent', 'inconclusive'), ('missing-field', 'inconclusive'),
                               ('wrong-treatment', 'inconclusive'), ('wrong-player', 'inconclusive')):
            with self.subTest(mode=mode):
                self.paired()
                for arm, directory in (('off', self.off), ('on', self.on)):
                    observations = []
                    for game in subject.dataset([directory])['games']:
                        records = [('session_start', -1, -1, 0, {})]
                        if mode != 'absent':
                            for house in ('0', '1'):
                                data = dict(treatment=int(arm == 'on' and house == game['candidate_house']),
                                    actual=1, committed=1, busy=0, serviceable_waiting=0,
                                    reason='legacy' if arm == 'off' else 'no_serviceable_saturation', state_id=0)
                                if mode == 'missing-field' and house == game['candidate_house']:
                                    data.pop('serviceable_waiting')
                                if mode == 'wrong-treatment' and arm == 'on':
                                    data['treatment'] = 0
                                records.append(('repair_capacity', int(house), int(house) + 1, 25, data))
                                if mode == 'wrong-player' and arm == 'on' and house == game['candidate_house']:
                                    records.append(('repair_capacity', int(house), 99, 50, data))
                        records += [('game_summary', -1, -1, 100, dict(houses={'0': {}, '1': {}})),
                                    ('session_end', -1, -1, 100, {})]
                        if mode == 'truncated':
                            records.pop()
                        events = [dict(schema_version=1, session='raw-' + game['id'], seq=seq, event=name,
                                       house=house, player=player, cycle=cycle, data=data)
                                  for seq, (name, house, player, cycle, data) in enumerate(records, 1)]
                        out = self.root / ('raw-' + arm + '-' + game['id'])
                        write_capture(out, events)
                        capture = audit['air_kill_capture'](out, [dict(house='0', team='1'), dict(house='1', team='2')], 100, True)
                        observations.extend(dict(id=game['id'], **row) for row in subject.repair_observations(capture))
                    path = directory / 'repair-capacity.csv'
                    if path.exists():
                        path.unlink()
                    audit['append'](path, audit['REPAIR_FIELDS'], observations)
                result = subject.experiment_report(self.path, 'pilot', self.off, self.on, self.evidence)
                self.assertEqual(result['status'], expected)
                self.assertFalse(result['facts']['power_sufficient'])
                if mode == 'complete':
                    self.assertEqual(result['metrics']['opportunity']['clusters'], 1)
                    self.assertEqual(result['metrics']['opportunity']['paired_observations'], 2)
                    self.assertEqual(result['opportunity_coverage']['max'], 0)
                    row = subject.dataset([self.on])['repairs'][0]
                    self.assertEqual((row['session'], row['seq'], row['cycle'], row['house'], row['player']),
                                     ('raw-g1-seat0', '2', '25', '0', '1'))
                    self.assertEqual(json.loads(row['data'])['state_id'], 0)
                elif mode == 'absent':
                    self.assertIsNone(result['opportunity_coverage']['max'])

    def test_duplicate_original_seat_cannot_complete_a_swapped_deck(self):
        self.paired()
        for arm, directory in (('off', self.off), ('on', self.on)):
            games = [self.game('g1-seat' + str(seat), arm=arm) for seat in range(2)]
            self.table(directory, 'games.csv', games)
            self.table(directory, 'houses.csv', [self.house(game['id'], house=house,
                seat_style=self.manifest['configs'][arm] if house == '0' else 'control')
                for game in games for house in ('0', '1')])
        result = subject.experiment_report(self.path, 'pilot', self.off, self.on, self.evidence)
        self.assertEqual(result['status'], 'inconclusive')
        self.assertFalse(result['facts']['opportunity_complete'])

    def test_missing_later_stock_does_not_disappear_from_coverage_or_become_last_zero(self):
        self.paired()
        data = subject.dataset([self.off])
        data['repairs'] = [dict(id='g1-seat0', house='0', cycle='25', seq='1',
                               repair_actual='1', capture_status='complete'),
                           dict(id='g1-seat0', house='0', cycle='50', seq='2',
                               repair_actual='', capture_status='incomplete')]
        self.assertEqual(subject.measured_metric(data, 'repair_actual', 'decision_max')[('g1-seat0', '0')], (1, True))
        self.assertEqual(subject.measured_metric(data, 'repair_actual', 'decision_last')[('g1-seat0', '0')], (None, True))


    def legacy_sample_header(self):
        # Retained samples.csv had 184 columns with these exact duplicate positions.
        fields = ['field_' + str(index) for index in range(184)]
        fields[:3] = ['id', 'house', 'cycle']
        fields[98] = fields[181] = 'harvesters_lost'
        fields[114] = fields[179] = 'refinery_bays'
        return fields

    def write_legacy_samples(self, fields, rows):
        path = self.off / 'samples.csv.gz'
        with gzip.open(path, 'wt', encoding='utf-8', newline='') as handle:
            writer = csv.writer(handle)
            writer.writerow(fields)
            writer.writerows(rows)
        return path

    def test_historical_duplicate_columns_agree_before_collapsing_missing_or_zero(self):
        fields = self.legacy_sample_header()
        blank = dict(id='g1', house='0', cycle='0')
        measured = dict(id='g1', house='0', cycle='18750', harvesters_lost='7', refinery_bays='0',
                        field_5='Quoted, diagnostic\nwith a second line')
        self.write_legacy_samples(fields, [[record.get(field, '') for field in fields] for record in (blank, measured)])
        rows = subject.read_table(self.off, 'samples.csv', ('id', 'house', 'cycle'))
        self.assertEqual([(row['harvesters_lost'], row['refinery_bays']) for row in rows], [('', ''), ('7', '0')])
        self.assertEqual(rows[1]['field_5'], measured['field_5'])
        self.assertEqual(subject.coverage(rows, 'harvesters_lost')['observed'], 1)
        self.assertEqual(subject.coverage(rows, 'refinery_bays')['observed'], 1)
        self.assertEqual(subject.coverage(rows, 'refinery_bays')['mean'], 0)

    def test_historical_duplicate_columns_require_exact_not_numeric_agreement(self):
        for field, first, last in (('harvesters_lost', '7', '7.0'), ('refinery_bays', '', '0'),
                                   ('future_metric', 'observed', 'different')):
            with self.subTest(field=field):
                fields = self.legacy_sample_header()
                if field == 'future_metric':
                    fields[98] = fields[181] = field
                values = [dict(id='g1', house='0', cycle='0').get(name, '') for name in fields]
                positions = [index for index, name in enumerate(fields) if name == field]
                values[positions[0]], values[positions[-1]] = first, last
                self.write_legacy_samples(fields, [values])
                with self.assertRaisesRegex(ValueError, 'Conflicting duplicate CSV column'):
                    subject.read_table(self.off, 'samples.csv', ('id', 'house', 'cycle'))

    def test_normalization_preserves_row_bounds_width_and_identity_checks(self):
        fields = self.legacy_sample_header()
        first = [dict(id='g1', house='0', cycle='0').get(field, '') for field in fields]
        second = [dict(id='g1', house='0', cycle='18750').get(field, '') for field in fields]
        self.write_legacy_samples(fields, [first, second])
        with patch.object(subject, 'MAX_ROWS', 1):
            with self.assertRaisesRegex(ValueError, 'Audit row bound'):
                subject.read_table(self.off, 'samples.csv', ('id', 'house', 'cycle'))
        for values in (first[:-1], first + ['extra']):
            self.write_legacy_samples(fields, [values])
            with self.assertRaisesRegex(ValueError, 'Malformed CSV'):
                subject.read_table(self.off, 'samples.csv', ('id', 'house', 'cycle'))
        first[0] = ''
        self.write_legacy_samples(fields, [first])
        with self.assertRaisesRegex(ValueError, 'Missing row identity'):
            subject.read_table(self.off, 'samples.csv', ('id', 'house', 'cycle'))


    def test_unknown_historical_duplicate_name_is_normalized_without_a_field_allowlist(self):
        fields = self.legacy_sample_header()
        fields[98] = fields[181] = 'future_metric'
        record = dict(id='g1', house='0', cycle='0', future_metric='exact, quoted\nobservation')
        self.write_legacy_samples(fields, [[record.get(field, '') for field in fields]])
        restored = subject.read_table(self.off, 'samples.csv', ('id', 'house', 'cycle'))
        self.assertEqual(restored[0]['future_metric'], record['future_metric'])
        self.assertEqual(restored[0]['refinery_bays'], '')


if __name__ == '__main__':
    unittest.main()
