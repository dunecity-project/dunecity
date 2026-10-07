"""Catalogs must describe immutable Git bytes, including published art updates."""
import configparser
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/generate-dune2r-asset-catalog.py"
spec = importlib.util.spec_from_file_location("asset_catalog", SCRIPT)
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


class CatalogRepository:
    """A throwaway asset repository whose commit dates the test chooses."""

    def __init__(self, directory):
        self.path = Path(directory)
        self.unit = self.path / "mods/Dune2R/graphics_hd/units/gravel"
        (self.unit / "compact").mkdir(parents=True)
        (self.unit / "tile.ini").write_bytes(b"[Tile]\nFull=compact/full.png\n")
        self.git("init")
        self.git("config", "user.name", "Catalog test")
        self.git("config", "user.email", "catalog@example.invalid")

    def git(self, *args, date=None):
        environment = dict(os.environ)
        if date is not None:
            environment["GIT_AUTHOR_DATE"] = date
            environment["GIT_COMMITTER_DATE"] = date
        return subprocess.check_output(
            ["git", *args], cwd=self.path, stderr=subprocess.DEVNULL, text=True, env=environment
        ).strip()

    def publish(self, tile, message, date=None):
        (self.unit / "compact/full.png").write_bytes(tile)
        self.git("add", ".")
        self.git("commit", "-m", message, date=date)
        return self.git("rev-parse", "HEAD")

    def commit_date(self, revision):
        return self.git("show", "-s", "--format=%ct", revision)


class AssetCatalogTests(unittest.TestCase):
    def test_catalog_pins_committed_bytes_and_updates_with_a_new_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = CatalogRepository(directory)
            original = b"original published tile"
            first = repo.publish(original, "Original art")
            updated = b"seamless published replacement tile"
            second = repo.publish(updated, "Seam fix")
            # An unrelated working edit must never leak into a public catalog.
            (repo.unit / "compact/full.png").write_bytes(b"unpublished local experiment")
            (repo.unit / "compact/local-only.png").write_bytes(b"not committed")
            published = None
            versions = []
            for revision, expected in ((first, original), (second, updated)):
                with self.subTest(revision=revision):
                    published = catalog.build_catalog(repo.path, revision, published)
                    result = configparser.ConfigParser(interpolation=None)
                    result.read_string(published)
                    self.assertEqual(result["Catalog"]["Revision"], revision)
                    self.assertIn("/" + revision + "/", result["Catalog"]["BaseURL"])
                    self.assertTrue(result["Catalog"]["BaseURL"].startswith(
                        "https://raw.githubusercontent.com/dunecity-project/dunecity/"))
                    self.assertEqual(result["Catalog"]["PackCount"], "1")
                    self.assertEqual(result["Pack.0"]["FileCount"], "2")
                    self.assertEqual(result["Pack.0"]["File.0"],
                        "compact/full.png|" + str(len(expected)) + "|" + hashlib.sha256(expected).hexdigest())
                    self.assertNotIn("local-only", published)
                    versions.append(int(result["Catalog"]["Version"]))
            # A changed target is a new publication, and the first one starts the count.
            self.assertEqual(versions, [1, 2])

    def test_publications_are_ordered_when_commit_dates_are_equal_or_reversed(self):
        for description, dates in (
            ("equal commit dates", ("2026-05-01T12:00:00+00:00", "2026-05-01T12:00:00+00:00")),
            ("reversed commit dates", ("2026-05-02T12:00:00+00:00", "2026-04-01T12:00:00+00:00")),
        ):
            with self.subTest(description), tempfile.TemporaryDirectory() as directory:
                repo = CatalogRepository(directory)
                first = repo.publish(b"first published tile", "First", date=dates[0])
                second = repo.publish(b"second published tile", "Second", date=dates[1])
                # The dates really are unusable as an order.
                self.assertLessEqual(int(repo.commit_date(second)), int(repo.commit_date(first)))
                before = catalog.build_catalog(repo.path, first)
                after = catalog.build_catalog(repo.path, second, before)
                self.assertEqual(catalog.catalog_version(before), 1)
                self.assertEqual(catalog.catalog_version(after), 2)

    def test_regenerating_an_unchanged_target_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = CatalogRepository(directory)
            revision = repo.publish(b"published tile", "Art")
            first = catalog.build_catalog(repo.path, revision)
            second = catalog.build_catalog(repo.path, revision, first)
            third = catalog.build_catalog(repo.path, revision, second)
            self.assertEqual(first, second)
            self.assertEqual(second, third)
            self.assertEqual(catalog.catalog_version(third), 1)

    def test_changed_metadata_at_the_same_revision_is_a_new_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = CatalogRepository(directory)
            revision = repo.publish(b"published tile", "Art")
            published = catalog.build_catalog(repo.path, revision)
            self.assertEqual(catalog.catalog_version(published), 1)
            # Same revision and same bytes, but the catalog describes the pack
            # differently: that is a different target and needs its own number.
            renamed = published.replace("DisplayName=Gravel Terrain Remastered",
                                        "DisplayName=Gravel Terrain")
            self.assertNotEqual(renamed, published)
            self.assertEqual(catalog.catalog_version(catalog.build_catalog(repo.path, revision, renamed)), 2)
            # A comment-only difference is not a target change.
            recommented = published.replace("# URLs are pinned", "# Pinned URLs")
            self.assertEqual(catalog.catalog_version(catalog.build_catalog(repo.path, revision, recommented)), 1)

    def test_a_legacy_catalog_without_a_counter_starts_at_one(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = CatalogRepository(directory)
            revision = repo.publish(b"published tile", "Art")
            published = catalog.build_catalog(repo.path, revision)
            legacy = "\n".join(
                line for line in published.splitlines() if not line.startswith("Version=")
            )
            self.assertEqual(catalog.catalog_version(legacy), 0)
            self.assertEqual(catalog.catalog_version(catalog.build_catalog(repo.path, revision, legacy)), 1)
            # Even a changed target only ever starts the count at 1 after a legacy file.
            changed = repo.publish(b"replacement tile", "Replacement")
            self.assertEqual(catalog.catalog_version(catalog.build_catalog(repo.path, changed, legacy)), 1)

    def test_the_shipped_catalog_is_a_positive_publication_of_its_own_target(self):
        repo = Path(__file__).resolve().parents[1]
        text = (repo / "mods/Dune2R/asset-catalog.ini").read_text(encoding="ascii")
        shipped = configparser.ConfigParser(interpolation=None)
        shipped.read_string(text)
        self.assertGreater(catalog.catalog_version(text), 0)
        self.assertEqual(int(shipped["Catalog"]["Version"]), catalog.catalog_version(text))
        try:
            catalog.git_revision(repo, shipped["Catalog"]["Revision"] + "^{commit}")
        except subprocess.CalledProcessError:
            self.skipTest("the pinned asset commit is not present in this clone")
        # Regenerating the checked-in catalog keeps both its target and its number.
        self.assertEqual(catalog.build_catalog(repo, shipped["Catalog"]["Revision"], text), text)


if __name__ == "__main__":
    unittest.main()
