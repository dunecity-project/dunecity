#!/usr/bin/env python3
"""Exact-save land-value continuations (Stefan, 2026-11-27: "there's still lots of low value see
mba save low").

--game-options is a frozen diagnostic INI: [Land Value Save] SHA256 and GzipBase64 keys (the queue
carries its exact bytes to each machine). Each arm continues the save for --extra-minutes with
every QuantBot playing one style, logging LAND_VALUE_AUDIT rows (tests/ai/land-value-audit.inc) at
the load, every --every minutes and at the end: per house its R/C lot land value, what holds the low
lots down (distance, pollution, crime, parks, Palace), its road strips, credits and population.

Arms: off (shipped defaults), rule-off (match rule Parks Count As Roads off), park-districts,
road-lane-parks, lot-parks, pollution-relocation, redevelop-all (tests/ai/play-styles/<arm>.ini).
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

ARMS = ('off', 'rule-off', 'park-districts', 'road-lane-parks', 'lot-parks', 'pollution-relocation', 'redevelop-all')
KEY = ('arm',)
FIELDS = KEY + ('sha256', 'extra_minutes', 'wall_s', 'error')
AUDIT_FIELDS = ('arm', 'phase', 'minute', 'house', 'credits', 'avg_land_value', 'pop_r', 'pop_c', 'pop_i', 'lots',
                'lot_lv_mean', 'lv_le40', 'lv_41_80', 'lv_81_120', 'lv_gt120', 'low', 'low_pollution_mean',
                'low_polluted20', 'low_crime190', 'low_distance_term_mean', 'low_distance_le40', 'low_park_mean',
                'low_no_park', 'low_palace_mean', 'low_no_palace', 'low_polluter_tiles_mean_x10', 'roads',
                'roads_in_2x2', 'items')


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


def frozen_save(args):
    packed = configparser.ConfigParser(interpolation=None)
    packed.read(args.game_options)
    data = gzip.decompress(base64.b64decode(packed['Land Value Save']['GzipBase64'], validate=True))
    digest = hashlib.sha256(data).hexdigest()
    if digest != packed['Land Value Save']['SHA256']:
        raise RuntimeError('Frozen save checksum mismatch')
    return data, digest


def play(args, scratch, binary, key):
    arm, = key
    if not args.game_options:
        raise ValueError('Playing an exact save requires the frozen --game-options input')
    out = scratch / arm
    out.mkdir(parents=True, exist_ok=True)
    data, digest = frozen_save(args)
    saved = out / 'land-value.dls'
    saved.write_bytes(data)
    source_profile = out / 'fixture-profile'
    restore_fixture_revisions(source_profile, [saved])
    run = out / 'run'
    env = dict(os.environ, BALANCE_LAND_VALUE_AUDIT='1', BALANCE_LAND_VALUE_AUDIT_EVERY=str(args.every),
               BALANCE_LAND_VALUE_AUDIT_EXTRA=str(args.extra_minutes))
    if arm == 'rule-off':
        env['BALANCE_PARKS_COUNT_AS_ROADS'] = '0'
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
               '--prebuilt-probe', str(binary), '--output-dir', str(run), '--load-save', str(saved),
               '--save-profile', str(source_profile), '--mod', 'dunecity', '--minutes', '1440',
               '--wall-timeout', str(args.wall_timeout), '--path-workers', '1', '--capture-mib', str(args.capture_mib)]
    if arm not in ('off', 'rule-off'):
        command += ['--scenario-style', arm]
    started = time.time()
    process = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True)
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    log = run / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    (results / (arm + '-runner.log')).write_text(process.stdout[-20000:] + process.stderr[-20000:])
    for events in (run / 'profile').rglob('events.jsonl'):
        with events.open('rb') as source, gzip.open(results / (arm + '-events.jsonl.gz'), 'wb') as dest:
            shutil.copyfileobj(source, dest)
    rows = []
    for line in lines:
        if 'LAND_VALUE_AUDIT:' in line:
            row = json.loads(line.split('LAND_VALUE_AUDIT:', 1)[1])
            row['items'] = json.dumps(row['items'], sort_keys=True)
            rows.append({'arm': arm, **{f: row.get(f, '') for f in AUDIT_FIELDS if f != 'arm'}})
    if rows:
        append(results / 'audit.csv', AUDIT_FIELDS, rows)
    passed = process.returncode == 0 and any(r['phase'] == 'end' for r in rows)
    if not passed and log.exists():
        shutil.copyfile(log, results / (arm + '.log'))
    row = {'arm': arm, 'sha256': digest, 'extra_minutes': args.extra_minutes,
           'wall_s': round(time.time() - started, 2), 'error': '' if passed else 'rc=' + str(process.returncode)}
    append(results / 'games.csv', FIELDS, [row])
    finish_game(scratch, out, not passed)
    return row


def report(args):
    rows = read(Path(args.results) / 'games.csv')
    failed = [row for row in rows if row['error']]
    print('Land-value continuation: %d passed, %d failed' % (len(rows) - len(failed), len(failed)))
    audit = read(Path(args.results) / 'audit.csv')
    for phase in ('loaded', 'end'):
        for row in audit:
            if row['phase'] == phase and int(row['lots'] or 0) > 0:
                print('%-21s %-6s m%-6s h%s lots=%s lv=%s le40=%s 41-80=%s credits=%s pop=%s/%s/%s '
                      'roads=%s two-lane=%s' % (row['arm'], phase, row['minute'], row['house'], row['lots'],
                      row['lot_lv_mean'], row['lv_le40'], row['lv_41_80'], row['credits'], row['pop_r'],
                      row['pop_c'], row['pop_i'], row['roads'], row['roads_in_2x2']))
    return bool(failed)


def run(args):
    require_free_space()
    with scratch_dir('land-value-continuation', args.scratch) as scratch:
        binary = build_probe(args, scratch)
        done = {row['arm'] for row in read(Path(args.results) / 'games.csv') if not row['error']}
        for key in schedule(args):
            if key[0] not in done:
                play(args, scratch, binary, key)
    return report(args)


def freeze(save, output):
    data = Path(save).read_bytes()
    packed = configparser.ConfigParser(interpolation=None)
    packed['Land Value Save'] = {'SHA256': hashlib.sha256(data).hexdigest(),
                                 'GzipBase64': base64.b64encode(gzip.compress(data, mtime=0)).decode()}
    with open(output, 'w') as handle:
        packed.write(handle)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--results')
parser.add_argument('--game-options', help='Frozen save input; required for play, not shared probe compilation')
parser.add_argument('--freeze', nargs=2, metavar=('SAVE', 'INI'), help='Write the frozen --game-options INI for SAVE')
parser.add_argument('--arms', nargs='+', choices=ARMS, default=list(ARMS))
parser.add_argument('--extra-minutes', type=int, default=30)
parser.add_argument('--every', type=int, default=5)
parser.add_argument('--wall-timeout', type=int, default=5400)
parser.add_argument('--capture-mib', type=int, default=256)
parser.add_argument('--scratch', type=Path)
parser.add_argument('--rebuild', action='store_true')
parser.add_argument('--report', action='store_true')
args = parser.parse_args()
if args.freeze:
    freeze(*args.freeze)
    raise SystemExit(0)
if not args.results:
    parser.error('--results is required')
if args.report:
    raise SystemExit(report(args))
raise SystemExit(run(args))
