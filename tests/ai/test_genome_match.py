"""Pure scheduling/transport/evidence contracts; never build or play the engine."""
import ast
import base64
from collections import Counter
import configparser
import csv
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


HARNESS = Path(__file__).with_name('genome-match.py')
nodes = []
for node in ast.parse(HARNESS.read_text(encoding='utf-8'), str(HARNESS)).body:
    if (isinstance(node, ast.Assign) and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
        break
    nodes.append(node)
else:
    raise AssertionError('Missing generic queue AST boundary')
league = {'__file__': str(HARNESS), '__name__': 'cluster_harness'}
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(HARNESS), 'exec'), league)


def inline(genomes=None):
    payload = dict(schema_version=1, genomes=genomes if genomes is not None else [
        dict(id='candidate', values={}, expression=[])])
    return base64.b64encode(json.dumps(payload).encode('utf-8')).decode('ascii')


def arguments(*argv):
    return league['parser'].parse_args([
        '--results', 'unused-results', '--build-dir', 'unused-build', '--manifest-b64', inline(),
        '--seeds', '8101', '--pairs', 'candidate:default', '--mods', 'vanilla',
        '--difficulties', 'hard', '--sizes', '128', '--bases', 'cy', '--spice', 'medium',
        '--houses', 'harkonnen', 'atreides', *argv])


class GenomeMatchTests(unittest.TestCase):
    def test_generic_prepare_needs_no_runtime_manifest_or_seeds(self):
        args = league['parser'].parse_args(['--results', 'unused-results', '--build-dir', 'unused-build'])
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(league['subprocess'], 'run') as run:
                binary = league['build_probe'](args, Path(temporary))
        command = run.call_args.args[0]
        self.assertIn('--build-only', command)
        self.assertNotIn('--genome-phase-dir', command)
        self.assertIn('probe', str(binary))
        with self.assertRaisesRegex(ValueError, 'manifest-b64'):
            league['schedule'](args)

    def test_report_cli_needs_no_runtime_manifest_pairs_or_seeds(self):
        with tempfile.TemporaryDirectory() as temporary:
            results = Path(temporary)
            scores = results / 'observations.csv.gz'
            outcome = subprocess.run([sys.executable, str(HARNESS), '--report', '--results', str(results),
                                      '--scores', str(scores)], capture_output=True, text=True)
            self.assertEqual(outcome.returncode, 0, outcome.stderr)
            with gzip.open(scores, 'rt') as stream:
                self.assertIn('slot', next(csv.reader(stream)))

    def test_report_recovers_slots_from_original_assignment_receipts(self):
        with tempfile.TemporaryDirectory() as temporary:
            results = Path(temporary)
            league['write_table'](results / 'games.csv', ('id', 'error', 'observations'),
                                  [dict(id='g', error='', observations='observations-g.csv.gz')])
            league['write_table'](results / 'assignments.csv', ('id', 'house', 'slot', 'genome_digest'),
                                  [dict(id='g', house='1', slot='2', genome_digest='digest')])
            with gzip.open(results / 'observations-g.csv.gz', 'wt', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=('game_id', 'house', 'genome_digest'))
                writer.writeheader()
                writer.writerow(dict(game_id='g', house='1', genome_digest='digest'))
            args = arguments('--report', '--results', str(results), '--scores', str(results / 'merged.csv.gz'))
            self.assertFalse(league['report'](args))
            with gzip.open(args.scores, 'rt') as stream:
                self.assertEqual(next(csv.DictReader(stream))['slot'], '2')

    def test_pairs_rotate_each_genome_over_house_and_slot(self):
        games = league['schedule'](arguments())
        self.assertEqual(len(games), 4)
        self.assertEqual(len({game['id'] for game in games}), 4)
        positions = {(house, slot) for game in games for slot, (house, label) in enumerate(game['sides'], 1)
                     if label == 'candidate'}
        self.assertEqual(positions, {('harkonnen', 1), ('harkonnen', 2), ('atreides', 1), ('atreides', 2)})
        self.assertEqual(len({game['comparison_id'] for game in games}), 1)

    def test_duplicate_ffa_genomes_remain_distinct_house_assignments(self):
        args = arguments('--formats', 'ffa', '--ffa', 'candidate,default,default,default',
                         '--houses', 'harkonnen', 'atreides', 'ordos', 'sardaukar')
        games = league['schedule'](args)
        self.assertEqual(len(games), 16)
        positions = Counter((house, slot) for game in games for slot, (house, label) in enumerate(game['sides'], 1)
                            if label == 'candidate')
        self.assertEqual(len(positions), 16)
        self.assertEqual(set(positions.values()), {1})
        self.assertTrue(all(sum(label == 'default' for _, label in game['sides']) == 3 for game in games))
        self.assertEqual(len({game['comparison_id'] for game in games}), 1)

    def test_all_mods_and_difficulties_use_genuine_mod_route(self):
        args = arguments('--mods', 'vanilla', 'Dune2R', 'DuneCity', '--difficulties', 'medium', 'hard', 'brutal')
        games = league['schedule'](args)
        self.assertEqual({(game['mod'], game['difficulty']) for game in games},
                         {(mod, difficulty) for mod in ('vanilla', 'dune2r', 'dunecity')
                          for difficulty in ('medium', 'hard', 'brutal')})
        game = next(game for game in games if game['mod'] == 'dune2r')
        command = league['command'](args, Path('probe'), game, Path('run'), Path('inputs'))
        self.assertEqual(command[command.index('--mod') + 1], 'Dune2R')
        self.assertEqual(command[command.index('--match-sides') + 1], 'harkonnen:default,atreides:default')
        self.assertNotIn('--no-ai-telemetry', command)
        self.assertIn('--genome-phase-dir', command)

    def test_manifest_and_phase_changes_invalidate_transport_not_context(self):
        args = arguments()
        before = league['schedule'](args)[0]
        args.manifest_b64 = inline([dict(id='candidate', values={'focus_fire': 0}, expression=[])])
        changed = league['schedule'](args)[0]
        self.assertNotEqual(before['id'], changed['id'])
        self.assertEqual(before['context_id'], changed['context_id'])
        args.phase_minutes = [0, 5, 15]
        phased = league['schedule'](args)[0]
        self.assertNotEqual(changed['id'], phased['id'])
        self.assertEqual(changed['context_id'], phased['context_id'])

    def test_default_manifest_is_sparse_and_replayable(self):
        normalized, genomes, _, replay = league['manifest'](inline())
        self.assertEqual(genomes['default']['values'], {})
        self.assertEqual(league['manifest'](replay)[0], normalized)
        with self.assertRaisesRegex(ValueError, 'Unknown genome'):
            league['schedule'](arguments('--pairs', 'missing:default'))
        with self.assertRaises(ValueError):
            league['manifest'](inline([dict(id='candidate', values={'focus_fire': 99999})]))
        raw = b'{"schema_version":1,"schema_version":1,"genomes":[]}'
        with self.assertRaisesRegex(ValueError, 'Duplicate JSON key'):
            league['manifest'](base64.b64encode(raw).decode())

    def test_phase_transport_uses_matching_context_and_independent_native_default(self):
        args = arguments('--research', 'on', '--technology-installments', 'off')
        args.manifest_b64 = inline([dict(id='candidate', values={}, expression=[
            dict(when={'phase': ['mid'], 'house': ['harkonnen']}, values={'focus_fire': 0})])])
        game = league['schedule'](args)[0]
        self.assertEqual(game['initial_tech'], 1)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'inputs'
            receipts = league['phase_inputs'](args, game, directory)
            self.assertEqual(len(receipts), 6)
            self.assertIn('FocusFire', (directory / '0-mid.ini').read_text())
            self.assertNotIn('FocusFire', (directory / '0-late.ini').read_text())
            self.assertNotIn('FocusFire', (directory / '1-mid.ini').read_text())
            options = configparser.ConfigParser()
            options.read(directory / 'requested-options.ini')
            self.assertTrue(options.getboolean('Game Options', 'Research Lab'))
            self.assertFalse(options.getboolean('Game Options', 'Technology Installments'))

    def test_game_rule_genes_set_one_explicit_rule_for_the_whole_game(self):
        candidate = dict(id='candidate', values={'simple_tech_savings': 1, 'technology_installments': 1}, expression=[])
        args = arguments()
        args.manifest_b64 = inline([candidate])
        games = league['schedule'](args)
        self.assertTrue(all(game['technology_installments'] == 'on' for game in games))
        self.assertTrue(all(game['game_rules'] == {'technology_installments': 1} for game in games))
        plain = league['schedule'](arguments())
        self.assertTrue(all(game['technology_installments'] == 'off' for game in plain))
        self.assertNotEqual(games[0]['comparison_id'], plain[0]['comparison_id'])
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'inputs'
            league['phase_inputs'](args, games[0], directory)
            options = configparser.ConfigParser()
            options.read(directory / 'requested-options.ini')
            self.assertTrue(options.getboolean('Game Options', 'Technology Installments'))
            self.assertNotIn('installments', (directory / '0-opening.ini').read_text().lower())
        conflicting = arguments('--technology-installments', 'off')
        conflicting.manifest_b64 = inline([candidate])
        with self.assertRaisesRegex(ValueError, 'conflicting whole-game rule'):
            league['schedule'](conflicting)
        with tempfile.TemporaryDirectory() as temporary:
            frozen = Path(temporary) / 'frozen.ini'
            frozen.write_text('[Game Options]\nTechnology Installments=false\n')
            from_file = arguments('--game-options', str(frozen))
            from_file.manifest_b64 = inline([candidate])
            with self.assertRaisesRegex(ValueError, 'conflicting whole-game rule'):
                league['schedule'](from_file)
        dependent_only = arguments()
        dependent_only.manifest_b64 = inline([dict(id='candidate', values={'simple_tech_savings': 1}, expression=[])])
        model = league['genome_model']
        registry = [dict(gene, requires={'technology_installments': 1})
                    if gene['id'] == 'simple_tech_savings' else gene for gene in model.load_registry()]
        with patch.object(model, 'load_registry', return_value=registry):
            with self.assertRaisesRegex(ValueError, 'requires game rule'):
                league['schedule'](dependent_only)

    def test_saved_phase_helper_supports_all_houses_and_actual_difficulty_variants(self):
        _, genomes, registry, _ = league['manifest'](inline())
        context = dict(mod='dunecity', difficulty='hard', format='save', size=128)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'inputs'
            receipts = league['render_phase_inputs']([(11, 'candidate')], context, genomes, directory,
                                                      registry=registry, elapsed=True, difficulty_variants=True)
            self.assertEqual(len(receipts), 15)
            self.assertEqual({row['context']['house'] for row in receipts}, {'tharpique'})
            self.assertEqual({row['context']['difficulty'] for row in receipts}, set(league['DIFFICULTIES']))
            self.assertTrue(all((directory / f'11-{difficulty}-late.ini').is_file() for difficulty in range(5)))
            self.assertIn('elapsed=true', (directory / 'schedule.ini').read_text())

    def test_frozen_options_identity_depends_on_bytes_not_local_filename(self):
        with tempfile.TemporaryDirectory() as temporary:
            first, second = Path(temporary) / 'a.ini', Path(temporary) / 'b.ini'
            first.write_text('[Game Options]\nFog of War=true\n')
            second.write_bytes(first.read_bytes())
            a = league['schedule'](arguments('--game-options', str(first)))[0]
            b = league['schedule'](arguments('--game-options', str(second)))[0]
            self.assertEqual(a['id'], b['id'])
            second.write_text('[Game Options]\nFog of War=false\n')
            changed = league['schedule'](arguments('--game-options', str(second)))[0]
            self.assertNotEqual(a['id'], changed['id'])

    def test_failed_game_compresses_trace_before_finish_and_records_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            top = Path(temporary)
            args = arguments('--results', str(top / 'results'))
            game = league['schedule'](args)[0]
            binary = top / 'probe'
            binary.write_bytes(b'test-only binary identity')
            scratch = top / 'scratch'
            scratch.mkdir()

            def native(command, **unused):
                out = Path(command[command.index('--output-dir') + 1])
                out.mkdir()
                (out / 'run.log').write_text('test fixture: engine did not complete\n')
                return subprocess.CompletedProcess(command, 1, '', 'fixture failure')

            def finish(parent, out, failed):
                self.assertTrue(failed)
                self.assertTrue((args.results / 'games.csv').is_file())
                compressed = args.results / ('evidence-' + game['id'] + '-run.log.gz')
                with gzip.open(compressed, 'rt') as stream:
                    self.assertIn('engine did not complete', stream.read())

            with patch.object(league['subprocess'], 'run', side_effect=native), patch.dict(league, finish_game=finish):
                result = league['play'](args, scratch, binary, game)
            self.assertIn('rc=1', result['error'])
            self.assertTrue((args.results / result['evidence']).is_file())
            self.assertFalse((scratch / (game['id'] + '-inputs')).exists())


if __name__ == '__main__':
    unittest.main()
