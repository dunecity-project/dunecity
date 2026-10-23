#!/usr/bin/env python3
"""Check real SDL_mixer startup and failure recovery in a packaged executable.

No display/audio hardware, game assets or player profile is needed. The binary
must implement --check-audio-runtime using the same initializer as normal startup.
"""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--require-driver", action="append", default=[])
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    for label, driver in (("dummy", "dummy"),
                          ("failed-backend", "dunecity-nonexistent-audio-driver"),
                          ("default-backend", None)):
        environment = dict(os.environ)
        environment.pop("SDL_AUDIODRIVER", None)
        if driver is not None:
            environment["SDL_AUDIODRIVER"] = driver
        result = subprocess.run([str(binary), "--check-audio-runtime"],
                                env=environment, capture_output=True, text=True,
                                timeout=30, check=False)
        output = result.stdout + result.stderr
        print(f"=== Audio startup: {label} ===\n{output}", flush=True)
        if result.returncode != 0 or "Audio runtime initialization passed" not in output:
            raise RuntimeError(f"{label}: packaged audio initialization failed ({result.returncode})")
        for required in args.require_driver:
            if f"Audio backend available: {required}\n" not in output:
                raise RuntimeError(f"Missing compiled audio backend: {required}")
        if driver is not None and "Audio driver: dummy\n" not in output:
            raise RuntimeError(f"{label}: did not open the dummy mixer")
        if label == "failed-backend" and "Continuing with silent audio" not in output:
            raise RuntimeError("Forced backend failure did not exercise silent recovery")
    print("All three packaged audio startup checks passed")


if __name__ == "__main__":
    main()
