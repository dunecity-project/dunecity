#!/usr/bin/env python3
"""Update current desktop release copy without changing mobile or release history."""
import argparse
from pathlib import Path
import re

VERSION = r"[0-9]+\.[0-9]+\.[0-9]+"


def verify_current_copy(html, version):
    # Fail on a new template wording rather than silently publish contradictory
    # desktop labels again. Mobile engine versions and historical prose are out
    # of this current-release scope.
    for paragraph in re.findall(r"<p\b[^>]*>[\s\S]*?</p>", html):
        if not any(marker in paragraph for marker in ('version-info', 'Current releases:', ' is current')):
            continue
        desktop = re.split(r"Android|iPhone|iPad|TestFlight", paragraph, maxsplit=1)[0]
        if any(found != version for found in re.findall(VERSION, desktop)):
            raise ValueError("Stale current desktop version in website copy")
    for href, label in re.findall(r'<a\b[^>]*href="([^"]+)"[^>]*>(.*?)</a>', html, re.S):
        if re.search(r'/DuneCity-[^/]+-(?:macOS|Windows|Linux)', href):
            if any(found != version for found in re.findall(VERSION, label)):
                raise ValueError("Stale desktop download button label")


def update(html, version):
    if not re.fullmatch(VERSION, version):
        raise ValueError("Expected numeric X.Y.Z version")
    # These are the two current download pages, never the history/changelog page.
    html = re.sub(r"/v" + VERSION + r"/DuneCity-" + VERSION + "-",
                  f"/v{version}/DuneCity-{version}-", html)
    html = re.sub(r"/v" + VERSION + r"/dunecity_" + VERSION + "_amd64.deb",
                  f"/v{version}/dunecity_{version}_amd64.deb", html)
    html = re.sub(r"releases/tag/v" + VERSION, f"releases/tag/v{version}", html)
    html = html.replace("Windows-x64.zip", "Windows-x64.exe")
    html = re.sub(r'(href="[^"]*Windows-x64\.exe"[^>]*>)Download ZIP<',
                  r'\1Download Installer<', html)
    # Explicit desktop labels only: Android/iOS engine versions are independent.
    prefixes = (r"Windows/Linux: ", r"Windows and Linux ", r"Windows, macOS and Linux ",
                r"Desktop: ", r"macOS: ", r"macOS ", r"Latest: ")
    for prefix in prefixes:
        html = re.sub("(" + prefix + ")" + VERSION, lambda m: m[1] + version, html)
    html = re.sub(r"(>Dune City )" + VERSION + r"(?=<| is current)",
                  lambda m: m[1] + version, html)
    html = re.sub(r"(>Download )" + VERSION + r"(?=(?: DMG)?</a>)",
                  lambda m: m[1] + version, html)
    html = re.sub(r'(softwareVersion": ")' + VERSION + '"',
                  lambda m: m[1] + version + '"', html)
    # Do not label the previous release's feature prose as new in this release.
    # Preserve an already-authored current paragraph; otherwise link to its notes.
    def news(match):
        if match[1] == version:
            return match[0]
        return (f'<p>New in {version}: <a href="https://github.com/dunecity-project/'
                f'dunecity/releases/tag/v{version}">Release notes and known issues</a>'
                ' · <a href="changelog.html">What\'s new in every release</a></p>')
    html = re.sub(r"<p>New in (" + VERSION + r")[\s\S]*?</p>", news, html)
    verify_current_copy(html, version)
    return html


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--website", type=Path, required=True)
    args = parser.parse_args()
    # Prepare both pages before writing either, including missing-file failures.
    pages = [(args.website / name) for name in ("index.html", "dune-city.html")]
    changed = [(path, update(path.read_text(), args.version)) for path in pages]
    for path, html in changed:
        path.write_text(html)


if __name__ == "__main__":
    main()
