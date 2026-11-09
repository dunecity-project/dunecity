"""Control-map schedule, candidate-only style transport and receipts: no builds or games."""
import argparse
import ast
import configparser
import contextlib
import csv
from fractions import Fraction
import importlib.util
import io
import json
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


def gate_rows(style, winners=(), changed=()):
    """Complete synthetic 26-game receipt: team 1 wins the ids in winners, team 2 the rest;
    ids in changed get a different final digest."""
    rows = []
    for index, game in enumerate(matrix['schedule'](controls())):
        house = game['sides'][0][0]
        rows.append({'id': game['id'], 'category': game['category'], 'cell': game['cell'], 'seed': str(game['seed']),
                     'mod': game['mod'], 'map': game['map'], 'limit': str(game['minutes']),
                     'sides': ' '.join(f'{h}:{t}:{c}' for h, t, c in game['sides']),
                     'winner_team': '1' if game['id'] in winners else '2', 'winner': 'QB', 'minutes': '10.00',
                     'digest': f'{index + (1000 if game["id"] in changed else 0):016x}', 'error': '',
                     'source_commit': 'a' * 40,
                     'style_seats': f'{matrix["HOUSE_NUMBERS"][house]}=candidate-{style}' if style else 'none'})
    return rows


def run_compare(native_rows, candidate_rows, **options):
    with tempfile.TemporaryDirectory() as scratch:
        baseline = Path(scratch) / 'baseline.json'
        baseline.write_text(json.dumps({'schema': 1, 'base': compare.BASE, 'style': 'native', 'games': native_rows}))
        results = Path(scratch) / 'games.csv'
        with results.open('w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=list(candidate_rows[0]))
            writer.writeheader()
            writer.writerows(candidate_rows)
        args = argparse.Namespace(baseline=str(baseline), results=str(results), in_scope=list(range(1, 14)),
                                  min_kept_ratio=Fraction(3, 4))
        vars(args).update(options)
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            code = compare.compare(args)
        return code, output.getvalue()


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
        outpost = [game for game in games if game['cell'].startswith('13-')]
        self.assertTrue(all(game['map'] == '4P - 128x128 - The Sardaukar Outpost' for game in outpost))
        self.assertEqual([house for house, _, _ in outpost[0]['sides']],
                         ['harkonnen', 'mercenary', 'atreides', 'ordos'])
        self.assertEqual(outpost[1]['sides'][0][0], 'mercenary')
        self.assertEqual({game['minutes'] for game in outpost}, {120})
        for game in games:
            if not game['cell'].startswith('13-'):
                large = '192x192' in game['map'] or '256x256' in game['map']
                self.assertEqual(game['minutes'], 80 if large else 60, game['id'])

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

    def test_observed_handmade_study_is_explicit_and_can_use_fresh_seeds(self):
        args = controls('--control-diagnostics', '--economy-samples', '--control-mod', 'dunecity',
                        '--control-seeds', '1180100', '1180101',
                        '--cells', '05-mountain-pass|06-wormhole|09-fertile-basin|10-enclaves')
        games = matrix['schedule'](args)
        self.assertEqual(len(games), 8)
        self.assertEqual({g['seed'] for g in games}, {1180100, 1180101})
        self.assertTrue(all(g['mod'] == 'dunecity' for g in games))
        self.assertTrue(all(g['sides'][0][1:] == (1, 'QB') for g in games))

    def test_diagnostic_seed_and_mod_overrides_cannot_change_a_gate(self):
        for flags in (['--control-seeds', '1180100'], ['--control-mod', 'dunecity'],
                      ['--control-diagnostics', '--check'],
                      ['--control-diagnostics', '--control-seeds', '1', '1'],
                      ['--control-diagnostics', '--control-seeds', '-1']):
            with self.assertRaises(SystemExit):
                matrix['schedule'](controls(*flags))
        args = matrix['parser'].parse_args(['--tier', 'full', '--control-diagnostics'])
        with self.assertRaises(SystemExit):
            matrix['schedule'](args)

    def test_comparator_rejects_observed_diagnostics_before_gate_validation(self):
        with self.assertRaisesRegex(ValueError, 'Observed diagnostic'):
            compare.validate([{'id': 'controls/diagnostic', 'control_diagnostics': '1'}])

    def test_shared_fleet_parser_retains_censoring_and_unknown_geometry(self):
        houses, refineries = matrix['fleet_observations']([
            'DUNECITY_FLEET: house=0 cycle=0 minute=0 fleet={"harvester_trip_cycles_min":-1,'
            '"harvester_trip_cycles_max":-1,"fleet_geometry_cycle":-1,"refined_spice_raw":123}',
            'DUNECITY_REFINERY: house=0 refinery=42 cycle=0 minute=0 geometry_cycle=-1 x=3 y=4',
        ])
        row = houses[(0, 0)]
        self.assertEqual(row['refined_spice_raw'], 123)
        self.assertEqual(row['harvester_trip_cycles_min'], '')
        self.assertEqual(refineries[0]['nearest_spice_distance_steps'], '')
        self.assertIn('"refinery":"42"', row['fleet_refinery_metrics'])

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

    def test_comparator_requires_three_quarters_of_all_native_wins(self):
        games = [game['id'] for game in matrix['schedule'](controls())]
        wins = [gid for gid in games if gid.split('/')[1][:2] in ('01', '02', '03', '13')]
        native = gate_rows('', winners=wins)
        self.assertEqual(len(wins), 8)
        code, text = run_compare(native, gate_rows(STYLE, winners=wins[2:], changed=wins[:2]))
        self.assertEqual(code, 0, text)
        self.assertIn('Native QB/team-1 wins on maps 1-13: 8; kept 6; ratio 0.750 (rule >= 75%): PASS', text)
        self.assertEqual(text.count('LOST: '), 2)
        self.assertIn('Controls: PASS (26 games; 0 violations; native wins kept 6/8)', text)
        code, text = run_compare(native, gate_rows(STYLE, winners=wins[3:], changed=wins[:3]))
        self.assertEqual(code, 1)
        self.assertIn('FAIL: native wins kept 5/8 below 75%', text)
        code, text = run_compare(native, gate_rows(STYLE, winners=wins[1:], changed=wins[:1]),
                                 min_kept_ratio=Fraction(1))
        self.assertEqual(code, 1)
        self.assertIn('FAIL: native wins kept 7/8 below 100%', text)

    def test_outpost_digest_changes_are_not_a_promotion_veto(self):
        outpost = [game['id'] for game in matrix['schedule'](controls()) if game['cell'].startswith('13-')]
        code, text = run_compare(gate_rows('', winners=outpost),
                                 gate_rows(STYLE, winners=outpost, changed=outpost))
        self.assertEqual(code, 0, text)
        self.assertIn('native wins kept 2/2', text)

    def test_outpost_native_win_loss_counts_normally(self):
        games = [game['id'] for game in matrix['schedule'](controls())]
        outpost = [gid for gid in games if gid.startswith('controls/13-')]
        wins = games[:2] + outpost
        code, text = run_compare(gate_rows('', winners=wins), gate_rows(STYLE, winners=wins[:-1], changed=outpost))
        self.assertEqual(code, 0, text)
        self.assertIn('native wins kept 3/4', text)
        self.assertIn(f'LOST: {outpost[-1]}:', text)
        code, text = run_compare(gate_rows('', winners=wins), gate_rows(STYLE, winners=wins[:-2], changed=outpost))
        self.assertEqual(code, 1)
        self.assertIn('native wins kept 2/4 below 75%', text)

    def test_old_silicon_receipts_cannot_qualify_as_outpost(self):
        rows = gate_rows('')
        for row in rows:
            if row['id'].startswith('controls/13-'):
                row.update(map='4P - 64x64 - Silicon Valley',
                           id=row['id'].replace('the-sardaukar-outpost', 'silicon-valley'))
        with self.assertRaisesRegex(ValueError, 'Incomplete/wrong controls'):
            compare.validate(rows)
        # Relabelling only the id must not bypass the authored map/setup identity.
        for row in rows:
            row['id'] = row['id'].replace('silicon-valley', 'the-sardaukar-outpost')
        with self.assertRaisesRegex(ValueError, 'wrong scheduled map'):
            compare.validate(rows)

    def test_sixty_minute_outpost_rows_cannot_satisfy_the_120_minute_schedule(self):
        rows = gate_rows('')
        outpost = [row for row in rows if row['id'].startswith('controls/13-')]
        for row in outpost:
            row.update(limit='60', minutes='60.00', winner_team='0', winner='')
        with self.assertRaisesRegex(ValueError, "wrong scheduled limit: '60' != '120'"):
            compare.validate(rows)
        # Relabelling the old draws' limit must not pass them off as 120-minute games.
        for row in outpost:
            row['limit'] = '120'
        with self.assertRaisesRegex(ValueError, 'Draw ended before its 120-minute limit'):
            compare.validate(rows)
        for row in outpost:
            row['minutes'] = '120.00'
        self.assertEqual(len(compare.validate(rows)), 26)

    def test_pairing_requires_the_same_source_on_each_map_seed(self):
        native, candidate = gate_rows(''), gate_rows(STYLE)
        candidate[-1]['source_commit'] = 'b' * 40
        code, text = run_compare(native, candidate)
        self.assertEqual(code, 1)
        self.assertIn('different native/candidate source pins', text)


if __name__ == '__main__':
    unittest.main()
