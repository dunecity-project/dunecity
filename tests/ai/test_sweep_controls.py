"""Pure control-map transport fixtures; no builds, games or claimed AI-strength evidence."""
import ast
import base64
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

    def test_every_quantbot_and_no_classic_ai_gets_the_same_constant_overlay(self):
        args = arguments(GENOME)
        name = controls['label'](controls['sweep_genome'](args.sweep_genome_b64)[0])
        with tempfile.TemporaryDirectory() as temporary:
            for index, game in enumerate(controls['schedule'](args)):
                out = Path(temporary) / f'g{index}'
                command = controls['command'](args, Path('unused-probe'), game, out)
                directory = Path(command[command.index('--genome-phase-dir') + 1])
                houses = controls['quantbot_houses'](game)
                self.assertEqual(len(houses), sum(code.startswith('Q') for _, _, code in game['sides']))
                schedule = (directory / 'schedule.ini').read_text()
                self.assertEqual(sorted(line.split('=')[0] for line in schedule.split('[Genomes]\n')[1].splitlines()),
                                 sorted(houses))
                self.assertTrue(all(line.endswith('=' + name) for line in schedule.split('[Genomes]\n')[1].splitlines()))
                payloads = {(directory / f'{house}-{phase}.ini').read_text()
                            for house in houses for phase in ('opening', 'mid', 'late')}
                self.assertEqual(len(payloads), 1)
                payload = payloads.pop()
                self.assertIn('WaveReadinessPercent=45', payload)
                self.assertIn('FocusFire=0', payload)
                self.assertIn(str(out), controls['pending'])
            self.assertNotIn('--genome-phase-dir', controls['command'](
                arguments(), Path('unused-probe'), controls['schedule'](arguments())[0], Path(temporary) / 'native'))

    def test_rejects_other_tiers_styles_expression_rules_and_bad_levels(self):
        for extra in (('--tier', 'smoke'), ('--style', 'launcher-kite-off')):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                controls['schedule'](arguments(GENOME, *extra))
        bad = [dict(id='bad', values={}, expression=[dict(when={'mod': ['vanilla']}, values={'focus_fire': 0})]),
               dict(id='bad', values={'technology_installments': 1}),
               dict(id='bad', values={'wave_readiness_percent': 61})]
        for genome in bad:
            with self.subTest(genome=genome), self.assertRaises(ValueError):
                controls['sweep_genome'](encode(genome))

    def test_receipt_verifier_requires_exact_application_for_every_quantbot(self):
        lines = ['SCENARIO_SETUP: seed=1 config_hash=native style=default',
                 'SCENARIO_AI_CONFIG: house=0 config_hash=changed',
                 'SCENARIO_AI_CONFIG: house=2 config_hash=changed',
                 'GENOME_PHASE: house=0 genome=sweep-x phase=opening cycle=0 config_hash=changed',
                 'GENOME_PHASE: house=2 genome=sweep-x phase=opening cycle=0 config_hash=changed']
        verify = controls['verify']
        self.assertEqual(verify(lines, 'sweep-x', ['0', '2']), (True, ''))
        self.assertFalse(verify(lines[:-1], 'sweep-x', ['0', '2'])[0])
        self.assertFalse(verify(lines, 'sweep-y', ['0', '2'])[0])
        self.assertFalse(verify([lines[0].replace('default', 'other')] + lines[1:], 'sweep-x', ['0', '2'])[0])
        self.assertFalse(verify(lines[:4] + [lines[4].replace('cycle=0', 'cycle=9')], 'sweep-x', ['0', '2'])[0])
        self.assertFalse(verify(lines[:4] + [lines[4].replace('config_hash=changed', 'config_hash=x')],
                                'sweep-x', ['0', '2'])[0])
        self.assertFalse(verify(lines, 'sweep-x', ['0', '2', '4'])[0])


if __name__ == '__main__':
    unittest.main()
