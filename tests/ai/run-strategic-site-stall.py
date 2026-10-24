#!/usr/bin/env python3
"""Reproduce the unsiteable overdue Palace without relying on a saved planning timer.

The 1.0.852 4P DuneCity seed-9101 fixture is saved at minute 35. The probe restores the
already-overdue Palace condition explicitly, verifies the real map has no Palace site,
and requires the city to complete other construction instead of idling at its storage cap.
The older continuation-only check was not discriminating: disk saves rebuild planning
state, so it let the broken override start again without its overdue timer.
"""
import argparse
import gzip
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=root / 'build')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--style', help='Existing play style for a same-build comparison, e.g. strategic-sites-off')
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fixture-', dir=args.output_dir) as fixture_dir:
        save = Path(fixture_dir) / 'city.dls'
        with gzip.open(root / 'tests/ai/ffa-9101-palace-stall.dls.gz', 'rb') as source, save.open('wb') as dest:
            shutil.copyfileobj(source, dest)
        command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'),
                   '--build-dir', str(args.build_dir), '--output-dir', str(args.output_dir / 'continuation'),
                   '--repeatable', '--load-save', str(save), '--mod', 'dunecity',
                   '--strategic-site-probe', '--minutes', '41', '--wall-timeout', '900',
                   '--path-workers', '1', '--no-ai-telemetry']
        if args.style:
            command += ['--scenario-style', args.style]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True)
    print(result.stdout, end='')
    print(result.stderr, end='', file=sys.stderr)
    if result.returncode:
        return result.returncode
    runs = sorted((args.output_dir / 'continuation').glob('run-*'), key=lambda p: p.stat().st_mtime)
    log = runs[-1] / 'run.log' if runs else None
    lines = log.read_text(errors='replace').splitlines() if log and log.is_file() else []
    for line in lines:
        if 'STRATEGIC_SITE_' in line:
            print(line)
    if not any('STRATEGIC_SITE_PASS:' in line for line in lines):
        print('FAIL: no successful strategic siting probe result')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
