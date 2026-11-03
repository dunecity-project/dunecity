"""Pure-Python Civ Wars scheduling/capture tests; never build or run the game."""
import ast
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch


HARNESS = Path(__file__).with_name('civ-wars-matrix.py')
# Match dunecity-queue's loader, including its literal top-level args boundary.
tree = ast.parse(HARNESS.read_text(encoding='utf-8'), str(HARNESS))
prefix = []
for node in tree.body:
    if (isinstance(node, ast.Assign) and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
        break
    prefix.append(node)
else:
    raise AssertionError('missing queue AST boundary')
matrix = {'__file__': str(HARNESS), '__name__': 'cluster_harness'}
exec(compile(ast.Module(body=prefix, type_ignores=[]), str(HARNESS), 'exec'), matrix)


def arguments(*argv):
    return matrix['parser'].parse_args(['--build-dir', 'unused-build', '--results', 'unused-results', *argv])


def capture_events(limited=False):
    events = [
        ('session_start', -1, {'seed': 7101}),
        ('ground_hunt', 1, {'members': 0, 'value': 0}),
        ('ground_hunt', 1, {'members': 3, 'value': 900}),
        ('lethal_damage', 1, {'object': 17, 'item': 31, 'attacker': 9}),
        ('lethal_damage', 1, {'object': 18, 'item': 31, 'attacker': 99}),
        ('performance_window', -1, {'dropped_samples': 0, 'start_cycle': 0, 'end_cycle': 60,
                                   'metrics': {'0': {'scope': 'frame.ai', 'house': -1, 'item': -1,
                                                     'unit': 'us', 'count': 5, 'sum': 1234, 'max': 500}}}),
    ]
    if limited:
        events.append(('capture_limit', -1, {'byte_limit': 268435456, 'terminal_events_retained': 1}))
    events.extend([
        ('game_summary', -1, {'houses': {
            '1': {'team': 1, 'actual': {'31': 2}, 'built': {'31': 3}, 'lost': {'31': 1}},
            '2': {'team': 2, 'actual': {'31': 3}, 'built': {'31': 3}, 'lost': {}},
        }}),
        ('session_end', -1, {}),
    ])
    return [dict(session='fixture', seq=index, cycle=0 if index == 1 else 60,
                 house=house, player=-1, event=event, data=data)
            for index, (event, house, data) in enumerate(events, 1)]


def write_capture(out, events):
    folder = out / 'profile/ai-decisions/fixture'
    folder.mkdir(parents=True)
    (folder / 'events.jsonl').write_text(''.join(json.dumps(event) + '\n' for event in events), encoding='utf-8')


class CivWarsMatrixTests(unittest.TestCase):
    def test_preparation_defaults_and_unique_schedule(self):
        for name in ('parser', 'schedule', 'play', 'build_probe', 'read', 'scratch_dir', 'finish_game'):
            self.assertIn(name, matrix)
        games = matrix['schedule'](arguments())
        self.assertEqual(len(games), 300)
        self.assertEqual(len({game['id'] for game in games}), 300)
        self.assertEqual({game['seed'] for game in games}, {7101, 7202, 7303, 7404, 7505})
        self.assertEqual({game['team_size'] for game in games}, {1, 2, 3})

    def test_side_swaps_move_strategy_with_controllers(self):
        args = arguments('--team-sizes', '2', '--pairs', 'hard:medium', '--seeds', '7101', '--both-sides')
        games = matrix['schedule'](args)
        self.assertEqual(len(games), 8)
        swapped = next(game for game in games if game['swap'] and game['arm'] == 'left')
        self.assertEqual((swapped['left'], swapped['right'], swapped['strategy'], swapped['focal_team']),
                         ('medium', 'hard', 'right', 2))
        command = matrix['command'](args, Path('probe'), swapped, Path('scratch/game'))
        self.assertEqual(command[command.index('--mod') + 1], 'CivWars')
        self.assertEqual(command[command.index('--civ-strategy') + 1], 'right')
        for forbidden in ('--scenario', '--custom-map', '--scenario-sides', '--roster', '--no-ai-telemetry'):
            self.assertNotIn(forbidden, command)

    def test_research_baseline_is_frozen_in_identity_and_forwarded(self):
        flags = ['--team-sizes', '1', '--pairs', 'hard:hard', '--arms', 'off', '--seeds', '138083612']
        normal_args = arguments(*flags)
        baseline_args = arguments(*flags, '--research-baseline')
        normal = matrix['schedule'](normal_args)[0]
        baseline = matrix['schedule'](baseline_args)[0]
        self.assertNotEqual(normal['id'], baseline['id'])
        self.assertNotEqual(normal['pair_id'], baseline['pair_id'])
        self.assertFalse(normal['research_baseline'])
        self.assertTrue(baseline['research_baseline'])
        # Unit dispatch may reconstruct args separately; the frozen game owns this flag.
        normal_command = matrix['command'](normal_args, Path('probe'), normal, Path('scratch/normal'))
        baseline_command = matrix['command'](normal_args, Path('probe'), baseline, Path('scratch/reference'))
        self.assertNotIn('--civ-research-baseline', normal_command)
        self.assertIn('--civ-research-baseline', baseline_command)

    def test_transport_path_does_not_change_identity_but_bytes_do(self):
        with tempfile.TemporaryDirectory() as temporary:
            first, second = Path(temporary) / 'a.ini', Path(temporary) / 'b.ini'
            first.write_bytes(b'[Game Options]\nFog of War = false\n')
            second.write_bytes(first.read_bytes())
            a = matrix['schedule'](arguments('--game-options', str(first)))
            b = matrix['schedule'](arguments('--game-options', str(second)))
            self.assertEqual([game['id'] for game in a], [game['id'] for game in b])
            second.write_bytes(b'[Game Options]\nFog of War = true\n')
            changed = matrix['schedule'](arguments('--game-options', str(second)))
            self.assertNotEqual(a[0]['id'], changed[0]['id'])

    def test_team_scoring_not_independent_houses(self):
        houses = [dict(house='0', team='1', alive='0', eliminated_s='10'),
                  dict(house='1', team='1', alive='0', eliminated_s='30'),
                  dict(house='2', team='2', alive='1', eliminated_s='-1'),
                  dict(house='3', team='2', alive='0', eliminated_s='20')]
        scores = matrix['score_houses'](houses)
        self.assertEqual([scores[h].score for h in ('0', '1', '2', '3')], [-1, -1, 1, 1])

    def test_complete_capture_matches_worm_attacker_and_nonempty_launches(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            write_capture(out, capture_events())
            capture = matrix['telemetry'](out, 60, [{'house': '1', 'team': '1'}, {'house': '2', 'team': '2'}], {'9'})
            self.assertTrue(capture['complete'])
            self.assertTrue(capture['performance_complete'])
            self.assertEqual(capture['attacks'], {'1': 1})
            self.assertEqual(capture['losses'], {'1': 1})
            self.assertEqual(len(capture['waves']), 1)
            final = [item for item in capture['items'] if item['event'] == 'final_counts' and str(item['house']) == '2']
            self.assertEqual(final[0]['lost'], 0)  # Sparse final ledgers explicitly omit zero entries.

    def test_short_and_gapped_capture_are_not_exact(self):
        for kind in ('short', 'gap', 'missing_end'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                events = capture_events()
                if kind == 'short':
                    events[-1]['cycle'] = 59
                elif kind == 'gap':
                    events[3]['seq'] = 40
                else:
                    events.pop()
                out = Path(temporary)
                write_capture(out, events)
                capture = matrix['telemetry'](out, 60, [{'house': '1', 'team': '1'}, {'house': '2', 'team': '2'}], {'9'})
                self.assertFalse(capture['complete'])
                self.assertFalse(capture['performance_complete'])

    def test_limited_play_keeps_lower_bounds_and_commits_games_last(self):
        self.exercise_play(limited=True, returncode=0)

    def test_failed_play_preserves_failed_scratch(self):
        self.exercise_play(limited=False, returncode=1)

    def exercise_play(self, limited, returncode):
        with tempfile.TemporaryDirectory() as temporary:
            top = Path(temporary)
            args = arguments('--team-sizes', '1', '--pairs', 'medium:medium', '--arms', 'off', '--seeds', '7101')
            args.results = str(top / 'results')
            game = matrix['schedule'](args)[0]
            finished, written = [], []
            original_append = matrix['append']

            def fake_runner(command, **kwargs):
                out = Path(command[command.index('--output-dir') + 1])
                out.mkdir(parents=True)
                lines = [
                    'CIV_WARS_SETUP: seed=7101 team_size=1 size=0 worms=1 spice=2 left=medium right=medium '
                    'strategy=off config_hash=config options_hash=options map_hash=map observing=1',
                    'CIV_WARS_WORM: id=9 object_id=9 cycle=0 item=40 house=6',
                    'CIV_WARS_HOUSE: cycle=60 house=1 team=1 alive=1 eliminated_s=-1 tech=1 islands=0',
                    'CIV_WARS_HOUSE: cycle=60 house=2 team=2 alive=1 eliminated_s=-1 tech=1 islands=0',
                    'CIV_WARS_RESULT: winner_team=0 outcome=time_limit end_s=60 worm_ids_complete=1 worm_count=1',
                    'CIV_WARS_PERF: loop_thread_cpu_ms=10 loop_wall_ms=20 cycles=60 cycles_per_s=3000',
                    'PROBE_DIGEST: cycle=60 digest=abcdef final=1',
                    'CAMPAIGN_BALANCE_RESULT: outcome=time_limit',
                ]
                (out / 'run.log').write_text('\n'.join(lines) + '\n', encoding='utf-8')
                (out / 'summary.json').write_text(json.dumps({'sourceCommit': 'fixture', 'workingTreeModified': False}),
                                                  encoding='utf-8')
                write_capture(out, capture_events(limited))
                return types.SimpleNamespace(returncode=returncode, stdout='', stderr='')

            def append(path, fields, rows):
                written.append(path.name)
                original_append(path, fields, rows)

            with patch.object(matrix['subprocess'], 'run', fake_runner), patch.dict(
                    matrix, {'append': append, 'finish_game': lambda scratch, out, failed: finished.append(failed)}):
                row = matrix['play'](args, top / 'scratch', Path('unused-probe'), game)
            self.assertEqual(written[-1], 'games.csv')
            self.assertEqual(finished, [returncode != 0])
            self.assertEqual(bool(row['error']), returncode != 0)
            self.assertEqual(row['worm_ids_complete'], '1')
            self.assertEqual(row['worm_losses_complete'], int(not limited))
            self.assertEqual(row['worm_losses'], '' if limited else 1)
            self.assertEqual(row['attacks'], '' if limited else 1)
            self.assertEqual(row['observed_worm_losses'], 1)
            self.assertEqual(row['observed_attacks'], 1)
            self.assertEqual(row['left_score'], -0.25)
            self.assertEqual(row['right_score'], -0.25)
            self.assertEqual(row['telemetry_status'], 'capture_limit' if limited else 'complete')
            houses = matrix['read'](top / 'results/houses.csv')
            self.assertEqual(houses[0]['worm_losses'], '' if limited else '1')
            self.assertEqual(houses[1]['worm_losses'], '' if limited else '0')
            self.assertEqual(matrix['read'](top / 'results/games.csv')[0]['digest'], 'abcdef')


if __name__ == '__main__':
    unittest.main()
