"""Pure input/capture checks; never compile or run a game."""
import base64
import hashlib
import importlib.util
import json
import tempfile
import unittest
from unittest import mock
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
        path, digest = league.style_receipt(SimpleNamespace(style='research-lab-progression-off'))
        self.assertEqual(hashlib.sha256(Path(path).read_bytes()).hexdigest(), digest)
        for name in ('../outside', 'research-lab-progression-off.ini'):
            with self.assertRaises(ValueError):
                league.style_receipt(SimpleNamespace(style=name))

    def test_private_windows_demangler_does_not_require_itanium_headers(self):
        source = league.probe_source()
        self.assertNotIn('#if defined( __clang__ ) || defined(__GNUG__) || defined( __GLIBCXX__ ) || defined( __GLIBCPP__ )', source)
        self.assertIn('#define BASELINE_HAS_GENOME_PHASE ', source)

    def test_posix_probe_uses_platform_link_target_and_native_data_directory(self):
        for platform in ('darwin', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                build, scratch = Path(directory) / 'build', Path(directory) / 'scratch'
                build.mkdir()
                mac = platform == 'darwin'
                target = 'bin/dunecity.app/Contents/MacOS/dunecity' if mac else 'bin/dunecity'
                main_object = 'src/CMakeFiles/dunecity.dir/main.cpp.o'
                compile_line = '/usr/bin/c++ -MD -MT native -MF native.d -o ' + main_object + ' -c /native/src/main.cpp'
                expanded = ': && /usr/bin/c++ ' + main_object + ' game.o -o ' + target + ' && :'
                commands = compile_line + '\n' + (expanded if mac else
                    ': && /usr/bin/c++ @CMakeFiles/dunecity.rsp -o bin/dunecity && :')
                calls = []
                def recipe(argv, **kwargs):
                    calls.append(argv)
                    if argv[-2:] == ['compdb', '-x']:
                        return json.dumps([dict(command=expanded)])
                    self.assertEqual(argv[-1], target)
                    return commands
                with mock.patch.object(league.sys, 'platform', platform), \
                     mock.patch.object(league, 'probe_source', return_value='int main() {}'), \
                     mock.patch.object(league.subprocess, 'check_output', side_effect=recipe), \
                     mock.patch.object(league.subprocess, 'run') as run:
                    binary = league.build_probe(SimpleNamespace(build_dir=build), scratch)
                self.assertEqual(len(calls), 1 if mac else 2)
                compile_argv, link_argv = [call.args[0] for call in run.call_args_list]
                self.assertIn(str(scratch / 'baseline-main.cpp'), compile_argv)
                self.assertIn(str(scratch / 'baseline-main.o'), link_argv)
                self.assertNotIn(main_object, link_argv)
                self.assertFalse(any(arg.endswith('.rsp') for arg in link_argv))
                self.assertEqual(binary.parent, scratch / 'ai-baseline-probe.app/Contents/MacOS'
                                 if mac else build / 'bin')

    def test_linux_private_source_includes_iterator_traits_compatibility(self):
        with mock.patch.object(league.sys, 'platform', 'linux'):
            source = league.probe_source()
        self.assertIn('robustlist-iterator-traits.inc', source)
        self.assertLess(source.index('robustlist-iterator-traits.inc'),
                        source.index('ai-baseline-observer.inc'))

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

    @staticmethod
    def genome_args(item, **extra):
        manifest = dict(schema_version=1, genomes=[item])
        encoded = base64.urlsafe_b64encode(json.dumps(manifest).encode()).decode()
        return SimpleNamespace(manifest_b64=encoded, genome=item['id'], game_options=None,
                               style=None, genome_phase_dir=None, phase_minutes=[0, 10, 20], **extra)

    def test_inline_genome_is_resolved_for_exact_native_houses_and_phases(self):
        import genome_model
        item = dict(id='candidate', values={'focus_fire': 0}, expression=[
            dict(when=dict(phase=['late'], house=['atreides']), values={'focus_fire': 1})])
        args = self.genome_args(item)
        game = next(game for game in league.fixture_games() if game['mod'] == 'Dune2R'
                    and game['format'] == '1v1')
        with tempfile.TemporaryDirectory() as directory:
            snapshot, digest = league.genome_receipt(args, Path(directory), game)
            snapshot = Path(snapshot)
            self.assertEqual(json.loads((snapshot / 'genome.json').read_text()), item)
            self.assertIn('genome_digest=' + genome_model.genome_id(item),
                          (snapshot / 'schedule.ini').read_text())
            names = {number: name for name, number in league.HOUSE_NUMBERS.items()}
            for house, _ in game['roster']:
                for phase in genome_model.PHASES:
                    context = dict(mod='dune2r', difficulty=game['difficulty'],
                                   format='1v1', size=128, house=names[house], phase=phase)
                    self.assertEqual((snapshot / ('%d-%s.ini' % (house, phase))).read_text(),
                                     genome_model.render_ini(item, context))
            self.assertEqual(len(digest), 64)

    def test_inline_genome_cannot_change_historical_game_rules(self):
        args = self.genome_args(dict(id='installments', values={'technology_installments': 1}))
        game = next(game for game in league.fixture_games() if game['mod'] == 'dunecity'
                    and game.get('research', 'off') == 'off')
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'frozen historical fixture'):
                league.genome_receipt(args, Path(directory), game)

    def test_inline_genome_requires_frozen_manifest_and_exclusive_treatment(self):
        args = self.genome_args(dict(id='default', values={}))
        args.style = 'research-lab-progression-on'
        with self.assertRaisesRegex(ValueError, 'cannot be mixed'):
            league.inline_genome(args)
        args.style = None
        args.genome = 'unknown'
        with self.assertRaisesRegex(ValueError, 'Unknown inline genome'):
            league.inline_genome(args)
        args.genome = None
        with self.assertRaisesRegex(ValueError, 'supplied together'):
            league.inline_genome(args)

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
