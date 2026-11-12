#!/usr/bin/env python3
"""Historical decisiveness counts on one deck; invalid inputs are rejected."""
import contextlib
import csv
import gzip
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('ai_baseline_gate', Path(__file__).with_name('ai-baseline-gate.py'))
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)
FIELDS = ('id', 'mod', 'difficulty', 'seed', 'research', 'map_sha256', 'options_sha256', 'source_commit',
          'baseline_release_source', 'outcome', 'error')
CURRENT = 'c' * 40


def deck(outcomes, source=CURRENT, release=CURRENT, **changes):
    return [dict(id=f'vanilla/matrix/1v1/{n}', mod='vanilla', difficulty='hard', seed=str(9101 + n),
                 research='off', map_sha256=f'map{n}', options_sha256='options', source_commit=source,
                 baseline_release_source=release, outcome=outcome, error='', **changes)
            for n, outcome in enumerate(outcomes)]


def history(outcomes):
    return deck(outcomes, source='b' * 40, release=gate.RELEASE_807)


def observation_deck():
    """Synthetic canonical win summaries on the committed 807 deck, not historical receipts."""
    manifest = json.loads(gate.FIXTURE_MANIFEST.read_text())
    selected = sorted((game for game in manifest['games'] if game.get('research', 'off') == 'off'
                       and game['seed'] in gate.SEEDS_807), key=lambda game: game['id'])
    observations, current = [], []
    for index, game in enumerate(selected):
        fields = dict(id=game['id'], mod=game['mod'], difficulty=game['difficulty'], seed=str(game['seed']),
                      research='off', map_sha256=game['map_sha256'],
                      options_sha256=manifest['off_options_by_mod'][game['mod']]['sha256'])
        current.append(dict(fields, source_commit=CURRENT, baseline_release_source=CURRENT,
                            outcome='time_limit', error=''))
        for house, team in game['roster']:
            observations.append(dict(fields, schema_version='1', release='1.0.807',
                source_commit=gate.BRIDGE_807, baseline_release_source=gate.RELEASE_807, arm='off',
                house=str(house), team=str(team),
                context_sha256=gate.scorecard.context_sha256(
                    game['id'], game['mod'], game['difficulty'], 'off', game['seed']),
                content_hash='0' * 16, object_data_hash='1' * 16, metric='win', minute='-1',
                value=str(int(index < 22 and team == 1)), status='measured',
                reason='native_winner_team_zero_preserves_no_winner'))
    return observations, current


class BaselineCheckTest(unittest.TestCase):
    def check(self, candidate, off, reference):
        with tempfile.TemporaryDirectory() as tmp:
            arms = []
            for name, rows in (('on', candidate), ('off', off), ('807', reference)):
                path = Path(tmp) / name
                path.mkdir()
                with open(path / 'games.csv', 'w', newline='') as stream:
                    writer = csv.DictWriter(stream, FIELDS)
                    writer.writeheader()
                    writer.writerows(rows)
                arms.append(str(path))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = gate.main(['--candidate', arms[0], '--off', arms[1], '--reference', arms[2]])
            return code, out.getvalue().strip()

    def test_equal_decided_counts_pass_and_history_is_information_only(self):
        result = self.check(deck(['decided', 'time_limit']), deck(['time_limit', 'decided']),
                            history(['decided', 'decided']))
        self.assertEqual(0, result[0])

    def test_fewer_decided_games_than_off_fail(self):
        result = self.check(deck(['time_limit', 'time_limit']), deck(['decided', 'time_limit']),
                            history(['time_limit', 'time_limit']))
        self.assertEqual(1, result[0])

    def test_another_deck_is_invalid(self):
        moved = deck(['decided', 'decided'])
        moved[1]['map_sha256'] = 'other'
        code, line = self.check(moved, deck(['decided', 'decided']), history(['decided', 'decided']))
        self.assertEqual(2, code)
        self.assertIn('candidate vanilla/matrix/1v1/1: map_sha256 differs from 1.0.807', line)
        code, line = self.check(deck(['decided']), deck(['decided', 'decided']), history(['decided', 'decided']))
        self.assertEqual((2, 'INVALID: candidate did not play the 1.0.807 deck (1 games differ)'), (code, line))

    def test_failed_games_mixed_sources_and_wrong_reference_are_invalid(self):
        failed = deck(['decided', 'time_limit'])
        failed[0]['error'] = 'probe crashed'
        code, line = self.check(failed, deck(['decided', 'decided']), history(['decided', 'decided']))
        self.assertEqual(2, code)
        self.assertIn('candidate vanilla/matrix/1v1/0: probe crashed', line)
        code, line = self.check(deck(['decided'], source='d' * 40), deck(['decided']), history(['decided']))
        self.assertEqual(2, code)
        self.assertIn('one source commit', line)
        code, line = self.check(deck(['decided']), deck(['decided']), deck(['decided']))
        self.assertEqual(2, code)
        self.assertIn('1.0.807 vanilla/matrix/1v1/0: native source', line)

    def check_observations(self, observations=None, *, candidate=None, off=None, compressed=False, columns=None):
        canonical, current = observation_deck()
        self.assertEqual(192, len(current))
        observations = canonical if observations is None else observations
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for name, rows in (('on', current if candidate is None else candidate),
                               ('off', current if off is None else off)):
                path = Path(tmp) / name
                path.mkdir()
                with (path / 'games.csv').open('w', newline='') as stream:
                    writer = csv.DictWriter(stream, FIELDS)
                    writer.writeheader()
                    writer.writerows(rows)
                paths.append(str(path))
            reference = Path(tmp) / ('observations.csv.gz' if compressed else 'observations.csv')
            opener = gzip.open if compressed else open
            with opener(reference, 'wt', newline='') as stream:
                writer = csv.DictWriter(stream, columns or gate.scorecard.COLUMNS, extrasaction='ignore')
                writer.writeheader()
                writer.writerows(observations)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = gate.main(['--candidate', paths[0], '--off', paths[1],
                                  '--reference-observations', str(reference)])
            return code, out.getvalue().strip()

    def test_observation_reference_plain_and_gzip_keep_807_informational(self):
        for compressed in (False, True):
            with self.subTest(compressed=compressed):
                code, _ = self.check_observations(compressed=compressed)
                self.assertEqual(0, code)

    def test_observation_reference_preserves_candidate_off_threshold(self):
        _, off = observation_deck()
        off[0]['outcome'] = 'decided'
        code, _ = self.check_observations(off=off)
        self.assertEqual(1, code)

    def test_observation_reference_rejects_schema_source_hash_seed_and_arm_changes(self):
        changes = (('schema_version', '2'), ('release', '1.0.808'), ('arm', 'candidate'),
                   ('research', 'on'), ('baseline_release_source', 'a' * 40),
                   ('source_commit', 'd' * 40), ('map_sha256', 'f' * 64),
                   ('options_sha256', 'f' * 64), ('context_sha256', 'f' * 64), ('seed', '1150101'),
                   ('content_hash', ''), ('object_data_hash', 'not-a-hash'))
        for field, value in changes:
            with self.subTest(field=field):
                rows, _ = observation_deck()
                rows[0][field] = value
                code, line = self.check_observations(rows)
                self.assertEqual(2, code)
                self.assertTrue(line.startswith('INVALID:'), line)

    def test_observation_reference_rejects_partial_context_or_win_roster(self):
        rows, _ = observation_deck()
        without_context = [row for row in rows if row['id'] != rows[0]['id']]
        for partial in ([], rows[:-1], without_context):
            with self.subTest(rows=len(partial)):
                code, line = self.check_observations(partial)
                self.assertEqual(2, code)
                self.assertIn('INVALID:', line)

    def test_observation_reference_rejects_duplicate_or_conflicting_rows(self):
        for value in ('1', '0'):
            with self.subTest(value=value):
                rows, _ = observation_deck()
                rows.append(dict(rows[0], value=value))
                code, line = self.check_observations(rows)
                self.assertEqual(2, code)
                self.assertIn('duplicate record', line)

    def test_observation_reference_rejects_ambiguous_or_unmeasured_winners(self):
        rows, _ = observation_deck()
        for row in rows:
            if row['id'] == rows[0]['id']:
                row['value'] = '1'
        code, line = self.check_observations(rows)
        self.assertEqual(2, code)
        self.assertIn('ambiguous native winner', line)
        for field, value in (('status', 'unavailable'), ('value', '0.5'), ('minute', '5'), ('team', '9'),
                             ('reason', 'not_a_native_winner_metric')):
            with self.subTest(field=field):
                rows, _ = observation_deck()
                rows[0][field] = value
                code, line = self.check_observations(rows)
                self.assertEqual(2, code)
                self.assertIn('INVALID:', line)

    def test_observation_reference_rejects_missing_or_duplicate_csv_columns(self):
        for columns in (gate.scorecard.COLUMNS[1:], (*gate.scorecard.COLUMNS, 'id')):
            with self.subTest(columns=len(columns)):
                code, line = self.check_observations(columns=columns)
                self.assertEqual(2, code)
                self.assertIn('INVALID:', line)

    def test_observation_reference_keeps_raw_arm_validation(self):
        _, candidate = observation_deck()
        candidate[0]['error'] = 'fixture crash'
        code, line = self.check_observations(candidate=candidate)
        self.assertEqual(2, code)
        self.assertIn('fixture crash', line)
        _, candidate = observation_deck()
        candidate[0]['map_sha256'] = 'other'
        code, line = self.check_observations(candidate=candidate)
        self.assertEqual(2, code)
        self.assertIn('map_sha256 differs', line)
        _, off = observation_deck()
        off[0]['source_commit'] = 'd' * 40
        code, line = self.check_observations(off=off)
        self.assertEqual(2, code)
        self.assertIn('one source commit', line)

    def test_reference_inputs_are_mutually_exclusive(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
            gate.main(['--candidate', 'on', '--off', 'off', '--reference', 'raw',
                       '--reference-observations', 'observations.csv.gz'])
        self.assertEqual(2, result.exception.code)


if __name__ == '__main__':
    unittest.main()
