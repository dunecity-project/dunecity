#!/usr/bin/env python3
"""Check packaged SDL renderer inventory, normal startup and software recovery.

Run under a real display or xvfb-run. The binary's --check-video-runtime must use
the game's renderer initializer and verify a target-texture draw/copy/readback.
No game assets, audio hardware or player profile is needed.
"""
import argparse
import os
from pathlib import Path
import subprocess


def check_video_runtime(binary, required_drivers=(), require_accelerated=False):
    binary = Path(binary).resolve(strict=True)
    for label, video, renderer in (("default-backend", None, None),
                                   ("explicit-software", None, "software"),
                                   ("no-acceleration", "dummy", None)):
        environment = dict(os.environ)
        environment.pop("SDL_VIDEODRIVER", None)
        environment.pop("SDL_RENDER_DRIVER", None)
        if video is not None:
            environment["SDL_VIDEODRIVER"] = video
        if renderer is not None:
            environment["SDL_RENDER_DRIVER"] = renderer
        result = subprocess.run([str(binary), "--check-video-runtime"],
                                env=environment, capture_output=True, text=True,
                                timeout=30, check=False)
        output = result.stdout + result.stderr
        print(f"=== Video startup: {label} ===\n{output}", flush=True)
        if result.returncode != 0 or "Video runtime target rendering passed\n" not in output:
            raise RuntimeError(f"{label}: packaged video initialization failed ({result.returncode})")
        for required in required_drivers:
            if f"Render backend available: {required}\n" not in output:
                raise RuntimeError(f"Missing compiled render backend: {required}")
        if label == "default-backend" and require_accelerated:
            if "Render accelerated: yes\n" not in output:
                raise RuntimeError("Default startup did not select an accelerated SDL renderer")
        if label != "default-backend" and "Render driver: software\n" not in output:
            raise RuntimeError(f"{label}: did not select the software renderer")
        if label == "no-acceleration":
            if "Video driver: dummy\n" not in output:
                raise RuntimeError("No-acceleration check did not use the dummy video driver")
            if "continuing with software rendering" not in output:
                raise RuntimeError("Missing acceleration did not exercise software recovery")
    print("All three packaged video startup checks passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--require-driver", action="append", default=[])
    parser.add_argument("--require-accelerated", action="store_true")
    args = parser.parse_args()
    check_video_runtime(args.binary, args.require_driver, args.require_accelerated)


if __name__ == "__main__":
    main()
