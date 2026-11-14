"""Exercise real CTest admission using actual single-game scheduling properties.

The tiny workers stand in for games; native game correctness and contention are
validated separately on the frozen shared build. No game is compiled here.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CASES = ('quantbot_custom_attack', 'quantbot_police_placement', 'quantbot_battery_clearance')
WORKER = '''import fcntl, sys, time
with open(sys.argv[1], 'a') as log:
    for event in ('start', 'end'):
        fcntl.flock(log, fcntl.LOCK_EX)
        log.write(event + ' ' + sys.argv[2] + '\\n'); log.flush()
        fcntl.flock(log, fcntl.LOCK_UN)
        if event == 'start': time.sleep(.25)
'''


@unittest.skipUnless(shutil.which('ctest') and sys.platform != 'win32', 'requires CTest and flock')
class SchedulingTests(unittest.TestCase):
    def peak(self, slots):
        source = (ROOT / 'tests/CMakeLists.txt').read_text()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'worker.py').write_text(WORKER)
            lines = []
            for name in CASES:
                properties = re.search(r'set_tests_properties\(' + name + r'\s+PROPERTIES[^)]*\)', source)[0]
                lines += [f'add_test({name} "{sys.executable}" "{root}/worker.py" "{root}/events" {name})', properties]
            (root / 'CTestTestfile.cmake').write_text('\n'.join(lines))
            subprocess.run(['ctest', '--test-dir', directory, '--parallel', str(slots), '--output-on-failure'],
                           check=True, capture_output=True, text=True, timeout=15)
            active, peak = set(), 0
            for line in (root / 'events').read_text().splitlines():
                event, name = line.split()
                if event == 'start':
                    self.assertNotIn(name, active)
                    active.add(name)
                    peak = max(peak, len(active))
                else:
                    active.remove(name)
            self.assertFalse(active)
            return peak

    def test_mini_overlaps_two_games_but_never_a_third(self):
        self.assertEqual(self.peak(4), 2)

    def test_hosted_budget_does_not_oversubscribe_games(self):
        self.assertEqual(self.peak(2), 1)


if __name__ == '__main__':
    unittest.main()
