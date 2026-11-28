"""Campaign fixture scheduling, command and captured-roster checks, and the smoke replay
check; no builds or games."""
import ast
from collections import Counter
from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import runpy
import sys
import unittest
from unittest.mock import patch


HARNESS = Path(__file__).with_name('scenario-matrix.py')
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


def balance_arguments(options):
    """Execute the real runner's CLI checks only, stopping before profile/build setup."""
    path = HARNESS.with_name('run-campaign-balance.py')
    source = ast.parse(path.read_text(encoding='utf-8'), str(path))
    body = []
    for node in source.body:
        if isinstance(node, ast.FunctionDef) and node.name == 'team_number':
            break
        body.append(node)
    else:
        raise AssertionError('missing balance prebuild boundary')
    namespace = {'__file__': str(path), '__name__': 'balance_cli'}
    with patch.object(sys, 'argv', [str(path), '--output-dir', 'unused-output', *options]):
        with redirect_stderr(io.StringIO()):
            exec(compile(ast.Module(body=body, type_ignores=[]), str(path), 'exec'), namespace)
    return namespace['args']


class CampaignCoverageTests(unittest.TestCase):
    def test_helper_style_cannot_leak_into_global_or_ai_only_setup(self):
        args = balance_arguments(['--scenario', '--helper-style', 'campaign-power-surplus'])
        self.assertEqual(args.helper_style, 'campaign-power-surplus')
        for extra in (['--scenario-style', 'campaign-power-one'], ['--campaign-ai-only'], ['--scenario-sides', 'harkonnen:1:qBotHard']):
            with self.assertRaises(SystemExit):
                balance_arguments(['--scenario', '--helper-style', 'campaign-power-surplus', *extra])

    def test_campaign_deck_preserves_explicit_cases_and_shards(self):
        import tempfile, json
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'deck.json'
            path.write_text(json.dumps({'schema': 'campaign-research-cases/1', 'cases': [
                dict(house='harkonnen', mission=n, seed=9901, helper='brutal', enemy='hard', mode='full', mod='vanilla', minutes=80)
                for n in (20, 21)]}))
            args = SimpleNamespace(case_manifest=path, shard=None)
            games = matrix['schedule'](args)
            self.assertEqual([g['mission'] for g in games], [20, 21])
            self.assertTrue(all(g['ai_only'] is False and g['enemy_ai'] == 'quantbot' for g in games))
            args.shard=(2, 2)
            self.assertEqual(matrix['schedule'](args), [games[1]])

    def test_noncampaign_seat_style_uses_only_first_quantbot(self):
        import tempfile
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as directory:
            args=SimpleNamespace(tier='full',seat_style='campaign-power-surplus',style=None)
            game=dict(kind='skirmish',sides=[('harkonnen',1,'QH'),('atreides',2,'QB')],id='test')
            path=matrix['control_style_inputs'](args,game,Path(directory)/'game')
            self.assertTrue((path/'0-opening.ini').is_file())
            self.assertFalse((path/'1-opening.ini').exists())
            self.assertEqual(matrix['expected_style_seats'](args,game),'0=candidate-campaign-power-surplus')

    def test_balance_cli_accepts_quantbot_tiers_and_ai_only_in_both_modes(self):
        for mode in ('full', 'single'):
            for tier in ('easy', 'medium', 'hard', 'brutal'):
                with self.subTest(mode=mode, enemy=tier):
                    args = balance_arguments([
                        '--scenario', '--mission', '8', '--level', '4', '--enemy-ai', 'quantbot',
                        '--enemy-difficulty', tier, '--partner-difficulty', 'brutal',
                        '--campaign-mode', mode, '--campaign-ai-only'])
                    self.assertEqual(args.campaign_mode, mode)
                    self.assertEqual(args.enemy_ai, 'quantbot')
                    self.assertEqual(args.enemy_difficulty, tier)
        legacy = balance_arguments(['--scenario', '--enemy-ai', 'campaign'])
        self.assertEqual(legacy.enemy_ai, 'campaign')

    def test_single_mission_cli_cannot_silently_run_a_generated_match(self):
        generated = [
            '--scenario', '--house', 'harkonnen', '--local-spectator',
            '--scenario-sides', 'harkonnen:1:qBotHard,atreides:2:qBotMedium',
            '--skirmish', 'size=64,base=cy,army=0']
        self.assertEqual(balance_arguments(generated).campaign_mode, 'full')
        for invalid in (['--campaign-mode', 'single'], generated + ['--campaign-mode', 'single']):
            with self.subTest(options=invalid), self.assertRaises(SystemExit) as error:
                balance_arguments(invalid)
            self.assertEqual(error.exception.code, 2)

    def test_every_real_vanilla_mission_once_with_quantbot_enemies_and_ai_support(self):
        args = matrix['parser'].parse_args([
            '--tier', 'full', '--categories', 'campaign',
            '--cells', '^campaign/player-brutal-v-quantbot-easy/vanilla$'])
        games = matrix['schedule'](args)
        self.assertEqual(len(games), 66)
        self.assertEqual(games, matrix['schedule'](args))
        for house in ('atreides', 'harkonnen', 'ordos'):
            own = [game for game in games if game['house'] == house]
            self.assertEqual(Counter(game['mission'] for game in own), Counter(range(1, 23)))
            self.assertEqual(Counter(game['level'] for game in own),
                             {1: 1, 2: 3, 3: 3, 4: 3, 5: 3, 6: 3, 7: 3, 8: 2, 9: 1})
            self.assertEqual(next(game['level'] for game in own if game['mission'] == 22), 9)
            for game in own:
                self.assertEqual(game['partner'], 'brutal')
                self.assertEqual(game['enemy_ai'], 'quantbot')
                self.assertEqual(game['enemy_difficulty'], 'easy')
                self.assertEqual(game['campaign_mode'], 'full')
                self.assertEqual(game['sides'], [(house, 1, 'QB'), ('enemies', 2, 'QE')])
                self.assertFalse(game['ai_only'])
                self.assertTrue(game['id'].endswith('#full-campaign#human-partner'))
                self.assertNotIn('campaign-ai', game['id'])
                self.assertEqual(game['expect'], ('campaign_won',))
                command = matrix['command'](args, Path('unused-probe'), game, Path('unused-output'))
                self.assertNotIn('--campaign-ai-only', command)
                self.assertNotIn('--local-spectator', command)
                self.assertEqual(command[command.index('--enemy-ai') + 1], 'quantbot')
                self.assertEqual(command[command.index('--enemy-difficulty') + 1], 'easy')
                self.assertEqual(command[command.index('--campaign-mode') + 1], 'full')
                self.assertEqual(command[command.index('--mission') + 1], str(game['mission']))
        shard_ids = []
        for k in range(1, 5):
            args.shard = k, 4
            shard_ids.extend(game['id'] for game in matrix['schedule'](args))
        self.assertEqual(Counter(shard_ids), Counter(game['id'] for game in games))

    def test_all_enemy_tiers_reach_both_native_menu_modes_without_changing_seeds(self):
        seen = set()
        baseline = None
        for mode in ('full', 'single'):
            for tier, code in (('easy', 'QE'), ('medium', 'QM'), ('hard', 'QH'), ('brutal', 'QB')):
                with self.subTest(mode=mode, enemy=tier):
                    args = matrix['parser'].parse_args([
                        '--tier', 'full', '--categories', 'campaign', '--seeds-per-cell', '1',
                        '--enemy-difficulty', tier, '--campaign-mode', mode])
                    games = matrix['schedule'](args)
                    self.assertEqual(len(games), 264)
                    self.assertEqual({game['partner'] for game in games}, {'easy', 'medium', 'hard', 'brutal'})
                    inputs = [(game['mod'], game['house'], game['level'], game['mission'], game['seed'])
                              for game in games]
                    if baseline is None:
                        baseline = inputs
                    self.assertEqual(inputs, baseline)
                    self.assertFalse(seen.intersection(game['id'] for game in games))
                    seen.update(game['id'] for game in games)
                    for game in games:
                        self.assertEqual(game['enemy_ai'], 'quantbot')
                        self.assertEqual(game['sides'][-1], ('enemies', 2, code))
                        self.assertFalse(game['ai_only'])
                        if game['mod'] == 'vanilla' or game['partner'] == 'hard':
                            self.assertEqual(game['expect'], ('campaign_won',))
                        command = matrix['command'](args, Path('unused-probe'), game, Path('unused-output'))
                        self.assertEqual(command[command.index('--enemy-ai') + 1], 'quantbot')
                        self.assertEqual(command[command.index('--enemy-difficulty') + 1], tier)
                        self.assertEqual(command[command.index('--campaign-mode') + 1], mode)
                        self.assertNotIn('--scenario-sides', command)
                        self.assertNotIn('--local-spectator', command)

    def test_smoke_routes_requested_tier_and_mode_and_preserves_win_assertions(self):
        for mode in ('full', 'single'):
            args = matrix['parser'].parse_args([
                '--tier', 'smoke', '--categories', 'campaign',
                '--enemy-difficulty', 'medium', '--campaign-mode', mode])
            games = matrix['schedule'](args)
            self.assertEqual(len(games), 3)
            self.assertTrue(all(game['enemy_difficulty'] == 'medium' and game['campaign_mode'] == mode
                                and game['sides'][-1][-1] == 'QM' and game['expect'] == ('campaign_won',)
                                and not game['ai_only'] for game in games))

    def test_explicit_fresh_campaign_seeds_keep_support_and_other_categories(self):
        options = ['--tier', 'full', '--categories', 'campaign', 'aaa',
                   '--cells', 'player-brutal-v-quantbot-easy/vanilla|qb-v-4ah/vanilla']
        native = matrix['schedule'](matrix['parser'].parse_args(options))
        fresh = matrix['schedule'](matrix['parser'].parse_args(
            options + ['--campaign-seeds', '1280000', '1280001', '1280000']))
        campaign = [game for game in fresh if game['kind'] == 'campaign']
        self.assertEqual(len(campaign), 132)
        self.assertEqual({game['seed'] for game in campaign}, {1280000, 1280001})
        self.assertTrue(all(not game['ai_only'] and game['id'].endswith('#human-partner')
                            for game in campaign))
        self.assertEqual([game for game in native if game['kind'] != 'campaign'],
                         [game for game in fresh if game['kind'] != 'campaign'])
        changed = matrix['schedule'](matrix['parser'].parse_args([
            '--tier', 'full', '--categories', 'aaa', '--enemy-difficulty', 'brutal',
            '--campaign-mode', 'single', '--cells', 'qb-v-4ah/vanilla']))
        self.assertEqual([game for game in native if game['kind'] != 'campaign'], changed)

    def test_reworked_twins_need_a_cells_pattern_that_names_them(self):
        schedule = lambda cells: matrix['schedule'](matrix['parser'].parse_args(
            ['--tier', 'full', '--categories', 'campaign', '--cells', cells]))
        original = schedule('player-brutal-v-quantbot-easy/vanilla')
        self.assertEqual(len(original), 66)
        self.assertFalse(any(game.get('reworked') for game in original))
        paired = schedule('^campaign/(reworked-)?partner-hard-v-quantbot-easy/vanilla$')
        reworked = {(game['house'], game['mission'], game['seed']) for game in paired if game.get('reworked')}
        plain = {(game['house'], game['mission'], game['seed']) for game in paired if not game.get('reworked')}
        self.assertEqual(len(reworked), 66)
        self.assertEqual(reworked, plain)
        self.assertTrue(all(game['id'].startswith('campaign/reworked-partner-hard-v-quantbot-easy/')
                            for game in paired if game.get('reworked')))

    def test_ai_only_diagnostic_drops_the_human_seat_not_the_quantbot_enemy(self):
        args = matrix['parser'].parse_args([
            '--tier', 'full', '--categories', 'campaign', '--campaign-ai-only',
            '--cells', '^campaign/player-brutal-v-quantbot-easy/vanilla$'])
        games = matrix['schedule'](args)
        self.assertEqual(len(games), 66)
        for game in games:
            self.assertTrue(game['ai_only'])
            self.assertNotIn('#human-partner', game['id'])
            self.assertEqual(game['expect'], ('campaign_won',))
            self.assertIn('--campaign-ai-only', matrix['command'](args, Path('unused-probe'), game, Path('unused-output')))

    def test_captured_roster_rejects_legacy_enemy_wrong_tier_and_missing_human(self):
        args = matrix['parser'].parse_args([
            '--tier', 'full', '--categories', 'campaign', '--enemy-difficulty', 'medium',
            '--cells', '^campaign/player-brutal-v-quantbot-medium/vanilla$'])
        game = matrix['schedule'](args)[0]
        roster = [
            {'house': '1', 'team': '1', 'players': 'HumanPlayer+qBotBrutal'},
            {'house': '0', 'team': '2', 'players': 'qBotMedium'},
            {'house': '2', 'team': '2', 'players': 'qBotMedium'},
        ]
        self.assertEqual(matrix['campaign_roster_error'](game, roster), '')
        for wrong in ('CampaignAIPlayer', 'AIPlayerMedium', 'qBotEasy'):
            roster[1]['players'] = wrong
            self.assertIn('not all qBotMedium', matrix['campaign_roster_error'](game, roster))
        roster[1]['players'] = 'qBotMedium'
        roster[0]['players'] = 'qBotBrutal'
        self.assertIn('human/support', matrix['campaign_roster_error'](game, roster))
        self.assertEqual(matrix['campaign_roster_error'](dict(game, ai_only=True), roster), '')

    def test_report_keeps_enemy_tiers_modes_and_real_failures_separate(self):
        rows = []
        for mode, tier, outcome in (('full', 'medium', 'won'), ('single', 'medium', 'lost'),
                                    ('single', 'hard', 'time_limit')):
            args = matrix['parser'].parse_args([
                '--tier', 'full', '--categories', 'campaign', '--enemy-difficulty', tier,
                '--campaign-mode', mode, '--cells', f'^campaign/player-brutal-v-quantbot-{tier}/vanilla$'])
            game = matrix['schedule'](args)[0]
            rows.append(dict(game, sides=' '.join(f'{h}:{t}:{c}' for h, t, c in game['sides']),
                             expect='campaign_won', error='', outcome=outcome, fired='2', minutes='12',
                             win_type='quota' if outcome == 'won' else ''))
        problems = []
        output = io.StringIO()
        with redirect_stdout(output):
            matrix['campaign_report'](rows, {}, problems)
        text = output.getvalue()
        self.assertIn('| vanilla | full | qBotBrutal | qBotMedium | 1 | 1 | 0 | 0 |', text)
        self.assertIn('| vanilla | single | qBotBrutal | qBotMedium | 1 | 0 | 1 | 0 |', text)
        self.assertIn('| vanilla | single | qBotBrutal | qBotHard | 1 | 0 | 0 | 1 |', text)
        self.assertEqual(len(problems), 2)
        self.assertTrue(matrix['expectation'](rows[0]))
        self.assertFalse(matrix['expectation'](rows[1]))
        self.assertFalse(matrix['expectation'](rows[2]))
        for row, mark in zip(rows, ('Wq', 'L', 'T')):
            self.assertIn(f'| {row["cell"]} | atreides | {mark} |', text)

    def test_wrapper_forwards_tier_mode_and_retains_win_gate(self):
        wrapper = runpy.run_path(str(HARNESS.with_name('run-campaign-matrix.py')), run_name='campaign_wrapper')
        with patch.object(sys, 'argv', ['run-campaign-matrix.py', '--list', '--enemy-difficulty', 'hard',
                                       '--campaign-mode', 'single']):
            with patch('runpy.run_path') as dispatch:
                wrapper['main']()
            dispatch.assert_called_once_with(str(HARNESS), run_name='__main__')
            args = matrix['parser'].parse_args(sys.argv[1:])
            games = matrix['schedule'](args)
        self.assertTrue(args.check)
        self.assertEqual(len(games), 66)
        self.assertTrue(all(game['enemy_difficulty'] == 'hard' and game['campaign_mode'] == 'single'
                            and game['expect'] == ('campaign_won',) and not game['ai_only'] for game in games))

    def test_throughput_campaign_uses_the_same_quantbot_enemy_fixture(self):
        path = HARNESS.with_name('simulation-throughput.py')
        source = ast.parse(path.read_text(encoding='utf-8'), str(path))
        functions = [node for node in source.body if isinstance(node, ast.FunctionDef)
                     and node.name in ('case_arguments', 'schedule')]
        namespace = {'CASES': ('campaign',), 'STRESS': 'stress256'}
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(path), 'exec'), namespace)
        command, _ = namespace['case_arguments']('campaign')
        self.assertEqual(command[command.index('--enemy-ai') + 1], 'quantbot')
        self.assertEqual(command[command.index('--enemy-difficulty') + 1], 'easy')
        self.assertEqual(command[command.index('--campaign-mode') + 1], 'full')
        self.assertNotIn('--campaign-ai-only', command)
        self.assertNotIn('--local-spectator', command)
        games = namespace['schedule'](matrix['argparse'].Namespace(cases='campaign', rounds=1))
        self.assertEqual(games[0]['case'], 'campaign')
        self.assertEqual(games[0]['id'], 'campaign-quantbot-easy-full/0')


class SmokeReplayCheckTests(unittest.TestCase):
    # The smoke tier replays the Construction Yard duel to prove determinism. A game that
    # never printed its final digest records '', and '' == '' must not count as reproduced.
    def test_replay_needs_both_final_digests_and_a_match(self):
        replay_problem = matrix['replay_problem']
        game = lambda digest: {'digest': digest}
        self.assertIsNone(replay_problem(game('838d4011501c1cd2'), game('838d4011501c1cd2')))
        self.assertIn('diverged', replay_problem(game('838d4011501c1cd2'), game('9a39d8e612300c36')))
        for original, replay in (('', ''), ('838d4011501c1cd2', ''), ('', '838d4011501c1cd2')):
            with self.subTest(original=original, replay=replay):
                self.assertIn('no final digest', replay_problem(game(original), game(replay)) or '')
        self.assertIn('no original game', replay_problem(None, game('838d4011501c1cd2')))


if __name__ == '__main__':
    unittest.main()
