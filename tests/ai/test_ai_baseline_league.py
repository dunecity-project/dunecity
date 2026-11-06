"""Pure input/capture checks; never compile or run a game."""
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

MODULE = Path(__file__).with_name('ai-baseline-league.py')
spec = importlib.util.spec_from_file_location('ai_baseline_league', MODULE)
league = importlib.util.module_from_spec(spec)
spec.loader.exec_module(league)


class NativeLeagueInputs(unittest.TestCase):
    def test_fixture_bytes_and_rosters(self):
        games = league.fixture_games()
        self.assertTrue(games)
        self.assertEqual(len(games), len({game['id'] for game in games}))
        for game in games:
            self.assertEqual(hashlib.sha256((league.fixtures / 'maps' / game['map']).read_bytes()).hexdigest(),
                             game['map_sha256'])
            options = league.option_receipt(league.fixtures / game['game_options']['file'])
            self.assertEqual(options['sha256'], game['game_options']['sha256'])
            self.assertEqual(len(game['roster']), len({h for h, _ in game['roster']}))
            self.assertEqual(game['initial_tech'], 1 if game.get('research') == 'on' else 9)
            self.assertEqual(options['research'], game.get('research', 'off'))
            self.assertEqual(game['minutes'], 40)

    def test_schedule_does_not_relabel_research_on(self):
        args = SimpleNamespace(mod='vanilla', seeds=[9101], difficulties=['hard'], research='off')
        games = league.schedule(args)
        self.assertTrue(games)
        self.assertTrue(all(game['mod'] == 'vanilla' and game['seed'] == 9101
                            and game['difficulty'] == 'hard' for game in games))
        self.assertTrue(all(game['game_options']['city_effects'] is False for game in games))

    def test_supported_research_has_separate_exact_contexts(self):
        args = SimpleNamespace(mod='dunecity', seeds=[9101], difficulties=['hard'], research='on')
        games = league.schedule(args)
        self.assertEqual(len(games), 4)
        self.assertTrue(all(game['initial_tech'] == 1 and game['research'] == 'on' for game in games))
        self.assertTrue(all(game['id'].endswith('/t1') for game in games))
        args.research = 'off'
        controls = league.schedule(args)
        self.assertFalse({game['id'] for game in games} & {game['id'] for game in controls})

    def test_bounded_power_cohort_keeps_exact_contexts(self):
        args = SimpleNamespace(mod='dunecity', seeds=[9101, 148680876], difficulties=['medium', 'hard', 'brutal'],
                               research='off', formats=['1v1'], spice=['medium'])
        games = league.schedule(args)
        self.assertEqual(len(games), 6)
        self.assertTrue(all(game['format'] == '1v1' and game['spice_level'] == 'medium' for game in games))

    def test_style_receipt_is_an_exact_committed_file(self):
        self.assertEqual(league.style_receipt(SimpleNamespace()), ('', ''))
        path, digest = league.style_receipt(SimpleNamespace(style='existing-composition-unit-choice-off'))
        self.assertEqual(hashlib.sha256(Path(path).read_bytes()).hexdigest(), digest)
        for name in ('../outside', 'existing-composition-unit-choice-off.ini'):
            with self.assertRaises(ValueError):
                league.style_receipt(SimpleNamespace(style=name))

    def test_private_windows_demangler_does_not_require_itanium_headers(self):
        source = league.probe_source()
        self.assertNotIn('#if defined( __clang__ ) || defined(__GNUG__) || defined( __GLIBCXX__ ) || defined( __GLIBCPP__ )', source)
        self.assertIn('#define BASELINE_HAS_GENOME_PHASE ', source)

    def test_natives_never_count_as_a_contesting_team(self):
        # isAlive() is always true for the Natives house (worms, crime troopers).
        # Without this exclusion no 1v1 can end once natives exist.
        house = (league.root / 'include/House.h').read_text()
        source = league.probe_source()
        enabled = int('isNatives()' in house)
        self.assertIn('#define BASELINE_HAS_NATIVES %d\n' % enabled, source)
        observer = (league.root / 'tests/ai/ai-baseline-observer.inc').read_text()
        winner = observer[observer.index('inline int survivingTeam'):observer.index('inline void digest')]
        self.assertIn('#if BASELINE_HAS_NATIVES', winner)
        self.assertIn('if(house->isNatives()) continue;', winner)

    def test_genome_inputs_are_snapshotted_and_hashed_by_name_and_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            inputs, out = parent / 'inputs', parent / 'out'
            inputs.mkdir()
            out.mkdir()
            (inputs / 'schedule.ini').write_text('[Schedule]\nopening=0\nmid=10\nlate=20\n')
            (inputs / '0-opening.ini').write_text('[QuantBot]\n')
            args = SimpleNamespace(genome_phase_dir=inputs, style=None)
            snapshot, digest = league.genome_receipt(args, out)
            expected = hashlib.sha256()
            for path in sorted(inputs.glob('*.ini')):
                expected.update(path.name.encode('utf-8') + b'\0' + path.read_bytes() + b'\0')
                self.assertEqual((Path(snapshot) / path.name).read_bytes(), path.read_bytes())
            self.assertEqual(digest, expected.hexdigest())
            (inputs / '0-opening.ini').write_text('changed after snapshot\n')
            self.assertNotEqual((Path(snapshot) / '0-opening.ini').read_bytes(), (inputs / '0-opening.ini').read_bytes())
            with self.assertRaises(ValueError):
                league.genome_receipt(SimpleNamespace(genome_phase_dir=inputs, style='gene-on'), out)

    def capture(self, events, cycle=100):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)
            (profile / 'events.jsonl').write_text(''.join(json.dumps(event) + '\n' for event in events))
            return league.capture(profile, cycle, [(0, 1), (1, 2)])

    @staticmethod
    def events():
        def event(seq, name, data, house=-1):
            return dict(schema_version=1, session='fixture', seq=seq, cycle=100,
                        house=house, event=name, data=data)
        return [event(1, 'session_start', {}),
                event(2, 'ground_hunt', {'members': 3, 'value': 1500}, 0),
                event(3, 'game_summary', {'houses': {'0': {}, '1': {}}}),
                event(4, 'session_end', {})]

    def test_exact_dispatch_requires_complete_stream(self):
        result = self.capture(self.events())
        self.assertEqual(result['status'], 'complete')
        self.assertEqual(result['waves'], [dict(house=0, cycle=100, members=3, value=1500)])

    def test_gap_never_means_zero_dispatches(self):
        events = self.events()
        events.pop(1)
        result = self.capture(events)
        self.assertEqual(result['status'], 'incomplete')
        self.assertIn('sequence_gap', result['reasons'])

    def test_short_and_partial_native_roster_fail_closed(self):
        self.assertEqual(self.capture(self.events(), cycle=101)['status'], 'incomplete')
        events = self.events()
        events[2]['data']['houses'].pop('1')
        result = self.capture(events)
        self.assertIn('incomplete_native_roster', result['reasons'])

    def test_capture_budget_exhaustion_is_not_complete(self):
        events = self.events()
        events[1]['event'] = 'capture_limit'
        result = self.capture(events)
        self.assertEqual(result['status'], 'incomplete')
        self.assertIn('capture_limit', result['reasons'])


if __name__ == '__main__':
    unittest.main()
