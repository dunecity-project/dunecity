#!/usr/bin/env python3
"""Read-only design construction checks; no game or build is launched."""
import contextlib
import importlib.util
import io
import itertools
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('ai_screen_design', Path(__file__).with_name('ai-screen-design.py'))
design = importlib.util.module_from_spec(spec)
spec.loader.exec_module(design)


class ScreeningDesignTest(unittest.TestCase):
    def test_eight_switch_resolution_four_sixteen_rows(self):
        dimensions, columns = design.fractional_columns(8)
        self.assertEqual(dimensions, 4)
        self.assertEqual(design.resolution(columns), 4)
        for masks in itertools.combinations(columns, 3):
            self.assertNotEqual(masks[0] ^ masks[1] ^ masks[2], 0)
        for left in columns:
            signs = [(-1) ** bin(row & left).count('1') for row in range(16)]
            self.assertEqual(sum(signs), 0)
            for right in columns:
                if left != right:
                    self.assertEqual(sum(sign * (-1) ** bin(row & right).count('1')
                                         for row, sign in enumerate(signs)), 0)

    def test_case_grid_keeps_every_primary_stratum(self):
        manifest = design.cases(2026110807, design.FORMATS, [64, 128, 160], design.SPICES, [3, 6, 9])
        self.assertEqual(len(manifest['cases']), 36)
        self.assertEqual(manifest, design.cases(2026110807, design.FORMATS, [64, 128, 160], design.SPICES, [3, 6, 9]))
        coverage = design.coverage(manifest['cases'])
        self.assertTrue(all(row['cases'] == 1 for row in coverage['joint_format_size_spice']))
        self.assertEqual(sorted(coverage['initial_tech'].values()), [12, 12, 12])
        self.assertEqual(sorted(coverage['composition'].values()), [12, 12, 12])
        self.assertEqual(len({row['seed'] for row in manifest['cases']}), 36)
        for cell in manifest['cases']:
            self.assertEqual((cell['rock'] % 5, cell['mountain'] % 5), (0, 0))

    def test_rejects_recipes_the_probe_would_clamp(self):
        cell = dict(format='ffa4', size=128, spice_level='low', initial_tech=6, seed=1, rock=12, mountain=5)
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            design.validate_cases(dict(schema_version=1, sample_seed=1, cases=[cell]))
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            design.validate_cases(dict(schema_version=1, sample_seed=1, cases=[dict(cell, rock=15, size=100)]))
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            design.validate_cases(dict(schema_version=1, sample_seed=1,
                                       cases=[dict(cell, rock=15), dict(cell, rock=45, mountain=15)]))

    def test_explicit_supplement_reuses_styles_and_names_its_case_file(self):
        cells = [dict(format='ffa4', size=64, spice_level='low', initial_tech=3, seed=9, rock=15, mountain=5),
                 dict(format='ffa6', size=160, spice_level='high', initial_tech=9, seed=10, rock=45, mountain=15)]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'cases.json').write_text(json.dumps(dict(schema_version=1, sample_seed=77, cases=cells)))
            base = ['--source-root', str(root / 'source'), '--cases', str(root / 'cases.json')]
            args = design.parser().parse_args(['pilot', '--output', str(root / 'supplement'),
                                               '--case-label', 'supplement'] + base)
            original = design.parser().parse_args(['pilot', '--output', str(root / 'original'),
                                                   '--source-root', str(root / 'source')])
            with contextlib.redirect_stdout(io.StringIO()):
                first = design.factorial(original, design.pilot_spec())
                plan = design.factorial(args, design.pilot_spec())
            self.assertEqual([run['ini_sha256'] for run in plan['runs']],
                             [run['ini_sha256'] for run in first['runs']])
            self.assertTrue((root / 'source/tests/ai/screen-designs/supplement-cases.json').is_file())
            self.assertTrue(all('tests/ai/screen-designs/supplement-cases.json' in item['argv']
                                for item in plan['commands']))
            self.assertEqual(plan['sample_seed'], 77)
            self.assertEqual(plan['game_counts']['factorial'], 33 * 4)
            tournament = design.parser().parse_args(['tournament', '--output', str(root / 'mixed'),
                '--variants', 'a', 'b', '--case-label', 'mixed-supplement'] + base)
            with contextlib.redirect_stdout(io.StringIO()):
                mixed = design.tournament(tournament)
            self.assertEqual(sorted(item['sample_seed'] for item in mixed['commands']), [81, 83])
            self.assertEqual(mixed['games'], 4 + 6)

    def test_pilot_dealiases_all_two_factor_terms(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = design.parser().parse_args(['pilot', '--output', str(root / 'result'),
                                               '--source-root', str(root / 'source')])
            with contextlib.redirect_stdout(io.StringIO()):
                plan = design.factorial(args, design.pilot_spec())
            self.assertEqual(plan['resolution'], 6)
            self.assertEqual(len(plan['runs']), 32)
            self.assertEqual(len(plan['interaction_aliases']), 15)
            self.assertTrue(all(group['individually_identifiable'] for group in plan['interaction_aliases']))
            self.assertEqual(plan['game_counts']['factorial'], 2376)
            self.assertEqual(plan['game_counts']['minimal_oat'], 504)
            self.assertEqual(plan['game_counts']['oat_equal_per_factor_exposure'], 8064)
            self.assertTrue(all(item['argv'][item['argv'].index('--case-manifest') + 1].startswith('tests/')
                                for item in plan['commands']))
            self.assertEqual(plan['commands'][-1]['style'], 'control')
            self.assertNotIn('--candidate-style', plan['commands'][-1]['argv'])
            self.assertEqual(sum(run['levels']['refinery6'] for run in plan['runs']), 16)

    def test_tournament_balances_all_pairs_and_seats(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = design.parser().parse_args(['tournament', '--output', str(root / 'result'),
                '--source-root', str(root / 'source'), '--variants', 'a', 'b', 'c', 'd', 'e',
                '--formats', 'ffa4', 'ffa6', '--seeds', '17001', '17002'])
            with contextlib.redirect_stdout(io.StringIO()):
                plan = design.tournament(args)
            for fmt in ('ffa4', 'ffa6'):
                rows = [row for row in plan['assignments'] if row['format'] == fmt]
                counts = {pair: sum(all(name in row['seats'] for name in pair) for row in rows)
                          for pair in itertools.combinations(args.variants, 2)}
                self.assertEqual(len(set(counts.values())), 1)
                for name in args.variants:
                    seats = [sum(row['seats'][seat] == name for row in rows)
                             for seat in range(int(fmt[-1]))]
                    self.assertEqual(len(set(seats)), 1)
            self.assertEqual(plan['games'], 92)


if __name__ == '__main__':
    unittest.main()
