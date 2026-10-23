import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch


script = Path(__file__).resolve().parents[1] / "check-audio-runtime.py"
spec = importlib.util.spec_from_file_location("audio_runtime_check", script)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def result(recovery=False, driver="dummy", backends="alsa"):
    text = f"Audio backend available: {backends}\nAudio driver: {driver}\n"
    text += "Audio runtime initialization passed\n"
    if recovery:
        text += "Continuing with silent audio\n"
    return subprocess.CompletedProcess([], 0, text, "")


class AudioRuntimeCheckTests(unittest.TestCase):
    def invoke(self, results):
        with patch.object(sys, "argv", [str(script), "--binary", str(script),
                                        "--require-driver", "alsa"]), \
             patch.object(checker.subprocess, "run", side_effect=results) as run, \
             patch("builtins.print"):
            checker.main()
            return run.call_args_list

    def test_three_paths_use_separate_driver_environments(self):
        with patch.dict(checker.os.environ, {"SDL_AUDIODRIVER": "inherited"}):
            calls = self.invoke([result(), result(True), result(driver="alsa")])
        self.assertEqual(len(calls), 3)
        self.assertEqual(calls[0].kwargs["env"]["SDL_AUDIODRIVER"], "dummy")
        self.assertEqual(calls[1].kwargs["env"]["SDL_AUDIODRIVER"], "dunecity-nonexistent-audio-driver")
        self.assertNotIn("SDL_AUDIODRIVER", calls[2].kwargs["env"])

    def test_missing_backend_fails(self):
        with self.assertRaisesRegex(RuntimeError, "Missing compiled audio backend"):
            self.invoke([result(backends="dsp")])

    def test_missing_recovery_fails(self):
        with self.assertRaisesRegex(RuntimeError, "did not exercise silent recovery"):
            self.invoke([result(), result()])

    def test_real_device_is_not_accepted_for_forced_dummy(self):
        with self.assertRaisesRegex(RuntimeError, "did not open the dummy mixer"):
            self.invoke([result(driver="alsa")])

    def test_failure_exit_fails_even_with_success_text(self):
        bad = result()
        bad.returncode = 1
        with self.assertRaisesRegex(RuntimeError, "initialization failed"):
            self.invoke([bad])


if __name__ == "__main__":
    unittest.main()
