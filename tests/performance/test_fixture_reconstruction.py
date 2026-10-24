"""A saved benchmark must remain reproducible after graphics-only replacements."""
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/performance'))
spec = importlib.util.spec_from_file_location('game_perf', ROOT / 'tests/performance/game-perf.py')
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class SavedBenchmarkTests(unittest.TestCase):
    def test_exact_saved_revisions_survive_current_art_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            save, profile = benchmark.build_eight_house_fixture(Path(temporary))
            self.assertEqual(save.read_bytes(), gzip.decompress(
                (benchmark.EIGHT_HOUSE / 'fpsstillfucked.dls.gz').read_bytes()))
            versions = json.loads((benchmark.EIGHT_HOUSE / 'versions.json').read_text())
            for revision in versions:
                root = profile / 'workshop/revisions' / revision
                manifest = (root / 'manifest').read_bytes()
                self.assertEqual(hashlib.sha256(manifest).hexdigest(), revision)
                count = 0
                for line in manifest.decode().splitlines():
                    if not line.startswith('file='):
                        continue
                    digest, size, encoded = line[5:].split(',', 2)
                    data = (root / 'files' / bytes.fromhex(encoded).decode()).read_bytes()
                    self.assertEqual(len(data), int(size))
                    self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
                    count += 1
                self.assertGreater(count, 0)


if __name__ == '__main__':
    unittest.main()
