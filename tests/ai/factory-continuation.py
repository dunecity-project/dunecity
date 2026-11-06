#!/usr/bin/env python3
"""Audit/continue an exact private save through the committed cluster engine.

The frozen --game-options transport uses the existing [Research Lab Save]
SHA256/GzipBase64 input. No private save is committed to the repository.
"""
import argparse
import base64
import configparser
import csv
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, probe_binary, require_free_space, restore_fixture_revisions, scratch_dir  # noqa: E402

KEY = ('arm',)
FIELDS = KEY + ('sha256', 'wall_s', 'error')


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def schedule(args):
    return [(arm,) for arm in args.arms]


def build_probe(args, scratch):
    probe = scratch / 'probe'
    if not args.rebuild:
        try:
            binary = probe_binary(probe)
            if binary.exists():
                return binary
        except OSError:
            pass
    shutil.rmtree(probe, ignore_errors=True)
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
                    '--output-dir', str(probe), '--duel-map-size', '64', '--duel-map-seed', '1',
                    '--duel-sides', 'atreides:box,harkonnen:box', '--local-spectator', '--enemy-difficulty', 'hard',
                    '--partner-difficulty', 'hard', '--minutes', '1', '--capture-mib', '256'],
                   cwd=root, check=True, capture_output=True, text=True)
    shutil.rmtree(probe / 'profile', ignore_errors=True)
    return probe_binary(probe)


def play(args, scratch, binary, key):
    arm, = key
    if not args.game_options:
        raise ValueError('Playing an exact save requires the frozen --game-options input')
    packed = configparser.ConfigParser(interpolation=None)
    packed.read(args.game_options)
    data = gzip.decompress(base64.b64decode(packed['Research Lab Save']['GzipBase64'], validate=True))
    digest = hashlib.sha256(data).hexdigest()
    if digest != packed['Research Lab Save']['SHA256']:
        raise RuntimeError('Frozen save checksum mismatch')
    out = scratch / arm
    out.mkdir(parents=True, exist_ok=True)
    saved = out / 'factory-save.dls'
    saved.write_bytes(data)
    source_profile = out / 'fixture-profile'
    restore_fixture_revisions(source_profile, [saved])
    run = out / 'run'
    env = dict(os.environ, BALANCE_FACTORY_CONTINUATION='1')
    if arm.startswith('audit'):
        env['BALANCE_FACTORY_SAVE_AUDIT'] = '1'
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
               '--prebuilt-probe', str(binary), '--output-dir', str(run), '--load-save', str(saved),
               '--save-profile', str(source_profile), '--mod', 'dunecity', '--minutes', str(args.minutes),
               '--wall-timeout', str(args.wall_timeout), '--path-workers', '1', '--capture-mib', str(args.capture_mib)]
    state = arm[6:] if arm.startswith('audit-') else arm
    if args.style_prefix and state in ('off', 'on'):
        command += ['--scenario-style', args.style_prefix + '-' + state]
    started = time.time()
    process = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True)
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    log = run / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    if log.exists():
        shutil.copyfile(log, results / (arm + '.log'))
    (results / (arm + '-runner.log')).write_text(process.stdout + process.stderr)
    for events in (run / 'profile').rglob('events.jsonl'):
        with events.open('rb') as source, gzip.open(results / (arm + '-events.jsonl.gz'), 'wb') as dest:
            shutil.copyfileobj(source, dest)
    measurements = []
    for line in lines:
        if 'FACTORY_SAVE_' in line or 'RESEARCH_LAB_SAVE_HOUSE:' in line:
            marker_start = line.index('FACTORY_SAVE_') if 'FACTORY_SAVE_' in line else line.index('RESEARCH_LAB_SAVE_HOUSE:')
            marker, body = line[marker_start:].split(':', 1)
            measurements.append({'marker': marker, 'data': json.loads(body) if body.strip().startswith('{') else body.strip()})
    (results / (arm + '-measurements.json')).write_text(json.dumps(measurements, indent=2) + '\n')
    passed = process.returncode == 0 and any('FACTORY_SAVE_RESULT:' in line for line in lines)
    row = {'arm': arm, 'sha256': digest, 'wall_s': round(time.time() - started, 2), 'error': '' if passed else 'rc=' + str(process.returncode)}
    path = results / 'games.csv'
    exists = path.exists()
    with path.open('a', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        if not exists:
            writer.writeheader()
        writer.writerow(row)
    finish_game(scratch, out, not passed)
    return row


def report(args):
    games = read(Path(args.results) / 'games.csv')
    failed = [row for row in games if row['error']]
    print('Exact-save factory continuations: %d passed, %d failed' % (len(games) - len(failed), len(failed)))
    for row in games:
        print('%s: sha256=%s wall=%ss error=%s' % (row['arm'], row['sha256'], row['wall_s'], row['error'] or 'none'))
    return bool(failed)


def run(args):
    require_free_space()
    with scratch_dir('factory-continuation', args.scratch) as scratch:
        binary = build_probe(args, scratch)
        done = {row['arm'] for row in read(Path(args.results) / 'games.csv') if not row['error']}
        for key in schedule(args):
            if key[0] not in done:
                play(args, scratch, binary, key)
    return report(args)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--results', required=True)
parser.add_argument('--game-options', help='Frozen save input; required for play, not shared probe preparation')
parser.add_argument('--arms', nargs='+', choices=('audit', 'baseline', 'audit-off', 'audit-on', 'off', 'on'), default=['audit', 'baseline'])
parser.add_argument('--style-prefix', default='')
parser.add_argument('--minutes', type=int, default=15)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--capture-mib', type=int, default=512)
parser.add_argument('--scratch', type=Path)
parser.add_argument('--rebuild', action='store_true')
parser.add_argument('--report', action='store_true')
args = parser.parse_args()
sys.exit(report(args) if args.report else run(args))
