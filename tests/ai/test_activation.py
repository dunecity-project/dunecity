#!/usr/bin/env python3
"""tests/ai/activation.py and its use by vanilla-match.py --report: an arm whose behaviour never
reached its effect reads NOT TESTED, never level / beats / loses to (CLAUDE.md, "Asked-for
behaviour: an acceptance list first, and the harness refuses untested verdicts", rule 4)."""
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import activation  # noqa: E402

KEY = ('tag', 'difficulty', 'map', 'seed', 'sides')


class StyleAndPatterns(unittest.TestCase):
    def test_style_of(self):
        self.assertEqual(activation.style_of('default'), '')
        self.assertEqual(activation.style_of('default@front-turrets'), 'front-turrets')
        self.assertEqual(activation.style_of('default@front-turrets#rush'), 'front-turrets')
        self.assertEqual(activation.style_of('default@front-turrets/weak'), 'front-turrets')

    def test_patterns(self):
        self.assertEqual(activation.parse_pattern('TURRET_FRONT: rule=turret_drill'),
                         ('TURRET_FRONT:', {'rule': 'turret_drill'}))
        self.assertEqual(activation.parse_pattern('TURRET_RETIRE:'), ('TURRET_RETIRE:', {}))
        with self.assertRaises(ValueError):
            activation.parse_pattern('no marker here')
        with self.assertRaises(ValueError):
            activation.parse_pattern('TURRET_FRONT: loose-word')

    def test_registry_is_valid_and_lists_the_turret_switches(self):
        registry = activation.load_registry()
        for style in ('front-turrets', 'advancing-turrets', 'turret-creep', 'rear-turret-retire', 'turret-drill'):
            self.assertIn(style, registry)
        for style in registry:
            play_style = HERE / 'play-styles' / f'{style}.ini'
            self.assertTrue(play_style.exists(), f'{style} is registered but has no play style')

    def test_count_matches_house_and_filters(self):
        registry = {'s': {'fire': [activation.parse_pattern('TURRET_FRONT: rule=turret_drill')],
                          'success': [activation.parse_pattern('TURRET_FRONT_ENGAGED: rule=turret_drill')]}}
        lines = ['x TURRET_FRONT: house=1 rule=turret_drill x=3',
                 'x TURRET_FRONT: house=1 rule=turret_creep x=3',      # another rule
                 'x TURRET_FRONT: house=2 rule=turret_drill x=3',      # another house
                 'x TURRET_FRONT_ENGAGED: house=1 rule=turret_drill',
                 'x TURRET_FRONT_STATE: house=1 drill=1/1/1/0']        # not the marker
        self.assertEqual(activation.count(lines, {1: 's', 3: 'unregistered'}, registry), {1: (1, 1)})

    def test_infantry_share_requires_its_own_completed_purchase(self):
        registry = activation.load_registry()
        census = 'INFANTRY_MIX: house=1 on_share=1'
        buy = 'INFANTRY_BUY: house=1 builder=20 item=32 reason=city_infantry_share'
        delivered = 'INFANTRY_DELIVERY: house=1 builder=20 item=32 men=1 expected=1 cycle=10'
        count = lambda lines: activation.count(lines, {1: 'infantry-in-mix'}, registry)[1]
        self.assertEqual(count([census]), (0, 0))  # Building survivors are not purchases.
        self.assertEqual(count([buy, census]), (1, 0))  # Accepted is not delivered.
        self.assertEqual(count([delivered]), (0, 0))  # Other factory output is not a share order.
        self.assertEqual(count([buy, delivered]), (1, 1))
        self.assertEqual(count([buy, delivered, delivered]), (1, 1))
        self.assertEqual(count([buy, delivered.replace('builder=20', 'builder=21')]), (1, 0))
        self.assertEqual(count([buy, delivered.replace('house=1', 'house=2')]), (1, 0))
        self.assertEqual(count([buy, delivered.replace('item=32', 'item=44')]), (1, 0))
        self.assertEqual(count([buy, delivered.replace('men=1', 'men=0')]), (1, 0))
        self.assertEqual(count([buy, delivered.replace('expected=1', 'expected=3')]), (1, 0))
        intervening = 'INFANTRY_ORDER: house=1 builder=20 item=32'
        self.assertEqual(count([buy, intervening, delivered]), (1, 0))
        support = buy.replace('city_infantry_share', 'trooper_infantry_share')
        self.assertEqual(count([support, delivered]), (0, 0))  # The ordinary support path also runs OFF.

    def test_houses_limit_the_house_games(self):
        registry = activation.load_registry()
        self.assertEqual(registry['palace-worm-1']['houses'], {'fremen'})
        key = dict(tag='t', difficulty='brutal', map='m', seed='1', sides='s')
        label = 'default@palace-worm-1'
        houses = [{**key, 'house': h, 'label': label} for h in ('fremen', 'harkonnen', 'atreides', 'ordos')]
        events = [{**key, 'house': 'fremen', 'label': label, 'style': 'palace-worm-1', 'fired': 2, 'succeeded': 1}]
        summary = activation.summarise(houses, events, registry)[label]
        # Only Fremen can call a worm: one house-game, fired and reached its effect.
        self.assertEqual((summary.house_games, summary.fired, summary.succeeded), (1, 1, 1))
        self.assertTrue(summary.tested)
        # A Fremen house-game without a worm still counts against the style.
        summary = activation.summarise(houses, [], registry)[label]
        self.assertEqual((summary.house_games, summary.fired), (1, 0))
        self.assertFalse(summary.tested)


def write(path, fields, rows):
    with path.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)


class Report(unittest.TestCase):
    """Four-house FFA games, two houses an arm, two the control; the arm wins every game."""

    def results(self, directory, label, events):
        games, houses, rows = [], [], []
        for seed in range(1, 9):
            sides = f'harkonnen:{label}|atreides:default|ordos:{label}|sardaukar:default'
            key = dict(tag='t', difficulty='brutal', map='m', seed=str(seed), sides=sides)
            games.append({**key, 'players': '4', 'winner': '0', 'winner_label': label, 'end_s': '600', 'waves': '0',
                          'wall_s': '1', 'error': ''})
            for slot, (house, side_label) in enumerate([('harkonnen', label), ('atreides', 'default'),
                                                       ('ordos', label), ('sardaukar', 'default')], 1):
                won = house == 'harkonnen'
                houses.append({**key, 'slot': slot, 'house': house, 'label': side_label,
                               'eliminated_s': '' if won else str(100 * slot), 'place': 1 if won else slot,
                               'kill_value': '0', 'loss_value': '0'})
                if side_label == label and events(seed):
                    rows.append({**key, 'house': house, 'label': side_label, 'style': 'front-turrets',
                                 'fired': 3, 'succeeded': 2})
        write(directory / 'games.csv', ('tag', 'difficulty', 'map', 'seed', 'sides', 'players', 'winner', 'winner_label',
                                        'end_s', 'waves', 'wall_s', 'error'), games)
        write(directory / 'houses.csv', ('tag', 'difficulty', 'map', 'seed', 'sides', 'slot', 'house', 'label',
                                         'eliminated_s', 'place', 'kill_value', 'loss_value'), houses)
        write(directory / 'activation.csv', KEY + ('house', 'label') + activation.FIELDS, rows)

    def report(self, label, events):
        with tempfile.TemporaryDirectory() as tmp:
            self.results(Path(tmp), label, events)
            out = subprocess.run([sys.executable, str(HERE / 'vanilla-match.py'), '--results', tmp, '--tag', 't',
                                  '--difficulty', 'brutal', '--ffa', f'{label},default', '--report'],
                                 capture_output=True, text=True, check=True)
            return out.stdout

    def verdict_line(self, text, label):
        return next(line for line in text.splitlines()
                    if line.startswith(f'| {label} |') and ('beats' in line or 'level' in line or 'loses' in line
                                                             or 'NOT TESTED' in line))

    def test_zero_events_reads_not_tested(self):
        text = self.report('default@front-turrets', lambda seed: False)
        line = self.verdict_line(text, 'default@front-turrets')
        self.assertIn('NOT TESTED', line)
        self.assertNotIn('beats', line)
        self.assertIn('Activation', text)

    def test_effect_in_under_half_reads_not_tested(self):
        line = self.verdict_line(self.report('default@front-turrets', lambda seed: seed <= 3), 'default@front-turrets')
        self.assertIn('NOT TESTED', line)

    def test_effect_in_every_game_keeps_the_verdict(self):
        line = self.verdict_line(self.report('default@front-turrets', lambda seed: True), 'default@front-turrets')
        self.assertIn('beats', line)
        self.assertIn('effect 100%', line)

    def test_undeclared_style_is_marked(self):
        line = self.verdict_line(self.report('default@no-such-style', lambda seed: False), 'default@no-such-style')
        self.assertIn('no activation events declared', line)


if __name__ == '__main__':
    unittest.main()
