#!/usr/bin/env python3
"""Read-only verification of the generated APK against staging and source."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    output = root / "build-android-apk/app/build/outputs/apk/debug"
    apk = output / "DuneLegacy.apk"
    payload = root / "build-android-payload"
    version = json.loads((root / "android-version.json").read_text(encoding="utf-8"))
    metadata = json.loads((output / "output-metadata.json").read_text(encoding="utf-8"))
    require(metadata["applicationId"] == "net.dunecity.dune2r", "Application ID changed")
    require(len(metadata["elements"]) == 1, "Expected one universal ARM64 APK")
    element = metadata["elements"][0]
    require(element["versionName"] == version["versionName"], "APK version name mismatch")
    require(element["versionCode"] == version["versionCode"], "APK version code mismatch")
    digest = lambda data: hashlib.sha256(data).hexdigest()
    with zipfile.ZipFile(apk) as archive:
        require(archive.testzip() is None, "APK ZIP CRC failure")
        prefix = "assets/dune2r_payload/"
        names = {name for name in archive.namelist() if not name.endswith("/")}
        count = 0
        for path in payload.rglob("*"):
            if not path.is_file():
                continue
            relative = path.relative_to(payload).as_posix()
            # Android deliberately omits repository housekeeping, not game data.
            if relative in {"data/.gitignore", "mods/Dune2R/graphics_hd/objpics/.gitkeep"}:
                require(prefix + relative not in names, "Repository housekeeping was packaged")
                continue
            packaged = archive.read(prefix + relative)
            require(digest(packaged) == digest(path.read_bytes()), f"Staging mismatch: {relative}")
            source = root / relative
            require(source.is_file() and digest(packaged) == digest(source.read_bytes()),
                    f"Source mismatch: {relative}")
            count += 1
        require(count > 0, "Empty Android payload")
        require(sum(name.startswith(prefix) for name in names) == count, "Unexpected APK payload entries")
        for relative in ("data/Righteous-Regular.ttf", "data/Righteous-OFL.txt"):
            require(prefix + relative in names, f"Missing interface asset: {relative}")
        require(re.search(rb"UI Skin\s*=\s*2\b", archive.read(prefix + "config/Dune City.ini")),
                "Classic Desert default missing")
        for name in ("libmain.so", "libc++_shared.so"):
            binary = archive.read("lib/arm64-v8a/" + name)
            require(binary[:4] == b"\x7fELF" and int.from_bytes(binary[18:20], "little") == 183,
                    f"Incorrect native architecture: {name}")
            staged = root / "build-android-apk/app/src/main/jniLibs/arm64-v8a" / name
            require(digest(binary) == digest(staged.read_bytes()), f"Native staging mismatch: {name}")
        require(not any(name.startswith(prefix + "mods/Dune2R/graphics_hd/units/") for name in names),
                "Optional HD unit downloads were embedded")
    print(f"APK {version['versionName']} / {version['versionCode']}: {count} payload files match staging and source")
    print("ZIP CRC, application ID, ARM64 libraries, Classic default, font/license and optional-asset exclusion passed")
    print(f"APK bytes: {apk.stat().st_size}; SHA-256: {digest(apk.read_bytes())}")


if __name__ == "__main__":
    main()
