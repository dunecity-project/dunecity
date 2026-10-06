#!/usr/bin/env python3
"""Publish one tested visual pack using an isolated Git index.

Never stages the operator's worktree, changes its branch, or force pushes.
The first commit contains art; the second pins the catalog to that art commit.
"""
from __future__ import annotations

import argparse
import configparser
import os
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image
from dune2_atlas_reference import load_helper


def binding(ini: configparser.ConfigParser) -> tuple[str, int, int]:
    for section in ("Building", "Unit", "Tile"):
        if section in ini:
            return (section, ini.getint(section, "TerrainType" if section == "Tile" else "ItemID"),
                    -1 if section == "Tile" else ini.getint(section, "HouseID", fallback=-1))
    raise ValueError("Visual manifest has no object identity")


def publish(repo: Path, pack: Path, *, remote="origin", branch="main", dry_run=True) -> str:
    repo, pack = repo.resolve(), pack.resolve()
    if not re.fullmatch(r"[a-z0-9_-]+", pack.name):
        raise ValueError("Unsafe pack identifier")
    manifests = [p for p in pack.glob("*.ini") if p.name in {"building.ini", "tile.ini", "unit.ini"}]
    if len(manifests) != 1:
        raise ValueError("One visual manifest is required")
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(manifests[0], encoding="utf-8-sig")
    identity = binding(ini)
    allowed = {manifests[0].name}
    for section in ini.sections():
        for key, value in ini[section].items():
            if key in {"atlas", "still", "image", "compact"} or key.startswith("atlas."):
                allowed.add(value)
    files = sorted(p for p in pack.rglob("*") if p.is_file())
    for path in files:
        relative = path.relative_to(pack).as_posix()
        if (path.is_symlink() or not path.resolve().is_relative_to(pack)
                or not re.fullmatch(r"[A-Za-z0-9_./-]+", relative)
                or relative not in allowed or path.suffix not in {".png", ".ini"}):
            raise ValueError(f"Not an approved visual asset: {relative}")
        if path.suffix == ".png":
            with Image.open(path) as image:
                if image.format != "PNG" or max(image.size) > 8192 or image.width * image.height > 16777216:
                    raise ValueError("Texture exceeds runtime limits")
                image.verify()
    if allowed != {p.relative_to(pack).as_posix() for p in files}:
        raise ValueError("Visual manifest references missing files")
    if dry_run:
        return f"Validated {len(files)} visual files; no Git changes or publication."
    # The production command is deliberately tied to this game's repository.
    url = subprocess.check_output(["git", "remote", "get-url", remote], cwd=repo, text=True).strip()
    if url.rstrip("/").removesuffix(".git").lower() not in {
            "https://github.com/dunecity-project/dunecity", "git@github.com:dunecity-project/dunecity",
            "https://github.com/vr48/dunecity", "git@github.com:vr48/dunecity"}:
        raise ValueError("Publishing remote is not the official Dune2R game repository")
    if branch != "main":
        raise ValueError("Only the main asset channel is configured")
    subprocess.run(["git", "fetch", remote, branch], cwd=repo, check=True, capture_output=True)
    parent = subprocess.check_output(["git", "rev-parse", "FETCH_HEAD"], cwd=repo, text=True).strip()
    prefix = f"mods/Dune2R/graphics_hd/units/{pack.name}/"
    existing = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", "-z", parent, "--", "mods/Dune2R/graphics_hd/units/"], cwd=repo
    ).decode("utf-8").split("\0")
    for name in existing:
        if Path(name).name not in {"building.ini", "unit.ini", "tile.ini"}:
            continue
        previous = configparser.ConfigParser(interpolation=None)
        previous.read_string(subprocess.check_output(["git", "show", f"{parent}:{name}"], cwd=repo).decode("utf-8-sig"))
        if name.startswith(prefix):
            if binding(previous) != identity:
                raise ValueError("Cannot repurpose an existing published pack for a different object or house")
        elif binding(previous) == identity:
            raise ValueError(f"Another published pack already owns this object and house: {Path(name).parent.name}")
    with tempfile.TemporaryDirectory(prefix="dune2r-publish-") as temporary:
        env = {**os.environ, "GIT_INDEX_FILE": str(Path(temporary) / "index")}
        def git(*args, data=None):
            return subprocess.run(["git", *args], cwd=repo, env=env, input=data,
                                  capture_output=True, check=True).stdout
        git("read-tree", parent)
        prior = git("ls-tree", "-r", "--name-only", "-z", parent, "--", prefix)
        if prior:
            git("update-index", "--force-remove", "-z", "--stdin", data=prior)
        for path in files:
            blob = git("hash-object", "-w", "--", str(path)).decode().strip()
            git("update-index", "--add", "--cacheinfo", f"100644,{blob},{prefix}{path.relative_to(pack).as_posix()}")
        tree = git("write-tree").decode().strip()
        art = git("commit-tree", tree, "-p", parent, "-m", f"Update Dune2R visual pack: {pack.name}").decode().strip()
        catalog = load_helper(repo / "scripts/generate-dune2r-asset-catalog.py").build_catalog(repo, art)
        blob = git("hash-object", "-w", "--stdin", data=catalog.encode("ascii")).decode().strip()
        git("update-index", "--add", "--cacheinfo", f"100644,{blob},mods/Dune2R/asset-catalog.ini")
        tree = git("write-tree").decode().strip()
        revision = git("commit-tree", tree, "-p", art, "-m",
                       f"Publish asset catalog for {pack.name}\n\n- Pin visual files to {art}\n- No gameplay or bot configuration changes").decode().strip()
        # A competing publication causes a normal non-fast-forward rejection.
        git("push", remote, f"{revision}:refs/heads/{branch}")
    return revision


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--publish", action="store_true")
    args = parser.parse_args()
    print(publish(args.repo, args.pack, dry_run=not args.publish))


if __name__ == "__main__":
    main()
