#!/usr/bin/env python3
"""The 1.0.807 promotion check: paired win counts on one deck, invalid inputs rejected."""
import contextlib
import csv
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('ai_baseline_gate', Path(__file__).with_name('ai-baseline-gate.py'))
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)
FIELDS = ('id', 'mod', 'difficulty', 'seed', 'research', 'map_sha256', 'options_sha256', 'source_commit',
          'baseline_release_source', 'outcome', 'error')
CURRENT = 'c' * 40


def deck(outcomes, source=CURRENT, release=CURRENT, **changes):
    return [dict(id=f'vanilla/matrix/1v1/{n}', mod='vanilla', difficulty='hard', seed=str(9101 + n),
                 research='off', map_sha256=f'map{n}', options_sha256='options', source_commit=source,
                 baseline_release_source=release, outcome=outcome, error='', **changes)
            for n, outcome in enumerate(outcomes)]


def history(outcomes):
    return deck(outcomes, source='b' * 40, release=gate.RELEASE_807)


class BaselineCheckTest(unittest.TestCase):
    def check(self, candidate, off, reference):
        with tempfile.TemporaryDirectory() as tmp:
            arms = []
            for name, rows in (('on', candidate), ('off', off), ('807', reference)):
                path = Path(tmp) / name
                path.mkdir()
                with open(path / 'games.csv', 'w', newline='') as stream:
                    writer = csv.DictWriter(stream, FIELDS)
                    writer.writeheader()
                    writer.writerows(rows)
                arms.append(str(path))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = gate.main(['--candidate', arms[0], '--off', arms[1], '--reference', arms[2]])
            return code, out.getvalue().strip()

    def test_equal_wins_pass_and_807_is_information_only(self):
        result = self.check(deck(['decided', 'time_limit']), deck(['time_limit', 'decided']),
                            history(['decided', 'decided']))
        self.assertEqual((0, 'PASS, candidate 1 / OFF 1 / 807 2 (wins of 2 games)'), result)

    def test_fewer_wins_than_off_fail(self):
        result = self.check(deck(['time_limit', 'time_limit']), deck(['decided', 'time_limit']),
                            history(['time_limit', 'time_limit']))
        self.assertEqual((1, 'FAIL, candidate 0 / OFF 1 / 807 0 (wins of 2 games)'), result)

    def test_another_deck_is_invalid(self):
        moved = deck(['decided', 'decided'])
        moved[1]['map_sha256'] = 'other'
        code, line = self.check(moved, deck(['decided', 'decided']), history(['decided', 'decided']))
        self.assertEqual(2, code)
        self.assertIn('candidate vanilla/matrix/1v1/1: map_sha256 differs from 1.0.807', line)
        code, line = self.check(deck(['decided']), deck(['decided', 'decided']), history(['decided', 'decided']))
        self.assertEqual((2, 'INVALID: candidate did not play the 1.0.807 deck (1 games differ)'), (code, line))

    def test_failed_games_mixed_sources_and_wrong_reference_are_invalid(self):
        failed = deck(['decided', 'time_limit'])
        failed[0]['error'] = 'probe crashed'
        code, line = self.check(failed, deck(['decided', 'decided']), history(['decided', 'decided']))
        self.assertEqual(2, code)
        self.assertIn('candidate vanilla/matrix/1v1/0: probe crashed', line)
        code, line = self.check(deck(['decided'], source='d' * 40), deck(['decided']), history(['decided']))
        self.assertEqual(2, code)
        self.assertIn('one source commit', line)
        code, line = self.check(deck(['decided']), deck(['decided']), deck(['decided']))
        self.assertEqual(2, code)
        self.assertIn('1.0.807 vanilla/matrix/1v1/0: native source', line)


if __name__ == '__main__':
    unittest.main()
