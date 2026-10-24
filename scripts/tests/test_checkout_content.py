"""Packaged text must have the same bytes on every checkout platform."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class CheckoutContentTests(unittest.TestCase):
    def test_windows_and_unix_checkouts_preserve_canonical_bytes(self):
        attributes = (Path(__file__).resolve().parents[2] / ".gitattributes").read_bytes()
        files = {
            "mods/dunecity/graphics_skins/Dune2/building.ini": b"[Building]\nFrames = 8\n",
            "mods/Dune2R/asset.json": b'{"frames":8}\n',
            "config/ObjectData.ini.default": b"[Infantry]\nSpeed = 1\n",
            "mods/dunecity/sprite.png": b"\x89PNG\r\n\x1a\n\0binary\r\n",
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            (source / ".gitattributes").write_bytes(attributes)
            for name, data in files.items():
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            def git(*args):
                subprocess.run(["git", "-C", str(source), *args], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            git("add", ".")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")
            for autocrlf in ("true", "false"):
                target = root / autocrlf
                subprocess.run(["git", "-c", f"core.autocrlf={autocrlf}", "clone", "-q",
                                "--no-local", str(source), str(target)], check=True)
                for name, data in files.items():
                    self.assertEqual((target / name).read_bytes(), data,
                                     f"{autocrlf}: {name}")


if __name__ == "__main__":
    unittest.main()
