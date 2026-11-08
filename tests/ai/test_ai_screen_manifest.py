"""Queue AST schedule contract checks; never execute games or builds."""
import ast
import json
from pathlib import Path
import tempfile
import unittest

HARNESS = Path(__file__).with_name('dunecity-audit.py')
tree = ast.parse(HARNESS.read_text(), str(HARNESS))
prefix = []
for node in tree.body:
    if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == 'args'
                                          for target in node.targets):
        break
    prefix.append(node)
audit = {'__file__': str(HARNESS), '__name__': 'cluster_harness'}
exec(compile(ast.Module(body=prefix, type_ignores=[]), str(HARNESS), 'exec'), audit)


class ManifestScreenTest(unittest.TestCase):
    def test_same_map_cells_and_candidate_seats_across_arms(self):
        cell = dict(format='ffa6', size=64, spice_level='low', initial_tech=3,
                    seed=17001, rock=15, mountain=15, difficulty='brutal')
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'cases.json'
            path.write_text(json.dumps(dict(schema_version=1, sample_seed=17007, cases=[cell])))
            parse = audit['parser'].parse_args
            control = parse(['--mode', 'sample', '--mod', 'vanilla', '--case-manifest', str(path)])
            candidate = parse(['--mode', 'sample', '--mod', 'vanilla', '--case-manifest', str(path),
                               '--candidate-style', 'stats-vanilla-r4-00'])
            left, right = audit['schedule'](control), audit['schedule'](candidate)
            self.assertEqual(len(left), 2)
            self.assertEqual([game['id'] for game in left], [game['id'] for game in right])
            self.assertEqual([game['skirmish'] for game in left], [game['skirmish'] for game in right])
            self.assertEqual(left[0]['sample_seed'], 17007)
            self.assertEqual(left[0]['sample_case'], 1)
            self.assertNotEqual(left[0]['candidate_house'], left[1]['candidate_house'])
            self.assertIn('sand=70,rock=15,mountain=15', left[0]['skirmish'])
            self.assertEqual(left[0]['limit'], 90)

    def test_no_spice_is_not_a_screen_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'cases.json'
            path.write_text(json.dumps(dict(schema_version=1, sample_seed=1,
                cases=[dict(format='ffa4', size=128, spice_level='none', initial_tech=9, seed=1)])))
            args = audit['parser'].parse_args(['--mode', 'sample', '--mod', 'vanilla',
                                              '--case-manifest', str(path)])
            with self.assertRaisesRegex(ValueError, 'Unsupported'):
                audit['schedule'](args)

    def test_terrain_off_the_engine_step_fails_before_queueing(self):
        for rock, mountain, size in ((12, 5, 128), (30, 5, 100), (48, 15, 64)):
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / 'cases.json'
                path.write_text(json.dumps(dict(schema_version=1, sample_seed=1, cases=[dict(
                    format='ffa4', size=size, spice_level='low', initial_tech=6, seed=1,
                    rock=rock, mountain=mountain)])))
                args = audit['parser'].parse_args(['--mode', 'sample', '--mod', 'vanilla',
                                                  '--case-manifest', str(path)])
                with self.assertRaisesRegex(ValueError, 'Unsupported'):
                    audit['schedule'](args)


if __name__ == '__main__':
    unittest.main()
