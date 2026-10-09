"""A converter must never replace its input with its scenario or report."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SENTINEL = b'private saved world sentinel'

class InputPreservationTests(unittest.TestCase):
    def convert(self, root, save, *arguments):
        """Run the exporter with --force and a build tree that cannot exist."""
        return subprocess.run([sys.executable, str(ROOT/'tools/scenarios/export-save-scenario.py'),
                               '--save', str(save), '--work-dir', str(root/'scratch'),
                               '--build-dir', str(root/'no-such-build'),
                               '--force', *arguments], capture_output=True, text=True)

    def assertRefused(self, root, save, *arguments):
        """The run must stop on the identity guard with the save untouched."""
        result = self.convert(root, save, *arguments)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('must be distinct paths', result.stderr)
        self.assertEqual(save.read_bytes(), SENTINEL)

    def test_conflicting_output_paths_leave_save_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            save = root / 'match.dls'
            save.write_bytes(SENTINEL)
            for arguments in (['--output', str(save)], ['--report', str(save)],
                              ['--output', str(root/'same.ini'), '--report', str(root/'same.ini')]):
                self.assertRefused(root, save, *arguments)

    def test_hardlink_alias_of_the_save_is_refused(self):
        # A hardlink resolves to its own name, so only filesystem identity
        # distinguishes it from the save it shares an inode with.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            save = root / 'match.dls'
            save.write_bytes(SENTINEL)
            alias = root / 'alias.ini'
            try:
                os.link(save, alias)
            except (OSError, NotImplementedError) as error:
                self.skipTest('This filesystem has no hardlinks: ' + str(error))
            self.assertNotEqual(str(alias.resolve()), str(save.resolve()))
            self.assertRefused(root, save, '--output', str(alias))
            self.assertRefused(root, save, '--report', str(alias))

    def test_symlink_alias_of_the_save_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            save = root / 'match.dls'
            save.write_bytes(SENTINEL)
            alias = root / 'alias.ini'
            try:
                alias.symlink_to(save)
            except (OSError, NotImplementedError) as error:
                self.skipTest('This filesystem has no symlinks: ' + str(error))
            self.assertRefused(root, save, '--output', str(alias))
            self.assertRefused(root, save, '--report', str(alias))

    def test_case_alias_of_the_save_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            save = root / 'match.dls'
            save.write_bytes(SENTINEL)
            alias = root / 'MATCH.DLS'
            if not alias.exists():
                self.skipTest('This filesystem is case-sensitive')
            self.assertRefused(root, save, '--output', str(alias))
            self.assertRefused(root, save, '--report', str(alias))

    def test_distinct_destinations_pass_the_identity_guard(self):
        # The negative control: a real new output and report must get past the
        # guard, so the refusals above cannot be passing for the wrong reason.
        # The run still fails later on the absent build tree, without writing.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            save = root / 'match.dls'
            save.write_bytes(SENTINEL)
            existing = root / 'previous.ini'
            existing.write_bytes(b'an earlier scenario that --force may replace')
            for output in (root / 'scenario.ini', existing):
                result = self.convert(root, save, '--output', str(output),
                                      '--report', str(root/'report.json'))
                self.assertNotIn('must be distinct paths', result.stderr)
                self.assertEqual(save.read_bytes(), SENTINEL)

if __name__ == '__main__':
    unittest.main()
