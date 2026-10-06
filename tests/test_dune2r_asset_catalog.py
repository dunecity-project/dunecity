"""Catalogs must describe immutable Git bytes, including published art updates."""
import configparser
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/generate-dune2r-asset-catalog.py"
spec = importlib.util.spec_from_file_location("asset_catalog", SCRIPT)
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


class AssetCatalogTests(unittest.TestCase):
    def test_catalog_pins_committed_bytes_and_updates_with_a_new_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            def git(*args):
                return subprocess.check_output(["git", *args], cwd=repo, stderr=subprocess.DEVNULL, text=True).strip()
            git("init")
            git("config", "user.name", "Catalog test")
            git("config", "user.email", "catalog@example.invalid")
            unit = repo / "mods/Dune2R/graphics_hd/units/gravel"
            (unit / "compact").mkdir(parents=True)
            (unit / "tile.ini").write_bytes(b"[Tile]\nFull=compact/full.png\n")
            original = b"original published tile"
            (unit / "compact/full.png").write_bytes(original)
            git("add", ".")
            git("commit", "-m", "Original art")
            first = git("rev-parse", "HEAD")
            updated = b"seamless published replacement tile"
            (unit / "compact/full.png").write_bytes(updated)
            git("add", ".")
            git("commit", "-m", "Seam fix")
            second = git("rev-parse", "HEAD")
            # An unrelated working edit must never leak into a public catalog.
            (unit / "compact/full.png").write_bytes(b"unpublished local experiment")
            (unit / "compact/local-only.png").write_bytes(b"not committed")
            for revision, expected in ((first, original), (second, updated)):
                with self.subTest(revision=revision):
                    result = configparser.ConfigParser(interpolation=None)
                    result.read_string(catalog.build_catalog(repo, revision))
                    self.assertEqual(result["Catalog"]["Revision"], revision)
                    self.assertIn("/" + revision + "/", result["Catalog"]["BaseURL"])
                    self.assertTrue(result["Catalog"]["BaseURL"].startswith(
                        "https://raw.githubusercontent.com/dunecity-project/dunecity/"))
                    self.assertEqual(result["Catalog"]["PackCount"], "1")
                    self.assertEqual(result["Pack.0"]["FileCount"], "2")
                    self.assertEqual(result["Pack.0"]["File.0"],
                        "compact/full.png|" + str(len(expected)) + "|" + hashlib.sha256(expected).hexdigest())
                    self.assertNotIn("local-only", catalog.build_catalog(repo, revision))


if __name__ == "__main__":
    unittest.main()
