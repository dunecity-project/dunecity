#!/usr/bin/env python3
"""Complete a source-authenticated unsigned Mac ZIP with its normal CPack DMG.

Only the explicit producer marker authorizes a missing DMG. The ZIP is an input,
never rebuilt or changed; no CMake configure, compilation or signing is performed.
A new DMG reaches the artifact directory only after the normal verifier accepts it.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile


SOURCE_ROOT = Path(__file__).resolve().parents[1]


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_file(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"Missing or invalid package input: {path.name}")


def read_object(path):
    require_file(path)
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path.name}")
    return value


def validate_inputs(directory, source_sha, version):
    if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
        raise ValueError("Mac package reuse requires a full source SHA")
    metadata = read_object(directory / "build-inputs-macos-arm64.json")
    configuration = metadata.get("configuration")
    if (metadata.get("sourceCommit") != source_sha or metadata.get("releaseBuild") != "OFF"
            or not isinstance(configuration, dict)
            or configuration.get("DUNECITY_RELEASE_BUILD:BOOL") != "OFF"):
        raise ValueError("Reused Mac package source/OFF identity does not match")

    archive = directory / f"DuneCity-{version}-macOS.zip"
    dmg = archive.with_suffix(".dmg")
    if list(directory.glob("*-macOS.zip")) != [archive]:
        raise ValueError(f"Expected exactly {archive.name}")
    require_file(archive)
    existing_dmgs = list(directory.glob("*-macOS.dmg"))
    if existing_dmgs:
        if existing_dmgs != [dmg]:
            raise ValueError(f"Expected exactly {dmg.name}")
        require_file(dmg)

    digest = sha256_file(archive)
    marker = directory / "macos-dmg-deferred.json"
    if marker.exists() or marker.is_symlink():
        deferred = read_object(marker)
        expected = dict(schema=1, sourceCommit=source_sha, version=version,
                        zip=archive.name, sha256=digest)
        if type(deferred.get("schema")) is not int or deferred != expected:
            raise ValueError("Deferred DMG marker does not match the exact source/version/ZIP digest")
    elif not existing_dmgs:
        raise ValueError("Missing macOS DMG without a valid deferred marker")
    return archive, dmg, digest


def cpack_config(source_root, volume, output, version):
    # Same DragNDrop recipe and root documents as CMakeLists.txt. Only the app
    # input changes: consume the installed ZIP instead of a CMake install tree.
    values = {
        "CPACK_GENERATOR": "DragNDrop",
        "CPACK_PACKAGE_NAME": "DuneCity",
        "CPACK_PACKAGE_VENDOR": "Dune City Team",
        "CPACK_PACKAGE_DESCRIPTION_SUMMARY": "DuneCity built using CMake",
        "CPACK_PACKAGE_VERSION": version,
        "CPACK_PACKAGE_FILE_NAME": f"DuneCity-{version}-macOS",
        "CPACK_PACKAGE_DIRECTORY": str(output),
        "CPACK_RESOURCE_FILE_LICENSE": str(source_root / "COPYING"),
        "CPACK_RESOURCE_FILE_README": str(source_root / "README.md"),
        "CPACK_DMG_VOLUME_NAME": f"Dune City {version}",
        "CPACK_DMG_FORMAT": "UDZO",
        # CMakeLists.txt's pre-CMP0133 minimum retains the license agreement.
        "CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE": "ON",
        "CPACK_DMG_DISABLE_APPLICATIONS_SYMLINK": "OFF",
        "CPACK_MONOLITHIC_INSTALL": "ON",
        "CPACK_SET_DESTDIR": "OFF",
        "CPACK_STRIP_FILES": "OFF",
        "CPACK_INSTALLED_DIRECTORIES": f"{volume};/",
        "CPACK_INSTALL_CMAKE_PROJECTS": "",
    }
    for name, filename in (("CPACK_DMG_BACKGROUND_IMAGE", "dmg_background.png"),
                           ("CPACK_DMG_DS_STORE_SETUP_SCRIPT", "DMGSetup.scpt")):
        path = source_root / "packaging" / filename
        if path.exists():
            values[name] = str(path)
    lines = []
    for name, value in values.items():
        delimiter = "=="
        while f"]{delimiter}]" in value:
            delimiter += "="
        lines.append(f"set({name} [{delimiter}[{value}]{delimiter}])\n")
    return "".join(lines)


def complete_package(directory, source_sha, source_root=SOURCE_ROOT):
    directory = Path(directory).resolve()
    source_root = Path(source_root).resolve()
    match = re.search(r"^project\(DuneCity VERSION ([0-9]+\.[0-9]+\.[0-9]+)(?:\s|\))",
                      (source_root / "CMakeLists.txt").read_text(), re.MULTILINE)
    if not match:
        raise ValueError("Cannot read the numeric Mac package version from source")
    version = match.group(1)
    archive, dmg, digest = validate_inputs(directory, source_sha, version)
    verifier = source_root / "scripts" / "verify-macos-dmg.sh"
    if dmg.exists():
        subprocess.run(["bash", str(verifier), str(dmg)], cwd=source_root, check=True)
        if sha256_file(archive) != digest:
            raise ValueError("Mac ZIP changed during DMG verification")
        return dmg

    with tempfile.TemporaryDirectory(prefix="dunecity-staging-macos-", dir=os.environ.get("RUNNER_TEMP")) as temp:
        root = Path(temp)
        extracted = root / "extracted"
        subprocess.run(["ditto", "-x", "-k", str(archive), str(extracted)], cwd=source_root, check=True)
        app = extracted / "dunecity.app"
        if app.is_symlink() or not app.is_dir():
            raise ValueError("The exact Mac ZIP is missing dunecity.app")
        with (app / "Contents" / "Info.plist").open("rb") as stream:
            info = plistlib.load(stream)
        if (info.get("CFBundleShortVersionString") != version or info.get("CFBundleVersion") != version
                or info.get("CFBundleExecutable") != "dunecity"
                or info.get("CFBundleIdentifier") != "net.dunecity.DuneCity"):
            raise ValueError("The exact Mac ZIP app identity/version does not match source")

        volume = root / "volume"
        volume.mkdir()
        app.rename(volume / app.name)
        shutil.copy2(source_root / "README.md", volume / "README")
        notes = source_root / "release_notes" / "RELEASE_NOTES.md"
        if notes.is_file():
            shutil.copy2(notes, volume / "Release Notes.md")
        output = root / "packages"
        output.mkdir()
        config = root / "CPackConfig.cmake"
        config.write_text(cpack_config(source_root, volume, output, version))
        subprocess.run(["cpack", "--config", str(config), "-G", "DragNDrop", "-C", "Release", "-B", str(output)],
                       cwd=source_root, check=True)
        pending = output / dmg.name
        require_file(pending)
        subprocess.run(["bash", str(verifier), str(pending)], cwd=source_root, check=True)
        if sha256_file(archive) != digest:
            raise ValueError("Mac ZIP changed during DMG packaging")
        if dmg.exists():
            raise ValueError("Refusing to replace an existing Mac DMG")
        pending.rename(dmg)
    return dmg


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--source-root", type=Path, default=SOURCE_ROOT)
    args = parser.parse_args()
    dmg = complete_package(args.directory, args.source_sha, args.source_root)
    print(f"Verified staging Mac package set: {dmg.with_suffix('.zip')} and {dmg}")


if __name__ == "__main__":
    main()
