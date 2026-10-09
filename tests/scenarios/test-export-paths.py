"""A converter must never replace its input with its scenario or report."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class InputPreservationTests(unittest.TestCase):
    def test_conflicting_output_paths_leave_save_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            save = root / 'match.dls'
            contents = b'private saved world sentinel'
            save.write_bytes(contents)
            for arguments in (['--output', str(save)], ['--report', str(save)],
                              ['--output', str(root/'same.ini'), '--report', str(root/'same.ini')]):
                result = subprocess.run([sys.executable, str(ROOT/'tools/scenarios/export-save-scenario.py'),
                                         '--save', str(save), '--work-dir', str(root/'scratch'),
                                         '--force', *arguments], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('must be distinct paths', result.stderr)
                self.assertEqual(save.read_bytes(), contents)

if __name__ == '__main__':
    unittest.main()
