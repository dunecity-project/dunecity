import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "check_video_runtime", Path(__file__).parents[1] / "check-video-runtime.py")
VIDEO = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VIDEO)


class VideoRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.binary = Path(self.directory.name) / "dunecity"
        self.binary.touch()
        common = "Render backend available: opengl\nRender backend available: software\n"
        self.outputs = [
            common + "Video driver: x11\nRender driver: opengl\nRender accelerated: yes\n",
            common + "Video driver: x11\nRender driver: software\nRender accelerated: no\n",
            common + "Video driver: dummy\nRender driver: software\nRender accelerated: no\n"
            + "Accelerated renderer failed: unavailable; continuing with software rendering\n",
        ]
        self.outputs = [output + "Video runtime target rendering passed\n" for output in self.outputs]

    def run_check(self, outputs=None, status=0):
        results = [subprocess.CompletedProcess([], status, output, "")
                   for output in (self.outputs if outputs is None else outputs)]
        with patch.object(VIDEO.subprocess, "run", side_effect=results) as run:
            VIDEO.check_video_runtime(self.binary, ["opengl"], require_accelerated=True)
            return run.call_args_list

    def test_success_and_environment_isolation(self):
        with patch.dict(os.environ, {"SDL_RENDER_DRIVER": "software", "SDL_VIDEODRIVER": "dummy"}):
            calls = self.run_check()
        self.assertEqual(len(calls), 3)
        self.assertNotIn("SDL_RENDER_DRIVER", calls[0].kwargs["env"])
        self.assertNotIn("SDL_VIDEODRIVER", calls[0].kwargs["env"])
        self.assertEqual(calls[1].kwargs["env"]["SDL_RENDER_DRIVER"], "software")
        self.assertEqual(calls[2].kwargs["env"]["SDL_VIDEODRIVER"], "dummy")
        self.assertNotIn("SDL_RENDER_DRIVER", calls[2].kwargs["env"])
        self.assertEqual(calls[0].args[0], [str(self.binary), "--check-video-runtime"])

    def test_linux_ibus_declares_its_dbus_dependency(self):
        manifest = json.loads((Path(__file__).parents[2] / "vcpkg.json").read_text())
        sdl = next(dependency for dependency in manifest["dependencies"]
                   if isinstance(dependency, dict) and dependency.get("name") == "sdl2"
                   and dependency.get("platform") == "linux")
        features = sdl.get("features", [])
        if "ibus" in features and sdl.get("default-features") is False:
            self.assertIn("dbus", features, "IBus requires SDL's D-Bus context")

    def test_missing_backend_is_failure(self):
        with self.assertRaisesRegex(RuntimeError, "Missing compiled render backend"):
            self.run_check([output.replace("Render backend available: opengl\n", "")
                            for output in self.outputs])

    def test_software_cannot_hide_default_acceleration_failure(self):
        self.outputs[0] = self.outputs[0].replace("Render accelerated: yes", "Render accelerated: no")
        with self.assertRaisesRegex(RuntimeError, "did not select an accelerated"):
            self.run_check()

    def test_explicit_software_must_be_selected(self):
        self.outputs[1] = self.outputs[1].replace("Render driver: software", "Render driver: opengl")
        with self.assertRaisesRegex(RuntimeError, "did not select the software"):
            self.run_check()

    def test_dummy_must_exercise_fallback(self):
        self.outputs[2] = self.outputs[2].replace("continuing with software rendering", "")
        with self.assertRaisesRegex(RuntimeError, "did not exercise software recovery"):
            self.run_check()

    def test_dummy_video_is_required(self):
        self.outputs[2] = self.outputs[2].replace("Video driver: dummy", "Video driver: x11")
        with self.assertRaisesRegex(RuntimeError, "did not use the dummy"):
            self.run_check()

    def test_target_round_trip_marker_required(self):
        self.outputs[0] = self.outputs[0].replace("Video runtime target rendering passed\n", "")
        with self.assertRaisesRegex(RuntimeError, "initialization failed"):
            self.run_check()

    def test_nonzero_exit_is_failure_even_with_success_marker(self):
        with self.assertRaisesRegex(RuntimeError, "initialization failed"):
            self.run_check(status=1)


if __name__ == "__main__":
    unittest.main()
