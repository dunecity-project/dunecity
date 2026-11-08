"""Control-map schedule, candidate-only style transport and receipts: no builds or games."""
import ast
import configparser
import importlib.util
from pathlib import Path
import tempfile
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
spec = importlib.util.spec_from_file_location('compare_control_maps', Path(__file__).with_name('compare-control-maps.py'))
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)

STYLE = 'mcvrunway'   # a config-only play style
SETUP = 'SCENARIO_SETUP: seed=1 cycle=0 config_hash=native style=default'


def controls(*extra):
    return matrix['parser'].parse_args(['--tier', 'controls', *extra])


def receipt(game, seats):
    return {'id': game['id'], 'sides': ' '.join(f'{h}:{t}:{c}' for h, t, c in game['sides']), 'style_seats': seats}


class ControlMapTests(unittest.TestCase):
    def test_schedule_is_fixed(self):
        games = matrix['schedule'](controls())
        self.assertEqual(len(games), 26)
        self.assertEqual(games, matrix['schedule'](controls()))
        self.assertEqual({game['seed'] for game in games}, {10801, 11810})
        self.assertEqual({game['mod'] for game in games if game['mod'] != 'vanilla'}, {'dunecity'})
        self.assertTrue(all(game['mod'] == 'dunecity' for game in games if game['cell'].startswith('12-')))
        for game in games:
            self.assertEqual(game['sides'][0][1:], (1, 'QB'))
        blake = next(game for game in games if game['cell'].startswith('04-'))
        self.assertEqual([(team, code) for _, team, code in blake['sides']], [(1, 'QB'), (1, 'QH'), (2, 'QM'), (2, 'QM')])

    def test_native_command_has_no_overlay(self):
        with tempfile.TemporaryDirectory() as scratch:
            for game in matrix['schedule'](controls()):
                command = matrix['command'](controls(), Path('probe'), game, Path(scratch) / 'g')
                self.assertNotIn('--scenario-style', command)
                self.assertNotIn('--genome-phase-dir', command)
                self.assertEqual(matrix['expected_style_seats'](controls(), game), 'none')

    def test_style_reaches_only_the_candidate_seat(self):
        args = controls('--style', STYLE)
        payload = (HARNESS.parent / 'play-styles' / f'{STYLE}.ini').read_bytes()
        with tempfile.TemporaryDirectory() as scratch:
            for game in matrix['schedule'](args):
                command = matrix['command'](args, Path('probe'), game, Path(scratch) / 'g')
                self.assertNotIn('--scenario-style', command)
                directory = Path(command[command.index('--genome-phase-dir') + 1])
                schedule = configparser.ConfigParser()
                schedule.read(directory / 'schedule.ini')
                candidate = str(matrix['HOUSE_NUMBERS'][game['sides'][0][0]])
                self.assertEqual(dict(schedule['Genomes']), {candidate: f'candidate-{STYLE}'})
                self.assertEqual(sorted(path.name for path in directory.glob('*.ini') if path.name != 'schedule.ini'),
                                 sorted(f'{candidate}-{phase}.ini' for phase in ('late', 'mid', 'opening')))
                self.assertTrue(all(path.read_bytes() == payload for path in directory.glob(f'{candidate}-*.ini')))
                self.assertEqual(matrix['expected_style_seats'](args, game), f'{candidate}=candidate-{STYLE}')

    def test_other_tiers_keep_the_all_quantbot_style(self):
        args = matrix['parser'].parse_args(['--tier', 'smoke', '--style', STYLE])
        game = next(g for g in matrix['schedule'](args) if g['kind'] == 'match')
        command = matrix['command'](args, Path('probe'), game, Path('unused'))
        self.assertEqual(command[command.index('--scenario-style') + 1], STYLE)

    def test_controls_refuse_play_style_sections_and_other_overlays(self):
        game = matrix['schedule'](controls())[0]
        with self.assertRaises(SystemExit):
            matrix['command'](controls('--style', 'boom'), Path('probe'), game, Path('unused'))
        for extra in (['--economy-samples'], ['--genome', 'g1'], ['--game-options', 'x.ini']):
            with self.assertRaises(SystemExit):
                matrix['schedule'](controls(*extra))

    def test_seat_receipt_from_the_log(self):
        seats = matrix['style_seats']
        native = [SETUP, 'SCENARIO_AI_CONFIG: house=0 config_hash=native',
                  'SCENARIO_AI_CONFIG: house=1 config_hash=native']
        self.assertEqual(seats(native), 'none')
        candidate = [SETUP, 'SCENARIO_AI_CONFIG: house=0 config_hash=changed',
                     'SCENARIO_AI_CONFIG: house=1 config_hash=native',
                     f'GENOME_PHASE: house=0 genome=candidate-{STYLE} phase=opening cycle=0 config_hash=changed']
        self.assertEqual(seats(candidate), f'0=candidate-{STYLE}')
        leaked = candidate[:2] + ['SCENARIO_AI_CONFIG: house=1 config_hash=changed'] + candidate[3:]
        self.assertEqual(seats(leaked), f'0=candidate-{STYLE} 1=config')
        everyone = [SETUP.replace('style=default', f'style={STYLE}'), 'SCENARIO_AI_CONFIG: house=0 config_hash=changed']
        self.assertEqual(seats(everyone), f'style={STYLE} 0=config')
        self.assertEqual(seats(['no receipt']), '')

    def test_comparator_refuses_non_candidate_seats(self):
        games = matrix['schedule'](controls())
        check = compare.check_style_seats
        self.assertEqual(check([receipt(g, 'none') for g in games], required=True), '')
        good = [receipt(g, f'{matrix["HOUSE_NUMBERS"][g["sides"][0][0]]}=candidate-{STYLE}') for g in games]
        self.assertEqual(check(good, required=True), STYLE)
        ally = next(g for g in games if g['cell'].startswith('04-'))
        wrong = [receipt(ally, f'{matrix["HOUSE_NUMBERS"][ally["sides"][1][0]]}=candidate-{STYLE}')]
        for rows in (wrong, [dict(good[0], style_seats=good[0]['style_seats'] + ' 2=config')],
                     [dict(good[0], style_seats=f'style={STYLE} 0=config')],
                     [dict(good[0], style_seats='')], good[:1] + [receipt(games[1], 'none')]):
            with self.assertRaises(ValueError):
                check(rows, required=True)
        self.assertEqual(check([{k: v for k, v in receipt(games[0], 'none').items() if k != 'style_seats'}],
                               required=False), '')


if __name__ == '__main__':
    unittest.main()
