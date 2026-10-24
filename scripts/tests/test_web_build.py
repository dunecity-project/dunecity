"""Offline rejection checks: unsafe paths must not execute an SDK installer."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[2]


class WebBuildSafetyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        (self.root / "tools/web").mkdir(parents=True)
        for name in ("build-emscripten.sh", "emsdk-version.txt", "emsdk-revision.txt"):
            shutil.copyfile(SOURCE / "tools/web" / name, self.root / "tools/web" / name)
        self.sdk = Path(self.temp.name) / "sdk"
        self.sdk.mkdir()
        self.marker = Path(self.temp.name) / "installer-executed"
        (self.sdk / "emsdk").write_text('#!/bin/sh\ntouch "$TEST_MARKER"\n')
        (self.sdk / "emsdk").chmod(0o755)
        self.env = dict(os.environ, EMSDK_DIR=str(self.sdk),
                        BUILD_DIR=str(Path(self.temp.name) / "build"),
                        TEST_MARKER=str(self.marker))

    def run_script(self):
        result = subprocess.run(["bash", str(self.root / "tools/web/build-emscripten.sh")],
                                env=self.env, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.marker.exists(), result.stdout + result.stderr)
        return result.stderr

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.sdk), *args], text=True).strip()

    def init_sdk(self, origin):
        self.git("init", "-q")
        self.git("remote", "add", "origin", origin)
        self.git("add", "emsdk")
        self.git("-c", "user.name=Build fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "core.hooksPath=/dev/null", "commit", "-qm", "fixture")

    def test_refuses_source_and_ancestor_before_sdk_execution(self):
        for path in (self.root, self.root / "tools/..", self.root.parent, Path.home()):
            with self.subTest(path=path):
                self.env["BUILD_DIR"] = str(path)
                self.assertIn("Refusing unsafe build directory", self.run_script())
                self.assertTrue((self.root / "tools/web/build-emscripten.sh").exists())

    def test_refuses_wrong_sdk_origin(self):
        self.init_sdk("https://example.invalid/not-emsdk.git")
        self.assertIn("unexpected origin", self.run_script())

    def test_refuses_wrong_sdk_revision(self):
        self.init_sdk("https://github.com/emscripten-core/emsdk.git")
        self.assertIn("unreviewed revision", self.run_script())

    def test_refuses_modified_installer_even_at_pinned_revision(self):
        self.init_sdk("https://github.com/emscripten-core/emsdk.git")
        (self.root / "tools/web/emsdk-revision.txt").write_text(self.git("rev-parse", "HEAD") + "\n")
        with (self.sdk / "emsdk").open("a") as out:
            out.write("# modified after pinning\n")
        self.assertIn("modified tracked files", self.run_script())

    def test_installs_sdk_dependencies_even_when_list_advertises_version(self):
        # The real pinned installer does not filter list output with --installed.
        # Activation must never run first just because the version is advertised.
        version = (self.root / "tools/web/emsdk-version.txt").read_text().strip()
        (self.sdk / "emsdk").write_text(
            '#!/bin/sh\ncase "$1" in\n'
            f'list) printf "%s\\n" "{version}" ;;\n'
            'install) printf "install %s\\n" "$2" >> "$TEST_MARKER" ;;\n'
            'activate) printf "activate %s\\n" "$2" >> "$TEST_MARKER"; exit 73 ;;\n'
            '*) exit 74 ;;\nesac\n')
        (self.sdk / "emsdk").chmod(0o755)
        self.init_sdk("https://github.com/emscripten-core/emsdk.git")
        (self.root / "tools/web/emsdk-revision.txt").write_text(self.git("rev-parse", "HEAD") + "\n")
        result = subprocess.run(["bash", str(self.root / "tools/web/build-emscripten.sh")],
                                env=self.env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 73, result.stdout + result.stderr)
        self.assertEqual(self.marker.read_text().splitlines(),
                         [f"install {version}", f"activate {version}"])


if __name__ == "__main__":
    unittest.main()
