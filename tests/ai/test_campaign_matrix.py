"""Campaign schedule coverage only: no builds or games."""
import ast
from collections import Counter
from pathlib import Path
import unittest


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


class CampaignCoverageTests(unittest.TestCase):
    def test_every_real_vanilla_mission_once_with_authored_enemies_and_ai_support(self):
        args = matrix['parser'].parse_args([
            '--tier', 'full', '--categories', 'campaign',
            '--cells', '^campaign/player-brutal-v-campaign-ai/vanilla$'])
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
                self.assertEqual(game['enemy_ai'], 'campaign')
                self.assertFalse(game['ai_only'])
                self.assertTrue(game['id'].endswith('#human-partner'))
                self.assertEqual(game['expect'], ('campaign_won',))
                command = matrix['command'](args, Path('unused-probe'), game, Path('unused-output'))
                self.assertNotIn('--campaign-ai-only', command)
                self.assertNotIn('--local-spectator', command)
                self.assertEqual(command[command.index('--enemy-ai') + 1], 'campaign')
                self.assertEqual(command[command.index('--mission') + 1], str(game['mission']))
        shard_ids = []
        for k in range(1, 5):
            args.shard = k, 4
            shard_ids.extend(game['id'] for game in matrix['schedule'](args))
        self.assertEqual(Counter(shard_ids), Counter(game['id'] for game in games))

    def test_ai_only_diagnostic_drops_the_human_seat(self):
        args = matrix['parser'].parse_args([
            '--tier', 'full', '--categories', 'campaign', '--campaign-ai-only',
            '--cells', '^campaign/player-brutal-v-campaign-ai/vanilla$'])
        games = matrix['schedule'](args)
        self.assertEqual(len(games), 66)
        for game in games:
            self.assertTrue(game['ai_only'])
            self.assertNotIn('#human-partner', game['id'])
            self.assertEqual(game['expect'], ('campaign_won',))
            self.assertIn('--campaign-ai-only', matrix['command'](args, Path('unused-probe'), game, Path('unused-output')))


if __name__ == '__main__':
    unittest.main()
