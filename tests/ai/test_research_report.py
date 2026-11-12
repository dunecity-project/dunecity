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

    def test_missing_repair_counts_are_not_zero_or_refinery_usage(self):
        self.paired()
        data = subject.dataset([self.on])
        report = subject.quality_report(data, [self.on])
        self.assertEqual(report['final_fields']['repair_yards']['observed'], 0)
        self.assertIsNone(report['final_fields']['repair_yards']['mean'])
        self.assertEqual(report['final_fields']['occupied_refinery_bays']['mean'], 2)
        self.assertIn('repair_health_restored', report['missing_repair_usage'])
        self.assertIsNone(report['cost']['game_cpu_s'])
        self.assertEqual(report['cost']['game_wall_s'], 20)
        self.assertEqual(report['outcomes']['engine_decided_games'], 2)
        self.assertEqual(report['outcomes']['candidate_house_results'], {'loss': 2})

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
        self.assertNotIn('repair_completed_service', row)
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
        self.table(self.on, 'games.csv', [self.game('g1-seat' + str(seat), arm='on', winner=1) for seat in range(2)])
        self.table(self.on, 'houses.csv', [self.house('g1-seat' + str(seat), won=1) for seat in range(2)])
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
