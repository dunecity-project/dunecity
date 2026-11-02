"""Reporting contract fixtures only: these are NOT simulated games or AI evidence.

Run directly with Python, unittest discovery, or pytest; no engine/dependencies needed.
"""
import contextlib
import csv
import io
from pathlib import Path
import statistics
import subprocess
import tempfile
import types
import unittest

from test_civ_wars_matrix import arguments, matrix
from source_provenance import source_fingerprint


def game_row(seed='7101', arm='off', swap='0', **extra):
    row = dict(id=f'fixture/{seed}/{swap}/{arm}', attempt='chosen', seed=str(seed), swap=str(swap),
               pair_id=f'fixture/{seed}/{swap}', format='1v1', team_size='1', pair='hard:medium',
               size='0', worms='1', spice='2', minutes='30', options_input_sha256='input',
               source_commit='fixture-source', working_tree_modified='False', options_hash='options',
               map_hash=f'map-{seed}', arm=arm, focal_team='1', left_score='1', right_score='-1',
               outcome='won', winner_team='1', end_s='600', telemetry_status='complete', error='',
               worm_losses_complete='1', observed_worm_losses='0')
    row.update(extra)
    return row


def write_rows(directory, name, rows):
    rows = list(rows)
    if not rows:
        return
    fields = tuple(dict.fromkeys(key for row in rows for key in row))
    matrix['append'](Path(directory) / name, fields, rows)


class CivWarsReportTests(unittest.TestCase):
    def test_record_parser_preserves_negative_timing_and_values(self):
        found = matrix['records']([
            'noise without a marker',
            'INFO CIV_WARS_HOUSE: house=1 tech2_s=-1.000000 tech3_s=120.0 ignored token key=a=b',
            'CIV_WARS_RESEARCH: house=1 event=start target=2 cycle=1875',
            'PROBE_DIGEST: final=1 digest=abc',
        ])
        self.assertEqual(found['CIV_WARS_HOUSE'][0]['tech2_s'], '-1.000000')
        self.assertEqual(found['CIV_WARS_HOUSE'][0]['key'], 'a=b')
        self.assertEqual(found['CIV_WARS_RESEARCH'][0]['event'], 'start')
        for value in ('nan', 'inf', '-inf', '', None):
            self.assertIsNone(matrix['number'](value))
        self.assertEqual(matrix['integer']('31.0'), 31)
        self.assertIsNone(matrix['integer']('31.5'))

    def test_all_harvester_families_are_not_combat(self):
        for item in (31, 59, 85, 86):
            self.assertEqual(matrix['loss_kind'](str(item)), 'harvester')
        for item in (28, 32, 33, 40, 41, 63, 64, 74, 75, 77, 82, 83):
            self.assertEqual(matrix['loss_kind'](str(item)), 'combat')
        for item in (27, 30, 34, 38, 48, 50, 51, 57, 66, 76, 87, 88, 'unknown'):
            self.assertEqual(matrix['loss_kind'](item), 'other')

    def test_worm_categories_exactness_and_partial_join(self):
        items = [dict(event='worm_loss', house='1', team='1', object=str(index), item=str(item),
                      observed_count='1', complete='1')
                 for index, item in enumerate((31, 59, 85, 86, 41, 34), 1)]
        items.append(dict(event='worm_loss', house='2', team='2', object='8', item='41',
                          observed_count='1', complete='1'))
        game = game_row(observed_worm_losses='7')
        observed, exact = matrix['worm_loss_counts'](game, items)
        self.assertEqual(exact, dict(harvester=4, combat=1, other=1))
        self.assertEqual(observed, exact)
        self.assertIsNone(matrix['worm_loss_counts'](game, items[:-1])[1])
        self.assertIsNone(matrix['worm_loss_counts'](dict(game, worm_losses_complete='0'), items)[1])
        self.assertIsNone(matrix['worm_loss_counts'](game, [dict(item, complete='0') for item in items])[1])
        self.assertEqual(matrix['worm_loss_counts'](game_row(), [])[1], dict(harvester=0, combat=0, other=0))
        self.assertIsNone(matrix['worm_loss_counts'](dict(game, observed_worm_losses=''), items)[1])

    def test_tech_unreached_is_not_a_timing_missing_is_not_unreached(self):
        games = [game_row(str(index), team_size='2') for index in range(5)]
        houses = {
            games[0]['id']: [dict(team='1', tech2_s='-1.000000'), dict(team='1', tech2_s='-1')],
            games[1]['id']: [dict(team='1', tech2_s='120'), dict(team='1', tech2_s='300')],
            games[2]['id']: [dict(team='1', tech2_s=''), dict(team='1', tech2_s='-1')],
            games[3]['id']: [dict(team='1', tech2_s='0'), dict(team='1', tech2_s='')],
            games[4]['id']: [dict(team='2', tech2_s='60')],
        }
        timings, denominator = matrix['tech_reach'](games, houses, 2)
        self.assertEqual(timings, [2, 0])
        self.assertEqual(statistics.median(timings), 1)
        self.assertEqual(denominator, 3)  # Known unreached stays; two unknown games do not.
        self.assertEqual(matrix['tech_reach'](games, houses, 9), ([], 0))

    def test_orientations_are_one_seed_trial_and_right_uses_right_baseline(self):
        rows = [game_row(arm='off', left_score='-1'), game_row(arm='on', left_score='1'),
                game_row(arm='off', swap='1', left_score='1'),
                game_row(arm='on', swap='1', left_score='-1')]
        matched, seeds = matrix['paired_scores'](rows, 'on')
        self.assertEqual(matched, 2)
        self.assertEqual(seeds, {'7101': (0, 0)})
        rows = [game_row(arm='off', right_score='-1'),
                game_row(arm='right', focal_team='2', right_score='1')]
        self.assertEqual(matrix['paired_scores'](rows, 'right'), (1, {'7101': (1, -1)}))

    def test_pairing_rejects_source_options_map_orientation_and_ambiguity(self):
        control = game_row()
        for extra in (dict(source_commit='other'), dict(source_commit=''), dict(map_hash='other'),
                      dict(options_hash='other'), dict(options_input_sha256='other'),
                      dict(working_tree_modified='True'), dict(working_tree_modified=''),
                      dict(swap='1'), dict(pair='brutal:hard'), dict(minutes='40'), dict(size='1')):
            with self.subTest(extra=extra):
                self.assertEqual(matrix['paired_scores']([control, game_row(arm='on', **extra)], 'on'), (0, {}))
        rows = [control, game_row(arm='on'), game_row(arm='on', map_hash='another-map')]
        self.assertEqual(matrix['paired_scores'](rows, 'on'), (0, {}))
        self.assertEqual(matrix['paired_scores']([dict(control, map_hash=''), game_row(arm='on', map_hash='')], 'on'), (0, {}))

    def test_csv_schema_migration_retains_old_columns_and_aligns_appends(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'games.csv'
            write_rows(temporary, 'games.csv', [dict(id='legacy', legacy_extra='kept')])
            matrix['append'](path, ('id', 'attempt', 'new_field'),
                             [dict(id='new', attempt='a', new_field='new')])
            matrix['append'](path, ('id', 'attempt', 'new_field'),
                             [dict(id='newer', attempt='b', new_field='later')])
            rows = matrix['read'](path)
            self.assertEqual(rows[0], dict(id='legacy', attempt='', new_field='', legacy_extra='kept'))
            self.assertEqual(rows[1], dict(id='new', attempt='a', new_field='new', legacy_extra=''))
            self.assertEqual(rows[2]['new_field'], 'later')
            self.assertTrue(all(None not in row for row in rows))
            with path.open(newline='') as handle:
                self.assertTrue(all(len(row) == 4 for row in csv.reader(handle)))

    def test_retry_attempt_join_and_successful_id_resume_are_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            game = game_row()
            write_rows(temporary, 'games.csv', [dict(game, attempt='partial', error='failed'), game,
                                                dict(game, attempt='late-failure', error='failed')])
            write_rows(temporary, 'houses.csv', [dict(id=game['id'], attempt=attempt, house='1', team='1',
                                                       tech2_s=timing)
                                                 for attempt, timing in (('partial', '1'), ('chosen', '120'),
                                                                         ('late-failure', '2'), ('orphan', '3'))])
            for name in ('research.csv', 'items.csv', 'waves.csv', 'perf.csv', 'worms.csv', 'decisions.csv'):
                write_rows(temporary, name, [dict(id=game['id'], attempt='orphan', event='wrong'),
                                             dict(id=game['id'], attempt='chosen', event='retained')])
            self.assertEqual(matrix['completed'](temporary)[game['id']]['attempt'], 'chosen')
            games, tables = matrix['load_report']([temporary])
            self.assertEqual(len(games), 1)
            key = games[0]['_report_key']
            self.assertEqual(tables['houses.csv'][key][0]['tech2_s'], '120')
            for name in ('research.csv', 'items.csv', 'waves.csv', 'perf.csv', 'worms.csv', 'decisions.csv'):
                self.assertEqual([row['event'] for row in tables[name][key]], ['retained'])

    def test_same_id_different_commits_are_retained_with_their_own_rows(self):
        with tempfile.TemporaryDirectory() as temporary:
            directories = [Path(temporary) / str(index) for index in range(2)]
            for index, directory in enumerate(directories):
                directory.mkdir()
                rows = [game_row(source_commit=f'source-{index}', attempt=f'attempt-{index}')]
                if index == 0:
                    rows.insert(0, game_row(source_commit='even-earlier', attempt='earlier'))
                write_rows(directory, 'games.csv', rows)
                write_rows(directory, 'houses.csv', [dict(id=game['id'], attempt=game['attempt'], house='1',
                                                          team='1', tech2_s=game['source_commit']) for game in rows])
            games, tables = matrix['load_report'](directories)
            self.assertEqual(len(games), 3)
            for game in games:
                self.assertEqual(tables['houses.csv'][game['_report_key']][0]['tech2_s'], game['source_commit'])

    def test_measured_fingerprints_identify_dirty_snapshots_without_backfilling_legacy_runs(self):
        fingerprint = 'a' * 64
        control = game_row(source_fingerprint=fingerprint, working_tree_modified='True')
        candidate = game_row(arm='on', source_fingerprint=fingerprint, working_tree_modified='True')
        self.assertEqual(matrix['paired_scores']([control, candidate], 'on'), (1, {'7101': (1, 1)}))
        # Generated-output dirtiness is irrelevant when actual source bytes are identical.
        clean_candidate = dict(candidate, working_tree_modified='False')
        self.assertEqual(matrix['paired_scores']([control, clean_candidate], 'on'), (1, {'7101': (1, 1)}))
        for extra in (dict(source_fingerprint='b' * 64), dict(source_fingerprint=''),
                      dict(source_fingerprint='not-a-sha256'), dict(source_fingerprint_error='missing input'),
                      dict(source_commit='another-commit')):
            with self.subTest(extra=extra):
                self.assertEqual(matrix['paired_scores']([control, dict(candidate, **extra)], 'on'), (0, {}))
        self.assertFalse(matrix['source_identified'](game_row(working_tree_modified='True')))
        self.assertFalse(matrix['source_identified'](game_row(source_fingerprint_error='failed')))
        self.assertTrue(matrix['source_identified'](game_row()))


    def test_legacy_missing_attempt_and_timing_columns_load_without_inventing_reach(self):
        with tempfile.TemporaryDirectory() as temporary:
            game = game_row()
            del game['attempt']
            write_rows(temporary, 'games.csv', [game])
            write_rows(temporary, 'houses.csv', [dict(id=game['id'], house='1', team='1')])
            games, tables = matrix['load_report']([temporary])
            self.assertEqual(len(matrix['focal_houses'](games[0], tables['houses.csv'])), 1)
            self.assertEqual(matrix['tech_reach'](games, tables['houses.csv'], 2), ([], 0))
            self.assertFalse(games[0]['_research_available'])

    def test_report_summarizes_all_formats_pairs_fixed_seeds_without_engine(self):
        # A formatting/aggregation fixture, never evidence of strategy performance.
        with tempfile.TemporaryDirectory() as temporary:
            games = matrix['schedule'](arguments('--both-sides'))
            rows, houses, research = [], [], []
            for game in games:
                row = game_row(**{key: str(value) for key, value in game.items()})
                row.update(outcome='time_limit', winner_team='0', left_score='-0.25', right_score='-0.25', end_s='1800')
                rows.append(row)
                for team in ('1', '2'):
                    for member in range(game['team_size']):
                        house = f'{team}-{member}'
                        houses.append(dict(id=row['id'], attempt='chosen', house=house, team=team,
                                           tech='1', islands='0', attacks='0', worm_losses='0',
                                           **{f'tech{level}_s': '-1.000000' for level in range(2, 10)}))
                research.append(dict(id=row['id'], attempt='chosen', team=str(game['focal_team']), house='1-0',
                                     event='start', target='2', cycle='3750'))
            write_rows(temporary, 'games.csv', rows)
            write_rows(temporary, 'houses.csv', houses)
            write_rows(temporary, 'research.csv', research)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                matrix['report'](types.SimpleNamespace(report=[temporary], results=None))
            text = output.getvalue()
            self.assertIn('600 complete games, 0 failed games', text)
            for team_size in (1, 2, 3):
                for pair in matrix['PAIRS']:
                    self.assertIn(f'## {team_size}v{team_size} {pair};', text)
            self.assertIn('10 (5)', text)  # Ten orientations but only five seeds.
            self.assertIn('10 / 5', text)
            self.assertIn('100.0%', text)  # Stalemates/time-outs, not material wins.
            self.assertIn('1.0 (10/10)', text)  # Actual research start at 3750 * 16ms.
            self.assertIn('- (0/10)', text)  # No negative tech medians.
            self.assertIn('not distinguished', text)
            self.assertIn('Observed lower-bound', text)
            self.assertNotIn('beats off', text)


class SourceFingerprintTests(unittest.TestCase):
    """Hash real temporary source files; never launch a simulation or fabricate evidence."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / 'actual-build'
        self.output = self.root / 'actual-output'
        self.build.mkdir()
        self.output.mkdir()
        subprocess.run(['git', 'init', '-q'], cwd=self.root, check=True, capture_output=True)
        self.source = self.root / 'tracked.cpp'
        self.source.write_text('int source = 1;\n', encoding='utf-8')
        subprocess.run(['git', 'add', 'tracked.cpp'], cwd=self.root, check=True, capture_output=True)

    def fingerprint(self):
        return source_fingerprint(self.root, self.build, self.output)

    def test_tracked_and_nonignored_untracked_source_mutations_change_identity(self):
        baseline = self.fingerprint()
        self.assertEqual(len(baseline), 64)
        self.assertEqual(self.fingerprint(), baseline)
        self.source.write_text('int source = 2;\n', encoding='utf-8')
        changed = self.fingerprint()
        self.assertNotEqual(changed, baseline)
        extra = self.root / 'untracked.inc'
        extra.write_text('actual untracked source\n', encoding='utf-8')
        untracked = self.fingerprint()
        self.assertNotEqual(untracked, changed)
        extra.write_text('different untracked source\n', encoding='utf-8')
        self.assertNotEqual(self.fingerprint(), untracked)

    def test_actual_build_output_and_cache_trees_are_excluded(self):
        baseline = self.fingerprint()
        for directory in (self.build, self.output, self.root / '__pycache__', self.root / 'node_modules',
                          self.root / '.pytest_cache', self.root / 'nested/__pycache__'):
            directory.mkdir(parents=True, exist_ok=True)
            artifact = directory / 'generated.bin'
            artifact.write_bytes(b'generated output')
            self.assertEqual(self.fingerprint(), baseline)
            artifact.write_bytes(b'changed generated output')
            self.assertEqual(self.fingerprint(), baseline)
        other = self.root / 'other-build'
        other.mkdir()
        (other / 'input.txt').write_text('not this invocation\n', encoding='utf-8')
        self.assertNotEqual(self.fingerprint(), baseline)

    def test_ignored_files_are_not_inputs(self):
        (self.root / '.gitignore').write_text('ignored/\n', encoding='utf-8')
        baseline = self.fingerprint()
        ignored = self.root / 'ignored'
        ignored.mkdir()
        (ignored / 'artifact').write_text('ignored\n', encoding='utf-8')
        self.assertEqual(self.fingerprint(), baseline)

    def test_missing_or_nonfile_tracked_inputs_fail_closed(self):
        self.source.unlink()
        with self.assertRaisesRegex(RuntimeError, 'tracked.cpp'):
            self.fingerprint()
        self.source.mkdir()
        with self.assertRaisesRegex(RuntimeError, 'tracked.cpp'):
            self.fingerprint()


if __name__ == '__main__':
    unittest.main()
