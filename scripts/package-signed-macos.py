#!/usr/bin/env python3
"""Sign, notarize and staple a portable installed app; emit ZIP and DMG.

The input app is copied before mutation. Credentials stay in the named Keychain
profile. An unsigned or unaccepted package is never reported as a final output.
"""
import argparse
import json
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def notarize(path, profile, keychain, evidence):
    result = subprocess.run(["xcrun", "notarytool", "submit", str(path), "--keychain-profile", profile,
                             "--keychain", keychain, "--wait", "--timeout", "20m", "--output-format", "json"],
                            capture_output=True, text=True)
    evidence.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError("Apple submission did not finish successfully: " + result.stderr.strip()[:500] + ". Inspect " + str(evidence))
    status = json.loads(result.stdout)
    if status.get("status") != "Accepted":
        if status.get("id"):
            subprocess.run(["xcrun", "notarytool", "log", status["id"], "--keychain-profile", profile,
                            "--keychain", keychain, str(evidence.with_name(evidence.stem + "-log.json"))], check=False)
        raise RuntimeError("Apple did not accept this package. Inspect " + str(evidence))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--identity", required=True)
    parser.add_argument("--signing-keychain", required=True)
    parser.add_argument("--notary-profile", required=True)
    parser.add_argument("--notary-keychain", required=True)
    args = parser.parse_args()
    if not args.app.is_dir(): raise ValueError("An installed app bundle is required")
    with (args.app / "Contents/Info.plist").open("rb") as f: version = plistlib.load(f)["CFBundleShortVersionString"]
    args.output.mkdir(parents=True, exist_ok=True)
    zip_path = args.output / f"DuneCity-{version}-macOS.zip"
    dmg_path = args.output / f"DuneCity-{version}-macOS.dmg"
    if zip_path.exists() or dmg_path.exists(): raise ValueError("Use a fresh output folder; existing packages will not be overwritten")
    with tempfile.TemporaryDirectory(prefix="dunecity-sign-", dir=args.output) as temp:
        root = Path(temp); stage = root / "volume"; stage.mkdir()
        app = stage / "dunecity.app"
        run("ditto", args.app, app)
        binaries = []
        for p in app.rglob("*"):
            if p.is_file() and not p.is_symlink():
                # Some bundled data produces non-UTF-8 descriptions from file(1).
                # Only the ASCII Mach-O marker is relevant to signing.
                kind = subprocess.check_output(["file", "-b", str(p)])
                if b"Mach-O" in kind:
                    linked = subprocess.check_output(["otool", "-L", str(p)], text=True).splitlines()
                    # Universal binaries have a separate unindented filename
                    # header for every architecture; those are not dependencies.
                    if any(line.startswith("\t") and line.strip().startswith(("/Users/", "/opt/homebrew/", "/usr/local/")) for line in linked):
                        raise ValueError("App contains non-portable libraries: " + str(p))
                    binaries.append(p)
        sign = ["codesign", "--force", "--sign", args.identity, "--keychain", args.signing_keychain,
                "--options", "runtime", "--timestamp", "--preserve-metadata=entitlements"]
        for p in sorted(binaries, key=lambda p: len(p.parts), reverse=True): run(*sign, p)
        bundles = [p for p in app.rglob("*") if p.is_dir() and not p.is_symlink() and p.suffix in (".app", ".xpc", ".framework")]
        for p in sorted(bundles, key=lambda p: len(p.parts), reverse=True): run(*sign, p)
        run(*sign, app)
        run("codesign", "--verify", "--deep", "--strict", app)
        upload = root / "submission.zip"
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, upload)
        notarize(upload, args.notary_profile, args.notary_keychain, args.output / "app-notarization.json")
        run("xcrun", "stapler", "staple", app)
        run("xcrun", "stapler", "validate", app)
        # First launch only after the ticket is written. Launching code Apple has
        # already notarized (a rerun of the same source) can put the bundle under
        # macOS App Management; the runner's stapler was then refused the write in
        # the third 1.0.868 attempt. Nothing below writes into the bundle.
        run(sys.executable, Path(__file__).with_name("verify-macos-runtime.py"), app)
        run("spctl", "--assess", "--type", "execute", "--verbose=2", app)
        # Both final containers carry the already stapled app.
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, zip_path)
        (stage / "Applications").symlink_to("/Applications")
        pending_dmg = root / dmg_path.name
        run("hdiutil", "create", "-volname", f"Dune City {version}", "-srcfolder", stage, "-format", "UDZO", pending_dmg)
        run("codesign", "--force", "--sign", args.identity, "--keychain", args.signing_keychain, "--timestamp", pending_dmg)
        notarize(pending_dmg, args.notary_profile, args.notary_keychain, args.output / "dmg-notarization.json")
        run("xcrun", "stapler", "staple", pending_dmg)
        run("xcrun", "stapler", "validate", pending_dmg)
        run("codesign", "--verify", "--strict", pending_dmg)
        run("spctl", "--assess", "--type", "open", "--context", "context:primary-signature", "--verbose=2", pending_dmg)
        pending_dmg.rename(dmg_path)
    print(f"Verified signed and notarized packages: {zip_path} and {dmg_path}")


if __name__ == "__main__": main()
