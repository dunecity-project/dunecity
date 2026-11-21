import importlib.util
import io
from contextlib import redirect_stdout
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("macos_runtime", Path(__file__).parents[1] / "verify-macos-runtime.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class MacRuntimePackagingTests(unittest.TestCase):
    def test_successful_static_sdl_needs_no_dylibs(self):
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.CompletedProcess([], 0, "Desktop runtime initialization and rendering passed\n", "")
            with patch.object(runtime.subprocess, "run", return_value=result), redirect_stdout(io.StringIO()):
                runtime.verify(Path(folder) / "dunecity.app")

    def test_external_sdl_is_rejected_despite_successful_render(self):
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.CompletedProcess([], 0,
                "SDL runtime library: /opt/homebrew/lib/libSDL2.dylib\nDesktop runtime initialization and rendering passed\n", "")
            with patch.object(runtime.subprocess, "run", return_value=result):
                with self.assertRaisesRegex(RuntimeError, "outside the app"):
                    runtime.verify(Path(folder) / "dunecity.app")

    def test_compat_without_sdl3_fails_before_launch(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder) / "dunecity.app"
            libraries = app / "Contents/Frameworks"
            libraries.mkdir(parents=True)
            (libraries / "libSDL2.dylib").write_bytes(b"SDL2COMPAT_DEBUG_LOGGING")
            with patch.object(runtime.subprocess, "run") as launch:
                with self.assertRaisesRegex(RuntimeError, "missing its runtime dependency"):
                    runtime.verify(app)
                launch.assert_not_called()


class MacPackagingOrderTests(unittest.TestCase):
    """The signed bundle is launched only after its ticket is stapled, and never written afterwards."""

    def package(self, folder, failing=None):
        spec = importlib.util.spec_from_file_location("package_signed_macos", Path(__file__).parents[1] / "package-signed-macos.py")
        packaging = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(packaging)
        commands = []

        def record(command, **_):
            commands.append(command)
            if failing and failing in " ".join(command): raise subprocess.CalledProcessError(1, command)
            if command[0] == "hdiutil": Path(command[-1]).write_bytes(b"")
            return subprocess.CompletedProcess(command, 0, "", "")

        installed = Path(folder) / "install/dunecity.app"
        (installed / "Contents").mkdir(parents=True)
        with (installed / "Contents/Info.plist").open("wb") as f: plistlib.dump({"CFBundleShortVersionString": "9.9.9"}, f)
        argv = ["package-signed-macos.py", "--app", str(installed), "--output", str(Path(folder) / "signed"), "--identity", "ID",
                "--signing-keychain", "K", "--notary-profile", "P", "--notary-keychain", "K"]
        with patch.object(packaging.subprocess, "run", side_effect=record), \
             patch.object(packaging, "notarize", side_effect=lambda path, *_: commands.append(["notarize", str(path)])), \
             patch.object(packaging.sys, "argv", argv), redirect_stdout(io.StringIO()):
            try:
                packaging.main()
            except subprocess.CalledProcessError as error:
                return commands, error
        return commands, None

    def test_first_launch_follows_staple_and_nothing_writes_the_bundle_after_it(self):
        with tempfile.TemporaryDirectory() as folder:
            commands, error = self.package(folder)
        self.assertIsNone(error)

        def position(*words):
            return next(i for i, command in enumerate(commands) if all(word in " ".join(command) for word in words))

        launch = position("verify-macos-runtime.py", "volume/dunecity.app")
        self.assertLess(position("notarize", "submission.zip"), position("stapler staple", "volume/dunecity.app"))
        self.assertLess(position("stapler validate", "volume/dunecity.app"), launch)
        self.assertLess(launch, position("ditto -c", "DuneCity-9.9.9-macOS.zip"))
        for command in commands[launch + 1:]:
            if command[-1].endswith("dunecity.app"):
                self.assertEqual(command[:2], ["spctl", "--assess"], command)

    def test_failed_runtime_check_after_staple_makes_no_final_package(self):
        with tempfile.TemporaryDirectory() as folder:
            commands, error = self.package(folder, failing="verify-macos-runtime.py")
            packages = list((Path(folder) / "signed").glob("*-macOS.*"))
        self.assertIsNotNone(error)
        self.assertIn("verify-macos-runtime.py", " ".join(commands[-1]))
        self.assertTrue(any(command[:3] == ["xcrun", "stapler", "staple"] for command in commands))
        self.assertFalse(any(command[0] == "hdiutil" or "-macOS.zip" in " ".join(command) for command in commands))
        self.assertEqual(packages, [])


if __name__ == "__main__":
    unittest.main()
