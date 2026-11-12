#!/usr/bin/env python3
"""Source-only regression cases; the batch integrator runs verification."""

import base64
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

import ai_map_features as features


def map_ini(rows, structures='', units='', players=('Harkonnen',)):
    width, height = len(rows[0]), len(rows)
    text = '[BASIC]\nVersion=2\n[MAP]\nSizeX=%d\nSizeY=%d\n' % (width, height)
    text += ''.join(f'{index:03}={row}\n' for index, row in enumerate(rows))
    text += ''.join(f'[{player}]\nCredits=3000\n' for player in players)
    text += f'[STRUCTURES]\n{structures}\n[UNITS]\n{units}\n'
    return text.encode()


def export_rows(identifier, artifact, data, chunk_size=17):
    chunks = [data[offset:offset + chunk_size] for offset in range(0, len(data), chunk_size)] or [b'']
    return [dict(id=identifier, artifact=artifact, sha256=hashlib.sha256(data).hexdigest(),
                 bytes=len(data), chunk=index, chunks=len(chunks), base64=base64.b64encode(chunk).decode())
            for index, chunk in enumerate(chunks)]


def table(path, rows):
    with gzip.open(path, 'wt', newline='') if path.suffix == '.gz' else path.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


class MapFeatureReceiptTests(unittest.TestCase):
    def setUp(self):
        self.game = dict(id='feature-fixture', size=5, base='mcv', sides=[['harkonnen', 1, 'hard']])

    def test_new_markers_share_schema_and_keep_unknown_blank(self):
        lines = [
            'INFO DUNECITY_MAP_FEATURE: cycle=0 house=-1 width=5 height=5 player_count=1 '
            'rock_area=25 rock_components=1 largest_rock_area=25 free3x3_count=9 '
            'largest_component_free3x3=9 base_distance_min=-1 base_distance_mean=-1',
            'INFO DUNECITY_MAP_FEATURE: cycle=0 house=0 width=5 height=5 rock_area=-1 base_x=1 base_y=2',
            'INFO DUNECITY_MAP_FEATURE: cycle=3750 house=0 rock_area=999',
        ]
        rows = features.rows_from_log(lines, self.game, dict(map_hash='receipt-hash'))
        self.assertEqual([row['house'] for row in rows], ['', '0'])
        self.assertTrue(all(tuple(row) == features.MAP_FEATURE_FIELDS for row in rows))
        self.assertEqual(rows[0]['free3x3_count'], 9)
        self.assertEqual(rows[0]['base_distance_min'], '')
        self.assertEqual(rows[1]['rock_area'], '')
        self.assertEqual(rows[1]['measurement_status'], 'engine_start')
        self.assertEqual(rows[1]['map_hash'], 'receipt-hash')
        self.assertEqual(rows[0]['size'], '5')

    def test_integer_start_evidence_retains_physical_houses_and_raw_display(self):
        lines = [
            'DUNECITY_MAP_DISTANCE: cycle=0 house=0 other_house=4 base_x=4 base_y=7 '
            'other_base_x=1 other_base_y=3 dx=3 dy=4 distance_squared=25',
            'DUNECITY_MAP_FEATURE: cycle=0 house=-1 width=8 height=8 player_count=2 '
            'initial_spice_density=0.015624999 map_tile_count=64 initial_spice_tiles=1 '
            'mountain_tiles=3 base_distance_pair_count=1 base_distance_min_squared=25',
            'DUNECITY_MAP_FEATURE: cycle=0 house=4 width=8 height=8 player_count=2 '
            'base_x=1 base_y=3 base_distance_pair_count=1 base_distance_min_squared=25',
        ]
        rows = {row['house']: row for row in features.rows_from_log(lines, self.game)}
        self.assertEqual(rows['']['map_tile_count'], 64)
        self.assertEqual(rows['']['initial_spice_tiles'], 1)
        self.assertEqual(rows['']['mountain_tiles'], 3)
        self.assertEqual(rows['']['initial_spice_density'], 0.015624999)
        self.assertEqual(rows['']['base_distance_min_squared'], 25)
        self.assertEqual(rows['']['base_start_coordinates'], '0:4:7;4:1:3')
        self.assertEqual(rows['4']['base_start_coordinates'], '0:4:7;4:1:3')
        self.assertEqual(rows['4']['base_distance_squared_pairs'], '0:4:25')
        self.assertEqual(rows['4']['base_x'], 1)  # Runtime house, not candidate/roster index.
        with self.assertRaisesRegex(ValueError, 'integer start-coordinate'):
            features.rows_from_log([lines[0].replace('distance_squared=25', 'distance_squared=26')], self.game)

    def test_legacy_rock_and_key_start_receipts_supply_exact_counts(self):
        lines = [
            'DUNECITY_ROCK: minute=0 cycle=0 house=0 rock_initial_area=25 rock_initial_free=21 '
            'rock_area=25 rock_free=21 rock_in_range=12 rock_committed=0',
            'DUNECITY_ROCK_KEY: minute=0 cycle=0 house=0 name=palace origins=5 range_origins=3',
            'DUNECITY_ROCK_KEY: minute=0 cycle=0 house=0 name=starport origins=5 range_origins=3',
            'DUNECITY_ROCK_KEY: minute=0 cycle=0 house=0 name=heavy origins=8 range_origins=6',
            'DUNECITY_ROCK_KEY: minute=0 cycle=0 house=0 name=ix origins=11 range_origins=7',
            'DUNECITY_ROCK: minute=1 cycle=3750 house=0 rock_area=3 rock_free=0',
        ]
        row, = features.rows_from_log(lines, self.game)
        self.assertEqual(row['rock_initial_free'], 21)
        self.assertEqual(row['free3x3_count'], 5)
        self.assertEqual(row['in_range3x3_count'], 3)
        self.assertEqual(row['free3x2_count'], 8)
        self.assertEqual(row['free2x2_count'], 11)
        self.assertEqual(row['largest_component_free3x3'], '')
        self.assertEqual(row['initial_spice_density'], '')
        self.assertEqual(row['measurement_status'], 'engine_start_partial')

    def test_receipt_hash_ignores_logger_prefix_not_measurement_text(self):
        marker = 'DUNECITY_MAP_FEATURE: cycle=0 house=-1 width=5 height=5'
        first, = features.rows_from_log(['2026 INFO ' + marker], self.game)
        second, = features.rows_from_log([marker], self.game)
        self.assertEqual(first['evidence_sha256'], second['evidence_sha256'])
        self.assertEqual(first['evidence_sha256'], hashlib.sha256((marker + '\n').encode()).hexdigest())

    def test_conflicting_same_shape_key_receipts_are_not_silently_selected(self):
        lines = ['DUNECITY_ROCK_KEY: cycle=0 house=0 name=palace origins=5',
                 'DUNECITY_ROCK_KEY: cycle=0 house=0 name=starport origins=6']
        with self.assertRaisesRegex(ValueError, 'Conflicting free3x3_count'):
            features.rows_from_log(lines, self.game)

    def test_export_chunks_reassemble_in_numeric_order_and_accept_identical_duplicates(self):
        data = bytes(range(256)) * 2
        rows = export_rows('fixture', 'map.ini', data)
        decoded = features.decode_export_rows(list(reversed(rows)) + [rows[0]])
        self.assertEqual(decoded[('fixture', 'map.ini')], data)

    def test_export_corruption_rejected_before_measurement(self):
        valid = export_rows('fixture', 'map.ini', b'abcdefghijklmnop', chunk_size=5)
        cases = []
        cases.append(valid[:-1])
        cases.append([dict(row, sha256='0' * 64) for row in valid])
        cases.append([dict(row, bytes=999) for row in valid])
        cases.append([dict(valid[0], base64='not base64!'), *valid[1:]])
        cases.append([*valid, dict(valid[0], base64=base64.b64encode(b'other').decode())])
        cases.append([dict(valid[0], chunks=0), *valid[1:]])
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                features.decode_export_rows(rows)

    def test_engine_map_hash_is_checked_independently_of_export_sha(self):
        data = map_ini(['%%%%%', '%%%%%', '%%%%%', '%%%%%', '%%%%%'])
        with self.assertRaisesRegex(ValueError, 'map hash mismatch'):
            features.rows_from_map(data, self.game, dict(map_hash='0' * 16, map_hash_algorithm='fnv1a64'))

    def test_overlapping_origins_are_not_disjoint_capacity(self):
        data = map_ini(['%%%%%'] * 5, units='ID0001=Harkonnen,MCV,256,0,64,Area Guard')
        global_row, house_row = features.rows_from_map(data, self.game)
        self.assertEqual(global_row['rock_area'], 25)
        self.assertEqual(global_row['rock_components'], 1)
        self.assertEqual(global_row['free3x3_count'], 9)  # Not floor(5/3)**2.
        self.assertEqual(global_row['largest_component_free3x3'], 9)
        self.assertEqual(house_row['free3x3_count'], 9)  # MCV traffic is ignored.
        self.assertEqual(global_row['initial_spice_amount'], '')
        self.assertEqual(global_row['rock_committed'], '')

    def test_occupied_yard_does_not_cut_connected_land(self):
        data = map_ini(['%%%%%'] * 3, structures='ID0001=Harkonnen,Const Yard,256,1')
        global_row, house_row = features.rows_from_map(data, self.game)
        self.assertEqual(house_row['rock_area'], 15)
        self.assertEqual(house_row['rock_components'], 1)
        self.assertEqual(house_row['rock_free'], 11)
        self.assertEqual(global_row['free2x2_count'], 2)
        self.assertEqual(global_row['free3x3_count'], 0)

    def test_diagonal_tiles_are_separate_and_largest_ties_are_row_major(self):
        data = map_ini(['%--', '-%-', '--%'])
        global_row, _ = features.rows_from_map(data, self.game)
        self.assertEqual(global_row['rock_area'], 3)
        self.assertEqual(global_row['rock_components'], 3)
        self.assertEqual(global_row['largest_rock_area'], 1)
        self.assertEqual(global_row['largest_component_free3x3'], 0)

    def test_spice_blooms_and_mountains_are_not_buildable_rock(self):
        data = map_ini(['~+O@@', 'gGB%%'])
        global_row, _ = features.rows_from_map(data, self.game)
        self.assertEqual(global_row['initial_spice_density'], 0.4)
        self.assertEqual(global_row['mountain_density'], 0.2)
        self.assertEqual(global_row['rock_area'], 2)

    def test_unknown_footprints_leave_free_geometry_unknown(self):
        data = map_ini(['%%%%%'] * 5, structures='ID0001=Harkonnen,Unknown Building,256,0')
        global_row, _ = features.rows_from_map(data, self.game)
        self.assertEqual(global_row['rock_area'], 25)
        self.assertEqual(global_row['free3x3_count'], '')
        self.assertEqual(global_row['measurement_status'], 'map_export_partial')

    def test_predeploy_exports_never_invent_starting_yard_free_land(self):
        data = map_ini(['%%%%%'] * 5, units='ID0001=Harkonnen,MCV,256,0,64,Area Guard')
        game = dict(self.game, base='cy')
        global_row, house_row = features.rows_from_map(data, game)
        self.assertEqual(global_row['rock_area'], 25)
        for field in ('free3x3_count', 'rock_free', 'largest_component_free3x3', 'rock_initial_free'):
            self.assertEqual(global_row[field], '')
            self.assertEqual(house_row[field], '')
        self.assertEqual(global_row['measurement_status'], 'map_export_predeploy')

    def test_player_sections_are_bound_from_receipts_not_roster_position(self):
        data = map_ini(['%%%%%'] * 5, players=('Player1', 'Player2'),
                       units='ID0001=Player1,MCV,256,0,64,Area Guard\n'
                             'ID0002=Player2,MCV,256,24,64,Area Guard')
        game = dict(self.game, sides=[['harkonnen', 1, 'hard'], ['sardaukar', 2, 'hard']])
        unbound = features.rows_from_map(data, game)
        self.assertEqual(unbound[1]['base_x'], '')
        setup = features.setup_from_log([
            'INIMapLoader: player setup resolvedHouse=4 section=player1',
            'INIMapLoader: player setup resolvedHouse=0 section=player2'])
        rows = {row['house']: row for row in features.rows_from_map(data, game, setup)}
        self.assertEqual(rows['4']['base_x'], 0)
        self.assertEqual(rows['0']['base_x'], 4)
        self.assertAlmostEqual(rows['']['base_distance_min'], 32 ** 0.5)
        self.assertAlmostEqual(rows['0']['base_distance_mean'], 32 ** 0.5)

    def test_missing_terrain_rows_are_unknown_not_sand_padding(self):
        data = b'[BASIC]\nVersion=2\n[MAP]\nSizeX=5\nSizeY=2\n000=%%%%%\n'
        row, = features.rows_from_map(data, self.game)
        self.assertEqual(row['rock_area'], '')
        self.assertEqual(row['measurement_status'], 'map_export_partial')

    def test_collected_gzip_exports_samples_and_archived_legacy_logs_work_together(self):
        game = dict(self.game, base='cy')
        data = map_ini(['%%%%%'] * 5, units='ID0001=Player1,MCV,256,0,64,Area Guard', players=('Player1',))
        map_hash = features._fnv1a64(data)
        log = ('INIMapLoader: player setup resolvedHouse=0 section=player1\n'
               f'SCENARIO_SETUP: cycle=0 map_hash={map_hash} map_hash_algorithm=fnv1a64\n'
               'DUNECITY_ROCK: cycle=0 house=0 rock_area=25 rock_initial_area=25 '
               'rock_free=21 rock_initial_free=21 rock_in_range=12 rock_committed=0\n'
               'DUNECITY_ROCK_KEY: cycle=0 house=0 name=palace origins=5 range_origins=3\n').encode()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            table(root / 'games.csv', [dict(id=game['id'], size=5, base='cy', sides='harkonnen:1:hard', map_hash=map_hash)])
            table(root / 'export-maps.csv.gz', export_rows(game['id'], 'map.ini', data))
            table(root / 'samples.csv.gz', [dict(id=game['id'], house=0, cycle=0, rock_area=25,
                                                  rock_initial_area=25, rock_initial_free=21, rock_free=21)])
            token = 'g' + hashlib.sha256(game['id'].encode()).hexdigest()[:12]
            with tarfile.open(root / 'probe-evidence.tar.gz', 'w:gz') as archive:
                member = tarfile.TarInfo(f'scratch/{token}/run.log')
                member.size = len(log)
                archive.addfile(member, io.BytesIO(log))
            rows = {row['house']: row for row in features.collect_results([root])}
        self.assertEqual(rows['']['rock_components'], 1)
        self.assertEqual(rows['0']['rock_initial_free'], 21)
        self.assertEqual(rows['0']['free3x3_count'], 5)
        self.assertEqual(rows['0']['in_range3x3_count'], 3)
        self.assertEqual(rows['0']['rock_components'], 1)
        self.assertIn('engine_start_partial+map_export_predeploy', rows['0']['measurement_status'])
        self.assertEqual(rows['0']['map_hash'], map_hash)
        self.assertEqual(len(rows['0']['evidence_sha256']), 64)

    def test_invalid_exports_do_not_truncate_requested_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / 'features.csv'
            output.write_text('existing evidence\n')
            rows = export_rows('fixture', 'map.ini', b'invalid map')
            rows[0]['sha256'] = '0' * 64
            table(root / 'export-maps.csv', rows)
            with self.assertRaises(ValueError):
                features.main(['--results', str(root), '--output', str(output)])
            self.assertEqual(output.read_text(), 'existing evidence\n')


if __name__ == '__main__':
    unittest.main()
