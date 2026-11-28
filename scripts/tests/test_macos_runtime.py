import importlib.util
import io
from contextlib import redirect_stdout
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import hashlib
import json
import os
import re
import shlex
import sys
import zipfile

spec = importlib.util.spec_from_file_location("macos_runtime", Path(__file__).parents[1] / "verify-macos-runtime.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


staging_spec = importlib.util.spec_from_file_location(
    "package_staging_macos", Path(__file__).parents[1] / "package-staging-macos.py")
staging = importlib.util.module_from_spec(staging_spec)
staging_spec.loader.exec_module(staging)


# Portable stand-ins for macOS-only tools. These exercise the real helper and
# failure/promotion boundary, not actual hdiutil, signatures or runtime coverage.
MAC_PACKAGE_TOOL = r'''
import json
import os
from pathlib import Path
import plistlib
import re
import sys
import zipfile

name = Path(sys.argv[0]).name
args = sys.argv[1:]
if args and args[0] == "verify":
    name = args.pop(0)
with open(os.environ["MAC_PACKAGE_COMMANDS"], "a") as stream:
    stream.write(json.dumps(dict(tool=name, args=args, cwd=os.getcwd())) + "\n")
if name == "ditto":
    assert args[:2] == ["-x", "-k"], args
    with zipfile.ZipFile(args[2]) as archive:
        archive.extractall(args[3])
elif name == "cpack":
    config = Path(args[args.index("--config") + 1]).read_text()
    values = {match[0]: match[2] for match in
              re.findall(r"set\((\w+) \[(=*)\[(.*?)\]\2\]\)", config, re.S)}
    assert args[args.index("-G") + 1] == "DragNDrop"
    assert args[args.index("-C") + 1] == "Release"
    assert values["CPACK_INSTALL_CMAKE_PROJECTS"] == ""
    assert values["CPACK_STRIP_FILES"] == "OFF"
    assert values["CPACK_INSTALLED_DIRECTORIES"].endswith(";/")
    assert Path(values["CPACK_RESOURCE_FILE_LICENSE"]).is_file()
    assert Path(values["CPACK_RESOURCE_FILE_README"]).is_file()
    output = Path(args[args.index("-B") + 1])
    target = output / (values["CPACK_PACKAGE_FILE_NAME"] + ".dmg")
    if os.environ.get("MAC_PACKAGE_OUTPUT") == "missing":
        sys.exit(0)
    if os.environ.get("MAC_PACKAGE_OUTPUT") == "empty":
        target.touch()
        sys.exit(0)
    volume = Path(values["CPACK_INSTALLED_DIRECTORIES"][:-2])
    with zipfile.ZipFile(target, "w") as archive:
        for path in volume.rglob("*"):
            if path.is_file():
                archive.write(path, path.relative_to(volume))
        archive.writestr("Applications", "/Applications")
        archive.writestr("_fixture_recipe.cmake", config)
    if os.environ.get("MAC_PACKAGE_FAIL_CPACK"):
        sys.exit(23)
elif name == "verify":
    with zipfile.ZipFile(args[0]) as archive:
        assert archive.read("Applications") == b"/Applications"
        assert archive.read("README") == b"Repository readme\n"
        assert archive.read("dunecity.app/Contents/MacOS/dunecity") == b"compiled fixture game"
        assert archive.read("dunecity.app/Contents/_CodeSignature/CodeResources") == b"unchanged app signature"
        info = plistlib.loads(archive.read("dunecity.app/Contents/Info.plist"))
        assert info["CFBundleShortVersionString"] == "1.2.3"
    if os.environ.get("MAC_PACKAGE_MUTATE_ZIP"):
        with open(os.environ["MAC_PACKAGE_MUTATE_ZIP"], "ab") as stream:
            stream.write(b"changed during packaging")
    if os.environ.get("MAC_PACKAGE_FAIL_VERIFY"):
        sys.exit(24)
else:
    raise AssertionError("Unexpected build/signing command: " + name)
'''


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



class MacStagingPackagingTests(unittest.TestCase):
    SHA = "a" * 40
    VERSION = "1.2.3"

    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="mac package fixtures ")
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name).resolve()
        self.source = self.root / "source"
        self.directory = self.root / "artifacts"
        self.tools = self.root / "tools"
        self.scratch = self.root / "scratch"
        for path in (self.source, self.directory, self.tools, self.scratch):
            path.mkdir()
        (self.source / "CMakeLists.txt").write_text(
            f"project(DuneCity VERSION {self.VERSION} LANGUAGES C CXX)\n")
        (self.source / "COPYING").write_text("Repository license\n")
        (self.source / "README.md").write_text("Repository readme\n")
        (self.source / "release_notes").mkdir()
        (self.source / "release_notes" / "RELEASE_NOTES.md").write_text("Repository release notes\n")
        (self.source / "packaging").mkdir()
        for filename in ("dmg_background.png", "DMGSetup.scpt"):
            (self.source / "packaging" / filename).write_bytes(b"layout fixture")
        self.commands = self.root / "commands.jsonl"
        self.commands.touch()
        tool = self.tools / "fixture-tool.py"
        tool.write_text(f"#!{sys.executable}\n" + MAC_PACKAGE_TOOL)
        tool.chmod(0o755)
        for name in ("ditto", "cpack", "cmake", "ninja", "codesign", "xcrun"):
            (self.tools / name).symlink_to(tool)
        (self.source / "scripts").mkdir()
        (self.source / "scripts" / "verify-macos-dmg.sh").write_text(
            "set -eu\nexec " + shlex.quote(sys.executable) + " " + shlex.quote(str(tool)) + ' verify "$1"\n')
        self.environment = dict(
            PATH=str(self.tools) + os.pathsep + os.environ.get("PATH", ""),
            RUNNER_TEMP=str(self.scratch), MAC_PACKAGE_COMMANDS=str(self.commands),
            MAC_PACKAGE_OUTPUT="", MAC_PACKAGE_FAIL_CPACK="", MAC_PACKAGE_FAIL_VERIFY="",
            MAC_PACKAGE_MUTATE_ZIP="", PYTHONDONTWRITEBYTECODE="1")
        self.archive = self.directory / f"DuneCity-{self.VERSION}-macOS.zip"
        self.dmg = self.archive.with_suffix(".dmg")
        self.marker = self.directory / "macos-dmg-deferred.json"
        self.metadata_path = self.directory / "build-inputs-macos-arm64.json"
        self.metadata = dict(sourceCommit=self.SHA, releaseBuild="OFF",
                             configuration={"DUNECITY_RELEASE_BUILD:BOOL": "OFF"})
        self.metadata_path.write_text(json.dumps(self.metadata))
        self.write_archive()
        self.deferred = dict(schema=1, sourceCommit=self.SHA, version=self.VERSION,
                             zip=self.archive.name, sha256=hashlib.sha256(self.archive.read_bytes()).hexdigest())
        self.marker.write_text(json.dumps(self.deferred))

    def write_archive(self, version=VERSION):
        info = dict(CFBundleShortVersionString=version, CFBundleVersion=version,
                    CFBundleExecutable="dunecity", CFBundleIdentifier="net.dunecity.DuneCity")
        with zipfile.ZipFile(self.archive, "w") as archive:
            archive.writestr("dunecity.app/Contents/Info.plist", plistlib.dumps(info))
            archive.writestr("dunecity.app/Contents/MacOS/dunecity", b"compiled fixture game")
            archive.writestr("dunecity.app/Contents/Resources/DUNE.PAK", b"game data")
            archive.writestr("dunecity.app/Contents/_CodeSignature/CodeResources", b"unchanged app signature")

    def invoke(self, **environment):
        with patch.dict(os.environ, dict(self.environment, **environment)):
            return staging.complete_package(self.directory, self.SHA, self.source)

    def command_log(self):
        return [json.loads(line) for line in self.commands.read_text().splitlines()]

    def test_exact_zip_becomes_normal_cpack_dmg_without_rebuilding_or_signing(self):
        before = self.archive.read_bytes()
        timestamp = self.archive.stat().st_mtime_ns
        marker = self.marker.read_bytes()
        self.assertEqual(self.invoke(), self.dmg)
        self.assertEqual(self.archive.read_bytes(), before)
        self.assertEqual(self.archive.stat().st_mtime_ns, timestamp)
        self.assertEqual(self.marker.read_bytes(), marker)
        self.assertEqual([row["tool"] for row in self.command_log()], ["ditto", "cpack", "verify"])
        self.assertTrue(all(row["cwd"] == str(self.source) for row in self.command_log()))
        self.assertFalse((self.source / "build").exists())
        self.assertEqual(list(self.scratch.iterdir()), [])
        with zipfile.ZipFile(self.dmg) as archive:
            self.assertEqual(archive.read("Release Notes.md"), b"Repository release notes\n")
            self.assertEqual(archive.read("dunecity.app/Contents/_CodeSignature/CodeResources"),
                             b"unchanged app signature")
            config = archive.read("_fixture_recipe.cmake").decode()
        values = {match[0]: match[2] for match in
                  re.findall(r"set\((\w+) \[(=*)\[(.*?)\]\2\]\)", config, re.S)}
        self.assertEqual(values["CPACK_DMG_VOLUME_NAME"], "Dune City " + self.VERSION)
        self.assertEqual(values["CPACK_DMG_FORMAT"], "UDZO")
        self.assertEqual(values["CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE"], "ON")
        self.assertEqual(values["CPACK_DMG_DISABLE_APPLICATIONS_SYMLINK"], "OFF")
        self.assertEqual(values["CPACK_RESOURCE_FILE_LICENSE"], str(self.source / "COPYING"))
        self.assertEqual(values["CPACK_RESOURCE_FILE_README"], str(self.source / "README.md"))
        self.assertEqual(values["CPACK_DMG_BACKGROUND_IMAGE"], str(self.source / "packaging/dmg_background.png"))
        self.assertEqual(values["CPACK_DMG_DS_STORE_SETUP_SCRIPT"], str(self.source / "packaging/DMGSetup.scpt"))
        self.assertEqual(values["CPACK_INSTALL_CMAKE_PROJECTS"], "")

    def test_complete_legacy_artifact_needs_no_marker_or_repackaging(self):
        with zipfile.ZipFile(self.archive) as source, zipfile.ZipFile(self.dmg, "w") as target:
            for name in source.namelist():
                target.writestr(name, source.read(name))
            target.writestr("README", b"Repository readme\n")
            target.writestr("Applications", b"/Applications")
        self.marker.unlink()
        before = self.archive.read_bytes()
        dmg_before = self.dmg.read_bytes()
        self.assertEqual(self.invoke(), self.dmg)
        self.assertEqual([row["tool"] for row in self.command_log()], ["verify"])
        self.assertEqual(self.archive.read_bytes(), before)
        self.assertEqual(self.dmg.read_bytes(), dmg_before)

    def test_missing_dmg_without_marker_is_not_a_rebuild_fallback(self):
        self.marker.unlink()
        with self.assertRaisesRegex(ValueError, "without a valid deferred marker"):
            self.invoke()
        self.assertEqual(self.command_log(), [])
        self.assertFalse(self.dmg.exists())

    def test_marker_contract_rejects_wrong_source_digest_version_name_or_schema(self):
        cases = [
            dict(self.deferred, sourceCommit="b" * 40),
            dict(self.deferred, sha256="b" * 64),
            dict(self.deferred, sha256="not-a-digest"),
            dict(self.deferred, version="1.2.4"),
            dict(self.deferred, zip="../" + self.archive.name),
            dict(self.deferred, schema=2),
            dict(self.deferred, schema=True),
            dict(self.deferred, unexpected="not schema 1"),
            {key: value for key, value in self.deferred.items() if key != "sha256"},
            [],
        ]
        for marker in cases:
            with self.subTest(marker=marker):
                self.marker.write_text(json.dumps(marker))
                with self.assertRaises(ValueError):
                    self.invoke()
                self.assertEqual(self.command_log(), [])
                self.assertFalse(self.dmg.exists())
        self.marker.write_text("{partial JSON")
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertEqual(self.command_log(), [])

    def test_source_and_both_off_identities_remain_mandatory(self):
        for changed in (
                dict(sourceCommit="b" * 40), dict(releaseBuild="ON"),
                dict(configuration={"DUNECITY_RELEASE_BUILD:BOOL": "ON"}),
                dict(configuration={}), dict(configuration=None)):
            with self.subTest(changed=changed):
                self.metadata_path.write_text(json.dumps(dict(self.metadata, **changed)))
                with self.assertRaisesRegex(ValueError, "source/OFF identity"):
                    self.invoke()
                self.assertEqual(self.command_log(), [])
        self.metadata_path.write_text(json.dumps(self.metadata))
        with self.assertRaisesRegex(ValueError, "full source SHA"):
            with patch.dict(os.environ, self.environment):
                staging.complete_package(self.directory, self.SHA[:12], self.source)
        self.assertEqual(self.command_log(), [])

    def test_archive_digest_and_unambiguous_basename_are_required(self):
        extra = self.directory / "DuneCity-1.2.4-macOS.zip"
        extra.write_bytes(self.archive.read_bytes())
        with self.assertRaisesRegex(ValueError, "Expected exactly"):
            self.invoke()
        extra.unlink()
        self.archive.rename(extra)
        with self.assertRaisesRegex(ValueError, "Expected exactly"):
            self.invoke()
        extra.rename(self.archive)
        with self.archive.open("ab") as stream:
            stream.write(b"post-marker drift")
        with self.assertRaisesRegex(ValueError, "ZIP digest"):
            self.invoke()
        self.assertEqual(self.command_log(), [])

    def test_bundle_version_must_match_even_when_archive_marker_authenticates(self):
        self.write_archive("1.2.4")
        self.marker.write_text(json.dumps(dict(
            self.deferred, sha256=hashlib.sha256(self.archive.read_bytes()).hexdigest())))
        with self.assertRaisesRegex(ValueError, "app identity/version"):
            self.invoke()
        self.assertEqual([row["tool"] for row in self.command_log()], ["ditto"])
        self.assertFalse(self.dmg.exists())

    def test_missing_or_empty_cpack_output_is_never_promoted(self):
        for output in ("missing", "empty"):
            with self.subTest(output=output):
                with self.assertRaisesRegex(ValueError, "package input"):
                    self.invoke(MAC_PACKAGE_OUTPUT=output)
                self.assertFalse(self.dmg.exists())
        self.assertNotIn("verify", [row["tool"] for row in self.command_log()])

    def test_failed_packaging_cannot_masquerade_as_a_successful_retry(self):
        before = self.archive.read_bytes()
        with self.assertRaises(subprocess.CalledProcessError):
            self.invoke(MAC_PACKAGE_FAIL_CPACK="1")
        self.assertFalse(self.dmg.exists())
        self.assertEqual(self.archive.read_bytes(), before)
        self.assertEqual([row["tool"] for row in self.command_log()], ["ditto", "cpack"])
        self.assertEqual(self.invoke(), self.dmg)
        self.assertEqual(self.archive.read_bytes(), before)
        self.assertEqual([row["tool"] for row in self.command_log()],
                         ["ditto", "cpack", "ditto", "cpack", "verify"])

    def test_failed_real_verifier_command_blocks_promotion_until_retry_succeeds(self):
        before = self.archive.read_bytes()
        with self.assertRaises(subprocess.CalledProcessError):
            self.invoke(MAC_PACKAGE_FAIL_VERIFY="1")
        self.assertFalse(self.dmg.exists())
        self.assertEqual(self.archive.read_bytes(), before)
        self.assertEqual(self.invoke(), self.dmg)
        self.assertEqual(self.archive.read_bytes(), before)
        self.assertEqual([row["tool"] for row in self.command_log()],
                         ["ditto", "cpack", "verify", "ditto", "cpack", "verify"])

    def test_archive_mutation_during_packaging_cannot_publish_a_dmg(self):
        with self.assertRaisesRegex(ValueError, "ZIP changed"):
            self.invoke(MAC_PACKAGE_MUTATE_ZIP=str(self.archive))
        self.assertFalse(self.dmg.exists())

    def test_cli_executes_helper_without_a_cmake_tree(self):
        result = subprocess.run(
            [sys.executable, str(Path(staging.__file__)), "--directory", str(self.directory),
             "--source-sha", self.SHA, "--source-root", str(self.source)],
            env=dict(os.environ, **self.environment), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Verified staging Mac package set:", result.stdout)
        self.assertTrue(self.dmg.is_file())
        self.assertEqual([row["tool"] for row in self.command_log()], ["ditto", "cpack", "verify"])

    def test_workflow_defers_only_initial_integration_and_retains_marker(self):
        workflow = (Path(__file__).parents[2] / ".github/workflows/build.yml").read_text()
        mac = workflow.split("  build-macos:\n", 1)[1].split("  build-linux:\n", 1)[0]
        flag = next(line.strip() for line in mac.splitlines() if "DUNECITY_MAC_DEFER_DMG:" in line)
        self.assertEqual(flag, "DUNECITY_MAC_DEFER_DMG: ${{ github.event_name == 'push' && "
                         "github.ref == 'refs/heads/integrate/feature-batch' && "
                         "needs.select-inputs.outputs.macos_run == '' && 'true' || 'false' }}")
        self.assertIn('run: python3 scripts/package-staging-macos.py --directory build --source-sha "$SOURCE_SHA"', mac)
        self.assertEqual(mac.count("            build/macos-dmg-deferred.json\n"), 2)
        self.assertIn("          max-size: 2G\n", mac)
        self.assertIn("          digest-mismatch: error\n", mac)


if __name__ == "__main__":
    unittest.main()
