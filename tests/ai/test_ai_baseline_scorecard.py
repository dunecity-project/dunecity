#!/usr/bin/env python3
"""Contract fixtures for native extraction; no game engine, builds or simulation."""
import copy
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ai_baseline_scorecard as scorecard


class ScorecardTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.game_id = 'matrix/cy-a0-c0/128/medium/1v1/hard/40m/9101'
        self.source = 'a' * 40
        self.native_source = 'b' * 40
        self.map_bytes = b'; exact frozen native map\n[MAP]\nSizeX = 128\n'
        self.options_bytes = b'[Game Options]\nCity Effects = true\nResearch Lab = false\n'
        self.map_sha = hashlib.sha256(self.map_bytes).hexdigest()
        self.options_sha = hashlib.sha256(self.options_bytes).hexdigest()
        self.end = 40 * scorecard.CYCLES_PER_MINUTE
        self.game = dict(id=self.game_id, mod='dunecity', difficulty='hard', seed=9101,
                         baseline_release_source=self.native_source, source_commit=self.source,
                         map_sha256=self.map_sha, options_sha256=self.options_sha,
                         probe_sha256='c' * 64, content_hash='d' * 16,
                         object_data_hash='e' * 16, city_sim=1, cycle=self.end,
                         winner_team=0, outcome='time_limit', error='', research='off',
                         telemetry_status='complete', unsupported_options='{}')
        self.points = []
        self.units = []
        for minute in range(41):
            cycle = minute * scorecard.CYCLES_PER_MINUTE
            for house in (0, 1):
                point = dict(id=self.game_id, mod='dunecity', difficulty='hard', seed=9101,
                             cycle=cycle, house=house, team=house + 1, alive=1,
                             credits=3000, structures=1, army_units=1, army_value=1000,
                             kill_value=100 * house, loss_value=0, tech_level=9,
                             eliminated_cycle=-1, harvesters=1, refineries=1,
                             population=100, zone_residential=1, zone_industrial=2,
                             zone_commercial=3, power=100, need=0, built=0,
                             structures_lost=0, base_hp=400, yards=1, yard_level=0,
                             queue=0, upgrading=0, waiting=0, research_enabled=0)
                for name in ('light', 'heavy', 'ix', 'starport', 'palace'):
                    point.update({name: 0, name + '_built': 0,
                                  'first_' + name + '_owned_cycle': -1,
                                  'first_' + name + '_completed_cycle': -1})
                self.points.append(point)
                for item in (28, 29, 40):
                    self.units.append(dict(id=self.game_id, mod='dunecity', difficulty='hard', seed=9101,
                                           house=house, cycle=cycle, item=item, built=0,
                                           live_count=0, live_value=0, lost=0))
        self.finals = [copy.deepcopy(point) for point in self.points if point['cycle'] == self.end]
        self.fixture = dict(release='1.0.807', release_source=self.native_source,
                            games=[dict(id=self.game_id, mod='dunecity', seed=9101,
                                        difficulty='hard', roster=[[0, 1], [1, 2]],
                                        map_sha256=self.map_sha, options_sha256=self.options_sha,
                                        city_effects=True)])
        self.fixture_path = self.root / 'fixtures.json'
        self.run = dict(release='1.0.807', source_commit=self.source,
                        baseline_release_source=self.native_source, arm='historical',
                        research='off', mod='dunecity', results=str(self.root),
                        format='historical', fixtures=str(self.fixture_path))

    def table(self, name, rows):
        fields = list(dict.fromkeys(field for row in rows for field in row))
        with (self.root / name).open('w', newline='', encoding='utf-8') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, lineterminator='\n')
            writer.writeheader()
            writer.writerows(rows)

    def save(self):
        self.table('games.csv', [self.game])
        self.table('houses.csv', self.finals)
        self.table('samples.csv', self.points)
        self.table('units.csv', self.units)
        self.fixture_path.write_text(json.dumps(self.fixture), encoding='utf-8')
        if not (self.root / 'waves.csv').is_file():
            with (self.root / 'waves.csv').open('w', newline='', encoding='utf-8') as handle:
                csv.writer(handle).writerow(('id', 'house', 'cycle', 'members', 'value'))

    def rows(self):
        self.save()
        return scorecard.scorecard(self.run)

    @staticmethod
    def find(rows, metric, house=0, minute=-1):
        matches = [row for row in rows if row['metric'] == metric and row['house'] == house and row['minute'] == minute]
        if len(matches) != 1:
            raise AssertionError('Expected unique row: %s/%s/%s: %r' % (metric, house, minute, matches))
        return matches[0]

    def test_exact_long_schema_context_and_unique_observations(self):
        rows = self.rows()
        expected = hashlib.sha256(json.dumps(dict(id=self.game_id, mod='dunecity', difficulty='hard',
                                                 research='off', seed=9101),
                                             sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        identities = set()
        for row in rows:
            self.assertEqual(tuple(row), scorecard.COLUMNS)
            self.assertEqual(row['schema_version'], 1)
            self.assertEqual(row['context_sha256'], expected)
            self.assertIn(row['status'], ('measured', 'unavailable', 'censored', 'not_applicable'))
            identity = row['id'], row['house'], row['metric'], row['minute']
            self.assertNotIn(identity, identities)
            identities.add(identity)
        self.assertEqual(len([row for row in rows if row['metric'] == 'army_value_time_mean']), 2)
        self.assertEqual({row['minute'] for row in rows if row['metric'] == 'army_value'}, {-1, 5, 10, 20, 30, 40})

    def test_no_winner_timeout_is_not_material_adjudicated(self):
        self.finals[0]['kill_value'] = 900000
        rows = self.rows()
        for house in (0, 1):
            self.assertEqual(self.find(rows, 'win', house)['value'], 0)
            self.assertEqual(self.find(rows, 'win_score', house)['value'], -.25)
            self.assertEqual(self.find(rows, 'placement', house)['value'], 1.5)

    def test_complete_zero_attacks_has_indicator_and_administrative_bound(self):
        rows = self.rows()
        self.assertEqual(self.find(rows, 'attack_count')['value'], 0)
        self.assertEqual(self.find(rows, 'first_attack_observed')['value'], 0)
        first = self.find(rows, 'first_attack_minute')
        self.assertEqual((first['status'], first['value']), ('censored', 40))
        self.assertIn('administrative', first['reason'])

    def test_nonempty_ground_dispatch_only_and_no_first_time_from_sorties(self):
        self.save()
        self.table('waves.csv', [dict(id=self.game_id, house=0, cycle=100, members=0, value=0),
                                 dict(id=self.game_id, house=0, cycle=8000, members=2, value=1000)])
        rows = scorecard.scorecard(self.run)
        self.assertEqual(self.find(rows, 'attack_count')['value'], 1)
        self.assertEqual(self.find(rows, 'first_attack_observed')['value'], 1)
        self.assertAlmostEqual(self.find(rows, 'first_attack_minute')['value'], 8000 / 3750)
        self.assertEqual(self.find(rows, 'first_attack_observed', 1)['value'], 0)

    def test_uncertified_historical_callbacks_never_become_zero(self):
        self.game['telemetry_status'] = 'capture_limit'
        rows = self.rows()
        for metric in ('attack_count', 'first_attack_observed', 'first_attack_minute'):
            row = self.find(rows, metric)
            self.assertEqual((row['status'], row['value']), ('unavailable', ''))

    def test_missing_certified_historical_wave_file_does_not_invent_zero(self):
        self.save()
        (self.root / 'waves.csv').unlink()
        rows = scorecard.scorecard(self.run)
        for metric in ('attack_count', 'first_attack_observed', 'first_attack_minute'):
            row = self.find(rows, metric)
            self.assertEqual((row['status'], row['value']), ('unavailable', ''))

    def test_certified_audit_house_dispatch_receipts_without_wave_sidecar(self):
        self.run.update(format='audit', arm='off')
        self.game.update(attack_telemetry=1, air_kill_telemetry='complete', source_fingerprint='f' * 64)
        self.finals[0].update(attack_count=2, first_attack_minute=3.25)
        self.finals[1].update(attack_count=0, first_attack_minute='')
        self.save()
        (self.root / 'waves.csv').unlink()
        rows = scorecard.scorecard(self.run)
        self.assertEqual(self.find(rows, 'attack_count')['value'], 2)
        self.assertEqual(self.find(rows, 'first_attack_minute')['value'], 3.25)
        self.assertEqual(self.find(rows, 'first_attack_observed', 1)['value'], 0)

    def test_defeat_zero_and_survivor_censoring_are_distinct(self):
        self.end = 8 * 3750
        death = 4.5 * 3750
        self.game.update(cycle=self.end, outcome='decided', winner_team=1)
        self.points = [point for point in self.points if point['cycle'] <= self.end]
        self.units = [point for point in self.units if point['cycle'] <= self.end]
        for point in self.points:
            if point['house'] == 1:
                point['eliminated_cycle'] = death if point['cycle'] >= death else -1
                if point['cycle'] >= death:
                    point.update(alive=0, army_value=0, army_units=0)
        self.finals = [copy.deepcopy(point) for point in self.points if point['cycle'] == self.end]
        rows = self.rows()
        self.assertEqual(self.find(rows, 'army_value', 1, 10)['value'], 0)
        self.assertEqual(self.find(rows, 'army_value', 1, 10)['status'], 'measured')
        self.assertEqual(self.find(rows, 'army_value', 0, 10)['status'], 'censored')
        self.assertEqual(self.find(rows, 'win_score', 1)['value'], -1)
        self.assertEqual(self.find(rows, 'win_score', 0)['value'], 1)
        self.assertAlmostEqual(self.find(rows, 'army_value_time_mean', 1)['value'], 1000 * 4.5 / 8)
        dead_first = self.find(rows, 'first_attack_minute', 1)
        self.assertEqual((dead_first['status'], dead_first['value']), ('not_applicable', ''))
        self.assertIn('competing_event', dead_first['reason'])

    def test_missing_checkpoint_is_not_nearest_sample_or_zero(self):
        self.points = [point for point in self.points if not (point['house'] == 0 and point['cycle'] == 10 * 3750)]
        rows = self.rows()
        checkpoint = self.find(rows, 'army_value', minute=10)
        self.assertEqual((checkpoint['status'], checkpoint['value']), ('unavailable', ''))
        self.assertEqual(self.find(rows, 'army_value_time_mean')['status'], 'unavailable')

    def test_missing_numeric_and_broad_807_fields_stay_unavailable(self):
        for point in self.points + self.finals:
            for field in ('harvesters', 'refineries', 'population', 'yards', 'queue', 'upgrading', 'built',
                          'structures_lost', 'light', 'light_built', 'first_light_owned_cycle',
                          'first_light_completed_cycle'):
                point.pop(field, None)
        self.finals[0]['kill_value'] = ''
        self.game.pop('telemetry_status')
        rows = self.rows()
        for metric in ('kill_value', 'harvesters', 'refineries', 'idle_money', 'adopt_light_factory',
                       'population', 'economy_stall_fraction', 'base_loss_fraction'):
            self.assertEqual((self.find(rows, metric)['status'], self.find(rows, metric)['value']), ('unavailable', ''))
        self.assertEqual(self.find(rows, 'adopt_ix')['value'], 0)

    def test_exact_completion_and_sampled_first_owned_are_not_interchanged(self):
        for point in self.points + self.finals:
            if point['house'] == 0 and point['cycle'] >= 5 * 3750:
                point.update(light=1, light_built=1, first_light_owned_cycle=10000,
                             first_light_completed_cycle=10100)
        rows = self.rows()
        self.assertEqual(self.find(rows, 'adopt_light_factory')['value'], 1)
        self.assertAlmostEqual(self.find(rows, 'first_completed_light_factory_minute')['value'], 10100 / 3750)
        self.assertAlmostEqual(self.find(rows, 'first_owned_light_factory_minute')['value'], 10000 / 3750)
        self.assertEqual(self.find(rows, 'first_sampled_owned_light_factory_minute')['value'], 5)

    def test_positive_build_without_first_event_does_not_invent_time(self):
        for point in self.points + self.finals:
            point.pop('first_heavy_owned_cycle')
            point.pop('first_heavy_completed_cycle')
            if point['house'] == 0 and point['cycle'] >= 10 * 3750:
                point.update(heavy=1, heavy_built=1)
        rows = self.rows()
        self.assertEqual(self.find(rows, 'adopt_heavy_factory')['value'], 1)
        self.assertEqual(self.find(rows, 'first_completed_heavy_factory_minute')['status'], 'unavailable')
        self.assertEqual(self.find(rows, 'first_sampled_owned_heavy_factory_minute')['value'], 10)

    def test_lifetime_built_is_not_reconstructed_from_live_share(self):
        for point in self.units:
            if point['house'] == 0 and point['item'] == 28:
                point.update(built=4, live_count=1, live_value=800, lost=3)
        rows = self.rows()
        self.assertEqual(self.find(rows, 'special_built')['value'], 4)
        self.assertEqual(self.find(rows, 'devastator_built')['value'], 4)
        self.assertEqual(self.find(rows, 'special_live_value_share')['value'], .8)
        self.assertEqual(self.find(rows, 'devastator_live_value_share', minute=5)['value'], .8)

    def test_city_disabled_and_research_off_are_explicit_not_applicable(self):
        self.game['city_sim'] = 0
        self.fixture['games'][0]['city_effects'] = False
        rows = self.rows()
        for metric in ('zone_residential', 'zone_industrial', 'zone_commercial', 'population'):
            self.assertEqual(self.find(rows, metric, minute=20)['status'], 'not_applicable')
        self.assertEqual(self.find(rows, 'research_level')['status'], 'not_applicable')

    def test_research_on_is_separate_and_native_max_is_measured(self):
        self.run['research'] = self.game['research'] = 'on'
        for point in self.points + self.finals:
            point['research_enabled'] = 1
            point['tech_level'] = 2 if point['cycle'] < 20 * 3750 else 4
        rows = self.rows()
        self.assertEqual(self.find(rows, 'research_level')['value'], 4)
        self.assertTrue(all(row['research'] == 'on' for row in rows))

    def test_unsupported_historical_research_on_is_rejected(self):
        self.run['research'] = 'on'
        self.save()
        with self.assertRaisesRegex(scorecard.ScorecardError, 'research'):
            scorecard.scorecard(self.run)

    def test_provenance_rejects_source_map_options_and_completion(self):
        mutations = [lambda: self.game.update(source_commit='f' * 40),
                     lambda: self.game.update(map_sha256='f' * 64),
                     lambda: self.game.update(options_sha256='f' * 64),
                     lambda: self.game.update(error='wall_timeout'),
                     lambda: self.game.update(winner_team=1),
                     lambda: self.finals.pop()]
        original_game, original_finals = copy.deepcopy(self.game), copy.deepcopy(self.finals)
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.game, self.finals = copy.deepcopy(original_game), copy.deepcopy(original_finals)
                mutate()
                self.save()
                with self.assertRaises(scorecard.ScorecardError):
                    scorecard.scorecard(self.run)

    def test_duplicate_snapshot_is_rejected_not_pseudoreplicated(self):
        self.points.append(copy.deepcopy(self.points[0]))
        self.save()
        with self.assertRaisesRegex(scorecard.ScorecardError, 'Duplicate snapshot'):
            scorecard.scorecard(self.run)

    def test_compact_summary_ledgers_without_reading_event_jsonl(self):
        for point in self.points + self.finals:
            point.pop('light')
            point.pop('light_built')
            point.pop('first_light_owned_cycle')
            point.pop('first_light_completed_cycle')
        self.units = []
        self.save()
        (self.root / 'units.csv').unlink()
        native_houses = {str(house): dict(team=house + 1, alive=1, actual={'7': 1},
                                        built={'7': 1, '28': 2}, lost={}) for house in (0, 1)}
        summary = dict(setup=dict(game=dict(id=self.game_id), source_commit=self.source),
                       game_summary=[dict(event='game_summary', cycle=self.end,
                                          data=dict(houses=native_houses))])
        archive = self.root / 'probe-evidence.tar.gz'
        with tarfile.open(str(archive), 'w:gz') as handle:
            for name, contents in [('scratch/test/profile/ai-decisions/session/events.jsonl', b'not valid JSON\n' * 100),
                                   ('scratch/test/setup.json', json.dumps(dict(game=dict(id=self.game_id))).encode()),
                                   ('scratch/test/summary.json', json.dumps(summary).encode())]:
                member = tarfile.TarInfo(name)
                member.size = len(contents)
                handle.addfile(member, io.BytesIO(contents))
        rows = scorecard.scorecard(self.run)
        self.assertEqual(self.find(rows, 'adopt_light_factory')['value'], 1)
        self.assertEqual(self.find(rows, 'special_built')['value'], 2)
        self.assertEqual(self.find(rows, 'deviator_built')['value'], 0)
        self.assertEqual(self.find(rows, 'first_completed_light_factory_minute')['status'], 'unavailable')

    def test_unchanged_audit_and_tech_sidecar_timing(self):
        self.run.update(format='audit', arm='off')
        self.game.update(attack_telemetry=1, air_kill_telemetry='complete', source_fingerprint='f' * 64)
        for point in self.points + self.finals:
            point['eliminated_s'] = -1.0
            for field in ('palace', 'palace_built', 'first_palace_owned_cycle', 'first_palace_completed_cycle'):
                point.pop(field)
        self.save()
        self.table('tech-houses.csv', [dict(id=self.game_id, house=0, first_owned_palace_minute=22.5,
                                           first_completed_palace_minute=22.5)])
        rows = scorecard.scorecard(self.run)
        self.assertEqual(self.find(rows, 'adopt_palace')['value'], 1)
        self.assertEqual(self.find(rows, 'first_completed_palace_minute')['value'], 22.5)
        self.assertEqual(self.find(rows, 'first_sampled_owned_palace_minute')['status'], 'unavailable')

    def test_compressed_only_native_csvs_preserve_the_exact_scorecard(self):
        expected = self.rows()
        for path in self.root.glob('*.csv'):
            with path.open('rb') as source:
                with gzip.GzipFile(str(path) + '.gz', 'wb', mtime=0) as destination:
                    destination.write(source.read())
            path.unlink()
        self.assertEqual(scorecard.scorecard(self.run), expected)

    def test_replay_hashes_exact_filedata_not_generator_or_engine_hash(self):
        def string(contents):
            return struct.pack('<I', len(contents)) + contents
        replay = string(b'observer') + bytes((2, 255)) + string(b'map name') + string(self.map_bytes) + b'ignored commands'
        self.assertEqual(scorecard._replay_map(replay), self.map_bytes)
        with self.assertRaises(scorecard.ScorecardError):
            scorecard._replay_map(replay[:12])

    def test_deterministic_gzip_and_cli_fail_closed(self):
        rows = self.rows()
        first, second = self.root / 'first.csv.gz', self.root / 'second.csv.gz'
        scorecard.write_observations(first, rows)
        scorecard.write_observations(second, rows)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with gzip.open(str(first), 'rt', encoding='utf-8', newline='') as handle:
            reader = csv.DictReader(handle)
            self.assertEqual(tuple(reader.fieldnames), scorecard.COLUMNS)
            self.assertEqual(len(list(reader)), len(rows))
        invalid_run = dict(self.run, source_commit='f' * 40, arm='invalid')
        manifest = self.root / 'manifest.json'
        manifest.write_text(json.dumps(dict(schema_version=1, runs=[self.run, invalid_run])))
        output = self.root / 'must-not-be-truncated.csv.gz'
        output.write_bytes(b'preserve preexisting output')
        diagnostics = self.root / 'diagnostics.json'
        self.assertEqual(scorecard.main(['--manifest', str(manifest), '--output', str(output),
                                         '--diagnostics', str(diagnostics)]), 2)
        self.assertEqual(output.read_bytes(), b'preserve preexisting output')
        self.assertEqual(json.loads(diagnostics.read_text())['status'], 'rejected')

    def test_explicit_game_selection_never_silently_omits_missing_ids(self):
        self.save()
        run = dict(self.run, ids=[self.game_id])
        self.assertEqual(len(scorecard.scorecard(run)), len(scorecard.scorecard(self.run)))
        with self.assertRaisesRegex(scorecard.ScorecardError, 'missing'):
            scorecard.scorecard(dict(self.run, ids=[self.game_id, 'not-collected']))

    def test_original_fixture_release_is_map_provenance_not_native_engine_pin(self):
        self.run.update(release='1.0.792', baseline_release_source='9' * 40)
        self.game.update(baseline_release_source='9' * 40)
        rows = self.rows()
        self.assertTrue(all(row['release'] == '1.0.792' for row in rows))
        self.assertTrue(all(row['baseline_release_source'] == '9' * 40 for row in rows))

    def test_explicit_context_receipt_is_the_output_id_not_a_hidden_hash_alias(self):
        context = 'genome-match/128/medium/1v1/hard/40m/9101'
        self.game['context_id'] = self.fixture['games'][0]['context_id'] = context
        rows = self.rows()
        self.assertTrue(all(row['id'] == context for row in rows))
        self.assertEqual(rows[0]['context_sha256'], scorecard.context_sha256(context, 'dunecity', 'hard', 'off', 9101))
        self.fixture['games'][0]['context_id'] = 'another-context'
        self.save()
        with self.assertRaisesRegex(scorecard.ScorecardError, 'context ID receipt'):
            scorecard.scorecard(self.run)

    def test_gzipped_exact_map_options_and_native_summary_receipts(self):
        for filename, contents in [('map.ini.gz', self.map_bytes), ('options.ini.gz', self.options_bytes)]:
            with gzip.open(str(self.root / filename), 'wb') as handle:
                handle.write(contents)
        self.fixture['games'][0].update(map_file='map.ini.gz', options_file='options.ini.gz',
                                       summary_file='native-summary.json.gz')
        for point in self.points + self.finals:
            for field in ('light', 'light_built', 'first_light_owned_cycle', 'first_light_completed_cycle'):
                point.pop(field)
        native = {str(house): dict(team=house + 1, alive=1, actual={'7': 1}, built={}, lost={})
                  for house in (0, 1)}
        summary = dict(sourceCommit=self.source, final=[
            dict(event='game_summary', cycle=self.end, data=dict(houses=native))])
        with gzip.open(str(self.root / 'native-summary.json.gz'), 'wt', encoding='utf-8') as handle:
            json.dump(summary, handle)
        rows = self.rows()
        self.assertEqual(self.find(rows, 'adopt_light_factory')['value'], 1)
        with gzip.open(str(self.root / 'map.ini.gz'), 'wb') as handle:
            handle.write(b'not the frozen map')
        self.save()
        with self.assertRaisesRegex(scorecard.ScorecardError, 'map file receipt'):
            scorecard.scorecard(self.run)

    def test_economy_stall_exposure_is_persistent_cycle_weighted_fraction(self):
        for point in self.points + self.finals:
            point['credits'] = 50
        rows = self.rows()
        self.assertEqual(self.find(rows, 'economy_stall_fraction')['value'], 1)
        self.assertEqual(self.find(rows, 'economy_stall_fraction')['status'], 'measured')

    def test_orphan_house_evidence_is_rejected_not_dropped(self):
        orphan = dict(self.points[0], house=5, team=6)
        self.points.append(orphan)
        self.save()
        with self.assertRaisesRegex(scorecard.ScorecardError, 'unregistered house'):
            scorecard.scorecard(self.run)

    def test_complete_extended_observer_never_opens_optional_raw_archives(self):
        from unittest.mock import patch
        self.save()
        with patch.object(scorecard._Evidence, 'raw', side_effect=AssertionError('unnecessary raw I/O')):
            rows = scorecard.scorecard(self.run)
        self.assertEqual(self.find(rows, 'special_built')['value'], 0)
        self.assertEqual(self.find(rows, 'adopt_light_factory')['value'], 0)

    def test_extinct_timeout_preserves_no_winner_and_elimination_placement(self):
        for point in self.finals:
            point.update(alive=0, eliminated_cycle=self.end, army_value=0)
        for point in self.points:
            if point['cycle'] == self.end:
                point.update(alive=0, eliminated_cycle=self.end, army_value=0)
        rows = self.rows()
        self.assertEqual(self.find(rows, 'win')['value'], 0)
        self.assertEqual(self.find(rows, 'win', 1)['value'], 0)
        self.assertEqual(self.find(rows, 'placement')['value'], 1.5)

    def test_empty_army_share_is_undefined_with_exposure_not_invented_zero(self):
        for point in self.points + self.finals:
            point.update(army_value=0, army_units=0)
        rows = self.rows()
        for metric in ('special_live_value_share', 'devastator_live_value_share',
                       'deviator_live_value_share', 'sonic_tank_live_value_share'):
            row = self.find(rows, metric)
            self.assertEqual((row['status'], row['value']), ('not_applicable', ''))
            self.assertEqual(row['reason'], 'no_live_army_denominator')
        for metric in ('army_nonempty_minutes', 'army_nonempty_episodes', 'army_nonempty_snapshots'):
            self.assertEqual(self.find(rows, metric)['value'], 0)

    def test_core_metadata_is_independent_and_margin_explicit(self):
        self.assertEqual(set(scorecard.CORE_FAMILIES), {metric for metric, definition in
                                                       scorecard.METRIC_DEFINITIONS.items() if definition['core']})
        for metric in scorecard.CORE_FAMILIES:
            definition = scorecard.METRIC_DEFINITIONS[metric]
            self.assertIn(definition['direction'], ('higher', 'lower'))
            expected = 'research_on' if metric == 'research_level' else 'city' if metric in (
                'zone_residential', 'zone_industrial', 'zone_commercial', 'population') else 'all'
            self.assertEqual(definition['applicability'], expected)
            self.assertEqual(definition['core_minutes'], (-1,))
            self.assertGreater(definition['practical_absolute_margin'] + definition['practical_relative_margin'], 0)
        self.assertEqual(scorecard.METRIC_DEFINITIONS['attack_count']['practical_relative_margin'], .10)
        self.assertEqual(scorecard.METRIC_DEFINITIONS['base_loss_fraction']['direction'], 'lower')


if __name__ == '__main__':
    unittest.main()
