#!/usr/bin/env python3
"""Exact-save Research Lab audit/continuations on the committed cluster source.

--game-options is a frozen diagnostic INI: [Research Lab Save] SHA256 and
GzipBase64 keys. Existing immutable input transport carries the exact save to
each machine. Game rules, roster, seed and initial resources come only from it.
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
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, probe_binary, prune_runs, require_free_space, restore_fixture_revisions, scratch_dir  # noqa: E402

KEY = ('arm',)
FIELDS = KEY + ('sha256', 'wall_s', 'error')


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def append(path, fields, rows):
    exists = path.exists()
    with path.open('a', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        if not exists:
            writer.writeheader()
        writer.writerows(rows)


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
    out = scratch / arm
    out.mkdir(parents=True, exist_ok=True)
    packed = configparser.ConfigParser(interpolation=None)
    packed.read(args.game_options)
    data = gzip.decompress(base64.b64decode(packed['Research Lab Save']['GzipBase64'], validate=True))
    digest = hashlib.sha256(data).hexdigest()
    if digest != packed['Research Lab Save']['SHA256']:
        raise RuntimeError('Frozen save checksum mismatch')
    saved = out / 'notbuildingresearchlab.dls'
    saved.write_bytes(data)
    source_profile = out / 'fixture-profile'
    restore_fixture_revisions(source_profile, [saved])
    run = out / 'run'
    env = dict(os.environ, BALANCE_RESEARCH_LAB_CONTINUATION='1')
    if arm.startswith('audit'):
        env['BALANCE_RESEARCH_LAB_AUDIT'] = '1'
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
               '--prebuilt-probe', str(binary), '--output-dir', str(run), '--load-save', str(saved),
               '--save-profile', str(source_profile),
               '--mod', 'dunecity', '--minutes', str(args.minutes), '--wall-timeout', str(args.wall_timeout),
               '--path-workers', '1', '--capture-mib', str(args.capture_mib)]
    if arm != 'audit':
        command += ['--scenario-style', 'research-lab-progression-' + arm.removeprefix('audit-')]
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
        if 'RESEARCH_LAB_SAVE_' in line:
            marker, body = line[line.index('RESEARCH_LAB_SAVE_'):].split(':', 1)
            measurements.append({'marker': marker, 'data': json.loads(body) if body.strip().startswith('{') else body.strip()})
    (results / (arm + '-measurements.json')).write_text(json.dumps(measurements, indent=2) + '\n')
    passed = process.returncode == 0 and any('RESEARCH_LAB_SAVE_RESULT:' in line for line in lines)
    row = {'arm': arm, 'sha256': digest, 'wall_s': round(time.time() - started, 2),
           'error': '' if passed else 'rc=' + str(process.returncode)}
    append(results / 'games.csv', FIELDS, [row])
    finish_game(scratch, out, not passed)
    return row


def verify_saved_crime(args):
    """The real-save regression: crime alone blocked two solvent legal labs."""
    results = Path(args.results)
    captures = {arm: json.loads((results / (arm + '-measurements.json')).read_text())
                for arm in ('audit-off', 'audit-on', 'off', 'on')}
    for house in args.verify_houses:
        audits = [next(row['data'] for row in captures[arm]
                       if row['marker'] == 'RESEARCH_LAB_SAVE_AUDIT' and row['data']['house'] == house)
                  for arm in ('audit-off', 'audit-on')]
        old, new = audits
        if not (old['dangerous_crime'] > 0 and old['opening_fleet'] == 0
                and not old['investment_ready'] and new['investment_ready'] and new['lab_funded']):
            print('FAIL: house %d did not isolate the solvent crime-only research veto' % house)
            return False
        events = {}
        for arm in ('off', 'on'):
            events[arm] = [dict(field.split('=', 1) for field in row['data'].split() if '=' in field)
                           for row in captures[arm] if row['marker'] == 'RESEARCH_LAB_SAVE_EVENT']
            events[arm] = [row for row in events[arm] if int(row['house']) == house]
        if any(int(row['labs']) > 0 for row in events['off']):
            print('FAIL: legacy house %d did not reproduce its missing-lab refusal' % house)
            return False
        placed = any(int(row['labs']) > 0 for row in events['on'])
        studied = any(int(row['target']) == 2 and int(row['remaining']) == 7500 for row in events['on'])
        if not placed or not studied:
            print('FAIL: corrected house %d did not build a lab and start normal level-2 research' % house)
            return False
        print('RESEARCH_LAB_SAVED_CRIME_PASS: house=%d old_refusal=1 new_lab=1 normal_research=1' % house)
    return True


def report(args):
    rows = read(Path(args.results) / 'games.csv')
    failed = [row for row in rows if row['error']]
    print('Research Lab exact save: %d passed, %d failed' % (len(rows) - len(failed), len(failed)))
    for row in rows:
        print('%s: sha256=%s wall=%ss error=%s' % (row['arm'], row['sha256'], row['wall_s'], row['error'] or 'none'))
    return bool(failed) or bool(args.verify_houses and not verify_saved_crime(args))


def run(args):
    require_free_space()
    if args.repeatable:
        destination = Path(args.results)
        destination.mkdir(parents=True, exist_ok=True)
        prune_runs(destination, 'run-', 2)
        args.results = tempfile.mkdtemp(prefix='run-', dir=destination)
    with scratch_dir('research-lab-continuation', args.scratch) as scratch:
        binary = build_probe(args, scratch)
        done = {row['arm'] for row in read(Path(args.results) / 'games.csv') if not row['error']}
        for key in schedule(args):
            if key[0] not in done:
                play(args, scratch, binary, key)
    return report(args)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--results', required=True)
parser.add_argument('--game-options', help='Frozen save input; required for play, not shared probe compilation')
parser.add_argument('--arms', nargs='+', choices=('audit', 'audit-off', 'audit-on', 'off', 'on'),
                    default=['audit-off', 'audit-on', 'off', 'on'])
parser.add_argument('--minutes', type=int, default=12)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--capture-mib', type=int, default=1024)
parser.add_argument('--scratch', type=Path)
parser.add_argument('--rebuild', action='store_true')
parser.add_argument('--report', action='store_true')
parser.add_argument('--verify-houses', nargs='+', type=int, default=[],
                    help='Require exact crime-only refusal off and paid lab/normal research on for these houses')
parser.add_argument('--repeatable', action='store_true', help='Use a fresh retained CTest result directory')
args = parser.parse_args()
if args.report:
    raise SystemExit(report(args))
raise SystemExit(run(args))
