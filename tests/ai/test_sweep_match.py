"""Pure transport fixtures; no builds, games or claimed AI-strength evidence."""
import ast
import base64
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


HARNESS = Path(__file__).with_name('sweep-match.py')
nodes = []
for node in ast.parse(HARNESS.read_text(encoding='utf-8'), str(HARNESS)).body:
    if (isinstance(node, ast.Assign) and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
        break
    nodes.append(node)
else:
    raise AssertionError('Missing queue AST boundary')
sweep = {'__file__': str(HARNESS), '__name__': 'cluster_harness'}
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(HARNESS), 'exec'), sweep)
WAVE = dict(id='wave', values={'wave_readiness_percent': 45}, expression=[])
CITY = dict(id='city', values={}, expression=[dict(when={'mod': ['dunecity']}, values={'city_first_factory_saving': 1})])


def encode(*genomes):
    payload = json.dumps(dict(schema_version=1, genomes=list(genomes))).encode()
    return base64.urlsafe_b64encode(payload).decode().rstrip('=')


def arguments(genomes=None, *extra):
    argv = ['--mode', 'sample', '--mod', 'both', '--cases', '6', '--sample-seed', '2610800001']
    if genomes:
        argv += ['--candidate-manifest-b64', encode(*genomes)]
    return sweep['parser'].parse_args(argv + list(extra))


class SweepMatchTests(unittest.TestCase):
    def test_native_is_exact_audit_schedule_and_candidates_replay_the_same_fixtures(self):
        native_args = arguments()
        native = sweep['schedule'](native_args)
        self.assertEqual(native, sweep['BASE_SCHEDULE'](native_args))
        self.assertEqual(len(native), 24)
        self.assertEqual({game['format'] for game in native}, {'1v1', '2v2', 'ffa4', 'ffa6'})
        candidates = sweep['schedule'](arguments([WAVE, CITY]))
        self.assertEqual(len(candidates), 48)
        self.assertEqual(len({game['id'] for game in candidates}), 48)
        for index, genome in enumerate((WAVE, CITY)):
            label = sweep['style'](genome)
            for before, after in zip(native, candidates[index * 24:(index + 1) * 24]):
                self.assertEqual(after['id'], before['id'] + '/' + label[:22])
                self.assertEqual((after['candidate_style'], after['sweep_genome']), (label, genome['id']))
                self.assertEqual(dict(after, id=before['id'], candidate_style='', sweep_genome=None),
                                 dict(before, sweep_genome=None))

    def test_overlay_is_sparse_constant_scoped_and_only_for_candidate(self):
        args = arguments([CITY])
        with tempfile.TemporaryDirectory() as temporary:
            for index, game in enumerate(sweep['schedule'](args)):
                directory = sweep['genome_inputs'](args, game, Path(temporary) / str(index))
                self.assertEqual({path.name for path in directory.iterdir()}, {'schedule.ini'} | {
                    f'{game["candidate_house"]}-{phase}.ini' for phase in ('opening', 'mid', 'late')})
                payloads = {(directory / f'{game["candidate_house"]}-{phase}.ini').read_text()
                            for phase in ('opening', 'mid', 'late')}
                self.assertEqual(len(payloads), 1)
                payload = payloads.pop()
                self.assertEqual('CityFirstFactorySaving=1' in payload, game['mod'] == 'dunecity')
                self.assertNotIn('FocusFire', payload)
                self.assertIn(f'{game["candidate_house"]}=candidate-{game["candidate_style"]}',
                              (directory / 'schedule.ini').read_text())
            native_game = sweep['schedule'](arguments())[0]
            self.assertIsNone(sweep['genome_inputs'](arguments(), native_game, Path(temporary) / 'native'))

    def test_bad_genes_levels_rules_phases_duplicates_and_empty_genomes_fail_closed(self):
        bad = [[dict(id='bad', values={'unknown_gene': 1})],
               [dict(id='bad', values={'wave_readiness_percent': 61})],
               [dict(id='bad', values={'technology_installments': 1})],
               [dict(id='bad', values={}, expression=[
                   dict(when={'phase': ['opening']}, values={'wave_readiness_percent': 45})])],
               [dict(id='bad', values={})],
               [WAVE, dict(WAVE, id='copy')],
               [WAVE, dict(CITY, id='wave')]]
        for genomes in bad:
            with self.subTest(genomes=genomes), self.assertRaises(ValueError):
                sweep['schedule'](arguments(genomes))
        duplicate = base64.b64encode(b'{"schema_version":1,"genomes":[],"genomes":[]}').decode()
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            sweep['candidate_manifest'](duplicate)

    def test_no_style_or_matrix_backdoor(self):
        for extra in (('--mode', 'matrix'), ('--style', 'launcher-kite-off'),
                      ('--candidate-style', 'launcher-kite-off'), ('--cases', '0')):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                sweep['schedule'](arguments([WAVE], *extra))

    def test_play_clears_inherited_overlay_environment_for_every_arm(self):
        seen = {}
        with patch.dict(os.environ, {key: 'inherited' for key in sweep['INHERITED']}), \
                patch.dict(sweep, BASE_PLAY=lambda *call: seen.update(
                    {key: os.environ.get(key) for key in sweep['INHERITED']})):
            sweep['play'](arguments(), Path('unused'), Path('unused'), {})
        self.assertEqual(seen, dict.fromkeys(sweep['INHERITED']))

    def test_existing_receipt_verifier_rejects_changed_opponent_or_missing_phase(self):
        game = sweep['schedule'](arguments([WAVE]))[0]
        candidate = str(game['candidate_house'])
        configs = [dict(house=str(sweep['AUDIT']['HOUSE_IDS'][name]),
                        config_hash='candidate' if sweep['AUDIT']['HOUSE_IDS'][name] == int(candidate) else 'native')
                   for name, _, _ in game['sides']]
        setup = dict(style='default', config_hash='native')
        lines = [f'GENOME_PHASE: house={candidate} genome=candidate-{game["candidate_style"]} '
                 'phase=opening cycle=0 config_hash=candidate']
        verified = sweep['AUDIT']['seat_styles_verified']
        self.assertTrue(verified(game, setup, configs, lines))
        self.assertFalse(verified(game, setup, configs, []))
        changed = [dict(row, config_hash='other') if row['house'] != candidate else row for row in configs]
        self.assertFalse(verified(game, setup, changed, lines))


if __name__ == '__main__':
    unittest.main()
