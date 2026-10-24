import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile


script = Path(__file__).resolve().parents[1] / "verify-android-payload.py"
spec = importlib.util.spec_from_file_location("android_payload_check", script)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class AndroidPayloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "build-android-apk/app/build/outputs/apk/debug"
        self.files = {
            "data/Righteous-Regular.ttf": b"font fixture",
            "data/Righteous-OFL.txt": b"license fixture",
            "config/Dune City.ini": b"[Video]\nUI Skin = 2\n",
            "mods/dunecity/mod.ini": b"[Mod]\nVersion = 1\n",
        }
        for name, content in self.files.items():
            self.write(name, content)
            self.write("build-android-payload/" + name, content)
        self.write("android-version.json", json.dumps({"versionName":"0.2.31","versionCode":1000551}).encode())
        self.metadata = {"applicationId":"net.dunecity.dune2r",
                         "elements":[{"versionName":"0.2.31","versionCode":1000551}]}
        self.output.mkdir(parents=True)
        self.binaries = {}
        for name in ("libmain.so", "libc++_shared.so"):
            elf = b"\x7fELF" + bytes(14) + (183).to_bytes(2,"little")
            self.binaries[name] = elf
            self.write("build-android-apk/app/src/main/jniLibs/arm64-v8a/"+name, elf)

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def invoke(self, extra=None):
        (self.output / "output-metadata.json").write_text(json.dumps(self.metadata))
        with zipfile.ZipFile(self.output / "DuneLegacy.apk", "w") as archive:
            for name, content in self.files.items():
                archive.writestr("assets/dune2r_payload/"+name, content)
            for name, content in self.binaries.items():
                archive.writestr("lib/arm64-v8a/"+name, content)
            if extra:
                archive.writestr("assets/dune2r_payload/unexpected.bin", b"extra")
        with patch.object(sys, "argv", [str(script),"--root",str(self.root)]), \
             contextlib.redirect_stdout(io.StringIO()):
            checker.main()

    def test_matching_payload_passes(self):
        self.invoke()

    def test_wrong_application_id_fails(self):
        self.metadata["applicationId"] = "wrong.application"
        with self.assertRaisesRegex(RuntimeError, "Application ID changed"):
            self.invoke()

    def test_wrong_version_fails(self):
        self.metadata["elements"][0]["versionCode"] = 1000550
        with self.assertRaisesRegex(RuntimeError, "version code mismatch"):
            self.invoke()

    def test_source_mismatch_fails(self):
        self.write("mods/dunecity/mod.ini", b"changed source")
        with self.assertRaisesRegex(RuntimeError, "Source mismatch"):
            self.invoke()

    def test_extra_payload_fails(self):
        with self.assertRaisesRegex(RuntimeError, "Unexpected APK payload entries"):
            self.invoke(extra=True)

    def test_wrong_native_architecture_fails(self):
        self.binaries["libmain.so"] = b"\x7fELF" + bytes(16)
        with self.assertRaisesRegex(RuntimeError, "Incorrect native architecture"):
            self.invoke()


if __name__ == "__main__":
    unittest.main()
