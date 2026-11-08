"""Pure control-map transport fixtures; no builds, games or claimed AI-strength evidence."""
import ast
import base64
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


HARNESS = Path(__file__).with_name('sweep-controls.py')
nodes = []
for node in ast.parse(HARNESS.read_text(encoding='utf-8'), str(HARNESS)).body:
    if (isinstance(node, ast.Assign) and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
        break
    nodes.append(node)
else:
    raise AssertionError('Missing queue AST boundary')
controls = {'__file__': str(HARNESS), '__name__': 'cluster_harness'}
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(HARNESS), 'exec'), controls)
spec = importlib.util.spec_from_file_location('compare_control_maps', HARNESS.with_name('compare-control-maps.py'))
comparator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparator)
MATRIX = controls['MATRIX']
GENOME = dict(id='pair', values={'wave_readiness_percent': 45, 'focus_fire': 0})


def encode(genome):
    return base64.urlsafe_b64encode(json.dumps(genome).encode()).decode().rstrip('=')


def arguments(genome=None, *extra):
    argv = ['--tier', 'controls', '--results', 'unused-results']
    if genome is not None:
        argv += ['--sweep-genome-b64', encode(genome)]
    return controls['parser'].parse_args(argv + list(extra))


class SweepControlsTests(unittest.TestCase):
    def test_schedule_is_the_unchanged_26_game_controls_tier(self):
        native = controls['schedule'](arguments())
        self.assertEqual(native, controls['BASE_SCHEDULE'](arguments()))
        self.assertEqual(len(native), 26)
        self.assertEqual(controls['schedule'](arguments(GENOME)), native)

    def test_only_the_candidate_brutal_seat_gets_the_constant_overlay(self):
        args = controls['configured'](arguments(GENOME))
        name = controls['label'](controls['sweep_genome'](args.sweep_genome_b64)[0])
        self.assertEqual(args.style, name)
        with tempfile.TemporaryDirectory() as temporary:
            for index, game in enumerate(controls['schedule'](args)):
                out = Path(temporary) / f'g{index}'
                command = MATRIX['command'](args, Path('unused-probe'), game, out)
                self.assertNotIn('--scenario-style', command)
                directory = Path(command[command.index('--genome-phase-dir') + 1])
                number = MATRIX['HOUSE_NUMBERS'][game['sides'][0][0]]
                self.assertEqual(game['sides'][0][2], 'QB')
                schedule = (directory / 'schedule.ini').read_text()
                self.assertEqual(schedule.split('[Genomes]\n')[1].splitlines(), [f'{number}=candidate-{name}'])
                files = sorted(path.name for path in directory.glob('*.ini') if path.name != 'schedule.ini')
                self.assertEqual(files, [f'{number}-{phase}.ini' for phase in ('late', 'mid', 'opening')])
                payloads = {(directory / name_).read_text() for name_ in files}
                self.assertEqual(len(payloads), 1)
                payload = payloads.pop()
                self.assertIn('WaveReadinessPercent=45', payload)
                self.assertIn('FocusFire=0', payload)
                self.assertEqual(MATRIX['expected_style_seats'](args, game), f'{number}=candidate-{name}')
            native = controls['configured'](arguments())
            game = controls['schedule'](native)[0]
            self.assertNotIn('--genome-phase-dir', MATRIX['command'](native, Path('unused-probe'), game,
                                                                     Path(temporary) / 'native'))
            self.assertEqual(MATRIX['expected_style_seats'](native, game), 'none')

    def test_tier_receipts_and_comparator_accept_only_the_candidate_seat(self):
        args = controls['configured'](arguments(GENOME))
        game = controls['schedule'](args)[0]
        house = game['sides'][0][0]
        number = str(MATRIX['HOUSE_NUMBERS'][house])
        other = next(str(MATRIX['HOUSE_NUMBERS'][h]) for h, _, code in game['sides'][1:] if code.startswith('Q')) \
            if any(code.startswith('Q') for _, _, code in game['sides'][1:]) else '5'
        lines = ['SCENARIO_SETUP: seed=1 config_hash=native style=default',
                 f'SCENARIO_AI_CONFIG: house={number} config_hash=changed',
                 f'SCENARIO_AI_CONFIG: house={other} config_hash=native',
                 f'GENOME_PHASE: house={number} genome=candidate-{args.style} phase=opening cycle=0 '
                 'config_hash=changed']
        self.assertEqual(MATRIX['style_seats'](lines), MATRIX['expected_style_seats'](args, game))
        leaked = lines[:2] + [lines[2].replace('config_hash=native', 'config_hash=changed')] + lines[3:]
        self.assertNotEqual(MATRIX['style_seats'](leaked), MATRIX['expected_style_seats'](args, game))
        sides = ' '.join(f'{h}:{t}:{c}' for h, t, c in game['sides'])
        self.assertEqual(comparator.check_style_seats(
            [dict(id='x', sides=sides, style_seats=f'{number}=candidate-{args.style}')], required=True), args.style)
        with self.assertRaises(ValueError):
            comparator.check_style_seats([dict(id='x', sides=sides, style_seats=f'{other}=candidate-{args.style}')],
                                         required=True)

    def test_rejects_other_tiers_styles_expression_rules_and_bad_levels(self):
        for extra in (('--tier', 'smoke'), ('--style', 'launcher-kite-off')):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                controls['schedule'](arguments(GENOME, *extra))
        with self.assertRaises(ValueError):
            controls['schedule'](arguments(None, '--style', 'launcher-kite-off'))
        bad = [dict(id='bad', values={}, expression=[dict(when={'mod': ['vanilla']}, values={'focus_fire': 0})]),
               dict(id='bad', values={'technology_installments': 1}),
               dict(id='bad', values={'wave_readiness_percent': 61})]
        for genome in bad:
            with self.subTest(genome=genome), self.assertRaises(ValueError):
                controls['sweep_genome'](encode(genome))


if __name__ == '__main__':
    unittest.main()
